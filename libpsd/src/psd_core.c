/*
 * psd_core.c - tracking processes as monsters, kill modes, audit log
 *
 * The bookkeeping follows psDooM's pr_check()/add_to_pid_list()/
 * cleanup_pid_list(): processes that appear are spawned, processes that
 * vanish are removed without a corpse, and a process whose monster died but
 * which is still running (it trapped the signal, or we lacked permission) is
 * raised again "archvile-style".
 *
 * Copyright (C) 1999 Dennis Chao
 * Copyright (C) 2000 David Koppenhofer
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#if !defined(_POSIX_C_SOURCE) && !defined(__HAIKU__)
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <fnmatch.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psd_internal.h"

enum { T_ALIVE = 0, T_DEAD = 1 };

typedef struct track {
	psd_proc p;
	int state;
	int seen;
	int wounded;                 /* classic: renice only on the first hit of a life */
	int nice_set;                /* highest nice we applied, -100 = none */
	long long kill_deadline;     /* modern: when TERM escalates to KILL, 0 = none */
	long long killed_ms;         /* when the engine reported the monster's death */
} track;

typedef struct hist {
	long pid;
	unsigned long long start_id;
	unsigned long long ticks;
} hist;

struct psd_ctx {
	struct psd_config cfg;       /* resolved copy */
	psd_backend be;
	char whoami[PSD_USER_MAX];
	int max_procs;
	int grace_ms;

	track *tracks;
	size_t ntracks, captracks;

	hist *prev;                  /* previous snapshot, sorted by pid, for cpu% */
	size_t nprev, capprev;
	long long prev_ms;

	FILE *log;
};

/*
 * Never killable in modern mode, matched against comm and the basename of
 * argv[0]. The display stack, the session, remote access and init. Audio
 * daemons are deliberately not here: shooting PipeWire is fair game.
 */
static const char *const builtin_protect[] = {
	/* init and service managers */
	"init", "systemd", "systemd-*", "launchd", "launch_daemon", "rc.d", "runit",
	"s6-svscan", "openrc*", "kthreadd",
	/* remote access */
	"sshd", "sshd-session", "sshd:*", "sshd-auth", "mosh-server",
	/* display servers, compositors, display managers, VNC */
	"Xorg", "X", "Xwayland", "Xvfb", "Xvnc", "Xephyr", "x11vnc", "wayvnc",
	"gnome-shell", "kwin_wayland", "kwin_x11", "mutter", "sway", "labwc", "weston",
	"Hyprland", "river", "wayfire", "niri", "cage", "cosmic-comp", "xfwm4",
	"gdm*", "sddm*", "lightdm*", "xdm", "greetd", "slim",
	/* session */
	"dbus-daemon", "dbus-broker*", "login", "agetty", "getty", "mingetty",
	"polkitd", "seatd", "elogind*",
	/* terminal multiplexers (the game's terminal is caught by the parent chain) */
	"tmux", "tmux:*", "screen", "SCREEN",
	/* Haiku */
	"kernel_team", "app_server", "input_server", "registrar", "debug_server",
	"net_server",                /* Haiku's network stack: ssh sessions die with it */
	NULL
};

/* ------------------------------------------------------------------ */

static long long now_ms(psd_ctx *ctx)
{
	if (ctx->be.now_ms != NULL)
		return ctx->be.now_ms(&ctx->be);
	return psd_platform_now_ms();
}

static void audit(psd_ctx *ctx, const char *action, const psd_proc *p,
                  const char *detail, const char *result)
{
	char ts[40];

	if (ctx->log == NULL)
		return;
	psd_platform_timestamp(ts, sizeof ts);
	fprintf(ctx->log, "%s mode=%s backend=%s%s action=%s %s pid=%ld user=%s name=%s result=%s\n",
	        ts,
	        ctx->cfg.mode == PSD_MODE_CLASSIC ? "classic" : "modern",
	        ctx->be.name,
	        ctx->cfg.no_act ? " dry-run=1" : "",
	        action, detail ? detail : "",
	        p->pid, p->user[0] ? p->user : "?", p->name,
	        result);
	fflush(ctx->log);
}

