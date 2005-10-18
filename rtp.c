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
 *
 * Real-time Protocol Support
 * 	Supports RTP and RTCP with Symmetric RTP support for NAT
 * 	traversal
 * 
 */

#include <stdio.h>
#include <stdlib.h>
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
#ifdef ENABLE_SRTP
#include <srtp/srtp.h>
#endif	/* ENABLE_SRTP */

#include "include/openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/rtp.h"
#include "openpbx/frame.h"
#include "openpbx/logger.h"
#include "openpbx/options.h"
#include "openpbx/channel.h"
#include "openpbx/acl.h"
#include "openpbx/channel.h"
#include "openpbx/config.h"
#include "openpbx/lock.h"
#include "openpbx/utils.h"
#include "openpbx/cli.h"
#include "openpbx/unaligned.h"
#include "openpbx/utils.h"

#define MAX_TIMESTAMP_SKEW	640

#define RTP_MTU		1200

static int dtmftimeout = 3000;	/* 3000 samples */
static int rtpstart = 0;
static int rtpend = 0;
static int rtpdebug = 0;		/* Are we debugging? */
static struct sockaddr_in rtpdebugaddr;	/* Debug packets to/from this host */
#ifdef SO_NO_CHECK
static int nochecksums = 0;
#endif

/* The value of each payload format mapping: */
struct rtpPayloadType {
	int isAstFormat; 	/* whether the following code is an OPBX_FORMAT */
	int code;
};

#define MAX_RTP_PT 256

#define FLAG_3389_WARNING		(1 << 0)
#define FLAG_NAT_ACTIVE			(3 << 1)
#define FLAG_NAT_INACTIVE		(0 << 1)
#define FLAG_NAT_INACTIVE_NOWARN	(1 << 1)

#ifdef ENABLE_SRTP
struct opbx_policy {
	srtp_policy_t sp;
};
#endif

struct opbx_rtp {
	int s;
	char resp;
	struct opbx_frame f;
	unsigned char rawdata[8192 + OPBX_FRIENDLY_OFFSET];
	unsigned int ssrc;
	unsigned int lastts;
	unsigned int lastdigitts;
	unsigned int lastrxts;
	unsigned int lastividtimestamp;
	unsigned int lastovidtimestamp;
	unsigned int lasteventseqn;
	unsigned int lasteventendseqn;
	int lasttxformat;
	int lastrxformat;
	int dtmfcount;
	unsigned int dtmfduration;
	int nat;
	unsigned int flags;
	int framems;
	int rtplen;
	struct sockaddr_in us;
	struct sockaddr_in them;
	struct timeval rxcore;
	struct timeval txcore;
	struct timeval dtmfmute;
	struct opbx_smoother *smoother;
	int *ioid;
	unsigned short seqno;
	unsigned short rxseqno;
	struct sched_context *sched;
	struct io_context *io;
	void *data;
	opbx_rtp_callback callback;
	struct rtpPayloadType current_RTP_PT[MAX_RTP_PT];
	int rtp_lookup_code_cache_isAstFormat;	/* a cache for the result of rtp_lookup_code(): */
	int rtp_lookup_code_cache_code;
	int rtp_lookup_code_cache_result;
	int rtp_offered_from_local;
	struct opbx_rtcp *rtcp;
#ifdef ENABLE_SRTP
	srtp_t srtp;
	rtp_generate_key_cb key_cb;
#endif
};

struct opbx_rtcp {
	int s;		/* Socket */
	struct sockaddr_in us;
	struct sockaddr_in them;
};

static struct opbx_rtp_protocol *protos = NULL;

struct rtp_codec_table {
	int format;
	int len;
	int defaultms;
	int increment;
	unsigned int flags;
};

struct rtp_codec_table RTP_CODEC_TABLE[] = {
	{OPBX_FORMAT_SLINEAR, 160, 20, 10, OPBX_SMOOTHER_FLAG_BE},
	{OPBX_FORMAT_ULAW, 80, 20, 10},
	{OPBX_FORMAT_G726, 40, 20, 10},
	{OPBX_FORMAT_ILBC, 50, 30, 30},
	{OPBX_FORMAT_G729A, 10, 20, 10, OPBX_SMOOTHER_FLAG_G729},
	{OPBX_FORMAT_GSM, 33, 20, 20},
	{0,0,0,0,0}
};

static struct rtp_codec_table *lookup_rtp_smoother_codec(int format, int *ms, int *len)
{
	int x;
	int res;
	struct rtp_codec_table *ent = NULL;

	*len = 0;
	for(x = 0 ; RTP_CODEC_TABLE[x].format ; x++) {
		if (RTP_CODEC_TABLE[x].format == format) {
			ent = &RTP_CODEC_TABLE[x];
			if (! *ms) {
				*ms = ent->defaultms;
			}
			while((res = (*ms % ent->increment))) {
				(*ms)++;
			}
			while((*len = (*ms / ent->increment) * ent->len) > RTP_MTU) {
				*ms -= ent->increment;
			}
			break;
		}
	}

	return ent;
}

int opbx_rtp_fd(struct opbx_rtp *rtp)
{
	return rtp->s;
}

int opbx_rtcp_fd(struct opbx_rtp *rtp)
{
	if (rtp->rtcp)
		return rtp->rtcp->s;
	return -1;
}

void opbx_rtp_set_data(struct opbx_rtp *rtp, void *data)
{
	rtp->data = data;
}

void opbx_rtp_set_callback(struct opbx_rtp *rtp, opbx_rtp_callback callback)
{
	rtp->callback = callback;
}

void opbx_rtp_setnat(struct opbx_rtp *rtp, int nat)
{
	rtp->nat = nat;
}

int opbx_rtp_set_framems(struct opbx_rtp *rtp, int ms) 
{
	if (ms) {
		if (rtp->smoother) {
            opbx_smoother_free(rtp->smoother);
			rtp->smoother = NULL;
		}

		rtp->framems = ms;
	}

	return rtp->framems;
}

static struct opbx_frame *send_dtmf(struct opbx_rtp *rtp)
{
	static struct opbx_frame null_frame = { OPBX_FRAME_NULL, };
	char iabuf[INET_ADDRSTRLEN];

	if (opbx_tvcmp(opbx_tvnow(), rtp->dtmfmute) < 0) {
		if (option_debug)
			opbx_log(LOG_DEBUG, "Ignore potential DTMF echo from '%s'\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr));
		rtp->resp = 0;
		rtp->dtmfduration = 0;
		return &null_frame;
	}
	if (option_debug)
		opbx_log(LOG_DEBUG, "Sending dtmf: %d (%c), at %s\n", rtp->resp, rtp->resp, opbx_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr));
	if (rtp->resp == 'X') {
		rtp->f.frametype = OPBX_FRAME_CONTROL;
		rtp->f.subclass = OPBX_CONTROL_FLASH;
	} else {
		rtp->f.frametype = OPBX_FRAME_DTMF;
		rtp->f.subclass = rtp->resp;
	}
	rtp->f.datalen = 0;
	rtp->f.samples = 0;
	rtp->f.mallocd = 0;
	rtp->f.src = "RTP";
	rtp->resp = 0;
	rtp->dtmfduration = 0;
	return &rtp->f;
	
}

static inline int rtp_debug_test_addr(struct sockaddr_in *addr)
{
	if (rtpdebug == 0)
		return 0;
	if (rtpdebugaddr.sin_addr.s_addr) {
		if (((ntohs(rtpdebugaddr.sin_port) != 0)
			&& (rtpdebugaddr.sin_port != addr->sin_port))
			|| (rtpdebugaddr.sin_addr.s_addr != addr->sin_addr.s_addr))
		return 0;
	}
	return 1;
}

static struct opbx_frame *process_cisco_dtmf(struct opbx_rtp *rtp, unsigned char *data, int len)
{
	unsigned int event;
	char resp = 0;
	struct opbx_frame *f = NULL;
	event = ntohl(*((unsigned int *)(data)));
	event &= 0x001F;
#if 0
	printf("Cisco Digit: %08x (len = %d)\n", event, len);
#endif	
	if (event < 10) {
		resp = '0' + event;
	} else if (event < 11) {
		resp = '*';
	} else if (event < 12) {
		resp = '#';
	} else if (event < 16) {
		resp = 'A' + (event - 12);
	} else if (event < 17) {
		resp = 'X';
	}
	if (rtp->resp && (rtp->resp != resp)) {
		f = send_dtmf(rtp);
	}
	rtp->resp = resp;
	rtp->dtmfcount = dtmftimeout;
	return f;
}

/* process_rfc2833: Process RTP DTMF and events according to RFC 2833:
	"RTP Payload for DTMF Digits, Telephony Tones and Telephony Signals"
*/
static struct opbx_frame *process_rfc2833(struct opbx_rtp *rtp, unsigned char *data, int len, unsigned int seqno)
{
	unsigned int event;
	unsigned int event_end;
	unsigned int duration;
	char resp = 0;
	struct opbx_frame *f = NULL;
	event = ntohl(*((unsigned int *)(data)));
	event >>= 24;
	event_end = ntohl(*((unsigned int *)(data)));
	event_end <<= 8;
	event_end >>= 24;
	duration = ntohl(*((unsigned int *)(data)));
	duration &= 0xFFFF;
	if (rtpdebug)
		opbx_log(LOG_DEBUG, "- RTP 2833 Event: %08x (len = %d)\n", event, len);
	if (event < 10) {
		resp = '0' + event;
	} else if (event < 11) {
		resp = '*';
	} else if (event < 12) {
		resp = '#';
	} else if (event < 16) {
		resp = 'A' + (event - 12);
	} else if (event < 17) {	/* Event 16: Hook flash */
		resp = 'X';	
	}
	if (rtp->resp && (rtp->resp != resp)) {
		f = send_dtmf(rtp);
	} else if(event_end & 0x80) {
		if (rtp->resp) {
			if(rtp->lasteventendseqn != seqno) {
				f = send_dtmf(rtp);
				rtp->lasteventendseqn = seqno;
			}
			rtp->resp = 0;
		}
		resp = 0;
		duration = 0;
	} else if(rtp->dtmfduration && (duration < rtp->dtmfduration)) {
		f = send_dtmf(rtp);
	}
	if (!(event_end & 0x80))
		rtp->resp = resp;
	rtp->dtmfcount = dtmftimeout;
	rtp->dtmfduration = duration;
	return f;
}

