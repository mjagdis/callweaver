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

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct cw_udptl_s;
typedef struct cw_udptl_s cw_udptl_t;

struct cw_udptl_protocol
{
	const char * const type;
	/* Get UDPTL struct, or NULL if unwilling to transfer */
	cw_udptl_t *(*get_udptl_info)(struct cw_channel *chan);
	/* Set UDPTL peer */
	int (* const set_udptl_peer)(struct cw_channel *chan, cw_udptl_t *peer);
	struct cw_udptl_protocol *next;
};

typedef int (*cw_udptl_callback)(cw_udptl_t *udptl, struct cw_frame *f, void *data);

extern CW_API_PUBLIC cw_udptl_t *cw_udptl_new_with_sock_info(struct sched_context *sched,
                                            struct io_context *io,
                                            int callbackmode,
                                            udp_state_t *sock_info);

extern CW_API_PUBLIC int cw_udptl_set_active(cw_udptl_t *udptl, int active);

extern CW_API_PUBLIC void cw_udptl_set_peer(cw_udptl_t *udptl, struct sockaddr_in *them);

extern CW_API_PUBLIC void cw_udptl_get_peer(cw_udptl_t *udptl, struct sockaddr_in *them);

extern CW_API_PUBLIC void cw_udptl_get_us(cw_udptl_t *udptl, struct sockaddr_in *us);

extern CW_API_PUBLIC int cw_udptl_get_stunstate(cw_udptl_t *udptl);

extern CW_API_PUBLIC void cw_udptl_destroy(cw_udptl_t *udptl);

extern CW_API_PUBLIC void cw_udptl_reset(cw_udptl_t *udptl);

extern CW_API_PUBLIC void cw_udptl_set_callback(cw_udptl_t *udptl, cw_udptl_callback callback);

extern CW_API_PUBLIC void cw_udptl_set_data(cw_udptl_t *udptl, void *data);

extern CW_API_PUBLIC int cw_udptl_write(cw_udptl_t *udptl, struct cw_frame *f);

extern CW_API_PUBLIC struct cw_frame *cw_udptl_read(cw_udptl_t *udptl);

extern CW_API_PUBLIC int cw_udptl_fd(cw_udptl_t *udptl);

extern CW_API_PUBLIC udp_state_t *cw_udptl_udp_socket(cw_udptl_t *udptl,
                                   udp_state_t *sock_info);

extern CW_API_PUBLIC int cw_udptl_settos(cw_udptl_t *udptl, int tos);

extern CW_API_PUBLIC void cw_udptl_set_m_type(cw_udptl_t *udptl, int pt);

extern CW_API_PUBLIC void cw_udptl_set_udptlmap_type(cw_udptl_t *udptl, int pt,
                                  char *mimeType, char *mimeSubtype);

extern CW_API_PUBLIC int cw_udptl_lookup_code(cw_udptl_t* udptl, int is_cw_format, int code);

extern CW_API_PUBLIC void cw_udptl_offered_from_local(cw_udptl_t *udptl, int local);

extern CW_API_PUBLIC int cw_udptl_get_preferred_error_correction_scheme(cw_udptl_t *udptl);

extern CW_API_PUBLIC int cw_udptl_get_current_error_correction_scheme(cw_udptl_t *udptl);

extern CW_API_PUBLIC void cw_udptl_set_error_correction_scheme(cw_udptl_t *udptl, int ec);

extern CW_API_PUBLIC int cw_udptl_get_local_max_datagram(cw_udptl_t *udptl);

extern CW_API_PUBLIC void cw_udptl_set_local_max_datagram(cw_udptl_t *udptl, int max_datagram);

extern CW_API_PUBLIC int cw_udptl_get_far_max_datagram(cw_udptl_t *udptl);

extern CW_API_PUBLIC void cw_udptl_set_far_max_datagram(cw_udptl_t* udptl, int max_datagram);

extern CW_API_PUBLIC void cw_udptl_get_current_formats(cw_udptl_t *udptl,
									int *cw_formats, int *non_cw_formats);

extern CW_API_PUBLIC void cw_udptl_setnat(cw_udptl_t *udptl, int nat);

extern CW_API_PUBLIC enum cw_bridge_result cw_udptl_bridge(struct cw_channel *c0, struct cw_channel *c1, int flags, struct cw_frame **fo, struct cw_channel **rc);

extern CW_API_PUBLIC int cw_udptl_proto_register(struct cw_udptl_protocol *proto);

extern CW_API_PUBLIC void cw_udptl_proto_unregister(struct cw_udptl_protocol *proto);

extern CW_API_PUBLIC void cw_udptl_stop(cw_udptl_t *udptl);

int cw_udptl_init(void);

void cw_udptl_reload(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
