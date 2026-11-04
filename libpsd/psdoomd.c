/*
 * psdoomd - serve this machine's processes to a remote psdoom-plus
 *
 *   PSDOOMD_TOKEN=<secret> psdoomd -l ADDR:PORT [libpsd options]
 *
 * For a DOS PC (or any other machine) running psdoom-plus with
 * -psnet HOST:PORT. psdoomd uses libpsd's native backend (or the psdoom-ng
 * hooks with -psbackend cmd, e.g. containers through a helper), so this
 * host's own rules apply: its mode (-psclassic, else modern) is the floor
 * for every client, its user selection (-psuser, -psallusers, ...) decides
 * which processes are offered, and its audit log records every action.
 *
 * It answers on TCP and UDP at the same address and port; the protocol is
 * described in src/backend_net.c (UDP is what DOS speaks).
 *
 * It listens only on the address it is given (no wildcard default), needs
 * a token of at least 12 characters from $PSDOOMD_TOKEN or -k FILE (never
 * on the command line, where ps would show it), and serves one TCP client
 * at a time. The token travels in clear text: run it between test machines
 * on a network you trust, never on one you care about.
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#if !defined(_POSIX_C_SOURCE) && !defined(__HAIKU__)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#define __BSD_VISIBLE 1
#endif

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "psd.h"

#define IDLE_SECS     300        /* drop a silent TCP client */
#define LINE_MAX_     512
#define MAX_LIST      4096
#define UDP_PAGE      1200       /* payload bytes per LIST datagram */
#define UDP_PEERS     8

static psd_ctx *ctx;
static const char *token;

/* ---- the process list, as protocol lines ----------------------------- */

static long polled[MAX_LIST];
static size_t npolled;

static int collect(void *user, psd_event ev, const psd_proc *p)
{
	(void)user;
	if (ev == PSD_EV_REMOVE)
		return 0;
	if (npolled < MAX_LIST)
		polled[npolled++] = p->pid;
	return 1;                    /* track it, so SIGNAL/RENICE know it */
}

static void clean(char *s, int no_spaces)
{
	for (; *s; s++)
		if (*s == '\n' || *s == '\r' || *s == '\t' || (no_spaces && *s == ' '))
			*s = no_spaces ? '_' : ' ';
}

/* Poll, then format every offered process; returns the number of lines. */
static size_t build_list(char (*lines)[LINE_MAX_], size_t max)
{
	size_t i, n = 0;

	npolled = 0;
	psd_poll(ctx, collect, NULL);
	/* the event's pointers die with the poll; the tracked copies do not */
	for (i = 0; i < npolled && n < max; i++) {
		const psd_proc *p = psd_lookup(ctx, polled[i]);
		char name[PSD_NAME_MAX], user[PSD_USER_MAX];
		char flags[4] = "-", *f = flags;

		if (p == NULL)
			continue;
		if (p->is_daemon) *f++ = 'd';
		if (p->is_kernel) *f++ = 'k';
		if (p->is_protected) *f++ = 'p';
		if (f != flags) *f = '\0';
		snprintf(name, sizeof name, "%s", p->name[0] ? p->name : "?");
		snprintf(user, sizeof user, "%s", p->user[0] ? p->user : "?");
		clean(name, 0);
		clean(user, 1);
		snprintf(lines[n++], LINE_MAX_, "%ld %ld %ld %llu %s %d %lu %s %s", p->pid, p->ppid,
		         p->uid, p->start_id, flags, p->nice, p->rss_kb, user, name);
	}
	return n;
}

/* ---- commands shared by both transports ------------------------------ */

static const char *err_word(int rc)
{
	switch (rc) {
	case PSD_OK:            return "OK";
	case PSD_ERR_PROTECTED: return "ERR protected";
	case PSD_ERR_GONE:      return "ERR gone";
	case PSD_ERR_UNKNOWN:   return "ERR unknown";
	default:                return errno == EPERM ? "ERR eperm" : "ERR error";
	}
}

