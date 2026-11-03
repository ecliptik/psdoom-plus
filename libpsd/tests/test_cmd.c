/*
 * test_cmd.c - psdoom-ng compatible hooks (PSDOOM_NG_*_CMD)
 *
 * A fake helper serves a process list from a file and records kills;
 * nothing real is signalled.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "psd.h"
#include "psd_test.h"

static char dir[256];
static char state[300], killed[300], reniced[300], helper[300];

typedef struct rec {
	int spawn, remove;
	long spawned[64];
	int nspawned;
	long removed[64];
	int nremoved;
} rec;

static int on_event(void *user, psd_event ev, const psd_proc *p)
{
	rec *r = user;

	if (ev == PSD_EV_SPAWN) {
		r->spawn++;
		if (r->nspawned < 64)
			r->spawned[r->nspawned++] = p->pid;
	} else if (ev == PSD_EV_REMOVE) {
		r->remove++;
		if (r->nremoved < 64)
			r->removed[r->nremoved++] = p->pid;
	}
	return 1;
}

static int has(const long *l, int n, long pid)
{
	int i;

	for (i = 0; i < n; i++)
		if (l[i] == pid)
			return 1;
	return 0;
}

static void msleep(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void write_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");

	fputs(text, f);
	fclose(f);
}

static int file_has(const char *path, const char *needle)
{
	char buf[4096];
	size_t n;
	FILE *f = fopen(path, "r");

	if (f == NULL)
		return 0;
	n = fread(buf, 1, sizeof buf - 1, f);
	buf[n] = '\0';
	fclose(f);
	return strstr(buf, needle) != NULL;
}

static void setup(void)
{
	char cmd[700];

	snprintf(state, sizeof state, "%s/state", dir);
	snprintf(killed, sizeof killed, "%s/killed", dir);
	snprintf(reniced, sizeof reniced, "%s/reniced", dir);
	snprintf(helper, sizeof helper, "%s/helper.sh", dir);
	write_file(helper,
	           "#!/bin/sh\n"
	           "# fake psdoom-ng helper: ps | renice <pid> | kill <pid>\n"
	           "case \"$1\" in\n"
	           "ps)     [ -n \"$DELAY\" ] && sleep \"$DELAY\"; cat \"$STATE\" ;;\n"
	           "renice) echo \"$2 $PSDOOM_NICE\" >> \"$RENICED\" ;;\n"
	           "kill)   echo \"$2 $PSDOOM_SIGNAL\" >> \"$KILLED\"\n"
	           "        grep -v \" $2 \" \"$STATE\" > \"$STATE.new\"; mv \"$STATE.new\" \"$STATE\" ;;\n"
	           "esac\n");
	setenv("STATE", state, 1);
	setenv("KILLED", killed, 1);
	setenv("RENICED", reniced, 1);
	snprintf(cmd, sizeof cmd, "sh %s ps", helper);
	setenv("PSDOOM_NG_PS_CMD", cmd, 1);
	snprintf(cmd, sizeof cmd, "sh %s renice", helper);
	setenv("PSDOOM_NG_RENICE_CMD", cmd, 1);
	snprintf(cmd, sizeof cmd, "sh %s kill", helper);
	setenv("PSDOOM_NG_KILL_CMD", cmd, 1);
	setenv("PSDOOMUSER", "keymon", 1);
}

static psd_ctx *open_cmd(psd_mode mode)
{
	psd_config *cfg = psd_config_new();
	psd_ctx *ctx;
	int err;

	psd_config_set_backend(cfg, PSD_BACKEND_CMD);
	psd_config_set_mode(cfg, mode);
	psd_config_set_log(cfg, "off");
	ctx = psd_open(cfg, &err);
	psd_config_free(cfg);
	if (ctx == NULL)
		fprintf(stderr, "psd_open: %s\n", psd_strerror(err));
	return ctx;
}

/* the psdoom-ng README's own example */
static void parses_psdoom_ng_format(void)
{
	rec r = { 0 };
	psd_ctx *ctx;
	const psd_proc *p;

	write_file(state,
	           "keymon 29 web4 1\n"
	           "keymon 30 web3 1\n"
	           "keymon 31 my app 0\n"
	           "other 32 core15 1\n"
	           "garbage line\n");
	ctx = open_cmd(PSD_MODE_CLASSIC);
	CHECK_EQ(psd_poll(ctx, on_event, &r), 3);
	p = psd_lookup(ctx, 31);
	CHECK(p != NULL);
	if (p) {
		CHECK_EQ(strcmp(p->name, "my app"), 0);
		CHECK_EQ(p->is_daemon, 0);
	}
	CHECK(psd_lookup(ctx, 29) && psd_lookup(ctx, 29)->is_daemon == 1);
	CHECK(psd_lookup(ctx, 32) == NULL);          /* other user */
	psd_close(ctx);
}