/*--- process_rfc3389: Process Comfort Noise RTP. 
	This is incomplete at the moment.
*/
static struct opbx_frame *process_rfc3389(struct opbx_rtp *rtp, unsigned char *data, int len)
{
	struct opbx_frame *f = NULL;
	/* Convert comfort noise into audio with various codecs.  Unfortunately this doesn't
	   totally help us out becuase we don't have an engine to keep it going and we are not
	   guaranteed to have it every 20ms or anything */
	if (rtpdebug)
		opbx_log(LOG_DEBUG, "- RTP 3389 Comfort noise event: Level %d (len = %d)\n", rtp->lastrxformat, len);

	if (!(opbx_test_flag(rtp, FLAG_3389_WARNING))) {
		char iabuf[INET_ADDRSTRLEN];

		opbx_log(LOG_NOTICE, "Comfort noise support incomplete in OpenPBX (RFC 3389). Please turn off on client if possible. Client IP: %s\n",
			opbx_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr));
		opbx_set_flag(rtp, FLAG_3389_WARNING);
	}

	/* Must have at least one byte */
	if (!len)
		return NULL;
	if (len < 24) {
		rtp->f.data = rtp->rawdata + OPBX_FRIENDLY_OFFSET;
		rtp->f.datalen = len - 1;
		rtp->f.offset = OPBX_FRIENDLY_OFFSET;
		memcpy(rtp->f.data, data + 1, len - 1);
	} else {
		rtp->f.data = NULL;
		rtp->f.offset = 0;
		rtp->f.datalen = 0;
	}
	rtp->f.frametype = OPBX_FRAME_CNG;
	rtp->f.subclass = data[0] & 0x7f;
	rtp->f.datalen = len - 1;
	rtp->f.samples = 0;
	rtp->f.delivery.tv_usec = rtp->f.delivery.tv_sec = 0;
	f = &rtp->f;
	return f;
}

#ifdef ENABLE_SRTP
static const char *srtp_errstr(int err)
{
	switch(err) {
	case err_status_ok:
		return "nothing to report";
	case err_status_fail:
		return "unspecified failure";
	case err_status_bad_param:
		return "unsupported parameter";
	case err_status_alloc_fail:
		return "couldn't allocate memory";
	case err_status_dealloc_fail:
		return "couldn't deallocate properly";
	case err_status_init_fail:
		return "couldn't initialize";
	case err_status_terminus:
		return "can't process as much data as requested";
	case err_status_auth_fail:
		return "authentication failure";
	case err_status_cipher_fail:
		return "cipher failure";
	case err_status_replay_fail:
		return "replay check failed (bad index)";
	case err_status_replay_old:
		return "replay check failed (index too old)";
	case err_status_algo_fail:
		return "algorithm failed test routine";
	case err_status_no_such_op:
		return "unsupported operation";
	case err_status_no_ctx:
		return "no appropriate context found";
	case err_status_cant_check:
		return "unable to perform desired validation";
	case err_status_key_expired:
		return "can't use key any more";
	default:
		return "unknown";
	}
}

/*
  opbx_policy_t
*/
static void srtp_event_cb(srtp_event_data_t *data)
{
	switch (data->event) {
	case event_ssrc_collision: {
		if (option_debug || rtpdebug) {
			opbx_log(LOG_DEBUG, "SSRC collision ssrc:%u dir:%d\n",
				ntohl(data->stream->ssrc),
				data->stream->direction);
		}
		break;
	}
	case event_key_soft_limit: {
		if (option_debug || rtpdebug) {
			opbx_log(LOG_DEBUG, "event_key_soft_limit\n");
		}
		break;
	}
	case event_key_hard_limit: {
		if (option_debug || rtpdebug) {
			opbx_log(LOG_DEBUG, "event_key_hard_limit\n");
		}
		break;
	}
	case event_packet_index_limit: {
		if (option_debug || rtpdebug) {
			opbx_log(LOG_DEBUG, "event_packet_index_limit\n");
		}
		break;
	}
	}
}

unsigned int opbx_rtp_get_ssrc(struct opbx_rtp *rtp)
{
	return rtp->ssrc;
}

void opbx_rtp_set_generate_key_cb(struct opbx_rtp *rtp,
				  rtp_generate_key_cb cb)
{
	rtp->key_cb = cb;
}

void opbx_policy_set_ssrc(opbx_policy_t *policy, struct opbx_rtp *rtp, 
			  unsigned long ssrc, int inbound)
{
	if (!ssrc && !inbound && rtp)
		ssrc = rtp->ssrc;

	if (ssrc) {
		policy->sp.ssrc.type = ssrc_specific;
		policy->sp.ssrc.value = ssrc;
	} else {
		policy->sp.ssrc.type =
			inbound ? ssrc_any_inbound : ssrc_any_outbound;
	}
}

int opbx_rtp_add_policy(struct opbx_rtp *rtp, opbx_policy_t *policy)
{
	int res = 0;

	printf("Adding SRTP policy: %d %d %d %d %d %d\n",
	       policy->sp.rtp.cipher_type,
	       policy->sp.rtp.cipher_key_len,
	       policy->sp.rtp.auth_type,
	       policy->sp.rtp.auth_key_len,
	       policy->sp.rtp.auth_tag_len,
	       policy->sp.rtp.sec_serv);

	if (!rtp->srtp) {
		res = srtp_create(&rtp->srtp, &policy->sp);
	} else {
		res = srtp_add_stream(rtp->srtp, &policy->sp);
	}

	if (res != err_status_ok) {
		opbx_log(LOG_WARNING, "SRTP policy: %s\n", srtp_errstr(res));
		return -1;
	}

	return 0;
}

opbx_policy_t *opbx_policy_alloc()
{
	opbx_policy_t *tmp = malloc(sizeof(opbx_policy_t));

	memset(tmp, 0, sizeof(opbx_policy_t));
	return tmp;
}

void opbx_policy_destroy(opbx_policy_t *policy)
{
	free(policy);
}

static int policy_set_suite(crypto_policy_t *p, int suite)
{
	switch (suite) {
	case OPBX_AES_CM_128_HMAC_SHA1_80:
		p->cipher_type = AES_128_ICM;
		p->cipher_key_len = 30;
		p->auth_type = HMAC_SHA1;
		p->auth_key_len = 20;
		p->auth_tag_len = 10;
		p->sec_serv = sec_serv_conf_and_auth;
		return 0;

	case OPBX_AES_CM_128_HMAC_SHA1_32:
		p->cipher_type = AES_128_ICM;
		p->cipher_key_len = 30;
		p->auth_type = HMAC_SHA1;
		p->auth_key_len = 20;
		p->auth_tag_len = 4;
		p->sec_serv = sec_serv_conf_and_auth;
		return 0;

	default:
		opbx_log(LOG_ERROR, "Invalid crypto suite: %d\n", suite);
		return -1;
	}
}

int opbx_policy_set_suite(opbx_policy_t *policy, int suite)
{
	int res = policy_set_suite(&policy->sp.rtp, suite) |
		policy_set_suite(&policy->sp.rtcp, suite);

	return res;
}

int opbx_policy_set_key(opbx_policy_t *policy, unsigned char *key,
		       size_t key_len)
{
	if (key_len != 30) {
		opbx_log(LOG_ERROR, "Invalid key length %d (!= 30)\n", key_len);
		return -1;
	}

	policy->sp.key = key;
	return 0;
}

int opbx_policy_set_encr_alg(opbx_policy_t *policy, int ealg)
{
	int type = -1;

	switch (ealg) {
	case MIKEY_SRTP_EALG_NULL:
		type = NULL_CIPHER;
		break;
	case MIKEY_SRTP_EALG_AESCM:
		type = AES_128_ICM;
		break;
	default:
		return -1;
	}

	policy->sp.rtp.cipher_type = type;
	policy->sp.rtcp.cipher_type = type;
	return 0;
}

int opbx_policy_set_auth_alg(opbx_policy_t *policy, int aalg)
{
	int type = -1;

	switch (aalg) {
	case MIKEY_SRTP_AALG_NULL:
		type = NULL_AUTH;
		break;
	case MIKEY_SRTP_AALG_SHA1HMAC:
		type = HMAC_SHA1;
		break;
	default:
		return -1;
	}

	policy->sp.rtp.auth_type = type;
	policy->sp.rtcp.auth_type = type;
	return 0;
}

void opbx_policy_set_encr_keylen(opbx_policy_t *policy, int ekeyl)
{
	policy->sp.rtp.cipher_key_len = ekeyl;
	policy->sp.rtcp.cipher_key_len = ekeyl;
}

void opbx_policy_set_auth_keylen(opbx_policy_t *policy, int akeyl)
{
	policy->sp.rtp.auth_key_len = akeyl;
	policy->sp.rtcp.auth_key_len = akeyl;
}

void opbx_policy_set_srtp_auth_taglen(opbx_policy_t *policy, int autht)
{
	policy->sp.rtp.auth_tag_len = autht;
	policy->sp.rtcp.auth_tag_len = autht;
	
}

void opbx_policy_set_srtp_encr_enable(opbx_policy_t *policy, int enable)
{
	int serv = enable ? sec_serv_conf : sec_serv_none;
	policy->sp.rtp.sec_serv = 
		(policy->sp.rtp.sec_serv & ~sec_serv_conf) | serv;
}

void opbx_policy_set_srtcp_encr_enable(opbx_policy_t *policy, int enable)
{
	int serv = enable ? sec_serv_conf : sec_serv_none;
	policy->sp.rtcp.sec_serv = 
		(policy->sp.rtcp.sec_serv & ~sec_serv_conf) | serv;
}

void opbx_policy_set_srtp_auth_enable(opbx_policy_t *policy, int enable)
{
	int serv = enable ? sec_serv_auth : sec_serv_none;
	policy->sp.rtp.sec_serv = 
		(policy->sp.rtp.sec_serv & ~sec_serv_auth) | serv;
}


int opbx_get_random(unsigned char *key, size_t len)
{
	int res = crypto_get_random(key, len);

	return res != err_status_ok ? -1: 0;
}
#endif	/* ENABLE_SRTP */

