/*
 * backend_mock.c - an in-memory process table for tests
 *
 * Nothing here touches the real system, so the core logic can be tested on
 * any machine, including ones we must not kill processes on.
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "psd_internal.h"

#define MOCK_MAX 256

typedef struct mproc {
	psd_proc p;
	int flags;
	int alive;
	int last_signal;
	int nice;
} mproc;

static mproc table[MOCK_MAX];
static int ntable;
static long self_pid = -1;
static long long clock_ms = 1000;
static int signal_count;
static psd_proc snap[MOCK_MAX];

void psd_mock_reset(void)
{
	memset(table, 0, sizeof table);
	ntable = 0;
	self_pid = -1;
	clock_ms = 1000;
	signal_count = 0;
}

void psd_mock_set_self(long pid) { self_pid = pid; }
void psd_mock_advance_ms(long ms) { clock_ms += ms; }
int psd_mock_signal_count(void) { return signal_count; }

static mproc *find(long pid)
{
	int i;

	for (i = 0; i < ntable; i++)
		if (table[i].alive && table[i].p.pid == pid)
			return &table[i];
	return NULL;
}

/* includes dead entries, for last_signal after a kill */
static mproc *find_any(long pid)
{
	int i;

	for (i = ntable - 1; i >= 0; i--)
		if (table[i].p.pid == pid)
			return &table[i];
	return NULL;
}

void psd_mock_add(long pid, long ppid, long uid, const char *user,
                  const char *name, unsigned long long start_id,
                  unsigned long long cpu_ticks, int flags)
{
	mproc *m;

	if (ntable >= MOCK_MAX)
		return;
	m = &table[ntable++];
	memset(m, 0, sizeof *m);
	m->p.pid = pid;
	m->p.ppid = ppid;
	m->p.uid = uid;
	psd_strlcpy(m->p.user, user, sizeof m->p.user);
	psd_strlcpy(m->p.name, name, sizeof m->p.name);
	psd_strlcpy(m->p.comm, psd_basename(name), sizeof m->p.comm);
	psd_strlcpy(m->p.cmdline, name, sizeof m->p.cmdline);
	m->p.is_daemon = (flags & PSD_MOCK_DAEMON) != 0;
	m->p.is_kernel = (flags & PSD_MOCK_KERNEL) != 0;
	m->p.start_id = start_id;
	m->p.cpu_ticks = cpu_ticks;
	m->flags = flags;
	m->alive = 1;
	m->nice = 0;
}

void psd_mock_remove(long pid)
{
	mproc *m = find(pid);

	if (m != NULL)
		m->alive = 0;
}

void psd_mock_set_cpu(long pid, unsigned long long cpu_ticks)
{
	mproc *m = find(pid);

	if (m != NULL)
		m->p.cpu_ticks = cpu_ticks;
}

int psd_mock_last_signal(long pid)
{
	mproc *m = find_any(pid);

	return m ? m->last_signal : 0;
}

int psd_mock_nice(long pid)
{
	mproc *m = find_any(pid);

	return m ? m->nice : -100;
}

static int mock_snapshot(psd_backend *b, psd_proc **out, size_t *n)
{
	int i, k = 0;
	(void)b;

	for (i = 0; i < ntable; i++) {
		if (!table[i].alive)
			continue;
		snap[k] = table[i].p;
		snap[k].nice = table[i].nice;
		k++;
	}
	*out = snap;
	*n = (size_t)k;
	return PSD_OK;
}

static int mock_signal(psd_backend *b, const psd_proc *p, psd_signal sig, int verify)
{
	mproc *m = find(p->pid);
	(void)b;

	if (m == NULL || (verify && p->start_id && m->p.start_id != p->start_id))
		return PSD_ERR_GONE;
	signal_count++;
	m->last_signal = (int)sig;
	if (m->flags & PSD_MOCK_IMMORTAL) {
		errno = EPERM;
		return PSD_ERR;
	}
	if (sig == PSD_SIG_KILL || (sig == PSD_SIG_TERM && !(m->flags & PSD_MOCK_IGNORE_TERM)))
		m->alive = 0;
	return PSD_OK;
}

static int mock_set_nice(psd_backend *b, const psd_proc *p, int nice)
{
	mproc *m = find(p->pid);
	(void)b;

	if (m == NULL)
		return PSD_ERR_GONE;
	if (m->flags & PSD_MOCK_IMMORTAL) {
		errno = EPERM;
		return PSD_ERR;
	}
	m->nice = nice;
	return PSD_OK;
}

static long mock_self(psd_backend *b) { (void)b; return self_pid; }
static long long mock_now(psd_backend *b) { (void)b; return clock_ms; }

int psd_backend_mock_open(psd_backend *b, const struct psd_config *cfg)
{
	(void)cfg;
	memset(b, 0, sizeof *b);
	b->name = "mock";
	b->ticks_per_sec = 100;
	b->snapshot = mock_snapshot;
	b->signal = mock_signal;
	b->set_nice = mock_set_nice;
	b->self_pid = mock_self;
	b->now_ms = mock_now;
	return PSD_OK;
}
