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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

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
#ifdef ENABLE_SRTP
#include <srtp/srtp.h>
#endif

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/udp.h"
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
#include "callweaver/cli.h"
#include "callweaver/unaligned.h"
#include "callweaver/utils.h"
#include "callweaver/stun.h"

#define MAX_TIMESTAMP_SKEW    640

#define RTP_MTU        1200

#define DEFAULT_RTPSTART 5000
#define DEFAULT_RTPEND 31000
#define DEFAULT_DTMFTIMEOUT 3000    /* 3000 samples */

static int dtmftimeout = DEFAULT_DTMFTIMEOUT;
static int rtpstart = 0;
static int rtpend = 0;
static int rtpdebug = 0;        /* Are we debugging? */
static struct sockaddr_in rtpdebugaddr;    /* Debug packets to/from this host */
static int nochecksums = 0;

#define FLAG_3389_WARNING           (1 << 0)
#define FLAG_NAT_ACTIVE             (3 << 1)
#define FLAG_NAT_INACTIVE           (0 << 1)
#define FLAG_NAT_INACTIVE_NOWARN    (1 << 1)

#ifdef ENABLE_SRTP
struct opbx_policy
{
    srtp_policy_t sp;
};
#endif

static struct opbx_rtp_protocol *protos = NULL;

struct rtp_codec_table
{
    int format;
    int len;
    int defaultms;
    int increment;
    unsigned int flags;
};

struct rtp_codec_table RTP_CODEC_TABLE[] =
{
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
    for (x = 0;  RTP_CODEC_TABLE[x].format;  x++)
    {
        if (RTP_CODEC_TABLE[x].format == format)
        {
            ent = &RTP_CODEC_TABLE[x];
            if (*ms == 0)
                *ms = ent->defaultms;
            while ((res = (*ms%ent->increment)))
                (*ms)++;
            while ((*len = (*ms/ent->increment)*ent->len) > RTP_MTU)
                *ms -= ent->increment;
            break;
        }
    }
    return ent;
}

int opbx_rtp_fd(struct opbx_rtp *rtp)
{
    return udp_socket_fd(rtp->rtp_sock_info);
}

int opbx_rtcp_fd(struct opbx_rtp *rtp)
{
    return udp_socket_fd(rtp->rtcp_sock_info);
}

udp_state_t *opbx_rtp_udp_socket(struct opbx_rtp *rtp,
                                 udp_state_t *sock_info)
{
    udp_state_t *old;
    
    old = rtp->rtp_sock_info;
    if (sock_info)
        rtp->rtp_sock_info = sock_info;
    return old;
}

udp_state_t *opbx_rtcp_udp_socket(struct opbx_rtp *rtp,
                                  udp_state_t *sock_info)
{
    udp_state_t *old;
    
    old = rtp->rtcp_sock_info;
    if (sock_info)
        rtp->rtcp_sock_info = sock_info;
    return old;
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
    udp_socket_set_nat(rtp->rtp_sock_info, nat);
    udp_socket_set_nat(rtp->rtcp_sock_info, nat);
}

