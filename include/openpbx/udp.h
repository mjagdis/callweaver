/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Steve Underwood
 *
 * Steve Underwood <steveu@coppice.org>
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 * \file udp.h
 * A simple abstration of UDP ports so they can be handed between streaming
 * protocols, such as when RTP switches to UDPTL on the same IP port.
 *
 */

#if !defined(_OPENPBX_UDP_H)
#define _OPENPBX_UDP_H

typedef struct udp_socket_info_s udp_socket_info_t;

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

udp_socket_info_t *udp_socket_create(int nochecksums);

int udp_socket_destroy(udp_socket_info_t *info);

int udp_socket_restart(udp_socket_info_t *info);

int udp_socket_fd(udp_socket_info_t *info);

int udp_socket_set_us(udp_socket_info_t *info, struct sockaddr_in *us);

void udp_socket_set_them(udp_socket_info_t *info, struct sockaddr_in *them);

int udp_socket_set_tos(udp_socket_info_t *info, int tos);

void udp_socket_set_nat(udp_socket_info_t *info, int nat_mode);

const struct sockaddr_in *udp_socket_get_us(udp_socket_info_t *info);

const struct sockaddr_in *udp_socket_get_them(udp_socket_info_t *info);

int udp_socket_recvfrom(udp_socket_info_t *info, void *buf, size_t size,
			int flags, struct sockaddr *sa, socklen_t *salen, int *actions);

int udp_socket_sendto(udp_socket_info_t *info, void *buf, size_t size, int flags);

int udp_socket_get_stunstate(udp_socket_info_t *info);

void udp_socket_set_stunstate(udp_socket_info_t *info, int state);

struct sockaddr_in *udp_socket_get_stun(udp_socket_info_t *info);

void udp_socket_set_stun(udp_socket_info_t *info, struct sockaddr_in *stun);


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _OPENPBX_UDP_H */