static int rtp_recvfrom(struct opbx_rtp *rtp, void *buf, size_t size,
			int flags, struct sockaddr *sa, socklen_t *salen)
{
	int len;

	len = recvfrom(rtp->s, buf, size, flags, sa, salen);

	if (len < 0)
		return len;

#ifdef ENABLE_SRTP
	if (rtp->srtp) {
		int res = 0;
		int i;

		for (i = 0; i < 5; i++) {
			srtp_hdr_t *header = buf;

			res = srtp_unprotect(rtp->srtp, buf, &len);
			if (res != err_status_no_ctx)
				break;

			if (rtp->key_cb) {
				if (rtp->key_cb(rtp, ntohl(header->ssrc),
						rtp->data) < 0) {
					break;
				}
				
			} else {
				break;
			}
		}

		if (res != err_status_ok) {
			if (option_debug || rtpdebug) {
				opbx_log(LOG_DEBUG, "SRTP unprotect: %s\n",
					srtp_errstr(res));
			}
			return -1;
		}
	}
#endif	/* ENABLE_SRTP */

	return len;
}

static int rtp_sendto(struct opbx_rtp *rtp, void *buf, size_t size,
		      int flags, struct sockaddr *sa, socklen_t salen)
{
	int len = size;

#ifdef ENABLE_SRTP
	if (rtp->srtp) {
		int res = srtp_protect(rtp->srtp, buf, &len);

		if (res != err_status_ok) {
			if (option_debug || rtpdebug) {
				opbx_log(LOG_DEBUG, "SRTP protect: %s\n",
					srtp_errstr(res));
			}
			return -1;
		}
	}
#endif	/* ENABLE_SRTP */

	return sendto(rtp->s, buf, len, flags, sa, salen);
}

static int rtpread(int *id, int fd, short events, void *cbdata)
{
	struct opbx_rtp *rtp = cbdata;
	struct opbx_frame *f;
	f = opbx_rtp_read(rtp);
	if (f) {
		if (rtp->callback)
			rtp->callback(rtp, f, rtp->data);
	}
	return 1;
}

struct opbx_frame *opbx_rtcp_read(struct opbx_rtp *rtp)
{
	static struct opbx_frame null_frame = { OPBX_FRAME_NULL, };
	socklen_t len;
	int hdrlen = 8;
	int res;
	struct sockaddr_in sin;
	unsigned int rtcpdata[1024];
	char iabuf[INET_ADDRSTRLEN];
	
	if (!rtp || !rtp->rtcp)
		return &null_frame;

	len = sizeof(sin);
	
	res = recvfrom(rtp->rtcp->s, rtcpdata, sizeof(rtcpdata),
					0, (struct sockaddr *)&sin, &len);
	
	if (res < 0) {
		if (errno != EAGAIN)
			opbx_log(LOG_WARNING, "RTP Read error: %s\n", strerror(errno));
		if (errno == EBADF)
			CRASH;
		return &null_frame;
	}

	if (res < hdrlen) {
		opbx_log(LOG_WARNING, "RTP Read too short\n");
		return &null_frame;
	}

	if (rtp->nat) {
		/* Send to whoever sent to us */
		if ((rtp->rtcp->them.sin_addr.s_addr != sin.sin_addr.s_addr) ||
		    (rtp->rtcp->them.sin_port != sin.sin_port)) {
			memcpy(&rtp->rtcp->them, &sin, sizeof(rtp->rtcp->them));
			if (option_debug || rtpdebug)
				opbx_log(LOG_DEBUG, "RTCP NAT: Got RTCP from other end. Now sending to address %s:%d\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), rtp->rtcp->them.sin_addr), ntohs(rtp->rtcp->them.sin_port));
		}
	}
	if (option_debug)
		opbx_log(LOG_DEBUG, "Got RTCP report of %d bytes\n", res);
	return &null_frame;
}

static void calc_rxstamp(struct timeval *tv, struct opbx_rtp *rtp, unsigned int timestamp, int mark)
{
	struct timeval ts = opbx_samp2tv( timestamp, 8000);
	if (opbx_tvzero(rtp->rxcore) || mark) {
		rtp->rxcore = opbx_tvsub(opbx_tvnow(), ts);
		/* Round to 20ms for nice, pretty timestamps */
		rtp->rxcore.tv_usec -= rtp->rxcore.tv_usec % 20000;
	}
	*tv = opbx_tvadd(rtp->rxcore, ts);
}

struct opbx_frame *opbx_rtp_read(struct opbx_rtp *rtp)
{
	int res;
	struct sockaddr_in sin;
	socklen_t len;
	unsigned int seqno;
	int version;
	int payloadtype;
	int hdrlen = 12;
	int padding;
	int mark;
	int ext;
	int x;
	char iabuf[INET_ADDRSTRLEN];
	unsigned int timestamp;
	unsigned int *rtpheader;
	static struct opbx_frame *f, null_frame = { OPBX_FRAME_NULL, };
	struct rtpPayloadType rtpPT;
	
	len = sizeof(sin);
	
	/* Cache where the header will go */
	res = rtp_recvfrom(rtp, rtp->rawdata + OPBX_FRIENDLY_OFFSET, sizeof(rtp->rawdata) - OPBX_FRIENDLY_OFFSET,
					0, (struct sockaddr *)&sin, &len);


	rtpheader = (unsigned int *)(rtp->rawdata + OPBX_FRIENDLY_OFFSET);
	if (res < 0) {
		if (errno != EAGAIN)
			opbx_log(LOG_WARNING, "RTP Read error: %s\n", strerror(errno));
		if (errno == EBADF)
			CRASH;
		return &null_frame;
	}
	if (res < hdrlen) {
		opbx_log(LOG_WARNING, "RTP Read too short\n");
		return &null_frame;
	}

	/* Ignore if the other side hasn't been given an address
	   yet.  */
	if (!rtp->them.sin_addr.s_addr || !rtp->them.sin_port)
		return &null_frame;

	if (rtp->nat) {
		/* Send to whoever sent to us */
		if ((rtp->them.sin_addr.s_addr != sin.sin_addr.s_addr) ||
		    (rtp->them.sin_port != sin.sin_port)) {
			memcpy(&rtp->them, &sin, sizeof(rtp->them));
			rtp->rxseqno = 0;
			opbx_set_flag(rtp, FLAG_NAT_ACTIVE);
			if (option_debug || rtpdebug)
				opbx_log(LOG_DEBUG, "RTP NAT: Got audio from other end. Now sending to address %s:%d\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr), ntohs(rtp->them.sin_port));
		}
	}

	/* Get fields */
	seqno = ntohl(rtpheader[0]);

	/* Check RTP version */
	version = (seqno & 0xC0000000) >> 30;
	if (version != 2)
		return &null_frame;
	
	payloadtype = (seqno & 0x7f0000) >> 16;
	padding = seqno & (1 << 29);
	mark = seqno & (1 << 23);
	ext = seqno & (1 << 28);
	seqno &= 0xffff;
	timestamp = ntohl(rtpheader[1]);
	
	if (padding) {
		/* Remove padding bytes */
		res -= rtp->rawdata[OPBX_FRIENDLY_OFFSET + res - 1];
	}
	
	if (ext) {
		/* RTP Extension present */
		hdrlen += 4;
		hdrlen += (ntohl(rtpheader[3]) & 0xffff) << 2;
	}

	if (res < hdrlen) {
		opbx_log(LOG_WARNING, "RTP Read too short (%d, expecting %d)\n", res, hdrlen);
		return &null_frame;
	}

	if(rtp_debug_test_addr(&sin))
		opbx_verbose("Got RTP packet from %s:%d (type %d, seq %d, ts %d, len %d)\n"
			, opbx_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr), ntohs(sin.sin_port), payloadtype, seqno, timestamp,res - hdrlen);

        rtpPT = opbx_rtp_lookup_pt(rtp, payloadtype);
	if (!rtpPT.isAstFormat) {
		/* This is special in-band data that's not one of our codecs */
		if (rtpPT.code == OPBX_RTP_DTMF) {
			/* It's special -- rfc2833 process it */
			if(rtp_debug_test_addr(&sin)) {
				unsigned char *data;
				unsigned int event;
				unsigned int event_end;
				unsigned int duration;
				data = rtp->rawdata + OPBX_FRIENDLY_OFFSET + hdrlen;
				event = ntohl(*((unsigned int *)(data)));
				event >>= 24;
				event_end = ntohl(*((unsigned int *)(data)));
				event_end <<= 8;
				event_end >>= 24;
				duration = ntohl(*((unsigned int *)(data)));
				duration &= 0xFFFF;
				opbx_verbose("Got rfc2833 RTP packet from %s:%d (type %d, seq %d, ts %d, len %d, mark %d, event %08x, end %d, duration %d) \n", opbx_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr), ntohs(sin.sin_port), payloadtype, seqno, timestamp, res - hdrlen, (mark?1:0), event, ((event_end & 0x80)?1:0), duration);
			}
	    		if (rtp->lasteventseqn <= seqno || rtp->resp == 0 || (rtp->lasteventseqn >= 65530 && seqno <= 6)) {
	      			f = process_rfc2833(rtp, rtp->rawdata + OPBX_FRIENDLY_OFFSET + hdrlen, res - hdrlen, seqno);
	      			rtp->lasteventseqn = seqno;
	    		} else 
				f = NULL;
	    		if (f) 
				return f; 
			else 
				return &null_frame;
	  	} else if (rtpPT.code == OPBX_RTP_CISCO_DTMF) {
	    		/* It's really special -- process it the Cisco way */
	    		if (rtp->lasteventseqn <= seqno || rtp->resp == 0 || (rtp->lasteventseqn >= 65530 && seqno <= 6)) {
	      			f = process_cisco_dtmf(rtp, rtp->rawdata + OPBX_FRIENDLY_OFFSET + hdrlen, res - hdrlen);
	      			rtp->lasteventseqn = seqno;
	    		} else 
				f = NULL;
	    		if (f) 
				return f; 
			else 
				return &null_frame;
	  	} else if (rtpPT.code == OPBX_RTP_CN) {
	    		/* Comfort Noise */
	    		f = process_rfc3389(rtp, rtp->rawdata + OPBX_FRIENDLY_OFFSET + hdrlen, res - hdrlen);
	    		if (f) 
				return f; 
			else 
				return &null_frame;
	  	} else {
	    		opbx_log(LOG_NOTICE, "Unknown RTP codec %d received\n", payloadtype);
	    		return &null_frame;
	  	}
	}
	rtp->f.subclass = rtpPT.code;
	if (rtp->f.subclass < OPBX_FORMAT_MAX_AUDIO)
		rtp->f.frametype = OPBX_FRAME_VOICE;
	else
		rtp->f.frametype = OPBX_FRAME_VIDEO;
	rtp->lastrxformat = rtp->f.subclass;