static const char *result_str(int rc)
{
	if (rc == PSD_OK)
		return "ok";
	if (rc == PSD_ERR_GONE)
		return "gone";
	if (rc == PSD_ERR && errno == EPERM)
		return "eperm";
	if (rc == PSD_ERR)
		return "error";
	return psd_strerror(rc);
}

static track *find_track(psd_ctx *ctx, long pid)
{
	size_t i;

	for (i = 0; i < ctx->ntracks; i++)
		if (ctx->tracks[i].p.pid == pid)
			return &ctx->tracks[i];
	return NULL;
}

static void drop_track(psd_ctx *ctx, track *t)
{
	size_t i = (size_t)(t - ctx->tracks);

	ctx->tracks[i] = ctx->tracks[--ctx->ntracks];
}

static track *add_track(psd_ctx *ctx, const psd_proc *p)
{
	track *t;

	if (ctx->ntracks == ctx->captracks) {
		size_t cap = ctx->captracks ? ctx->captracks * 2 : 64;
		track *nt = realloc(ctx->tracks, cap * sizeof *nt);
		if (nt == NULL)
			return NULL;
		ctx->tracks = nt;
		ctx->captracks = cap;
	}
	t = &ctx->tracks[ctx->ntracks++];
	memset(t, 0, sizeof *t);
	t->p = *p;
	t->state = T_ALIVE;
	t->nice_set = -100;
	return t;
}

/* ------------------------------------------------------------------ */

static int user_in(const char (*list)[PSD_USER_MAX], int n, const char *user)
{
	int i;

	for (i = 0; i < n; i++)
		if (strcmp(list[i], user) == 0)
			return 1;
	return 0;
}

/* psDooM: (psallusers || in psuser list) && !(in psnotuser list) */
static int user_wanted(psd_ctx *ctx, const psd_proc *p)
{
	const struct psd_config *c = &ctx->cfg;

	if (!(c->all_users || user_in(c->users, c->nusers, p->user)))
		return 0;
	return !user_in(c->notusers, c->nnotusers, p->user);
}

static int glob_hit(const char *pat, const psd_proc *p)
{
	const char *base = psd_basename(p->name);

	if (*base == '-')            /* login shells: "-bash" */
		base++;
	return fnmatch(pat, p->comm, 0) == 0 || fnmatch(pat, base, 0) == 0 ||
	       fnmatch(pat, p->name, 0) == 0;
}

static int name_protected(psd_ctx *ctx, const psd_proc *p)
{
	int i;

	for (i = 0; builtin_protect[i] != NULL; i++)
		if (glob_hit(builtin_protect[i], p))
			return 1;
	for (i = 0; i < ctx->cfg.nprotect; i++)
		if (glob_hit(ctx->cfg.protect[i], p))
			return 1;
	return 0;
}

static psd_proc *snap_find(psd_proc *s, size_t n, long pid)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (s[i].pid == pid)
			return &s[i];
	return NULL;
}

/* Modern mode: mark ourselves, our parent chain, kernel threads, helpers, names. */
static void mark_protected(psd_ctx *ctx, psd_proc *s, size_t n)
{
	long self = ctx->be.self_pid ? ctx->be.self_pid(&ctx->be) : -1;
	size_t i;
	int depth;

	for (i = 0; i < n; i++) {
		psd_proc *p = &s[i];
		p->is_protected = ctx->cfg.mode == PSD_MODE_MODERN &&
			(p->pid <= 1 || p->is_kernel || name_protected(ctx, p) ||
			 (ctx->be.is_helper && ctx->be.is_helper(&ctx->be, p->pid)));
	}
	if (ctx->cfg.mode != PSD_MODE_MODERN || self < 0)
		return;
	for (depth = 0; self > 1 && depth < 128; depth++) {
		psd_proc *p = snap_find(s, n, self);
		if (p == NULL)
			break;
		p->is_protected = 1;
		self = p->ppid;
	}
}

