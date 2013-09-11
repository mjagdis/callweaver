/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 */

/*
 *
 * Real-time Protocol Support
 *      Supports RTP and RTCP with Symmetric RTP support for NAT
 *      traversal
 * 
 */
#include <ctype.h>
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

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include <callweaver/udp.h>
#include "callweaver/rtp.h"
#include "callweaver/frame.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/acl.h"
#include "callweaver/channel.h"
#include "callweaver/config.h"
#include "callweaver/lock.h"
#include "callweaver/utils.h"
#include "callweaver/manager.h"
#include "callweaver/cli.h"
#include "callweaver/unaligned.h"
#include "callweaver/utils.h"
#include "callweaver/io.h"


#undef INCREMENTAL_RFC2833_EVENTS     /* If defined we increase the duration of the event each time
                                       * send a continuation. If not defined we specify the full
                                       * duration (which is always the DTMF duration below) in
                                       * every continuation. Specifying the full length each time
                                       * helps avoid problems in high jitter situations with some
                                       * types of peer. It could conceivably lead to problems
                                       * with some peers that don't expect to see durations beyond
                                       * some window but this has not yet been seen whereas jitter
                                       * problems causing dropped or repeated digits have :-)
                                       */

#define DTMF_TONE_DURATION 100        /* Duration of a DTMF tone in ms. Currently this MUST be 100 due to fixed values elsewhere */


#define MAX_TIMESTAMP_SKEW    640

#define RTP_MTU        1200

#define DEFAULT_RTPSTART 5000
#define DEFAULT_RTPEND 31000
#define DEFAULT_DTMFTIMEOUT 3000    /* 3000 of whatever the remote is using for clock ticks (generally samples) */

static int dtmftimeout = DEFAULT_DTMFTIMEOUT;
static int rtpstart = 0;
static int rtpend = 0;
static int rtpdebug = 0;        /* Are we debugging? */
static struct cw_sockaddr_net rtpdebugaddr;    /* Debug packets to/from this host */
static int nochecksums = 0;

#define NAT_STATE_INACTIVE           0
#define NAT_STATE_INACTIVE_NOWARN    1
#define NAT_STATE_ACTIVE             2

static struct cw_rtp_protocol *protos = NULL;

#ifdef ENABLE_SRTP
struct cw_srtp_res *g_srtp_res;
struct cw_srtp_policy_res *g_policy_res;
#endif

struct rtp_codec_table
{
    int format;
    int len;
    int defaultms;
    int increment;
    unsigned int flags;
};

static const struct rtp_codec_table RTP_CODEC_TABLE[] =
{
    {CW_FORMAT_SLINEAR, 160, 20, 10, CW_SMOOTHER_FLAG_BE},
    {CW_FORMAT_ULAW,     80, 20, 10},
    {CW_FORMAT_G726,     40, 20, 10},
    {CW_FORMAT_ILBC,     50, 30, 30},
    {CW_FORMAT_G729A,    10, 20, 10, CW_SMOOTHER_FLAG_G729},
    {CW_FORMAT_GSM,      33, 20, 20},
    {0,0,0,0,0}
};

static const struct rtp_codec_table *lookup_rtp_smoother_codec(int format, int *ms, int *len)
{
    const struct rtp_codec_table *ent = NULL;
    int x;

    *len = 0;
    for (x = 0;  RTP_CODEC_TABLE[x].format;  x++)
    {
        if (RTP_CODEC_TABLE[x].format == format)
        {
            ent = &RTP_CODEC_TABLE[x];
            if (*ms == 0)
                *ms = ent->defaultms;
            while (*ms % ent->increment)
                (*ms)++;
            while ((*len = (*ms / ent->increment) * ent->len) > RTP_MTU)
                *ms -= ent->increment;
            break;
        }
    }
    return ent;
}

int cw_rtp_fd(struct cw_rtp *rtp)
{
	return udp_socket_fd(&rtp->sock_info[0]);
}

int cw_rtcp_fd(struct cw_rtp *rtp)
{
	return udp_socket_fd(&rtp->sock_info[1]);
}

struct sockaddr *cw_rtp_get_peer(struct cw_rtp *rtp)
{
	return &rtp->sock_info[0].peer.sa;
}

void cw_rtp_setnat(struct cw_rtp *rtp, int nat)
{
    rtp->nat = nat;
    udp_socket_set_nat(&rtp->sock_info[0], nat);
    udp_socket_set_nat(&rtp->sock_info[1], nat);
}

int cw_rtp_set_framems(struct cw_rtp *rtp, int ms) 
{
    if (ms)
    {
        if (rtp->smoother)
        {
            cw_smoother_free(rtp->smoother);
            rtp->smoother = NULL;
        }
        rtp->framems = ms;
    }
    return rtp->framems;
}

static struct cw_frame *send_dtmf(struct cw_rtp *rtp)
{
    if (cw_tvcmp(cw_tvnow(), rtp->dtmfmute) < 0)
    {
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "Ignore potential DTMF echo from %@\n", cw_rtp_get_peer(rtp));
        rtp->lastevent_code = 0;
        return &cw_null_frame;
    }

    if (option_debug)
        cw_log(CW_LOG_DEBUG, "Sending dtmf: %d (%c), to %@\n", rtp->lastevent_code, rtp->lastevent_code, cw_rtp_get_peer(rtp));

    cw_fr_init(&rtp->f);
    if (rtp->lastevent_code == 'X')
    {
        rtp->f.frametype = CW_FRAME_CONTROL;
        rtp->f.subclass = CW_CONTROL_FLASH;
    }
    else
    {
        rtp->f.frametype = CW_FRAME_DTMF;
        rtp->f.subclass = rtp->lastevent_code;
    }
    rtp->lastevent_code = 0;
    return  &rtp->f;
}

static inline int rtp_debug_test_addr(const struct sockaddr *addr)
{
    if (rtpdebug == 0 || cw_sockaddr_cmp(addr, &rtpdebugaddr.sa, -1, cw_sockaddr_get_port(&rtpdebugaddr.sa)))
        return 0;

    return 1;
}

static struct cw_frame *process_cisco_dtmf(struct cw_rtp *rtp, unsigned char *data)
{
    static char map[] = "0123456789*#ABCDX";
    unsigned int event;
    char code;
    struct cw_frame *f = NULL;

    event = ntohl(*((unsigned int *) data)) & 0x001F;
#if 0
    printf("Cisco Digit: %08x\n", event);
#endif    
    code = (event < sizeof(map)/sizeof(map[0]) ? map[event] : '?');
    if (rtp->lastevent_code && (rtp->lastevent_code != code))
        f = send_dtmf(rtp);
    rtp->lastevent_code = code;
    rtp->lastevent_duration = dtmftimeout;
    return f;
}

/* process_rfc2833: Process RTP DTMF and events according to RFC 2833:
    "RTP Payload for DTMF Digits, Telephony Tones and Telephony Signals"
*/
static struct cw_frame *process_rfc2833(struct cw_rtp *rtp, unsigned char *data, int len, unsigned int seqno, uint32_t timestamp)
{
    struct cw_frame *f = NULL;
    uint32_t event;
    uint16_t duration;
    int event_end;
    int samets;

    event = ntohl(*((uint32_t *) data));
    event_end = (event >> 23) & 1;
    duration = event & 0xFFFF;
    event >>= 24;

    if (rtpdebug)
        cw_log(CW_LOG_DEBUG, "- RTP 2833 Event: %08x (len = %d)\n", event, len);

    /* If the timestamp has changed but still comes before the end time of
     * the current event plus 40ms we consider it to be equal.
     * There are broken senders out there...
     * (The 40ms is the minimum inter-digit interval in known specifications,
     * specifically BT SIN351 - see cw_rtp_senddigit_continue)
     * If the timestamp changes during the end packets we'll be in trouble
     * but this hasn't been seen in the wild.
     */
    samets = (timestamp == rtp->lastevent_startts || timestamp - rtp->lastevent_startts <= rtp->lastevent_duration + 40 * 8);

    /* If we have something in progress we should send it up the stack if
     * the this packet is not a continuation of the same event.
     */
    if (rtp->lastevent_code && !samets)
        f = send_dtmf(rtp);

    /* If it's the first packet we've seen for this event (it might
     * not be the actual start packet - that might be out of order
     * or dropped) set up the event.
     */
    if (!samets)
    {
        static char map[] = "0123456789*#ABCDX";
        rtp->lastevent_code = (event < sizeof(map)/sizeof(map[0]) ? map[event] : '?');
        rtp->lastevent_seqno = seqno;
        rtp->lastevent_startts = timestamp;
        rtp->lastevent_duration = duration;
    }
    else
    {
        /* If this packet comes after any other packet we've seen so far
	 * in the current event we'll take its duration as an update to
	 * the event duration, but still using the original start time.
	 * If it comes before any other packet we've seen and its timestamp
	 * is earlier than our current event start we adjust our idea of
	 * the event start time - we are seeing both changing timestamps
	 * and packet reordering.
         * N.B. seqnos and timestamps are unsigned and wrap...
         */
        if (seqno - rtp->lastevent_seqno < 1000)
        {
            rtp->lastevent_duration = timestamp + duration - rtp->lastevent_startts;
            rtp->lastevent_seqno = seqno;
        }
	else if (timestamp - rtp->lastevent_startts < 10000)
        {
            rtp->lastevent_duration += rtp->lastevent_startts - timestamp;
            rtp->lastevent_startts = timestamp;
        }
    }

    /* If we have an event in progress and the end flag is set what
     * we know is all we need so we can send the event up the stack.
     * Unless we already have an event to send up. If that happens
     * we'll just have to hope that we don't drop the duplicate end
     * packets, otherwise the only thing that will stop the second
     * event is the maximum event duration timeout.
     */
    if (rtp->lastevent_code && event_end && !f)
        f = send_dtmf(rtp);