	if (!rtp->lastrxts)
		rtp->lastrxts = timestamp;

	if (rtp->rxseqno) {
		for (x=rtp->rxseqno + 1; x < seqno; x++) {
			/* Queue empty frames */
			rtp->f.mallocd = 0;
			rtp->f.datalen = 0;
			rtp->f.data = NULL;
			rtp->f.offset = 0;
			rtp->f.samples = 0;
			rtp->f.src = "RTPMissedFrame";
		}
	}
	rtp->rxseqno = seqno;

	if (rtp->dtmfcount) {
#if 0
		printf("dtmfcount was %d\n", rtp->dtmfcount);
#endif		
		rtp->dtmfcount -= (timestamp - rtp->lastrxts);
		if (rtp->dtmfcount < 0)
			rtp->dtmfcount = 0;
#if 0
		if (dtmftimeout != rtp->dtmfcount)
			printf("dtmfcount is %d\n", rtp->dtmfcount);
#endif
	}
	rtp->lastrxts = timestamp;

	/* Send any pending DTMF */
	if (rtp->resp && !rtp->dtmfcount) {
		if (option_debug)
			opbx_log(LOG_DEBUG, "Sending pending DTMF\n");
		return send_dtmf(rtp);
	}
	rtp->f.mallocd = 0;
	rtp->f.datalen = res - hdrlen;
	rtp->f.data = rtp->rawdata + hdrlen + OPBX_FRIENDLY_OFFSET;
	rtp->f.offset = hdrlen + OPBX_FRIENDLY_OFFSET;
	if (rtp->f.subclass < OPBX_FORMAT_MAX_AUDIO) {
		rtp->f.samples = opbx_codec_get_samples(&rtp->f);
		if (rtp->f.subclass == OPBX_FORMAT_SLINEAR) 
			opbx_frame_byteswap_be(&rtp->f);
		calc_rxstamp(&rtp->f.delivery, rtp, timestamp, mark);
	} else {
		/* Video -- samples is # of samples vs. 90000 */
		if (!rtp->lastividtimestamp)
			rtp->lastividtimestamp = timestamp;
		rtp->f.samples = timestamp - rtp->lastividtimestamp;
		rtp->lastividtimestamp = timestamp;
		rtp->f.delivery.tv_sec = 0;
		rtp->f.delivery.tv_usec = 0;
		if (mark)
			rtp->f.subclass |= 0x1;
		
	}
	rtp->f.src = "RTP";
	return &rtp->f;
}

/* The following array defines the MIME Media type (and subtype) for each
   of our codecs, or RTP-specific data type. */
static struct {
  struct rtpPayloadType payloadType;
  char* type;
  char* subtype;
} mimeTypes[] = {
  {{1, OPBX_FORMAT_G723_1}, "audio", "G723"},
  {{1, OPBX_FORMAT_GSM}, "audio", "GSM"},
  {{1, OPBX_FORMAT_ULAW}, "audio", "PCMU"},
  {{1, OPBX_FORMAT_ALAW}, "audio", "PCMA"},
  {{1, OPBX_FORMAT_G726}, "audio", "G726-32"},
  {{1, OPBX_FORMAT_ADPCM}, "audio", "DVI4"},
  {{1, OPBX_FORMAT_SLINEAR}, "audio", "L16"},
  {{1, OPBX_FORMAT_LPC10}, "audio", "LPC"},
  {{1, OPBX_FORMAT_G729A}, "audio", "G729"},
  {{1, OPBX_FORMAT_SPEEX}, "audio", "speex"},
  {{1, OPBX_FORMAT_ILBC}, "audio", "iLBC"},
  {{0, OPBX_RTP_DTMF}, "audio", "telephone-event"},
  {{0, OPBX_RTP_CISCO_DTMF}, "audio", "cisco-telephone-event"},
  {{0, OPBX_RTP_CN}, "audio", "CN"},
  {{1, OPBX_FORMAT_JPEG}, "video", "JPEG"},
  {{1, OPBX_FORMAT_PNG}, "video", "PNG"},
  {{1, OPBX_FORMAT_H261}, "video", "H261"},
  {{1, OPBX_FORMAT_H263}, "video", "H263"},
  {{1, OPBX_FORMAT_H263_PLUS}, "video", "h263-1998"},
};

/* Static (i.e., well-known) RTP payload types for our "OPBX_FORMAT..."s:
   also, our own choices for dynamic payload types.  This is our master
   table for transmission */
static struct rtpPayloadType static_RTP_PT[MAX_RTP_PT] = {
  [0] = {1, OPBX_FORMAT_ULAW},
#ifdef USE_DEPRECATED_G726
  [2] = {1, OPBX_FORMAT_G726}, /* Technically this is G.721, but if Cisco can do it, so can we... */
#endif
  [3] = {1, OPBX_FORMAT_GSM},
  [4] = {1, OPBX_FORMAT_G723_1},
  [5] = {1, OPBX_FORMAT_ADPCM}, /* 8 kHz */
  [6] = {1, OPBX_FORMAT_ADPCM}, /* 16 kHz */
  [7] = {1, OPBX_FORMAT_LPC10},
  [8] = {1, OPBX_FORMAT_ALAW},
  [10] = {1, OPBX_FORMAT_SLINEAR}, /* 2 channels */
  [11] = {1, OPBX_FORMAT_SLINEAR}, /* 1 channel */
  [13] = {0, OPBX_RTP_CN},
  [16] = {1, OPBX_FORMAT_ADPCM}, /* 11.025 kHz */
  [17] = {1, OPBX_FORMAT_ADPCM}, /* 22.050 kHz */
  [18] = {1, OPBX_FORMAT_G729A},
  [19] = {0, OPBX_RTP_CN},		/* Also used for CN */
  [26] = {1, OPBX_FORMAT_JPEG},
  [31] = {1, OPBX_FORMAT_H261},
  [34] = {1, OPBX_FORMAT_H263},
  [103] = {1, OPBX_FORMAT_H263_PLUS},
  [97] = {1, OPBX_FORMAT_ILBC},
  [101] = {0, OPBX_RTP_DTMF},
  [110] = {1, OPBX_FORMAT_SPEEX},
  [111] = {1, OPBX_FORMAT_G726},
  [121] = {0, OPBX_RTP_CISCO_DTMF}, /* Must be type 121 */
};

void opbx_rtp_pt_clear(struct opbx_rtp* rtp) 
{
	int i;
	if (!rtp)
		return;

	for (i = 0; i < MAX_RTP_PT; ++i) {
		rtp->current_RTP_PT[i].isAstFormat = 0;
		rtp->current_RTP_PT[i].code = 0;
	}

	rtp->rtp_lookup_code_cache_isAstFormat = 0;
	rtp->rtp_lookup_code_cache_code = 0;
	rtp->rtp_lookup_code_cache_result = 0;
}

void opbx_rtp_pt_default(struct opbx_rtp* rtp) 
{
	int i;

	/* Initialize to default payload types */
	for (i = 0; i < MAX_RTP_PT; ++i) {
		rtp->current_RTP_PT[i].isAstFormat = static_RTP_PT[i].isAstFormat;
		rtp->current_RTP_PT[i].code = static_RTP_PT[i].code;
	}

	rtp->rtp_lookup_code_cache_isAstFormat = 0;
	rtp->rtp_lookup_code_cache_code = 0;
	rtp->rtp_lookup_code_cache_result = 0;
}

/* Make a note of a RTP paymoad type that was seen in a SDP "m=" line. */
/* By default, use the well-known value for this type (although it may */
/* still be set to a different value by a subsequent "a=rtpmap:" line): */
void opbx_rtp_set_m_type(struct opbx_rtp* rtp, int pt) {
	if (pt < 0 || pt > MAX_RTP_PT) 
		return; /* bogus payload type */

	if (static_RTP_PT[pt].code != 0) {
		rtp->current_RTP_PT[pt] = static_RTP_PT[pt];
	}
} 

/* Make a note of a RTP payload type (with MIME type) that was seen in */
/* a SDP "a=rtpmap:" line. */
void opbx_rtp_set_rtpmap_type(struct opbx_rtp* rtp, int pt,
			 char* mimeType, char* mimeSubtype) {
	int i;

	if (pt < 0 || pt > MAX_RTP_PT) 
			return; /* bogus payload type */

	for (i = 0; i < sizeof mimeTypes/sizeof mimeTypes[0]; ++i) {
		if (strcasecmp(mimeSubtype, mimeTypes[i].subtype) == 0 &&
		     strcasecmp(mimeType, mimeTypes[i].type) == 0) {
			rtp->current_RTP_PT[pt] = mimeTypes[i].payloadType;
		return;
		}
	}
} 

/* Return the union of all of the codecs that were set by rtp_set...() calls */
/* They're returned as two distinct sets: OPBX_FORMATs, and OPBX_RTPs */
void opbx_rtp_get_current_formats(struct opbx_rtp* rtp,
			     int* astFormats, int* nonAstFormats) {
	int pt;

	*astFormats = *nonAstFormats = 0;
	for (pt = 0; pt < MAX_RTP_PT; ++pt) {
		if (rtp->current_RTP_PT[pt].isAstFormat) {
			*astFormats |= rtp->current_RTP_PT[pt].code;
		} else {
			*nonAstFormats |= rtp->current_RTP_PT[pt].code;
		}
	}
}

void opbx_rtp_offered_from_local(struct opbx_rtp* rtp, int local) {
	if (rtp)
		rtp->rtp_offered_from_local = local;
	else
		opbx_log(LOG_WARNING, "rtp structure is null\n");
}

struct rtpPayloadType opbx_rtp_lookup_pt(struct opbx_rtp* rtp, int pt) 
{
	struct rtpPayloadType result;

	result.isAstFormat = result.code = 0;
	if (pt < 0 || pt > MAX_RTP_PT) 
		return result; /* bogus payload type */

	/* Start with the negotiated codecs */
	if (!rtp->rtp_offered_from_local)
		result = rtp->current_RTP_PT[pt];

