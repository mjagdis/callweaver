#define USE_VALE
/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * UDPTL support for T.38
 * 
 * Copyright (C) 2005, Steve Underwood, partly based on RTP code which is
 * Copyright (C) 1999-2004, Digium, Inc.
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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <vale/udptl.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include <callweaver/udp.h>
#include "callweaver/udptl.h"
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

#define UDPTL_MTU        1200

#if !defined(FALSE)
#define FALSE 0
#endif
#if !defined(TRUE)
#define TRUE (!FALSE)
#endif

static int udptldebug = 0;                  /* Are we debugging? */
static struct cw_sockaddr_net udptldebugaddr;   /* Debug packets to/from this host */

CW_MUTEX_DEFINE_STATIC(settingslock);
static int nochecksums = 0;
static int udptlfectype = UDPTL_ERROR_CORRECTION_NONE;
static int udptlfecentries = 0;
static int udptlfecspan = 0;
static int udptlmaxdatagram = 0;

#ifndef LOCAL_FAX_MAX_FEC_PACKETS
#  define LOCAL_FAX_MAX_FEC_PACKETS   4
#endif
#define LOCAL_FAX_MAX_DATAGRAM      400
#define LOCAL_FAX_MAX_FEC_SPAN      4

#define UDPTL_BUF_MASK              15

struct cw_udptl_s
{
    udp_state_t *udptl_sock_info;
    char resp;
    struct cw_frame f[16];
    int f_no;
    uint8_t rawdata[8192 + CW_FRIENDLY_OFFSET];
    uint32_t lasteventseqn;
    int nat;
    int flags;
    struct cw_io_rec ioid;
    void *data;
    int udptl_offered_from_local;
    int verbose;

    udptl_state_t state;
};

static struct cw_udptl_protocol *protos = NULL;

static int cw_udptl_rx_packet(cw_udptl_t *s, const uint8_t buf[], int len);

static inline int udptl_debug_test_addr(const struct sockaddr *addr)
{
    if (udptldebug == 0 || cw_sockaddr_cmp(addr, &udptldebugaddr.sa, -1, cw_sockaddr_get_port(&udptldebugaddr.sa)))
        return 0;

    return 1;
}

static int udptl_rx_packet_handler(void *user_data, const uint8_t msg[], int len, int seq_no)
{
    cw_udptl_t *s;
    
    s = (cw_udptl_t *) user_data;
    s->f[s->f_no].frametype = CW_FRAME_MODEM;
    s->f[s->f_no].subclass = CW_MODEM_T38;
    s->f[s->f_no].mallocd = 0;

    s->f[s->f_no].seq_no = seq_no;
    s->f[s->f_no].tx_copies = 1;
    s->f[s->f_no].datalen = len;
    s->f[s->f_no].data = (uint8_t *) msg;
    s->f[s->f_no].offset = 0;
    if (s->f_no > 0)
    {
        s->f[s->f_no].prev = &s->f[s->f_no - 1];
        s->f[s->f_no - 1].next = &s->f[s->f_no];
    }
    s->f[s->f_no].next = NULL;
    s->f_no++;
    return 0;
}

static int cw_udptl_rx_packet(cw_udptl_t *s, const uint8_t buf[], int len)
{
    s->f[0].prev = NULL;
    s->f[0].next = NULL;
    s->f_no = 0;
    udptl_rx_packet(&s->state, buf, len);
    return 0;
}


void cw_udptl_setnat(cw_udptl_t *udptl, int nat)
{
    udptl->nat = nat;
    udp_socket_set_nat(udptl->udptl_sock_info, nat);
}

struct cw_frame *cw_udptl_read(cw_udptl_t *udptl)
{
    struct cw_sockaddr_net original_dest;
    int res;
    int actions;

    cw_sockaddr_copy(&original_dest.sa, &udptl->udptl_sock_info->peer.sa);

