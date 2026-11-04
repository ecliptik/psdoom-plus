/*
 * backend_dos.c - MS-DOS: resident programs and device drivers as monsters
 *
 * DOS has no processes, but it has a memory arena: the MCB chain. Every
 * program DOS loaded owns blocks there through its PSP segment, and a TSR
 * is a program that left without freeing them. Device drivers loaded from
 * CONFIG.SYS live in sub-blocks of the DOS "SD" (system data) block.
 *
 *   pid        the owner's PSP segment, or a driver's own segment
 *   name       the 8-character name DOS 4+ writes into the MCB
 *   ppid       the parent PSP (PSP:16h), so the core's parent-chain rule
 *              protects our own PSP and the COMMAND.COM above it
 *   is_daemon  device drivers (Demons); programs are Shotgun guys
 *   is_kernel  modern mode's "never": CWSDPMI (our DPMI host), any block an
 *              interrupt vector points into, any program further down one
 *              of those interrupt chains, any driver still in the DOS
 *              device chain. Freeing those hangs the PC on the next call.
 *
 * Killing a program frees every block it owns (owner word := 0), as
 * INT 21h AH=49h would. Killing a driver (classic mode only, modern always
 * refuses) unlinks it from the device chain; its memory stays allocated,
 * so we remember it and leave it out of later snapshots. Classic mode does
 * this to anything, interrupt vectors or not: if the victim hooked an
 * interrupt, the PC hangs the next time that interrupt fires, which is the
 * authentic psDooM experience.
 *
 * DOS has no priorities, so set_nice does nothing (the game says so).
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <ctype.h>
#include <dpmi.h>
#include <stdio.h>
#include <go32.h>
#include <stdlib.h>
#include <string.h>
#include <sys/movedata.h>

#include "psd_internal.h"

#define MAX_BLOCKS   512          /* MCBs in one walk (a busy 640K has ~40) */
#define MAX_OWNERS   128
#define MAX_HIDDEN   32

typedef struct {
	unsigned seg;                 /* the MCB's own segment; data at seg+1 */
	unsigned owner;
	unsigned size;                /* paragraphs */
	char type;
	char name[9];
} mcb;

typedef struct {
	unsigned short off, seg;
} farptr;

typedef struct {
	psd_proc procs[MAX_OWNERS];
	size_t nprocs;
	mcb blocks[MAX_BLOCKS];
	size_t nblocks;
	unsigned self_psp;
	unsigned hidden[MAX_HIDDEN];  /* drivers unlinked by a classic kill */
	size_t nhidden;
	char user[PSD_USER_MAX];
	int modern;                   /* only modern mode needs the chain scan */
	unsigned long chain_sig;      /* arena + IVT the last scan saw */
	long chained[MAX_OWNERS];     /* what it found */
	size_t nchained;
} dos_state;

/* ---- real-mode memory ---------------------------------------------- */

static void peek(unsigned seg, unsigned off, void *buf, size_t len)
{
	dosmemget(seg * 16u + off, len, buf);
}

static unsigned peekw(unsigned seg, unsigned off)
{
	unsigned short w;

	peek(seg, off, &w, sizeof w);
	return w;
}

static void pokew(unsigned seg, unsigned off, unsigned v)
{
	unsigned short w = (unsigned short)v;

	dosmemput(&w, sizeof w, seg * 16u + off);
}

/* INT 21h AH=52h: ES:BX -> DOS's List of Lists */
static int list_of_lists(unsigned *seg, unsigned *off)
{
	__dpmi_regs r;

	memset(&r, 0, sizeof r);
	r.h.ah = 0x52;
	if (__dpmi_int(0x21, &r) != 0)
		return PSD_ERR;
	*seg = r.x.es;
	*off = r.x.bx;
	return PSD_OK;
}

/* AX=5802h/5803h: is the UMB chain linked into the conventional one? */
static int umb_link_get(void)
{
	__dpmi_regs r;

	memset(&r, 0, sizeof r);
	r.x.ax = 0x5802;
	if (__dpmi_int(0x21, &r) != 0 || (r.x.flags & 1))
		return -1;
	return r.h.al;
}

static void umb_link_set(int on)
{
	__dpmi_regs r;

	memset(&r, 0, sizeof r);
	r.x.ax = 0x5803;
	r.x.bx = on ? 1 : 0;
	__dpmi_int(0x21, &r);
}

