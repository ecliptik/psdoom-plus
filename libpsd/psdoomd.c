/*
 * psdoomd - serve this machine's processes to a remote psdoom-plus
 *
 *   PSDOOMD_TOKEN=<secret> psdoomd -l ADDR:PORT [libpsd options]
 *
 * For a DOS PC (or any other machine) running psdoom-plus with
 * -psnet HOST:PORT. psdoomd uses libpsd's native backend (or the psdoom-ng
 * hooks with -psbackend cmd, e.g. containers through a helper), so this host's
 * own rules apply: its mode (-psclassic, else modern) is the floor for
 * every client, its user selection (-psuser, -psallusers, ...) decides
 * which processes are offered, and its audit log records every action.
 * The protocol is described in src/backend_net.c.
 *
 * It listens only on the address it is given (no wildcard default), needs
 * a token of at least 12 characters from $PSDOOMD_TOKEN or -k FILE (never
 * on the command line, where ps would show it), and serves one client at a
 * time. The token travels in clear text: run it between test machines on a
 * network you trust, never on one you care about.
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

#define IDLE_SECS   300          /* drop a silent client */
#define LINE_MAX_   512

static psd_ctx *ctx;
static long listed[4096];
static size_t nlisted;

static int collect(void *user, psd_event ev, const psd_proc *p)
{
	(void)user;
	if (ev == PSD_EV_REMOVE)
		return 0;
	if (nlisted < sizeof listed / sizeof listed[0])
		listed[nlisted++] = p->pid;
	return 1;                    /* track it, so SIGNAL/RENICE know it */
}

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

static void clean(char *s)
{
	for (; *s; s++)
		if (*s == '\n' || *s == '\r' || *s == '\t')
			*s = ' ';
}

static void do_list(int fd)
{
	char line[LINE_MAX_];
	size_t i;

	nlisted = 0;
	psd_poll(ctx, collect, NULL);
	/* the event's pointers die with the poll; the tracked copies do not */
	for (i = 0; i < nlisted; i++) {
		const psd_proc *p = psd_lookup(ctx, listed[i]);
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
		clean(name);
		clean(user);
		for (f = user; *f; f++)
			if (*f == ' ')
				*f = '_';
		snprintf(line, sizeof line, "%ld %ld %ld %llu %s %d %lu %s %s\n", p->pid, p->ppid,
		         p->uid, p->start_id, flags, p->nice, p->rss_kb, user, name);
		say(fd, line);
	}
	say(fd, ".\n");
}

static const char *err_word(int rc)
{
	switch (rc) {
	case PSD_OK:            return "OK\n";
	case PSD_ERR_PROTECTED: return "ERR protected\n";
	case PSD_ERR_GONE:      return "ERR gone\n";
	case PSD_ERR_UNKNOWN:   return "ERR unknown\n";
	default:                return errno == EPERM ? "ERR eperm\n" : "ERR error\n";
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

/* constant time, so the answer time says nothing about the token */
static int same(const char *a, const char *b)
{
	size_t la = strlen(a), lb = strlen(b), i;
	unsigned char d = (unsigned char)(la != lb);

	for (i = 0; i < la; i++)
		d |= (unsigned char)(a[i] ^ b[i % (lb ? lb : 1)]);
	return d == 0;
}

static void serve(int fd, const char *token, const char *peer)
{
	char buf[LINE_MAX_], line[LINE_MAX_];
	size_t len = 0;
	int authed = 0;

	for (;;) {
		char *nl;
		fd_set set;
		struct timeval tv;
		ssize_t n;

		while ((nl = memchr(buf, '\n', len)) == NULL) {
			if (len == sizeof buf)
				return;
			FD_ZERO(&set);
			FD_SET(fd, &set);
			tv.tv_sec = authed ? IDLE_SECS : 10;
			tv.tv_usec = 0;
			if (select(fd + 1, &set, NULL, NULL, &tv) <= 0)
				return;
			n = recv(fd, buf + len, sizeof buf - len, 0);
			if (n <= 0)
				return;
			len += (size_t)n;
		}
		*nl = '\0';
		snprintf(line, sizeof line, "%s", buf);
		memmove(buf, nl + 1, len - (size_t)(nl + 1 - buf));
		len -= (size_t)(nl + 1 - buf);
		if ((nl = strchr(line, '\r')) != NULL)
			*nl = '\0';

		if (!authed) {
			if (strncmp(line, "HELLO 1 ", 8) != 0 || !same(line + 8, token)) {
				fprintf(stderr, "psdoomd: %s: bad hello\n", peer);
				sleep(1);    /* make guessing slow */
				say(fd, "ERR auth\n");
				return;
			}
			authed = 1;
			snprintf(line, sizeof line, "OK psdoomd %s %s\n", PSD_VERSION,
			         psd_mode_of(ctx) == PSD_MODE_CLASSIC ? "classic" : "modern");
			say(fd, line);
			fprintf(stderr, "psdoomd: %s: client in\n", peer);
		} else if (strcmp(line, "LIST") == 0) {
			do_list(fd);
		} else if (strncmp(line, "SIGNAL ", 7) == 0) {
			char sig[8];
			long pid;
			psd_signal s;

			if (sscanf(line + 7, "%ld %7s", &pid, sig) != 2 || parse_sig(sig, &s) != 0) {
				say(fd, "ERR syntax\n");
				continue;
			}
			if (psd_lookup(ctx, pid) == NULL)
				psd_poll(ctx, collect, NULL);   /* not seen yet: look again */
			errno = 0;
			say(fd, err_word(psd_send(ctx, pid, s)));
		} else if (strncmp(line, "RENICE ", 7) == 0) {
			long pid;
			int nice;

			if (sscanf(line + 7, "%ld %d", &pid, &nice) != 2) {
				say(fd, "ERR syntax\n");
				continue;
			}
			errno = 0;
			say(fd, err_word(psd_renice(ctx, pid, nice)));
		} else if (strcmp(line, "QUIT") == 0) {
			return;
		} else {
			say(fd, "ERR syntax\n");
		}
	}
}

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
	        "  -l ADDR:PORT   listen here (an explicit address: there is no default)\n"
	        "  -k FILE        read the token from FILE instead of $PSDOOMD_TOKEN\n"
	        "  libpsd options as for the game: -psclassic, -psuser, -psallusers,\n"
	        "  -psprotect GLOB, -pslog PATH, -nopsact, -psconf PATH ...\n");
}