    /* Cache where the header will go */
    res = udp_socket_recv(udptl->udptl_sock_info,
                              udptl->rawdata + CW_FRIENDLY_OFFSET,
                              sizeof(udptl->rawdata) - CW_FRIENDLY_OFFSET,
                              0,
                              &actions);
    if (res < 0)
    {
        if (errno != EAGAIN)
            cw_log(CW_LOG_WARNING, "UDPTL read error: %s\n", strerror(errno));
        return &cw_null_frame;
    }
    if ((actions & 1))
    {
        if (option_debug || udptldebug)
            cw_log(CW_LOG_DEBUG, "UDPTL NAT: Using address %l@\n", &udptl->udptl_sock_info->peer.sa);
    }

    if (udptl_debug_test_addr(&udptl->udptl_sock_info->peer.sa))
    {
        cw_verbose("Got UDPTL packet from %l@ (len %d)\n", &udptl->udptl_sock_info->peer.sa, res);
    }
    /* If its not a valid UDPTL packet, restore the original port */
    if (cw_udptl_rx_packet(udptl, udptl->rawdata + CW_FRIENDLY_OFFSET, res) < 0)
        udp_socket_set_far(udptl->udptl_sock_info, &original_dest.sa);

    return &udptl->f[0];
}

void cw_udptl_offered_from_local(cw_udptl_t *udptl, int local)
{
    if (udptl)
        udptl->udptl_offered_from_local = local;
    else
        cw_log(CW_LOG_WARNING, "udptl structure is null\n");
}

int cw_udptl_get_preferred_error_correction_scheme(cw_udptl_t *udptl)
{
    int ret;

    CW_UNUSED(udptl);

    cw_mutex_lock(&settingslock);
    ret = udptlfectype;
    cw_mutex_unlock(&settingslock);
    return ret;
}

int cw_udptl_get_current_error_correction_scheme(cw_udptl_t *udptl)
{
    int ec_scheme;

    if (udptl)
    {
        udptl_get_error_correction(&udptl->state,
                                   &ec_scheme,
                                   NULL,
                                   NULL);
        return ec_scheme;
    }
    cw_log(CW_LOG_WARNING, "udptl structure is null\n");
    return -1;
}

void cw_udptl_set_error_correction_scheme(cw_udptl_t *udptl, int ec)
{
    if (udptl)
    {
        switch (ec)
        {
        case UDPTL_ERROR_CORRECTION_FEC:
        case UDPTL_ERROR_CORRECTION_REDUNDANCY:
        case UDPTL_ERROR_CORRECTION_NONE:
            udptl_set_error_correction(&udptl->state,
                                       ec,
                                       udptlfecspan,
                                       udptlfecentries);
            break;
        default:
            cw_log(CW_LOG_WARNING, "error correction parameter invalid");
            break;
        }
    }
    else
    {
        cw_log(CW_LOG_WARNING, "udptl structure is null\n");
    }
}

int cw_udptl_get_local_max_datagram(cw_udptl_t *udptl)
{
    if (udptl)
        return udptl_get_local_max_datagram(&udptl->state);
    cw_log(CW_LOG_WARNING, "udptl structure is null\n");
    return -1;
}

int cw_udptl_get_far_max_datagram(cw_udptl_t *udptl)
{
    if (udptl)
        return udptl_get_far_max_datagram(&udptl->state);
    cw_log(CW_LOG_WARNING, "udptl structure is null\n");
    return -1;
}

void cw_udptl_set_local_max_datagram(cw_udptl_t *udptl, int max_datagram)
{
    if (udptl)
        udptl_set_local_max_datagram(&udptl->state, max_datagram);
    else
        cw_log(CW_LOG_WARNING, "udptl structure is null\n");
}

void cw_udptl_set_far_max_datagram(cw_udptl_t *udptl, int max_datagram)
{
    if (udptl)
        udptl_set_far_max_datagram(&udptl->state, max_datagram);
    else
        cw_log(CW_LOG_WARNING, "udptl structure is null\n");
}