	/* If it doesn't exist, check our static RTP type list, just in case */
	if (!result.code) 
		result = static_RTP_PT[pt];
	return result;
}

/* Looks up an RTP code out of our *static* outbound list */
int opbx_rtp_lookup_code(struct opbx_rtp* rtp, const int isAstFormat, const int code) {

	int pt;

	if (isAstFormat == rtp->rtp_lookup_code_cache_isAstFormat &&
		code == rtp->rtp_lookup_code_cache_code) {

		/* Use our cached mapping, to avoid the overhead of the loop below */
		return rtp->rtp_lookup_code_cache_result;
	}

	/* Check the dynamic list first */
	for (pt = 0; pt < MAX_RTP_PT; ++pt) {
  		if (rtp->current_RTP_PT[pt].code == code && rtp->current_RTP_PT[pt].isAstFormat == isAstFormat) {
			rtp->rtp_lookup_code_cache_isAstFormat = isAstFormat;
			rtp->rtp_lookup_code_cache_code = code;
			rtp->rtp_lookup_code_cache_result = pt;
			return pt;
		}
	}

	/* Then the static list */
	for (pt = 0; pt < MAX_RTP_PT; ++pt) {
		if (static_RTP_PT[pt].code == code && static_RTP_PT[pt].isAstFormat == isAstFormat) {
			rtp->rtp_lookup_code_cache_isAstFormat = isAstFormat;
  			rtp->rtp_lookup_code_cache_code = code;
			rtp->rtp_lookup_code_cache_result = pt;
			return pt;
		}
	}
	return -1;
}

char* opbx_rtp_lookup_mime_subtype(const int isAstFormat, const int code) {

	int i;

	for (i = 0; i < sizeof mimeTypes/sizeof mimeTypes[0]; ++i) {
	if (mimeTypes[i].payloadType.code == code && mimeTypes[i].payloadType.isAstFormat == isAstFormat) {
      		return mimeTypes[i].subtype;
		}
	}
	return "";
}

char *opbx_rtp_lookup_mime_multiple(char *buf, int size, const int capability, const int isAstFormat)
{
	int format;
	unsigned len;
	char *end = buf;
	char *start = buf;

	if (!buf || !size)
		return NULL;

	snprintf(end, size, "0x%x (", capability);

	len = strlen(end);
	end += len;
	size -= len;
	start = end;

	for (format = 1; format < OPBX_RTP_MAX; format <<= 1) {
		if (capability & format) {
			const char *name = opbx_rtp_lookup_mime_subtype(isAstFormat, format);
			snprintf(end, size, "%s|", name);
			len = strlen(end);
			end += len;
			size -= len;
		}
	}

	if (start == end)
		snprintf(start, size, "nothing)"); 
	else if (size > 1)
		*(end -1) = ')';
	
	return buf;
}

static int rtp_socket(void)
{
	int s;
	long flags;
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s > -1) {
		flags = fcntl(s, F_GETFL);
		fcntl(s, F_SETFL, flags | O_NONBLOCK);
#ifdef SO_NO_CHECK
		if (nochecksums)
			setsockopt(s, SOL_SOCKET, SO_NO_CHECK, &nochecksums, sizeof(nochecksums));
#endif
	}
	return s;
}

static struct opbx_rtcp *opbx_rtcp_new(void)
{
	struct opbx_rtcp *rtcp;
	rtcp = malloc(sizeof(struct opbx_rtcp));
	if (!rtcp)
		return NULL;
	memset(rtcp, 0, sizeof(struct opbx_rtcp));
	rtcp->s = rtp_socket();
	rtcp->us.sin_family = AF_INET;
	if (rtcp->s < 0) {
		free(rtcp);
		opbx_log(LOG_WARNING, "Unable to allocate socket: %s\n", strerror(errno));
		return NULL;
	}
	return rtcp;
}

struct opbx_rtp *opbx_rtp_new_with_bindaddr(struct sched_context *sched, struct io_context *io, int rtcpenable, int callbackmode, struct in_addr addr)
{
	struct opbx_rtp *rtp;
	int x;
	int first;
	int startplace;
	rtp = malloc(sizeof(struct opbx_rtp));
	if (!rtp)
		return NULL;
	memset(rtp, 0, sizeof(struct opbx_rtp));
	rtp->them.sin_family = AF_INET;
	rtp->us.sin_family = AF_INET;
	rtp->s = rtp_socket();
	rtp->ssrc = rand();
	rtp->seqno = rand() & 0xffff;
	if (rtp->s < 0) {
		free(rtp);
		opbx_log(LOG_ERROR, "Unable to allocate socket: %s\n", strerror(errno));
		return NULL;
	}
	if (sched && rtcpenable) {
		rtp->sched = sched;
		rtp->rtcp = opbx_rtcp_new();
	}
	/* Find us a place */
	x = (rand() % (rtpend-rtpstart)) + rtpstart;
	x = x & ~1;
	startplace = x;
	for (;;) {
		/* Must be an even port number by RTP spec */
		rtp->us.sin_port = htons(x);
		rtp->us.sin_addr = addr;
		if (rtp->rtcp)
			rtp->rtcp->us.sin_port = htons(x + 1);
		if (!(first = bind(rtp->s, (struct sockaddr *)&rtp->us, sizeof(rtp->us))) &&
			(!rtp->rtcp || !bind(rtp->rtcp->s, (struct sockaddr *)&rtp->rtcp->us, sizeof(rtp->rtcp->us))))
			break;
		if (!first) {
			/* Primary bind succeeded! Gotta recreate it */
			close(rtp->s);
			rtp->s = rtp_socket();
		}
		if (errno != EADDRINUSE) {
			opbx_log(LOG_ERROR, "Unexpected bind error: %s\n", strerror(errno));
			close(rtp->s);
			if (rtp->rtcp) {
				close(rtp->rtcp->s);
				free(rtp->rtcp);
			}
			free(rtp);
			return NULL;
		}
		x += 2;
		if (x > rtpend)
			x = (rtpstart + 1) & ~1;
		if (x == startplace) {
			opbx_log(LOG_ERROR, "No RTP ports remaining. Can't setup media stream for this call.\n");
			close(rtp->s);
			if (rtp->rtcp) {
				close(rtp->rtcp->s);
				free(rtp->rtcp);
			}
			free(rtp);
			return NULL;
		}
	}
	if (io && sched && callbackmode) {
		/* Operate this one in a callback mode */
		rtp->sched = sched;
		rtp->io = io;
		rtp->ioid = opbx_io_add(rtp->io, rtp->s, rtpread, OPBX_IO_IN, rtp);
	}
	opbx_rtp_pt_default(rtp);
	return rtp;
}

struct opbx_rtp *opbx_rtp_new(struct sched_context *sched, struct io_context *io, int rtcpenable, int callbackmode)
{
	struct in_addr ia;

	memset(&ia, 0, sizeof(ia));
	return opbx_rtp_new_with_bindaddr(sched, io, rtcpenable, callbackmode, ia);
}

int opbx_rtp_settos(struct opbx_rtp *rtp, int tos)
{
	int res;

	if ((res = setsockopt(rtp->s, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)))) 
		opbx_log(LOG_WARNING, "Unable to set TOS to %d\n", tos);
	return res;
}

void opbx_rtp_set_peer(struct opbx_rtp *rtp, struct sockaddr_in *them)
{
	rtp->them.sin_port = them->sin_port;
	rtp->them.sin_addr = them->sin_addr;
	if (rtp->rtcp) {
		rtp->rtcp->them.sin_port = htons(ntohs(them->sin_port) + 1);
		rtp->rtcp->them.sin_addr = them->sin_addr;
	}
	rtp->rxseqno = 0;
}

void opbx_rtp_get_peer(struct opbx_rtp *rtp, struct sockaddr_in *them)
{
	them->sin_family = AF_INET;
	them->sin_port = rtp->them.sin_port;
	them->sin_addr = rtp->them.sin_addr;
}

void opbx_rtp_get_us(struct opbx_rtp *rtp, struct sockaddr_in *us)
{
	memcpy(us, &rtp->us, sizeof(rtp->us));
}

void opbx_rtp_stop(struct opbx_rtp *rtp)
{
	memset(&rtp->them.sin_addr, 0, sizeof(rtp->them.sin_addr));
	memset(&rtp->them.sin_port, 0, sizeof(rtp->them.sin_port));
	if (rtp->rtcp) {
		memset(&rtp->rtcp->them.sin_addr, 0, sizeof(rtp->them.sin_addr));
		memset(&rtp->rtcp->them.sin_port, 0, sizeof(rtp->them.sin_port));
	}
}

void opbx_rtp_reset(struct opbx_rtp *rtp)
{
	memset(&rtp->rxcore, 0, sizeof(rtp->rxcore));
	memset(&rtp->txcore, 0, sizeof(rtp->txcore));
	memset(&rtp->dtmfmute, 0, sizeof(rtp->dtmfmute));
	rtp->lastts = 0;
	rtp->lastdigitts = 0;
	rtp->lastrxts = 0;
	rtp->lastividtimestamp = 0;
	rtp->lastovidtimestamp = 0;
	rtp->lasteventseqn = 0;
	rtp->lasteventendseqn = 0;
	rtp->lasttxformat = 0;
	rtp->lastrxformat = 0;
	rtp->dtmfcount = 0;
	rtp->dtmfduration = 0;
	rtp->seqno = 0;
	rtp->rxseqno = 0;
}

void opbx_rtp_destroy(struct opbx_rtp *rtp)
{
	if (rtp->smoother)
		opbx_smoother_free(rtp->smoother);
	if (rtp->ioid)
		opbx_io_remove(rtp->io, rtp->ioid);
	if (rtp->s > -1)
		close(rtp->s);
	if (rtp->rtcp) {
		close(rtp->rtcp->s);
		free(rtp->rtcp);
	}
#ifdef ENABLE_SRTP
	if (rtp->srtp) {
		srtp_dealloc(rtp->srtp);
		rtp->srtp = NULL;
	}
#endif
	free(rtp);
}

