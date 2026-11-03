/*
 * backend_posix.c - pieces the BSD and Haiku backends share
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#include "backend_posix.h"

#define USER_CACHE 64

static struct {
	long uid;
	char name[PSD_USER_MAX];
} users[USER_CACHE];
static int nusers;

const char *psd_posix_user(long uid)
{
	struct passwd *pw;
	int i;

	for (i = 0; i < nusers; i++)
		if (users[i].uid == uid)
			return users[i].name;
	i = nusers < USER_CACHE ? nusers++ : USER_CACHE - 1;
	users[i].uid = uid;
	pw = getpwuid((uid_t)uid);
	if (pw != NULL)
		psd_strlcpy(users[i].name, pw->pw_name, PSD_USER_MAX);
	else
		snprintf(users[i].name, PSD_USER_MAX, "%ld", uid);
	return users[i].name;
}

int psd_posix_signo(psd_signal sig)
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

int psd_posix_kill(long pid, psd_signal sig)
{
	if (kill((pid_t)pid, psd_posix_signo(sig)) == 0)
		return PSD_OK;
	return errno == ESRCH ? PSD_ERR_GONE : PSD_ERR;
}

int psd_posix_set_nice(long pid, int nice)
{
	errno = 0;
	if (setpriority(PRIO_PROCESS, (id_t)pid, nice) == 0)
		return PSD_OK;
	return errno == ESRCH ? PSD_ERR_GONE : PSD_ERR;
}

long psd_posix_self(psd_backend *b)
{
	(void)b;
	return (long)getpid();
}

/* argv as NUL-separated strings -> name = argv[0], cmdline = joined */
void psd_posix_set_argv(psd_proc *p, const char *buf, size_t len)
{
	char line[PSD_CMDLINE_MAX];
	size_t i, n = len < sizeof line - 1 ? len : sizeof line - 1;

	if (len == 0 || buf[0] == '\0') {
		snprintf(p->name, sizeof p->name, "[%s]", p->comm);
		p->cmdline[0] = '\0';
		return;
	}
	psd_strlcpy(p->name, buf, sizeof p->name);
	memcpy(line, buf, n);
	line[n] = '\0';
	for (i = 0; i < n; i++)
		if (line[i] == '\0')
			line[i] = ' ';
	while (n > 0 && line[n - 1] == ' ')
		line[--n] = '\0';
	psd_strlcpy(p->cmdline, line, sizeof p->cmdline);
}

int psd_posix_grow(psd_proc **procs, size_t *cap, size_t need)
{
	psd_proc *np;
	size_t c = *cap ? *cap : 256;

	if (need <= *cap)
		return PSD_OK;
	while (c < need)
		c *= 2;
	np = realloc(*procs, c * sizeof *np);
	if (np == NULL)
		return PSD_ERR;
	*procs = np;
	*cap = c;
	return PSD_OK;
}
