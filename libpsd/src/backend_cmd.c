/*
 * backend_cmd.c - psdoom-ng's external command hooks
 *
 *   PSDOOM_NG_PS_CMD      prints one line per process: <user> <pid> <name> <is_daemon>
 *   PSDOOM_NG_RENICE_CMD  gets the pid as its argument
 *   PSDOOM_NG_KILL_CMD    gets the pid as its argument
 *
 * Same names and format as psdoom-ng (Hector Rivas Gandara's hooks), so
 * psdoom-ng scripts and kubedoom-style helpers work unchanged. Unlike
 * psdoom-ng the commands run asynchronously: a slow helper never stalls the
 * game, the snapshot is just one helper run old. Extension: the helpers also
 * get PSDOOM_SIGNAL=TERM|KILL|... and PSDOOM_NICE=<n> in the environment.
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#if !defined(_POSIX_C_SOURCE) && !defined(__HAIKU__)
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "psd_internal.h"

extern char **environ;

#define CMD_MAX_HELPERS     32
#define CMD_FIRST_WAIT_MS   2000    /* first snapshot may block this long */
#define CMD_TIMEOUT_MS      10000   /* a ps helper running longer is killed */

typedef struct cstate {
	char ps_cmd[1024];
	char renice_cmd[1024];
	char kill_cmd[1024];

	/* running ps helper */
	pid_t ps_pid;
	int ps_fd;
	long long ps_started;
	char *buf;
	size_t blen, bcap;

	/* last complete result */
	long long result_started;    /* when the run that produced it began */
	psd_proc *procs;
	size_t nprocs, cap;
	int have_result;

	pid_t helpers[CMD_MAX_HELPERS];
	int nhelpers;
} cstate;

static int env_cmd(char *dst, size_t len, const char *var)
{
	const char *v = getenv(var);

	if (v == NULL || v[0] == '\0') {
		dst[0] = '\0';
		return 0;
	}
	psd_strlcpy(dst, v, len);
	return 1;
}

/* sh -c '<cmd> "$1"' sh <arg>, with extra env entries; stdout -> *fd_out if given */
static pid_t spawn_sh(const char *cmd, const char *arg, char **extra_env, int *fd_out)
{
	char script[1200];
	char *argv[6];
	char **envp = environ;
	char **merged = NULL;
	posix_spawn_file_actions_t fa;
	int pipefd[2] = { -1, -1 };
	pid_t pid;
	int rc;

	argv[0] = "sh";
	argv[1] = "-c";
	argv[2] = script;
	if (arg != NULL) {
		/* the pid travels as $1, never pasted into the script */
		snprintf(script, sizeof script, "%s \"$1\"", cmd);
		argv[3] = "sh";
		argv[4] = (char *)arg;
		argv[5] = NULL;
	} else {
		psd_strlcpy(script, cmd, sizeof script);
		argv[3] = NULL;
	}

	if (extra_env != NULL) {
		size_t ne = 0, nx = 0, i;
		while (environ[ne])
			ne++;
		while (extra_env[nx])
			nx++;
		merged = calloc(ne + nx + 1, sizeof *merged);
		if (merged == NULL)
			return -1;
		for (i = 0; i < nx; i++)
			merged[i] = extra_env[i];
		for (i = 0; i < ne; i++)
			merged[nx + i] = environ[i];
		envp = merged;
	}

	posix_spawn_file_actions_init(&fa);
	if (fd_out != NULL) {
		if (pipe(pipefd) != 0) {
			posix_spawn_file_actions_destroy(&fa);
			free(merged);
			return -1;
		}
		posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDOUT_FILENO);
		posix_spawn_file_actions_addclose(&fa, pipefd[0]);
		posix_spawn_file_actions_addclose(&fa, pipefd[1]);
	}
	rc = posix_spawn(&pid, "/bin/sh", &fa, NULL, argv, envp);
	posix_spawn_file_actions_destroy(&fa);
	free(merged);

	if (fd_out != NULL) {
		close(pipefd[1]);
		if (rc != 0) {
			close(pipefd[0]);
			return -1;
		}
		fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL) | O_NONBLOCK);
		fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
		*fd_out = pipefd[0];
	}
	return rc == 0 ? pid : -1;
}

