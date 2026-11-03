/*
 * test_core.c - libpsd core logic against the mock backend
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include "psd.h"
#include "psd_test.h"

/* ---- event recorder ---- */

typedef struct rec {
	int spawn, update, resurrect, remove;
	long spawned[256];
	int nspawned;
	long removed[256];
	int nremoved;
	long refuse_pid;             /* refuse to place this pid once */
	int refuse_all;
	int protected_seen;
} rec;

static int on_event(void *user, psd_event ev, const psd_proc *p)
{
	rec *r = user;

	switch (ev) {
	case PSD_EV_SPAWN:
		if (r->refuse_all)
			return 0;
		if (r->refuse_pid == p->pid) {
			r->refuse_pid = 0;
			return 0;
		}
		r->spawn++;
		if (r->nspawned < 256)
			r->spawned[r->nspawned++] = p->pid;
		if (p->is_protected)
			r->protected_seen++;
		return 1;
	case PSD_EV_UPDATE:
		r->update++;
		return 1;
	case PSD_EV_RESURRECT:
		r->resurrect++;
		return 1;
	case PSD_EV_REMOVE:
		r->remove++;
		if (r->nremoved < 256)
			r->removed[r->nremoved++] = p->pid;
		return 1;
	}
	return 0;
}

static int has(const long *list, int n, long pid)
{
	int i;

	for (i = 0; i < n; i++)
		if (list[i] == pid)
			return 1;
	return 0;
}

/*
 * A small world:
 *   1 init, 2 kthreadd, 50 sshd, 60 cron (root)
 *   900 Xorg (root)
 *   998 foot -> 999 -bash -> 1000 psdoom (us) -> ... (alice)
 *   1100 sleep, 1101 pipewire (alice)
 *   1200 vim, 1201 sh (bob)
 */
static void world(const char *me)
{
	psd_mock_reset();
	setenv("PSDOOMUSER", me, 1);
	psd_mock_set_self(1000);
	psd_mock_add(1, 0, 0, "root", "/sbin/init", 1, 0, PSD_MOCK_DAEMON);
	psd_mock_add(2, 0, 0, "root", "[kthreadd]", 2, 0, PSD_MOCK_DAEMON | PSD_MOCK_KERNEL);
	psd_mock_add(50, 1, 0, "root", "/usr/sbin/sshd", 50, 0, PSD_MOCK_DAEMON);
	psd_mock_add(60, 1, 0, "root", "/usr/sbin/cron", 60, 0, PSD_MOCK_DAEMON);
	psd_mock_add(900, 1, 0, "root", "/usr/lib/xorg/Xorg", 900, 0, 0);
	psd_mock_add(998, 1, 1001, "alice", "/usr/bin/foot", 998, 0, 0);
	psd_mock_add(999, 998, 1001, "alice", "-bash", 999, 0, 0);
	psd_mock_add(1000, 999, 1001, "alice", "/usr/games/psdoom", 1000, 0, 0);
	psd_mock_add(1100, 999, 1001, "alice", "/usr/bin/sleep", 1100, 0, 0);
	psd_mock_add(1101, 1, 1001, "alice", "/usr/bin/pipewire", 1101, 0, PSD_MOCK_DAEMON);
	psd_mock_add(1200, 1, 1002, "bob", "/usr/bin/vim", 1200, 0, 0);
	psd_mock_add(1201, 1200, 1002, "bob", "/bin/sh", 1201, 0, 0);
}

static psd_ctx *open_with(psd_mode mode, int argc, char **argv)
{
	psd_config *cfg = psd_config_new();
	psd_ctx *ctx;
	int err;

	psd_config_set_backend(cfg, PSD_BACKEND_MOCK);
	psd_config_set_mode(cfg, mode);
	psd_config_set_log(cfg, "off");
	if (argc > 0)
		psd_config_parse_args(cfg, argc, argv);
	ctx = psd_open(cfg, &err);
	psd_config_free(cfg);
	if (ctx == NULL)
		fprintf(stderr, "psd_open: %s\n", psd_strerror(err));
	return ctx;
}

/* ---- tests ---- */

