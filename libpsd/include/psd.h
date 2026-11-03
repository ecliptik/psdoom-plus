/*
 * psd.h - libpsd: processes as monsters, for psdoom-plus engines
 *
 * Copyright (C) 1999 Dennis Chao
 * Copyright (C) 2000 David Koppenhofer
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The engine owns the monsters; libpsd owns everything about processes.
 *
 *   psd_config *cfg = psd_config_new();
 *   psd_config_load_default_file(cfg);         // $XDG_CONFIG_HOME/psdoom/psdoom.conf
 *   psd_config_parse_args(cfg, argc, argv);    // -psuser, -psmode, ... override the file
 *   psd_ctx *ctx = psd_open(cfg, &err);
 *
 *   every frame:          psd_tick(ctx);
 *   every few seconds:    psd_poll(ctx, on_event, engine_state);
 *   monster first hurt:   psd_wound(ctx, pid, health, spawnhealth);
 *   monster died:         psd_kill(ctx, pid);
 *
 * on_event() is called with PSD_EV_SPAWN / UPDATE / RESURRECT / REMOVE.
 * For SPAWN and RESURRECT, return nonzero if the engine placed the monster;
 * return 0 (e.g. no room) and libpsd offers the same process again on the
 * next poll, as psDooM did.
 */
#ifndef PSD_H
#define PSD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSD_VERSION       "0.1.0"
#define PSD_API_VERSION   1

#define PSD_USER_MAX      32
#define PSD_NAME_MAX      64
#define PSD_COMM_MAX      32
#define PSD_CMDLINE_MAX   256
#define PSD_LABEL_LEN     7      /* psDooM showed the last 7 chars of the name */

typedef enum {
	PSD_MODE_MODERN  = 0,        /* protected processes, TERM then KILL, nice scales with damage */
	PSD_MODE_CLASSIC = 1         /* psDooM 2000: renice +5 on first hit, SIGKILL on death, nothing protected */
} psd_mode;

typedef enum {
	PSD_BACKEND_NATIVE = 0,      /* this OS's process table */
	PSD_BACKEND_CMD,             /* psdoom-ng hooks: PSDOOM_NG_PS_CMD / _RENICE_CMD / _KILL_CMD */
	PSD_BACKEND_MOCK,            /* in-memory table, for tests */
	PSD_BACKEND_NET              /* remote psdoomd (phase 7) */
} psd_backend_kind;

/* Portable signal numbers; backends map them to the OS's own. */
typedef enum {
	PSD_SIG_TERM = 1,
	PSD_SIG_KILL,
	PSD_SIG_HUP,
	PSD_SIG_STOP,
	PSD_SIG_CONT
} psd_signal;

/* Return codes. */
#define PSD_OK              0
#define PSD_ERR            -1   /* generic failure, see errno */
#define PSD_ERR_PROTECTED  -2   /* modern mode refused: protected process */
#define PSD_ERR_UNKNOWN    -3   /* pid is not a tracked monster */
#define PSD_ERR_NOUSER     -4   /* could not determine the current user name */
#define PSD_ERR_BACKEND    -5   /* backend not available on this platform */
#define PSD_ERR_GONE       -6   /* the process (or its identity) changed before we could act */
#define PSD_ERR_DISABLED   -7   /* -nopsmon */

typedef struct psd_proc {
	long pid;
	long ppid;
	long uid;                    /* -1 if unknown (cmd backend) */
	char user[PSD_USER_MAX];
	char name[PSD_NAME_MAX];     /* argv[0] like ps's COMMAND column, else [comm] */
	char comm[PSD_COMM_MAX];     /* kernel short name */
	char cmdline[PSD_CMDLINE_MAX];
	char label[PSD_LABEL_LEN + 1]; /* last 7 chars of name, for the floating label */
	int is_daemon;               /* no controlling terminal: psDooM spawned these as Demons */
	int is_kernel;               /* kernel thread / system process */
	int is_protected;            /* modern mode: never killable (only shown with show_protected) */
	int nice;
	unsigned long long start_id; /* process identity (start time); 0 = unknown */
	unsigned long long cpu_ticks;/* cumulative CPU time in backend ticks */
	unsigned long rss_kb;
	double cpu_pct;              /* computed by libpsd between polls; 0 on the first */
} psd_proc;

typedef enum {
	PSD_EV_SPAWN = 1,            /* new process: spawn a monster */
	PSD_EV_UPDATE,               /* still alive: refresh label / stats if wanted */
	PSD_EV_RESURRECT,            /* the monster died but the process did not: raise it */
	PSD_EV_REMOVE                /* the process is gone: remove the monster, no corpse */
} psd_event;

typedef int (*psd_event_fn)(void *user, psd_event ev, const psd_proc *proc);

typedef struct psd_config psd_config;
typedef struct psd_ctx psd_ctx;

/* ---- configuration ---- */