static int parse_sig(const char *s, psd_signal *out)
{
	static const struct { const char *name; psd_signal sig; } sigs[] = {
		{ "TERM", PSD_SIG_TERM }, { "KILL", PSD_SIG_KILL }, { "HUP", PSD_SIG_HUP },
		{ "STOP", PSD_SIG_STOP }, { "CONT", PSD_SIG_CONT },
	};
	size_t i;

	for (i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
		if (strcmp(s, sigs[i].name) == 0) {
			*out = sigs[i].sig;
			return 0;
		}
	return -1;
}

/* HELLO, SIGNAL, RENICE: one answer line into out. LIST is per transport. */
static void handle(const char *cmd, char *out, size_t outlen)
{
	if (strcmp(cmd, "HELLO") == 0) {
		snprintf(out, outlen, "OK psdoomd %s %s", PSD_VERSION,
		         psd_mode_of(ctx) == PSD_MODE_CLASSIC ? "classic" : "modern");
	} else if (strncmp(cmd, "SIGNAL ", 7) == 0) {
		char sig[8];
		long pid;
		psd_signal s;

		if (sscanf(cmd + 7, "%ld %7s", &pid, sig) != 2 || parse_sig(sig, &s) != 0) {
			snprintf(out, outlen, "ERR syntax");
			return;
		}
		if (psd_lookup(ctx, pid) == NULL) {
			npolled = 0;
			psd_poll(ctx, collect, NULL);   /* not seen yet: look again */
		}
		errno = 0;
		snprintf(out, outlen, "%s", err_word(psd_send(ctx, pid, s)));
	} else if (strncmp(cmd, "RENICE ", 7) == 0) {
		long pid;
		int nice;

		if (sscanf(cmd + 7, "%ld %d", &pid, &nice) != 2) {
			snprintf(out, outlen, "ERR syntax");
			return;
		}
		errno = 0;
		snprintf(out, outlen, "%s", err_word(psd_renice(ctx, pid, nice)));
	} else {
		snprintf(out, outlen, "ERR syntax");
	}
}

/* constant time, so the answer time says nothing about the token */
static int same(const char *a, const char *b)
{
	size_t la = strlen(a), lb = strlen(b), i;
	unsigned char d = (unsigned char)(la != lb);

	for (i = 0; i < la; i++)
		d |= (unsigned char)(a[i] ^ b[i % (lb ? lb : 1)]);
	return d == 0;
}

/* ---- TCP: one line per request, LIST ends with "." -------------------- */

typedef struct {
	int fd;
	int authed;
	char buf[LINE_MAX_];
	size_t len;
	time_t last;
	char peer[64];
} tcp_client;

static void say(int fd, const char *s)
{
	size_t len = strlen(s), off = 0;

	while (off < len) {
		ssize_t n = send(fd, s + off, len - off, 0);
		if (n <= 0)
			return;
		off += (size_t)n;
	}
}

static char list_lines[MAX_LIST][LINE_MAX_];

/* 0: keep the client, -1: drop it */
static int tcp_line(tcp_client *c, char *line)
{
	char out[LINE_MAX_ + 2];

	if (!c->authed) {
		if (strncmp(line, "HELLO 1 ", 8) != 0 || !same(line + 8, token)) {
			fprintf(stderr, "psdoomd: %s: bad hello\n", c->peer);
			sleep(1);            /* make guessing slow */
			say(c->fd, "ERR auth\n");
			return -1;
		}
		c->authed = 1;
		handle("HELLO", out, sizeof out - 1);
		strcat(out, "\n");
		say(c->fd, out);
		fprintf(stderr, "psdoomd: %s: client in (tcp)\n", c->peer);
		return 0;
	}
	if (strcmp(line, "QUIT") == 0)
		return -1;
	if (strcmp(line, "LIST") == 0) {
		size_t i, n = build_list(list_lines, MAX_LIST);

		for (i = 0; i < n; i++) {
			say(c->fd, list_lines[i]);
			say(c->fd, "\n");
		}
		say(c->fd, ".\n");
		return 0;
	}
	handle(line, out, sizeof out - 1);
	strcat(out, "\n");
	say(c->fd, out);
	return 0;
}

/* Read what is there; handle complete lines. -1: drop the client. */
static int tcp_readable(tcp_client *c)
{
	ssize_t n = recv(c->fd, c->buf + c->len, sizeof c->buf - c->len, 0);
	char *nl;

	if (n <= 0)
		return -1;
	c->len += (size_t)n;
	c->last = time(NULL);
	while ((nl = memchr(c->buf, '\n', c->len)) != NULL) {
		char line[LINE_MAX_];
		size_t l = (size_t)(nl - c->buf);

		memcpy(line, c->buf, l);
		line[l] = '\0';
		if (l > 0 && line[l - 1] == '\r')
			line[l - 1] = '\0';
		memmove(c->buf, nl + 1, c->len - l - 1);
		c->len -= l + 1;
		if (tcp_line(c, line) != 0)
			return -1;
	}
	return c->len == sizeof c->buf ? -1 : 0;
}

/* ---- UDP: "PSD1 <token> <id> <command>" -> "PSD1 <id> <answer>" -------
 *
 * Every datagram carries the token. LIST is paged: "LIST <snap> <offset>"
 * answers "LIST <next>" (or "LIST ." at the end) plus the lines that fit;
 * pages of one <snap> come from the same poll. A repeated <id> gets the
 * cached answer again, so a lost reply never runs a SIGNAL twice. */

typedef struct {
	struct sockaddr_storage addr;
	socklen_t alen;
	long last_id;
	char last_reply[UDP_PAGE + 128];
	size_t last_len;
	long snap;
	char (*lines)[LINE_MAX_];
	size_t nlines;
	time_t bad_until;            /* ignored after a wrong token */
	time_t seen;
} udp_peer;

static udp_peer peers[UDP_PEERS];

static udp_peer *peer_of(const struct sockaddr_storage *a, socklen_t alen)
{
	udp_peer *p, *oldest = &peers[0];
	size_t i;

	for (i = 0; i < UDP_PEERS; i++) {
		p = &peers[i];
		if (p->alen == alen && memcmp(&p->addr, a, alen) == 0)
			return p;
		if (p->seen < oldest->seen)
			oldest = p;
	}
	free(oldest->lines);
	memset(oldest, 0, sizeof *oldest);
	oldest->addr = *a;
	oldest->alen = alen;
	oldest->last_id = -1;
	oldest->snap = -1;
	return oldest;
}

static void udp_readable(int ufd)
{
	char req[LINE_MAX_], reply[UDP_PAGE + 128], tok[160], cmd[LINE_MAX_], host[64] = "?";
	struct sockaddr_storage from;
	socklen_t flen = sizeof from;
	ssize_t n = recvfrom(ufd, req, sizeof req - 1, 0, (struct sockaddr *)&from, &flen);
	long id;
	int used = 0;
	udp_peer *p;
	time_t now = time(NULL);

	if (n <= 0)
		return;
	req[n] = '\0';
	if (req[n - 1] == '\n')
		req[n - 1] = '\0';
	if (sscanf(req, "PSD1 %159s %ld %n", tok, &id, &used) < 2 || used == 0)
		return;                  /* not ours: stay silent */
	snprintf(cmd, sizeof cmd, "%s", req + used);
	p = peer_of(&from, flen);
	p->seen = now;
	if (now < p->bad_until)
		return;
	if (!same(tok, token)) {
		getnameinfo((struct sockaddr *)&from, flen, host, sizeof host, NULL, 0, NI_NUMERICHOST);
		fprintf(stderr, "psdoomd: %s: bad token (udp)\n", host);
		p->bad_until = now + 2;
		snprintf(reply, sizeof reply, "PSD1 %ld ERR auth\n", id);
		sendto(ufd, reply, strlen(reply), 0, (struct sockaddr *)&from, flen);
		return;
	}
	if (id == p->last_id && p->last_len > 0) {
		sendto(ufd, p->last_reply, p->last_len, 0, (struct sockaddr *)&from, flen);
		return;
	}

	if (strncmp(cmd, "LIST ", 5) == 0) {
		long snap;
		size_t off, i, len;

		if (sscanf(cmd + 5, "%ld %zu", &snap, &off) != 2) {
			snprintf(reply, sizeof reply, "PSD1 %ld ERR syntax\n", id);
		} else {
			if (snap != p->snap || p->lines == NULL) {
				if (p->lines == NULL)
					p->lines = malloc(sizeof(char[LINE_MAX_]) * MAX_LIST);
				p->nlines = p->lines ? build_list(p->lines, MAX_LIST) : 0;
				p->snap = snap;
			}
			/* header first, with room for the widest "next" */
			snprintf(reply, sizeof reply, "PSD1 %ld LIST %-10s\n", id, ".");
			len = strlen(reply);
			for (i = off; i < p->nlines; i++) {
				size_t l = strlen(p->lines[i]);
				if (len + l + 1 > UDP_PAGE)
					break;
				memcpy(reply + len, p->lines[i], l);
				reply[len + l] = '\n';
				len += l + 1;
			}
			reply[len] = '\0';
			if (i < p->nlines) {
				char next[16];
				char *h = strstr(reply, " LIST ") + 6;

				snprintf(next, sizeof next, "%-10zu", i);
				memcpy(h, next, 10);
			}
		}
	} else if (strcmp(cmd, "QUIT") == 0) {
		return;
	} else {
		char out[LINE_MAX_];

		handle(cmd, out, sizeof out);
		snprintf(reply, sizeof reply, "PSD1 %ld %s\n", id, out);
		if (strcmp(cmd, "HELLO") == 0) {
			getnameinfo((struct sockaddr *)&from, flen, host, sizeof host, NULL, 0, NI_NUMERICHOST);
			fprintf(stderr, "psdoomd: %s: client in (udp)\n", host);
		}
	}
	p->last_id = id;
	p->last_len = strlen(reply);
	memcpy(p->last_reply, reply, p->last_len);
	sendto(ufd, reply, p->last_len, 0, (struct sockaddr *)&from, flen);
}

/* ---- setup ----------------------------------------------------------- */

static int read_token_file(const char *path, char *out, size_t len)
{
	FILE *f = fopen(path, "r");
	char *nl;

	if (f == NULL || fgets(out, (int)len, f) == NULL) {
		if (f)
			fclose(f);
		return -1;
	}
	fclose(f);
	if ((nl = strpbrk(out, "\r\n")) != NULL)
		*nl = '\0';
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
	        "usage: PSDOOMD_TOKEN=<secret> psdoomd -l ADDR:PORT [-k TOKENFILE] [libpsd options]\n"
	        "  -l ADDR:PORT   listen here, TCP and UDP (an explicit address: no default)\n"
	        "  -k FILE        read the token from FILE instead of $PSDOOMD_TOKEN\n"
	        "  libpsd options as for the game: -psclassic, -psuser, -psallusers,\n"
	        "  -psprotect GLOB, -pslog PATH, -nopsact, -psbackend cmd, -psconf PATH ...\n");
}