int main(int argc, char **argv)
{
	const char *listen_at = NULL, *tokfile = NULL, *env;
	char token[256] = "", host[128], *colon;
	struct addrinfo hints, *res;
	psd_config *cfg;
	int err, i, lfd, one = 1;

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
		if (read_token_file(tokfile, token, sizeof token) != 0) {
			fprintf(stderr, "psdoomd: cannot read %s\n", tokfile);
			return 2;
		}
	} else if ((env = getenv("PSDOOMD_TOKEN")) != NULL) {
		snprintf(token, sizeof token, "%s", env);
	}
	if (strlen(token) < 12 || strchr(token, ' ') != NULL) {
		fprintf(stderr, "psdoomd: need a token of 12+ characters, no spaces "
		        "($PSDOOMD_TOKEN or -k FILE)\n");
		return 2;
	}

	cfg = psd_config_new();
	psd_config_load_default_file(cfg);
	psd_config_parse_args(cfg, argc, argv);
	for (i = 1; i < argc; i++)                          /* never a relay */
		if (strcmp(argv[i], "-psnet") == 0 ||
		    (strcmp(argv[i], "-psbackend") == 0 && i + 1 < argc && strcmp(argv[i + 1], "net") == 0)) {
			fprintf(stderr, "psdoomd: will not serve another psdoomd\n");
			return 2;
		}
	psd_config_set_show_protected(cfg, 1);            /* clients see them, can't kill them */
	psd_config_set_max_procs(cfg, 0);                 /* the client caps */
	ctx = psd_open(cfg, &err);
	psd_config_free(cfg);
	if (ctx == NULL) {
		fprintf(stderr, "psdoomd: %s\n", psd_strerror(err));
		return 1;
	}

	snprintf(host, sizeof host, "%.*s", (int)(colon - listen_at), listen_at);
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if (getaddrinfo(host, colon + 1, &hints, &res) != 0) {
		fprintf(stderr, "psdoomd: cannot resolve %s\n", listen_at);
		return 1;
	}
	lfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (lfd < 0 || setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) != 0 ||
	    bind(lfd, res->ai_addr, res->ai_addrlen) != 0 || listen(lfd, 1) != 0) {
		fprintf(stderr, "psdoomd: cannot listen on %s: %s\n", listen_at, strerror(errno));
		return 1;
	}
	freeaddrinfo(res);
	signal(SIGPIPE, SIG_IGN);
	fprintf(stderr, "psdoomd %s: %s mode, %s backend, listening on %s\n", PSD_VERSION,
	        psd_mode_of(ctx) == PSD_MODE_CLASSIC ? "classic" : "modern",
	        psd_backend_name(ctx), listen_at);

	for (;;) {
		struct sockaddr_storage peer;
		socklen_t plen = sizeof peer;
		char pname[64] = "?";
		int fd = accept(lfd, (struct sockaddr *)&peer, &plen);

		if (fd < 0) {
			if (errno == EINTR)
				continue;
			sleep(1);
			continue;
		}
		getnameinfo((struct sockaddr *)&peer, plen, pname, sizeof pname, NULL, 0, NI_NUMERICHOST);
		serve(fd, token, pname);
		close(fd);
		psd_reset(ctx);
		fprintf(stderr, "psdoomd: %s: client out\n", pname);
	}
}