/* MCB names: up to 8 bytes, NUL- or space-padded, sometimes junk (DOS < 4) */
static void mcb_name(const unsigned char *raw, char *out)
{
	int i;

	for (i = 0; i < 8; i++) {
		unsigned char c = raw[i];
		if (c == 0)
			break;
		if (c < 0x20 || c > 0x7e) {
			i = 0;               /* not a name */
			break;
		}
		out[i] = (char)c;
	}
	while (i > 0 && out[i - 1] == ' ')
		i--;
	out[i] = '\0';
}

static int read_mcb(unsigned seg, mcb *m)
{
	unsigned char raw[16];

	peek(seg, 0, raw, sizeof raw);
	m->seg = seg;
	m->type = (char)raw[0];
	m->owner = raw[1] | (raw[2] << 8);
	m->size = raw[3] | (raw[4] << 8);
	mcb_name(raw + 8, m->name);
	return m->type == 'M' || m->type == 'Z';
}

/* Walk the arena (with the UMB chain linked in) into s->blocks. */
static int walk(dos_state *s)
{
	unsigned lol_seg, lol_off, seg;
	int link, guard;
	mcb m;

	s->nblocks = 0;
	if (list_of_lists(&lol_seg, &lol_off) != PSD_OK)
		return PSD_ERR;
	seg = peekw(lol_seg, lol_off - 2);

	link = umb_link_get();
	if (link == 0)
		umb_link_set(1);
	for (guard = 0; guard < MAX_BLOCKS && seg != 0 && seg < 0xffff; guard++) {
		if (!read_mcb(seg, &m))
			break;           /* end of a corrupted or unlinked chain */
		s->blocks[s->nblocks++] = m;
		if (m.type == 'Z')
			break;
		seg = seg + m.size + 1;
	}
	if (link == 0)
		umb_link_set(0);
	return s->nblocks > 0 ? PSD_OK : PSD_ERR;
}

/* ---- what points where ---------------------------------------------- */

static unsigned long vectors[256];  /* linear addresses of the IVT entries */

static void read_vectors(void)
{
	farptr ivt[256];
	int i;

	dosmemget(0, sizeof ivt, ivt);
	for (i = 0; i < 256; i++)
		vectors[i] = ivt[i].seg * 16ul + ivt[i].off;
}

/* Does any interrupt vector point into one of the blocks owner owns? */
static int hooked(const dos_state *s, unsigned owner)
{
	size_t i;
	int v;

	for (i = 0; i < s->nblocks; i++) {
		const mcb *m = &s->blocks[i];
		unsigned long lo = (m->seg + 1ul) * 16, hi = lo + m->size * 16ul;

		if (m->owner != owner)
			continue;
		for (v = 0; v < 256; v++)
			if (vectors[v] >= lo && vectors[v] < hi)
				return 1;
	}
	return 0;
}

/* Is the driver at seg [seg, seg+size) still linked in the device chain? */
static int in_device_chain(unsigned seg, unsigned size, farptr *prev_out, farptr *self_out)
{
	unsigned lol_seg, lol_off;
	farptr prev, cur;
	int guard;

	if (list_of_lists(&lol_seg, &lol_off) != PSD_OK)
		return 0;
	/* the NUL device header is at LoL+22h (DOS 3.1+) */
	prev.seg = (unsigned short)lol_seg;
	prev.off = (unsigned short)(lol_off + 0x22);
	for (guard = 0; guard < 256; guard++) {
		unsigned long lin;

		peek(prev.seg, prev.off, &cur, sizeof cur);
		if (cur.off == 0xffff)
			return 0;
		lin = cur.seg * 16ul + cur.off;
		if (lin >= seg * 16ul && lin < (seg + (unsigned long)size) * 16ul) {
			if (prev_out)
				*prev_out = prev;
			if (self_out)
				*self_out = cur;
			return 1;
		}
		prev = cur;
	}
	return 0;
}

/*
 * Interrupt chains. A TSR that hooks a vector keeps the previous handler as
 * a far pointer in its own memory and jumps there, so once a later TSR hooks
 * the same vector the IVT only shows the newest one (on the Gateway 2000:
 * UNIVBE, DOSKEY and MSCDEX sat behind CTMOUSE and SMARTDRV and looked
 * free to kill). So: every block an interrupt vector points into is part of
 * a chain; scan it for far pointers into other programs' blocks; those are
 * in a chain too; repeat until nothing new turns up. Protecting too much is
 * fine in modern mode; freeing a mid-chain hooker hangs the PC.
 */
