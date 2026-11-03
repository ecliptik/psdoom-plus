/*
 * backend_openbsd.c - OpenBSD and NetBSD process tables via sysctl
 *
 * Both export a flat, versioned kinfo structure with p_* fields:
 * OpenBSD's struct kinfo_proc (KERN_PROC) and NetBSD's struct kinfo_proc2
 * (KERN_PROC2). The differences are a handful of names, handled below.
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "backend_posix.h"

#ifdef __NetBSD__
typedef struct kinfo_proc2 kinfo_t;
#define PSD_KERN_PROC   KERN_PROC2
#define PSD_NAME        "netbsd"
#define IS_ZOMBIE(k)    ((k)->p_stat == LSZOMB || (k)->p_stat == SZOMB)
#define IS_SYSTEM(k)    (((k)->p_flag & P_SYSTEM) != 0)
#else
typedef struct kinfo_proc kinfo_t;
#define PSD_KERN_PROC   KERN_PROC
#define PSD_NAME        "openbsd"
#define IS_ZOMBIE(k)    (((k)->p_psflags & PS_ZOMBIE) != 0 || (k)->p_stat == SDEAD)
#define IS_SYSTEM(k)    (((k)->p_flag & P_SYSTEM) != 0 || ((k)->p_psflags & PS_SYSTEM) != 0)
#endif

#ifndef NZERO
#define NZERO 20
#endif

typedef struct ostate {
	psd_proc *procs;
	size_t cap;
	kinfo_t *kp;
	size_t kplen;
	long pagekb;
} ostate;

static unsigned long long start_of(const kinfo_t *k)
{
	return (unsigned long long)k->p_ustart_sec * 1000000ULL +
	       (unsigned long long)k->p_ustart_usec;
}

static void read_args(pid_t pid, psd_proc *p)
{
	int mib[4] = { CTL_KERN, KERN_PROC_ARGS, 0, KERN_PROC_ARGV };
	static char buf[65536];      /* OpenBSD fails with ENOMEM rather than truncate */
	size_t len = sizeof buf;

	mib[2] = (int)pid;
	if (sysctl(mib, 4, buf, &len, NULL, 0) != 0) {
		psd_posix_set_argv(p, "", 0);
		return;
	}
#ifdef __OpenBSD__
	/* OpenBSD returns a NULL-terminated char *argv[] followed by the strings */
	{
		char **argv = (char **)(void *)buf;
		char line[PSD_CMDLINE_MAX];
		size_t used = 0;
		int i;

		if (argv[0] == NULL) {
			psd_posix_set_argv(p, "", 0);
			return;
		}
		psd_strlcpy(p->name, argv[0], sizeof p->name);
		line[0] = '\0';
		for (i = 0; argv[i] != NULL && used + 1 < sizeof line; i++) {
			int w = snprintf(line + used, sizeof line - used, "%s%s",
			                 i ? " " : "", argv[i]);
			if (w < 0)
				break;
			used += (size_t)w;
		}
		psd_strlcpy(p->cmdline, line, sizeof p->cmdline);
	}
#else
	/* NetBSD returns the strings NUL-separated */
	psd_posix_set_argv(p, buf, len);
#endif
}

static int o_snapshot(psd_backend *b, psd_proc **out, size_t *n)
{
	ostate *st = b->state;
	int mib[6] = { CTL_KERN, PSD_KERN_PROC, KERN_PROC_ALL, 0, sizeof(kinfo_t), 0 };
	size_t len = 0, count, i, k = 0;

	for (;;) {
		mib[5] = 0;
		if (sysctl(mib, 6, NULL, &len, NULL, 0) != 0)
			return PSD_ERR;
		len += len / 8 + sizeof(kinfo_t) * 16;
		if (len > st->kplen) {
			void *np = realloc(st->kp, len);
			if (np == NULL)
				return PSD_ERR;
			st->kp = np;
			st->kplen = len;
		}
		len = st->kplen;
		mib[5] = (int)(len / sizeof(kinfo_t));
		if (sysctl(mib, 6, st->kp, &len, NULL, 0) == 0)
			break;
		if (errno != ENOMEM)
			return PSD_ERR;
	}
	count = len / sizeof(kinfo_t);
	if (psd_posix_grow(&st->procs, &st->cap, count) != PSD_OK)
		return PSD_ERR;

	for (i = 0; i < count; i++) {
		const kinfo_t *kp = &st->kp[i];
		psd_proc *p;

		if (IS_ZOMBIE(kp))
			continue;
		p = &st->procs[k++];
		memset(p, 0, sizeof *p);
		p->pid = kp->p_pid;
		p->ppid = kp->p_ppid;
		p->uid = (long)kp->p_uid;
		psd_strlcpy(p->user, psd_posix_user(p->uid), sizeof p->user);
		psd_strlcpy(p->comm, kp->p_comm, sizeof p->comm);
		p->is_kernel = IS_SYSTEM(kp) || kp->p_pid == 0;
		p->is_daemon = kp->p_tdev == (uint32_t)NODEV;
		p->nice = (int)kp->p_nice - NZERO;
		p->start_id = start_of(kp);
		p->cpu_ticks = (unsigned long long)kp->p_rtime_sec * 1000000ULL + kp->p_rtime_usec;
		p->rss_kb = (unsigned long)kp->p_vm_rssize * (unsigned long)st->pagekb;
		if (p->is_kernel)
			snprintf(p->name, sizeof p->name, "[%s]", p->comm);
		else
			read_args(kp->p_pid, p);
	}
	*out = st->procs;
	*n = k;
	return PSD_OK;
}

/* No pidfd here: re-read the process and compare its start time. */
static int same_process(const psd_proc *p)
{
	int mib[6] = { CTL_KERN, PSD_KERN_PROC, KERN_PROC_PID, 0, sizeof(kinfo_t), 1 };
	kinfo_t kp;
	size_t len = sizeof kp;

	mib[3] = (int)p->pid;
	if (sysctl(mib, 6, &kp, &len, NULL, 0) != 0 || len == 0)
		return 0;
	return p->start_id == 0 || start_of(&kp) == p->start_id;
}

static int o_signal(psd_backend *b, const psd_proc *p, psd_signal sig, int verify)
{
	(void)b;
	if (verify && !same_process(p))
		return PSD_ERR_GONE;
	return psd_posix_kill(p->pid, sig);
}

static int o_set_nice(psd_backend *b, const psd_proc *p, int nice)
{
	(void)b;
	return psd_posix_set_nice(p->pid, nice);
}

static void o_close(psd_backend *b)
{
	ostate *st = b->state;

	if (st != NULL) {
		free(st->procs);
		free(st->kp);
	}
	free(st);
	b->state = NULL;
}

int psd_backend_native_open(psd_backend *b, const struct psd_config *cfg)
{
	ostate *st = calloc(1, sizeof *st);
	long pg = sysconf(_SC_PAGESIZE);
	(void)cfg;

	if (st == NULL)
		return PSD_ERR;
	st->pagekb = pg > 0 ? pg / 1024 : 4;
	memset(b, 0, sizeof *b);
	b->name = PSD_NAME;
	b->ticks_per_sec = 1000000;                     /* p_rtime in us */
	b->state = st;
	b->snapshot = o_snapshot;
	b->signal = o_signal;
	b->set_nice = o_set_nice;
	b->self_pid = psd_posix_self;
	b->close = o_close;
	return PSD_OK;
}