static void classic_default_is_my_processes(void)
{
	rec r = { 0 };
	psd_ctx *ctx;

	world("alice");
	ctx = open_with(PSD_MODE_CLASSIC, 0, NULL);
	CHECK(ctx != NULL);
	CHECK_EQ(psd_poll(ctx, on_event, &r), 5);
	CHECK(has(r.spawned, r.nspawned, 1000));     /* classic shows itself */
	CHECK(has(r.spawned, r.nspawned, 999));
	CHECK(!has(r.spawned, r.nspawned, 1200));    /* bob */
	CHECK(!has(r.spawned, r.nspawned, 50));      /* root */
	psd_close(ctx);
}

static void classic_root_sees_everyone(void)
{
	rec r = { 0 };
	psd_ctx *ctx;

	world("root");
	ctx = open_with(PSD_MODE_CLASSIC, 0, NULL);
	CHECK_EQ(psd_poll(ctx, on_event, &r), 12);
	CHECK(has(r.spawned, r.nspawned, 2));        /* even kernel threads */
	psd_close(ctx);
}

static void psuser_and_psnotuser(void)
{
	char *a1[] = { "doom", "-psuser", "bob", "-nomonsters" };
	char *a2[] = { "doom", "-psallusers", "-psnotuser", "bob", "root" };
	char *a3[] = { "doom", "-psallusers", "-psnotuser" };   /* no names: me */
	rec r1 = { 0 }, r2 = { 0 }, r3 = { 0 };
	psd_ctx *ctx;

	world("alice");
	ctx = open_with(PSD_MODE_CLASSIC, 4, a1);
	CHECK_EQ(psd_poll(ctx, on_event, &r1), 2);
	CHECK(has(r1.spawned, r1.nspawned, 1201));
	psd_close(ctx);

	ctx = open_with(PSD_MODE_CLASSIC, 5, a2);
	CHECK_EQ(psd_poll(ctx, on_event, &r2), 5);   /* alice's only */
	psd_close(ctx);

	ctx = open_with(PSD_MODE_CLASSIC, 3, a3);
	CHECK_EQ(psd_poll(ctx, on_event, &r3), 7);   /* everyone but alice */
	CHECK(!has(r3.spawned, r3.nspawned, 1100));
	psd_close(ctx);
}

static void modern_protects_session(void)
{
	char *show[] = { "doom", "-psshowprotected" };
	rec r = { 0 }, rs = { 0 };
	psd_ctx *ctx;

	world("root");
	ctx = open_with(PSD_MODE_MODERN, 0, NULL);
	CHECK_EQ(psd_poll(ctx, on_event, &r), 5);
	CHECK(has(r.spawned, r.nspawned, 1100));     /* sleep */
	CHECK(has(r.spawned, r.nspawned, 1101));     /* pipewire: audio is fair game */
	CHECK(has(r.spawned, r.nspawned, 60));       /* cron */
	CHECK(!has(r.spawned, r.nspawned, 1));       /* init */
	CHECK(!has(r.spawned, r.nspawned, 2));       /* kernel thread */
	CHECK(!has(r.spawned, r.nspawned, 50));      /* sshd */
	CHECK(!has(r.spawned, r.nspawned, 900));     /* Xorg */
	CHECK(!has(r.spawned, r.nspawned, 1000));    /* ourselves */
	CHECK(!has(r.spawned, r.nspawned, 999));     /* our shell */
	CHECK(!has(r.spawned, r.nspawned, 998));     /* our terminal */
	CHECK_EQ(psd_kill(ctx, 1000), PSD_ERR_UNKNOWN);
	psd_close(ctx);

	world("root");
	ctx = open_with(PSD_MODE_MODERN, 2, show);
	CHECK_EQ(psd_poll(ctx, on_event, &rs), 12);
	CHECK_EQ(rs.protected_seen, 7);
	CHECK_EQ(psd_kill(ctx, 1000), PSD_ERR_PROTECTED);
	CHECK_EQ(psd_kill(ctx, 50), PSD_ERR_PROTECTED);
	CHECK_EQ(psd_wound(ctx, 900, 10, 100), PSD_ERR_PROTECTED);
	CHECK_EQ(psd_mock_signal_count(), 0);
	psd_close(ctx);
}

