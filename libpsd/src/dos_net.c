/*
 * dos_net.c - just enough IPv4 for psdoomd: UDP over a DOS packet driver
 *
 * DJGPP has no TCP/IP stack of its own, and the usual one (Watt-32) cannot
 * be linked into a GPL program, so the DOS build talks UDP itself:
 *
 *   packet driver  found by its "PKT DRVR" signature on INT 60h-80h; two
 *                  handles (IPv4 and ARP) whose receiver is a DPMI real-mode
 *                  callback filling a ring of conventional-memory slots
 *   ARP            asks for the next hop, answers for our own address
 *   IPv4 + UDP     no options, no fragments, no checksum on what we send
 *
 * Our address comes from $PSDOOM_IP / $PSDOOM_NETMASK / $PSDOOM_GATEWAY, else
 * from mTCP's configuration ($MTCPCFG: IPADDR, NETMASK, GATEWAY, which mTCP's
 * DHCP client fills in), else from WATTCP.CFG ($WATTCP.CFG: my_ip, netmask,
 * gateway). No DNS: psdoomd is given as an IP address.
 *
 * The packet driver keeps calling our receiver until we release the
 * handles, so they are released at exit (atexit), whichever way the game
 * ends; a crash that bypasses exit() leaves a callback behind, and the next
 * frame then hangs the PC.
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <dpmi.h>
#include <go32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/movedata.h>

#include "dos_net.h"
#include "psd_internal.h"

#define NSLOT       8
#define SLOT_BYTES  1536            /* a full Ethernet frame, rounded */
#define SLOT_PARAS  (SLOT_BYTES / 16)
#define ETH_MIN     60
#define ETH_MAX     1514
#define ARP_CACHE   4

#define ETH_IP      0x0800
#define ETH_ARP     0x0806

/* ---- receive ring, filled by the packet driver's upcall --------------- */

static volatile unsigned char slot_state[NSLOT];   /* 0 free, 1 filling, 2 ready */
static volatile unsigned short slot_len[NSLOT];
static volatile unsigned long slot_seq[NSLOT];
static volatile unsigned long rx_seq;
static volatile unsigned rx_dropped;
static unsigned ring_seg;                          /* NSLOT slots, then tx, then templates */
static int ring_sel;

static _go32_dpmi_seginfo cb_info;
static _go32_dpmi_registers cb_regs;
static int cb_alloc;

/* AX=0: "give me a buffer for CX bytes" (ES:DI, or 0:0 to drop it);
 * AX=1: "the buffer at DS:SI is full". Runs inside the driver's interrupt. */
static void receiver(_go32_dpmi_registers *r)
{
	int i;

	if (r->x.ax == 0) {
		r->x.es = 0;
		r->x.di = 0;
		if (r->x.cx > ETH_MAX) {
			rx_dropped++;
			return;
		}
		for (i = 0; i < NSLOT; i++)
			if (slot_state[i] == 0) {
				slot_state[i] = 1;
				slot_len[i] = r->x.cx;
				r->x.es = (unsigned short)(ring_seg + i * SLOT_PARAS);
				return;
			}
		rx_dropped++;
	} else {
		i = (int)((r->x.ds - ring_seg) / SLOT_PARAS);
		if (i >= 0 && i < NSLOT && slot_state[i] == 1) {
			slot_seq[i] = ++rx_seq;
			slot_state[i] = 2;
		}
	}
}

/* ---- state ------------------------------------------------------------ */

static int opened, failed;
static int pkt_int;
static int h_ip = -1, h_arp = -1;
static unsigned char my_mac[6];
static unsigned long my_ip, my_mask, my_gw;
static unsigned short my_port;
static unsigned short ip_id;
static struct { unsigned long ip; unsigned char mac[6]; } arp_cache[ARP_CACHE];
static int arp_next;

static unsigned tx_seg(void) { return ring_seg + NSLOT * SLOT_PARAS; }
static unsigned tpl_seg(void) { return tx_seg() + SLOT_PARAS; }