/*
 * The program whose resident code seg:off is a handler in, or -1. A saved
 * handler is a CS:IP into that program's PSP block: CS is never below the
 * PSP (a .COM runs at CS = PSP, an .EXE above it) and IP lands past the
 * 256-byte PSP header. Anything else that happens to add up to an address
 * in the block (other segment arithmetic, FCB pointers into the PSP) is
 * not an interrupt chain.
 */
static long owner_at(const dos_state *s, unsigned seg, unsigned off)
{
	unsigned long lin = seg * 16ul + off;
	size_t i;

	for (i = 0; i < s->nblocks; i++) {
		const mcb *m = &s->blocks[i];
		unsigned psp = m->seg + 1;
		unsigned long lo = psp * 16ul, hi = lo + m->size * 16ul;

		if (lin < lo || lin >= hi)
			continue;
		if (m->owner != psp || seg < psp || lin < lo + 0x100)
			return -1;
		return (long)psp;
	}
	return -1;
}

static psd_proc *proc_of(dos_state *s, long pid)
{
	size_t i;

	for (i = 0; i < s->nprocs; i++)
		if (s->procs[i].pid == pid && !s->procs[i].is_daemon)
			return &s->procs[i];
	return NULL;
}

static void chain_guard(dos_state *s)
{
	static unsigned char buf[4096];
	unsigned char scan[MAX_BLOCKS], done[MAX_BLOCKS];
	unsigned lo_seg = 0xffff, hi_seg = 0;
	size_t i, j;
	int v, more = 1;

	memset(scan, 0, sizeof scan);
	memset(done, 0, sizeof done);
	for (i = 0; i < s->nblocks; i++) {
		const mcb *m = &s->blocks[i];
		unsigned long lo = (m->seg + 1ul) * 16, hi = lo + m->size * 16ul;

		if (getenv("PSD_CHAIN_DEBUG"))
			for (v = 0; v < 256; v++)
				if (vectors[v] >= lo && vectors[v] < hi)
					printf("chain: INT %02Xh -> block %04X (owner %04X)\n", v, m->seg, m->owner);
		if (m->owner != 0 && m->seg < lo_seg)
			lo_seg = m->seg;
		if (m->owner != 0 && m->seg + m->size + 1 > hi_seg)
			hi_seg = m->seg + m->size + 1;
		for (v = 0; v < 256 && m->owner != 0; v++)
			if (vectors[v] >= lo && vectors[v] < hi)
				scan[i] = 1;     /* the head of a chain (or DOS/a driver) */
	}
	while (more) {
		more = 0;
		for (i = 0; i < s->nblocks; i++) {
			const mcb *m = &s->blocks[i];
			unsigned long base = (m->seg + 1ul) * 16, len = m->size * 16ul, off;

			if (!scan[i] || done[i])
				continue;
			done[i] = 1;
			/* only programs' own memory: DOS's system blocks are full of
			 * PSP segment words that pair up into false pointers, and our
			 * own blocks hold DJGPP's transfer buffer (data, not chains) */
			if (m->owner == 0 || m->owner == 8 || m->owner == s->self_psp)
				continue;
			if (m->owner == m->seg + 1)
				off = 0x100;    /* skip the PSP header: parent links, not hooks */
			else
				off = 0;
			for (; off < len; off += sizeof buf - 3) {
				size_t n = len - off < sizeof buf ? len - off : sizeof buf, k;

				peek((unsigned)((base + off) >> 4), (unsigned)((base + off) & 15), buf, n);
				for (k = 0; k + 4 <= n; k++) {
					unsigned seg = buf[k + 2] | (buf[k + 3] << 8);
					long o;
					psd_proc *p;

					if (seg < lo_seg || seg >= hi_seg)
						continue;   /* cheap: most bytes are not pointers */
					o = owner_at(s, seg, buf[k] | (buf[k + 1] << 8));
					if (o < 0 || o == (long)m->owner || (p = proc_of(s, o)) == NULL ||
					    p->is_kernel)
						continue;
					p->is_kernel = 1;
					if (getenv("PSD_CHAIN_DEBUG"))
						printf("chain: block %04X (owner %04X) +%04lX -> %04X:%04X in %s\n",
						       m->seg, m->owner, off + k, seg,
						       buf[k] | (buf[k + 1] << 8), p->name);
					for (j = 0; j < s->nblocks; j++)
						if (s->blocks[j].owner == (unsigned)o && !done[j]) {
							scan[j] = 1;
							more = 1;
						}
				}
			}
		}
	}
}

