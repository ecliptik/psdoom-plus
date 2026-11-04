/*
 * psdps - list DOS residents the way psdoom-plus sees them (libpsd's mcb
 * backend): a ps(1) for the MCB chain, used by the DOS test and handy for
 * checking what a level will be full of.
 *
 *   PSDPS [-psclassic] [other libpsd options]
 *   PSDPS -v      the raw MCB chain (UMBs linked) and the sub-blocks of every
 *                 DOS "SD" block: what the mcb backend reads, for checking it
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <dpmi.h>
#include <go32.h>
#include <stdio.h>
#include <string.h>
#include <sys/movedata.h>

#include "psd.h"

static void name8(const unsigned char *raw, char *out)
{
	int i;

	for (i = 0; i < 8; i++)
		out[i] = raw[i] >= 0x20 && raw[i] < 0x7f ? (char)raw[i] : (raw[i] ? '?' : ' ');
	out[8] = '\0';
}

static int raw_dump(void)
{
	__dpmi_regs r;
	unsigned seg, lol_seg, lol_off;
	unsigned short w;
	int guard, link;

	memset(&r, 0, sizeof r);
	r.h.ah = 0x52;
	__dpmi_int(0x21, &r);
	lol_seg = r.x.es;
	lol_off = r.x.bx;
	dosmemget(lol_seg * 16u + lol_off - 2, 2, &w);
	seg = w;
	memset(&r, 0, sizeof r);
	r.x.ax = 0x5802;
	__dpmi_int(0x21, &r);
	link = (r.x.flags & 1) ? -1 : r.h.al;
	printf("List of Lists %04X:%04X, first MCB %04X, UMB link was %d\n", lol_seg, lol_off, seg, link);
	if (link == 0) {
		r.x.ax = 0x5803;
		r.x.bx = 1;
		__dpmi_int(0x21, &r);
	}
	printf("MCB  T OWNER SIZE  NAME\n");
	for (guard = 0; guard < 512 && seg && seg < 0xffff; guard++) {
		unsigned char m[16];
		char nm[9];
		unsigned owner, size;

		dosmemget(seg * 16u, 16, m);
		owner = m[1] | (m[2] << 8);
		size = m[3] | (m[4] << 8);
		name8(m + 8, nm);
		printf("%04X %c %04X  %04X  %s\n", seg, m[0] >= 0x20 ? m[0] : '?', owner, size, nm);
		if (m[0] != 'M' && m[0] != 'Z')
			break;
		if (owner == 8 && (memcmp(m + 8, "SD", 2) == 0)) {
			unsigned sub = seg + 1, end = seg + 1 + size;
			int g;

			for (g = 0; g < 64 && sub < end; g++) {
				unsigned char sm[16];
				unsigned ssize;

				dosmemget(sub * 16u, 16, sm);
				ssize = sm[3] | (sm[4] << 8);
				name8(sm + 8, nm);
				printf("  sub %04X %c %04X  %04X  %s\n", sub, sm[0] >= 0x20 ? sm[0] : '?',
				       sm[1] | (sm[2] << 8), ssize, nm);
				if (ssize == 0)
					break;
				sub += ssize + 1;
			}
		}
		if (m[0] == 'Z')
			break;
		seg += size + 1;
	}
	if (link == 0) {
		r.x.ax = 0x5803;
		r.x.bx = 0;
		__dpmi_int(0x21, &r);
	}
	return 0;
}

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

	if (argc > 1 && strcmp(argv[1], "-v") == 0)
		return raw_dump();

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