    return f;
}

/*--- process_rfc3389: Process Comfort Noise RTP. 
    This is incomplete at the moment.
*/
static struct cw_frame *process_rfc3389(struct cw_rtp *rtp, unsigned char *data, int len)
{
    struct cw_frame *f = NULL;

    /* Convert comfort noise into audio with various codecs.  Unfortunately this doesn't
       totally help us out becuase we don't have an engine to keep it going and we are not
       guaranteed to have it every 20ms or anything */
    if (rtpdebug)
        cw_log(CW_LOG_DEBUG, "- RTP 3389 Comfort noise event: Level %d (len = %d)\n", rtp->lastrxformat, len);

    if (!rtp->warn_3389)
    {
        cw_log(CW_LOG_NOTICE, "Comfort noise support incomplete in CallWeaver (RFC 3389). Please turn off on client if possible. Client IP: %@\n", cw_rtp_get_peer(rtp));
        rtp->warn_3389 = 1;
    }

    /* Must have at least one byte */
    if (len == 0)
        return NULL;
    if (len < 24)
    {
        rtp->f.data = rtp->rawdata + CW_FRIENDLY_OFFSET;
        rtp->f.datalen = len - 1;
        rtp->f.offset = CW_FRIENDLY_OFFSET;
        memcpy(rtp->f.data, data + 1, len - 1);
    }
    else
    {
        rtp->f.data = NULL;
        rtp->f.offset = 0;
        rtp->f.datalen = 0;
    }
    cw_fr_init(&rtp->f);
    rtp->f.frametype = CW_FRAME_CNG;
    rtp->f.subclass = data[0] & 0x7F;
    rtp->f.datalen = len - 1;
    f = &rtp->f;
    return f;
}

static int rtp_recv(struct cw_rtp *rtp, void *buf, size_t size, int flags, int *actions)
{
    int len;

    len = udp_socket_recv(&rtp->sock_info[0], buf, size, flags, actions);
    if (len < 0)
        return len;

#ifdef ENABLE_SRTP
    if (g_srtp_res && rtp->srtp) {
        int res;

        res = g_srtp_res->unprotect(rtp->srtp, buf, &len);
        if (res < 0)
            return -1;
    }
#endif

    return len;
}

static int rtp_sendto(struct cw_rtp *rtp, void *buf, size_t size, int flags)
{
#ifdef ENABLE_SRTP
    if (g_srtp_res && rtp->srtp) {
        int res;

	res = g_srtp_res->protect(rtp->srtp, &buf, &size);
        if (res < 0)
            return -1;
    }
#endif

    return udp_socket_send(&rtp->sock_info[0], buf, size, flags);
}

#ifdef ENABLE_SRTP
int cw_rtp_register_srtp(struct cw_srtp_res *srtp_res,
			   struct cw_srtp_policy_res *policy_res)
{
	if (g_srtp_res || g_policy_res)
		return -1;

	if (!srtp_res || !policy_res)
		return -1;

	g_srtp_res = srtp_res;
	g_policy_res = policy_res;
	return 0;
}

int cw_rtp_unregister_srtp(struct cw_srtp_res *srtp_res, struct cw_srtp_policy_res *policy_res)
{
	CW_UNUSED(srtp_res);
	CW_UNUSED(policy_res);

	g_srtp_res = NULL;
	g_policy_res = NULL;
	return 0;
}

int cw_srtp_is_registered(void)
{
	return g_srtp_res && g_policy_res;
}

unsigned int cw_rtp_get_ssrc(struct cw_rtp *rtp)
{
	return rtp->ssrc;
}

void cw_rtp_set_srtp_cb(struct cw_rtp *rtp, const struct cw_srtp_cb *cb,
			 void *data)
{
	if (!g_srtp_res || !rtp->srtp)
		return;

	g_srtp_res->set_cb(rtp->srtp, cb, data);
}

void
cw_srtp_policy_set_ssrc(struct cw_srtp_policy *policy,
			  unsigned long ssrc, int inbound)
{
	if (!g_policy_res)
		return;

	g_policy_res->set_ssrc(policy, ssrc, inbound);
}

int cw_rtp_add_srtp_policy(struct cw_rtp *rtp, struct cw_srtp_policy *policy)
{
	int res;

	if (!g_srtp_res)
		return -1;

	if (!rtp->srtp) {
		res = g_srtp_res->create(&rtp->srtp, rtp, policy);
	} else {
		res = g_srtp_res->add_stream(rtp->srtp, policy);
	}

	return res;
}

struct cw_srtp_policy *cw_srtp_policy_alloc()
{
	if (!g_policy_res)
		return NULL;

	return g_policy_res->alloc();
}

void
cw_srtp_policy_destroy(struct cw_srtp_policy *policy)
{
	if (!g_policy_res)
		return;

	g_policy_res->destroy(policy);
}
int
cw_srtp_policy_set_suite(struct cw_srtp_policy *policy,
			   enum cw_srtp_suite suite)
{
	if (!g_policy_res)
		return -1;

	return g_policy_res->set_suite(policy, suite);
}

int
cw_srtp_policy_set_master_key(struct cw_srtp_policy *policy,
				const unsigned char *key, size_t key_len,
				const unsigned char *salt, size_t salt_len)
{
	if (!g_policy_res)
		return -1;

	return g_policy_res->set_master_key(policy, key, key_len,
					    salt, salt_len);
}

int
cw_srtp_policy_set_encr_alg(struct cw_srtp_policy *policy,
			      enum cw_srtp_ealg ealg)
{
	if (!g_policy_res)
		return -1;

	return g_policy_res->set_encr_alg(policy, ealg);
}

int
cw_srtp_policy_set_auth_alg(struct cw_srtp_policy *policy,
			      enum cw_srtp_aalg aalg)
{
	if (!g_policy_res)
		return -1;

	return g_policy_res->set_auth_alg(policy, aalg);
}

void cw_srtp_policy_set_encr_keylen(struct cw_srtp_policy *policy, int ekeyl)
{
	if (!g_policy_res)
		return;

	return g_policy_res->set_encr_keylen(policy, ekeyl);
}

void
cw_srtp_policy_set_auth_keylen(struct cw_srtp_policy *policy,
				 int akeyl)
{
	if (!g_policy_res)
		return;

	return g_policy_res->set_auth_keylen(policy, akeyl);
}

void
cw_srtp_policy_set_srtp_auth_taglen(struct cw_srtp_policy *policy,
				      int autht)
{
	if (!g_policy_res)
		return;

	return g_policy_res->set_srtp_auth_taglen(policy, autht);
}

void
cw_srtp_policy_set_srtp_encr_enable(struct cw_srtp_policy *policy,
				      int enable)
{
	if (!g_policy_res)
		return;

	return g_policy_res->set_srtp_encr_enable(policy, enable);
}

void
cw_srtp_policy_set_srtcp_encr_enable(struct cw_srtp_policy *policy,
				       int enable)
{
	if (!g_policy_res)
		return;

	return g_policy_res->set_srtcp_encr_enable(policy, enable);
}

void
cw_srtp_policy_set_srtp_auth_enable(struct cw_srtp_policy *policy,
				      int enable)
{
	if (!g_policy_res)
		return;

	return g_policy_res->set_srtp_auth_enable(policy, enable);
}

int cw_srtp_get_random(unsigned char *key, size_t len)
{
	if (!g_srtp_res)
		return -1;

	return g_srtp_res->get_random(key, len);
}
#endif


struct cw_frame *cw_rtcp_read(struct cw_channel *chan, struct cw_rtp *rtp)
{
    uint32_t rtcpdata[1024];
    int res, pkt;
    int actions;
    
    if (rtp == NULL)
        return &cw_null_frame;

    res = udp_socket_recv(&rtp->sock_info[1], rtcpdata, sizeof(rtcpdata), 0, &actions);
    if (res < 0)
    {
        if (errno != EAGAIN)
            cw_log(CW_LOG_WARNING, "RTP read error: %s\n", strerror(errno));
        return &cw_null_frame;
    }
    if ((actions & 1))
    {
        if (option_debug || rtpdebug)
            cw_log(CW_LOG_DEBUG, "RTCP NAT: Got RTCP from other end. Now sending to address %l@\n", &rtp->sock_info[1].peer.sa);
    }

    if (res < 8)
    {
        cw_log(CW_LOG_DEBUG, "RTCP Read too short\n");
        return &cw_null_frame;
    }