/* ---- packet driver calls ---------------------------------------------- */

static int pkt_call(__dpmi_regs *r)
{
	__dpmi_int(pkt_int, r);
	return (r->x.flags & 1) ? -1 : 0;     /* carry = error, DH = code */
}

static int find_driver(void)
{
	int v;

	for (v = 0x60; v <= 0x80; v++) {
		unsigned short vec[2];
		char sig[12];

		dosmemget(v * 4, sizeof vec, vec);
		if (vec[0] == 0 && vec[1] == 0)
			continue;
		dosmemget(vec[1] * 16ul + vec[0], sizeof sig, sig);
		if (memcmp(sig + 3, "PKT DRVR", 8) == 0)
			return v;
	}
	return -1;
}

static int access_type(unsigned short ethertype)
{
	__dpmi_regs r;
	unsigned char tpl[2] = { (unsigned char)(ethertype >> 8), (unsigned char)ethertype };
	unsigned off = ethertype == ETH_IP ? 0 : 2;

	dosmemput(tpl, 2, tpl_seg() * 16ul + off);
	memset(&r, 0, sizeof r);
	r.h.ah = 2;                  /* access_type */
	r.h.al = 1;                  /* class: DIX Ethernet */
	r.x.bx = 0xffff;             /* any type of card */
	r.h.dl = 0;                  /* any interface number */
	r.x.ds = (unsigned short)tpl_seg();
	r.x.si = (unsigned short)off;
	r.x.cx = 2;
	r.x.es = cb_info.rm_segment;
	r.x.di = cb_info.rm_offset;
	if (pkt_call(&r) != 0)
		return -1;
	return r.x.ax;
}

static void release(int h)
{
	__dpmi_regs r;

	if (h < 0)
		return;
	memset(&r, 0, sizeof r);
	r.h.ah = 3;
	r.x.bx = (unsigned short)h;
	pkt_call(&r);
}

void dosnet_close(void)
{
	release(h_ip);
	release(h_arp);
	h_ip = h_arp = -1;
	if (cb_alloc) {
		_go32_dpmi_free_real_mode_callback(&cb_info);
		cb_alloc = 0;
	}
	if (ring_sel) {
		__dpmi_free_dos_memory(ring_sel);
		ring_sel = 0;
	}
	opened = 0;
}

/* ---- addresses -------------------------------------------------------- */

int dosnet_parse_ip(const char *s, unsigned long *ip)
{
	unsigned a, b, c, d;
	char extra;

	if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 ||
	    a > 255 || b > 255 || c > 255 || d > 255)
		return PSD_ERR;
	*ip = ((unsigned long)a << 24) | ((unsigned long)b << 16) | (c << 8) | d;
	return PSD_OK;
}

/* "KEY value" (mTCP) or "key = value" (WATT), case-insensitive keys */
static void read_cfg(const char *path, const char *k_ip, const char *k_mask, const char *k_gw)
{
	FILE *f = fopen(path, "r");
	char line[256];

	if (f == NULL)
		return;
	while (fgets(line, sizeof line, f)) {
		char key[64], val[64];
		unsigned long v;

		if (sscanf(line, " %63[A-Za-z_.] = %63s", key, val) != 2 &&
		    sscanf(line, " %63[A-Za-z_.] %63s", key, val) != 2)
			continue;
		if (dosnet_parse_ip(val, &v) != PSD_OK)
			continue;
		if (strcasecmp(key, k_ip) == 0 && !my_ip)
			my_ip = v;
		else if (strcasecmp(key, k_mask) == 0 && !my_mask)
			my_mask = v;
		else if (strcasecmp(key, k_gw) == 0 && !my_gw)
			my_gw = v;
	}
	fclose(f);
}