static void kill_and_renice_reach_the_helper(void)
{
	rec r = { 0 };
	psd_ctx *ctx;
	long long until;

	write_file(state, "keymon 29 web4 1\nkeymon 30 web3 1\n");
	remove(killed);
	remove(reniced);
	ctx = open_cmd(PSD_MODE_CLASSIC);
	psd_poll(ctx, on_event, &r);
	CHECK_EQ(psd_wound(ctx, 30, 50, 100), PSD_OK);
	CHECK_EQ(psd_kill(ctx, 29), PSD_OK);

	until = now_ms() + 5000;
	while (now_ms() < until && !has(r.removed, r.nremoved, 29)) {
		psd_tick(ctx);
		psd_poll(ctx, on_event, &r);
		msleep(50);
	}
	CHECK(file_has(killed, "29 KILL"));
	CHECK(file_has(reniced, "30 5"));
	CHECK(has(r.removed, r.nremoved, 29));
	CHECK(!has(r.removed, r.nremoved, 30));
	psd_close(ctx);
}

static void slow_helper_does_not_stall(void)
{
	rec r = { 0 };
	psd_ctx *ctx;
	long long t0, worst = 0;
	int i;

	write_file(state, "keymon 40 slowpoke 1\n");
	setenv("DELAY", "1", 1);
	ctx = open_cmd(PSD_MODE_MODERN);
	psd_poll(ctx, on_event, &r);                 /* the first one may wait */
	CHECK(has(r.spawned, r.nspawned, 40));
	for (i = 0; i < 20; i++) {
		t0 = now_ms();
		psd_tick(ctx);
		psd_poll(ctx, on_event, &r);
		if (now_ms() - t0 > worst)
			worst = now_ms() - t0;
		msleep(50);
	}
	printf("  slowest poll with a 1 s helper: %lld ms\n", worst);
	CHECK(worst < 100);
	unsetenv("DELAY");
	psd_close(ctx);
}

static void missing_hook_is_unavailable(void)
{
	psd_config *cfg = psd_config_new();
	char *saved = getenv("PSDOOM_NG_PS_CMD");
	char keep[700];
	int err = 0;

	snprintf(keep, sizeof keep, "%s", saved ? saved : "");
	unsetenv("PSDOOM_NG_PS_CMD");
	psd_config_set_backend(cfg, PSD_BACKEND_CMD);
	CHECK(psd_open(cfg, &err) == NULL);
	CHECK_EQ(err, PSD_ERR_BACKEND);
	setenv("PSDOOM_NG_PS_CMD", keep, 1);
	psd_config_free(cfg);
}

int main(void)
{
	const char *tmp = getenv("TMPDIR");

	snprintf(dir, sizeof dir, "%s/psdtest.XXXXXX", tmp && *tmp ? tmp : "/tmp");
	if (mkdtemp(dir) == NULL) {
		perror("mkdtemp");
		return 1;
	}
	setup();
	RUN(parses_psdoom_ng_format);
	RUN(kill_and_renice_reach_the_helper);
	RUN(slow_helper_does_not_stall);
	RUN(missing_hook_is_unavailable);

	remove(state);
	remove(killed);
	remove(reniced);
	remove(helper);
	rmdir(dir);
	TEST_DONE();
}
