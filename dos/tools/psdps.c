/*
 * psdps - list DOS residents the way psdoom-plus sees them (libpsd's mcb
 * backend): a ps(1) for the MCB chain, used by the DOS test and handy for
 * checking what a level will be full of.
 *
 *   PSDPS [-psclassic] [other libpsd options]
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>

#include "psd.h"

static int show(void *user, psd_event ev, const psd_proc *p)
{
	(void)user;
	if (ev != PSD_EV_SPAWN)
		return 0;
	printf("%04lX %04lX %-8s %5lu KB %-7s %s\n", p->pid, p->ppid, p->name,
	       p->rss_kb, p->is_daemon ? "driver" : "program",
	       p->is_protected ? "protected" : "-");
	return 1;
}

int main(int argc, char **argv)
{
	psd_config *cfg = psd_config_new();
	psd_ctx *ctx;
	int err;

	psd_config_set_log(cfg, "off");
	psd_config_set_max_procs(cfg, 0);
	psd_config_parse_args(cfg, argc, argv);
	ctx = psd_open(cfg, &err);
	psd_config_free(cfg);
	if (ctx == NULL) {
		printf("psdps: %s\n", psd_strerror(err));
		return 1;
	}
	printf("SEG  PARENT NAME        SIZE TYPE    MODERN\n");
	psd_poll(ctx, show, NULL);
	psd_close(ctx);
	return 0;
}
