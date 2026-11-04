/*
 * backend_net.c - processes on another machine, through psdoomd
 *
 * psdoomd (libpsd/psdoomd.c) runs libpsd's native backend on a Linux, BSD
 * or Haiku host and answers a line protocol over TCP:
 *
 *   C: HELLO 1 <token>        S: OK psdoomd <version> <mode>   | ERR auth
 *   C: LIST                   S: <pid> <ppid> <uid> <start_id> <flags> <nice> <rss_kb> <user> <name>
 *                             S: ...
 *                             S: .
 *   C: SIGNAL <pid> <sig>     S: OK | ERR <why>     (sig: TERM KILL HUP STOP CONT)
 *   C: RENICE <pid> <nice>    S: OK | ERR <why>
 *   C: QUIT
 *
 * flags: 'd' daemon, 'k' kernel thread, 'p' protected by psdoomd's own
 * (modern) rules, '-' none. psdoomd's mode is the floor: a client playing
 * classic still cannot kill what a modern psdoomd protects. The client's
 * own kill mode (TERM then KILL, protection display) works as usual.
 *
 * Plain BSD sockets, and Watt-32's on DOS (WATT32). Every call has a short
 * timeout: a LAN answers in milliseconds, a dead host costs one poll, and
 * the next poll reconnects.
 *
 * The token is a shared secret sent in clear text: psdoomd is a LAN toy for
 * test machines, not a remote administration tool.
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#if !defined(_POSIX_C_SOURCE) && !defined(__HAIKU__) && !defined(__DJGPP__)
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WATT32
#include <tcp.h>                 /* sock_init() */
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#ifdef __HAIKU__
#include <sys/select.h>
#endif

#include "psd_internal.h"

#ifdef WATT32
#define sock_close   close_s
#define sock_select  select_s
#else
#define sock_close   close
#define sock_select  select
#endif

#define NET_TIMEOUT_MS  1500
#define NET_MAX_PROCS   1024

typedef struct {
	char host[128];
	char port[16];
	char token[128];
	int fd;
	char buf[4096];              /* received, not yet consumed */
	size_t len;
	psd_proc *procs;
	size_t nprocs, cap;
	long long next_try_ms;       /* back off after a failed connect */
} net_state;

/* ---- socket plumbing ------------------------------------------------ */

static int wait_fd(int fd, int for_write, int ms)
{
	fd_set set;
	struct timeval tv;

	FD_ZERO(&set);
	FD_SET(fd, &set);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	return sock_select(fd + 1, for_write ? NULL : &set, for_write ? &set : NULL, NULL, &tv);
}

static void net_drop(net_state *s)
{
	if (s->fd >= 0)
		sock_close(s->fd);
	s->fd = -1;
	s->len = 0;
}

static int send_line(net_state *s, const char *line)
{
	size_t len = strlen(line), off = 0;

	while (off < len) {
		int n;

		if (wait_fd(s->fd, 1, NET_TIMEOUT_MS) <= 0)
			return PSD_ERR;
		n = (int)send(s->fd, line + off, len - off, 0);
		if (n <= 0)
			return PSD_ERR;
		off += (size_t)n;
	}
	return PSD_OK;
}

/* One line, without the newline, into out. */
static int recv_line(net_state *s, char *out, size_t outlen)
{
	for (;;) {
		char *nl = memchr(s->buf, '\n', s->len);
		int n;

		if (nl != NULL) {
			size_t l = (size_t)(nl - s->buf);

			if (l > 0 && s->buf[l - 1] == '\r')
				s->buf[l - 1] = '\0';
			s->buf[l] = '\0';
			psd_strlcpy(out, s->buf, outlen);
			memmove(s->buf, nl + 1, s->len - l - 1);
			s->len -= l + 1;
			return PSD_OK;
		}
		if (s->len == sizeof s->buf)
			return PSD_ERR;      /* a line longer than we will ever send */
		if (wait_fd(s->fd, 0, NET_TIMEOUT_MS) <= 0)
			return PSD_ERR;
		n = (int)recv(s->fd, s->buf + s->len, sizeof s->buf - s->len, 0);
		if (n <= 0)
			return PSD_ERR;
		s->len += (size_t)n;
	}
}

