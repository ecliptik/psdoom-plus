/*
 * backend_none.c - platforms without a native backend (yet)
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "psd_internal.h"

int psd_backend_native_open(psd_backend *b, const struct psd_config *cfg)
{
	(void)b;
	(void)cfg;
	return PSD_ERR_BACKEND;
}
