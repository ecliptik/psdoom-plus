/*
 * psd_util.c - small string helpers
 *
 * Copyright (C) 2000 David Koppenhofer
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>

#include "psd_internal.h"

void psd_strlcpy(char *dst, const char *src, size_t len)
{
	size_t n;

	if (len == 0)
		return;
	if (src == NULL)
		src = "";
	n = strlen(src);
	if (n >= len)
		n = len - 1;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

/*
 * psDooM wanted "the last 7 chars of the process name, not the first 7.
 * ie: '/sbin/mingetty' is shown as 'ingetty' rather than '/sbin/m'".
 */
void psd_make_label(psd_proc *p)
{
	const char *src = p->name[0] ? p->name : p->comm;
	size_t n = strlen(src);

	if (n > PSD_LABEL_LEN)
		src += n - PSD_LABEL_LEN;
	psd_strlcpy(p->label, src, sizeof p->label);
}

const char *psd_basename(const char *path)
{
	const char *s;

	if (path == NULL)
		return "";
	s = strrchr(path, '/');
	return s ? s + 1 : path;
}

const char *psd_signal_name(psd_signal sig)
{
	switch (sig) {
	case PSD_SIG_TERM: return "TERM";
	case PSD_SIG_KILL: return "KILL";
	case PSD_SIG_HUP:  return "HUP";
	case PSD_SIG_STOP: return "STOP";
	case PSD_SIG_CONT: return "CONT";
	}
	return "?";
}

const char *psd_strerror(int err)
{
	switch (err) {
	case PSD_OK:            return "ok";
	case PSD_ERR:           return "system error";
	case PSD_ERR_PROTECTED: return "protected process";
	case PSD_ERR_UNKNOWN:   return "not a tracked process";
	case PSD_ERR_NOUSER:    return "could not determine your user name: set PSDOOMUSER, LOGNAME, USER or USERNAME";
	case PSD_ERR_BACKEND:   return "process backend not available here";
	case PSD_ERR_GONE:      return "process is gone or was replaced";
	case PSD_ERR_DISABLED:  return "process monitoring disabled (-nopsmon)";
	}
	return "unknown error";
}