static int hist_cmp(const void *a, const void *b)
{
	long pa = ((const hist *)a)->pid, pb = ((const hist *)b)->pid;

	return (pa > pb) - (pa < pb);
}

static void compute_cpu(psd_ctx *ctx, psd_proc *s, size_t n, long long now)
{
	long long dt = now - ctx->prev_ms;
	size_t i;

	for (i = 0; i < n; i++) {
		hist key, *h;

		s[i].cpu_pct = 0.0;
		if (ctx->be.ticks_per_sec == 0 || ctx->nprev == 0 || dt <= 0)
			continue;
		key.pid = s[i].pid;
		h = bsearch(&key, ctx->prev, ctx->nprev, sizeof *h, hist_cmp);
		if (h == NULL || h->start_id != s[i].start_id || s[i].cpu_ticks < h->ticks)
			continue;
		s[i].cpu_pct = 100.0 * (double)(s[i].cpu_ticks - h->ticks) * 1000.0 /
		               ((double)ctx->be.ticks_per_sec * (double)dt);
	}

	if (n > ctx->capprev) {
		hist *nh = realloc(ctx->prev, n * sizeof *nh);
		if (nh == NULL) {
			ctx->nprev = 0;
			return;
		}
		ctx->prev = nh;
		ctx->capprev = n;
	}
	for (i = 0; i < n; i++) {
		ctx->prev[i].pid = s[i].pid;
		ctx->prev[i].start_id = s[i].start_id;
		ctx->prev[i].ticks = s[i].cpu_ticks;
	}
	ctx->nprev = n;
	qsort(ctx->prev, n, sizeof *ctx->prev, hist_cmp);
	ctx->prev_ms = now;
}

static int cpu_desc(const void *a, const void *b)
{
	const psd_proc *pa = *(const psd_proc *const *)a, *pb = *(const psd_proc *const *)b;

	if (pa->cpu_pct != pb->cpu_pct)
		return pa->cpu_pct < pb->cpu_pct ? 1 : -1;
	return (pa->pid > pb->pid) - (pa->pid < pb->pid);
}

/* ------------------------------------------------------------------ */

