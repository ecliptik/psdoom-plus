/*
 * psd_platform.c - clocks, user name, XDG paths (POSIX)
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#if !defined(_POSIX_C_SOURCE) && !defined(__HAIKU__) && !defined(__DJGPP__)
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "psd_internal.h"

long long psd_platform_now_ms(void)
{
#if defined(__DJGPP__)
	/* the clock SDL3-DOS uses too: PIT-based, microsecond-ish, but it can
	 * step back a BIOS tick (55 ms) when one lands between its two reads */
	static long long last;
	long long now = (long long)uclock() * 1000 / UCLOCKS_PER_SEC;

	if (now > last)
		last = now;
	return last;
#elif defined(CLOCK_MONOTONIC)
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
		return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
	return (long long)time(NULL) * 1000;
}

/*
 * psDooM checked PSDOOMUSER, LOGNAME, USER and USERNAME, in that order, and
 * gave up if none was set. We fall back to the password database.
 */
const char *psd_platform_whoami(void)
{
	static const char *vars[] = { "PSDOOMUSER", "LOGNAME", "USER", "USERNAME" };
	static char buf[PSD_USER_MAX];
	struct passwd *pw;
	size_t i;

	for (i = 0; i < sizeof vars / sizeof vars[0]; i++) {
		const char *v = getenv(vars[i]);
		if (v != NULL && v[0] != '\0')
			return v;
	}
#ifdef __DJGPP__
	(void)pw;
	return "dos";                /* one user, and it is you */
#endif
	pw = getpwuid(geteuid());
	if (pw != NULL && pw->pw_name != NULL) {
		psd_strlcpy(buf, pw->pw_name, sizeof buf);
		return buf;
	}
	return NULL;
}

static int xdg_path(char *buf, size_t len, const char *xdg_var,
                    const char *home_rel, const char *leaf)
{
#ifdef __DJGPP__
	/* DOS: next to the game, in 8.3 names (psdoom.conf -> psd.cfg) */
	(void)xdg_var;
	(void)home_rel;
	psd_strlcpy(buf, strcmp(leaf, "psdoom.conf") == 0 ? "psd.cfg" :
	                 strcmp(leaf, "actions.log") == 0 ? "psd.log" : leaf, len);
	return PSD_OK;
#endif
	const char *base = getenv(xdg_var);
	int n;

	if (base != NULL && base[0] == '/') {
		n = snprintf(buf, len, "%s/psdoom/%s", base, leaf);
	} else {
		const char *home = getenv("HOME");
		if (home == NULL || home[0] == '\0')
			return PSD_ERR;
		n = snprintf(buf, len, "%s/%s/psdoom/%s", home, home_rel, leaf);
	}
	return (n < 0 || (size_t)n >= len) ? PSD_ERR : PSD_OK;
}

int psd_platform_default_conf(char *buf, size_t len)
{
	const char *env = getenv("PSDOOM_CONF");

	if (env != NULL && env[0] != '\0') {
		psd_strlcpy(buf, env, len);
		return PSD_OK;
	}
	return xdg_path(buf, len, "XDG_CONFIG_HOME", ".config", "psdoom.conf");
}

int psd_platform_default_log(char *buf, size_t len)
{
	return xdg_path(buf, len, "XDG_STATE_HOME", ".local/state", "actions.log");
}

int psd_platform_mkdir_parents(const char *path)
{
	char tmp[PSD_PATH_MAX];
	char *p;

	psd_strlcpy(tmp, path, sizeof tmp);
	for (p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(tmp, 0700) != 0 && errno != EEXIST)
			return PSD_ERR;
		*p = '/';
	}
	return PSD_OK;
}

void psd_platform_timestamp(char *buf, size_t len)
{
	time_t t = time(NULL);
	struct tm tm;

#ifdef __DJGPP__
	/* DOS has no time zones (TZ is usually unset): local time, no offset */
	const char *fmt = "%Y-%m-%dT%H:%M:%S";
#else
	const char *fmt = "%Y-%m-%dT%H:%M:%S%z";
#endif

	if (localtime_r(&t, &tm) == NULL || strftime(buf, len, fmt, &tm) == 0)
		psd_strlcpy(buf, "?", len);
}