psd_config *psd_config_new(void);
void        psd_config_free(psd_config *cfg);

/* Key = value file. Missing file is not an error (returns PSD_OK). */
int  psd_config_load_file(psd_config *cfg, const char *path);
/* $PSDOOM_CONF, else $XDG_CONFIG_HOME/psdoom/psdoom.conf, else ~/.config/psdoom/psdoom.conf */
int  psd_config_load_default_file(psd_config *cfg);
/*
 * Recognises psDooM's flags (-nopsmon -nopsact -nopssafety -nopslev -psallusers
 * -psuser [u...] -psnotuser [u...]) and psdoom-plus's (-psmode classic|modern,
 * -psclassic, -psbackend native|cmd|mock|net, -psmax N, -psgrace MS,
 * -psshowprotected, -psprotect GLOB, -pslog PATH|off, -psconf PATH).
 * Other arguments are ignored, so an engine can pass its whole argv.
 */
int  psd_config_parse_args(psd_config *cfg, int argc, char **argv);

void psd_config_set_mode(psd_config *cfg, psd_mode mode);
void psd_config_set_backend(psd_config *cfg, psd_backend_kind kind);
void psd_config_set_all_users(psd_config *cfg, int on);
int  psd_config_add_user(psd_config *cfg, const char *user);      /* -psuser */
int  psd_config_add_notuser(psd_config *cfg, const char *user);   /* -psnotuser */
int  psd_config_add_protect(psd_config *cfg, const char *glob);   /* modern: extra protected names */
void psd_config_set_no_act(psd_config *cfg, int on);              /* -nopsact: log, don't signal */
void psd_config_set_max_procs(psd_config *cfg, int n);            /* 0 = unlimited */
void psd_config_set_grace_ms(psd_config *cfg, int ms);
void psd_config_set_show_protected(psd_config *cfg, int on);
int  psd_config_set_log(psd_config *cfg, const char *path);       /* NULL or "off" disables */

/* Engine-level flags libpsd parses for convenience but does not act on. */
psd_mode psd_config_mode(const psd_config *cfg);
int  psd_config_disabled(const psd_config *cfg);     /* -nopsmon */
int  psd_config_no_safety(const psd_config *cfg);    /* -nopssafety: others may hurt pid monsters */
int  psd_config_no_pslev(const psd_config *cfg);     /* -nopslev: don't auto-load psdoom1/2.wad */

/* ---- running ---- */

/* cfg is copied; the caller may free it afterwards. *err gets a PSD_ERR_* on failure. */
psd_ctx *psd_open(const psd_config *cfg, int *err);
void     psd_close(psd_ctx *ctx);

/* Take a snapshot and report differences. Returns the number of tracked monsters, or < 0. */
int psd_poll(psd_ctx *ctx, psd_event_fn fn, void *user);
/* Cheap; call every frame. Escalates TERM -> KILL, reaps helper commands. */
void psd_tick(psd_ctx *ctx);

/* Monster took damage. health is after the hit. */
int psd_wound(psd_ctx *ctx, long pid, int health, int spawnhealth);
/* Monster died: classic SIGKILL, modern SIGTERM then SIGKILL after the grace period. */
int psd_kill(psd_ctx *ctx, long pid);
/* Send one specific signal (weapon -> signal mapping, later phases). */
int psd_send(psd_ctx *ctx, long pid, psd_signal sig);

/* Tracked process, or NULL. Valid until the next psd_poll. */
const psd_proc *psd_lookup(psd_ctx *ctx, long pid);
/* Modern mode asks before killing a root-owned process. */
int psd_needs_confirm(psd_ctx *ctx, long pid);

psd_mode    psd_mode_of(const psd_ctx *ctx);
const char *psd_backend_name(const psd_ctx *ctx);
const char *psd_strerror(int err);

/* ---- mock backend (tests) ---- */

/* flags for psd_mock_add */
#define PSD_MOCK_DAEMON       0x01
#define PSD_MOCK_KERNEL       0x02
#define PSD_MOCK_IGNORE_TERM  0x04   /* survives SIGTERM */
#define PSD_MOCK_IMMORTAL     0x08   /* survives everything (permission denied) */

void psd_mock_reset(void);
void psd_mock_set_self(long pid);
void psd_mock_add(long pid, long ppid, long uid, const char *user,
                  const char *name, unsigned long long start_id,
                  unsigned long long cpu_ticks, int flags);
void psd_mock_remove(long pid);
void psd_mock_set_cpu(long pid, unsigned long long cpu_ticks);
/* Last signal / nice applied to pid by libpsd (0 / -100 if none). */
int  psd_mock_last_signal(long pid);
int  psd_mock_nice(long pid);
int  psd_mock_signal_count(void);
/* Advance the mock clock (milliseconds) used for grace periods and cpu%. */
void psd_mock_advance_ms(long ms);

#ifdef __cplusplus
}
#endif

#endif /* PSD_H */
