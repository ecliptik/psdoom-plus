/*
 * backend_linux.c - Linux process table from /proc
 *
 * psDooM ran `ps h a x u OT` and `kill -9`; we read /proc and call
 * kill(2)/setpriority(2) directly. Modern mode signals through a pidfd after
 * re-checking the start time, so a recycled pid is never hit.
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "psd_internal.h"

#define PF_KTHREAD 0x00200000

typedef struct uidname {
	long uid;
	char name[PSD_USER_MAX];
} uidname;

typedef struct lstate {
	psd_proc *procs;
	size_t cap;
	uidname users[64];
	int nusers;
	long pagekb;
} lstate;

static const char *user_of(lstate *st, long uid)
{
	struct passwd *pw;
	int i;

	for (i = 0; i < st->nusers; i++)
		if (st->users[i].uid == uid)
			return st->users[i].name;
	i = st->nusers < 64 ? st->nusers++ : 63;
	st->users[i].uid = uid;
	pw = getpwuid((uid_t)uid);
	if (pw != NULL)
		psd_strlcpy(st->users[i].name, pw->pw_name, PSD_USER_MAX);
	else
		snprintf(st->users[i].name, PSD_USER_MAX, "%ld", uid);
	return st->users[i].name;
}

static ssize_t read_file(const char *path, char *buf, size_t len)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = read(fd, buf, len - 1);
	close(fd);
	if (n < 0)
		return -1;
	buf[n] = '\0';
	return n;
}

/*
 * /proc/<pid>/stat: pid (comm) state ppid pgrp session tty_nr tpgid flags
 * minflt cminflt majflt cmajflt utime stime cutime cstime priority nice
 * num_threads itrealvalue starttime vsize rss ...
 * comm may contain spaces and ')' so parse from the last ')'.
 */
typedef struct statf {
	char comm[PSD_COMM_MAX];
	char state;
	long ppid;
	long tty_nr;
	unsigned long flags;
	unsigned long long utime, stime, starttime;
	long nice;
	long rss_pages;
} statf;

static int read_stat(long pid, statf *sf)
{
	char path[64], buf[1024];
	char *open_paren, *close_paren, *s;
	size_t clen;
	int fields;

	snprintf(path, sizeof path, "/proc/%ld/stat", pid);
	if (read_file(path, buf, sizeof buf) <= 0)
		return -1;
	open_paren = strchr(buf, '(');
	close_paren = strrchr(buf, ')');
	if (open_paren == NULL || close_paren == NULL || close_paren < open_paren)
		return -1;
	clen = (size_t)(close_paren - open_paren - 1);
	if (clen >= sizeof sf->comm)
		clen = sizeof sf->comm - 1;
	memcpy(sf->comm, open_paren + 1, clen);
	sf->comm[clen] = '\0';

	s = close_paren + 2;
	fields = sscanf(s,
		"%c %ld %*s %*s %ld %*s %lu %*s %*s %*s %*s %llu %llu %*s %*s %*s %ld %*s %*s %llu %*s %ld",
		&sf->state, &sf->ppid, &sf->tty_nr, &sf->flags,
		&sf->utime, &sf->stime, &sf->nice, &sf->starttime, &sf->rss_pages);
	return fields == 9 ? 0 : -1;
}

static void read_cmdline(long pid, psd_proc *p)
{
	char path[64], buf[PSD_CMDLINE_MAX];
	ssize_t n, i;

	snprintf(path, sizeof path, "/proc/%ld/cmdline", pid);
	n = read_file(path, buf, sizeof buf);
	if (n <= 0) {
		/* kernel threads have no argv: ps shows [comm] */
		snprintf(p->name, sizeof p->name, "[%s]", p->comm);
		p->cmdline[0] = '\0';
		return;
	}
	psd_strlcpy(p->name, buf, sizeof p->name);           /* argv[0] */
	for (i = 0; i < n; i++)
		if (buf[i] == '\0')
			buf[i] = ' ';
	while (n > 0 && buf[n - 1] == ' ')
		buf[--n] = '\0';
	psd_strlcpy(p->cmdline, buf, sizeof p->cmdline);
}

