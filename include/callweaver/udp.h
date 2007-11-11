/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Steve Underwood
 *
 * Steve Underwood <steveu@coppice.org>
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 *
 * $HeadURL$
 * $Revision$
 */

/*
 * \file udp.h
 * A simple abstration of UDP ports so they can be handed between streaming
 * protocols, such as when RTP switches to UDPTL on the same IP port.
 *
 */

#if !defined(_CALLWEAVER_UDP_H)
#define _CALLWEAVER_UDP_H

typedef struct udp_state_s udp_state_t;

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

udp_state_t *udp_socket_create(int nochecksums);

udp_state_t *udp_socket_group_create_and_bind(int group, int nochecksums, struct in_addr *addr, int lowest_port, int highest_port);

int udp_socket_destroy(udp_state_t *s);

int udp_socket_destroy_group(udp_state_t *s);

udp_state_t *udp_socket_find_group_element(udp_state_t *s, int element);

int udp_socket_restart(udp_state_t *s);

int udp_socket_fd(udp_state_t *s);

int udp_socket_get_stunstate(udp_state_t *s);

void udp_socket_set_stunstate(udp_state_t *s, int state);

const struct sockaddr_in *udp_socket_get_stun(udp_state_t *s);

void udp_socket_set_stun(udp_state_t *s, const struct sockaddr_in *stun);

int udp_socket_set_local(udp_state_t *s, const struct sockaddr_in *us);

void udp_socket_set_far(udp_state_t *s, const struct sockaddr_in *them);

int udp_socket_set_tos(udp_state_t *s, int tos);

void udp_socket_set_nat(udp_state_t *s, int nat_mode);

const struct sockaddr_in *udp_socket_get_us(udp_state_t *s);

const struct sockaddr_in *udp_socket_get_apparent_us(udp_state_t *s);

const struct sockaddr_in *udp_socket_get_them(udp_state_t *s);

int udp_socket_recvfrom(udp_state_t *s,
                        void *buf,
                        size_t size,
			            int flags,
                        struct sockaddr *sa,
                        socklen_t *salen,
                        int *actions);

int udp_socket_recv(udp_state_t *s,
                    void *buf,
                    size_t size,
                    int flags,
                    int *action);

int udp_socket_send(udp_state_t *s, const void *buf, size_t size, int flags);

int udp_socket_sendto(udp_state_t *s,
                      const void *buf,
                      size_t size,
                      int flags,
                      struct sockaddr *sa,
                      socklen_t salen);
                          
#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_UDP_H */
