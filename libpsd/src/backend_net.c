/*
 * backend_net.c - processes on another machine, through psdoomd
 *
 * psdoomd (libpsd/psdoomd.c) runs libpsd on a Linux, BSD or Haiku host and
 * answers on TCP and UDP at one address and port.
 *
 * TCP, one line per request:
 *
 *   C: HELLO 1 <token>        S: OK psdoomd <version> <mode>   | ERR auth
 *   C: LIST                   S: <pid> <ppid> <uid> <start_id> <flags> <nice> <rss_kb> <user> <name>
 *                             S: ...
 *                             S: .
 *   C: SIGNAL <pid> <sig>     S: OK | ERR <why>     (sig: TERM KILL HUP STOP CONT)
 *   C: RENICE <pid> <nice>    S: OK | ERR <why>
 *   C: QUIT
 *
 * UDP (what DOS speaks, over dos_net.c): every datagram is
 * "PSD1 <token> <id> <command>" and the answer "PSD1 <id> <answer>". LIST
 * is paged, "LIST <snap> <offset>" -> "LIST <next>|." plus the lines that
 * fit; a lost answer is asked for again with the same <id>, and psdoomd
 * replays it instead of acting twice. -psnet udp:HOST:PORT picks UDP
 * elsewhere too (that is how it is tested without DOS).
 *
 * flags: 'd' daemon, 'k' kernel thread, 'p' protected by psdoomd's own
 * (modern) rules, '-' none. psdoomd's mode is the floor: a client playing
 * classic still cannot kill what a modern psdoomd protects. The client's
 * own kill mode (TERM then KILL, protection display) works as usual.
 *
 * Every call has a short timeout: a LAN answers in milliseconds, a dead
 * host costs one poll, and later polls reconnect (with a 3 s backoff).
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

#ifdef PSD_DOSNET
#include "dos_net.h"
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/select.h>
#endif

#include "psd_internal.h"

#define NET_TIMEOUT_MS  1500     /* TCP */
#define UDP_WAIT_MS     400      /* UDP: per try */
#define UDP_TRIES       4
#define NET_MAX_PROCS   1024

typedef struct {
	char host[128];
	char port[16];
	char token[128];
	int udp;
	int fd;                      /* TCP or connected UDP socket; DOS: 0 when open */
	char buf[4096];              /* TCP: received, not yet consumed */
	size_t len;
	long next_id;
	psd_proc *procs;
	size_t nprocs, cap;
	long long next_try_ms;       /* back off after a failed connect */
#ifdef PSD_DOSNET
	unsigned long ip;
	unsigned short uport;
#endif
} net_state;

/* ---- POSIX sockets ---------------------------------------------------- */

#ifndef PSD_DOSNET
static int wait_fd(int fd, int for_write, int ms)
{
	fd_set set;
	struct timeval tv;

	FD_ZERO(&set);
	FD_SET(fd, &set);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	return select(fd + 1, for_write ? NULL : &set, for_write ? &set : NULL, NULL, &tv);
}

static int sock_open(net_state *s)
{
	struct addrinfo hints, *res = NULL, *ai;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = s->udp ? SOCK_DGRAM : SOCK_STREAM;
	if (getaddrinfo(s->host, s->port, &hints, &res) != 0 || res == NULL) {
		fprintf(stderr, "psdoom: net: cannot resolve %s\n", s->host);
		return PSD_ERR;
	}
	for (ai = res; ai != NULL && s->fd < 0; ai = ai->ai_next) {
		int fd = (int)socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);

		if (fd < 0)
			continue;
		/* blocking connect: the LAN answers or refuses quickly */
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
			s->fd = fd;
		else
			close(fd);
	}
	freeaddrinfo(res);
	if (s->fd < 0) {
		fprintf(stderr, "psdoom: net: cannot reach %s:%s\n", s->host, s->port);
		return PSD_ERR;
	}
	return PSD_OK;
}

static void sock_close(net_state *s)
{
	if (s->fd >= 0)
		close(s->fd);
	s->fd = -1;
}

static int dgram_send(net_state *s, const char *buf, size_t len)
{
	return send(s->fd, buf, len, 0) == (ssize_t)len ? PSD_OK : PSD_ERR;
}

static int dgram_recv(net_state *s, char *buf, size_t max, int ms)
{
	ssize_t n;

	if (wait_fd(s->fd, 0, ms) <= 0)
		return -1;
	n = recv(s->fd, buf, max, 0);
	return n < 0 ? -1 : (int)n;
}
#else /* PSD_DOSNET */
static int sock_open(net_state *s)
{
	if (dosnet_open() != PSD_OK)
		return PSD_ERR;
	s->fd = 0;
	return PSD_OK;
}