cw_udptl_t *cw_udptl_new_with_sock_info(udp_state_t *sock_info)
{
	cw_udptl_t *udptl = NULL;

	if ((udptl = calloc(1, sizeof(cw_udptl_t)))) {
		udptl_init(&udptl->state, udptlfectype, udptlfecspan, udptlfecentries, udptl_rx_packet_handler, (void *)udptl);

		udptl_set_far_max_datagram(&udptl->state, udptlmaxdatagram);
		udptl_set_local_max_datagram(&udptl->state, udptlmaxdatagram);

		/* This sock_info should already be bound to an address */
		udptl->udptl_sock_info = sock_info;
	} else
		cw_log(CW_LOG_ERROR, "Out of memory\n");

	return udptl;
}

void cw_udptl_destroy(cw_udptl_t *udptl)
{
	free(udptl);
}

int cw_udptl_settos(cw_udptl_t *udptl, int tos)
{
    return udp_socket_set_tos(udptl->udptl_sock_info, tos);
}

void cw_udptl_set_peer(cw_udptl_t *udptl, struct sockaddr *them)
{
    udp_socket_set_far(udptl->udptl_sock_info, them);
}

struct sockaddr *cw_udptl_get_peer(cw_udptl_t *udptl)
{
    return &udptl->udptl_sock_info->peer.sa;
}

struct sockaddr *cw_udptl_get_us(cw_udptl_t *udptl)
{
    return udp_socket_get_apparent_local(udptl->udptl_sock_info);
}

void cw_udptl_stop(cw_udptl_t *udptl)
{
    udp_socket_restart(udptl->udptl_sock_info);
}

int cw_udptl_write(cw_udptl_t *s, struct cw_frame *f)
{
    uint8_t buf[LOCAL_FAX_MAX_DATAGRAM];
    const struct sockaddr *them;
    int len;
    int copies;
    int i;

    them = &s->udptl_sock_info->peer.sa;

    /* If we have no peer, return immediately */    
    if (them->sa_family == AF_UNSPEC)
        return 0;

    /* If there is no data length, return immediately */
    if (f->datalen == 0)
        return 0;
    
    if (f->frametype != CW_FRAME_MODEM)
    {
        cw_log(CW_LOG_WARNING, "UDPTL can only send T.38 data\n");
        return -1;
    }
    /* Cook up the UDPTL packet, with the relevant EC info. */
    len = udptl_build_packet(&s->state, buf, f->data, f->datalen);

    if (len > 0)
    {
#if 0
        printf("Sending %d copies of %d bytes of UDPTL data to %l@\n", f->state.tx_copies, len, them);
#endif
        copies = (f->tx_copies > 0)  ?  f->tx_copies  :  1;
        for (i = 0;  i < copies;  i++)
        {
            if (udp_socket_send(s->udptl_sock_info, buf, len, 0) < 0)
                cw_log(CW_LOG_NOTICE, "UDPTL Transmission error to %l@: %s\n", them, strerror(errno));
        }

        if (udptl_debug_test_addr(them))
        {
            cw_verbose("Sent UDPTL packet to %l@ (seq %d, len %d)\n",
                         them,
                         (s->state.tx_seq_no - 1) & 0xFFFF,
                         len);
        }
    }
    return 0;
}

void cw_udptl_proto_unregister(struct cw_udptl_protocol *proto)
{
    struct cw_udptl_protocol *cur;
    struct cw_udptl_protocol *prev;

    cw_log(CW_LOG_NOTICE,"Unregistering UDPTL protocol.\n");
    for (cur = protos, prev = NULL;  cur;  prev = cur, cur = cur->next)
    {
        if (cur == proto)
        {
            if (prev)
                prev->next = proto->next;
            else
                protos = proto->next;
            return;
        }
    }
}

int cw_udptl_proto_register(struct cw_udptl_protocol *proto)
{
    struct cw_udptl_protocol *cur;

    cw_log(CW_LOG_NOTICE,"Registering UDPTL protocol.\n");
    for (cur = protos;  cur;  cur = cur->next)
    {
        if (cur->type == proto->type)
        {
            cw_log(CW_LOG_WARNING, "Tried to register same protocol '%s' twice\n", cur->type);
            return -1;
        }
    }
    proto->next = protos;
    protos = proto;
    return 0;
}

static struct cw_udptl_protocol *get_proto(struct cw_channel *chan)
{
    struct cw_udptl_protocol *cur;