static unsigned int calc_txstamp(struct opbx_rtp *rtp, struct timeval *delivery)
{
	struct timeval t;
	long ms;
	if (opbx_tvzero(rtp->txcore)) {
		rtp->txcore = opbx_tvnow();
		/* Round to 20ms for nice, pretty timestamps */
		rtp->txcore.tv_usec -= rtp->txcore.tv_usec % 20000;
	}
	/* Use previous txcore if available */
	t = (delivery && !opbx_tvzero(*delivery)) ? *delivery : opbx_tvnow();
	ms = opbx_tvdiff_ms(t, rtp->txcore);
	/* Use what we just got for next time */
	rtp->txcore = t;
	return (unsigned int) ms;
}

int opbx_rtp_senddigit(struct opbx_rtp *rtp, char digit)
{
	unsigned int *rtpheader;
	int hdrlen = 12;
	int res;
	int x;
	int payload;
	char data[256];
	char iabuf[INET_ADDRSTRLEN];

	if ((digit <= '9') && (digit >= '0'))
		digit -= '0';
	else if (digit == '*')
		digit = 10;
	else if (digit == '#')
		digit = 11;
	else if ((digit >= 'A') && (digit <= 'D')) 
		digit = digit - 'A' + 12;
	else if ((digit >= 'a') && (digit <= 'd')) 
		digit = digit - 'a' + 12;
	else {
		opbx_log(LOG_WARNING, "Don't know how to represent '%c'\n", digit);
		return -1;
	}
	payload = opbx_rtp_lookup_code(rtp, 0, OPBX_RTP_DTMF);

	/* If we have no peer, return immediately */	
	if (!rtp->them.sin_addr.s_addr)
		return 0;

	rtp->dtmfmute = opbx_tvadd(opbx_tvnow(), opbx_tv(0, 500000));
	
	/* Get a pointer to the header */
	rtpheader = (unsigned int *)data;
	rtpheader[0] = htonl((2 << 30) | (1 << 23) | (payload << 16) | (rtp->seqno));
	rtpheader[1] = htonl(rtp->lastdigitts);
	rtpheader[2] = htonl(rtp->ssrc); 
	rtpheader[3] = htonl((digit << 24) | (0xa << 16) | (0));
	for (x = 0; x < 6; x++) {
		if (rtp->them.sin_port && rtp->them.sin_addr.s_addr) {
			res = rtp_sendto(rtp, (void *) rtpheader, hdrlen + 4, 0, (struct sockaddr *) &rtp->them, sizeof(rtp->them));
			if (res < 0) 
				opbx_log(LOG_ERROR, "RTP Transmission error to %s:%d: %s\n",
					opbx_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr),
					ntohs(rtp->them.sin_port), strerror(errno));
			if (rtp_debug_test_addr(&rtp->them))
				opbx_verbose("Sent RTP packet to %s:%d (type %d, seq %d, ts %d, len %d)\n",
					    opbx_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr),
					    ntohs(rtp->them.sin_port), payload, rtp->seqno, rtp->lastdigitts, res - hdrlen);
		}
		/* Sequence number of last two end packets does not get incremented */
		if (x < 3)
			rtp->seqno++;
		/* Clear marker bit and set seqno */
		rtpheader[0] = htonl((2 << 30) | (payload << 16) | (rtp->seqno));
		/* For the last three packets, set the duration and the end bit */
		if (x == 2) {
#if 0
			/* No, this is wrong...  Do not increment lastdigitts, that's not according
			   to the RFC, as best we can determine */
			rtp->lastdigitts++; /* or else the SPA3000 will click instead of beeping... */
			rtpheader[1] = htonl(rtp->lastdigitts);
#endif			
			/* Make duration 800 (100ms) */
			rtpheader[3] |= htonl((800));
			/* Set the End bit */
			rtpheader[3] |= htonl((1 << 23));
		}
	}
	/* Increment the digit timestamp by 120ms, to ensure that digits
	   sent sequentially with no intervening non-digit packets do not
	   get sent with the same timestamp, and that sequential digits
	   have some 'dead air' in between them
	*/
	rtp->lastdigitts += 960;
	/* Increment the sequence number to reflect the last packet
	   that was sent
	*/
	rtp->seqno++;
	return 0;
}

int opbx_rtp_sendcng(struct opbx_rtp *rtp, int level)
{
	unsigned int *rtpheader;
	int hdrlen = 12;
	int res;
	int payload;
	char data[256];
	char iabuf[INET_ADDRSTRLEN];
	level = 127 - (level & 0x7f);
	payload = opbx_rtp_lookup_code(rtp, 0, OPBX_RTP_CN);

	/* If we have no peer, return immediately */	
	if (!rtp->them.sin_addr.s_addr)
		return 0;

	rtp->dtmfmute = opbx_tvadd(opbx_tvnow(), opbx_tv(0, 500000));

	/* Get a pointer to the header */
	rtpheader = (unsigned int *)data;
	rtpheader[0] = htonl((2 << 30) | (1 << 23) | (payload << 16) | (rtp->seqno++));
	rtpheader[1] = htonl(rtp->lastts);
	rtpheader[2] = htonl(rtp->ssrc); 
	data[12] = level;
	if (rtp->them.sin_port && rtp->them.sin_addr.s_addr) {
		res = rtp_sendto(rtp, (void *)rtpheader, hdrlen + 1, 0, (struct sockaddr *)&rtp->them, sizeof(rtp->them));
		if (res <0) 
			opbx_log(LOG_ERROR, "RTP Comfort Noise Transmission error to %s:%d: %s\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr), ntohs(rtp->them.sin_port), strerror(errno));
		if(rtp_debug_test_addr(&rtp->them))
			opbx_verbose("Sent Comfort Noise RTP packet to %s:%d (type %d, seq %d, ts %d, len %d)\n"
					, opbx_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr), ntohs(rtp->them.sin_port), payload, rtp->seqno, rtp->lastts,res - hdrlen);		   
		   
	}
	return 0;
}

static int opbx_rtp_raw_write(struct opbx_rtp *rtp, struct opbx_frame *f, int codec)
{
	unsigned char *rtpheader;
	char iabuf[INET_ADDRSTRLEN];
	int hdrlen = 12;
	int res;
	int ms;
	int pred;
	int mark = 0;

	ms = calc_txstamp(rtp, &f->delivery);
	/* Default prediction */
	if (f->subclass < OPBX_FORMAT_MAX_AUDIO) {
		pred = rtp->lastts + f->samples;

		/* Re-calculate last TS */
		rtp->lastts = rtp->lastts + ms * 8;
		if (opbx_tvzero(f->delivery)) {
			/* If this isn't an absolute delivery time, Check if it is close to our prediction, 
			   and if so, go with our prediction */
			if (abs(rtp->lastts - pred) < MAX_TIMESTAMP_SKEW)
				rtp->lastts = pred;
			else {
				if (option_debug > 2)
					opbx_log(LOG_DEBUG, "Difference is %d, ms is %d\n", abs(rtp->lastts - pred), ms);
				mark = 1;
			}
		}
	} else {
		mark = f->subclass & 0x1;
		pred = rtp->lastovidtimestamp + f->samples;
		/* Re-calculate last TS */
		rtp->lastts = rtp->lastts + ms * 90;
		/* If it's close to our prediction, go for it */
		if (opbx_tvzero(f->delivery)) {
			if (abs(rtp->lastts - pred) < 7200) {
				rtp->lastts = pred;
				rtp->lastovidtimestamp += f->samples;
			} else {
				if (option_debug > 2)
					opbx_log(LOG_DEBUG, "Difference is %d, ms is %d (%d), pred/ts/samples %d/%d/%d\n", abs(rtp->lastts - pred), ms, ms * 90, rtp->lastts, pred, f->samples);
				rtp->lastovidtimestamp = rtp->lastts;
			}
		}
	}
	/* If the timestamp for non-digit packets has moved beyond the timestamp
	   for digits, update the digit timestamp.
	*/
	if (rtp->lastts > rtp->lastdigitts)
		rtp->lastdigitts = rtp->lastts;

	/* Get a pointer to the header */
	rtpheader = (unsigned char *)(f->data - hdrlen);

	put_unaligned_uint32(rtpheader, htonl((2 << 30) | (codec << 16) | (rtp->seqno) | (mark << 23)));
	put_unaligned_uint32(rtpheader + 4, htonl(rtp->lastts));
	put_unaligned_uint32(rtpheader + 8, htonl(rtp->ssrc)); 

	if (rtp->them.sin_port && rtp->them.sin_addr.s_addr) {
		res = rtp_sendto(rtp, (void *)rtpheader, f->datalen + hdrlen, 0, (struct sockaddr *)&rtp->them, sizeof(rtp->them));
		if (res <0) {
			if (!rtp->nat || (rtp->nat && (opbx_test_flag(rtp, FLAG_NAT_ACTIVE) == FLAG_NAT_ACTIVE))) {
				opbx_log(LOG_DEBUG, "RTP Transmission error of packet %d to %s:%d: %s\n", rtp->seqno, opbx_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr), ntohs(rtp->them.sin_port), strerror(errno));
			} else if ((opbx_test_flag(rtp, FLAG_NAT_ACTIVE) == FLAG_NAT_INACTIVE) || rtpdebug) {
				/* Only give this error message once if we are not RTP debugging */
				if (option_debug || rtpdebug)
					opbx_log(LOG_DEBUG, "RTP NAT: Can't write RTP to private address %s:%d, waiting for other end to send audio...\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr), ntohs(rtp->them.sin_port));
				opbx_set_flag(rtp, FLAG_NAT_INACTIVE_NOWARN);
			}
		}
				
		if(rtp_debug_test_addr(&rtp->them))
			opbx_verbose("Sent RTP packet to %s:%d (type %d, seq %d, ts %d, len %d)\n"
					, opbx_inet_ntoa(iabuf, sizeof(iabuf), rtp->them.sin_addr), ntohs(rtp->them.sin_port), codec, rtp->seqno, rtp->lastts,res - hdrlen);
	}

	rtp->seqno++;

	return 0;
}

