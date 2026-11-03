/*
 * psd_internal.h - libpsd internals shared by the core and the backends
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PSD_INTERNAL_H
#define PSD_INTERNAL_H

#include "psd.h"

#define PSD_MAX_USERS     32
#define PSD_MAX_PROTECT   64
#define PSD_GLOB_MAX      64
#define PSD_PATH_MAX      512

#define PSD_DEFAULT_MAX_PROCS  64     /* modern */
#define PSD_DEFAULT_GRACE_MS   3000   /* modern TERM -> KILL */

struct psd_config {
	psd_mode mode;
	psd_backend_kind backend;

	int all_users;
	int userlist_given;          /* any of -psallusers/-psuser/-psnotuser seen */
	int user_add_self;           /* -psuser with no names */
	int notuser_add_self;        /* -psnotuser with no names */
	char users[PSD_MAX_USERS][PSD_USER_MAX];
	int nusers;
	char notusers[PSD_MAX_USERS][PSD_USER_MAX];
	int nnotusers;

	char protect[PSD_MAX_PROTECT][PSD_GLOB_MAX];
	int nprotect;

	int no_act;
	int max_procs;               /* -1 = mode default */
	int grace_ms;                /* -1 = mode default */
	int show_protected;
	int log_set;                 /* 0 = default path, 1 = log_path (empty = off) */
	char log_path[PSD_PATH_MAX];

	/* engine-level */
	int disabled;
	int no_safety;
	int no_pslev;
};

typedef struct psd_backend psd_backend;
struct psd_backend {
	const char *name;
	unsigned long ticks_per_sec; /* for cpu_ticks; 0 = no cpu info */
	long long snap_ms;           /* when the last snapshot's data was gathered */
	int settle_ms;               /* a kill may take this long to show in a snapshot */
	void *state;

	/* Fills *out with a backend-owned array valid until the next call. */
	int  (*snapshot)(psd_backend *b, psd_proc **out, size_t *n);
	/* verify: refuse (PSD_ERR_GONE) if p->start_id no longer matches the pid. */
	int  (*signal)(psd_backend *b, const psd_proc *p, psd_signal sig, int verify);
	int  (*set_nice)(psd_backend *b, const psd_proc *p, int nice);
	void (*tick)(psd_backend *b);                 /* optional */
	long (*self_pid)(psd_backend *b);             /* -1: we are not in this table */
	int  (*is_helper)(psd_backend *b, long pid);  /* optional: our own helper children */
	long long (*now_ms)(psd_backend *b);          /* optional clock override (mock) */
	void (*close)(psd_backend *b);
};

int psd_backend_native_open(psd_backend *b, const struct psd_config *cfg);
int psd_backend_cmd_open(psd_backend *b, const struct psd_config *cfg);
int psd_backend_mock_open(psd_backend *b, const struct psd_config *cfg);

/* psd_platform.c */
long long   psd_platform_now_ms(void);
const char *psd_platform_whoami(void);           /* NULL if unknown */
int         psd_platform_default_conf(char *buf, size_t len);
int         psd_platform_default_log(char *buf, size_t len);
int         psd_platform_mkdir_parents(const char *path);
void        psd_platform_timestamp(char *buf, size_t len);

/* psd_util.c */
void psd_strlcpy(char *dst, const char *src, size_t len);
void psd_make_label(psd_proc *p);
const char *psd_basename(const char *path);
const char *psd_signal_name(psd_signal sig);

#endif
