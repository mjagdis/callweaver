/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * UDPTL support for T.38
 * 
 * Copyright (C) 2005, Steve Underood, partly based on RTP code which is
 * Copyright (C) 1999-2004, Digium, Inc.
 *
 * Steve Underood <steveu@coppice.org>
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

#ifndef _CALLWEAVER_UDPTL_H
#define _CALLWEAVER_UDPTL_H

#include "callweaver/frame.h"
#include "callweaver/io.h"
#include "callweaver/sched.h"
#include "callweaver/channel.h"

#include <netinet/in.h>

#if !defined(USE_VALE)
enum
{
    UDPTL_ERROR_CORRECTION_NONE,
    UDPTL_ERROR_CORRECTION_FEC,
    UDPTL_ERROR_CORRECTION_REDUNDANCY
};
#endif

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct opbx_udptl_s;
typedef struct opbx_udptl_s opbx_udptl_t;

struct opbx_udptl_protocol
{
	/* Get UDPTL struct, or NULL if unwilling to transfer */
	opbx_udptl_t *(*get_udptl_info)(struct opbx_channel *chan);
	/* Set UDPTL peer */
	int (* const set_udptl_peer)(struct opbx_channel *chan, opbx_udptl_t *peer);
	const char * const type;
	struct opbx_udptl_protocol *next;
};

typedef int (*opbx_udptl_callback)(opbx_udptl_t *udptl, struct opbx_frame *f, void *data);

opbx_udptl_t *opbx_udptl_new_with_sock_info(struct sched_context *sched,
                                            struct io_context *io,
                                            int callbackmode,
                                            udp_state_t *sock_info);

int opbx_udptl_set_active(opbx_udptl_t *udptl, int active);

void opbx_udptl_set_peer(opbx_udptl_t *udptl, struct sockaddr_in *them);

void opbx_udptl_get_peer(opbx_udptl_t *udptl, struct sockaddr_in *them);

void opbx_udptl_get_us(opbx_udptl_t *udptl, struct sockaddr_in *us);

int opbx_udptl_get_stunstate(opbx_udptl_t *udptl);

void opbx_udptl_destroy(opbx_udptl_t *udptl);

void opbx_udptl_reset(opbx_udptl_t *udptl);

void opbx_udptl_set_callback(opbx_udptl_t *udptl, opbx_udptl_callback callback);

void opbx_udptl_set_data(opbx_udptl_t *udptl, void *data);

int opbx_udptl_write(opbx_udptl_t *udptl, struct opbx_frame *f);

struct opbx_frame *opbx_udptl_read(opbx_udptl_t *udptl);

int opbx_udptl_fd(opbx_udptl_t *udptl);

udp_state_t *opbx_udptl_udp_socket(opbx_udptl_t *udptl,
                                   udp_state_t *sock_info);

int opbx_udptl_settos(opbx_udptl_t *udptl, int tos);

void opbx_udptl_set_m_type(opbx_udptl_t *udptl, int pt);

void opbx_udptl_set_udptlmap_type(opbx_udptl_t *udptl, int pt,
                                  char *mimeType, char *mimeSubtype);

int opbx_udptl_lookup_code(opbx_udptl_t* udptl, int is_opbx_format, int code);

void opbx_udptl_offered_from_local(opbx_udptl_t *udptl, int local);

int opbx_udptl_get_error_correction_scheme(opbx_udptl_t *udptl);

void opbx_udptl_set_error_correction_scheme(opbx_udptl_t *udptl, int ec);

int opbx_udptl_get_local_max_datagram(opbx_udptl_t *udptl);

void opbx_udptl_set_local_max_datagram(opbx_udptl_t *udptl, int max_datagram);

int opbx_udptl_get_far_max_datagram(opbx_udptl_t *udptl);

void opbx_udptl_set_far_max_datagram(opbx_udptl_t* udptl, int max_datagram);

void opbx_udptl_get_current_formats(opbx_udptl_t *udptl,
									int *opbx_formats, int *non_opbx_formats);

void opbx_udptl_setnat(opbx_udptl_t *udptl, int nat);

enum opbx_bridge_result opbx_udptl_bridge(struct opbx_channel *c0, struct opbx_channel *c1, int flags, struct opbx_frame **fo, struct opbx_channel **rc);

int opbx_udptl_proto_register(struct opbx_udptl_protocol *proto);

void opbx_udptl_proto_unregister(struct opbx_udptl_protocol *proto);

void opbx_udptl_stop(opbx_udptl_t *udptl);

void opbx_udptl_init(void);

void opbx_udptl_reload(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