static int is_hidden(const dos_state *s, unsigned seg)
{
	size_t i;

	for (i = 0; i < s->nhidden; i++)
		if (s->hidden[i] == seg)
			return 1;
	return 0;
}

/* ---- snapshot ------------------------------------------------------- */

static psd_proc *add_proc(dos_state *s, long pid)
{
	psd_proc *p;
	size_t i;

	for (i = 0; i < s->nprocs; i++)
		if (s->procs[i].pid == pid)
			return &s->procs[i];
	if (s->nprocs == MAX_OWNERS)
		return NULL;
	p = &s->procs[s->nprocs++];
	memset(p, 0, sizeof *p);
	p->pid = pid;
	p->uid = -1;
	psd_strlcpy(p->user, s->user, sizeof p->user);
	return p;
}

static unsigned long long ident(unsigned seg, const char *name)
{
	unsigned long long h = seg;

	while (*name)
		h = h * 131 + (unsigned char)*name++;
	return h ? h : 1;
}

static void finish(psd_proc *p, const char *name, int is_driver)
{
	if (name[0])
		psd_strlcpy(p->name, name, sizeof p->name);
	else
		snprintf(p->name, sizeof p->name, "%s%04lX", is_driver ? "DEV" : "PSP", p->pid);
	psd_strlcpy(p->comm, p->name, sizeof p->comm);
	psd_strlcpy(p->cmdline, p->name, sizeof p->cmdline);
	p->start_id = ident((unsigned)p->pid, p->name);
	psd_make_label(p);
}

/* Device drivers: sub-blocks of an owner-8 "SD" block (DOS 4+). */
static void add_drivers(dos_state *s, const mcb *sd)
{
	unsigned seg = sd->seg + 1, end = sd->seg + 1 + sd->size;
	int guard;

	for (guard = 0; guard < 64 && seg < end; guard++) {
		mcb sub;
		psd_proc *p;

		read_mcb(seg, &sub);
		if (sub.size == 0)
			break;
		if (sub.type == 'D' && !is_hidden(s, seg + 1) &&
		    (p = add_proc(s, (long)(seg + 1))) != NULL) {
			p->is_daemon = 1;
			p->rss_kb = sub.size * 16ul / 1024;
			p->is_kernel = in_device_chain(seg + 1, sub.size, NULL, NULL);
			finish(p, sub.name, 1);
		}
		seg += sub.size + 1;
	}
}

static int dos_snapshot(psd_backend *b, psd_proc **out, size_t *n)
{
	dos_state *s = b->state;
	size_t i;

	s->nprocs = 0;
	if (walk(s) != PSD_OK)
		return PSD_ERR;
	read_vectors();

	for (i = 0; i < s->nblocks; i++) {
		const mcb *m = &s->blocks[i];
		psd_proc *p;

		if (m->owner == 8 && strcmp(m->name, "SD") == 0) {
			add_drivers(s, m);
			continue;
		}
		/* a program's PSP block is the one it owns right after its MCB */
		if (m->owner == 0 || m->owner == 8 || m->owner != m->seg + 1)
			continue;
		p = add_proc(s, (long)m->owner);
		if (p == NULL)
			break;
		p->ppid = (long)peekw(m->owner, 0x16);
		finish(p, m->name, 0);
	}

	for (i = 0; i < s->nprocs; i++) {
		psd_proc *p = &s->procs[i];
		size_t j;

		if (p->is_daemon)
			continue;
		for (j = 0; j < s->nblocks; j++)
			if (s->blocks[j].owner == (unsigned)p->pid)
				p->rss_kb += s->blocks[j].size * 16ul;
		p->rss_kb /= 1024;
		if (p->ppid == p->pid)
			p->ppid = 0;     /* the root COMMAND.COM is its own parent */
		p->is_kernel = strcmp(p->name, "CWSDPMI") == 0 ||
		               hooked(s, (unsigned)p->pid);
	}
	if (s->modern) {
		/* the scan reads every hooked block: redo it only when the arena
		 * or the vectors changed, else reapply what it found */
		unsigned long sig = 2166136261ul;
		int v;

		for (i = 0; i < s->nblocks; i++)
			sig = (sig ^ (s->blocks[i].seg ^ (s->blocks[i].owner << 7) ^
			              ((unsigned long)s->blocks[i].size << 13))) * 16777619ul;
		for (v = 0; v < 256; v++)
			sig = (sig ^ vectors[v]) * 16777619ul;
		if (sig != s->chain_sig) {
			unsigned char before[MAX_OWNERS];

			for (i = 0; i < s->nprocs; i++)
				before[i] = (unsigned char)s->procs[i].is_kernel;
			chain_guard(s);
			s->nchained = 0;
			for (i = 0; i < s->nprocs; i++)
				if (s->procs[i].is_kernel && !before[i])
					s->chained[s->nchained++] = s->procs[i].pid;
			s->chain_sig = sig;
		} else {
			for (i = 0; i < s->nchained; i++) {
				psd_proc *p = proc_of(s, s->chained[i]);
				if (p)
					p->is_kernel = 1;
			}
		}
	}
	*out = s->procs;
	*n = s->nprocs;
	return PSD_OK;
}