static void parse_result(cstate *st)
{
	char *line = st->buf, *nl;
	size_t count = 0;

	if (st->buf == NULL) {
		st->nprocs = 0;
		st->have_result = 1;
		return;
	}
	st->buf[st->blen] = '\0';
	for (; line && *line; line = nl ? nl + 1 : NULL) {
		char user[PSD_USER_MAX], *tok[64], *s;
		int ntok = 0;
		psd_proc *p;
		long pid;
		char *end;

		nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';
		/* tokenise on whitespace */
		for (s = strtok(line, " \t\r"); s && ntok < 64; s = strtok(NULL, " \t\r"))
			tok[ntok++] = s;
		if (ntok < 3)
			continue;
		pid = strtol(tok[1], &end, 10);
		if (*end != '\0' || pid <= 0)
			continue;
		psd_strlcpy(user, tok[0], sizeof user);

		if (count == st->cap) {
			size_t cap = st->cap ? st->cap * 2 : 128;
			psd_proc *np = realloc(st->procs, cap * sizeof *np);
			if (np == NULL)
				break;
			st->procs = np;
			st->cap = cap;
		}
		p = &st->procs[count++];
		memset(p, 0, sizeof *p);
		p->pid = pid;
		p->ppid = -1;
		p->uid = -1;
		psd_strlcpy(p->user, user, sizeof p->user);
		/* <name> may contain spaces; the last token is is_daemon when it's 0/1 */
		{
			int last = ntok - 1, i;
			size_t used = 0;

			if (ntok >= 4 && (strcmp(tok[last], "0") == 0 || strcmp(tok[last], "1") == 0))
				p->is_daemon = tok[last][0] == '1';
			else
				last = ntok;
			for (i = 2; i < last; i++) {
				int w = snprintf(p->name + used, sizeof p->name - used, "%s%s",
				                 i > 2 ? " " : "", tok[i]);
				if (w < 0 || (size_t)w >= sizeof p->name - used)
					break;
				used += (size_t)w;
			}
		}
		psd_strlcpy(p->comm, psd_basename(p->name), sizeof p->comm);
		psd_strlcpy(p->cmdline, p->name, sizeof p->cmdline);
	}
	st->nprocs = count;
	st->have_result = 1;
}

static void reap_helpers(cstate *st)
{
	int i;

	for (i = 0; i < st->nhelpers; ) {
		if (waitpid(st->helpers[i], NULL, WNOHANG) != 0)
			st->helpers[i] = st->helpers[--st->nhelpers];
		else
			i++;
	}
}

/* drain the ps helper's output; finish when it closes stdout */
static void pump_ps(cstate *st)
{
	char chunk[4096];

	if (st->ps_pid <= 0)
		return;
	for (;;) {
		ssize_t r = read(st->ps_fd, chunk, sizeof chunk);
		if (r > 0) {
			if (st->blen + (size_t)r + 1 > st->bcap) {
				size_t cap = st->bcap ? st->bcap * 2 : 8192;
				char *nb;
				while (cap < st->blen + (size_t)r + 1)
					cap *= 2;
				nb = realloc(st->buf, cap);
				if (nb == NULL)
					return;
				st->buf = nb;
				st->bcap = cap;
			}
			memcpy(st->buf + st->blen, chunk, (size_t)r);
			st->blen += (size_t)r;
			continue;
		}
		if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			if (psd_platform_now_ms() - st->ps_started > CMD_TIMEOUT_MS) {
				kill(st->ps_pid, SIGKILL);
				waitpid(st->ps_pid, NULL, 0);
				close(st->ps_fd);
				st->ps_pid = 0;
				fprintf(stderr, "psdoom: PSDOOM_NG_PS_CMD timed out\n");
			}
			return;
		}
		if (r < 0 && errno == EINTR)
			continue;
		/* EOF or error: complete */
		close(st->ps_fd);
		waitpid(st->ps_pid, NULL, 0);
		st->ps_pid = 0;
		parse_result(st);
		st->result_started = st->ps_started;
		return;
	}
}

static void start_ps(cstate *st)
{
	if (st->ps_pid > 0)
		return;
	st->blen = 0;
	st->ps_started = psd_platform_now_ms();
	st->ps_pid = spawn_sh(st->ps_cmd, NULL, NULL, &st->ps_fd);
	if (st->ps_pid < 0) {
		st->ps_pid = 0;
		fprintf(stderr, "psdoom: cannot run PSDOOM_NG_PS_CMD\n");
	}
}

static void cmd_tick(psd_backend *b)
{
	cstate *st = b->state;

	pump_ps(st);
	reap_helpers(st);
}