    for (cur = protos;  cur;  cur = cur->next)
    {
        if (cur->type == chan->type)
            return cur;
    }
    return NULL;
}

enum cw_bridge_result cw_udptl_bridge(struct cw_channel *c0, struct cw_channel *c1, int flags, struct cw_frame **fo, struct cw_channel **rc)
{
    struct cw_frame *f;
    struct cw_channel *who;
    struct cw_channel *cs[3];
    cw_udptl_t *p0;
    cw_udptl_t *p1;
    struct cw_udptl_protocol *pr0;
    struct cw_udptl_protocol *pr1;
    struct cw_sockaddr_net ac0;
    struct cw_sockaddr_net ac1;
    struct cw_sockaddr_net t0;
    struct cw_sockaddr_net t1;
    void *pvt0;
    void *pvt1;
    int to;
    
    CW_UNUSED(flags);

    cw_channel_lock(c0);
    while (cw_channel_trylock(c1))
    {
        cw_channel_unlock(c0);
        usleep(1);
        cw_channel_lock(c0);
    }
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
    pvt0 = c0->tech_pvt;
    pvt1 = c1->tech_pvt;
    p0 = pr0->get_udptl_info(c0);
    p1 = pr1->get_udptl_info(c1);
    if (!p0  ||  !p1)
    {
        /* Somebody doesn't want to play... */
        cw_channel_unlock(c0);
        cw_channel_unlock(c1);
        return CW_BRIDGE_FAILED_NOWARN;
    }
    if (pr0->set_udptl_peer(c0, p1))
    {
        cw_log(CW_LOG_WARNING, "Channel '%s' failed to talk to '%s'\n", c0->name, c1->name);
    }
    else
    {
        /* Store UDPTL peer */
        cw_sockaddr_copy(&ac1.sa, cw_udptl_get_peer(p1));
    }
    if (pr1->set_udptl_peer(c1, p0))
    {
        cw_log(CW_LOG_WARNING, "Channel '%s' failed to talk back to '%s'\n", c1->name, c0->name);
    }
    else
    {
        /* Store UDPTL peer */
        cw_sockaddr_copy(&ac0.sa, cw_udptl_get_peer(p0));
    }
    cw_channel_unlock(c0);
    cw_channel_unlock(c1);
    cs[0] = c0;
    cs[1] = c1;
    cs[2] = NULL;
    for (;;)
    {
        if ((c0->tech_pvt != pvt0)
            ||
            (c1->tech_pvt != pvt1)
            ||
            (c0->masq  ||  c0->masqr  ||  c1->masq  ||  c1->masqr))
        {
            cw_log(CW_LOG_DEBUG, "Oooh, something is weird, backing out\n");
            /* Tell it to try again later */
            return CW_BRIDGE_RETRY;
        }
        to = -1;
        cw_sockaddr_copy(&t1.sa, cw_udptl_get_peer(p1));
        cw_sockaddr_copy(&t0.sa, cw_udptl_get_peer(p0));
        if (cw_sockaddr_cmp(&t1.sa, &ac1.sa, -1, 1))
        {
            cw_log(CW_LOG_DEBUG, "Oooh, '%s' changed end address to %l@\n", c1->name, &t1.sa);
            cw_log(CW_LOG_DEBUG, "Oooh, '%s' was %l@\n", c1->name, &ac1.sa);
        }
        if (cw_sockaddr_cmp(&t0.sa, &ac0.sa, -1, 1))
        {
            cw_log(CW_LOG_DEBUG, "Oooh, '%s' changed end address to %l@\n", c0->name, &t0.sa);
            cw_log(CW_LOG_DEBUG, "Oooh, '%s' was %l@\n", c0->name, &ac0.sa);
        }
        if ((who = cw_waitfor_n(cs, 2, &to)) == 0)
        {
            cw_log(CW_LOG_DEBUG, "Ooh, empty read...\n");
            /* Check for hangup / whentohangup */
            if (cw_check_hangup(c0)  ||  cw_check_hangup(c1))
                break;
            continue;
        }
        if ((f = cw_read(who)) == 0)
        {
            *fo = f;
            *rc = who;
            cw_log(CW_LOG_DEBUG, "Oooh, got a %s\n", (f)  ?  "digit"  :  "hangup");
            /* That's all we needed */
            return CW_BRIDGE_COMPLETE;
        }
        if (f->frametype == CW_FRAME_MODEM)
        {
            /* Forward T.38 frames if they happen upon us */
            if (who == c0)
                cw_write(c1, &f);
            else if (who == c1)
                cw_write(c0, &f);
        }
        cw_fr_free(f);
        /* Swap priority. Not that it's a big deal at this point */
        cs[2] = cs[0];
        cs[0] = cs[1];
        cs[1] = cs[2];
    }
    return CW_BRIDGE_FAILED;
}