psd_ctx *psd_open(const psd_config *cfg, int *err)
{
	psd_ctx *ctx;
	const char *me;
	int rc;

	if (err)
		*err = PSD_OK;
	if (cfg->disabled) {
		if (err)
			*err = PSD_ERR_DISABLED;
		return NULL;
	}
	ctx = calloc(1, sizeof *ctx);
	if (ctx == NULL) {
		if (err)
			*err = PSD_ERR;
		return NULL;
	}
	ctx->cfg = *cfg;

	/* psDooM's user defaults */
	me = psd_platform_whoami();
	if (me != NULL)
		psd_strlcpy(ctx->whoami, me, sizeof ctx->whoami);
	if (ctx->cfg.user_add_self || ctx->cfg.notuser_add_self || !ctx->cfg.userlist_given) {
		if (me == NULL) {
			free(ctx);
			if (err)
				*err = PSD_ERR_NOUSER;
			return NULL;
		}
		if (ctx->cfg.user_add_self)
			psd_config_add_user(&ctx->cfg, me);
		if (ctx->cfg.notuser_add_self)
			psd_config_add_notuser(&ctx->cfg, me);
		if (!cfg->userlist_given) {
			if (strcmp(me, "root") == 0)
				ctx->cfg.all_users = 1;
			else
				psd_config_add_user(&ctx->cfg, me);
		}
	}

	/* psdoomd already chose the users on its side; our name means nothing there */
	if (ctx->cfg.backend == PSD_BACKEND_NET && !cfg->userlist_given)
		ctx->cfg.all_users = 1;

	if (ctx->cfg.mode == PSD_MODE_CLASSIC) {
		ctx->max_procs = cfg->max_procs >= 0 ? cfg->max_procs : 0;
		ctx->grace_ms = 0;
	} else {
		ctx->max_procs = cfg->max_procs >= 0 ? cfg->max_procs : PSD_DEFAULT_MAX_PROCS;
		ctx->grace_ms = cfg->grace_ms >= 0 ? cfg->grace_ms : PSD_DEFAULT_GRACE_MS;
	}

	switch (ctx->cfg.backend) {
	case PSD_BACKEND_NATIVE: rc = psd_backend_native_open(&ctx->be, &ctx->cfg); break;
#ifndef PSD_NO_CMD
	case PSD_BACKEND_CMD:    rc = psd_backend_cmd_open(&ctx->be, &ctx->cfg); break;
#endif
	case PSD_BACKEND_MOCK:   rc = psd_backend_mock_open(&ctx->be, &ctx->cfg); break;
	case PSD_BACKEND_NET:    rc = psd_backend_net_open(&ctx->be, &ctx->cfg); break;
	default:                 rc = PSD_ERR_BACKEND; break;
	}
	if (rc != PSD_OK) {
		free(ctx);
		if (err)
			*err = rc;
		return NULL;
	}

	/* audit log: on by default */
	{
		char path[PSD_PATH_MAX];
		int have = 0;

		if (ctx->cfg.log_set) {
			if (ctx->cfg.log_path[0]) {
				psd_strlcpy(path, ctx->cfg.log_path, sizeof path);
				have = 1;
			}
		} else if (psd_platform_default_log(path, sizeof path) == PSD_OK) {
			have = 1;
		}
		if (have) {
			psd_platform_mkdir_parents(path);
			ctx->log = fopen(path, "a");
			if (ctx->log == NULL)
				fprintf(stderr, "psdoom: cannot open audit log %s\n", path);
		}
	}
	return ctx;
}

void psd_close(psd_ctx *ctx)
{
	if (ctx == NULL)
		return;
	if (ctx->be.close)
		ctx->be.close(&ctx->be);
	if (ctx->log)
		fclose(ctx->log);
	free(ctx->tracks);
	free(ctx->prev);
	free(ctx);
}

psd_mode psd_mode_of(const psd_ctx *ctx) { return ctx->cfg.mode; }
const char *psd_backend_name(const psd_ctx *ctx) { return ctx->be.name; }