    pkt = 0;
    res >>= 2;
    while (pkt < res)
    {
        uint32_t l, length = ntohl(rtcpdata[pkt]);
        uint8_t PT = (length >> 16) & 0xff;
        uint8_t RC = (length >> 24) & 0x1f;
        uint8_t P = (length >> 24) & 0x20;
        uint8_t version = (length >> 30);
        int i;

        l = length &= 0xffff;
        if (P)
                l -= (ntohl(rtcpdata[pkt+length]) & 0xff) >> 2;

        if (pkt + l + 1 > res)
        {
            if (rtpdebug || rtp_debug_test_addr(&rtp->sock_info[1].peer.sa))
                cw_log(CW_LOG_DEBUG, "RTCP packet extends beyond received data. Ignored.\n");
            break;
        }

        if (version != 2)
        {
            if (rtpdebug || rtp_debug_test_addr(&rtp->sock_info[1].peer.sa))
                cw_log(CW_LOG_DEBUG, "RTCP packet version %d ignored. We only support version 2\n", version);
            goto next;
        }

        i = pkt + 2;

        switch (PT)
        {
            case 200: /* Sender Report */
                cw_manager_event(EVENT_FLAG_CALL, "RTCP-SR",
                    6,
                    cw_msg_tuple("Channel",  "%s",    chan->name),
                    cw_msg_tuple("Uniqueid", "%s",    chan->uniqueid),
                    cw_msg_tuple("NTP",      "%u.%u", ntohl(rtcpdata[i]), ntohl(rtcpdata[i+1])),
                    cw_msg_tuple("RTP",      "%u",    ntohl(rtcpdata[i+2])),
                    cw_msg_tuple("Packets",  "%u",    ntohl(rtcpdata[i+3])),
                    cw_msg_tuple("Data",     "%u",    ntohl(rtcpdata[i+4]))
                );
                i += 5;
                /* Fall through */
            case 201: /* Reception Report(s) */
                while (RC--)
                {
                    cw_manager_event(EVENT_FLAG_CALL, "RTCP-RR",
                        8,
                        cw_msg_tuple("Channel",    "%s",     chan->name),
                        cw_msg_tuple("Uniqueid",   "%s",     chan->uniqueid),
                        cw_msg_tuple("Loss rate",  "%u/256", ntohl(rtcpdata[i+1]) >> 24),
                        cw_msg_tuple("Loss count", "%u",     ntohl(rtcpdata[i+1]) & 0x00ffffff),
                        cw_msg_tuple("Extseq",     "0x%x",   ntohl(rtcpdata[i+2])),
                        cw_msg_tuple("Jitter",     "%u",     ntohl(rtcpdata[i+3])),
                        cw_msg_tuple("LSR",        "%u",     ntohl(rtcpdata[i+4])),
                        cw_msg_tuple("DLSR",       "%u",     ntohl(rtcpdata[i+5]))
                    );
                    i += 6;
                }
                if (i <= pkt + l && (rtpdebug || rtp_debug_test_addr(&rtp->sock_info[1].peer.sa)))
                    cw_log(CW_LOG_DEBUG, "RTCP SR/RR has %u words of profile-specific extension (ignored)\n", pkt+l+1 - i);
                break;
        }

next:
        pkt += length + 1;
    }

    return &cw_null_frame;
}


static void cw_rtp_senddigit_continue(struct cw_rtp *rtp, const struct cw_frame *f)
{
	uint32_t pkt[4];

	rtp->dtmfmute = cw_tvadd(cw_tvnow(), cw_tv(0, 500000));

	if (!rtp->sendevent) {
		pkt[0] = rtp->sendevent_rtphdr;
	} else {
		/* Start of a new event */
		rtp->sendevent_rtphdr = htonl((2 << 30) | (cw_rtp_lookup_code(rtp, 0, CW_RTP_DTMF) << 16));
		rtp->sendevent_startts = rtp->lastts;
		rtp->sendevent_duration = (uint16_t)((unsigned int)(rtp->sendevent & 0xffff) * f->samplerate / 1000);
#ifdef INCREMENTAL_RFC2833_EVENTS
		rtp->sendevent_payload = (rtp->sendevent & ~0xffff);
#else
		rtp->sendevent_payload = (rtp->sendevent & ~0xffff) | rtp->sendevent_duration;
#endif
		rtp->sendevent = 0;
		pkt[0] = rtp->sendevent_rtphdr | htonl(1 << 23);
	}

	pkt[1] = htonl(rtp->sendevent_startts);
	pkt[2] = htonl(rtp->ssrc);

	/* DTMF tone duration is fixed at 100ms since we don't have
	 * end signalling from higher levels.
	 * N.B. This limit is the only thing that prevents us having
	 * to deal with a duration > 0xffff (see RFC4733)
	 * N.B. The DTMF tone generator in the core gives 100ms tone
	 * followed by 100ms silence. Since it is also used to generate
	 * silence to clock out RFC2833 there is a limit of 200ms less
	 * 3 packets for tone, beyond which the RTP sequence may become
	 * badly disjoint for channels that are not currently bridged
	 * to any other audio source.
	 * BT SIN 351 (http://www.sinet.bt.com/) specifies a minimum
	 * requirement of 40ms tone with at least 40ms between tones.
	 * ETR 206 specifies a minimum requirement of 70ms +- 5ms
	 * tone with at least 65ms between tones.
	 * I believe Bell Systems specifies 50ms tone and NZ wants
	 * 60ms tone / 60ms gap. CCITT Blue Book, Rec. Q.23 may be
	 * 60/60ms as well?
	 * RFC2833 suggests 40/93ms as the minimums in their survey.
	 * That's the nice thing about standards :-)
	 */
	if (rtp->sendevent_payload & (1 << 22)) {
		/* Third end packet */
		rtp->sendevent_payload &= ~(1 << 22);
		pkt[3] = htonl(rtp->sendevent_payload);
		rtp->sendevent_payload = 0;
	} else if (rtp->sendevent_payload & (1 << 23)) {
		/* Second end packet */
		pkt[3] = htonl(rtp->sendevent_payload);
		rtp->sendevent_payload |= 1 << 22;
	} else {
		/* Sonus RTP handling is seriously broken.
		 * It requires all RTP packets to have different timestamps, no more
		 * than 100ms between packets, doesn't handle discontinuities in RTP
		 * streams, is confused by overlapping events and audio...
		 * So in Sonus bug mode we suppress audio whenever we are sending
		 * event packets and send the event packets with increasing (i.e.
		 * broken) timestamps.
		 */
		if (!rtp->bug_sonus) {
#ifdef INCREMENTAL_RFC2833_EVENTS
			rtp->sendevent_payload = (rtp->sendevent_payload & ~0xffff) | (rtp->lastts - rtp->sendevent_startts + f->samples);
#endif
		} else {
#ifdef INCREMENTAL_RFC2833_EVENTS
			rtp->sendevent_payload = (rtp->sendevent_payload & ~0xffff) | f->samples;
#else
			rtp->sendevent_payload = (rtp->sendevent_payload & ~0xffff) | (rtp->sendevent_duration - (rtp->lastts - rtp->sendevent_startts));
#endif
			pkt[1] = htonl(rtp->lastts);
		}

		rtp->sendevent_seqno = rtp->seqno++;
		if (rtp->lastts - rtp->sendevent_startts + f->samples >= rtp->sendevent_duration) {
			/* First end packet */
			rtp->sendevent_payload |= 1 << 23;
			if (rtp->bug_sonus)
				rtp->sendevent_startts = rtp->lastts;
		}
		pkt[3] = htonl(rtp->sendevent_payload);
	}

	pkt[0] |= htonl(rtp->sendevent_seqno);

	if (rtp_sendto(rtp, (void *)pkt, sizeof(pkt), 0) < 0)
		cw_log(CW_LOG_ERROR, "RTP Transmission error to %l@: %s\n", cw_rtp_get_peer(rtp), strerror(errno));

	if (rtp_debug_test_addr(cw_rtp_get_peer(rtp))) {
		cw_verbose("Sent RTP packet to %l@ (type %u, seq %hu, ts %u, len 4) - DTMF payload 0x%08x duration %u (%ums)\n",
			cw_rtp_get_peer(rtp),
			(rtp->sendevent_rtphdr & !(2 << 30)),
			rtp->sendevent_seqno,
			ntohl(pkt[1]),
			ntohl(pkt[3]),
			ntohl(pkt[3]) & 0xffff,
			1000 * (ntohl(pkt[3]) & 0xffff) / f->samplerate);
	}
}


int cw_rtp_sendevent(struct cw_rtp * const rtp, char event, uint16_t duration)
{
	static const char *eventcodes = "0123456789*#ABCDX";
	char *p;

	if (!(p = strchr(eventcodes, toupper(event)))) {
		cw_log(CW_LOG_WARNING, "Don't know how to represent '%c'\n", event);
		return -1;
	}

	if (rtp->sendevent_payload)
		cw_log(CW_LOG_WARNING, "RFC2833 DTMF overrrun, '%c' incomplete when starting '%c'\n", eventcodes[rtp->sendevent_payload >> 24], event);
	else if (rtp->sendevent)
		cw_log(CW_LOG_ERROR, "RFC2833 DTMF overrrun, '%c' never started before starting '%c'\n", eventcodes[rtp->sendevent >> 24], event);

	rtp->sendevent = ((p - eventcodes) << 24) | (0xa << 16) | duration;
	return 0;
}


static void calc_rxstamp(struct timeval *tv, struct cw_rtp *rtp, unsigned int timestamp, int mark)
{
    struct timeval ts = cw_samp2tv(timestamp, 8000);

    if (cw_tvzero(rtp->rxcore)  ||  mark)
    {
        rtp->rxcore = cw_tvsub(cw_tvnow(), ts);
        /* Round to 20ms for nice, pretty timestamps */
        rtp->rxcore.tv_usec -= rtp->rxcore.tv_usec % 20000;
    }
    *tv = cw_tvadd(rtp->rxcore, ts);
}

struct cw_frame *cw_rtp_read(struct cw_rtp *rtp)
{
    struct rtpPayloadType rtpPT;
    struct cw_frame *f;
    uint32_t *rtpheader;
    int res;
    int payloadtype;
    int hdrlen = 3*sizeof(uint32_t);
    int mark;
    int actions;
    uint32_t seqno;
    uint32_t csrc_count;
    uint32_t timestamp;

    /* Cache where the header will go */
    res = rtp_recv(rtp, rtp->rawdata + CW_FRIENDLY_OFFSET, sizeof(rtp->rawdata) - CW_FRIENDLY_OFFSET, 0, &actions);

    if (res < 0)
    {
        if (errno != EAGAIN)
            cw_log(CW_LOG_WARNING, "RTP read error: %s\n", strerror(errno));
        return &cw_null_frame;
    }

