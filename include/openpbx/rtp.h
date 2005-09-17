/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * Real-time Transport Protocol support
 */

#ifndef _OPENPBX_RTP_H
#define _OPENPBX_RTP_H

#include "openpbx/frame.h"
#include "openpbx/io.h"
#include "openpbx/sched.h"
#include "openpbx/channel.h"

#include <netinet/in.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Codes for RTP-specific data - not defined by our OPBX_FORMAT codes */
/*! DTMF (RFC2833) */
#define OPBX_RTP_DTMF            (1 << 0)
/*! 'Comfort Noise' (RFC3389) */
#define OPBX_RTP_CN              (1 << 1)
/*! DTMF (Cisco Proprietary) */
#define OPBX_RTP_CISCO_DTMF      (1 << 2)
/*! Maximum RTP-specific code */
#define OPBX_RTP_MAX             OPBX_RTP_CISCO_DTMF

struct opbx_rtp_protocol {
	/* Get RTP struct, or NULL if unwilling to transfer */
	struct opbx_rtp *(* const get_rtp_info)(struct opbx_channel *chan);
	/* Get RTP struct, or NULL if unwilling to transfer */
	struct opbx_rtp *(* const get_vrtp_info)(struct opbx_channel *chan);
	/* Set RTP peer */
	int (* const set_rtp_peer)(struct opbx_channel *chan, struct opbx_rtp *peer, struct opbx_rtp *vpeer, int codecs, int nat_active);
	int (* const get_codec)(struct opbx_channel *chan);
	const char * const type;
	struct opbx_rtp_protocol *next;
};

struct opbx_rtp;

typedef int (*opbx_rtp_callback)(struct opbx_rtp *rtp, struct opbx_frame *f, void *data);

struct opbx_rtp *opbx_rtp_new(struct sched_context *sched, struct io_context *io, int rtcpenable, int callbackmode);

struct opbx_rtp *opbx_rtp_new_with_bindaddr(struct sched_context *sched, struct io_context *io, int rtcpenable, int callbackmode, struct in_addr in);

void opbx_rtp_set_peer(struct opbx_rtp *rtp, struct sockaddr_in *them);

void opbx_rtp_get_peer(struct opbx_rtp *rtp, struct sockaddr_in *them);

void opbx_rtp_get_us(struct opbx_rtp *rtp, struct sockaddr_in *us);

void opbx_rtp_destroy(struct opbx_rtp *rtp);

void opbx_rtp_reset(struct opbx_rtp *rtp);

void opbx_rtp_set_callback(struct opbx_rtp *rtp, opbx_rtp_callback callback);

void opbx_rtp_set_data(struct opbx_rtp *rtp, void *data);

int opbx_rtp_write(struct opbx_rtp *rtp, struct opbx_frame *f);

struct opbx_frame *opbx_rtp_read(struct opbx_rtp *rtp);

struct opbx_frame *opbx_rtcp_read(struct opbx_rtp *rtp);

int opbx_rtp_fd(struct opbx_rtp *rtp);

int opbx_rtcp_fd(struct opbx_rtp *rtp);

int opbx_rtp_senddigit(struct opbx_rtp *rtp, char digit);

int opbx_rtp_sendcng(struct opbx_rtp *rtp, int level);

int opbx_rtp_settos(struct opbx_rtp *rtp, int tos);

/*  Setting RTP payload types from lines in a SDP description: */
void opbx_rtp_pt_clear(struct opbx_rtp* rtp);
/* Set payload types to defaults */
void opbx_rtp_pt_default(struct opbx_rtp* rtp);
void opbx_rtp_set_m_type(struct opbx_rtp* rtp, int pt);
void opbx_rtp_set_rtpmap_type(struct opbx_rtp* rtp, int pt,
			 char* mimeType, char* mimeSubtype);

/*  Mapping between RTP payload format codes and OpenPBX codes: */
struct rtpPayloadType opbx_rtp_lookup_pt(struct opbx_rtp* rtp, int pt);
int opbx_rtp_lookup_code(struct opbx_rtp* rtp, int isAstFormat, int code);
void opbx_rtp_offered_from_local(struct opbx_rtp* rtp, int local);

void opbx_rtp_get_current_formats(struct opbx_rtp* rtp,
			     int* astFormats, int* nonAstFormats);

/*  Mapping an OpenPBX code into a MIME subtype (string): */
char* opbx_rtp_lookup_mime_subtype(int isAstFormat, int code);

/* Build a string of MIME subtype names from a capability list */
char *opbx_rtp_lookup_mime_multiple(char *buf, int size, const int capability, const int isAstFormat);

void opbx_rtp_setnat(struct opbx_rtp *rtp, int nat);

int opbx_rtp_bridge(struct opbx_channel *c0, struct opbx_channel *c1, int flags, struct opbx_frame **fo, struct opbx_channel **rc);

int opbx_rtp_proto_register(struct opbx_rtp_protocol *proto);

void opbx_rtp_proto_unregister(struct opbx_rtp_protocol *proto);

void opbx_rtp_stop(struct opbx_rtp *rtp);

void opbx_rtp_init(void);

void opbx_rtp_reload(void);

int opbx_rtp_set_framems(struct opbx_rtp *rtp, int ms);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _OPENPBX_RTP_H */
