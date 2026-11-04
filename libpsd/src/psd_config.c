/*
 * psd_config.c - command line flags and the psdoom.conf file
 *
 * Copyright (C) 2000 David Koppenhofer
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psd_internal.h"

psd_config *psd_config_new(void)
{
	psd_config *cfg = calloc(1, sizeof *cfg);

	if (cfg == NULL)
		return NULL;
	cfg->mode = PSD_MODE_MODERN;
	cfg->backend = PSD_BACKEND_NATIVE;
	cfg->max_procs = -1;
	cfg->grace_ms = -1;
#ifdef __DJGPP__
	/* a DOS box has a handful of residents: show the untouchable ones too,
	 * as invulnerable monsters (CWSDPMI, hooked TSRs, linked drivers) */
	cfg->show_protected = 1;
#endif
	return cfg;
}

void psd_config_free(psd_config *cfg)
{
	free(cfg);
}

void psd_config_set_mode(psd_config *cfg, psd_mode mode) { cfg->mode = mode; }
void psd_config_set_backend(psd_config *cfg, psd_backend_kind k) { cfg->backend = k; }
void psd_config_set_no_act(psd_config *cfg, int on) { cfg->no_act = on != 0; }
void psd_config_set_max_procs(psd_config *cfg, int n) { cfg->max_procs = n < 0 ? 0 : n; }
void psd_config_set_grace_ms(psd_config *cfg, int ms) { cfg->grace_ms = ms < 0 ? 0 : ms; }
void psd_config_set_show_protected(psd_config *cfg, int on) { cfg->show_protected = on != 0; }

void psd_config_set_all_users(psd_config *cfg, int on)
{
	cfg->all_users = on != 0;
	cfg->userlist_given = 1;
}

static int add_name(char list[][PSD_USER_MAX], int *n, const char *name)
{
	if (*n >= PSD_MAX_USERS || name == NULL || name[0] == '\0')
		return PSD_ERR;
	psd_strlcpy(list[*n], name, PSD_USER_MAX);
	(*n)++;
	return PSD_OK;
}

int psd_config_add_user(psd_config *cfg, const char *user)
{
	cfg->userlist_given = 1;
	return add_name(cfg->users, &cfg->nusers, user);
}

int psd_config_add_notuser(psd_config *cfg, const char *user)
{
	cfg->userlist_given = 1;
	return add_name(cfg->notusers, &cfg->nnotusers, user);
}

int psd_config_add_protect(psd_config *cfg, const char *glob)
{
	if (cfg->nprotect >= PSD_MAX_PROTECT || glob == NULL || glob[0] == '\0')
		return PSD_ERR;
	psd_strlcpy(cfg->protect[cfg->nprotect++], glob, PSD_GLOB_MAX);
	return PSD_OK;
}

int psd_config_set_log(psd_config *cfg, const char *path)
{
	cfg->log_set = 1;
	if (path == NULL || strcmp(path, "off") == 0 || strcmp(path, "none") == 0)
		cfg->log_path[0] = '\0';
	else
		psd_strlcpy(cfg->log_path, path, sizeof cfg->log_path);
	return PSD_OK;
}

psd_mode psd_config_mode(const psd_config *cfg) { return cfg->mode; }
int psd_config_disabled(const psd_config *cfg) { return cfg->disabled; }
int psd_config_no_safety(const psd_config *cfg) { return cfg->no_safety; }
int psd_config_no_pslev(const psd_config *cfg) { return cfg->no_pslev; }

static int parse_mode(const char *s, psd_mode *out)
{
	if (strcmp(s, "modern") == 0) { *out = PSD_MODE_MODERN; return PSD_OK; }
	if (strcmp(s, "classic") == 0) { *out = PSD_MODE_CLASSIC; return PSD_OK; }
	return PSD_ERR;
}