int opbx_rtp_write(struct opbx_rtp *rtp, struct opbx_frame *_f)
{
	struct opbx_frame *f;
	int codec;
	int hdrlen = 12;
	int subclass;
	

	/* If we have no peer, return immediately */	
	if (!rtp->them.sin_addr.s_addr)
		return 0;

	/* If there is no data length, return immediately */
	if (!_f->datalen) 
		return 0;
	
	/* Make sure we have enough space for RTP header */
	if ((_f->frametype != OPBX_FRAME_VOICE) && (_f->frametype != OPBX_FRAME_VIDEO)) {
		opbx_log(LOG_WARNING, "RTP can only send voice\n");
		return -1;
	}

	subclass = _f->subclass;
	if (_f->frametype == OPBX_FRAME_VIDEO)
		subclass &= ~0x1;

	codec = opbx_rtp_lookup_code(rtp, 1, subclass);
	if (codec < 0) {
		opbx_log(LOG_WARNING, "Don't know how to send format %s packets with RTP\n", opbx_getformatname(_f->subclass));
		return -1;
	}

	if (rtp->lasttxformat != subclass) {
		/* New format, reset the smoother */
		if (option_debug)
			opbx_log(LOG_DEBUG, "Ooh, format changed from %s to %s\n", opbx_getformatname(rtp->lasttxformat), opbx_getformatname(subclass));
		rtp->lasttxformat = subclass;
		if (rtp->smoother)
			opbx_smoother_free(rtp->smoother);
		rtp->smoother = NULL;
	}

	
	if (!rtp->smoother) {
		struct rtp_codec_table *ent;
		int ms = rtp->framems;
		int len;

		if ((ent = lookup_rtp_smoother_codec(subclass, &rtp->framems, &len))) {
			
			if (rtp->framems != ms) {
				opbx_log(LOG_DEBUG, "Had to change frame MS from %d to %d\n", ms, rtp->framems);
			}

			if (!(rtp->smoother = opbx_smoother_new(len))) {
				opbx_log(LOG_WARNING, "Unable to create smoother ms: %d len: %d:(\n", rtp->framems, len);
				return -1;
			}

			if (ent->flags) {
				opbx_smoother_set_flags(rtp->smoother, ent->flags);			
			}

			opbx_log(LOG_DEBUG, "Able to create smoother :) ms: %d len %d\n", rtp->framems, len);
		}
	}

	if (rtp->smoother) {
		if (opbx_smoother_test_flag(rtp->smoother, OPBX_SMOOTHER_FLAG_BE)) {
			opbx_smoother_feed_be(rtp->smoother, _f);
		} else {
			opbx_smoother_feed(rtp->smoother, _f);
		}

		while((f = opbx_smoother_read(rtp->smoother))) {
			opbx_rtp_raw_write(rtp, f, codec);
		} 
	} else {
		/* Don't buffer outgoing frames; send them one-per-packet: */
		if (_f->offset < hdrlen) {
			f = opbx_frdup(_f);
		} else {
			f = _f;
		}
		opbx_rtp_raw_write(rtp, f, codec);
	}

	return 0;
}

/*--- opbx_rtp_proto_unregister: Unregister interface to channel driver */
void opbx_rtp_proto_unregister(struct opbx_rtp_protocol *proto)
{
	struct opbx_rtp_protocol *cur, *prev;

	cur = protos;
	prev = NULL;
	while(cur) {
		if (cur == proto) {
			if (prev)
				prev->next = proto->next;
			else
				protos = proto->next;
			return;
		}
		prev = cur;
		cur = cur->next;
	}
}

/*--- opbx_rtp_proto_register: Register interface to channel driver */
int opbx_rtp_proto_register(struct opbx_rtp_protocol *proto)
{
	struct opbx_rtp_protocol *cur;
	cur = protos;
	while(cur) {
		if (cur->type == proto->type) {
			opbx_log(LOG_WARNING, "Tried to register same protocol '%s' twice\n", cur->type);
			return -1;
		}
		cur = cur->next;
	}
	proto->next = protos;
	protos = proto;
	return 0;
}

/*--- get_proto: Get channel driver interface structure */
static struct opbx_rtp_protocol *get_proto(struct opbx_channel *chan)
{
	struct opbx_rtp_protocol *cur;

	cur = protos;
	while(cur) {
		if (cur->type == chan->type) {
			return cur;
		}
		cur = cur->next;
	}
	return NULL;
}

/* opbx_rtp_bridge: Bridge calls. If possible and allowed, initiate
	re-invite so the peers exchange media directly outside 
	of OpenPBX. */
enum opbx_bridge_result opbx_rtp_bridge(struct opbx_channel *c0, struct opbx_channel *c1, int flags, struct opbx_frame **fo, struct opbx_channel **rc)
{
	struct opbx_frame *f;
	struct opbx_channel *who, *cs[3];
	struct opbx_rtp *p0, *p1;		/* Audio RTP Channels */
	struct opbx_rtp *vp0, *vp1;		/* Video RTP channels */
	struct opbx_rtp_protocol *pr0, *pr1;
	struct sockaddr_in ac0, ac1;
	struct sockaddr_in vac0, vac1;
	struct sockaddr_in t0, t1;
	struct sockaddr_in vt0, vt1;
	char iabuf[INET_ADDRSTRLEN];
	
	void *pvt0, *pvt1;
	int to;
	int codec0,codec1, oldcodec0, oldcodec1;
	
	memset(&vt0, 0, sizeof(vt0));
	memset(&vt1, 0, sizeof(vt1));
	memset(&vac0, 0, sizeof(vac0));
	memset(&vac1, 0, sizeof(vac1));

	/* if need DTMF, cant native bridge */
	if (flags & (OPBX_BRIDGE_DTMF_CHANNEL_0 | OPBX_BRIDGE_DTMF_CHANNEL_1))
		return OPBX_BRIDGE_FAILED_NOWARN;

	/* Lock channels */
	opbx_mutex_lock(&c0->lock);
	while(opbx_mutex_trylock(&c1->lock)) {
		opbx_mutex_unlock(&c0->lock);
		usleep(1);
		opbx_mutex_lock(&c0->lock);
	}

	/* Find channel driver interfaces */
	pr0 = get_proto(c0);
	pr1 = get_proto(c1);
	if (!pr0) {
		opbx_log(LOG_WARNING, "Can't find native functions for channel '%s'\n", c0->name);
		opbx_mutex_unlock(&c0->lock);
		opbx_mutex_unlock(&c1->lock);
		return OPBX_BRIDGE_FAILED;
	}
	if (!pr1) {
		opbx_log(LOG_WARNING, "Can't find native functions for channel '%s'\n", c1->name);
		opbx_mutex_unlock(&c0->lock);
		opbx_mutex_unlock(&c1->lock);
		return OPBX_BRIDGE_FAILED;
	}

	/* Get channel specific interface structures */
	pvt0 = c0->tech_pvt;
	pvt1 = c1->tech_pvt;

	/* Get audio and video interface (if native bridge is possible) */
	p0 = pr0->get_rtp_info(c0);
	if (pr0->get_vrtp_info)
		vp0 = pr0->get_vrtp_info(c0);
	else
		vp0 = NULL;
	p1 = pr1->get_rtp_info(c1);
	if (pr1->get_vrtp_info)
		vp1 = pr1->get_vrtp_info(c1);
	else
		vp1 = NULL;

	/* Check if bridge is still possible (In SIP canreinvite=no stops this, like NAT) */
	if (!p0 || !p1) {
		/* Somebody doesn't want to play... */
		opbx_mutex_unlock(&c0->lock);
		opbx_mutex_unlock(&c1->lock);
		return OPBX_BRIDGE_FAILED_NOWARN;
	}

#ifdef ENABLE_SRTP
	if (p0->srtp || p1->srtp) {
		opbx_log(LOG_NOTICE, "Cannot native bridge in SRTP.\n");
		opbx_mutex_unlock(&c0->lock);
		opbx_mutex_unlock(&c1->lock);
		return OPBX_BRIDGE_FAILED_NOWARN;
	}
#endif
	/* Get codecs from both sides */
	if (pr0->get_codec)
		codec0 = pr0->get_codec(c0);
	else
		codec0 = 0;
	if (pr1->get_codec)
		codec1 = pr1->get_codec(c1);
	else
		codec1 = 0;
	if (pr0->get_codec && pr1->get_codec) {
		/* Hey, we can't do reinvite if both parties speak different codecs */
		if (!(codec0 & codec1)) {
			if (option_debug)
				opbx_log(LOG_DEBUG, "Channel codec0 = %d is not codec1 = %d, cannot native bridge in RTP.\n", codec0, codec1);
			opbx_mutex_unlock(&c0->lock);
			opbx_mutex_unlock(&c1->lock);
			return OPBX_BRIDGE_FAILED_NOWARN;
		}
	}

