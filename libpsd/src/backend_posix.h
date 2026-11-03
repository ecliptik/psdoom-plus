/*
 * backend_posix.h - shared helpers for the BSD and Haiku backends
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef BACKEND_POSIX_H
#define BACKEND_POSIX_H

#include <stdlib.h>

#include "psd_internal.h"

const char *psd_posix_user(long uid);
int  psd_posix_signo(psd_signal sig);
int  psd_posix_kill(long pid, psd_signal sig);
int  psd_posix_set_nice(long pid, int nice);
long psd_posix_self(psd_backend *b);
void psd_posix_set_argv(psd_proc *p, const char *buf, size_t len);
int  psd_posix_grow(psd_proc **procs, size_t *cap, size_t need);

#endif
