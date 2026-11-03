/*
 * backend_haiku.c - Haiku teams as processes
 *
 * A Haiku team is a process; its id is the pid kill() takes. There are no
 * terminals to tell daemons apart, so servers started by launch_daemon (and
 * the kernel's own children) count as daemons.
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <OS.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "backend_posix.h"

typedef struct hstate {
	psd_proc *procs;
	size_t cap;
} hstate;

static const psd_proc *find(const psd_proc *ps, size_t n, long pid)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (ps[i].pid == pid)
			return &ps[i];
	return NULL;
}

static int haiku_snapshot(psd_backend *b, psd_proc **out, size_t *n)
{
	hstate *st = b->state;
	team_info ti;
	int32 cookie = 0;
	size_t k = 0, i;

	while (get_next_team_info(&cookie, &ti) == B_OK) {
		team_usage_info usage;
		psd_proc *p;

		if (psd_posix_grow(&st->procs, &st->cap, k + 1) != PSD_OK)
			return PSD_ERR;
		p = &st->procs[k++];
		memset(p, 0, sizeof *p);
		p->pid = ti.team;
		p->ppid = ti.parent;
		p->uid = (long)ti.uid;
		psd_strlcpy(p->user, psd_posix_user(p->uid), sizeof p->user);
		psd_strlcpy(p->comm, ti.name, sizeof p->comm);
		p->is_kernel = ti.team == B_SYSTEM_TEAM;
		p->start_id = (unsigned long long)ti.start_time;
		if (get_team_usage_info(ti.team, B_TEAM_USAGE_SELF, &usage) == B_OK)
			p->cpu_ticks = (unsigned long long)(usage.user_time + usage.kernel_time);
		/* args holds the command line (truncated to 64 bytes) */
		if (ti.args[0] != '\0') {
			char *sp;
			psd_strlcpy(p->cmdline, ti.args, sizeof p->cmdline);
			psd_strlcpy(p->name, ti.args, sizeof p->name);
			if ((sp = strchr(p->name, ' ')) != NULL)
				*sp = '\0';
		} else {
			snprintf(p->name, sizeof p->name, "[%s]", p->comm);
		}
		p->nice = 0;
	}

	/* daemons: children of the kernel team or of launch_daemon */
	for (i = 0; i < k; i++) {
		const psd_proc *parent = find(st->procs, k, st->procs[i].ppid);
		st->procs[i].is_daemon = st->procs[i].ppid == B_SYSTEM_TEAM ||
			(parent != NULL && strcmp(parent->comm, "launch_daemon") == 0);
	}
	*out = st->procs;
	*n = k;
	return PSD_OK;
}

static int same_process(const psd_proc *p)
{
	team_info ti;

	if (get_team_info((team_id)p->pid, &ti) != B_OK)
		return 0;
	return p->start_id == 0 || (unsigned long long)ti.start_time == p->start_id;
}

static int haiku_signal(psd_backend *b, const psd_proc *p, psd_signal sig, int verify)
{
	(void)b;
	if (verify && !same_process(p))
		return PSD_ERR_GONE;
	return psd_posix_kill(p->pid, sig);
}

/*
 * Haiku schedules threads, not teams: map nice (0..19) onto each thread's
 * priority, from B_NORMAL_PRIORITY (10) down to B_LOWEST_ACTIVE_PRIORITY (1).
 */
static int haiku_set_nice(psd_backend *b, const psd_proc *p, int nice)
{
	thread_info th;
	int32 cookie = 0;
	int32 prio = B_NORMAL_PRIORITY - (nice * (B_NORMAL_PRIORITY - B_LOWEST_ACTIVE_PRIORITY)) / 19;
	int any = 0;
	(void)b;

	/* (setpriority() exists but is a no-op that reports success) */
	while (get_next_thread_info((team_id)p->pid, &cookie, &th) == B_OK) {
		if (th.priority > prio && set_thread_priority(th.thread, prio) >= 0)
			any = 1;
	}
	if (any)
		return PSD_OK;
	errno = EPERM;
	return same_process(p) ? PSD_ERR : PSD_ERR_GONE;
}

static void haiku_close(psd_backend *b)
{
	hstate *st = b->state;

	if (st != NULL)
		free(st->procs);
	free(st);
	b->state = NULL;
}

int psd_backend_native_open(psd_backend *b, const struct psd_config *cfg)
{
	hstate *st = calloc(1, sizeof *st);
	(void)cfg;

	if (st == NULL)
		return PSD_ERR;
	memset(b, 0, sizeof *b);
	b->name = "haiku";
	b->ticks_per_sec = 1000000;                     /* bigtime_t is in us */
	b->state = st;
	b->snapshot = haiku_snapshot;
	b->signal = haiku_signal;
	b->set_nice = haiku_set_nice;
	b->self_pid = psd_posix_self;
	b->close = haiku_close;
	return PSD_OK;
}