static void sock_close(net_state *s)
{
	s->fd = -1;                  /* the stack stays up until exit */
}

static int dgram_send(net_state *s, const char *buf, size_t len)
{
	return dosnet_send(s->ip, s->uport, buf, len);
}

static int dgram_recv(net_state *s, char *buf, size_t max, int ms)
{
	(void)s;
	return dosnet_recv(buf, max, ms);
}
#endif

static void net_drop(net_state *s)
{
	sock_close(s);
	s->len = 0;
}

/* ---- TCP lines -------------------------------------------------------- */

#ifndef PSD_DOSNET
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
#endif

/* ---- UDP exchanges ---------------------------------------------------- */

/* Send one command, wait for its answer (after "PSD1 <id> ") into out. */
static int udp_xchg(net_state *s, const char *cmd, char *out, size_t outlen)
{
	char req[512], prefix[32], *buf = s->buf;
	long id = ++s->next_id;
	int try, n;

	snprintf(req, sizeof req, "PSD1 %s %ld %s", s->token, id, cmd);
	snprintf(prefix, sizeof prefix, "PSD1 %ld ", id);
	for (try = 0; try < UDP_TRIES; try++) {
		long long until = psd_platform_now_ms() + UDP_WAIT_MS;

		if (dgram_send(s, req, strlen(req)) != PSD_OK)
			return PSD_ERR;
		for (;;) {
			long long left = until - psd_platform_now_ms();

			if (left <= 0)
				break;
			n = dgram_recv(s, buf, sizeof s->buf - 1, (int)left);
			if (n <= 0)
				break;
			buf[n] = '\0';
			if (strncmp(buf, prefix, strlen(prefix)) == 0) {
				psd_strlcpy(out, buf + strlen(prefix), outlen);
				return PSD_OK;
			}
			/* a late answer to an earlier request: ignore it */
		}
	}
	return PSD_ERR;
}

/* First line of a multi-line answer, in place. */
static char *first_line(char *s)
{
	char *nl = strchr(s, '\n');

	if (nl)
		*nl = '\0';
	return nl ? nl + 1 : s + strlen(s);
}

/* ---- connect + hello -------------------------------------------------- */

static int net_connect(net_state *s)
{
	char line[256];
	long long now = psd_platform_now_ms();
	int ok;

	if (s->fd >= 0)
		return PSD_OK;
	if (now < s->next_try_ms)
		return PSD_ERR;
	s->next_try_ms = now + 3000;
	if (sock_open(s) != PSD_OK)
		return PSD_ERR;

	if (s->udp) {
		ok = udp_xchg(s, "HELLO", line, sizeof line) == PSD_OK;
		first_line(line);
	} else {
#ifndef PSD_DOSNET
		snprintf(line, sizeof line, "HELLO 1 %s\n", s->token);
		ok = send_line(s, line) == PSD_OK && recv_line(s, line, sizeof line) == PSD_OK;
#else
		ok = 0;
#endif
	}
	if (!ok || strncmp(line, "OK ", 3) != 0) {
		fprintf(stderr, "psdoom: net: %s:%s refused us (%s)\n", s->host, s->port,
		        ok ? line : "no answer");
		net_drop(s);
		return PSD_ERR;
	}
	s->next_try_ms = 0;
	return PSD_OK;
}

/* ---- the protocol ----------------------------------------------------- */

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

static int add_line(net_state *s, const char *line)
{
	if (s->nprocs == s->cap) {
		size_t cap = s->cap ? s->cap * 2 : 64;
		psd_proc *np;

		if (cap > NET_MAX_PROCS)
			return PSD_ERR;
		np = realloc(s->procs, cap * sizeof *np);
		if (np == NULL)
			return PSD_ERR;
		s->procs = np;
		s->cap = cap;
	}
	if (parse_proc(line, &s->procs[s->nprocs]) == PSD_OK)
		s->nprocs++;
	return PSD_OK;
}

static int list_udp(net_state *s)
{
	static char page[4096];
	long snap = s->next_id + 1;
	unsigned long off = 0;
	int pages;

	for (pages = 0; pages < 64; pages++) {
		char cmd[64], next[16], *rest, *line;

		snprintf(cmd, sizeof cmd, "LIST %ld %lu", snap, off);
		if (udp_xchg(s, cmd, page, sizeof page) != PSD_OK)
			return PSD_ERR;
		rest = first_line(page);
		if (sscanf(page, "LIST %15s", next) != 1)
			return PSD_ERR;
		for (line = rest; *line; ) {
			char *nl = strchr(line, '\n');

			if (nl)
				*nl = '\0';
			if (*line && add_line(s, line) != PSD_OK)
				return PSD_ERR;
			if (!nl)
				break;
			line = nl + 1;
		}
		if (strcmp(next, ".") == 0)
			return PSD_OK;
		off = strtoul(next, NULL, 10);
	}
	return PSD_ERR;
}

