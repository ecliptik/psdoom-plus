/*
 * backend_freebsd.c - FreeBSD process table via sysctl(KERN_PROC)
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "backend_posix.h"

typedef struct fstate {
	psd_proc *procs;
	size_t cap;
	struct kinfo_proc *kp;
	size_t kplen;
	long pagekb;
} fstate;

static unsigned long long start_of(const struct kinfo_proc *k)
{
	return (unsigned long long)k->ki_start.tv_sec * 1000000ULL +
	       (unsigned long long)k->ki_start.tv_usec;
}

static void read_args(pid_t pid, psd_proc *p)
{
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ARGS, 0 };
	static char buf[16384];
	size_t len = sizeof buf;

	mib[3] = (int)pid;
	if (sysctl(mib, 4, buf, &len, NULL, 0) != 0)
		len = 0;
	psd_posix_set_argv(p, buf, len);
}

static int fbsd_snapshot(psd_backend *b, psd_proc **out, size_t *n)
{
	fstate *st = b->state;
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
	size_t len = 0, count, i, k = 0;

	/* the table can grow between the two calls: retry with headroom */
	for (;;) {
		if (sysctl(mib, 3, NULL, &len, NULL, 0) != 0)
			return PSD_ERR;
		len += len / 8 + sizeof(struct kinfo_proc) * 16;
		if (len > st->kplen) {
			void *np = realloc(st->kp, len);
			if (np == NULL)
				return PSD_ERR;
			st->kp = np;
			st->kplen = len;
		}
		len = st->kplen;
		if (sysctl(mib, 3, st->kp, &len, NULL, 0) == 0)
			break;
		if (errno != ENOMEM)
			return PSD_ERR;
	}
	count = len / sizeof(struct kinfo_proc);
	if (psd_posix_grow(&st->procs, &st->cap, count) != PSD_OK)
		return PSD_ERR;

	for (i = 0; i < count; i++) {
		const struct kinfo_proc *kp = &st->kp[i];
		psd_proc *p;

		if (kp->ki_stat == SZOMB)
			continue;
		p = &st->procs[k++];
		memset(p, 0, sizeof *p);
		p->pid = kp->ki_pid;
		p->ppid = kp->ki_ppid;
		p->uid = (long)kp->ki_uid;
		psd_strlcpy(p->user, psd_posix_user(p->uid), sizeof p->user);
		psd_strlcpy(p->comm, kp->ki_comm, sizeof p->comm);
		p->is_kernel = (kp->ki_flag & P_SYSTEM) != 0;
		p->is_daemon = kp->ki_tdev == (uint64_t)NODEV;
		p->nice = kp->ki_nice;
		p->start_id = start_of(kp);
		p->cpu_ticks = kp->ki_runtime;          /* microseconds */
		p->rss_kb = (unsigned long)kp->ki_rssize * (unsigned long)st->pagekb;
		if (p->is_kernel)
			snprintf(p->name, sizeof p->name, "[%s]", p->comm);
		else
			read_args(kp->ki_pid, p);
	}
	*out = st->procs;
	*n = k;
	return PSD_OK;
}

/* No pidfd here: re-read the process and compare its start time. */
static int same_process(const psd_proc *p)
{
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, 0 };
	struct kinfo_proc kp;
	size_t len = sizeof kp;

	mib[3] = (int)p->pid;
	if (sysctl(mib, 4, &kp, &len, NULL, 0) != 0 || len == 0)
		return 0;
	return p->start_id == 0 || start_of(&kp) == p->start_id;
}

static int fbsd_signal(psd_backend *b, const psd_proc *p, psd_signal sig, int verify)
{
	(void)b;
	if (verify && !same_process(p))
		return PSD_ERR_GONE;
	return psd_posix_kill(p->pid, sig);
}

static int fbsd_set_nice(psd_backend *b, const psd_proc *p, int nice)
{
	(void)b;
	return psd_posix_set_nice(p->pid, nice);
}

static void fbsd_close(psd_backend *b)
{
	fstate *st = b->state;

	if (st != NULL) {
		free(st->procs);
		free(st->kp);
	}
	free(st);
	b->state = NULL;
}

int psd_backend_native_open(psd_backend *b, const struct psd_config *cfg)
{
	fstate *st = calloc(1, sizeof *st);
	long pg = sysconf(_SC_PAGESIZE);
	(void)cfg;

	if (st == NULL)
		return PSD_ERR;
	st->pagekb = pg > 0 ? pg / 1024 : 4;
	memset(b, 0, sizeof *b);
	b->name = "freebsd";
	b->ticks_per_sec = 1000000;                     /* ki_runtime is in us */
	b->state = st;
	b->snapshot = fbsd_snapshot;
	b->signal = fbsd_signal;
	b->set_nice = fbsd_set_nice;
	b->self_pid = psd_posix_self;
	b->close = fbsd_close;
	return PSD_OK;
}