static void find_my_address(void)
{
	const char *e;
	char path[256];

	if ((e = getenv("PSDOOM_IP")) != NULL)
		dosnet_parse_ip(e, &my_ip);
	if ((e = getenv("PSDOOM_NETMASK")) != NULL)
		dosnet_parse_ip(e, &my_mask);
	if ((e = getenv("PSDOOM_GATEWAY")) != NULL)
		dosnet_parse_ip(e, &my_gw);
	if ((e = getenv("MTCPCFG")) != NULL)
		read_cfg(e, "IPADDR", "NETMASK", "GATEWAY");
	if ((e = getenv("WATTCP.CFG")) != NULL) {
		snprintf(path, sizeof path, "%s\\WATTCP.CFG", e);
		read_cfg(path, "my_ip", "netmask", "gateway");
	}
	if (!my_mask)
		my_mask = 0xffffff00ul;
}

static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
static void put32(unsigned char *p, unsigned long v) { put16(p, (unsigned)(v >> 16)); put16(p + 2, (unsigned)v); }
static unsigned get16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned long get32(const unsigned char *p) { return ((unsigned long)get16(p) << 16) | get16(p + 2); }

/* ---- frames ----------------------------------------------------------- */

static int send_frame(const unsigned char *dst, unsigned type, const unsigned char *payload, size_t len)
{
	unsigned char f[ETH_MAX];
	__dpmi_regs r;
	size_t n = 14 + len;

	if (n > ETH_MAX)
		return PSD_ERR;
	memcpy(f, dst, 6);
	memcpy(f + 6, my_mac, 6);
	put16(f + 12, type);
	memcpy(f + 14, payload, len);
	if (n < ETH_MIN) {
		memset(f + n, 0, ETH_MIN - n);
		n = ETH_MIN;
	}
	dosmemput(f, n, tx_seg() * 16ul);
	memset(&r, 0, sizeof r);
	r.h.ah = 4;                  /* send_pkt */
	r.x.ds = (unsigned short)tx_seg();
	r.x.si = 0;
	r.x.cx = (unsigned short)n;
	return pkt_call(&r) == 0 ? PSD_OK : PSD_ERR;
}

static void arp_send(int op, const unsigned char *tmac, unsigned long tip)
{
	static const unsigned char bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	unsigned char a[28];

	put16(a, 1);                 /* Ethernet */
	put16(a + 2, ETH_IP);
	a[4] = 6;
	a[5] = 4;
	put16(a + 6, (unsigned)op);
	memcpy(a + 8, my_mac, 6);
	put32(a + 14, my_ip);
	if (tmac)
		memcpy(a + 18, tmac, 6);
	else
		memset(a + 18, 0, 6);
	put32(a + 24, tip);
	send_frame(tmac ? tmac : bcast, ETH_ARP, a, sizeof a);
}

static void arp_learn(unsigned long ip, const unsigned char *mac)
{
	int i;

	for (i = 0; i < ARP_CACHE; i++)
		if (arp_cache[i].ip == ip) {
			memcpy(arp_cache[i].mac, mac, 6);
			return;
		}
	arp_cache[arp_next].ip = ip;
	memcpy(arp_cache[arp_next].mac, mac, 6);
	arp_next = (arp_next + 1) % ARP_CACHE;
}

static const unsigned char *arp_lookup(unsigned long ip)
{
	int i;

	for (i = 0; i < ARP_CACHE; i++)
		if (arp_cache[i].ip == ip && ip != 0)
			return arp_cache[i].mac;
	return NULL;
}

/* The oldest received frame into f; its length, or 0. */
static size_t take_frame(unsigned char *f)
{
	unsigned long best = 0;
	int i, pick = -1;
	size_t len;

	for (i = 0; i < NSLOT; i++)
		if (slot_state[i] == 2 && (pick < 0 || slot_seq[i] < best)) {
			pick = i;
			best = slot_seq[i];
		}
	if (pick < 0)
		return 0;
	len = slot_len[pick];
	dosmemget((ring_seg + pick * SLOT_PARAS) * 16ul, len, f);
	slot_state[pick] = 0;
	return len;
}

/* Handle one frame: ARP inline; a UDP datagram for us is copied to out.
 * Returns the datagram's length, or -1 for anything else. */