static void modern_extra_protect_globs(void)
{
	char *a[] = { "doom", "-psprotect", "vi*" };
	rec r = { 0 };
	psd_ctx *ctx;

	world("root");
	ctx = open_with(PSD_MODE_MODERN, 3, a);
	psd_poll(ctx, on_event, &r);
	CHECK(!has(r.spawned, r.nspawned, 1200));
	psd_close(ctx);
}

static void classic_kill_then_remove(void)
{
	rec r = { 0 };
	psd_ctx *ctx;

	world("alice");
	ctx = open_with(PSD_MODE_CLASSIC, 0, NULL);
	psd_poll(ctx, on_event, &r);
	CHECK_EQ(psd_kill(ctx, 1100), PSD_OK);
	CHECK_EQ(psd_mock_last_signal(1100), PSD_SIG_KILL);
	psd_poll(ctx, on_event, &r);
	CHECK(has(r.removed, r.nremoved, 1100));
	CHECK_EQ(r.resurrect, 0);
	/* classic happily kills itself too */
	CHECK_EQ(psd_kill(ctx, 1000), PSD_OK);
	CHECK_EQ(psd_mock_last_signal(1000), PSD_SIG_KILL);
	psd_close(ctx);
}

static void survivor_is_resurrected(void)
{
	rec r = { 0 };
	psd_ctx *ctx;

	world("alice");
	psd_mock_add(1300, 1, 1001, "alice", "/usr/bin/stubborn", 1300, 0, PSD_MOCK_IMMORTAL);
	ctx = open_with(PSD_MODE_CLASSIC, 0, NULL);
	psd_poll(ctx, on_event, &r);
	CHECK(psd_kill(ctx, 1300) != PSD_OK);        /* EPERM */
	psd_poll(ctx, on_event, &r);
	CHECK_EQ(r.resurrect, 1);
	CHECK(!has(r.removed, r.nremoved, 1300));
	psd_close(ctx);
}

static void modern_term_then_kill(void)
{
	rec r = { 0 };
	psd_ctx *ctx;

	world("alice");
	psd_mock_add(1400, 1, 1001, "alice", "/usr/bin/stubborn", 1400, 0, PSD_MOCK_IGNORE_TERM);
	ctx = open_with(PSD_MODE_MODERN, 0, NULL);
	psd_poll(ctx, on_event, &r);
	CHECK_EQ(psd_kill(ctx, 1400), PSD_OK);
	CHECK_EQ(psd_mock_last_signal(1400), PSD_SIG_TERM);

	psd_mock_advance_ms(1000);
	psd_poll(ctx, on_event, &r);                 /* inside the grace period */
	CHECK_EQ(r.resurrect, 0);
	CHECK(!has(r.removed, r.nremoved, 1400));

	psd_mock_advance_ms(2500);
	psd_tick(ctx);
	CHECK_EQ(psd_mock_last_signal(1400), PSD_SIG_KILL);
	psd_poll(ctx, on_event, &r);
	CHECK(has(r.removed, r.nremoved, 1400));
	psd_close(ctx);
}

static void modern_term_is_enough(void)
{
	rec r = { 0 };
	psd_ctx *ctx;

	world("alice");
	ctx = open_with(PSD_MODE_MODERN, 0, NULL);
	psd_poll(ctx, on_event, &r);
	psd_kill(ctx, 1100);
	psd_poll(ctx, on_event, &r);
	CHECK(has(r.removed, r.nremoved, 1100));
	psd_mock_advance_ms(5000);
	psd_tick(ctx);
	CHECK_EQ(psd_mock_signal_count(), 1);        /* no KILL after a clean exit */
	psd_close(ctx);
}

static void refused_spawn_is_offered_again(void)
{
	rec r = { 0 };
	psd_ctx *ctx;

	world("alice");
	r.refuse_pid = 1100;
	ctx = open_with(PSD_MODE_CLASSIC, 0, NULL);
	psd_poll(ctx, on_event, &r);
	CHECK(!has(r.spawned, r.nspawned, 1100));
	psd_poll(ctx, on_event, &r);
	CHECK(has(r.spawned, r.nspawned, 1100));
	psd_close(ctx);
}