int opbx_rtp_set_framems(struct opbx_rtp *rtp, int ms) 
{
    if (ms)
    {
        if (rtp->smoother)
        {
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
    const struct sockaddr_in *them;

    them = udp_socket_get_them(rtp->rtp_sock_info);
    if (opbx_tvcmp(opbx_tvnow(), rtp->dtmfmute) < 0)
    {
        if (option_debug)
            opbx_log(OPBX_LOG_DEBUG, "Ignore potential DTMF echo from '%s'\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), them->sin_addr));
        rtp->resp = 0;
        rtp->dtmfduration = 0;
        return &null_frame;
    }
    if (option_debug)
        opbx_log(OPBX_LOG_DEBUG, "Sending dtmf: %d (%c), at %s\n", rtp->resp, rtp->resp, opbx_inet_ntoa(iabuf, sizeof(iabuf), them->sin_addr));
    opbx_fr_init(&rtp->f);
    if (rtp->resp == 'X')
    {
        rtp->f.frametype = OPBX_FRAME_CONTROL;
        rtp->f.subclass = OPBX_CONTROL_FLASH;
    }
    else
    {
        rtp->f.frametype = OPBX_FRAME_DTMF;
        rtp->f.subclass = rtp->resp;
    }
    rtp->f.src = "RTP";
    rtp->resp = 0;
    rtp->dtmfduration = 0;
    return  &rtp->f;
}

static inline int rtp_debug_test_addr(const struct sockaddr_in *addr)
{
    if (rtpdebug == 0)
        return 0;
    if (rtpdebugaddr.sin_addr.s_addr)
    {
        if (((ntohs(rtpdebugaddr.sin_port) != 0)  &&  (rtpdebugaddr.sin_port != addr->sin_port))
            ||
            (rtpdebugaddr.sin_addr.s_addr != addr->sin_addr.s_addr))
        {
            return 0;
        }
    }
    return 1;
}

static struct opbx_frame *process_cisco_dtmf(struct opbx_rtp *rtp, unsigned char *data, int len)
{
    unsigned int event;
    char resp = 0;
    struct opbx_frame *f = NULL;

    event = ntohl(*((unsigned int *) data)) & 0x001F;
#if 0
    printf("Cisco Digit: %08x (len = %d)\n", event, len);
#endif    
    if (event < 10)
        resp = '0' + event;
    else if (event < 11)
        resp = '*';
    else if (event < 12)
        resp = '#';
    else if (event < 16)
        resp = 'A' + (event - 12);
    else if (event < 17)
        resp = 'X';
    if (rtp->resp && (rtp->resp != resp))
        f = send_dtmf(rtp);
    rtp->resp = resp;
    rtp->dtmfcount = dtmftimeout;
    return f;
}

/* process_rfc2833: Process RTP DTMF and events according to RFC 2833:
    "RTP Payload for DTMF Digits, Telephony Tones and Telephony Signals"
*/
static struct opbx_frame *process_rfc2833(struct opbx_rtp *rtp, unsigned char *data, int len, unsigned int seqno)
{
    uint32_t event;
    uint32_t event_end;
    uint32_t duration;
    char resp = 0;
    struct opbx_frame *f = NULL;

    event = ntohl(*((uint32_t *) data));
    event >>= 24;
    event_end = ntohl(*((uint32_t *) data));
    event_end <<= 8;
    event_end >>= 24;
    duration = ntohl(*((uint32_t *) data)) & 0xFFFF;
    if (rtpdebug)
        opbx_log(OPBX_LOG_DEBUG, "- RTP 2833 Event: %08x (len = %d)\n", event, len);
    if (event < 10)
        resp = '0' + event;
    else if (event < 11)
        resp = '*';
    else if (event < 12)
        resp = '#';
    else if (event < 16)
        resp = 'A' + (event - 12);
    else if (event < 17)    /* Event 16: Hook flash */
        resp = 'X';
    if (rtp->resp  &&  (rtp->resp != resp))
    {
        f = send_dtmf(rtp);
    }
    else if (event_end & 0x80)
    {
        if (rtp->resp)
        {
            if (rtp->lasteventendseqn != seqno)
            {
                f = send_dtmf(rtp);
                rtp->lasteventendseqn = seqno;
            }
            rtp->resp = 0;
        }
        resp = 0;
        duration = 0;
    }
    else if (rtp->dtmfduration  &&  (duration < rtp->dtmfduration))
    {
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
        opbx_log(OPBX_LOG_DEBUG, "- RTP 3389 Comfort noise event: Level %d (len = %d)\n", rtp->lastrxformat, len);

    if (!(opbx_test_flag(rtp, FLAG_3389_WARNING)))
    {
        char iabuf[INET_ADDRSTRLEN];

        opbx_log(OPBX_LOG_NOTICE, "Comfort noise support incomplete in CallWeaver (RFC 3389). Please turn off on client if possible. Client IP: %s\n",
                 opbx_inet_ntoa(iabuf, sizeof(iabuf), udp_socket_get_them(rtp->rtp_sock_info)->sin_addr));
        opbx_set_flag(rtp, FLAG_3389_WARNING);
    }

    /* Must have at least one byte */
    if (len == 0)
        return NULL;
    if (len < 24)
    {
        rtp->f.data = rtp->rawdata + OPBX_FRIENDLY_OFFSET;
        rtp->f.datalen = len - 1;
        rtp->f.offset = OPBX_FRIENDLY_OFFSET;
        memcpy(rtp->f.data, data + 1, len - 1);
    }
    else
    {
        rtp->f.data = NULL;
        rtp->f.offset = 0;
        rtp->f.datalen = 0;
    }
    opbx_fr_init(&rtp->f);
    rtp->f.frametype = OPBX_FRAME_CNG;
    rtp->f.subclass = data[0] & 0x7F;
    rtp->f.datalen = len - 1;
    f = &rtp->f;
    return f;
}

#ifdef ENABLE_SRTP
static const char *srtp_errstr(int err)
{
    switch (err)
    {
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
    switch (data->event)
    {
    case event_ssrc_collision:
        if (option_debug  ||  rtpdebug)
        {
            opbx_log(OPBX_LOG_DEBUG, "SSRC collision ssrc:%u dir:%d\n",
                     ntohl(data->stream->ssrc),
                     data->stream->direction);
        }
        break;
    case event_key_soft_limit:
        if (option_debug  ||  rtpdebug)
            opbx_log(OPBX_LOG_DEBUG, "event_key_soft_limit\n");
        break;
    case event_key_hard_limit:
        if (option_debug  ||  rtpdebug)
            opbx_log(OPBX_LOG_DEBUG, "event_key_hard_limit\n");
        break;
    case event_packet_index_limit:
        if (option_debug  ||  rtpdebug)
            opbx_log(OPBX_LOG_DEBUG, "event_packet_index_limit\n");
        break;
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

void opbx_policy_set_ssrc(opbx_policy_t *policy,
                          struct opbx_rtp *rtp, 
                          uint32_t ssrc,
                          int inbound)
{
    if (ssrc == 0  &&  !inbound  &&  rtp)
        ssrc = rtp->ssrc;

    if (ssrc)
    {
        policy->sp.ssrc.type = ssrc_specific;
        policy->sp.ssrc.value = ssrc;
    }
    else
    {
        policy->sp.ssrc.type = inbound  ?  ssrc_any_inbound  :  ssrc_any_outbound;
    }
}

int opbx_rtp_add_policy(struct opbx_rtp *rtp, opbx_policy_t *policy)
{
    int res = 0;

    opbx_log(OPBX_LOG_NOTICE, "Adding SRTP policy: %d %d %d %d %d %d\n",
           policy->sp.rtp.cipher_type,
           policy->sp.rtp.cipher_key_len,
           policy->sp.rtp.auth_type,
           policy->sp.rtp.auth_key_len,
           policy->sp.rtp.auth_tag_len,
           policy->sp.rtp.sec_serv);

    if (!rtp->srtp)
        res = srtp_create(&rtp->srtp, &policy->sp);
    else
        res = srtp_add_stream(rtp->srtp, &policy->sp);

    if (res != err_status_ok)
    {
        opbx_log(OPBX_LOG_WARNING, "SRTP policy: %s\n", srtp_errstr(res));
        return -1;
    }

    return 0;
}

opbx_policy_t *opbx_policy_alloc()
{
    opbx_policy_t *tmp;

    if ((tmp = malloc(sizeof(opbx_policy_t))) == NULL)
        return NULL;
    memset(tmp, 0, sizeof(opbx_policy_t));
    return tmp;
}

void opbx_policy_destroy(opbx_policy_t *policy)
{
    if (policy->sp.key)
    {
        free(policy->sp.key);
        policy->sp.key = NULL;
    }
    free(policy);
}

static int policy_set_suite(crypto_policy_t *p, int suite)
{
    switch (suite)
    {
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
        opbx_log(OPBX_LOG_ERROR, "Invalid crypto suite: %d\n", suite);
        return -1;
    }
}

int opbx_policy_set_suite(opbx_policy_t *policy, int suite)
{
    int res;
    
    res = policy_set_suite(&policy->sp.rtp, suite)
        | policy_set_suite(&policy->sp.rtcp, suite);
    return res;
}

int opbx_policy_set_master_key(opbx_policy_t *policy,
                               const unsigned char *key, size_t key_len,
                               const unsigned char *salt, size_t salt_len)
{
    size_t size = key_len + salt_len;
    unsigned char *master_key = NULL;

    if (policy->sp.key)
    {
        free(policy->sp.key);
        policy->sp.key = NULL;
    }

    master_key = malloc(size);

    memcpy(master_key, key, key_len);
    memcpy(master_key + key_len, salt, salt_len);

    policy->sp.key = master_key;
    return 0;
}

int opbx_policy_set_encr_alg(opbx_policy_t *policy, int ealg)
{
    int type = -1;

    switch (ealg)
    {
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

    switch (aalg)
    {
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
    int serv = enable  ?  sec_serv_conf  :  sec_serv_none;

    policy->sp.rtp.sec_serv = (policy->sp.rtp.sec_serv & ~sec_serv_conf) | serv;
}

void opbx_policy_set_srtcp_encr_enable(opbx_policy_t *policy, int enable)
{
    int serv = enable  ?  sec_serv_conf  :  sec_serv_none;

    policy->sp.rtcp.sec_serv = (policy->sp.rtcp.sec_serv & ~sec_serv_conf) | serv;
}

void opbx_policy_set_srtp_auth_enable(opbx_policy_t *policy, int enable)
{
    int serv = enable  ?  sec_serv_auth  :  sec_serv_none;

    policy->sp.rtp.sec_serv = (policy->sp.rtp.sec_serv & ~sec_serv_auth) | serv;
}

int opbx_get_random(unsigned char *key, size_t len)
{
    int res = crypto_get_random(key, len);

    return (res != err_status_ok)  ?  -1  :  0;
}
#endif    /* ENABLE_SRTP */

static int rtp_recvfrom(struct opbx_rtp *rtp, void *buf, size_t size,
                        int flags, struct sockaddr *sa, socklen_t *salen, int *actions)
{
    int len;

    len = udp_socket_recvfrom(rtp->rtp_sock_info, buf, size, flags, sa, salen, actions);
    if (len < 0)
        return len;

#ifdef ENABLE_SRTP
    if (rtp->srtp)
    {
        int res = 0;
        int i;

        for (i = 0;  i < 5;  i++)
        {
            srtp_hdr_t *header = buf;

            res = srtp_unprotect(rtp->srtp, buf, &len);
            if (res != err_status_no_ctx)
                break;

            if (rtp->key_cb)
            {
                if (rtp->key_cb(rtp, ntohl(header->ssrc), rtp->data) < 0)
                    break;
            }
            else
            {
                break;
            }
        }

        if (res != err_status_ok)
        {
            if (option_debug  ||  rtpdebug)
                opbx_log(OPBX_LOG_DEBUG, "SRTP unprotect: %s\n", srtp_errstr(res));
            return -1;
        }
    }
#endif    /* ENABLE_SRTP */

    return len;
}

static int rtp_sendto(struct opbx_rtp *rtp, void *buf, size_t size, int flags)
{
    int len = size;

#ifdef ENABLE_SRTP
    if (rtp->srtp)
    {
        int res = srtp_protect(rtp->srtp, buf, &len);

        if (res != err_status_ok)
        {
            if (option_debug  ||  rtpdebug)
                opbx_log(OPBX_LOG_DEBUG, "SRTP protect: %s\n", srtp_errstr(res));
            return -1;
        }
    }
#endif    /* ENABLE_SRTP */

    return udp_socket_sendto(rtp->rtp_sock_info, buf, len, flags);
}

static int rtpread(int *id, int fd, short events, void *cbdata)
{
    struct opbx_rtp *rtp = cbdata;
    struct opbx_frame *f;

    if ((f = opbx_rtp_read(rtp)))
    {
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
    int actions;
    struct sockaddr_in sin;
    unsigned int rtcpdata[1024];
    char iabuf[INET_ADDRSTRLEN];
    
    if (rtp == NULL)
        return &null_frame;

    len = sizeof(sin);

    res = udp_socket_recvfrom(rtp->rtcp_sock_info, rtcpdata, sizeof(rtcpdata), 0, (struct sockaddr *) &sin, &len, &actions);
    if (res < 0)
    {
        if (errno == EBADF)
        {
            opbx_log(OPBX_LOG_ERROR, "RTP read error: %s\n", strerror(errno));
            opbx_rtp_set_active(rtp, 0);
        }
        else if (errno != EAGAIN)
            opbx_log(OPBX_LOG_WARNING, "RTP read error: %s\n", strerror(errno));
        return &null_frame;
    }
    if ((actions & 1))
    {
        if (option_debug  ||  rtpdebug)
            opbx_log(OPBX_LOG_DEBUG, "RTCP NAT: Got RTCP from other end. Now sending to address %s:%d\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), udp_socket_get_them(rtp->rtcp_sock_info)->sin_addr), ntohs(udp_socket_get_them(rtp->rtcp_sock_info)->sin_port));
    }

    if (res < hdrlen)
    {
        opbx_log(OPBX_LOG_DEBUG, "RTP Read too short\n");
        return &null_frame;
    }
    if (option_debug)
        opbx_log(OPBX_LOG_DEBUG, "Got RTCP report of %d bytes\n", res);
    return &null_frame;
}


static int opbx_rtp_senddigit_continue(void *data)
{
	uint32_t pkt[4];
	char iabuf[INET_ADDRSTRLEN];
	struct opbx_rtp *rtp = data;
	const struct sockaddr_in *them;
	int repeat;
	int more = 1;

	them = udp_socket_get_them(rtp->rtp_sock_info);

	rtp->dtmfmute = opbx_tvadd(opbx_tvnow(), opbx_tv(0, 500000));

	/* Assumption: the rate used for telephone-events is the
	 * default 8kHz and the packetization interval of the
	 * incoming audio (which we are clocking off) is 20ms
	 */
	rtp->senddtmf_duration += 160;

	pkt[0] = htonl(rtp->senddtmf_rtphdr | rtp->seqno);
	pkt[1] = htonl(rtp->lastdigitts);
	pkt[2] = htonl(rtp->ssrc); 
	pkt[3] = htonl(rtp->senddtmf_payload | rtp->senddtmf_duration);

	/* DTMF tone duration is fixed at 100ms (given the above
	 * assumptions are true). The inter-digit time is fixed
	 * at 80ms. However, note that we're not negotiating
	 * back off with higher levels. If we're asked to send
	 * back-to-back DTMF we'll simply send events with
	 * timestamps in the future.
	 */
	repeat = 1;
	if (rtp->senddtmf_duration >= 100 * 8) {
		rtp->lastdigitts += rtp->senddtmf_duration + (80 * 8);
		/* End event packets are sent 3 times back-to-back */
		rtp->senddtmf_rtphdr = 0;
		pkt[3] |= htonl(1 << 23);
		repeat = 3;
		more = 0;
	}

	while (repeat--) {
		if (rtp_sendto(rtp, (void *)pkt, sizeof(pkt), 0) < 0) {
			opbx_log(OPBX_LOG_ERROR, "RTP Transmission error to %s:%d: %s\n",
				opbx_inet_ntoa(iabuf, sizeof(iabuf), them->sin_addr),
				ntohs(them->sin_port),
				strerror(errno));
		}
		if (rtp_debug_test_addr(them)) {
			opbx_verbose("Sent RTP packet to %s:%d (type %d, seq %d, ts %d, len 4) - DTMF cont (duration=%d)\n",
				opbx_inet_ntoa(iabuf, sizeof(iabuf), them->sin_addr),
				ntohs(them->sin_port),
				(rtp->senddtmf_rtphdr & !(2 << 30)),
				rtp->seqno,
				rtp->lastdigitts,
				rtp->senddtmf_duration);
		}
	}

	rtp->seqno++;
	return more;
}


int opbx_rtp_senddigit(struct opbx_rtp * const rtp, char digit)
{
	static char *digitcodes = "0123456789*#ABCD";
	uint32_t pkt[4];
	char *p;
	char iabuf[INET_ADDRSTRLEN];
	const struct sockaddr_in *them;

	/* If we have no peer, return immediately */
	them = udp_socket_get_them(rtp->rtp_sock_info);
	if (them->sin_addr.s_addr == 0 || them->sin_port == 0)
		return 0;

	if (!(p = strchr(digitcodes, toupper(digit)))) {
		opbx_log(OPBX_LOG_WARNING, "Don't know how to represent '%c'\n", digit);
		return -1;
	}

	rtp->senddtmf_rtphdr = ((2 << 30) | (opbx_rtp_lookup_code(rtp, 0, OPBX_RTP_DTMF) << 16));
	rtp->senddtmf_payload = ((p - digitcodes) << 24) | (0xa << 16);
	rtp->senddtmf_duration = 0;

	rtp->dtmfmute = opbx_tvadd(opbx_tvnow(), opbx_tv(0, 500000));
 
	pkt[0] = htonl(rtp->senddtmf_rtphdr | (1 << 23) | (rtp->seqno));
	pkt[1] = htonl(rtp->lastdigitts);
	pkt[2] = htonl(rtp->ssrc); 
	pkt[3] = htonl(rtp->senddtmf_payload);

	if (rtp_sendto(rtp, (void *)pkt, sizeof(pkt), 0) < 0) {
		opbx_log(OPBX_LOG_ERROR, "RTP Transmission error to %s:%d: %s\n",
			opbx_inet_ntoa(iabuf, sizeof(iabuf), them->sin_addr),
			ntohs(them->sin_port),
			strerror(errno));
	}
	if (rtp_debug_test_addr(them)) {
		opbx_verbose("Sent RTP packet to %s:%d (type %d, seq %d, ts %d, len 4) - DTMF start (duration = %d)\n",
			opbx_inet_ntoa(iabuf, sizeof(iabuf), them->sin_addr),
			ntohs(them->sin_port),
			(rtp->senddtmf_rtphdr & !(2 << 30)),
			rtp->seqno,
			rtp->lastdigitts,
			rtp->senddtmf_duration);
	}

	rtp->seqno++;

	if (rtp->sched)
		opbx_sched_add(rtp->sched, 20, opbx_rtp_senddigit_continue, rtp);

	return 0;
}


static void calc_rxstamp(struct timeval *tv, struct opbx_rtp *rtp, unsigned int timestamp, int mark)
{
    struct timeval ts = opbx_samp2tv(timestamp, 8000);

    if (opbx_tvzero(rtp->rxcore)  ||  mark)
    {
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
    uint32_t seqno;
    uint32_t csrc_count;
    int version;
    int payloadtype;
    int hdrlen = 3*sizeof(uint32_t);
    int mark;
    int actions;
    /* Remove the variable for the pointless loop */
    char iabuf[INET_ADDRSTRLEN];
    uint32_t timestamp;
    uint32_t ssrc;
    uint32_t *rtpheader;
    static struct opbx_frame *f, null_frame = { OPBX_FRAME_NULL, };
    struct rtpPayloadType rtpPT;

    /* Is this always right? It clocks off incoming data on
     * the assumption that we are reading data at regular
     * known intervals - specifically that incoming packets
     * are 20ms long and evenly paced.
     * (see opbx_rtp_senddigit_continue)
     */
    if (rtp->senddtmf_rtphdr && !rtp->sched)
	    opbx_rtp_senddigit_continue(rtp);

    len = sizeof(sin);

    /* Cache where the header will go */
    res = rtp_recvfrom(rtp, rtp->rawdata + OPBX_FRIENDLY_OFFSET, sizeof(rtp->rawdata) - OPBX_FRIENDLY_OFFSET,
                       0, (struct sockaddr *) &sin, &len, &actions);

    rtpheader = (uint32_t *)(rtp->rawdata + OPBX_FRIENDLY_OFFSET);
    if (res < 0)
    {
        if (errno == EBADF)
        {
            opbx_log(OPBX_LOG_ERROR, "RTP read error: %s\n", strerror(errno));
            opbx_rtp_set_active(rtp, 0);
        }
        else if (errno != EAGAIN)
            opbx_log(OPBX_LOG_WARNING, "RTP read error: %s\n", strerror(errno));
        return &null_frame;
    }

    if (res < 3*sizeof(uint32_t))
    {
        /* Too short for an RTP packet. */
        opbx_log(OPBX_LOG_DEBUG, "RTP Read too short\n");
        return &null_frame;
    }

    /* Ignore if the other side hasn't been given an address yet. */
    if (udp_socket_get_them(rtp->rtp_sock_info)->sin_addr.s_addr == 0  ||  udp_socket_get_them(rtp->rtp_sock_info)->sin_port == 0)
        return &null_frame;

    if (rtp->nat)
    {
        if (actions & 1)
        {
            /* The other side changed */
            rtp->rxseqno = 0;
            opbx_set_flag(rtp, FLAG_NAT_ACTIVE);
            if (option_debug  ||  rtpdebug)
            {
                opbx_log(OPBX_LOG_DEBUG, "RTP NAT: Got audio from other end. Now sending to address %s:%d\n",
                         opbx_inet_ntoa(iabuf, sizeof(iabuf), udp_socket_get_them(rtp->rtp_sock_info)->sin_addr),
                         ntohs(udp_socket_get_them(rtp->rtp_sock_info)->sin_port));
            }
        }
    }

    /* Get fields */
    seqno = ntohl(rtpheader[0]);

    /* Check RTP version */
    if ((version = (seqno & 0xC0000000) >> 30) != 2)
        return &null_frame;
    
    if ((seqno & (1 << 29)))
    {
        /* There are some padding bytes. Remove them. */
        res -= rtp->rawdata[OPBX_FRIENDLY_OFFSET + res - 1];
    }
    if ((csrc_count = (seqno >> 24) & 0x0F))
    {
        /* Allow for the contributing sources, but don't attempt to
           process them. */
        hdrlen += (csrc_count << 2);
        if (res < hdrlen)
        {
            /* Too short for an RTP packet. */
            return &null_frame;
        }
    }
    if ((seqno & (1 << 28)))
    {
        /* RTP extension present. Skip over it. */
        hdrlen += sizeof(uint32_t);
        if (len >= hdrlen)
            hdrlen += ((ntohl(rtpheader[hdrlen >> 2]) & 0xFFFF)*sizeof(uint32_t));
        if (len < hdrlen)
        {
            opbx_log(OPBX_LOG_DEBUG, "RTP Read too short (%d, expecting %d)\n", res, hdrlen);
            return &null_frame;
        }
    }
    mark = (seqno >> 23) & 1;
    payloadtype = (seqno >> 16) & 0x7F;
    seqno &= 0xFFFF;
    timestamp = ntohl(rtpheader[1]);
    ssrc = ntohl(rtpheader[2]);

    if (rtp_debug_test_addr(&sin))
    {
        opbx_verbose("Got RTP packet from %s:%d (type %d, seq %d, ts %d, len %d)\n",
                     opbx_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr),
                     ntohs(sin.sin_port),
                     payloadtype,
                     seqno,
                     timestamp,
                     res - hdrlen);
    }

    rtpPT = opbx_rtp_lookup_pt(rtp, payloadtype);
    if (!rtpPT.is_opbx_format)
    {
        /* This is special in-band data that's not one of our codecs */
        if (rtpPT.code == OPBX_RTP_DTMF)
        {
            /* It's special -- rfc2833 process it */
            if (rtp_debug_test_addr(&sin))
            {
                unsigned char *data;
                unsigned int event;
                unsigned int event_end;
                unsigned int duration;

                data = rtp->rawdata + OPBX_FRIENDLY_OFFSET + hdrlen;
                event = ntohl(*((unsigned int *) (data)));
                event >>= 24;
                event_end = ntohl(*((unsigned int *) (data)));
                event_end <<= 8;
                event_end >>= 24;
                duration = ntohl(*((unsigned int *) (data)));
                duration &= 0xFFFF;
                opbx_verbose("Got rfc2833 RTP packet from %s:%d (type %d, seq %d, ts %d, len %d, mark %d, event %08x, end %d, duration %d) \n", opbx_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr), ntohs(sin.sin_port), payloadtype, seqno, timestamp, res - hdrlen, (mark?1:0), event, ((event_end & 0x80)?1:0), duration);
            }
            if (rtp->lasteventseqn <= seqno || rtp->resp == 0 || (rtp->lasteventseqn >= 65530 && seqno <= 6))
            {
                f = process_rfc2833(rtp, rtp->rawdata + OPBX_FRIENDLY_OFFSET + hdrlen, res - hdrlen, seqno);
                rtp->lasteventseqn = seqno;
            }
            else
                f = NULL;
            if (f) 
                return f; 
            return &null_frame;
        }
        else if (rtpPT.code == OPBX_RTP_CISCO_DTMF)
        {
            /* It's really special -- process it the Cisco way */
            if (rtp->lasteventseqn <= seqno  ||  rtp->resp == 0  ||  (rtp->lasteventseqn >= 65530  &&  seqno <= 6))
            {
                f = process_cisco_dtmf(rtp, rtp->rawdata + OPBX_FRIENDLY_OFFSET + hdrlen, res - hdrlen);
                rtp->lasteventseqn = seqno;
            }
            else 
                f = NULL;
            if (f) 
                return f; 
            else 
                return &null_frame;
        }
        else if (rtpPT.code == OPBX_RTP_CN)
        {
            /* Comfort Noise */
            f = process_rfc3389(rtp, rtp->rawdata + OPBX_FRIENDLY_OFFSET + hdrlen, res - hdrlen);
            if (f) 
                return f; 
            else 
                return &null_frame;
        }
        else
        {
            opbx_log(OPBX_LOG_NOTICE, "Unknown RTP codec %d received\n", payloadtype);
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

    rtp->rxseqno = seqno;

    if (rtp->dtmfcount)
    {
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
    if (rtp->resp && !rtp->dtmfcount)
    {
        if (option_debug)
            opbx_log(OPBX_LOG_DEBUG, "Sending pending DTMF\n");
        return send_dtmf(rtp);
    }
    rtp->f.mallocd = 0;
    rtp->f.datalen = res - hdrlen;
    rtp->f.data = rtp->rawdata + hdrlen + OPBX_FRIENDLY_OFFSET;
    rtp->f.offset = hdrlen + OPBX_FRIENDLY_OFFSET;
    if (rtp->f.subclass < OPBX_FORMAT_MAX_AUDIO)
    {
        rtp->f.samples = opbx_codec_get_samples(&rtp->f);
        if (rtp->f.subclass == OPBX_FORMAT_SLINEAR) 
            opbx_frame_byteswap_be(&rtp->f);
        calc_rxstamp(&rtp->f.delivery, rtp, timestamp, mark);

        /* Add timing data to let opbx_generic_bridge() put the frame
         * into a jitterbuf */
        rtp->f.has_timing_info = 1;
        rtp->f.ts = timestamp / 8;
        rtp->f.len = rtp->f.samples / 8;
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
    rtp->f.src = "RTP";
    return &rtp->f;
}

/* The following array defines the MIME Media type (and subtype) for each
   of our codecs, or RTP-specific data type. */
static struct
{
  struct rtpPayloadType payloadType;
  char* type;
  char* subtype;
} mimeTypes[] =
{
    {{1, OPBX_FORMAT_G723_1}, "audio", "G723"},
    {{1, OPBX_FORMAT_GSM}, "audio", "GSM"},
    {{1, OPBX_FORMAT_ULAW}, "audio", "PCMU"},
    {{1, OPBX_FORMAT_ALAW}, "audio", "PCMA"},
    {{1, OPBX_FORMAT_G726}, "audio", "G726-32"},
    {{1, OPBX_FORMAT_DVI_ADPCM}, "audio", "DVI4"},
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
    {{1, OPBX_FORMAT_H264}, "video", "H264"},
};

/* Static (i.e., well-known) RTP payload types for our "OPBX_FORMAT..."s:
   also, our own choices for dynamic payload types.  This is our master
   table for transmission */
static struct rtpPayloadType static_RTP_PT[MAX_RTP_PT] =
{
    [0] = {1, OPBX_FORMAT_ULAW},
#ifdef USE_DEPRECATED_G726
    [2] = {1, OPBX_FORMAT_G726}, /* Technically this is G.721, but if Cisco can do it, so can we... */
#endif
    [3] = {1, OPBX_FORMAT_GSM},
    [4] = {1, OPBX_FORMAT_G723_1},
    [5] = {1, OPBX_FORMAT_DVI_ADPCM}, /* 8 kHz */
    [6] = {1, OPBX_FORMAT_DVI_ADPCM}, /* 16 kHz */
    [7] = {1, OPBX_FORMAT_LPC10},
    [8] = {1, OPBX_FORMAT_ALAW},
    [10] = {1, OPBX_FORMAT_SLINEAR}, /* 2 channels */
    [11] = {1, OPBX_FORMAT_SLINEAR}, /* 1 channel */
    [13] = {0, OPBX_RTP_CN},
    [16] = {1, OPBX_FORMAT_DVI_ADPCM}, /* 11.025 kHz */
    [17] = {1, OPBX_FORMAT_DVI_ADPCM}, /* 22.050 kHz */
    [18] = {1, OPBX_FORMAT_G729A},
    [19] = {0, OPBX_RTP_CN},        /* Also used for CN */
    [26] = {1, OPBX_FORMAT_JPEG},
    [31] = {1, OPBX_FORMAT_H261},
    [34] = {1, OPBX_FORMAT_H263},
    [103] = {1, OPBX_FORMAT_H263_PLUS},
    [97] = {1, OPBX_FORMAT_ILBC},
    [99] = {1, OPBX_FORMAT_H264},
    [101] = {0, OPBX_RTP_DTMF},
    [110] = {1, OPBX_FORMAT_SPEEX},
    [111] = {1, OPBX_FORMAT_G726},
    [121] = {0, OPBX_RTP_CISCO_DTMF}, /* Must be type 121 */
    /* 122 used by T38 RTP */
};

void opbx_rtp_pt_clear(struct opbx_rtp* rtp) 
{
    int i;

    if (!rtp)
        return;
    for (i = 0;  i < MAX_RTP_PT;  ++i)
    {
        rtp->current_RTP_PT[i].is_opbx_format = 0;
        rtp->current_RTP_PT[i].code = 0;
    }

    rtp->rtp_lookup_code_cache_is_opbx_format = 0;
    rtp->rtp_lookup_code_cache_code = 0;
    rtp->rtp_lookup_code_cache_result = 0;
}

void opbx_rtp_pt_default(struct opbx_rtp* rtp) 
{
    int i;

    /* Initialize to default payload types */
    for (i = 0;  i < MAX_RTP_PT;  ++i)
    {
        rtp->current_RTP_PT[i].is_opbx_format = static_RTP_PT[i].is_opbx_format;
        rtp->current_RTP_PT[i].code = static_RTP_PT[i].code;
    }

    rtp->rtp_lookup_code_cache_is_opbx_format = 0;
    rtp->rtp_lookup_code_cache_code = 0;
    rtp->rtp_lookup_code_cache_result = 0;
}

/* Make a note of a RTP paymoad type that was seen in a SDP "m=" line. */
/* By default, use the well-known value for this type (although it may */
/* still be set to a different value by a subsequent "a=rtpmap:" line): */
void opbx_rtp_set_m_type(struct opbx_rtp* rtp, int pt)
{
    if (pt < 0  ||  pt > MAX_RTP_PT) 
        return; /* bogus payload type */

    if (static_RTP_PT[pt].code != 0)
        rtp->current_RTP_PT[pt] = static_RTP_PT[pt];
} 

/* Make a note of a RTP payload type (with MIME type) that was seen in */
/* a SDP "a=rtpmap:" line. */
void opbx_rtp_set_rtpmap_type(struct opbx_rtp *rtp, int pt,
                              char *mimeType, char *mimeSubtype)
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
/* They're returned as two distinct sets: OPBX_FORMATs, and OPBX_RTPs */
void opbx_rtp_get_current_formats(struct opbx_rtp *rtp,
                                  int *opbx_formats,
                                  int *non_opbx_formats)
{
    int pt;

    *opbx_formats =
    *non_opbx_formats = 0;
    for (pt = 0;  pt < MAX_RTP_PT;  ++pt)
    {
        if (rtp->current_RTP_PT[pt].is_opbx_format)
            *opbx_formats |= rtp->current_RTP_PT[pt].code;
        else
            *non_opbx_formats |= rtp->current_RTP_PT[pt].code;
    }
}

void opbx_rtp_offered_from_local(struct opbx_rtp* rtp, int local)
{
    if (rtp)
        rtp->rtp_offered_from_local = local;
    else
        opbx_log(OPBX_LOG_WARNING, "rtp structure is null\n");
}

struct rtpPayloadType opbx_rtp_lookup_pt(struct opbx_rtp* rtp, int pt) 
{
    struct rtpPayloadType result;

    result.is_opbx_format =
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
int opbx_rtp_lookup_code(struct opbx_rtp* rtp, const int is_opbx_format, const int code)
{
    int pt;

    if (is_opbx_format == rtp->rtp_lookup_code_cache_is_opbx_format
        &&
        code == rtp->rtp_lookup_code_cache_code)
    {
        /* Use our cached mapping, to avoid the overhead of the loop below */
        return rtp->rtp_lookup_code_cache_result;
    }

    /* Check the dynamic list first */
    for (pt = 0;  pt < MAX_RTP_PT;  ++pt)
    {
        if (rtp->current_RTP_PT[pt].code == code  &&  rtp->current_RTP_PT[pt].is_opbx_format == is_opbx_format)
        {
            rtp->rtp_lookup_code_cache_is_opbx_format = is_opbx_format;
            rtp->rtp_lookup_code_cache_code = code;
            rtp->rtp_lookup_code_cache_result = pt;
            return pt;
        }
    }

    /* Then the static list */
    for (pt = 0;  pt < MAX_RTP_PT;  ++pt)
    {
        if (static_RTP_PT[pt].code == code  &&  static_RTP_PT[pt].is_opbx_format == is_opbx_format)
        {
            rtp->rtp_lookup_code_cache_is_opbx_format = is_opbx_format;
              rtp->rtp_lookup_code_cache_code = code;
            rtp->rtp_lookup_code_cache_result = pt;
            return pt;
        }
    }
    return -1;
}

char *opbx_rtp_lookup_mime_subtype(const int is_opbx_format, const int code)
{
    int i;

    for (i = 0;  i < sizeof mimeTypes/sizeof mimeTypes[0];  ++i)
    {
        if (mimeTypes[i].payloadType.code == code  &&  mimeTypes[i].payloadType.is_opbx_format == is_opbx_format)
            return mimeTypes[i].subtype;
    }
    return "";
}

char *opbx_rtp_lookup_mime_multiple(char *buf, int size, const int capability, const int is_opbx_format)
{
    int format;
    unsigned int len;
    char *end = buf;
    char *start = buf;

    if (!buf  ||  !size)
        return NULL;

    snprintf(end, size, "0x%x (", capability);

    len = strlen(end);
    end += len;
    size -= len;
    start = end;

    for (format = 1;  format < OPBX_RTP_MAX;  format <<= 1)
    {
        if (capability & format)
        {
            const char *name = opbx_rtp_lookup_mime_subtype(is_opbx_format, format);
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

struct opbx_rtp *opbx_rtp_new_with_bindaddr(struct sched_context *sched, struct io_context *io, int rtcpenable, int callbackmode, struct in_addr addr)
{
    struct opbx_rtp *rtp;

    if ((rtp = malloc(sizeof(*rtp))) == NULL)
        return NULL;
    memset(rtp, 0, sizeof(struct opbx_rtp));

    if (sched  &&  rtcpenable)
        rtp->rtp_sock_info = udp_socket_create_group_with_bindaddr(nochecksums, 2, &addr, rtpstart, rtpend);
    else
        rtp->rtp_sock_info = udp_socket_create_group_with_bindaddr(nochecksums, 1, &addr, rtpstart, rtpend);

    if (rtp->rtp_sock_info == NULL)
    {
        free(rtp);
        return NULL;
    }

    rtp->ssrc = rand();
    rtp->seqno = rand() & 0xFFFF;

    if (sched  &&  rtcpenable)
    {
        rtp->sched = sched;
        rtp->rtcp_sock_info = udp_socket_find_group_element(rtp->rtp_sock_info, 1);
    }
    if (io  &&  sched  &&  callbackmode)
    {
        /* Operate this one in a callback mode */
        rtp->sched = sched;
        rtp->io = io;
        rtp->ioid = opbx_io_add(rtp->io, udp_socket_fd(rtp->rtp_sock_info), rtpread, OPBX_IO_IN, rtp);
    }
    opbx_rtp_pt_default(rtp);
    return rtp;
}

int opbx_rtp_set_active(struct opbx_rtp *rtp, int active)
{
    if (rtp == NULL)
       return 0;

    if (rtp->sched  &&  rtp->io)
    {
        if (active)
        {
            if (rtp->ioid == NULL)
                rtp->ioid = opbx_io_add(rtp->io, udp_socket_fd(rtp->rtp_sock_info), rtpread, OPBX_IO_IN, rtp);
        }
        else
        {
            if (rtp->ioid)
            {
                opbx_io_remove(rtp->io, rtp->ioid);
                rtp->ioid = NULL;
            }
        }
    }
    return 0;
}

int opbx_rtp_settos(struct opbx_rtp *rtp, int tos)
{
    return udp_socket_set_tos(rtp->rtp_sock_info, tos);
}

void opbx_rtp_set_peer(struct opbx_rtp *rtp, struct sockaddr_in *them)
{
    struct sockaddr_in them_rtcp;
    
    udp_socket_set_them(rtp->rtp_sock_info, them);
    /* We need to cook up the RTCP address */
    memcpy(&them_rtcp, them, sizeof(them_rtcp));
    them_rtcp.sin_port = htons(ntohs(them->sin_port) + 1);
    udp_socket_set_them(rtp->rtcp_sock_info, &them_rtcp);
    rtp->rxseqno = 0;
}

void opbx_rtp_get_peer(struct opbx_rtp *rtp, struct sockaddr_in *them)
{
    memcpy(them, udp_socket_get_them(rtp->rtp_sock_info), sizeof(*them));
}

void opbx_rtp_get_us(struct opbx_rtp *rtp, struct sockaddr_in *us)
{
    memcpy(us, udp_socket_get_apparent_us(rtp->rtp_sock_info), sizeof(*us));
}

int opbx_rtp_get_stunstate(struct opbx_rtp *rtp)
{
    if (rtp)
        return udp_socket_get_stunstate(rtp->rtp_sock_info);
    return 0;
}

void opbx_rtp_stop(struct opbx_rtp *rtp)
{
    udp_socket_restart(rtp->rtp_sock_info);
    udp_socket_restart(rtp->rtcp_sock_info);
}

void opbx_rtp_reset(struct opbx_rtp *rtp)
{
    memset(&rtp->rxcore, 0, sizeof(rtp->rxcore));
    memset(&rtp->txcore, 0, sizeof(rtp->txcore));
    memset(&rtp->dtmfmute, 0, sizeof(rtp->dtmfmute));
    rtp->lastts = 0;
    rtp->senddtmf_rtphdr = 0;
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
    udp_socket_destroy_group(rtp->rtp_sock_info);
#ifdef ENABLE_SRTP
    if (rtp->srtp)
        srtp_dealloc(rtp->srtp);
#endif
    free(rtp);
}

static uint32_t calc_txstamp(struct opbx_rtp *rtp, struct timeval *delivery)
{
    struct timeval t;
    long int ms;
    
    if (opbx_tvzero(rtp->txcore))
    {
        rtp->txcore = opbx_tvnow();
        /* Round to 20ms for nice, pretty timestamps */
        rtp->txcore.tv_usec -= rtp->txcore.tv_usec % 20000;
    }
    /* Use previous txcore if available */
    t = (delivery  &&  !opbx_tvzero(*delivery))  ?  *delivery  :  opbx_tvnow();
    ms = opbx_tvdiff_ms(t, rtp->txcore);
    /* Use what we just got for next time */
    rtp->txcore = t;
    return (uint32_t) ms;
}

int opbx_rtp_sendcng(struct opbx_rtp *rtp, int level)
{
    unsigned int *rtpheader;
    int hdrlen = 12;
    int res;
    int payload;
    char data[256];
    char iabuf[INET_ADDRSTRLEN];
    const struct sockaddr_in *them;

    level = 127 - (level & 0x7F);
    payload = opbx_rtp_lookup_code(rtp, 0, OPBX_RTP_CN);

    them = udp_socket_get_them(rtp->rtp_sock_info);

    /* If we have no peer, return immediately */    
    if (them->sin_addr.s_addr == 0)
        return 0;

    rtp->dtmfmute = opbx_tvadd(opbx_tvnow(), opbx_tv(0, 500000));

    /* Get a pointer to the header */
    rtpheader = (unsigned int *) data;
    rtpheader[0] = htonl((2 << 30) | (1 << 23) | (payload << 16) | (rtp->seqno++));
    rtpheader[1] = htonl(rtp->lastts);
    rtpheader[2] = htonl(rtp->ssrc); 
    data[12] = level;
    if (them->sin_port  &&  them->sin_addr.s_addr)
    {
        res = rtp_sendto(rtp, (void *) rtpheader, hdrlen + 1, 0);
        if (res <0) 
            opbx_log(OPBX_LOG_ERROR, "RTP Comfort Noise Transmission error to %s:%d: %s\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), them->sin_addr), ntohs(them->sin_port), strerror(errno));
        if (rtp_debug_test_addr(them))
        {
            opbx_verbose("Sent Comfort Noise RTP packet to %s:%d (type %d, seq %d, ts %d, len %d)\n",
                         opbx_inet_ntoa(iabuf, sizeof(iabuf), them->sin_addr),
                         ntohs(them->sin_port),
                         payload,
                         rtp->seqno,
                         rtp->lastts,
                         res - hdrlen);
        }
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
    const struct sockaddr_in *them;

    them = udp_socket_get_them(rtp->rtp_sock_info);
    ms = calc_txstamp(rtp, &f->delivery);
    /* Default prediction */
    if (f->subclass < OPBX_FORMAT_MAX_AUDIO)
    {
        pred = rtp->lastts + f->samples;

        /* Re-calculate last TS */
        rtp->lastts = rtp->lastts + ms*8;
        if (opbx_tvzero(f->delivery))
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
                    opbx_log(OPBX_LOG_DEBUG, "Difference is %d, ms is %d\n", abs(rtp->lastts - pred), ms);
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
        if (opbx_tvzero(f->delivery))
        {
            if (abs(rtp->lastts - pred) < 7200)
            {
                rtp->lastts = pred;
                rtp->lastovidtimestamp += f->samples;
            }
            else
            {
                if (option_debug > 2)
                    opbx_log(OPBX_LOG_DEBUG, "Difference is %d, ms is %d (%d), pred/ts/samples %d/%d/%d\n", abs(rtp->lastts - pred), ms, ms * 90, rtp->lastts, pred, f->samples);
                rtp->lastovidtimestamp = rtp->lastts;
            }
        }
    }
    /* If the timestamp for non-digit packets has moved beyond the timestamp
       for digits, update the digit timestamp.
    */
    if (rtp->lastts > rtp->lastdigitts)
        rtp->lastdigitts = rtp->lastts;

    /* Take the timestamp from the callweaver frame if it has timing */
    if (f->has_timing_info)
        rtp->lastts = f->ts*8;

    /* Get a pointer to the header */
    rtpheader = (uint8_t *) (f->data - hdrlen);

    put_unaligned_uint32(rtpheader, htonl((2 << 30) | (codec << 16) | (rtp->seqno) | (mark << 23)));
    put_unaligned_uint32(rtpheader + 4, htonl(rtp->lastts));
    put_unaligned_uint32(rtpheader + 8, htonl(rtp->ssrc)); 

    if (them->sin_port  &&  them->sin_addr.s_addr)
    {
        if ((res = rtp_sendto(rtp, (void *) rtpheader, f->datalen + hdrlen, 0)) < 0)
        {
            if (!rtp->nat  ||  (rtp->nat && (opbx_test_flag(rtp, FLAG_NAT_ACTIVE) == FLAG_NAT_ACTIVE)))
            {
                opbx_log(OPBX_LOG_WARNING, "RTP Transmission error of packet %d to %s:%d: %s\n", rtp->seqno, opbx_inet_ntoa(iabuf, sizeof(iabuf), them->sin_addr), ntohs(them->sin_port), strerror(errno));
            }
            else if ((opbx_test_flag(rtp, FLAG_NAT_ACTIVE) == FLAG_NAT_INACTIVE) || rtpdebug)
            {
                /* Only give this error message once if we are not RTP debugging */
                if (option_debug  ||  rtpdebug)
                    opbx_log(OPBX_LOG_DEBUG, "RTP NAT: Can't write RTP to private address %s:%d, waiting for other end to send audio...\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), them->sin_addr), ntohs(them->sin_port));
                opbx_set_flag(rtp, FLAG_NAT_INACTIVE_NOWARN);
            }
        }
                
        if (rtp_debug_test_addr(them))
        {
            opbx_verbose("Sent RTP packet to %s:%d (type %d, seq %d, ts %d, len %d)\n",
                         opbx_inet_ntoa(iabuf, sizeof(iabuf), them->sin_addr),
                         ntohs(them->sin_port),
                         codec,
                         rtp->seqno,
                         rtp->lastts,
                         res - hdrlen);
        }
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

    /* If there is no data length, return immediately */
    if (!_f->datalen) 
        return 0;
    
    /* If we have no peer, return immediately */    
    if (udp_socket_get_them(rtp->rtp_sock_info)->sin_addr.s_addr == 0)
        return 0;

    /* Make sure we have enough space for RTP header */
    if ((_f->frametype != OPBX_FRAME_VOICE)  &&  (_f->frametype != OPBX_FRAME_VIDEO))
    {
        opbx_log(OPBX_LOG_WARNING, "RTP can only send voice\n");
        return -1;
    }

    subclass = _f->subclass;
    if (_f->frametype == OPBX_FRAME_VIDEO)
        subclass &= ~0x1;

    if ((codec = opbx_rtp_lookup_code(rtp, 1, subclass)) < 0)
    {
        opbx_log(OPBX_LOG_WARNING, "Don't know how to send format %s packets with RTP\n", opbx_getformatname(_f->subclass));
        return -1;
    }

    if (rtp->lasttxformat != subclass)
    {
        /* New format, reset the smoother */
        if (option_debug)
            opbx_log(OPBX_LOG_DEBUG, "Ooh, format changed from %s to %s\n", opbx_getformatname(rtp->lasttxformat), opbx_getformatname(subclass));
        rtp->lasttxformat = subclass;
        if (rtp->smoother)
            opbx_smoother_free(rtp->smoother);
        rtp->smoother = NULL;
    }
    
    if (!rtp->smoother)
    {
        struct rtp_codec_table *ent;
        int ms = rtp->framems;
        int len;

        if ((ent = lookup_rtp_smoother_codec(subclass, &rtp->framems, &len)))
        {
            if (rtp->framems != ms)
                opbx_log(OPBX_LOG_DEBUG, "Had to change frame MS from %d to %d\n", ms, rtp->framems);
            if (!(rtp->smoother = opbx_smoother_new(len)))
            {
                opbx_log(OPBX_LOG_WARNING, "Unable to create smoother ms: %d len: %d:(\n", rtp->framems, len);
                return -1;
            }

            if (ent->flags)
                opbx_smoother_set_flags(rtp->smoother, ent->flags);            
            opbx_log(OPBX_LOG_DEBUG, "Able to create smoother :) ms: %d len %d\n", rtp->framems, len);
        }
    }

    if (rtp->smoother)
    {
        if (opbx_smoother_test_flag(rtp->smoother, OPBX_SMOOTHER_FLAG_BE))
            opbx_smoother_feed_be(rtp->smoother, _f);
        else
            opbx_smoother_feed(rtp->smoother, _f);
        while ((f = opbx_smoother_read(rtp->smoother)))
            opbx_rtp_raw_write(rtp, f, codec);
    }
    else
    {
        /* Don't buffer outgoing frames; send them one-per-packet: */
        if (_f->offset < hdrlen)
        {
            if ((f = opbx_frdup(_f)))
            {
                opbx_rtp_raw_write(rtp, f, codec);
                opbx_fr_free(f);
            }
        }
        else
        {
           opbx_rtp_raw_write(rtp, _f, codec);
        }
    }
    return 0;
}

/*--- opbx_rtp_proto_unregister: Unregister interface to channel driver */
void opbx_rtp_proto_unregister(struct opbx_rtp_protocol *proto)
{
    struct opbx_rtp_protocol *cur;
    struct opbx_rtp_protocol *prev;

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

/*--- opbx_rtp_proto_register: Register interface to channel driver */
int opbx_rtp_proto_register(struct opbx_rtp_protocol *proto)
{
    struct opbx_rtp_protocol *cur;

    cur = protos;
    while (cur)
    {
        if (cur->type == proto->type)
        {
            opbx_log(OPBX_LOG_WARNING, "Tried to register same protocol '%s' twice\n", cur->type);
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
    while (cur)
    {
        if (cur->type == chan->type)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

/* opbx_rtp_bridge: Bridge calls. If possible and allowed, initiate
   re-invite so the peers exchange media directly outside 
   of CallWeaver. */
enum opbx_bridge_result opbx_rtp_bridge(struct opbx_channel *c0, struct opbx_channel *c1, int flags, struct opbx_frame **fo, struct opbx_channel **rc, int timeoutms)
{
    struct opbx_frame *f;
    struct opbx_channel *who;
    struct opbx_channel *cs[3];
    struct opbx_rtp *p0;        /* Audio RTP Channels */
    struct opbx_rtp *p1;        /* Audio RTP Channels */
    struct opbx_rtp *vp0;        /* Video RTP channels */
    struct opbx_rtp *vp1;        /* Video RTP channels */
    struct opbx_rtp_protocol *pr0;
    struct opbx_rtp_protocol *pr1;
    struct sockaddr_in ac0;
    struct sockaddr_in ac1;
    struct sockaddr_in vac0;
    struct sockaddr_in vac1;
    struct sockaddr_in t0;
    struct sockaddr_in t1;
    struct sockaddr_in vt0;
    struct sockaddr_in vt1;
    char iabuf[INET_ADDRSTRLEN];
    void *pvt0;
    void *pvt1;
    int codec0;
    int codec1;
    int oldcodec0;
    int oldcodec1;
    
    memset(&vt0, 0, sizeof(vt0));
    memset(&vt1, 0, sizeof(vt1));
    memset(&vac0, 0, sizeof(vac0));
    memset(&vac1, 0, sizeof(vac1));

    /* If we need DTMF, we can't do a native bridge */
    if ((flags & (OPBX_BRIDGE_DTMF_CHANNEL_0 | OPBX_BRIDGE_DTMF_CHANNEL_1)))
        return OPBX_BRIDGE_FAILED_NOWARN;

    /* Lock channels */
    opbx_mutex_lock(&c0->lock);
    while (opbx_mutex_trylock(&c1->lock))
    {
        opbx_mutex_unlock(&c0->lock);
        usleep(1);
        opbx_mutex_lock(&c0->lock);
    }

    /* Find channel driver interfaces */
    pr0 = get_proto(c0);
    pr1 = get_proto(c1);
    if (!pr0)
    {
        opbx_log(OPBX_LOG_WARNING, "Can't find native functions for channel '%s'\n", c0->name);
        opbx_mutex_unlock(&c0->lock);
        opbx_mutex_unlock(&c1->lock);
        return OPBX_BRIDGE_FAILED;
    }
    if (!pr1)
    {
        opbx_log(OPBX_LOG_WARNING, "Can't find native functions for channel '%s'\n", c1->name);
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
    if (!p0  ||  !p1)
    {
        /* Somebody doesn't want to play... */
        opbx_mutex_unlock(&c0->lock);
        opbx_mutex_unlock(&c1->lock);
        return OPBX_BRIDGE_FAILED_NOWARN;
    }

#ifdef ENABLE_SRTP
    if (p0->srtp  ||  p1->srtp)
    {
        opbx_log(OPBX_LOG_NOTICE, "Cannot native bridge in SRTP.\n");
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
    if (pr0->get_codec  &&  pr1->get_codec)
    {
        /* Hey, we can't do reinvite if both parties speak different codecs */
        if (!(codec0 & codec1))
        {
            if (option_debug)
                opbx_log(OPBX_LOG_DEBUG, "Channel codec0 = %d is not codec1 = %d, cannot native bridge in RTP.\n", codec0, codec1);
            opbx_mutex_unlock(&c0->lock);
            opbx_mutex_unlock(&c1->lock);
            return OPBX_BRIDGE_FAILED_NOWARN;
        }
    }

    /* Ok, we should be able to redirect the media. Start with one channel */
    if (pr0->set_rtp_peer(c0, p1, vp1, codec1, opbx_test_flag(p1, FLAG_NAT_ACTIVE)))
    {
        opbx_log(OPBX_LOG_WARNING, "Channel '%s' failed to talk to '%s'\n", c0->name, c1->name);
    }
    else
    {
        /* Store RTP peer */
        opbx_rtp_get_peer(p1, &ac1);
        if (vp1)
            opbx_rtp_get_peer(vp1, &vac1);
    }
    /* Then test the other channel */
    if (pr1->set_rtp_peer(c1, p0, vp0, codec0, opbx_test_flag(p0, FLAG_NAT_ACTIVE)))
    {
        opbx_log(OPBX_LOG_WARNING, "Channel '%s' failed to talk back to '%s'\n", c1->name, c0->name);
    }
    else
    {
        /* Store RTP peer */
        opbx_rtp_get_peer(p0, &ac0);
        if (vp0)
            opbx_rtp_get_peer(vp0, &vac0);
    }
    opbx_mutex_unlock(&c0->lock);
    opbx_mutex_unlock(&c1->lock);
    /* External RTP Bridge up, now loop and see if something happes that force us to take the
       media back to CallWeaver */
    cs[0] = c0;
    cs[1] = c1;
    cs[2] = NULL;
    oldcodec0 = codec0;
    oldcodec1 = codec1;
    for (;;)
    {
        /* Check if something changed... */
        if ((c0->tech_pvt != pvt0)
            ||
            (c1->tech_pvt != pvt1)
            ||
            (c0->masq  ||  c0->masqr  ||  c1->masq  ||  c1->masqr))
        {
            opbx_log(OPBX_LOG_DEBUG, "Oooh, something is weird, backing out\n");
            if (c0->tech_pvt == pvt0)
            {
                if (pr0->set_rtp_peer(c0, NULL, NULL, 0, 0)) 
                    opbx_log(OPBX_LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c0->name);
            }
            if (c1->tech_pvt == pvt1)
            {
                if (pr1->set_rtp_peer(c1, NULL, NULL, 0, 0)) 
                    opbx_log(OPBX_LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c1->name);
            }
            return OPBX_BRIDGE_RETRY;
        }
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
        if (inaddrcmp(&t1, &ac1)  ||  (vp1  &&  inaddrcmp(&vt1, &vac1))  ||  (codec1 != oldcodec1))
        {
            if (option_debug > 1)
            {
                opbx_log(OPBX_LOG_DEBUG, "Oooh, '%s' changed end address to %s:%d (format %d)\n", 
                    c1->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), t1.sin_addr), ntohs(t1.sin_port), codec1);
                opbx_log(OPBX_LOG_DEBUG, "Oooh, '%s' changed end vaddress to %s:%d (format %d)\n", 
                    c1->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), vt1.sin_addr), ntohs(vt1.sin_port), codec1);
                opbx_log(OPBX_LOG_DEBUG, "Oooh, '%s' was %s:%d/(format %d)\n", 
                    c1->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), ac1.sin_addr), ntohs(ac1.sin_port), oldcodec1);
                opbx_log(OPBX_LOG_DEBUG, "Oooh, '%s' was %s:%d/(format %d)\n", 
                    c1->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), vac1.sin_addr), ntohs(vac1.sin_port), oldcodec1);
            }
            if (pr0->set_rtp_peer(c0, t1.sin_addr.s_addr ? p1 : NULL, vt1.sin_addr.s_addr ? vp1 : NULL, codec1, opbx_test_flag(p1, FLAG_NAT_ACTIVE))) 
                opbx_log(OPBX_LOG_WARNING, "Channel '%s' failed to update to '%s'\n", c0->name, c1->name);
            memcpy(&ac1, &t1, sizeof(ac1));
            memcpy(&vac1, &vt1, sizeof(vac1));
            oldcodec1 = codec1;
        }
        if (inaddrcmp(&t0, &ac0) || (vp0 && inaddrcmp(&vt0, &vac0)))
        {
            if (option_debug)
            {
                opbx_log(OPBX_LOG_DEBUG, "Oooh, '%s' changed end address to %s:%d (format %d)\n", 
                    c0->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), t0.sin_addr), ntohs(t0.sin_port), codec0);
                opbx_log(OPBX_LOG_DEBUG, "Oooh, '%s' was %s:%d/(format %d)\n", 
                    c0->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), ac0.sin_addr), ntohs(ac0.sin_port), oldcodec0);
            }
            if (pr1->set_rtp_peer(c1, t0.sin_addr.s_addr ? p0 : NULL, vt0.sin_addr.s_addr ? vp0 : NULL, codec0, opbx_test_flag(p0, FLAG_NAT_ACTIVE)))
                opbx_log(OPBX_LOG_WARNING, "Channel '%s' failed to update to '%s'\n", c1->name, c0->name);
            memcpy(&ac0, &t0, sizeof(ac0));
            memcpy(&vac0, &vt0, sizeof(vac0));
            oldcodec0 = codec0;
        }
        if ((who = opbx_waitfor_n(cs, 2, &timeoutms)) == 0)
        {
            if (!timeoutms) 
                return OPBX_BRIDGE_RETRY;
            if (option_debug)
                opbx_log(OPBX_LOG_DEBUG, "Ooh, empty read...\n");
            /* check for hangup / whentohangup */
            if (opbx_check_hangup(c0) || opbx_check_hangup(c1))
                break;
            continue;
        }
        f = opbx_read(who);
        if (f == NULL
            ||
                ((f->frametype == OPBX_FRAME_DTMF)
                &&
                (((who == c0)  &&  (flags & OPBX_BRIDGE_DTMF_CHANNEL_0))
            || 
            ((who == c1)  &&  (flags & OPBX_BRIDGE_DTMF_CHANNEL_1)))))
        {
            *fo = f;
            *rc = who;
            if (option_debug)
                opbx_log(OPBX_LOG_DEBUG, "Oooh, got a %s\n", f  ?  "digit"  :  "hangup");
            if ((c0->tech_pvt == pvt0)  &&  (!c0->_softhangup))
            {
                if (pr0->set_rtp_peer(c0, NULL, NULL, 0, 0)) 
                    opbx_log(OPBX_LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c0->name);
            }
            if ((c1->tech_pvt == pvt1)  &&  (!c1->_softhangup))
            {
                if (pr1->set_rtp_peer(c1, NULL, NULL, 0, 0)) 
                    opbx_log(OPBX_LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c1->name);
            }
            return OPBX_BRIDGE_COMPLETE;
        }
        else if ((f->frametype == OPBX_FRAME_CONTROL)  &&  !(flags & OPBX_BRIDGE_IGNORE_SIGS))
        {
            if ((f->subclass == OPBX_CONTROL_HOLD)
                ||
                (f->subclass == OPBX_CONTROL_UNHOLD)
                ||
                (f->subclass == OPBX_CONTROL_VIDUPDATE))
            {
                opbx_indicate((who == c0)  ?  c1  :  c0, f->subclass);
                opbx_fr_free(f);
            }
            else
            {
                *fo = f;
                *rc = who;
                opbx_log(OPBX_LOG_DEBUG, "Got a FRAME_CONTROL (%d) frame on channel %s\n", f->subclass, who->name);
                return OPBX_BRIDGE_COMPLETE;
            }
        }
        else
        {
            if ((f->frametype == OPBX_FRAME_DTMF)
                ||
                (f->frametype == OPBX_FRAME_VOICE)
                ||
                (f->frametype == OPBX_FRAME_VIDEO))
            {
                /* Forward voice or DTMF frames if they happen upon us */
                if (who == c0)
                    opbx_write(c1, f);
                else if (who == c1)
                    opbx_write(c0, f);
            }
            opbx_fr_free(f);
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
    char *p;
    char *arg;

    if (argc != 4)
        return RESULT_SHOWUSAGE;
    arg = argv[3];
    if ((p = strstr(arg, ":")))
    {
        *p = '\0';
        p++;
        port = atoi(p);
    }
    if ((hp = opbx_gethostbyname(arg, &ahp)) == NULL)
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
    if (argc != 2)
    {
        if (argc != 4)
            return RESULT_SHOWUSAGE;
        return rtp_do_debug_ip(fd, argc, argv);
    }
    rtpdebug = 1;
    memset(&rtpdebugaddr, 0, sizeof(rtpdebugaddr));
    opbx_cli(fd, "RTP Debugging Enabled\n");
    return RESULT_SUCCESS;
}
   
static int rtp_no_debug(int fd, int argc, char *argv[])
{
    if (argc !=3)
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

static struct opbx_clicmd  cli_debug_ip = {
	.cmda = { "rtp", "debug", "ip", NULL },
	.handler = rtp_do_debug,
	.summary = "Enable RTP debugging on IP",
	.usage = debug_usage,
};

static struct opbx_clicmd  cli_debug = {
	.cmda = { "rtp", "debug", NULL },
	.handler = rtp_do_debug,
	.summary = "Enable RTP debugging",
	.usage = debug_usage,
};

static struct opbx_clicmd  cli_no_debug = {
	.cmda = { "rtp", "no", "debug", NULL },
	.handler = rtp_no_debug,
	.summary = "Disable RTP debugging",
	.usage = no_debug_usage,
};

void opbx_rtp_reload(void)
{
    struct opbx_config *cfg;
    char *s;

    /* Set defaults */
    rtpstart = DEFAULT_RTPSTART;
    rtpend = DEFAULT_RTPEND;
    dtmftimeout = DEFAULT_DTMFTIMEOUT;

    cfg = opbx_config_load("rtp.conf");
    if (cfg)
    {
        if ((s = opbx_variable_retrieve(cfg, "general", "rtpstart")))
        {
            rtpstart = atoi(s);
            if (rtpstart < 1024)
                rtpstart = 1024;
            if (rtpstart > 65535)
                rtpstart = 65535;
        }
        if ((s = opbx_variable_retrieve(cfg, "general", "rtpend")))
        {
            rtpend = atoi(s);
            if (rtpend < 1024)
                rtpend = 1024;
            if (rtpend > 65535)
                rtpend = 65535;
        }
        if ((s = opbx_variable_retrieve(cfg, "general", "dtmftimeout")))
        {
            dtmftimeout = atoi(s);
            if (dtmftimeout <= 1)
            {
                opbx_log(OPBX_LOG_WARNING, "Invalid dtmftimeout given: %d, using default value %d", dtmftimeout, DEFAULT_DTMFTIMEOUT);
                dtmftimeout = DEFAULT_DTMFTIMEOUT;
            }
        }
        if ((s = opbx_variable_retrieve(cfg, "general", "rtpchecksums")))
        {
#ifdef SO_NO_CHECK
            if (opbx_false(s))
                nochecksums = 1;
            else
                nochecksums = 0;
#else
            if (opbx_false(s))
                opbx_log(OPBX_LOG_WARNING, "Disabling RTP checksums is not supported on this operating system!\n");
#endif
        }
        opbx_config_destroy(cfg);
    }
    if (rtpstart >= rtpend)
    {
        opbx_log(OPBX_LOG_WARNING, "Unreasonable values for RTP start/end port in rtp.conf\n");
        rtpstart = 5000;
        rtpend = 31000;
    }
    if (option_verbose > 1)
        opbx_verbose(VERBOSE_PREFIX_2 "RTP Allocating from port range %d -> %d\n", rtpstart, rtpend);
}

void opbx_rtp_init(void)
{
    opbx_cli_register(&cli_debug);
    opbx_cli_register(&cli_debug_ip);
    opbx_cli_register(&cli_no_debug);
    opbx_rtp_reload();
#ifdef ENABLE_SRTP
    opbx_log(OPBX_LOG_NOTICE, "srtp_init\n");
    srtp_init();
    srtp_install_event_handler(srtp_event_cb);
#endif
}