static int parse_backend(const char *s, psd_backend_kind *out)
{
	if (strcmp(s, "native") == 0) { *out = PSD_BACKEND_NATIVE; return PSD_OK; }
	if (strcmp(s, "cmd") == 0)    { *out = PSD_BACKEND_CMD; return PSD_OK; }
	if (strcmp(s, "mock") == 0)   { *out = PSD_BACKEND_MOCK; return PSD_OK; }
	if (strcmp(s, "net") == 0)    { *out = PSD_BACKEND_NET; return PSD_OK; }
	return PSD_ERR;
}

static int parse_bool(const char *s)
{
	return strcmp(s, "1") == 0 || strcmp(s, "yes") == 0 ||
	       strcmp(s, "true") == 0 || strcmp(s, "on") == 0;
}

static int parse_int(const char *s, int *out)
{
	char *end;
	long v = strtol(s, &end, 10);

	if (end == s || *end != '\0' || v < 0 || v > 1000000)
		return PSD_ERR;
	*out = (int)v;
	return PSD_OK;
}

/* "a b,c" -> callback per word */
static void each_word(const char *s, psd_config *cfg,
                      int (*fn)(psd_config *, const char *))
{
	char word[PSD_GLOB_MAX];
	size_t n;

	while (*s) {
		while (*s == ' ' || *s == '\t' || *s == ',')
			s++;
		n = 0;
		while (*s && *s != ' ' && *s != '\t' && *s != ',') {
			if (n + 1 < sizeof word)
				word[n++] = *s;
			s++;
		}
		word[n] = '\0';
		if (n > 0)
			fn(cfg, word);
	}
}

static char *trim(char *s)
{
	char *e;

	while (isspace((unsigned char)*s))
		s++;
	e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1]))
		*--e = '\0';
	return s;
}

static int apply_key(psd_config *cfg, const char *key, const char *val)
{
	int n;

	if (strcmp(key, "mode") == 0)
		return parse_mode(val, &cfg->mode);
	if (strcmp(key, "backend") == 0)
		return parse_backend(val, &cfg->backend);
	if (strcmp(key, "all_users") == 0) {
		psd_config_set_all_users(cfg, parse_bool(val));
		return PSD_OK;
	}
	if (strcmp(key, "users") == 0) {
		each_word(val, cfg, psd_config_add_user);
		return PSD_OK;
	}
	if (strcmp(key, "notusers") == 0) {
		each_word(val, cfg, psd_config_add_notuser);
		return PSD_OK;
	}
	if (strcmp(key, "protect") == 0) {
		each_word(val, cfg, psd_config_add_protect);
		return PSD_OK;
	}
	if (strcmp(key, "no_act") == 0) {
		cfg->no_act = parse_bool(val);
		return PSD_OK;
	}
	if (strcmp(key, "show_protected") == 0) {
		cfg->show_protected = parse_bool(val);
		return PSD_OK;
	}
	if (strcmp(key, "max_procs") == 0) {
		if (parse_int(val, &n) != PSD_OK)
			return PSD_ERR;
		cfg->max_procs = n;
		return PSD_OK;
	}
	if (strcmp(key, "grace_ms") == 0) {
		if (parse_int(val, &n) != PSD_OK)
			return PSD_ERR;
		cfg->grace_ms = n;
		return PSD_OK;
	}
	if (strcmp(key, "log") == 0)
		return psd_config_set_log(cfg, val);
	return PSD_ERR;
}

int psd_config_load_file(psd_config *cfg, const char *path)
{
	FILE *f = fopen(path, "r");
	char line[1024];
	int lineno = 0, bad = 0;

	if (f == NULL)
		return PSD_OK;           /* no config file is fine */
	while (fgets(line, sizeof line, f) != NULL) {
		char *s, *eq, *key, *val, *hash;

		lineno++;
		if ((hash = strchr(line, '#')) != NULL)
			*hash = '\0';
		s = trim(line);
		if (*s == '\0')
			continue;
		eq = strchr(s, '=');
		if (eq == NULL) {
			fprintf(stderr, "psdoom: %s:%d: expected key = value\n", path, lineno);
			bad = 1;
			continue;
		}
		*eq = '\0';
		key = trim(s);
		val = trim(eq + 1);
		if (apply_key(cfg, key, val) != PSD_OK) {
			fprintf(stderr, "psdoom: %s:%d: bad setting '%s'\n", path, lineno, key);
			bad = 1;
		}
	}
	fclose(f);
	return bad ? PSD_ERR : PSD_OK;
}