int psd_poll(psd_ctx *ctx, psd_event_fn fn, void *user)
{
	psd_proc *snap = NULL, **fresh = NULL;
	size_t n = 0, nfresh = 0, i;
	long long now;
	int rc;

	psd_tick(ctx);
	now = now_ms(ctx);
	ctx->be.snap_ms = now;           /* synchronous backends; async ones override */
	rc = ctx->be.snapshot(&ctx->be, &snap, &n);
	if (rc != PSD_OK)
		return rc;

	for (i = 0; i < n; i++)
		psd_make_label(&snap[i]);
	compute_cpu(ctx, snap, n, now);
	mark_protected(ctx, snap, n);

	if (n > 0) {
		fresh = malloc(n * sizeof *fresh);
		if (fresh == NULL)
			return PSD_ERR;
	}
	for (i = 0; i < ctx->ntracks; i++)
		ctx->tracks[i].seen = 0;

	for (i = 0; i < n; i++) {
		psd_proc *p = &snap[i];
		track *t;

		if (!user_wanted(ctx, p))
			continue;
		if (p->is_protected && !ctx->cfg.show_protected)
			continue;

		t = find_track(ctx, p->pid);
		if (t != NULL && t->p.start_id && p->start_id && t->p.start_id != p->start_id) {
			/* pid reused by a different process */
			psd_proc old = t->p;
			drop_track(ctx, t);
			fn(user, PSD_EV_REMOVE, &old);
			t = NULL;
		}
		if (t == NULL) {
			fresh[nfresh++] = p;
			continue;
		}

		t->seen = 1;
		{
			int state = t->state;
			t->p = *p;
			t->state = state;
		}
		if (t->state == T_DEAD) {
			if (t->kill_deadline && now < t->kill_deadline)
				continue;    /* modern: still inside the TERM grace period */
			if (ctx->be.snap_ms < t->killed_ms + ctx->be.settle_ms)
				continue;    /* this snapshot predates the kill */
			/* the monster died but the process did not */
			if (fn(user, PSD_EV_RESURRECT, &t->p)) {
				t->state = T_ALIVE;
				t->wounded = 0;
				t->kill_deadline = 0;
				audit(ctx, "resurrect", &t->p, NULL, "ok");
			} else {
				/* engine removed the body: offer it as new next time */
				drop_track(ctx, t);
			}
		} else {
			fn(user, PSD_EV_UPDATE, &t->p);
		}
	}

	/* processes that vanished: remove, no corpse */
	for (i = 0; i < ctx->ntracks; ) {
		if (!ctx->tracks[i].seen) {
			psd_proc old = ctx->tracks[i].p;
			drop_track(ctx, &ctx->tracks[i]);
			fn(user, PSD_EV_REMOVE, &old);
		} else {
			i++;
		}
	}

	/* new processes, busiest first when capped */
	if (ctx->max_procs > 0)
		qsort(fresh, nfresh, sizeof *fresh, cpu_desc);
	for (i = 0; i < nfresh; i++) {
		if (ctx->max_procs > 0 && ctx->ntracks >= (size_t)ctx->max_procs)
			break;
		if (fn(user, PSD_EV_SPAWN, fresh[i]))
			add_track(ctx, fresh[i]);
	}
	free(fresh);
	return (int)ctx->ntracks;
}

void psd_tick(psd_ctx *ctx)
{
	long long now;
	size_t i;

	if (ctx->be.tick)
		ctx->be.tick(&ctx->be);
	now = now_ms(ctx);
	for (i = 0; i < ctx->ntracks; i++) {
		track *t = &ctx->tracks[i];
		int rc;

		if (t->state != T_DEAD || t->kill_deadline == 0 || now < t->kill_deadline)
			continue;
		t->kill_deadline = 0;
		if (ctx->cfg.no_act) {
			audit(ctx, "escalate", &t->p, "sig=KILL", "dry-run");
			continue;
		}
		rc = ctx->be.signal(&ctx->be, &t->p, PSD_SIG_KILL, 1);
		audit(ctx, "escalate", &t->p, "sig=KILL", result_str(rc));
	}
}

static int deliver(psd_ctx *ctx, track *t, psd_signal sig, const char *action)
{
	char detail[32];
	int rc;

	snprintf(detail, sizeof detail, "sig=%s", psd_signal_name(sig));
	if (ctx->cfg.no_act) {
		audit(ctx, action, &t->p, detail, "dry-run");
		return PSD_OK;
	}
	rc = ctx->be.signal(&ctx->be, &t->p, sig, ctx->cfg.mode == PSD_MODE_MODERN);
	audit(ctx, action, &t->p, detail, result_str(rc));
	return rc;
}

int psd_kill(psd_ctx *ctx, long pid)
{
	track *t = find_track(ctx, pid);

	if (t == NULL)
		return PSD_ERR_UNKNOWN;
	if (t->p.is_protected && ctx->cfg.mode == PSD_MODE_MODERN) {
		audit(ctx, "kill", &t->p, NULL, "protected");
		return PSD_ERR_PROTECTED;
	}
	t->state = T_DEAD;
	t->killed_ms = now_ms(ctx);
	if (ctx->cfg.mode == PSD_MODE_CLASSIC || ctx->grace_ms == 0)
		return deliver(ctx, t, PSD_SIG_KILL, "kill");

	t->kill_deadline = now_ms(ctx) + ctx->grace_ms;
	return deliver(ctx, t, PSD_SIG_TERM, "kill");
}