static int handle_frame(const unsigned char *f, size_t len, void *out, size_t max)
{
	unsigned type;

	if (len < 14)
		return -1;
	type = get16(f + 12);
	f += 14;
	len -= 14;
	if (type == ETH_ARP && len >= 28 && get16(f) == 1 && get16(f + 2) == ETH_IP) {
		unsigned op = get16(f + 6);
		unsigned long sip = get32(f + 14), tip = get32(f + 24);

		if (sip != 0)
			arp_learn(sip, f + 8);
		if (op == 1 && tip == my_ip)
			arp_send(2, f + 8, sip);
		return -1;
	}
	if (type == ETH_IP && len >= 28 && (f[0] >> 4) == 4) {
		size_t ihl = (size_t)(f[0] & 15) * 4, tot = get16(f + 2), ulen;

		if (ihl < 20 || tot > len || tot < ihl + 8 || f[9] != 17 ||
		    (get16(f + 6) & 0x3fff) != 0 || get32(f + 16) != my_ip)
			return -1;           /* not UDP, a fragment, or not ours */
		f += ihl;
		if (get16(f + 2) != my_port)
			return -1;
		ulen = get16(f + 4);
		if (ulen < 8 || ulen > tot - ihl)
			return -1;
		ulen -= 8;
		if (ulen > max)
			ulen = max;
		memcpy(out, f + 8, ulen);
		return (int)ulen;
	}
	return -1;
}

/* Process received frames for up to ms (ARP included); a datagram ends it. */
static int pump(void *out, size_t max, int ms)
{
	static unsigned char f[SLOT_BYTES];
	long long until = psd_platform_now_ms() + ms;

	do {
		size_t len;

		while ((len = take_frame(f)) > 0) {
			int n = handle_frame(f, len, out, max);
			if (n >= 0 && out != NULL)
				return n;
		}
	} while (psd_platform_now_ms() < until);
	return -1;
}

static const unsigned char *resolve(unsigned long ip)
{
	unsigned long hop = ((ip ^ my_ip) & my_mask) == 0 ? ip : my_gw;
	const unsigned char *mac;
	int try;

	if (hop == 0)
		return NULL;
	for (try = 0; try < 3; try++) {
		if ((mac = arp_lookup(hop)) != NULL)
			return mac;
		arp_send(1, NULL, hop);
		{
			long long until = psd_platform_now_ms() + 300;
			while (psd_platform_now_ms() < until && arp_lookup(hop) == NULL)
				pump(NULL, 0, 10);
		}
	}
	return arp_lookup(hop);
}

/* ---- the API ------------------------------------------------------------ */

static unsigned checksum(const unsigned char *p, size_t len)
{
	unsigned long sum = 0;

	for (; len > 1; p += 2, len -= 2)
		sum += get16(p);
	if (len)
		sum += (unsigned)p[0] << 8;
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return (unsigned)(~sum & 0xffff);
}

int dosnet_send(unsigned long ip, unsigned short port, const void *buf, size_t len)
{
	unsigned char pkt[ETH_MAX - 14];
	const unsigned char *mac;

	if (!opened || len > sizeof pkt - 28)
		return PSD_ERR;
	if ((mac = resolve(ip)) == NULL) {
		fprintf(stderr, "psdoom: net: no ARP answer for the next hop\n");
		return PSD_ERR;
	}
	memset(pkt, 0, 28);
	pkt[0] = 0x45;
	put16(pkt + 2, (unsigned)(28 + len));
	put16(pkt + 4, ip_id++);
	put16(pkt + 6, 0x4000);      /* don't fragment */
	pkt[8] = 64;
	pkt[9] = 17;
	put32(pkt + 12, my_ip);
	put32(pkt + 16, ip);
	put16(pkt + 10, checksum(pkt, 20));
	put16(pkt + 20, my_port);
	put16(pkt + 22, port);
	put16(pkt + 24, (unsigned)(8 + len));
	put16(pkt + 26, 0);          /* no UDP checksum */
	memcpy(pkt + 28, buf, len);
	return send_frame(mac, ETH_IP, pkt, 28 + len);
}