static int cmd_snapshot(psd_backend *b, psd_proc **out, size_t *n)
{
	cstate *st = b->state;

	cmd_tick(b);
	if (st->ps_pid == 0)
		start_ps(st);
	if (!st->have_result) {
		long long until = psd_platform_now_ms() + CMD_FIRST_WAIT_MS;
		while (!st->have_result && st->ps_pid > 0 && psd_platform_now_ms() < until) {
			struct timespec ts = { 0, 10 * 1000000L };
			nanosleep(&ts, NULL);
			pump_ps(st);
		}
		if (st->ps_pid == 0 && st->have_result)
			start_ps(st);    /* keep the next one coming */
	}
	*out = st->procs;
	*n = st->have_result ? st->nprocs : 0;
	b->snap_ms = st->result_started;
	return PSD_OK;
}

static int run_helper(cstate *st, const char *cmd, long pid, char *env1)
{
	char arg[32];
	char *extra[2];
	pid_t h;

	if (cmd[0] == '\0')
		return PSD_OK;           /* hook not set: nothing to do */
	snprintf(arg, sizeof arg, "%ld", pid);
	extra[0] = env1;
	extra[1] = NULL;
	reap_helpers(st);
	h = spawn_sh(cmd, arg, extra, NULL);
	if (h < 0)
		return PSD_ERR;
	if (st->nhelpers < CMD_MAX_HELPERS)
		st->helpers[st->nhelpers++] = h;
	else
		waitpid(h, NULL, 0);
	return PSD_OK;
}

static int cmd_signal(psd_backend *b, const psd_proc *p, psd_signal sig, int verify)
{
	char env[32];
	(void)verify;

	snprintf(env, sizeof env, "PSDOOM_SIGNAL=%s", psd_signal_name(sig));
	return run_helper(b->state, ((cstate *)b->state)->kill_cmd, p->pid, env);
}

static int cmd_set_nice(psd_backend *b, const psd_proc *p, int nice)
{
	char env[32];

	snprintf(env, sizeof env, "PSDOOM_NICE=%d", nice);
	return run_helper(b->state, ((cstate *)b->state)->renice_cmd, p->pid, env);
}

static int cmd_is_helper(psd_backend *b, long pid)
{
	cstate *st = b->state;
	int i;

	if (st->ps_pid > 0 && pid == (long)st->ps_pid)
		return 1;
	for (i = 0; i < st->nhelpers; i++)
		if (pid == (long)st->helpers[i])
			return 1;
	return 0;
}

static long cmd_self(psd_backend *b)
{
	(void)b;
	return (long)getpid();       /* only matters if the hook lists local processes */
}

static void cmd_close(psd_backend *b)
{
	cstate *st = b->state;
	int i;

	if (st == NULL)
		return;
	if (st->ps_pid > 0) {
		kill(st->ps_pid, SIGKILL);
		waitpid(st->ps_pid, NULL, 0);
		close(st->ps_fd);
	}
	for (i = 0; i < st->nhelpers; i++)
		waitpid(st->helpers[i], NULL, 0);
	free(st->buf);
	free(st->procs);
	free(st);
	b->state = NULL;
}

int psd_backend_cmd_open(psd_backend *b, const struct psd_config *cfg)
{
	cstate *st;
	(void)cfg;

	st = calloc(1, sizeof *st);
	if (st == NULL)
		return PSD_ERR;
	if (!env_cmd(st->ps_cmd, sizeof st->ps_cmd, "PSDOOM_NG_PS_CMD")) {
		free(st);
		return PSD_ERR_BACKEND;
	}
	env_cmd(st->renice_cmd, sizeof st->renice_cmd, "PSDOOM_NG_RENICE_CMD");
	env_cmd(st->kill_cmd, sizeof st->kill_cmd, "PSDOOM_NG_KILL_CMD");

	memset(b, 0, sizeof *b);
	b->name = "cmd";
	b->ticks_per_sec = 0;
	b->settle_ms = 1000;         /* the kill hook runs asynchronously too */
	b->state = st;
	b->snapshot = cmd_snapshot;
	b->signal = cmd_signal;
	b->set_nice = cmd_set_nice;
	b->tick = cmd_tick;
	b->self_pid = cmd_self;
	b->is_helper = cmd_is_helper;
	b->close = cmd_close;
	return PSD_OK;
}