	/* Ok, we should be able to redirect the media. Start with one channel */
	if (pr0->set_rtp_peer(c0, p1, vp1, codec1, opbx_test_flag(p1, FLAG_NAT_ACTIVE))) 
		opbx_log(LOG_WARNING, "Channel '%s' failed to talk to '%s'\n", c0->name, c1->name);
	else {
		/* Store RTP peer */
		opbx_rtp_get_peer(p1, &ac1);
		if (vp1)
			opbx_rtp_get_peer(vp1, &vac1);
	}
	/* Then test the other channel */
	if (pr1->set_rtp_peer(c1, p0, vp0, codec0, opbx_test_flag(p0, FLAG_NAT_ACTIVE)))
		opbx_log(LOG_WARNING, "Channel '%s' failed to talk back to '%s'\n", c1->name, c0->name);
	else {
		/* Store RTP peer */
		opbx_rtp_get_peer(p0, &ac0);
		if (vp0)
			opbx_rtp_get_peer(vp0, &vac0);
	}
	opbx_mutex_unlock(&c0->lock);
	opbx_mutex_unlock(&c1->lock);
	/* External RTP Bridge up, now loop and see if something happes that force us to take the
		media back to OpenPBX */
	cs[0] = c0;
	cs[1] = c1;
	cs[2] = NULL;
	oldcodec0 = codec0;
	oldcodec1 = codec1;
	for (;;) {
		/* Check if something changed... */
		if ((c0->tech_pvt != pvt0)  ||
			(c1->tech_pvt != pvt1) ||
			(c0->masq || c0->masqr || c1->masq || c1->masqr)) {
				opbx_log(LOG_DEBUG, "Oooh, something is weird, backing out\n");
				if (c0->tech_pvt == pvt0) {
					if (pr0->set_rtp_peer(c0, NULL, NULL, 0, 0)) 
						opbx_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c0->name);
				}
				if (c1->tech_pvt == pvt1) {
					if (pr1->set_rtp_peer(c1, NULL, NULL, 0, 0)) 
						opbx_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c1->name);
				}
				return OPBX_BRIDGE_RETRY;
		}
		to = -1;
		/* Now check if they have changed address */
		opbx_rtp_get_peer(p1, &t1);
		opbx_rtp_get_peer(p0, &t0);
		if (pr0->get_codec)
			codec0 = pr0->get_codec(c0);
		if (pr1->get_codec)
			codec1 = pr1->get_codec(c1);
		if (vp1)
			opbx_rtp_get_peer(vp1, &vt1);
		if (vp0)
			opbx_rtp_get_peer(vp0, &vt0);
		if (inaddrcmp(&t1, &ac1) || (vp1 && inaddrcmp(&vt1, &vac1)) || (codec1 != oldcodec1)) {
			if (option_debug > 1) {
				opbx_log(LOG_DEBUG, "Oooh, '%s' changed end address to %s:%d (format %d)\n", 
					c1->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), t1.sin_addr), ntohs(t1.sin_port), codec1);
				opbx_log(LOG_DEBUG, "Oooh, '%s' changed end vaddress to %s:%d (format %d)\n", 
					c1->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), vt1.sin_addr), ntohs(vt1.sin_port), codec1);
				opbx_log(LOG_DEBUG, "Oooh, '%s' was %s:%d/(format %d)\n", 
					c1->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), ac1.sin_addr), ntohs(ac1.sin_port), oldcodec1);
				opbx_log(LOG_DEBUG, "Oooh, '%s' was %s:%d/(format %d)\n", 
					c1->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), vac1.sin_addr), ntohs(vac1.sin_port), oldcodec1);
			}
			if (pr0->set_rtp_peer(c0, t1.sin_addr.s_addr ? p1 : NULL, vt1.sin_addr.s_addr ? vp1 : NULL, codec1, opbx_test_flag(p1, FLAG_NAT_ACTIVE))) 
				opbx_log(LOG_WARNING, "Channel '%s' failed to update to '%s'\n", c0->name, c1->name);
			memcpy(&ac1, &t1, sizeof(ac1));
			memcpy(&vac1, &vt1, sizeof(vac1));
			oldcodec1 = codec1;
		}
		if (inaddrcmp(&t0, &ac0) || (vp0 && inaddrcmp(&vt0, &vac0))) {
			if (option_debug) {
				opbx_log(LOG_DEBUG, "Oooh, '%s' changed end address to %s:%d (format %d)\n", 
					c0->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), t0.sin_addr), ntohs(t0.sin_port), codec0);
				opbx_log(LOG_DEBUG, "Oooh, '%s' was %s:%d/(format %d)\n", 
					c0->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), ac0.sin_addr), ntohs(ac0.sin_port), oldcodec0);
			}
			if (pr1->set_rtp_peer(c1, t0.sin_addr.s_addr ? p0 : NULL, vt0.sin_addr.s_addr ? vp0 : NULL, codec0, opbx_test_flag(p0, FLAG_NAT_ACTIVE)))
				opbx_log(LOG_WARNING, "Channel '%s' failed to update to '%s'\n", c1->name, c0->name);
			memcpy(&ac0, &t0, sizeof(ac0));
			memcpy(&vac0, &vt0, sizeof(vac0));
			oldcodec0 = codec0;
		}
		who = opbx_waitfor_n(cs, 2, &to);
		if (!who) {
			if (option_debug)
				opbx_log(LOG_DEBUG, "Ooh, empty read...\n");
			/* check for hangup / whentohangup */
			if (opbx_check_hangup(c0) || opbx_check_hangup(c1))
				break;
			continue;
		}
		f = opbx_read(who);
		if (!f || ((f->frametype == OPBX_FRAME_DTMF) &&
				   (((who == c0) && (flags & OPBX_BRIDGE_DTMF_CHANNEL_0)) || 
			       ((who == c1) && (flags & OPBX_BRIDGE_DTMF_CHANNEL_1))))) {
			*fo = f;
			*rc = who;
			if (option_debug)
				opbx_log(LOG_DEBUG, "Oooh, got a %s\n", f ? "digit" : "hangup");
			if ((c0->tech_pvt == pvt0) && (!c0->_softhangup)) {
				if (pr0->set_rtp_peer(c0, NULL, NULL, 0, 0)) 
					opbx_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c0->name);
			}
			if ((c1->tech_pvt == pvt1) && (!c1->_softhangup)) {
				if (pr1->set_rtp_peer(c1, NULL, NULL, 0, 0)) 
					opbx_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c1->name);
			}
			return OPBX_BRIDGE_COMPLETE;
		} else if ((f->frametype == OPBX_FRAME_CONTROL) && !(flags & OPBX_BRIDGE_IGNORE_SIGS)) {
			if ((f->subclass == OPBX_CONTROL_HOLD) || (f->subclass == OPBX_CONTROL_UNHOLD) ||
			    (f->subclass == OPBX_CONTROL_VIDUPDATE)) {
				opbx_indicate(who == c0 ? c1 : c0, f->subclass);
				opbx_frfree(f);
			} else {
				*fo = f;
				*rc = who;
				opbx_log(LOG_DEBUG, "Got a FRAME_CONTROL (%d) frame on channel %s\n", f->subclass, who->name);
				return OPBX_BRIDGE_COMPLETE;
			}
		} else {
			if ((f->frametype == OPBX_FRAME_DTMF) || 
				(f->frametype == OPBX_FRAME_VOICE) || 
				(f->frametype == OPBX_FRAME_VIDEO)) {
				/* Forward voice or DTMF frames if they happen upon us */
				if (who == c0) {
					opbx_write(c1, f);
				} else if (who == c1) {
					opbx_write(c0, f);
				}
			}
			opbx_frfree(f);
		}
		/* Swap priority not that it's a big deal at this point */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
		
	}
	return OPBX_BRIDGE_FAILED;
}

static int rtp_do_debug_ip(int fd, int argc, char *argv[])
{
	struct hostent *hp;
	struct opbx_hostent ahp;
	char iabuf[INET_ADDRSTRLEN];
	int port = 0;
	char *p, *arg;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	arg = argv[3];
	p = strstr(arg, ":");
	if (p) {
		*p = '\0';
		p++;
		port = atoi(p);
	}
	hp = opbx_gethostbyname(arg, &ahp);
	if (hp == NULL)
		return RESULT_SHOWUSAGE;
	rtpdebugaddr.sin_family = AF_INET;
	memcpy(&rtpdebugaddr.sin_addr, hp->h_addr, sizeof(rtpdebugaddr.sin_addr));
	rtpdebugaddr.sin_port = htons(port);
	if (port == 0)
		opbx_cli(fd, "RTP Debugging Enabled for IP: %s\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), rtpdebugaddr.sin_addr));
	else
		opbx_cli(fd, "RTP Debugging Enabled for IP: %s:%d\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), rtpdebugaddr.sin_addr), port);
	rtpdebug = 1;
	return RESULT_SUCCESS;
}

static int rtp_do_debug(int fd, int argc, char *argv[])
{
	if(argc != 2) {
		if(argc != 4)
			return RESULT_SHOWUSAGE;
		return rtp_do_debug_ip(fd, argc, argv);
	}
	rtpdebug = 1;
	memset(&rtpdebugaddr,0,sizeof(rtpdebugaddr));
	opbx_cli(fd, "RTP Debugging Enabled\n");
	return RESULT_SUCCESS;
}
   
static int rtp_no_debug(int fd, int argc, char *argv[])
{
	if(argc !=3)
		return RESULT_SHOWUSAGE;
	rtpdebug = 0;
	opbx_cli(fd,"RTP Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static char debug_usage[] =
  "Usage: rtp debug [ip host[:port]]\n"
  "       Enable dumping of all RTP packets to and from host.\n";

static char no_debug_usage[] =
  "Usage: rtp no debug\n"
  "       Disable all RTP debugging\n";

static struct opbx_cli_entry  cli_debug_ip =
{{ "rtp", "debug", "ip", NULL } , rtp_do_debug, "Enable RTP debugging on IP", debug_usage };

static struct opbx_cli_entry  cli_debug =
{{ "rtp", "debug", NULL } , rtp_do_debug, "Enable RTP debugging", debug_usage };

static struct opbx_cli_entry  cli_no_debug =
{{ "rtp", "no", "debug", NULL } , rtp_no_debug, "Disable RTP debugging", no_debug_usage };

void opbx_rtp_reload(void)
{
	struct opbx_config *cfg;
	char *s;

	rtpstart = 5000;
	rtpend = 31000;
	cfg = opbx_config_load("rtp.conf");
	if (cfg) {
		if ((s = opbx_variable_retrieve(cfg, "general", "rtpstart"))) {
			rtpstart = atoi(s);
			if (rtpstart < 1024)
				rtpstart = 1024;
			if (rtpstart > 65535)
				rtpstart = 65535;
		}
		if ((s = opbx_variable_retrieve(cfg, "general", "rtpend"))) {
			rtpend = atoi(s);
			if (rtpend < 1024)
				rtpend = 1024;
			if (rtpend > 65535)
				rtpend = 65535;
		}
		if ((s = opbx_variable_retrieve(cfg, "general", "rtpchecksums"))) {
#ifdef SO_NO_CHECK
			if (opbx_false(s))
				nochecksums = 1;
			else
				nochecksums = 0;
#else
			if (opbx_false(s))
				opbx_log(LOG_WARNING, "Disabling RTP checksums is not supported on this operating system!\n");
#endif
		}
		opbx_config_destroy(cfg);
	}
	if (rtpstart >= rtpend) {
		opbx_log(LOG_WARNING, "Unreasonable values for RTP start/end port in rtp.conf\n");
		rtpstart = 5000;
		rtpend = 31000;
	}
	if (option_verbose > 1)
		opbx_verbose(VERBOSE_PREFIX_2 "RTP Allocating from port range %d -> %d\n", rtpstart, rtpend);
	
}

/*--- opbx_rtp_init: Initialize the RTP system in OpenPBX */
void opbx_rtp_init(void)
{
	opbx_cli_register(&cli_debug);
	opbx_cli_register(&cli_debug_ip);
	opbx_cli_register(&cli_no_debug);
	opbx_rtp_reload();
#ifdef ENABLE_SRTP
	opbx_log(LOG_NOTICE, "srtp_init\n");
	srtp_init();
	srtp_install_event_handler(srtp_event_cb);
#endif
}