static void pid_reuse_is_a_new_process(void)
{
	rec r = { 0 };
	psd_ctx *ctx;
	int spawns;

	world("alice");
	ctx = open_with(PSD_MODE_CLASSIC, 0, NULL);
	psd_poll(ctx, on_event, &r);
	spawns = r.spawn;
	psd_mock_remove(1100);
	psd_mock_add(1100, 1, 1001, "alice", "/usr/bin/other", 99999, 0, 0);
	psd_poll(ctx, on_event, &r);
	CHECK(has(r.removed, r.nremoved, 1100));
	CHECK_EQ(r.spawn, spawns + 1);
	CHECK_EQ(strcmp(psd_lookup(ctx, 1100)->name, "/usr/bin/other"), 0);
	psd_close(ctx);
}

static void wound_classic_and_modern(void)
{
	rec r = { 0 };
	psd_ctx *ctx;
	int before;

	world("alice");
	ctx = open_with(PSD_MODE_CLASSIC, 0, NULL);
	psd_poll(ctx, on_event, &r);
	CHECK_EQ(psd_wound(ctx, 1100, 80, 100), PSD_OK);
	CHECK_EQ(psd_mock_nice(1100), 5);
	psd_mock_set_cpu(1100, 0);
	CHECK_EQ(psd_wound(ctx, 1100, 10, 100), PSD_OK);
	CHECK_EQ(psd_mock_nice(1100), 5);            /* only the first hit renices */
	psd_close(ctx);

	world("alice");
	ctx = open_with(PSD_MODE_MODERN, 0, NULL);
	psd_poll(ctx, on_event, &r);
	psd_wound(ctx, 1100, 50, 100);
	CHECK_EQ(psd_mock_nice(1100), 10);           /* ceil(0.5 * 19) */
	before = psd_mock_nice(1100);
	psd_wound(ctx, 1100, 90, 100);
	CHECK_EQ(psd_mock_nice(1100), before);       /* never lowers */
	psd_wound(ctx, 1100, -20, 100);
	CHECK_EQ(psd_mock_nice(1100), 19);
	CHECK_EQ(psd_wound(ctx, 1100, 100, 100), PSD_OK);  /* no damage, no-op */
	psd_close(ctx);
}

static void modern_cap_prefers_busy(void)
{
	char *a[] = { "doom", "-psmax", "2" };
	rec r = { 0 };
	psd_ctx *ctx;

	world("root");
	ctx = open_with(PSD_MODE_MODERN, 3, a);
	r.refuse_all = 1;
	psd_poll(ctx, on_event, &r);                 /* baseline for cpu% */
	psd_mock_advance_ms(1000);
	psd_mock_set_cpu(1200, 100);                 /* 100% */
	psd_mock_set_cpu(1201, 50);                  /* 50% */
	psd_mock_set_cpu(1100, 10);                  /* 10% */
	r.refuse_all = 0;
	CHECK_EQ(psd_poll(ctx, on_event, &r), 2);
	CHECK(has(r.spawned, r.nspawned, 1200));
	CHECK(has(r.spawned, r.nspawned, 1201));
	CHECK(psd_lookup(ctx, 1200)->cpu_pct > 99.0 && psd_lookup(ctx, 1200)->cpu_pct < 101.0);
	psd_close(ctx);
}

static void dry_run_signals_nothing(void)
{
	char *a[] = { "doom", "-nopsact", "-pslog", "test_core_actions.log" };
	rec r = { 0 };
	psd_config *cfg;
	psd_ctx *ctx;
	char line[512];
	int saw = 0;
	FILE *f;

	remove("test_core_actions.log");
	world("alice");
	cfg = psd_config_new();
	psd_config_set_backend(cfg, PSD_BACKEND_MOCK);
	psd_config_set_mode(cfg, PSD_MODE_CLASSIC);
	psd_config_parse_args(cfg, 4, a);
	ctx = psd_open(cfg, NULL);
	psd_config_free(cfg);
	psd_poll(ctx, on_event, &r);
	CHECK_EQ(psd_kill(ctx, 1100), PSD_OK);
	CHECK_EQ(psd_wound(ctx, 1101, 50, 100), PSD_OK);
	CHECK_EQ(psd_mock_signal_count(), 0);
	CHECK_EQ(psd_mock_nice(1101), 0);
	psd_close(ctx);

	f = fopen("test_core_actions.log", "r");
	CHECK(f != NULL);
	while (f && fgets(line, sizeof line, f))
		if (strstr(line, "action=kill") && strstr(line, "pid=1100") &&
		    strstr(line, "result=dry-run") && strstr(line, "mode=classic"))
			saw = 1;
	if (f)
		fclose(f);
	CHECK(saw);
	remove("test_core_actions.log");
}

