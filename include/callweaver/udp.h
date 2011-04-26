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

#if !defined(_VALE_UDP_H_)
#define _VALE_UDP_H_

#include <sys/socket.h>
#include <netinet/in.h>


#if !defined(FALSE)
#define FALSE 0
#endif
#if !defined(TRUE)
#define TRUE (!FALSE)
#endif

typedef struct udp_state_s udp_state_t;

#if defined(__cplusplus)
extern "C"
{
#endif

extern CW_API_PUBLIC udp_state_t *udp_socket_create(int nochecksums);

extern CW_API_PUBLIC udp_state_t *udp_socket_create_and_bind(int nochecksums, struct in_addr *addr, int lowest_port, int highest_port);

extern CW_API_PUBLIC udp_state_t *udp_socket_group_create_and_bind(int group, int nochecksums, struct in_addr *addr, int lowest_port, int highest_port);

extern CW_API_PUBLIC int udp_socket_destroy(udp_state_t *s);

extern CW_API_PUBLIC int udp_socket_destroy_group(udp_state_t *s);

extern CW_API_PUBLIC udp_state_t *udp_socket_find_group_element(udp_state_t *s, int element);

extern CW_API_PUBLIC int udp_socket_restart(udp_state_t *s);

extern CW_API_PUBLIC int udp_socket_fd(udp_state_t *s);

extern CW_API_PUBLIC int udp_socket_get_rfc3489_state(udp_state_t *s);

extern CW_API_PUBLIC void udp_socket_set_rfc3489_state(udp_state_t *s, int state);

extern CW_API_PUBLIC const struct sockaddr_in *udp_socket_get_rfc3489(udp_state_t *s);

extern CW_API_PUBLIC void udp_socket_set_rfc3489(udp_state_t *s, const struct sockaddr_in *rfc3489);

extern CW_API_PUBLIC int udp_socket_set_local(udp_state_t *s, const struct sockaddr_in *us);

extern CW_API_PUBLIC void udp_socket_set_far(udp_state_t *s, const struct sockaddr_in *far);

extern CW_API_PUBLIC void udp_socket_group_set_far(udp_state_t *s, const struct sockaddr_in *far);

extern CW_API_PUBLIC int udp_socket_set_tos(udp_state_t *s, int tos);

extern CW_API_PUBLIC void udp_socket_set_nat(udp_state_t *s, int nat_mode);

extern CW_API_PUBLIC void udp_socket_group_set_nat(udp_state_t *s, int nat_mode);

extern CW_API_PUBLIC const struct sockaddr_in *udp_socket_get_local(udp_state_t *s);

extern CW_API_PUBLIC const struct sockaddr_in *udp_socket_get_apparent_local(udp_state_t *s);

extern CW_API_PUBLIC const struct sockaddr_in *udp_socket_get_far(udp_state_t *s);

extern CW_API_PUBLIC int udp_socket_recvfrom(udp_state_t *s,
                        void *buf,
                        size_t size,
                        int flags,
                        struct sockaddr *sa,
                        socklen_t *salen,
                        int *action);

extern CW_API_PUBLIC int udp_socket_recv(udp_state_t *s,
                    void *buf,
                    size_t size,
                    int flags,
                    int *action);

extern CW_API_PUBLIC int udp_socket_send(udp_state_t *s, const void *buf, size_t size, int flags);

extern CW_API_PUBLIC int udp_socket_sendto(udp_state_t *s,
                      const void *buf,
                      size_t size,
                      int flags,
                      struct sockaddr *sa,
                      socklen_t salen);
                          
#if defined(__cplusplus)
}
#endif

#endif
/*- End of file ------------------------------------------------------------*/
