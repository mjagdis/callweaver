/*
 * Vale - a library for media streaming.
 *
 * udp.c - A simple abstraction of UDP ports.
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *
 * Copyright (C) 2006 Steve Underwood
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id: udp.c,v 1.2 2007/04/13 12:24:49 steveu Exp $
 */

/*! \file */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/sockaddr.h"
#include "callweaver/stun.h"
#include "callweaver/udp.h"

/*
 *
 * A simple abstration of UDP ports so they can be handed between streaming
 * protocols, such as when RTP switches to UDPTL on the same IP port.
 *
 * RFC3489 STUN is integrated for NAT traversal.
 * 
 */


static int rfc3489_active = 0;


static uint16_t make_mask16(uint16_t x)
{
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    return x;
}
/*- End of function --------------------------------------------------------*/

int udp_socket_group_create_and_bind(udp_state_t *s, int nelem, int nochecksums, struct sockaddr *addr, int lowest_port, int highest_port)
{
	int i;
	int base;
	int port_mask;
	int starting_point;
	int err;
	int ret = -1;

	/* Find a port or group of ports we can bind to, within a specified numeric range */
	port_mask = make_mask16(nelem - 1);
	/* Trim the port range to suitable multiples of the size of the blocks of ports being used. */
	lowest_port = (lowest_port + port_mask) & ~port_mask;
	if (highest_port < lowest_port + nelem - 1)
		return ret;
	highest_port &= ~port_mask;
	base = lowest_port;
	if (highest_port != lowest_port)
		base += ((cw_random()%(highest_port - lowest_port + 1)) & ~port_mask);
	starting_point = base;

	for (i = 0; i < nelem; i++) {
		s[i].peer.sa.sa_family = AF_UNSPEC;
		s[i].rfc3489_local.sa.sa_family = AF_UNSPEC;
		s[i].nochecksums = nochecksums;
		s[i].rfc3489_state = RFC3489_STATE_IDLE;
	}

	for (;;) {
		for (i = 0; i < nelem; i++) {
			cw_sockaddr_set_port(addr, base + i);
			if ((s[i].fd = socket_cloexec(addr->sa_family, SOCK_DGRAM, 0)) < 0)
				break;
			fcntl(s[i].fd, F_SETFL, fcntl(s[i].fd, F_GETFL) | O_NONBLOCK);
#ifdef SO_NO_CHECK
			if (nochecksums)
				setsockopt(s[i].fd, SOL_SOCKET, SO_NO_CHECK, &nochecksums, sizeof(nochecksums));
#endif
			if (bind(s[i].fd, addr, cw_sockaddr_len(addr)))
				break;

			cw_sockaddr_copy(&s[i].local.sa, addr);
		}
		if (i >= nelem) {
			/* We must have bound all ports OK. */
			ret = 0;
			break;
		}

		err = errno;

		while (--i >= 0)
			close(s[i].fd);

		base += (port_mask + 1);
		if (base > highest_port)
			base = lowest_port;

		if (base == starting_point || err != EADDRINUSE)
			break;
	}

	return ret;
}
/*- End of function --------------------------------------------------------*/

void udp_socket_set_far(udp_state_t *s, const struct sockaddr *far)
{
	cw_sockaddr_copy(&s->peer.sa, far);
}
/*- End of function --------------------------------------------------------*/

int udp_socket_set_tos(udp_state_t *s, int tos)
{
    if (s == NULL)
        return -1;
    return setsockopt(s->fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
}
/*- End of function --------------------------------------------------------*/

void udp_socket_set_nat(udp_state_t *s, int nat_mode)
{
    if (s == NULL)
        return;
    s->nat = nat_mode;
    if (nat_mode  &&  s->rfc3489_state == RFC3489_STATE_IDLE  &&  rfc3489_active)
    {
        /* FIXME: peer should be stunserver_ip */
        cw_stun_bindrequest(s->fd, &s->local.sa, sizeof(s->local), &s->peer.sa, sizeof(s->peer), &s->rfc3489_local.sin);
        s->rfc3489_state = RFC3489_STATE_REQUEST_PENDING;
    }
}
/*- End of function --------------------------------------------------------*/

int udp_socket_restart(udp_state_t *s)
{
    if (s == NULL)
        return -1;

    s->peer.sa.sa_family = AF_UNSPEC;
    return 0;
}
/*- End of function --------------------------------------------------------*/

int udp_socket_fd(udp_state_t *s)
{
    if (s == NULL)
        return -1;
    return s->fd;
}
/*- End of function --------------------------------------------------------*/

int udp_socket_recv(udp_state_t *s, void *buf, size_t size, int flags, int *action)
{
	struct cw_sockaddr_net addr;
	socklen_t salen = sizeof(addr);
	int res;

	*action = 0;

	if (s == NULL  ||  s->fd < 0)
		return 0;

	if ((res = recvfrom(s->fd, buf, size, flags, &addr.sa, &salen)) >= 0) {
		if (s->nat && (!rfc3489_active || (rfc3489_active  &&  s->rfc3489_state == RFC3489_STATE_IDLE))) {
			/* Send to whoever sent to us */
			if (cw_sockaddr_cmp(&addr.sa, &s->peer.sa, -1, 1)) {
				cw_sockaddr_copy(&s->peer.sa, &addr.sa);
				*action |= 1;
			}
		}
		if (s->rfc3489_state == RFC3489_STATE_REQUEST_PENDING) {
			cw_stun_handle_packet(s->fd, &addr.sin, buf, res, &s->rfc3489_local.sin);
			if (s->rfc3489_local.sa.sa_family != AF_UNSPEC) {
				s->rfc3489_state = RFC3489_STATE_RESPONSE_RECEIVED;
				*action |= 2;
				errno = EAGAIN;
				return -1;
			}
		}
	}
	return res;
}
/*- End of function --------------------------------------------------------*/

int udp_socket_send(udp_state_t *s, const void *buf, size_t size, int flags)
{
	int ret = 0;

	if (s && s->fd >= 0 && cw_sockaddr_get_port(&s->peer.sa))
		ret = sendto(s->fd, buf, size, flags, &s->peer.sa, sizeof(s->peer));
	return ret;
}
/*- End of function --------------------------------------------------------*/

/*- End of file ------------------------------------------------------------*/