    if (res < 3*sizeof(uint32_t))
    {
        /* Too short for an RTP packet. */
        cw_log(CW_LOG_DEBUG, "RTP Read too short\n");
        return &cw_null_frame;
    }

    /* Ignore if the other side hasn't been given an address yet. */
    if (cw_rtp_get_peer(rtp)->sa_family == AF_UNSPEC)
        return &cw_null_frame;

    if (rtp->nat)
    {
        if (actions & 1)
        {
            /* The other side changed */
            rtp->rxseqno = 0;
            rtp->nat_state = NAT_STATE_ACTIVE;
            if (option_debug  ||  rtpdebug)
                cw_log(CW_LOG_DEBUG, "RTP NAT: Got audio from other end. Now sending to address %l@\n", cw_rtp_get_peer(rtp));
        }
    }

    /* Get fields */
    rtpheader = (uint32_t *)(rtp->rawdata + CW_FRIENDLY_OFFSET);
    seqno = ntohl(rtpheader[0]);

    /* Check RTP version */
    if ((seqno & 0xC0000000) >> 30 != 2)
        return &cw_null_frame;
    
    if ((seqno & (1 << 29)))
    {
        /* There are some padding bytes. Remove them. */
        res -= rtp->rawdata[CW_FRIENDLY_OFFSET + res - 1];
    }
    if ((csrc_count = (seqno >> 24) & 0x0F))
    {
        /* Allow for the contributing sources, but don't attempt to
           process them. */
        hdrlen += (csrc_count << 2);
        if (res < hdrlen)
        {
            /* Too short for an RTP packet. */
            return &cw_null_frame;
        }
    }
    if ((seqno & (1 << 28)))
    {
        /* RTP extension present. Skip over it. */
        hdrlen += sizeof(uint32_t);
        if (res >= hdrlen)
            hdrlen += ((ntohl(rtpheader[hdrlen >> 2]) & 0xFFFF)*sizeof(uint32_t));
        if (res < hdrlen)
        {
            cw_log(CW_LOG_DEBUG, "RTP Read too short (%d, expecting %d)\n", res, hdrlen);
            return &cw_null_frame;
        }
    }
    mark = (seqno >> 23) & 1;
    payloadtype = (seqno >> 16) & 0x7F;
    seqno &= 0xFFFF;
    timestamp = ntohl(rtpheader[1]);

    if (rtp_debug_test_addr(cw_rtp_get_peer(rtp)))
    {
        cw_verbose("Got RTP packet from %l@ (type %d, seq %u, ts %u, len %d)\n",
                     cw_rtp_get_peer(rtp),
                     payloadtype,
                     seqno,
                     timestamp,
                     res - hdrlen);
    }

    rtpPT = cw_rtp_lookup_pt(rtp, payloadtype);
    if (!rtpPT.is_cw_format)
    {
        /* This is special in-band data that's not one of our codecs */
        if (rtpPT.code == CW_RTP_DTMF)
        {
            /* It's special -- rfc2833 process it */
            if (rtp_debug_test_addr(cw_rtp_get_peer(rtp)))
            {
                unsigned char *data;
                unsigned int event;
                unsigned int event_end;
                unsigned int duration;

                data = rtp->rawdata + CW_FRIENDLY_OFFSET + hdrlen;
                event = ntohl(*((unsigned int *) (data)));
                event >>= 24;
                event_end = ntohl(*((unsigned int *) (data)));
                event_end <<= 8;
                event_end >>= 24;
                duration = ntohl(*((unsigned int *) (data)));
                duration &= 0xFFFF;
                cw_verbose("Got rfc2833 RTP packet from %l@ (type %d, seq %u, ts %u, len %d, mark %d, event %08x, end %d, duration %u)\n",
                    cw_rtp_get_peer(rtp),
                    payloadtype, seqno, timestamp, res - hdrlen, (mark?1:0), event, ((event_end & 0x80)?1:0), duration);
            }
            f = process_rfc2833(rtp, rtp->rawdata + CW_FRIENDLY_OFFSET + hdrlen, res - hdrlen, seqno, timestamp);
            if (f) 
                return f; 
            return &cw_null_frame;
        }
        else if (rtpPT.code == CW_RTP_CISCO_DTMF)
        {
            /* It's really special -- process it the Cisco way */
            if (rtp->lastevent_seqno <= seqno  ||  rtp->lastevent_code == 0  ||  (rtp->lastevent_seqno >= 65530  &&  seqno <= 6))
            {
                f = process_cisco_dtmf(rtp, rtp->rawdata + CW_FRIENDLY_OFFSET + hdrlen);
                rtp->lastevent_seqno = seqno;
            }
            else 
                f = NULL;
            if (f) 
                return f; 
            else 
                return &cw_null_frame;
        }
        else if (rtpPT.code == CW_RTP_CN)
        {
            /* Comfort Noise */
            f = process_rfc3389(rtp, rtp->rawdata + CW_FRIENDLY_OFFSET + hdrlen, res - hdrlen);
            if (f) 
                return f; 
            else 
                return &cw_null_frame;
        }
        else
        {
            /* Counter-path Eyebeam/X-Lite seems to send periodic 126s with a 4 byte payload
             * even though it doesn't negotiate this codec. No one seems to know what it is :-(
             */
            if (payloadtype != 126 || res - hdrlen != 4)
                cw_log(CW_LOG_NOTICE, "Unknown RTP codec %d received, payload length %d\n", payloadtype, res - hdrlen);
            return &cw_null_frame;
        }
    }
    rtp->f.subclass = rtpPT.code;
    if (rtp->f.subclass < CW_FORMAT_MAX_AUDIO)
        rtp->f.frametype = CW_FRAME_VOICE;
    else
        rtp->f.frametype = CW_FRAME_VIDEO;
    rtp->lastrxformat = rtp->f.subclass;

    if (!rtp->lastrxts)
        rtp->lastrxts = timestamp;

    rtp->rxseqno = seqno;

    /* Send any pending DTMF */
    if (rtp->lastevent_code && timestamp - rtp->lastevent_startts > dtmftimeout)
    {
        /* Since we can't return more than one frame we have
	 * to drop the received data in order to send the
	 * DTMF event up the stack. This is an "unfortunate"
	 * flaw in the design.
	 */
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "Sending pending DTMF after max duration reached\n");
        return send_dtmf(rtp);
    }

    rtp->lastrxts = timestamp;

    rtp->f.mallocd = 0;
    rtp->f.datalen = res - hdrlen;
    rtp->f.data = rtp->rawdata + hdrlen + CW_FRIENDLY_OFFSET;
    rtp->f.offset = hdrlen + CW_FRIENDLY_OFFSET;
    if (rtp->f.subclass < CW_FORMAT_MAX_AUDIO)
    {
        rtp->f.samples = cw_codec_get_samples(&rtp->f);
        if (rtp->f.subclass == CW_FORMAT_SLINEAR) 
            cw_frame_byteswap_be(&rtp->f);
        calc_rxstamp(&rtp->f.delivery, rtp, timestamp, mark);

        /* Add timing data to let cw_generic_bridge() put the frame
         * into a jitterbuf */
        rtp->f.has_timing_info = 1;
        rtp->f.ts = timestamp / 8;
        rtp->f.duration = rtp->f.samples / 8;
    }
    else
    {
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
    rtp->f.seq_no = seqno;
	rtp->f.tx_copies = 0;
    return &rtp->f;
}

/* The following array defines the MIME Media type (and subtype) for each
   of our codecs, or RTP-specific data type. */
static struct
{
  struct rtpPayloadType payloadType;
  const char *type;
  const char *subtype;
} mimeTypes[] =
{
    {{1, CW_FORMAT_G723_1}, "audio", "G723"},
    {{1, CW_FORMAT_GSM}, "audio", "GSM"},
    {{1, CW_FORMAT_ULAW}, "audio", "PCMU"},
    {{1, CW_FORMAT_ALAW}, "audio", "PCMA"},
    {{1, CW_FORMAT_G726}, "audio", "G726-32"},
    {{1, CW_FORMAT_DVI_ADPCM}, "audio", "DVI4"},
    {{1, CW_FORMAT_SLINEAR}, "audio", "L16"},
    {{1, CW_FORMAT_LPC10}, "audio", "LPC"},
    {{1, CW_FORMAT_G729A}, "audio", "G729"},
    {{1, CW_FORMAT_SPEEX}, "audio", "speex"},
    {{1, CW_FORMAT_ILBC}, "audio", "iLBC"},
    {{0, CW_RTP_DTMF}, "audio", "telephone-event"},
    {{0, CW_RTP_CISCO_DTMF}, "audio", "cisco-telephone-event"},
    {{0, CW_RTP_CN}, "audio", "CN"},
    {{1, CW_FORMAT_JPEG}, "video", "JPEG"},
    {{1, CW_FORMAT_PNG}, "video", "PNG"},
    {{1, CW_FORMAT_H261}, "video", "H261"},
    {{1, CW_FORMAT_H263}, "video", "H263"},
    {{1, CW_FORMAT_H263_PLUS}, "video", "h263-1998"},
    {{1, CW_FORMAT_H264}, "video", "H264"},
};

/* Static (i.e., well-known) RTP payload types for our "CW_FORMAT..."s:
   also, our own choices for dynamic payload types.  This is our master
   table for transmission */