static int udptl_do_debug_ip(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    struct addrinfo *addrs;
    int err;

    if (argc != 4)
        return RESULT_SHOWUSAGE;

    if (!(err = cw_getaddrinfo(argv[3], "0", NULL, &addrs, NULL))) {
        memcpy(&udptldebugaddr, addrs->ai_addr, addrs->ai_addrlen);
        cw_dynstr_printf(ds_p, "UDPTL debugging enabled for IP: %l@\n", udptldebugaddr);
        udptldebug = 1;
    } else
        cw_log(CW_LOG_WARNING, "%s: %s\n", argv[3], gai_strerror(err));

    return RESULT_SUCCESS;
}

static int udptl_do_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    if (argc != 2)
    {
        if (argc != 4)
            return RESULT_SHOWUSAGE;
        return udptl_do_debug_ip(ds_p, argc, argv);
    }
    udptldebug = 1;
    memset(&udptldebugaddr, 0, sizeof(udptldebugaddr));
    cw_dynstr_printf(ds_p, "UDPTL Debugging Enabled\n");
    return RESULT_SUCCESS;
}
   
static int udptl_no_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    CW_UNUSED(argv);

    if (argc != 3)
        return RESULT_SHOWUSAGE;

    udptldebug = 0;
    cw_dynstr_printf(ds_p, "UDPTL Debugging Disabled\n");
    return RESULT_SUCCESS;
}

static int udptl_reload(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    CW_UNUSED(ds_p);
    CW_UNUSED(argv);

    if (argc != 2)
        return RESULT_SHOWUSAGE;

    cw_udptl_reload();
    return RESULT_SUCCESS;
}

static int udptl_show_settings(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    const char *error_correction_str;

    CW_UNUSED(argv);

    if (argc != 3)
        return RESULT_SHOWUSAGE;

    cw_mutex_lock(&settingslock);

    if (udptlfectype == UDPTL_ERROR_CORRECTION_FEC)
        error_correction_str = "FEC";
    else if (udptlfectype == UDPTL_ERROR_CORRECTION_REDUNDANCY)
        error_correction_str = "Redundancy";
    else
        error_correction_str = "None";

    cw_dynstr_tprintf(ds_p, 8,
        cw_fmtval("\n\nUDPTL Settings:\n"),
        cw_fmtval("---------------\n"),
        cw_fmtval("Checksum UDPTL traffic: %s\n", nochecksums ? "No" : "Yes"),
        cw_fmtval("Error correction:       %s\n", error_correction_str),
        cw_fmtval("Max UDPTL packet:       %d bytes\n", udptlmaxdatagram),
        cw_fmtval("FEC entries:            %d\n", udptlfecentries),
        cw_fmtval("FEC span:               %d\n", udptlfecspan),
        cw_fmtval("\n----\n")
    );

    cw_mutex_unlock(&settingslock);

    return RESULT_SUCCESS;
}

static const char debug_usage[] =
    "Usage: udptl debug [ip host[:port]]\n"
    "       Enable dumping of all UDPTL packets to and from host.\n";

static const char no_debug_usage[] =
    "Usage: udptl no debug\n"
    "       Disable all UDPTL debugging\n";

static const char reload_usage[] =
    "Usage: udptl reload\n"
    "       Reload UDPTL settings\n";