static int open_socket(const char *host, const char *port, int type)
{
	struct addrinfo hints, *res;
	int fd, one = 1;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = type;
	hints.ai_flags = AI_PASSIVE;
	if (getaddrinfo(host, port, &hints, &res) != 0)
		return -1;
	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) != 0 ||
	    bind(fd, res->ai_addr, res->ai_addrlen) != 0 ||
	    (type == SOCK_STREAM && listen(fd, 1) != 0)) {
		if (fd >= 0)
			close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	return fd;
}

int main(int argc, char **argv)
{
	const char *listen_at = NULL, *tokfile = NULL, *env;
	static char tokbuf[256];
	char host[128], *colon;
	psd_config *cfg;
	tcp_client cl;
	int err, i, lfd, ufd;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-l") == 0 && i + 1 < argc)
			listen_at = argv[++i];
		else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc)
			tokfile = argv[++i];
		else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			usage();
			return 0;
		}
	}
	if (listen_at == NULL || (colon = strrchr(listen_at, ':')) == NULL || colon == listen_at) {
		usage();
		return 2;
	}
	if (tokfile != NULL) {
		if (read_token_file(tokfile, tokbuf, sizeof tokbuf) != 0) {
			fprintf(stderr, "psdoomd: cannot read %s\n", tokfile);
			return 2;
		}
	} else if ((env = getenv("PSDOOMD_TOKEN")) != NULL) {
		snprintf(tokbuf, sizeof tokbuf, "%s", env);
	}
	if (strlen(tokbuf) < 12 || strpbrk(tokbuf, " \t") != NULL) {
		fprintf(stderr, "psdoomd: need a token of 12+ characters, no spaces "
		        "($PSDOOMD_TOKEN or -k FILE)\n");
		return 2;
	}
	token = tokbuf;
	for (i = 1; i < argc; i++)                          /* never a relay */
		if (strcmp(argv[i], "-psnet") == 0 ||
		    (strcmp(argv[i], "-psbackend") == 0 && i + 1 < argc && strcmp(argv[i + 1], "net") == 0)) {
			fprintf(stderr, "psdoomd: will not serve another psdoomd\n");
			return 2;
		}

	cfg = psd_config_new();
	psd_config_load_default_file(cfg);
	psd_config_parse_args(cfg, argc, argv);
	psd_config_set_show_protected(cfg, 1);            /* clients see them, can't kill them */
	psd_config_set_max_procs(cfg, 0);                 /* the client caps */
	ctx = psd_open(cfg, &err);
	psd_config_free(cfg);
	if (ctx == NULL) {
		fprintf(stderr, "psdoomd: %s\n", psd_strerror(err));
		return 1;
	}

	snprintf(host, sizeof host, "%.*s", (int)(colon - listen_at), listen_at);
	lfd = open_socket(host, colon + 1, SOCK_STREAM);
	ufd = open_socket(host, colon + 1, SOCK_DGRAM);
	if (lfd < 0 || ufd < 0) {
		fprintf(stderr, "psdoomd: cannot listen on %s: %s\n", listen_at, strerror(errno));
		return 1;
	}
	signal(SIGPIPE, SIG_IGN);
	fprintf(stderr, "psdoomd %s: %s mode, %s backend, listening on %s (tcp, udp)\n",
	        PSD_VERSION, psd_mode_of(ctx) == PSD_MODE_CLASSIC ? "classic" : "modern",
	        psd_backend_name(ctx), listen_at);

	memset(&cl, 0, sizeof cl);
	cl.fd = -1;
	for (;;) {
		fd_set set;
		struct timeval tv = { 1, 0 };
		int maxfd = lfd > ufd ? lfd : ufd;

		FD_ZERO(&set);
		FD_SET(ufd, &set);
		if (cl.fd >= 0) {
			FD_SET(cl.fd, &set);
			if (cl.fd > maxfd)
				maxfd = cl.fd;
		} else {
			FD_SET(lfd, &set);
		}
		if (select(maxfd + 1, &set, NULL, NULL, &tv) < 0) {
			if (errno != EINTR)
				sleep(1);
			continue;
		}
		if (FD_ISSET(ufd, &set))
			udp_readable(ufd);
		if (cl.fd < 0 && FD_ISSET(lfd, &set)) {
			struct sockaddr_storage peer;
			socklen_t plen = sizeof peer;
			int fd = accept(lfd, (struct sockaddr *)&peer, &plen);

			if (fd >= 0) {
				memset(&cl, 0, sizeof cl);
				cl.fd = fd;
				cl.last = time(NULL);
				snprintf(cl.peer, sizeof cl.peer, "?");
				getnameinfo((struct sockaddr *)&peer, plen, cl.peer, sizeof cl.peer,
				            NULL, 0, NI_NUMERICHOST);
			}
		} else if (cl.fd >= 0) {
			int drop = 0;

			if (FD_ISSET(cl.fd, &set))
				drop = tcp_readable(&cl) != 0;
			else if (time(NULL) - cl.last > (cl.authed ? IDLE_SECS : 10))
				drop = 1;
			if (drop) {
				close(cl.fd);
				cl.fd = -1;
				if (cl.authed)
					fprintf(stderr, "psdoomd: %s: client out\n", cl.peer);
			}
		}
	}
}