static void labels_are_last_seven(void)
{
	rec r = { 0 };
	psd_ctx *ctx;

	world("alice");
	psd_mock_add(1500, 1, 1001, "alice", "/sbin/mingetty", 1500, 0, 0);
	psd_mock_add(1501, 1, 1001, "alice", "sh", 1501, 0, 0);
	ctx = open_with(PSD_MODE_CLASSIC, 0, NULL);
	psd_poll(ctx, on_event, &r);
	CHECK_EQ(strcmp(psd_lookup(ctx, 1500)->label, "ingetty"), 0);
	CHECK_EQ(strcmp(psd_lookup(ctx, 1501)->label, "sh"), 0);
	psd_close(ctx);
}

static void confirm_root_kills_in_modern(void)
{
	rec r = { 0 };
	psd_ctx *ctx;

	world("root");
	ctx = open_with(PSD_MODE_MODERN, 0, NULL);
	psd_poll(ctx, on_event, &r);
	CHECK(psd_needs_confirm(ctx, 60));           /* root's cron */
	CHECK(!psd_needs_confirm(ctx, 1100));
	psd_close(ctx);

	world("root");
	ctx = open_with(PSD_MODE_CLASSIC, 0, NULL);
	psd_poll(ctx, on_event, &r);
	CHECK(!psd_needs_confirm(ctx, 60));
	psd_close(ctx);
}

static void reset_and_forget(void)
{
	rec r = { 0 };
	psd_ctx *ctx;

	world("alice");
	ctx = open_with(PSD_MODE_CLASSIC, 0, NULL);
	psd_poll(ctx, on_event, &r);
	psd_forget(ctx, 1100);
	CHECK(psd_lookup(ctx, 1100) == NULL);
	r.spawn = 0;
	psd_poll(ctx, on_event, &r);
	CHECK_EQ(r.spawn, 1);                        /* offered again */
	psd_reset(ctx);                              /* new level */
	r.spawn = 0;
	r.remove = 0;
	psd_poll(ctx, on_event, &r);
	CHECK_EQ(r.spawn, 5);
	CHECK_EQ(r.remove, 0);                       /* no events for the old level */
	psd_close(ctx);
}

static void nopsmon_disables(void)
{
	char *a[] = { "doom", "-nopsmon" };
	psd_config *cfg = psd_config_new();
	int err = 0;

	psd_config_set_backend(cfg, PSD_BACKEND_MOCK);
	psd_config_parse_args(cfg, 2, a);
	CHECK(psd_config_disabled(cfg));
	CHECK(psd_open(cfg, &err) == NULL);
	CHECK_EQ(err, PSD_ERR_DISABLED);
	psd_config_free(cfg);
}

int main(void)
{
	RUN(classic_default_is_my_processes);
	RUN(classic_root_sees_everyone);
	RUN(psuser_and_psnotuser);
	RUN(modern_protects_session);
	RUN(modern_extra_protect_globs);
	RUN(classic_kill_then_remove);
	RUN(survivor_is_resurrected);
	RUN(modern_term_then_kill);
	RUN(modern_term_is_enough);
	RUN(refused_spawn_is_offered_again);
	RUN(pid_reuse_is_a_new_process);
	RUN(wound_classic_and_modern);
	RUN(modern_cap_prefers_busy);
	RUN(dry_run_signals_nothing);
	RUN(labels_are_last_seven);
	RUN(confirm_root_kills_in_modern);
	RUN(reset_and_forget);
	RUN(nopsmon_disables);
	TEST_DONE();
}