int dosnet_recv(void *buf, size_t max, int ms)
{
	if (!opened)
		return -1;
	return pump(buf, max, ms);
}

int dosnet_open(void)
{
	__dpmi_regs r;

	if (opened)
		return PSD_OK;
	if (failed)
		return PSD_ERR;
	failed = 1;                  /* until we get all the way */

	find_my_address();
	if (my_ip == 0) {
		fprintf(stderr, "psdoom: net: no IP address: set PSDOOM_IP (and PSDOOM_GATEWAY), "
		        "or MTCPCFG / WATTCP.CFG\n");
		return PSD_ERR;
	}
	if ((pkt_int = find_driver()) < 0) {
		fprintf(stderr, "psdoom: net: no packet driver on INT 60h-80h\n");
		return PSD_ERR;
	}
	memset(&r, 0, sizeof r);
	r.x.ax = 0x01ff;             /* driver_info */
	if (pkt_call(&r) != 0 || r.h.ch != 1) {
		fprintf(stderr, "psdoom: net: the packet driver on INT %02Xh is not Ethernet\n", pkt_int);
		return PSD_ERR;
	}

	/* slots, one tx buffer, the type templates */
	ring_seg = (unsigned)__dpmi_allocate_dos_memory((NSLOT + 2) * SLOT_PARAS, &ring_sel);
	if ((int)ring_seg == -1) {
		ring_sel = 0;
		fprintf(stderr, "psdoom: net: no conventional memory for packet buffers\n");
		return PSD_ERR;
	}
	/* the upcall runs inside the driver's interrupt: nothing it touches may
	 * be paged out (an end-marker function would not bound it: the compiler
	 * orders functions as it likes, so lock a generous span) */
	_go32_dpmi_lock_code((void *)receiver, 4096);
	_go32_dpmi_lock_data((void *)slot_state, sizeof slot_state);
	_go32_dpmi_lock_data((void *)slot_len, sizeof slot_len);
	_go32_dpmi_lock_data((void *)slot_seq, sizeof slot_seq);
	_go32_dpmi_lock_data((void *)&rx_seq, sizeof rx_seq);
	_go32_dpmi_lock_data((void *)&rx_dropped, sizeof rx_dropped);
	_go32_dpmi_lock_data((void *)&ring_seg, sizeof ring_seg);
	_go32_dpmi_lock_data((void *)&cb_regs, sizeof cb_regs);
	cb_info.pm_offset = (unsigned long)receiver;
	if (_go32_dpmi_allocate_real_mode_callback_retf(&cb_info, &cb_regs) != 0) {
		dosnet_close();
		fprintf(stderr, "psdoom: net: no DPMI real-mode callback\n");
		return PSD_ERR;
	}
	cb_alloc = 1;

	h_ip = access_type(ETH_IP);
	h_arp = access_type(ETH_ARP);
	if (h_ip < 0 || h_arp < 0) {
		dosnet_close();
		fprintf(stderr, "psdoom: net: the packet driver refused our handles\n");
		return PSD_ERR;
	}
	memset(&r, 0, sizeof r);
	r.h.ah = 6;                  /* get_address */
	r.x.bx = (unsigned short)h_ip;
	r.x.es = (unsigned short)tx_seg();
	r.x.di = 0;
	r.x.cx = 6;
	if (pkt_call(&r) != 0) {
		dosnet_close();
		fprintf(stderr, "psdoom: net: cannot read the card's address\n");
		return PSD_ERR;
	}
	dosmemget(tx_seg() * 16ul, 6, my_mac);

	my_port = (unsigned short)(40000 + psd_platform_now_ms() % 20000);
	opened = 1;
	failed = 0;
	atexit(dosnet_close);
	fprintf(stderr, "psdoom: net: packet driver on INT %02Xh, %lu.%lu.%lu.%lu port %u\n",
	        pkt_int, my_ip >> 24, (my_ip >> 16) & 255, (my_ip >> 8) & 255, my_ip & 255, my_port);
	return PSD_OK;
}