static int net_connect(net_state *s)
{
	struct addrinfo hints, *res = NULL, *ai;
	char line[256];
	long long now = psd_platform_now_ms();

	if (s->fd >= 0)
		return PSD_OK;
	if (now < s->next_try_ms)
		return PSD_ERR;
	s->next_try_ms = now + 3000;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(s->host, s->port, &hints, &res) != 0 || res == NULL) {
		fprintf(stderr, "psdoom: net: cannot resolve %s\n", s->host);
		return PSD_ERR;
	}
	for (ai = res; ai != NULL && s->fd < 0; ai = ai->ai_next) {
		int fd = (int)socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);

		if (fd < 0)
			continue;
		/* blocking connect: Watt-32 and every POSIX do this the same way,
		 * and the LAN answers or refuses quickly */
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
			s->fd = fd;
		else
			sock_close(fd);
	}
	freeaddrinfo(res);
	if (s->fd < 0) {
		fprintf(stderr, "psdoom: net: cannot connect to %s:%s\n", s->host, s->port);
		return PSD_ERR;
	}

	snprintf(line, sizeof line, "HELLO 1 %s\n", s->token);
	if (send_line(s, line) != PSD_OK || recv_line(s, line, sizeof line) != PSD_OK ||
	    strncmp(line, "OK ", 3) != 0) {
		fprintf(stderr, "psdoom: net: %s:%s refused us (%s)\n", s->host, s->port,
		        strncmp(line, "ERR", 3) == 0 ? line : "no answer");
		net_drop(s);
		return PSD_ERR;
	}
	s->next_try_ms = 0;
	return PSD_OK;
}

/* ---- the protocol --------------------------------------------------- */

static int parse_proc(const char *line, psd_proc *p)
{
	long pid, ppid, uid;
	unsigned long long start_id;
	char flags[8], user[PSD_USER_MAX];
	int nice, used = 0;
	unsigned long rss;

	memset(p, 0, sizeof *p);
	if (sscanf(line, "%ld %ld %ld %llu %7s %d %lu %31s %n", &pid, &ppid, &uid, &start_id,
	           flags, &nice, &rss, user, &used) < 8 || used == 0 || line[used] == '\0')
		return PSD_ERR;
	p->pid = pid;
	p->ppid = ppid;
	p->uid = uid;
	p->start_id = start_id;
	p->nice = nice;
	p->rss_kb = rss;
	p->is_daemon = strchr(flags, 'd') != NULL;
	p->is_kernel = strchr(flags, 'k') != NULL || strchr(flags, 'p') != NULL;
	psd_strlcpy(p->user, user, sizeof p->user);
	psd_strlcpy(p->name, line + used, sizeof p->name);
	psd_strlcpy(p->cmdline, line + used, sizeof p->cmdline);
	/* comm: the basename of the first word, as the native backends give it */
	{
		const char *n = p->name, *sp = strchr(n, ' '), *sl;
		char word[PSD_NAME_MAX];

		psd_strlcpy(word, n, sp && (size_t)(sp - n) < sizeof word ? (size_t)(sp - n) + 1 : sizeof word);
		sl = strrchr(word, '/');
		psd_strlcpy(p->comm, sl ? sl + 1 : word, sizeof p->comm);
	}
	psd_make_label(p);
	return PSD_OK;
}

static int net_snapshot(psd_backend *b, psd_proc **out, size_t *n)
{
	net_state *s = b->state;
	char line[512];

	if (net_connect(s) != PSD_OK)
		return PSD_ERR;
	if (send_line(s, "LIST\n") != PSD_OK)
		goto fail;
	s->nprocs = 0;
	for (;;) {
		if (recv_line(s, line, sizeof line) != PSD_OK)
			goto fail;
		if (strcmp(line, ".") == 0)
			break;
		if (s->nprocs == s->cap) {
			size_t cap = s->cap ? s->cap * 2 : 64;
			psd_proc *np;

			if (cap > NET_MAX_PROCS)
				goto fail;
			np = realloc(s->procs, cap * sizeof *np);
			if (np == NULL)
				goto fail;
			s->procs = np;
			s->cap = cap;
		}
		if (parse_proc(line, &s->procs[s->nprocs]) == PSD_OK)
			s->nprocs++;
	}
	*out = s->procs;
	*n = s->nprocs;
	return PSD_OK;
fail:
	net_drop(s);
	return PSD_ERR;
}