static struct rtpPayloadType static_RTP_PT[MAX_RTP_PT] =
{
    [0] = {1, CW_FORMAT_ULAW},
#ifdef USE_DEPRECATED_G726
    [2] = {1, CW_FORMAT_G726}, /* Technically this is G.721, but if Cisco can do it, so can we... */
#endif
    [3] = {1, CW_FORMAT_GSM},
    [4] = {1, CW_FORMAT_G723_1},
    [5] = {1, CW_FORMAT_DVI_ADPCM}, /* 8 kHz */
    [6] = {1, CW_FORMAT_DVI_ADPCM}, /* 16 kHz */
    [7] = {1, CW_FORMAT_LPC10},
    [8] = {1, CW_FORMAT_ALAW},
    [10] = {1, CW_FORMAT_SLINEAR}, /* 2 channels */
    [11] = {1, CW_FORMAT_SLINEAR}, /* 1 channel */
    [13] = {0, CW_RTP_CN},
    [16] = {1, CW_FORMAT_DVI_ADPCM}, /* 11.025 kHz */
    [17] = {1, CW_FORMAT_DVI_ADPCM}, /* 22.050 kHz */
    [18] = {1, CW_FORMAT_G729A},
    [19] = {0, CW_RTP_CN},        /* Also used for CN */
    [26] = {1, CW_FORMAT_JPEG},
    [31] = {1, CW_FORMAT_H261},
    [34] = {1, CW_FORMAT_H263},
    [103] = {1, CW_FORMAT_H263_PLUS},
    [97] = {1, CW_FORMAT_ILBC},
    [99] = {1, CW_FORMAT_H264},
    [101] = {0, CW_RTP_DTMF},
    [110] = {1, CW_FORMAT_SPEEX},
    [111] = {1, CW_FORMAT_G726},
    [121] = {0, CW_RTP_CISCO_DTMF}, /* Must be type 121 */
    /* 122 used by T38 RTP */
};

void cw_rtp_pt_clear(struct cw_rtp* rtp) 
{
    int i;

    if (!rtp)
        return;
    for (i = 0;  i < MAX_RTP_PT;  ++i)
    {
        rtp->current_RTP_PT[i].is_cw_format = 0;
        rtp->current_RTP_PT[i].code = 0;
    }

    rtp->rtp_lookup_code_cache_is_cw_format = 0;
    rtp->rtp_lookup_code_cache_code = 0;
    rtp->rtp_lookup_code_cache_result = 0;
}

void cw_rtp_pt_default(struct cw_rtp* rtp) 
{
    int i;

    /* Initialize to default payload types */
    for (i = 0;  i < MAX_RTP_PT;  ++i)
    {
        rtp->current_RTP_PT[i].is_cw_format = static_RTP_PT[i].is_cw_format;
        rtp->current_RTP_PT[i].code = static_RTP_PT[i].code;
    }

    rtp->rtp_lookup_code_cache_is_cw_format = 0;
    rtp->rtp_lookup_code_cache_code = 0;
    rtp->rtp_lookup_code_cache_result = 0;
}

/* Make a note of a RTP paymoad type that was seen in a SDP "m=" line. */
/* By default, use the well-known value for this type (although it may */
/* still be set to a different value by a subsequent "a=rtpmap:" line): */
void cw_rtp_set_m_type(struct cw_rtp* rtp, int pt)
{
    if (pt < 0  ||  pt > MAX_RTP_PT) 
        return; /* bogus payload type */

    if (static_RTP_PT[pt].code != 0)
        rtp->current_RTP_PT[pt] = static_RTP_PT[pt];
} 

/* Make a note of a RTP payload type (with MIME type) that was seen in */
/* a SDP "a=rtpmap:" line. */
void cw_rtp_set_rtpmap_type(struct cw_rtp *rtp, int pt, const char *mimeType, const char *mimeSubtype)
{
    int i;

    if (pt < 0  ||  pt > MAX_RTP_PT) 
        return; /* bogus payload type */

    for (i = 0;  i < sizeof mimeTypes/sizeof mimeTypes[0];  i++)
    {
        if (strcasecmp(mimeSubtype, mimeTypes[i].subtype) == 0
            &&
            strcasecmp(mimeType, mimeTypes[i].type) == 0)
        {
            rtp->current_RTP_PT[pt] = mimeTypes[i].payloadType;
            return;
        }
    }
} 

/* Return the union of all of the codecs that were set by rtp_set...() calls */
/* They're returned as two distinct sets: CW_FORMATs, and CW_RTPs */
void cw_rtp_get_current_formats(struct cw_rtp *rtp,
                                  int *cw_formats,
                                  int *non_cw_formats)
{
    int pt;

    *cw_formats =
    *non_cw_formats = 0;
    for (pt = 0;  pt < MAX_RTP_PT;  ++pt)
    {
        if (rtp->current_RTP_PT[pt].is_cw_format)
            *cw_formats |= rtp->current_RTP_PT[pt].code;
        else
            *non_cw_formats |= rtp->current_RTP_PT[pt].code;
    }
}

void cw_rtp_offered_from_local(struct cw_rtp* rtp, int local)
{
    if (rtp)
        rtp->rtp_offered_from_local = local;
    else
        cw_log(CW_LOG_WARNING, "rtp structure is null\n");
}

struct rtpPayloadType cw_rtp_lookup_pt(struct cw_rtp* rtp, int pt) 
{
    struct rtpPayloadType result;

    result.is_cw_format =
    result.code = 0;
    if (pt < 0  ||  pt > MAX_RTP_PT) 
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
int cw_rtp_lookup_code(struct cw_rtp* rtp, const int is_cw_format, const int code)
{
    int pt;

    if (is_cw_format == rtp->rtp_lookup_code_cache_is_cw_format
        &&
        code == rtp->rtp_lookup_code_cache_code)
    {
        /* Use our cached mapping, to avoid the overhead of the loop below */
        return rtp->rtp_lookup_code_cache_result;
    }

    /* Check the dynamic list first */
    for (pt = 0;  pt < MAX_RTP_PT;  ++pt)
    {
        if (rtp->current_RTP_PT[pt].code == code  &&  rtp->current_RTP_PT[pt].is_cw_format == is_cw_format)
        {
            rtp->rtp_lookup_code_cache_is_cw_format = is_cw_format;
            rtp->rtp_lookup_code_cache_code = code;
            rtp->rtp_lookup_code_cache_result = pt;
            return pt;
        }
    }

    /* Then the static list */
    for (pt = 0;  pt < MAX_RTP_PT;  ++pt)
    {
        if (static_RTP_PT[pt].code == code  &&  static_RTP_PT[pt].is_cw_format == is_cw_format)
        {
            rtp->rtp_lookup_code_cache_is_cw_format = is_cw_format;
              rtp->rtp_lookup_code_cache_code = code;
            rtp->rtp_lookup_code_cache_result = pt;
            return pt;
        }
    }
    return -1;
}

const char *cw_rtp_lookup_mime_subtype(const int is_cw_format, const int code)
{
    int i;

    for (i = 0;  i < sizeof mimeTypes/sizeof mimeTypes[0];  ++i)
    {
        if (mimeTypes[i].payloadType.code == code  &&  mimeTypes[i].payloadType.is_cw_format == is_cw_format)
            return mimeTypes[i].subtype;
    }
    return "";
}

void cw_rtp_lookup_mime_multiple(struct cw_dynstr *result, const int capability, const int is_cw_format)
{
    size_t mark;
    int format;

    cw_dynstr_printf(result, "0x%x (", capability);

    mark = result->used;

    for (format = 1;  format < CW_RTP_MAX;  format <<= 1)
    {
        if (capability & format)
        {
            const char *name = cw_rtp_lookup_mime_subtype(is_cw_format, format);
            cw_dynstr_printf(result, "%s|", name);
        }
    }

    cw_dynstr_printf(result, (result->used != mark ? ")" : "nothing)"));
}

struct cw_rtp *cw_rtp_new_with_bindaddr(struct cw_sockaddr_net *addr)
{
	struct cw_rtp *rtp = NULL;

	if ((rtp = calloc(1, sizeof(*rtp)))) {
		if (!udp_socket_group_create_and_bind(rtp->sock_info, arraysize(rtp->sock_info), nochecksums, addr, rtpstart, rtpend)) {
			rtp->ssrc = cw_random();
			rtp->seqno = (uint16_t)(cw_random() & 0xFFFF);
			cw_rtp_pt_default(rtp);
		} else {
			free(rtp);
			rtp = NULL;
		}
	} else
		cw_log(CW_LOG_ERROR, "Out of memory\n");

