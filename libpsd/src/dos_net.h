/*
 * dos_net.h - just enough IPv4 for psdoomd: UDP over a DOS packet driver
 *
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PSD_DOS_NET_H
#define PSD_DOS_NET_H

#include <stddef.h>

/* Find the packet driver, read our address, start receiving. PSD_OK or PSD_ERR
 * (with the reason on stderr). Idempotent; released at exit. */
int  dosnet_open(void);
int  dosnet_parse_ip(const char *s, unsigned long *ip);   /* dotted quad only */
/* One datagram to ip:port from our port. */
int  dosnet_send(unsigned long ip, unsigned short port, const void *buf, size_t len);
/* The next datagram to our port, waiting up to ms; its length, or -1. */
int  dosnet_recv(void *buf, size_t max, int ms);
void dosnet_close(void);

#endif