static int answer(net_state *s, const char *req)
{
	char line[128];

	if (net_connect(s) != PSD_OK)
		return PSD_ERR;
	if (send_line(s, req) != PSD_OK || recv_line(s, line, sizeof line) != PSD_OK) {
		net_drop(s);
		return PSD_ERR;
	}
	if (strcmp(line, "OK") == 0)
		return PSD_OK;
	if (strcmp(line, "ERR protected") == 0)
		return PSD_ERR_PROTECTED;
	if (strcmp(line, "ERR gone") == 0)
		return PSD_ERR_GONE;
	if (strcmp(line, "ERR unknown") == 0)
		return PSD_ERR_UNKNOWN;
	if (strcmp(line, "ERR eperm") == 0)
		errno = EPERM;
	return PSD_ERR;
}

static int net_signal(psd_backend *b, const psd_proc *p, psd_signal sig, int verify)
{
	char req[64];

	(void)verify;                /* psdoomd checks the identity itself */
	snprintf(req, sizeof req, "SIGNAL %ld %s\n", p->pid, psd_signal_name(sig));
	return answer(b->state, req);
}

static int net_set_nice(psd_backend *b, const psd_proc *p, int nice)
{
	char req[64];

	snprintf(req, sizeof req, "RENICE %ld %d\n", p->pid, nice);
	return answer(b->state, req);
}

static long net_self_pid(psd_backend *b)
{
	(void)b;
	return -1;                   /* we are not on that machine */
}

static void net_close(psd_backend *b)
{
	net_state *s = b->state;

	if (s->fd >= 0)
		send_line(s, "QUIT\n");
	net_drop(s);
	free(s->procs);
	free(s);
	b->state = NULL;
}

int psd_backend_net_open(psd_backend *b, const struct psd_config *cfg)
{
	net_state *s;
	const char *colon, *tok = getenv("PSDOOM_NET_TOKEN");

	colon = strrchr(cfg->net_addr, ':');
	if (cfg->net_addr[0] == '\0' || colon == NULL || colon == cfg->net_addr || colon[1] == '\0') {
		fprintf(stderr, "psdoom: net: give psdoomd's address as -psnet HOST:PORT\n");
		return PSD_ERR_BACKEND;
	}
	if ((tok == NULL || tok[0] == '\0') && cfg->net_token[0] == '\0') {
		fprintf(stderr, "psdoom: net: set PSDOOM_NET_TOKEN (or net_token in the config)\n");
		return PSD_ERR_BACKEND;
	}
#ifdef WATT32
	if (sock_init() != 0) {
		fprintf(stderr, "psdoom: net: no network (Watt-32: packet driver? WATTCP.CFG?)\n");
		return PSD_ERR_BACKEND;
	}
#endif
	s = calloc(1, sizeof *s);
	if (s == NULL)
		return PSD_ERR;
	psd_strlcpy(s->host, cfg->net_addr, sizeof s->host);
	s->host[colon - cfg->net_addr] = '\0';
	psd_strlcpy(s->port, colon + 1, sizeof s->port);
	psd_strlcpy(s->token, tok && tok[0] ? tok : cfg->net_token, sizeof s->token);
	s->fd = -1;

	memset(b, 0, sizeof *b);
	b->name = "net";
	b->ticks_per_sec = 0;
	b->settle_ms = 200;
	b->state = s;
	b->snapshot = net_snapshot;
	b->signal = net_signal;
	b->set_nice = net_set_nice;
	b->self_pid = net_self_pid;
	b->close = net_close;
	return PSD_OK;
}