static int linux_snapshot(psd_backend *b, psd_proc **out, size_t *n)
{
	lstate *st = b->state;
	DIR *d = opendir("/proc");
	struct dirent *de;
	size_t count = 0;

	if (d == NULL)
		return PSD_ERR;
	while ((de = readdir(d)) != NULL) {
		char path[64];
		struct stat sb;
		statf sf;
		psd_proc *p;
		long pid;

		if (!isdigit((unsigned char)de->d_name[0]))
			continue;
		pid = strtol(de->d_name, NULL, 10);
		if (read_stat(pid, &sf) != 0)
			continue;            /* exited meanwhile */
		if (sf.state == 'Z' || sf.state == 'X')
			continue;            /* zombies are not running processes */
		snprintf(path, sizeof path, "/proc/%ld", pid);
		if (stat(path, &sb) != 0)
			continue;

		if (count == st->cap) {
			size_t cap = st->cap ? st->cap * 2 : 256;
			psd_proc *np = realloc(st->procs, cap * sizeof *np);
			if (np == NULL)
				break;
			st->procs = np;
			st->cap = cap;
		}
		p = &st->procs[count++];
		memset(p, 0, sizeof *p);
		p->pid = pid;
		p->ppid = sf.ppid;
		p->uid = (long)sb.st_uid;
		psd_strlcpy(p->user, user_of(st, p->uid), sizeof p->user);
		psd_strlcpy(p->comm, sf.comm, sizeof p->comm);
		read_cmdline(pid, p);
		p->is_daemon = sf.tty_nr == 0;
		p->is_kernel = (sf.flags & PF_KTHREAD) != 0 || pid == 2 || sf.ppid == 2;
		p->nice = (int)sf.nice;
		p->start_id = sf.starttime;
		p->cpu_ticks = sf.utime + sf.stime;
		p->rss_kb = (unsigned long)(sf.rss_pages * st->pagekb);
	}
	closedir(d);
	*out = st->procs;
	*n = count;
	return PSD_OK;
}

static int sig_native(psd_signal sig)
{
	switch (sig) {
	case PSD_SIG_TERM: return SIGTERM;
	case PSD_SIG_KILL: return SIGKILL;
	case PSD_SIG_HUP:  return SIGHUP;
	case PSD_SIG_STOP: return SIGSTOP;
	case PSD_SIG_CONT: return SIGCONT;
	}
	return SIGTERM;
}

static int same_process(const psd_proc *p)
{
	statf sf;

	if (read_stat(p->pid, &sf) != 0)
		return 0;
	return p->start_id == 0 || sf.starttime == p->start_id;
}

static int linux_signal(psd_backend *b, const psd_proc *p, psd_signal sig, int verify)
{
	int s = sig_native(sig);
	(void)b;

	if (!verify)
		return kill((pid_t)p->pid, s) == 0 ? PSD_OK : (errno == ESRCH ? PSD_ERR_GONE : PSD_ERR);

#if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
	{
		int fd = (int)syscall(SYS_pidfd_open, (pid_t)p->pid, 0);
		if (fd >= 0) {
			int rc;
			/* the pidfd pins the process: if it still matches now, it is ours */
			if (!same_process(p)) {
				close(fd);
				return PSD_ERR_GONE;
			}
			rc = (int)syscall(SYS_pidfd_send_signal, fd, s, NULL, 0);
			{
				int e = errno;
				close(fd);
				errno = e;
			}
			return rc == 0 ? PSD_OK : (errno == ESRCH ? PSD_ERR_GONE : PSD_ERR);
		}
		if (errno == ESRCH)
			return PSD_ERR_GONE;
		/* ENOSYS (old kernel): fall through to check-then-kill */
	}
#endif
	if (!same_process(p))
		return PSD_ERR_GONE;
	return kill((pid_t)p->pid, s) == 0 ? PSD_OK : (errno == ESRCH ? PSD_ERR_GONE : PSD_ERR);
}

static int linux_set_nice(psd_backend *b, const psd_proc *p, int nice)
{
	(void)b;
	errno = 0;
	if (setpriority(PRIO_PROCESS, (id_t)p->pid, nice) == 0)
		return PSD_OK;
	return errno == ESRCH ? PSD_ERR_GONE : PSD_ERR;
}

static long linux_self(psd_backend *b)
{
	(void)b;
	return (long)getpid();
}

static void linux_close(psd_backend *b)
{
	lstate *st = b->state;

	if (st != NULL)
		free(st->procs);
	free(st);
	b->state = NULL;
}

int psd_backend_native_open(psd_backend *b, const struct psd_config *cfg)
{
	lstate *st = calloc(1, sizeof *st);
	long tck = sysconf(_SC_CLK_TCK);
	long pg = sysconf(_SC_PAGESIZE);
	(void)cfg;

	if (st == NULL)
		return PSD_ERR;
	st->pagekb = pg > 0 ? pg / 1024 : 4;
	memset(b, 0, sizeof *b);
	b->name = "linux";
	b->ticks_per_sec = tck > 0 ? (unsigned long)tck : 100;
	b->state = st;
	b->snapshot = linux_snapshot;
	b->signal = linux_signal;
	b->set_nice = linux_set_nice;
	b->self_pid = linux_self;
	b->close = linux_close;
	return PSD_OK;
}