	return rtp;
}

int cw_rtp_settos(struct cw_rtp *rtp, int tos)
{
    return udp_socket_set_tos(&rtp->sock_info[0], tos);
}

void cw_rtp_set_peer(struct cw_rtp *rtp, struct sockaddr *them)
{
    struct cw_sockaddr_net addr;

    udp_socket_set_far(&rtp->sock_info[0], them);

    /* We need to cook up the RTCP address */
    cw_sockaddr_copy(&addr.sa, them);
    cw_sockaddr_set_port(&addr.sa, cw_sockaddr_get_port(them) + 1);
    udp_socket_set_far(&rtp->sock_info[1], &addr.sa);

    rtp->rxseqno = 0;
}

struct sockaddr *cw_rtp_get_us(struct cw_rtp *rtp)
{
    return udp_socket_get_apparent_local(&rtp->sock_info[0]);
}


void cw_rtp_stop(struct cw_rtp *rtp)
{
    udp_socket_restart(&rtp->sock_info[0]);
    udp_socket_restart(&rtp->sock_info[1]);
}

void cw_rtp_reset(struct cw_rtp *rtp)
{
    memset(&rtp->rxcore, 0, sizeof(rtp->rxcore));
    memset(&rtp->txcore, 0, sizeof(rtp->txcore));
    memset(&rtp->dtmfmute, 0, sizeof(rtp->dtmfmute));
    rtp->lastts = 0;
    rtp->sendevent_rtphdr = 0;
    rtp->lastrxts = 0;
    rtp->lastividtimestamp = 0;
    rtp->lastovidtimestamp = 0;
    rtp->lastevent_code = 0;
    rtp->lastevent_duration = 0;
    rtp->lastevent_seqno = 0;
    rtp->lasttxformat = 0;
    rtp->lastrxformat = 0;
    rtp->seqno = 0;
    rtp->rxseqno = 0;
}

void cw_rtp_destroy(struct cw_rtp *rtp)
{
    int i;

    if (rtp->smoother)
        cw_smoother_free(rtp->smoother);
#ifdef ENABLE_SRTP
    if (g_srtp_res && rtp->srtp) {
        g_srtp_res->destroy(rtp->srtp);
        rtp->srtp = NULL;
    }
#endif

    for (i = 0; i < arraysize(rtp->sock_info); i++)
        close(rtp->sock_info[i].fd);

    free(rtp);
}

static uint32_t calc_txstamp(struct cw_rtp *rtp, struct timeval *delivery)
{
    struct timeval t;
    long int ms;
    
    if (cw_tvzero(rtp->txcore))
    {
        rtp->txcore = cw_tvnow();
        /* Round to 20ms for nice, pretty timestamps */
        rtp->txcore.tv_usec -= rtp->txcore.tv_usec % 20000;
    }
    /* Use previous txcore if available */
    t = (delivery  &&  !cw_tvzero(*delivery))  ?  *delivery  :  cw_tvnow();
    ms = cw_tvdiff_ms(t, rtp->txcore);
    /* Use what we just got for next time */
    rtp->txcore = t;
    return (uint32_t) ms;
}

int cw_rtp_sendcng(struct cw_rtp *rtp, int level)
{
    char data[256];
    const struct sockaddr *them;
    uint32_t *rtpheader;
    int hdrlen = 12;
    int payload;
    int res;

    them = cw_rtp_get_peer(rtp);

    if (them->sa_family != AF_UNSPEC) {
        level = 127 - (level & 0x7F);
        payload = cw_rtp_lookup_code(rtp, 0, CW_RTP_CN);

        rtp->dtmfmute = cw_tvadd(cw_tvnow(), cw_tv(0, 500000));

        /* Get a pointer to the header */
        rtpheader = (uint32_t *) data;
        rtpheader[0] = htonl((2 << 30) | (1 << 23) | (payload << 16) | (rtp->seqno++));
        rtpheader[1] = htonl(rtp->lastts);
        rtpheader[2] = htonl(rtp->ssrc);
        data[12] = (char)level;

        res = rtp_sendto(rtp, (void *) rtpheader, hdrlen + 1, 0);
        if (res >= 0) {
	    if (rtp_debug_test_addr(them)) {
                cw_verbose("Sent Comfort Noise RTP packet to %l@ (type %d, seq %hu, ts %u, len %d)\n",
                             them,
                             payload,
                             rtp->seqno,
                             rtp->lastts,
                             res - hdrlen);
            }
	} else
            cw_log(CW_LOG_ERROR, "RTP Comfort Noise Transmission error to %l@: %s\n", them, strerror(errno));
    }
    return 0;
}

static int cw_rtp_raw_write(struct cw_rtp *rtp, struct cw_frame *f, int codec)
{
    const struct sockaddr *them;
    unsigned char *rtpheader;
    int hdrlen = 12;
    int res;
    int ms;
    int pred;
    int mark = 0;

    ms = calc_txstamp(rtp, &f->delivery);
    /* Default prediction */
    if (f->subclass < CW_FORMAT_MAX_AUDIO)
    {
        pred = rtp->lastts + f->samples;

        /* Re-calculate last TS */
        rtp->lastts = rtp->lastts + ms*8;
        if (cw_tvzero(f->delivery))
        {
            /* If this isn't an absolute delivery time, Check if it is close to our prediction, 
               and if so, go with our prediction */
            if (abs(rtp->lastts - pred) < MAX_TIMESTAMP_SKEW)
            {
                rtp->lastts = pred;
            }
            else
            {
                if (option_debug > 2)
                    cw_log(CW_LOG_DEBUG, "Difference is %d, ms is %d\n", abs(rtp->lastts - pred), ms);
                mark = 1;
            }
        }
    }
    else
    {
        mark = f->subclass & 0x1;
        pred = rtp->lastovidtimestamp + f->samples;
        /* Re-calculate last TS */
        rtp->lastts = rtp->lastts + ms * 90;
        /* If it's close to our prediction, go for it */
        if (cw_tvzero(f->delivery))
        {
            if (abs(rtp->lastts - pred) < 7200)
            {
                rtp->lastts = pred;
                rtp->lastovidtimestamp += f->samples;
            }
            else
            {
                if (option_debug > 2)
                    cw_log(CW_LOG_DEBUG, "Difference is %d, ms is %d (%d), pred/ts/samples %d/%u/%d\n", abs(rtp->lastts - pred), ms, ms * 90, pred, rtp->lastts, f->samples);
                rtp->lastovidtimestamp = rtp->lastts;
            }
        }
    }

    /* Take the timestamp from the callweaver frame if it has timing */
    if (f->has_timing_info)
        rtp->lastts = f->ts*8;

    them = cw_rtp_get_peer(rtp);

    if (them->sa_family != AF_UNSPEC)
    {
        int in_event = 0;

        /* If we're in the process of sending a DTMF digit let the
         * receiver know that the event is on-going and covers
         * the audio packet that follows.
         */
        if (rtp->sendevent_payload || rtp->sendevent)
        {
            in_event = !(rtp->sendevent_payload & (1 << 23));
            cw_rtp_senddigit_continue(rtp, f);
        }

         /* Sonus RTP handling is seriously broken.
          * It requires all RTP packets to have different timestamps, no more
          * than 100ms between packets, doesn't handle discontinuities in RTP
          * streams, is confused by overlapping events and audio...
          * So in Sonus bug mode we suppress audio whenever we are sending
          * event packets and send the event packets with increasing (i.e.
          * broken) timestamps.
          */
        if (!in_event || !rtp->bug_sonus)
        {
            /* Get a pointer to the header */
            rtpheader = (uint8_t *)f->data - hdrlen;

            put_unaligned_uint32(rtpheader, htonl((2 << 30) | (codec << 16) | (rtp->seqno) | (mark << 23)));
            put_unaligned_uint32(rtpheader + 4, htonl(rtp->lastts));
            put_unaligned_uint32(rtpheader + 8, htonl(rtp->ssrc));

            if ((res = rtp_sendto(rtp, (void *) rtpheader, f->datalen + hdrlen, 0)) < 0)
            {
                if (!rtp->nat || rtp->nat_state == NAT_STATE_ACTIVE)
                {
                    cw_log(CW_LOG_WARNING, "RTP Transmission error of packet %d to %l@: %s\n", rtp->seqno, them, strerror(errno));
                }
                else if (rtp->nat_state == NAT_STATE_INACTIVE || rtpdebug)
                {
                    /* Only give this error message once if we are not RTP debugging */
                    if (option_debug  ||  rtpdebug)
                        cw_log(CW_LOG_DEBUG, "RTP NAT: Can't write RTP to private address %l@, waiting for other end to send audio...\n", them);
                    rtp->nat_state = NAT_STATE_INACTIVE_NOWARN;
                }
            }

            if (rtp_debug_test_addr(them))
            {
                cw_verbose("Sent RTP packet to %l@ (type %d, seq %hu, ts %u, len %d)\n",
                             them,
                             codec,
                             rtp->seqno,
                             rtp->lastts,
                             res - hdrlen);
            }

            rtp->seqno++;
        }
    }
    return 0;
}

int cw_rtp_write(struct cw_rtp *rtp, struct cw_frame *_f)
{
    struct cw_frame *f;
    int codec;
    int hdrlen = 12;
    int subclass;

    /* If there is no data length, return immediately */
    if (!_f->datalen) 
        return 0;
    
    /* If we have no peer, return immediately */    
    if (cw_rtp_get_peer(rtp)->sa_family == AF_UNSPEC)
        return 0;

    /* Make sure we have enough space for RTP header */
    if ((_f->frametype != CW_FRAME_VOICE)  &&  (_f->frametype != CW_FRAME_VIDEO))
    {
        cw_log(CW_LOG_WARNING, "RTP can only send voice\n");
        return -1;
    }

    subclass = _f->subclass;
    if (_f->frametype == CW_FRAME_VIDEO)
        subclass &= ~0x1;

    if ((codec = cw_rtp_lookup_code(rtp, 1, subclass)) < 0)
    {
        cw_log(CW_LOG_WARNING, "Don't know how to send format %s packets with RTP\n", cw_getformatname(_f->subclass));
        return -1;
    }

    if (rtp->lasttxformat != subclass)
    {
        /* New format, reset the smoother */
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "Ooh, format changed from %s to %s\n", cw_getformatname(rtp->lasttxformat), cw_getformatname(subclass));
        rtp->lasttxformat = subclass;
        if (rtp->smoother)
            cw_smoother_free(rtp->smoother);
        rtp->smoother = NULL;
    }
    
    if (!rtp->smoother)
    {
        const struct rtp_codec_table *ent;
        int ms = rtp->framems;
        int len;

        if ((ent = lookup_rtp_smoother_codec(subclass, &rtp->framems, &len)))
        {
            if (rtp->framems != ms)
                cw_log(CW_LOG_DEBUG, "Had to change frame MS from %d to %d\n", ms, rtp->framems);
            if (!(rtp->smoother = cw_smoother_new(len)))
            {
                cw_log(CW_LOG_WARNING, "Unable to create smoother ms: %d len: %d:(\n", rtp->framems, len);
                return -1;
            }

            if (ent->flags)
                cw_smoother_set_flags(rtp->smoother, ent->flags);            
            cw_log(CW_LOG_DEBUG, "Able to create smoother :) ms: %d len %d\n", rtp->framems, len);
        }
    }