int psd_config_load_default_file(psd_config *cfg)
{
	char path[PSD_PATH_MAX];

	if (psd_platform_default_conf(path, sizeof path) != PSD_OK)
		return PSD_OK;
	return psd_config_load_file(cfg, path);
}

/*
 * psDooM's own flags keep their exact meaning: -psuser/-psnotuser take the
 * following words up to the next '-' word, and with no words mean "me".
 */
int psd_config_parse_args(psd_config *cfg, int argc, char **argv)
{
	int i, rc = PSD_OK;

	/* -psconf first, so the command line still overrides the file */
	for (i = 1; i < argc; i++)
		if (strcmp(argv[i], "-psconf") == 0 && i + 1 < argc)
			if (psd_config_load_file(cfg, argv[i + 1]) != PSD_OK)
				rc = PSD_ERR;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;

		if (strcmp(a, "-nopsmon") == 0) {
			cfg->disabled = 1;
		} else if (strcmp(a, "-nopsact") == 0 || strcmp(a, "-psnoact") == 0) {
			cfg->no_act = 1;
		} else if (strcmp(a, "-nopssafety") == 0) {
			cfg->no_safety = 1;
		} else if (strcmp(a, "-nopslev") == 0) {
			cfg->no_pslev = 1;
		} else if (strcmp(a, "-psallusers") == 0) {
			psd_config_set_all_users(cfg, 1);
		} else if (strcmp(a, "-psuser") == 0 || strcmp(a, "-psnotuser") == 0) {
			int notuser = a[3] == 'n';
			int given = 0;

			cfg->userlist_given = 1;
			while (i + 1 < argc && argv[i + 1][0] != '-') {
				i++;
				given = 1;
				if (notuser)
					psd_config_add_notuser(cfg, argv[i]);
				else
					psd_config_add_user(cfg, argv[i]);
			}
			if (!given) {
				if (notuser)
					cfg->notuser_add_self = 1;
				else
					cfg->user_add_self = 1;
			}
		} else if (strcmp(a, "-psclassic") == 0) {
			cfg->mode = PSD_MODE_CLASSIC;
		} else if (strcmp(a, "-psmode") == 0 && next) {
			if (parse_mode(next, &cfg->mode) != PSD_OK) {
				fprintf(stderr, "psdoom: -psmode wants classic or modern\n");
				rc = PSD_ERR;
			}
			i++;
		} else if (strcmp(a, "-psbackend") == 0 && next) {
			if (parse_backend(next, &cfg->backend) != PSD_OK) {
				fprintf(stderr, "psdoom: -psbackend wants native, cmd, mock or net\n");
				rc = PSD_ERR;
			}
			i++;
		} else if (strcmp(a, "-psmax") == 0 && next) {
			if (parse_int(next, &cfg->max_procs) != PSD_OK)
				rc = PSD_ERR;
			i++;
		} else if (strcmp(a, "-psgrace") == 0 && next) {
			if (parse_int(next, &cfg->grace_ms) != PSD_OK)
				rc = PSD_ERR;
			i++;
		} else if (strcmp(a, "-psshowprotected") == 0) {
			cfg->show_protected = 1;
		} else if (strcmp(a, "-psprotect") == 0 && next) {
			psd_config_add_protect(cfg, next);
			i++;
		} else if (strcmp(a, "-pslog") == 0 && next) {
			psd_config_set_log(cfg, next);
			i++;
		} else if (strcmp(a, "-psconf") == 0 && next) {
			i++;             /* already loaded */
		}
	}
	return rc;
}