static int list_tcp(net_state *s)
{
#ifndef PSD_DOSNET
	char line[512];

	if (send_line(s, "LIST\n") != PSD_OK)
		return PSD_ERR;
	for (;;) {
		if (recv_line(s, line, sizeof line) != PSD_OK)
			return PSD_ERR;
		if (strcmp(line, ".") == 0)
			return PSD_OK;
		if (add_line(s, line) != PSD_OK)
			return PSD_ERR;
	}
#else
	(void)s;
	return PSD_ERR;
#endif
}

static int net_snapshot(psd_backend *b, psd_proc **out, size_t *n)
{
	net_state *s = b->state;

	if (net_connect(s) != PSD_OK)
		return PSD_ERR;
	s->nprocs = 0;
	if ((s->udp ? list_udp(s) : list_tcp(s)) != PSD_OK) {
		net_drop(s);
		return PSD_ERR;
	}
	*out = s->procs;
	*n = s->nprocs;
	return PSD_OK;
}

static int answer(net_state *s, const char *cmd)
{
	char line[128];
	int ok;

	if (net_connect(s) != PSD_OK)
		return PSD_ERR;
	if (s->udp) {
		ok = udp_xchg(s, cmd, line, sizeof line) == PSD_OK;
		first_line(line);
	} else {
#ifndef PSD_DOSNET
		char req[128];

		snprintf(req, sizeof req, "%s\n", cmd);
		ok = send_line(s, req) == PSD_OK && recv_line(s, line, sizeof line) == PSD_OK;
#else
		ok = 0;
#endif
	}
	if (!ok) {
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
	snprintf(req, sizeof req, "SIGNAL %ld %s", p->pid, psd_signal_name(sig));
	return answer(b->state, req);
}

static int net_set_nice(psd_backend *b, const psd_proc *p, int nice)
{
	char req[64];

	snprintf(req, sizeof req, "RENICE %ld %d", p->pid, nice);
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

#ifndef PSD_DOSNET
	if (s->fd >= 0 && !s->udp)
		send_line(s, "QUIT\n");
#endif
	net_drop(s);
	free(s->procs);
	free(s);
	b->state = NULL;
}

int psd_backend_net_open(psd_backend *b, const struct psd_config *cfg)
{
	net_state *s;
	const char *addr = cfg->net_addr, *colon, *tok = getenv("PSDOOM_NET_TOKEN");
	int udp = 0;

	if (strncmp(addr, "udp:", 4) == 0) {
		udp = 1;
		addr += 4;
	} else if (strncmp(addr, "tcp:", 4) == 0) {
		addr += 4;
	}
#ifdef PSD_DOSNET
	udp = 1;                     /* DOS has our own UDP and nothing else */
#endif
	colon = strrchr(addr, ':');
	if (addr[0] == '\0' || colon == NULL || colon == addr || colon[1] == '\0') {
		fprintf(stderr, "psdoom: net: give psdoomd's address as -psnet HOST:PORT\n");
		return PSD_ERR_BACKEND;
	}
	if ((tok == NULL || tok[0] == '\0') && cfg->net_token[0] == '\0') {
		fprintf(stderr, "psdoom: net: set PSDOOM_NET_TOKEN (or net_token in the config)\n");
		return PSD_ERR_BACKEND;
	}
	s = calloc(1, sizeof *s);
	if (s == NULL)
		return PSD_ERR;
	psd_strlcpy(s->host, addr, sizeof s->host);
	s->host[colon - addr] = '\0';
	psd_strlcpy(s->port, colon + 1, sizeof s->port);
	psd_strlcpy(s->token, tok && tok[0] ? tok : cfg->net_token, sizeof s->token);
	s->udp = udp;
	s->fd = -1;
	s->next_id = (long)(psd_platform_now_ms() % 100000) * 1000;
#ifdef PSD_DOSNET
	if (dosnet_parse_ip(s->host, &s->ip) != PSD_OK) {
		fprintf(stderr, "psdoom: net: DOS wants psdoomd as an IP address (no DNS)\n");
		free(s);
		return PSD_ERR_BACKEND;
	}
	s->uport = (unsigned short)atoi(s->port);
#endif

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