int psd_send(psd_ctx *ctx, long pid, psd_signal sig)
{
	track *t = find_track(ctx, pid);

	if (t == NULL)
		return PSD_ERR_UNKNOWN;
	if (t->p.is_protected && ctx->cfg.mode == PSD_MODE_MODERN) {
		char detail[32];

		snprintf(detail, sizeof detail, "sig=%s", psd_signal_name(sig));
		audit(ctx, "signal", &t->p, detail, "protected");
		return PSD_ERR_PROTECTED;
	}
	return deliver(ctx, t, sig, "signal");
}

int psd_renice(psd_ctx *ctx, long pid, int nice)
{
	track *t = find_track(ctx, pid);
	char detail[32];
	int rc;

	if (t == NULL)
		return PSD_ERR_UNKNOWN;
	if (t->p.is_protected && ctx->cfg.mode == PSD_MODE_MODERN) {
		audit(ctx, "renice", &t->p, NULL, "protected");
		return PSD_ERR_PROTECTED;
	}
	if (nice < -20 || nice > 19)
		return PSD_ERR;
	snprintf(detail, sizeof detail, "nice=%d", nice);
	if (ctx->cfg.no_act) {
		audit(ctx, "renice", &t->p, detail, "dry-run");
		return PSD_OK;
	}
	rc = ctx->be.set_nice(&ctx->be, &t->p, nice);
	audit(ctx, "renice", &t->p, detail, result_str(rc));
	return rc;
}

int psd_wound(psd_ctx *ctx, long pid, int health, int spawnhealth)
{
	track *t = find_track(ctx, pid);
	char detail[32];
	int target, rc;

	if (t == NULL)
		return PSD_ERR_UNKNOWN;
	if (t->p.is_protected && ctx->cfg.mode == PSD_MODE_MODERN)
		return PSD_ERR_PROTECTED;
	if (spawnhealth <= 0 || health >= spawnhealth)
		return PSD_OK;           /* no damage */

	if (ctx->cfg.mode == PSD_MODE_CLASSIC) {
		/* psDooM: "renice +5 <pid>" on the first damage only */
		if (t->wounded)
			return PSD_OK;
		t->wounded = 1;
		target = 5;
	} else {
		/* nice climbs from 1 to 19 as health drops to 0 */
		double frac = (double)(spawnhealth - (health < 0 ? 0 : health)) / spawnhealth;
		target = (int)ceil(frac * 19.0);
		if (target < 1)
			target = 1;
		if (target > 19)
			target = 19;
		if (target <= t->nice_set || target <= t->p.nice)
			return PSD_OK;
	}

	snprintf(detail, sizeof detail, "nice=%d", target);
	if (ctx->cfg.no_act) {
		audit(ctx, "renice", &t->p, detail, "dry-run");
		t->nice_set = target;
		return PSD_OK;
	}
	rc = ctx->be.set_nice(&ctx->be, &t->p, target);
	audit(ctx, "renice", &t->p, detail, result_str(rc));
	if (rc == PSD_OK)
		t->nice_set = target;
	return rc;
}

void psd_reset(psd_ctx *ctx)
{
	ctx->ntracks = 0;
}

void psd_forget(psd_ctx *ctx, long pid)
{
	track *t = find_track(ctx, pid);

	if (t != NULL)
		drop_track(ctx, t);
}

const psd_proc *psd_lookup(psd_ctx *ctx, long pid)
{
	track *t = find_track(ctx, pid);

	return t ? &t->p : NULL;
}

int psd_needs_confirm(psd_ctx *ctx, long pid)
{
	track *t = find_track(ctx, pid);

#ifdef __HAIKU__
	(void)t;
	return 0;                    /* everything runs as uid 0 on Haiku */
#else
	return t != NULL && ctx->cfg.mode == PSD_MODE_MODERN && !ctx->cfg.no_act &&
	       t->p.uid == 0;
#endif
}