static const char show_settings_usage[] =
    "Usage: udptl show settings\n"
    "       Show UDPTL settings\n";

static struct cw_clicmd  cli_debug_ip =
{
	.cmda = { "udptl", "debug", "ip", NULL },
	.handler = udptl_do_debug,
	.summary = "Enable UDPTL debugging on IP",
	.usage = debug_usage,
};

static struct cw_clicmd  cli_debug =
{
	.cmda = { "udptl", "debug", NULL },
	.handler = udptl_do_debug,
	.summary = "Enable UDPTL debugging",
	.usage = debug_usage,
};

static struct cw_clicmd  cli_no_debug =
{
	.cmda = { "udptl", "no", "debug", NULL },
	.handler = udptl_no_debug,
	.summary = "Disable UDPTL debugging",
	.usage = no_debug_usage,
};

static struct cw_clicmd  cli_reload =
{
	.cmda = { "udptl", "reload", NULL },
	.handler = udptl_reload,
	.summary = "Reload UDPTL settings",
	.usage = reload_usage,
};

static struct cw_clicmd  cli_show_settings =
{
	.cmda = { "udptl", "show", "settings", NULL },
	.handler = udptl_show_settings,
	.summary = "Show UDPTL settings",
	.usage = show_settings_usage,
};

void cw_udptl_reload(void)
{
    struct cw_config *cfg;
    char *s;

    cw_mutex_lock(&settingslock);

    udptlfectype = UDPTL_ERROR_CORRECTION_NONE;
    udptlfecentries = 1;
    udptlfecspan = 0;
    udptlmaxdatagram = 0;

    if ((cfg = cw_config_load("udptl.conf")))
    {
        if ((s = cw_variable_retrieve(cfg, "general", "udptlchecksums")))
        {
#ifdef SO_NO_CHECK
            if (cw_false(s))
                nochecksums = 1;
            else
                nochecksums = 0;
#else
            if (cw_false(s))
                cw_log(CW_LOG_WARNING, "Disabling UDPTL checksums is not supported on this operating system!\n");
#endif
        }
        if ((s = cw_variable_retrieve(cfg, "general", "T38FaxUdpEC")))
        {
            if (strcmp(s, "t38UDPFEC") == 0)
                udptlfectype = UDPTL_ERROR_CORRECTION_FEC;
            else if (strcmp(s, "t38UDPRedundancy") == 0)
                udptlfectype = UDPTL_ERROR_CORRECTION_REDUNDANCY;
        }
        if ((s = cw_variable_retrieve(cfg, "general", "T38FaxMaxDatagram")))
        {
            udptlmaxdatagram = atoi(s);
            if (udptlmaxdatagram < 0)
                udptlmaxdatagram = 0;
            if (udptlmaxdatagram > LOCAL_FAX_MAX_DATAGRAM)
                udptlmaxdatagram = LOCAL_FAX_MAX_DATAGRAM;
        }
        if ((s = cw_variable_retrieve(cfg, "general", "UDPTLFECentries")))
        {
            udptlfecentries = atoi(s);
            if (udptlfecentries < 0)
                udptlfecentries = 0;
            if (udptlfecentries > LOCAL_FAX_MAX_FEC_PACKETS)
                udptlfecentries = LOCAL_FAX_MAX_FEC_PACKETS;
        }
        if ((s = cw_variable_retrieve(cfg, "general", "UDPTLFECspan")))
        {
            udptlfecspan = atoi(s);
            if (udptlfecspan < 0)
                udptlfecspan = 0;
            if (udptlfecspan > LOCAL_FAX_MAX_FEC_SPAN)
                udptlfecspan = LOCAL_FAX_MAX_FEC_SPAN;
        }
        cw_config_destroy(cfg);
    }

    cw_mutex_unlock(&settingslock);
}

int cw_udptl_init(void)
{
    cw_cli_register(&cli_debug);
    cw_cli_register(&cli_debug_ip);
    cw_cli_register(&cli_no_debug);
    cw_cli_register(&cli_reload);
    cw_cli_register(&cli_show_settings);
    cw_udptl_reload();
    return 0;
}