/* ---- actions -------------------------------------------------------- */

static const psd_proc *still_there(dos_state *s, const psd_proc *p)
{
	psd_proc *snap;
	size_t n, i;
	psd_backend tmp;

	tmp.state = s;
	if (dos_snapshot(&tmp, &snap, &n) != PSD_OK)
		return NULL;
	for (i = 0; i < n; i++)
		if (snap[i].pid == p->pid)
			return snap[i].start_id == p->start_id ? &snap[i] : NULL;
	return NULL;
}

static int free_program(dos_state *s, unsigned psp)
{
	size_t i;
	int freed = 0;

	if (walk(s) != PSD_OK)
		return PSD_ERR;
	for (i = 0; i < s->nblocks; i++)
		if (s->blocks[i].owner == psp) {
			pokew(s->blocks[i].seg, 1, 0);
			freed++;
		}
	return freed ? PSD_OK : PSD_ERR_GONE;
}

static int unlink_driver(dos_state *s, const psd_proc *p)
{
	farptr prev, self, next;
	mcb sub;

	read_mcb((unsigned)p->pid - 1, &sub);   /* the driver's sub-block header */
	if (!in_device_chain((unsigned)p->pid, sub.size, &prev, &self))
		return PSD_ERR_GONE;
	peek(self.seg, self.off, &next, sizeof next);
	dosmemput(&next, sizeof next, prev.seg * 16ul + prev.off);
	if (s->nhidden < MAX_HIDDEN)
		s->hidden[s->nhidden++] = (unsigned)p->pid;
	return PSD_OK;
}

static int dos_signal(psd_backend *b, const psd_proc *p, psd_signal sig, int verify)
{
	dos_state *s = b->state;
	const psd_proc *cur;

	if (sig != PSD_SIG_TERM && sig != PSD_SIG_KILL)
		return PSD_ERR;          /* no HUP/STOP/CONT on DOS */
	if ((unsigned)p->pid == s->self_psp)
		return PSD_ERR_PROTECTED;
	cur = still_there(s, p);
	if (cur == NULL)
		return PSD_ERR_GONE;
	(void)verify;                /* the identity check above is cheap: always */
	if (cur->is_daemon)
		return unlink_driver(s, cur);
	return free_program(s, (unsigned)p->pid);
}

static int dos_set_nice(psd_backend *b, const psd_proc *p, int nice)
{
	(void)b;
	(void)p;
	(void)nice;
	return PSD_OK;               /* DOS has no priorities */
}

static long dos_self_pid(psd_backend *b)
{
	return (long)((dos_state *)b->state)->self_psp;
}

static void dos_close(psd_backend *b)
{
	free(b->state);
	b->state = NULL;
}

int psd_backend_native_open(psd_backend *b, const struct psd_config *cfg)
{
	dos_state *s;
	const char *me;
	mcb probe;
	unsigned lol_seg, lol_off;

	if (list_of_lists(&lol_seg, &lol_off) != PSD_OK ||
	    !read_mcb(peekw(lol_seg, lol_off - 2), &probe))
		return PSD_ERR_BACKEND;  /* not a DOS we understand */
	s = calloc(1, sizeof *s);
	if (s == NULL)
		return PSD_ERR;
	s->self_psp = _go32_info_block.linear_address_of_original_psp >> 4;
	s->modern = cfg->mode == PSD_MODE_MODERN;
	me = psd_platform_whoami();
	psd_strlcpy(s->user, me ? me : "dos", sizeof s->user);

	memset(b, 0, sizeof *b);
	b->name = "mcb";
	b->state = s;
	b->snapshot = dos_snapshot;
	b->signal = dos_signal;
	b->set_nice = dos_set_nice;
	b->self_pid = dos_self_pid;
	b->close = dos_close;
	return PSD_OK;
}