    if (rtp->smoother)
    {
        if (cw_smoother_test_flag(rtp->smoother, CW_SMOOTHER_FLAG_BE))
            cw_smoother_feed_be(rtp->smoother, _f);
        else
            cw_smoother_feed(rtp->smoother, _f);
        while ((f = cw_smoother_read(rtp->smoother)))
            cw_rtp_raw_write(rtp, f, codec);
    }
    else
    {
        /* Don't buffer outgoing frames; send them one-per-packet: */
        if (_f->offset < hdrlen)
        {
            if ((f = cw_frdup(_f)))
            {
                cw_rtp_raw_write(rtp, f, codec);
                cw_fr_free(f);
            }
        }
        else
        {
           cw_rtp_raw_write(rtp, _f, codec);
        }
    }
    return 0;
}

/*--- cw_rtp_proto_unregister: Unregister interface to channel driver */
void cw_rtp_proto_unregister(struct cw_rtp_protocol *proto)
{
    struct cw_rtp_protocol *cur;
    struct cw_rtp_protocol *prev;

    cur = protos;
    prev = NULL;
    while (cur)
    {
        if (cur == proto)
        {
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

/*--- cw_rtp_proto_register: Register interface to channel driver */
int cw_rtp_proto_register(struct cw_rtp_protocol *proto)
{
    struct cw_rtp_protocol *cur;

    cur = protos;
    while (cur)
    {
        if (cur->type == proto->type)
        {
            cw_log(CW_LOG_WARNING, "Tried to register same protocol '%s' twice\n", cur->type);
            return -1;
        }
        cur = cur->next;
    }
    proto->next = protos;
    protos = proto;
    return 0;
}

/*--- get_proto: Get channel driver interface structure */
static struct cw_rtp_protocol *get_proto(struct cw_channel *chan)
{
    struct cw_rtp_protocol *cur;

    cur = protos;
    while (cur)
    {
        if (cur->type == chan->type)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

/* cw_rtp_bridge: Bridge calls. If possible and allowed, initiate
   re-invite so the peers exchange media directly outside 
   of CallWeaver. */
enum cw_bridge_result cw_rtp_bridge(struct cw_channel *c0, struct cw_channel *c1, int flags, struct cw_frame **fo, struct cw_channel **rc, int timeoutms)
{
    struct cw_frame *f;
    struct cw_channel *who;
    struct cw_channel *cs[3];
    struct cw_rtp *p0;        /* Audio RTP Channels */
    struct cw_rtp *p1;        /* Audio RTP Channels */
    struct cw_rtp *vp0;        /* Video RTP channels */
    struct cw_rtp *vp1;        /* Video RTP channels */
    struct cw_rtp_protocol *pr0;
    struct cw_rtp_protocol *pr1;
    struct cw_sockaddr_net ac0;
    struct cw_sockaddr_net ac1;
    struct cw_sockaddr_net vac0;
    struct cw_sockaddr_net vac1;
    struct cw_sockaddr_net t0;
    struct cw_sockaddr_net t1;
    struct cw_sockaddr_net vt0;
    struct cw_sockaddr_net vt1;
    void *pvt0;
    void *pvt1;
    int codec0;
    int codec1;
    int oldcodec0;
    int oldcodec1;
    
    vt0.sa.sa_family = AF_UNSPEC;
    vt1.sa.sa_family = AF_UNSPEC;
    vac0.sa.sa_family = AF_UNSPEC;
    vac1.sa.sa_family = AF_UNSPEC;

    /* If we need DTMF, we can't do a native bridge */
    if ((flags & (CW_BRIDGE_DTMF_CHANNEL_0 | CW_BRIDGE_DTMF_CHANNEL_1)))
        return CW_BRIDGE_FAILED_NOWARN;

    /* Lock channels */
    cw_channel_lock(c0);
    while (cw_channel_trylock(c1))
    {
        cw_channel_unlock(c0);
        usleep(1);
        cw_channel_lock(c0);
    }

    /* Find channel driver interfaces */
    pr0 = get_proto(c0);
    pr1 = get_proto(c1);
    if (!pr0)
    {
        cw_log(CW_LOG_WARNING, "Can't find native functions for channel '%s'\n", c0->name);
        cw_channel_unlock(c0);
        cw_channel_unlock(c1);
        return CW_BRIDGE_FAILED;
    }
    if (!pr1)
    {
        cw_log(CW_LOG_WARNING, "Can't find native functions for channel '%s'\n", c1->name);
        cw_channel_unlock(c0);
        cw_channel_unlock(c1);
        return CW_BRIDGE_FAILED;
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
    if (!p0  ||  !p1)
    {
        /* Somebody doesn't want to play... */
        cw_channel_unlock(c0);
        cw_channel_unlock(c1);
        return CW_BRIDGE_FAILED_NOWARN;
    }

#ifdef ENABLE_SRTP
    if (p0->srtp || p1->srtp)
    {
        cw_log(CW_LOG_NOTICE, "Cannot native bridge in SRTP.\n");
        cw_channel_unlock(c0);
        cw_channel_unlock(c1);
        return CW_BRIDGE_FAILED_NOWARN;
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
    if (pr0->get_codec  &&  pr1->get_codec)
    {
        /* Hey, we can't do reinvite if both parties speak different codecs */
        if (!(codec0 & codec1))
        {
            if (option_debug)
                cw_log(CW_LOG_DEBUG, "Channel codec0 = %d is not codec1 = %d, cannot native bridge in RTP.\n", codec0, codec1);
            cw_channel_unlock(c0);
            cw_channel_unlock(c1);
            return CW_BRIDGE_FAILED_NOWARN;
        }
    }

    /* Ok, we should be able to redirect the media. Start with one channel */
    if (pr0->set_rtp_peer(c0, p1, vp1, codec1, (p1->nat_state != NAT_STATE_INACTIVE)))
    {
        cw_log(CW_LOG_WARNING, "Channel '%s' failed to talk to '%s'\n", c0->name, c1->name);
    }
    else
    {
        /* Store RTP peer */
        cw_sockaddr_copy(&ac1.sa, cw_rtp_get_peer(p1));
        if (vp1)
            cw_sockaddr_copy(&vac1.sa, cw_rtp_get_peer(vp1));
    }
    /* Then test the other channel */
    if (pr1->set_rtp_peer(c1, p0, vp0, codec0, (p0->nat_state != NAT_STATE_INACTIVE)))
    {
        cw_log(CW_LOG_WARNING, "Channel '%s' failed to talk back to '%s'\n", c1->name, c0->name);
    }
    else
    {
        /* Store RTP peer */
        cw_sockaddr_copy(&ac0.sa, cw_rtp_get_peer(p0));
        if (vp0)
            cw_sockaddr_copy(&vac0.sa, cw_rtp_get_peer(vp0));
    }
    cw_channel_unlock(c0);
    cw_channel_unlock(c1);
    /* External RTP Bridge up, now loop and see if something happes that force us to take the
       media back to CallWeaver */
    cs[0] = c0;
    cs[1] = c1;
    cs[2] = NULL;
    oldcodec0 = codec0;
    oldcodec1 = codec1;

    int res1, res2;

    for (;;)
    {
        res1 = cw_channel_get_t38_status(c0);
        res2 = cw_channel_get_t38_status(c1);
        if ( res1 != res2 )
            return CW_BRIDGE_RETRY;

        /* Check if something changed... */
        if ((c0->tech_pvt != pvt0)
            ||
            (c1->tech_pvt != pvt1)
            ||
            (c0->masq  ||  c0->masqr  ||  c1->masq  ||  c1->masqr))
        {
            cw_log(CW_LOG_DEBUG, "Oooh, something is weird, backing out\n");
            if (c0->tech_pvt == pvt0)
            {
                if (pr0->set_rtp_peer(c0, NULL, NULL, 0, 0)) 
                    cw_log(CW_LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c0->name);
            }
            if (c1->tech_pvt == pvt1)
            {
                if (pr1->set_rtp_peer(c1, NULL, NULL, 0, 0)) 
                    cw_log(CW_LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c1->name);
            }
            return CW_BRIDGE_RETRY;
        }
        /* Now check if they have changed address */
        t0.sa.sa_family = AF_UNSPEC;
        t1.sa.sa_family = AF_UNSPEC;
        vt0.sa.sa_family = AF_UNSPEC;
        vt1.sa.sa_family = AF_UNSPEC;
        codec0 = oldcodec0;
        codec1 = oldcodec1;
        cw_sockaddr_copy(&t0.sa, cw_rtp_get_peer(p0));
        cw_sockaddr_copy(&t1.sa, cw_rtp_get_peer(p1));
        if (pr0->get_codec)
            codec0 = pr0->get_codec(c0);
        if (pr1->get_codec)
            codec1 = pr1->get_codec(c1);
        if (vp0)
            cw_sockaddr_copy(&vt0.sa, cw_rtp_get_peer(vp0));
        if (vp1)
            cw_sockaddr_copy(&vt1.sa, cw_rtp_get_peer(vp1));
        if (cw_sockaddr_cmp(&t1.sa, &ac1.sa, -1, 1) || (vp1 && cw_sockaddr_cmp(&vt1.sa, &vac1.sa, -1, 1))  ||  (codec1 != oldcodec1))
        {
            if (option_debug > 1)
            {
                cw_log(CW_LOG_DEBUG, "Oooh, '%s' changed end address to %l@ (format %d)\n", c1->name, &t1.sa, codec1);
                cw_log(CW_LOG_DEBUG, "Oooh, '%s' changed end vaddress to %l@ (format %d)\n", c1->name, &vt1.sa, codec1);
                cw_log(CW_LOG_DEBUG, "Oooh, '%s' was %l@/(format %d)\n", c1->name, &ac1.sa, oldcodec1);
                cw_log(CW_LOG_DEBUG, "Oooh, '%s' was %l@/(format %d)\n", c1->name, &vac1.sa, oldcodec1);
            }
            if (pr0->set_rtp_peer(c0, (t1.sa.sa_family != AF_UNSPEC ? p1 : NULL), (vt1.sa.sa_family != AF_UNSPEC ? vp1 : NULL), codec1, (p1->nat_state != NAT_STATE_INACTIVE)))
                cw_log(CW_LOG_WARNING, "Channel '%s' failed to update to '%s'\n", c0->name, c1->name);
            cw_sockaddr_copy(&ac1.sa, &t1.sa);
            cw_sockaddr_copy(&vac1.sa, &vt1.sa);
            oldcodec1 = codec1;
        }
        if (cw_sockaddr_cmp(&t0.sa, &ac0.sa, -1, 1) || (vp0 && cw_sockaddr_cmp(&vt0.sa, &vac0.sa, -1, 1)))
        {
            if (option_debug)
            {
                cw_log(CW_LOG_DEBUG, "Oooh, '%s' changed end address to %l@ (format %d)\n", c0->name, &t0.sa, codec0);
                cw_log(CW_LOG_DEBUG, "Oooh, '%s' was %l@/(format %d)\n", c0->name, &ac0.sa, oldcodec0);
            }
            if (pr1->set_rtp_peer(c1, (t0.sa.sa_family != AF_UNSPEC ? p0 : NULL), (vt0.sa.sa_family != AF_UNSPEC ? vp0 : NULL), codec0, (p0->nat_state != NAT_STATE_INACTIVE)))
                cw_log(CW_LOG_WARNING, "Channel '%s' failed to update to '%s'\n", c1->name, c0->name);
            cw_sockaddr_copy(&ac0.sa, &t0.sa);
            cw_sockaddr_copy(&vac0.sa, &vt0.sa);
            oldcodec0 = codec0;
        }
        if ((who = cw_waitfor_n(cs, 2, &timeoutms)) == 0)
        {
            if (!timeoutms)
            {
                if (pr0->set_rtp_peer(c0, NULL, NULL, 0, 0))
                    cw_log(CW_LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c0->name);
                if (pr1->set_rtp_peer(c1, NULL, NULL, 0, 0))
                    cw_log(CW_LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c1->name);
                return CW_BRIDGE_RETRY;
            }
            if (option_debug)
                cw_log(CW_LOG_DEBUG, "Ooh, empty read...\n");
            /* check for hangup / whentohangup */
            if (cw_check_hangup(c0) || cw_check_hangup(c1))
                break;
            continue;
        }
        f = cw_read(who);
        if (f == NULL
        || (f->frametype == CW_FRAME_DTMF
            && ((who == c0 && (flags & CW_BRIDGE_DTMF_CHANNEL_0)) || (who == c1 && (flags & CW_BRIDGE_DTMF_CHANNEL_1)))))
        {
            *fo = f;
            *rc = who;
            if (option_debug)
                cw_log(CW_LOG_DEBUG, "Oooh, got a %s\n", f  ?  "digit"  :  "hangup");
            if (c0->tech_pvt == pvt0 && !c0->_softhangup) {
                if (pr0->set_rtp_peer(c0, NULL, NULL, 0, 0)) 
                    cw_log(CW_LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c0->name);
            }
            if (c1->tech_pvt == pvt1 && !c1->_softhangup) {
                if (pr1->set_rtp_peer(c1, NULL, NULL, 0, 0)) 
                    cw_log(CW_LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c1->name);
            }
            return CW_BRIDGE_COMPLETE;
        } else if (f->frametype == CW_FRAME_CONTROL && !(flags & CW_BRIDGE_IGNORE_SIGS)) {
            if (f->subclass == CW_CONTROL_HOLD
            || f->subclass == CW_CONTROL_UNHOLD
            || f->subclass == CW_CONTROL_VIDUPDATE) {
                cw_indicate((who == c0 ? c1 : c0), f->subclass);
                cw_fr_free(f);
            } else {
                *fo = f;
                *rc = who;
                cw_log(CW_LOG_DEBUG, "Got a FRAME_CONTROL (%d) frame on channel %s\n", f->subclass, who->name);
                return CW_BRIDGE_COMPLETE;
            }
        } else {
            if (f->frametype == CW_FRAME_DTMF
            || f->frametype == CW_FRAME_VOICE
            || f->frametype == CW_FRAME_VIDEO) {
                /* Forward voice or DTMF frames if they happen upon us */
                if (who == c0)
                    cw_write(c1, &f);
                else if (who == c1)
                    cw_write(c0, &f);
            }
            cw_fr_free(f);
        }
        /* Swap priority not that it's a big deal at this point */
        cs[2] = cs[0];
        cs[0] = cs[1];
        cs[1] = cs[2];
    }
    return CW_BRIDGE_FAILED;
}

static int rtp_do_debug_ip(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    struct addrinfo *addrs;
    int err;

    if (argc != 4)
        return RESULT_SHOWUSAGE;

    if (!(err = cw_getaddrinfo(argv[3], "0", NULL, &addrs, NULL))) {
        memcpy(&rtpdebugaddr, addrs->ai_addr, addrs->ai_addrlen);
        cw_dynstr_printf(ds_p, "RTP debugging enabled for IP: %l@\n", rtpdebugaddr);
        rtpdebug = 1;
    } else
        cw_log(CW_LOG_WARNING, "%s: %s\n", argv[3], gai_strerror(err));

    return RESULT_SUCCESS;
}

static int rtp_do_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    if (argc != 2)
    {
        if (argc != 4)
            return RESULT_SHOWUSAGE;
        return rtp_do_debug_ip(ds_p, argc, argv);
    }
    rtpdebug = 1;
    memset(&rtpdebugaddr, 0, sizeof(rtpdebugaddr));
    cw_dynstr_printf(ds_p, "RTP debugging enabled for all IPs\n");
    return RESULT_SUCCESS;
}
   
static int rtp_no_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    CW_UNUSED(argv);

    if (argc !=3)
        return RESULT_SHOWUSAGE;

    rtpdebug = 0;
    cw_dynstr_printf(ds_p,"RTP Debugging Disabled\n");

    return RESULT_SUCCESS;
}

static const char debug_usage[] =
    "Usage: rtp debug [ip host[:port]]\n"
    "       Enable dumping of all RTP packets to and from host.\n";

static const char no_debug_usage[] =
    "Usage: rtp no debug\n"
    "       Disable all RTP debugging\n";

static struct cw_clicmd  cli_debug_ip = {
	.cmda = { "rtp", "debug", "ip", NULL },
	.handler = rtp_do_debug,
	.summary = "Enable RTP debugging on IP",
	.usage = debug_usage,
};

static struct cw_clicmd  cli_debug = {
	.cmda = { "rtp", "debug", NULL },
	.handler = rtp_do_debug,
	.summary = "Enable RTP debugging",
	.usage = debug_usage,
};

static struct cw_clicmd  cli_no_debug = {
	.cmda = { "rtp", "no", "debug", NULL },
	.handler = rtp_no_debug,
	.summary = "Disable RTP debugging",
	.usage = no_debug_usage,
};

void cw_rtp_reload(void)
{
    struct cw_config *cfg;
    char *s;

    /* Set defaults */
    rtpstart = DEFAULT_RTPSTART;
    rtpend = DEFAULT_RTPEND;
    dtmftimeout = DEFAULT_DTMFTIMEOUT;

    cfg = cw_config_load("rtp.conf");
    if (cfg)
    {
        if ((s = cw_variable_retrieve(cfg, "general", "rtpstart")))
        {
            rtpstart = atoi(s);
            if (rtpstart < 1024)
                rtpstart = 1024;
            if (rtpstart > 65535)
                rtpstart = 65535;
        }
        if ((s = cw_variable_retrieve(cfg, "general", "rtpend")))
        {
            rtpend = atoi(s);
            if (rtpend < 1024)
                rtpend = 1024;
            if (rtpend > 65535)
                rtpend = 65535;
        }
        if ((s = cw_variable_retrieve(cfg, "general", "dtmftimeout")))
        {
            dtmftimeout = atoi(s);
            if (dtmftimeout <= 1)
            {
                cw_log(CW_LOG_WARNING, "Invalid dtmftimeout given: %d, using default value %d", dtmftimeout, DEFAULT_DTMFTIMEOUT);
                dtmftimeout = DEFAULT_DTMFTIMEOUT;
            }
        }
        if ((s = cw_variable_retrieve(cfg, "general", "rtpchecksums")))
        {
#ifdef SO_NO_CHECK
            if (cw_false(s))
                nochecksums = 1;
            else
                nochecksums = 0;
#else
            if (cw_false(s))
                cw_log(CW_LOG_WARNING, "Disabling RTP checksums is not supported on this operating system!\n");
#endif
        }
        cw_config_destroy(cfg);
    }
    if (rtpstart >= rtpend)
    {
        cw_log(CW_LOG_WARNING, "Unreasonable values for RTP start/end port in rtp.conf\n");
        rtpstart = 5000;
        rtpend = 31000;
    }
    if (option_verbose > 1)
        cw_verbose(VERBOSE_PREFIX_2 "RTP Allocating from port range %d -> %d\n", rtpstart, rtpend);
}

int cw_rtp_init(void)
{
    cw_cli_register(&cli_debug);
    cw_cli_register(&cli_debug_ip);
    cw_cli_register(&cli_no_debug);
    cw_rtp_reload();
    return 0;
}
