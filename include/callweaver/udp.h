/*
 * Vale - a library for media streaming.
 *
 * udp.h - A simple abstraction of UDP ports.
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
 * $Id: udp.h,v 1.1.1.1 2007/04/03 13:13:37 steveu Exp $
 */

/*
 * \file udp.h
 * A simple abstration of UDP ports so they can be handed between streaming
 * protocols, such as when RTP switches to UDPTL on the same IP port.
 *
 */

#if !defined(_CALLWEAVER_UDP_H_)
#define _CALLWEAVER_UDP_H_

#include <sys/socket.h>
#include <netinet/in.h>

#include "callweaver/sockaddr.h"


#if !defined(FALSE)
#  define FALSE 0
#endif
#if !defined(TRUE)
#  define TRUE (!FALSE)
#endif


enum
{
    RFC3489_STATE_IDLE = 0,
    RFC3489_STATE_REQUEST_PENDING,
    RFC3489_STATE_RESPONSE_RECEIVED
};


struct udp_state_s {
	int fd;
	struct cw_sockaddr_net local;
	struct cw_sockaddr_net peer;
	struct cw_sockaddr_net rfc3489_local;
	int nochecksums;
	int nat;
	int rfc3489_state;
};

typedef struct udp_state_s udp_state_t;


extern CW_API_PUBLIC int udp_socket_group_create_and_bind(udp_state_t *s, int nelem, int nochecksums, struct cw_sockaddr_net *addr, int lowest_port, int highest_port);

extern CW_API_PUBLIC int udp_socket_restart(udp_state_t *s);

extern CW_API_PUBLIC int udp_socket_fd(udp_state_t *s);

extern CW_API_PUBLIC void udp_socket_set_far(udp_state_t *s, const struct sockaddr *far);

extern CW_API_PUBLIC int udp_socket_set_tos(udp_state_t *s, int tos);

extern CW_API_PUBLIC void udp_socket_set_nat(udp_state_t *s, int nat_mode);

extern CW_API_PUBLIC void udp_socket_group_set_nat(udp_state_t *s, int nat_mode);

extern CW_API_PUBLIC int udp_socket_recv(udp_state_t *s, void *buf, size_t size, int flags, int *action);

extern CW_API_PUBLIC int udp_socket_send(udp_state_t *s, const void *buf, size_t size, int flags);


static inline __attribute__ (( __pure__, __nonnull__ (1) )) struct sockaddr *udp_socket_get_apparent_local(udp_state_t *s)
{
	return (s->rfc3489_state == RFC3489_STATE_RESPONSE_RECEIVED ? &s->rfc3489_local.sa : &s->local.sa);
}


#endif
