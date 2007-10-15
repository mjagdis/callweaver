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

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

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
#include <vale/rfc3489.h>
#include <vale/udptl.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/udp.h"
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
#include "callweaver/stun.h"

#define UDPTL_MTU        1200

#if !defined(FALSE)
#define FALSE 0
#endif
#if !defined(TRUE)
#define TRUE (!FALSE)
#endif

static int udptldebug = 0;                  /* Are we debugging? */
static struct sockaddr_in udptldebugaddr;   /* Debug packets to/from this host */
static int nochecksums = 0;
static int udptlfectype = 0;
static int udptlfecentries = 0;
static int udptlfecspan = 0;
static int udptlmaxdatagram = 0;

#define LOCAL_FAX_MAX_DATAGRAM      400
#define LOCAL_FAX_MAX_FEC_PACKETS   5
#define LOCAL_FAX_MAX_FEC_SPAN      5

#define UDPTL_BUF_MASK              15

struct opbx_udptl_s
{
    udp_state_t *udptl_sock_info;
    char resp;
    struct opbx_frame f[16];
    int f_no;
    uint8_t rawdata[8192 + OPBX_FRIENDLY_OFFSET];
    uint32_t lasteventseqn;
    int nat;
    int flags;
    int *ioid;
    struct sched_context *sched;
    struct io_context *io;
    void *data;
    opbx_udptl_callback callback;
    int udptl_offered_from_local;

    int created_sock_info;
    
    int verbose;

    struct sockaddr_in far;

    udptl_state_t state;
};

static struct opbx_udptl_protocol *protos = NULL;

static int cw_udptl_rx_packet(opbx_udptl_t *s, const uint8_t buf[], int len);

static inline int udptl_debug_test_addr(const struct sockaddr_in *addr)
{
    if (udptldebug == 0)
        return 0;
    if (udptldebugaddr.sin_addr.s_addr)
    {
        if (((ntohs(udptldebugaddr.sin_port) != 0)  &&  (udptldebugaddr.sin_port != addr->sin_port))
            ||
            (udptldebugaddr.sin_addr.s_addr != addr->sin_addr.s_addr))
        {
            return 0;
        }
    }
    return 1;
}

static int udptl_rx_packet_handler(void *user_data, const uint8_t msg[], int len, int seq_no)
{
    opbx_udptl_t *s;
    
    s = (opbx_udptl_t *) user_data;
    s->f[s->f_no].frametype = OPBX_FRAME_MODEM;
    s->f[s->f_no].subclass = OPBX_MODEM_T38;
    s->f[s->f_no].mallocd = 0;

    s->f[s->f_no].seq_no = seq_no;
    s->f[s->f_no].tx_copies = 1;
    s->f[s->f_no].datalen = len;
    s->f[s->f_no].data = (uint8_t *) msg;
    s->f[s->f_no].offset = 0;
    s->f[s->f_no].src = "UDPTL";
    if (s->f_no > 0)
    {
        s->f[s->f_no].prev = &s->f[s->f_no - 1];
        s->f[s->f_no - 1].next = &s->f[s->f_no];
    }
    s->f[s->f_no].next = NULL;
    s->f_no++;
    return 0;
}

static int cw_udptl_rx_packet(opbx_udptl_t *s, const uint8_t buf[], int len)
{
    s->f[0].prev = NULL;
    s->f[0].next = NULL;
    s->f_no = 0;
    udptl_rx_packet(&s->state, buf, len);
    return 0;
}

int opbx_udptl_fd(opbx_udptl_t *udptl)
{
    return udp_socket_fd(udptl->udptl_sock_info);
}

udp_state_t *opbx_udptl_udp_socket(opbx_udptl_t *udptl,
                                   udp_state_t *sock_info)
{
    udp_state_t *old;
    
    old = udptl->udptl_sock_info;
    if (sock_info)
        udptl->udptl_sock_info = sock_info;
    return old;
}

void opbx_udptl_set_data(opbx_udptl_t *udptl, void *data)
{
    udptl->data = data;
}

void opbx_udptl_set_callback(opbx_udptl_t *udptl, opbx_udptl_callback callback)
{
    udptl->callback = callback;
}

void opbx_udptl_setnat(opbx_udptl_t *udptl, int nat)
{
    udptl->nat = nat;
    udp_socket_set_nat(udptl->udptl_sock_info, nat);
}

static int udptlread(int *id, int fd, short events, void *cbdata)
{
    opbx_udptl_t *udptl = cbdata;
    struct opbx_frame *f;

    if ((f = opbx_udptl_read(udptl)))
    {
        if (udptl->callback)
            udptl->callback(udptl, f, udptl->data);
    }
    return 1;
}

struct opbx_frame *opbx_udptl_read(opbx_udptl_t *udptl)
{
    int res;
    int actions;
    struct sockaddr_in sin;
    socklen_t len;
    char iabuf[INET_ADDRSTRLEN];
    uint16_t *udptlheader;
    static struct opbx_frame null_frame = { OPBX_FRAME_NULL, };

    len = sizeof(sin);
    
    /* Cache where the header will go */
    res = udp_socket_recvfrom(udptl->udptl_sock_info,
                              udptl->rawdata + OPBX_FRIENDLY_OFFSET,
                              sizeof(udptl->rawdata) - OPBX_FRIENDLY_OFFSET,
                              0,
                              (struct sockaddr *) &sin,
                              &len,
                              &actions);
    udptlheader = (uint16_t *)(udptl->rawdata + OPBX_FRIENDLY_OFFSET);
    if (res < 0)
    {
        if (errno != EAGAIN)
        {
            if (errno == EBADF)
            {
                opbx_log(OPBX_LOG_ERROR, "UDPTL read error: %s\n", strerror(errno));
                opbx_udptl_set_active(udptl, 0);
            }
            else
                opbx_log(OPBX_LOG_WARNING, "UDPTL read error: %s\n", strerror(errno));
        }
        return &null_frame;
    }
    if ((actions & 1))
    {
        if (option_debug || udptldebug)
            opbx_log(OPBX_LOG_DEBUG, "UDPTL NAT: Using address %s:%d\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), udp_socket_get_them(udptl->udptl_sock_info)->sin_addr), ntohs(udp_socket_get_them(udptl->udptl_sock_info)->sin_port));
    }

    if (udptl_debug_test_addr(&sin))
    {
        opbx_verbose("Got UDPTL packet from %s:%d (len %d)\n",
            opbx_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr), ntohs(sin.sin_port), res);
    }
#if 0
    printf("Got UDPTL packet from %s:%d (len %d)\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr), ntohs(sin.sin_port), res);
#endif
    cw_udptl_rx_packet(udptl, udptl->rawdata + OPBX_FRIENDLY_OFFSET, res);

    return &udptl->f[0];
}

void opbx_udptl_offered_from_local(opbx_udptl_t *udptl, int local)
{
    if (udptl)
        udptl->udptl_offered_from_local = local;
    else
        opbx_log(OPBX_LOG_WARNING, "udptl structure is null\n");
}

int opbx_udptl_get_error_correction_scheme(opbx_udptl_t *udptl)
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
    opbx_log(OPBX_LOG_WARNING, "udptl structure is null\n");
    return -1;
}

void opbx_udptl_set_error_correction_scheme(opbx_udptl_t *udptl, int ec)
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
            opbx_log(OPBX_LOG_WARNING, "error correction parameter invalid");
            break;
        }
    }
    else
    {
        opbx_log(OPBX_LOG_WARNING, "udptl structure is null\n");
    }
}

int opbx_udptl_get_local_max_datagram(opbx_udptl_t *udptl)
{
    if (udptl)
        return udptl_get_local_max_datagram(&udptl->state);
    opbx_log(OPBX_LOG_WARNING, "udptl structure is null\n");
    return -1;
}

int opbx_udptl_get_far_max_datagram(opbx_udptl_t *udptl)
{
    if (udptl)
        return udptl_get_far_max_datagram(&udptl->state);
    opbx_log(OPBX_LOG_WARNING, "udptl structure is null\n");
    return -1;
}

void opbx_udptl_set_local_max_datagram(opbx_udptl_t *udptl, int max_datagram)
{
    if (udptl)
        udptl_set_local_max_datagram(&udptl->state, max_datagram);
    else
        opbx_log(OPBX_LOG_WARNING, "udptl structure is null\n");
}

void opbx_udptl_set_far_max_datagram(opbx_udptl_t *udptl, int max_datagram)
{
    if (udptl)
        udptl_set_far_max_datagram(&udptl->state, max_datagram);
    else
        opbx_log(OPBX_LOG_WARNING, "udptl structure is null\n");
}

opbx_udptl_t *opbx_udptl_new_with_sock_info(struct sched_context *sched,
                                            struct io_context *io,
                                            int callbackmode,
                                            udp_state_t *sock_info)
{
    opbx_udptl_t *udptl;
    int i;

    if ((udptl = malloc(sizeof(opbx_udptl_t))) == NULL)
        return NULL;
    memset(udptl, 0, sizeof(opbx_udptl_t));

    udptl_init(&udptl->state,
               udptlfectype,
               udptlfecspan,
               udptlfecentries,
               udptl_rx_packet_handler,
               (void *) udptl);

    udptl_set_far_max_datagram(&udptl->state, udptlmaxdatagram);
    udptl_set_local_max_datagram(&udptl->state, udptlmaxdatagram);

    /* This sock_info should already be bound to an address */
    udptl->udptl_sock_info = sock_info;
    if (io  &&  sched  &&  callbackmode)
    {
        /* Operate this one in a callback mode */
        udptl->sched = sched;
        udptl->io = io;
        udptl->ioid = NULL;
    }
    udptl->created_sock_info = FALSE;
    return udptl;
}

int opbx_udptl_set_active(opbx_udptl_t *udptl, int active)
{
    if (udptl->sched  &&  udptl->io)
    {
        if (active)
        {
            if (udptl->ioid == NULL)
                udptl->ioid = opbx_io_add(udptl->io, udp_socket_fd(udptl->udptl_sock_info), udptlread, OPBX_IO_IN, udptl);
        }
        else
        {
            if (udptl->ioid)
            {
                opbx_io_remove(udptl->io, udptl->ioid);
                udptl->ioid = NULL;
            }
        }
    }
    return 0;
}

int opbx_udptl_settos(opbx_udptl_t *udptl, int tos)
{
    return udp_socket_set_tos(udptl->udptl_sock_info, tos);
}

void opbx_udptl_set_peer(opbx_udptl_t *udptl, struct sockaddr_in *them)
{
    udp_socket_set_them(udptl->udptl_sock_info, them);
}

void opbx_udptl_get_peer(opbx_udptl_t *udptl, struct sockaddr_in *them)
{
    memcpy(them, udp_socket_get_them(udptl->udptl_sock_info), sizeof(*them));
}

void opbx_udptl_get_us(opbx_udptl_t *udptl, struct sockaddr_in *us)
{
    memcpy(us, udp_socket_get_apparent_us(udptl->udptl_sock_info), sizeof(*us));
}

int opbx_udptl_get_stunstate(opbx_udptl_t *udptl)
{
    if (udptl)
        return udp_socket_get_stunstate(udptl->udptl_sock_info);
    return 0;
}

void opbx_udptl_stop(opbx_udptl_t *udptl)
{
    udp_socket_restart(udptl->udptl_sock_info);
}

void opbx_udptl_destroy(opbx_udptl_t *udptl)
{
    if (udptl->ioid)
        opbx_io_remove(udptl->io, udptl->ioid);
    //if (udptl->created_sock_info)
    //    udp_socket_destroy_group(udptl->udptl_sock_info);
    free(udptl);
}

int opbx_udptl_write(opbx_udptl_t *s, struct opbx_frame *f)
{
    int len;
    int res;
    int copies;
    int i;
    uint8_t buf[LOCAL_FAX_MAX_DATAGRAM];
    char iabuf[INET_ADDRSTRLEN];
    const struct sockaddr_in *them;

    them = udp_socket_get_them(s->udptl_sock_info);

    /* If we have no peer, return immediately */    
    if (them->sin_addr.s_addr == INADDR_ANY)
        return 0;

    /* If there is no data length, return immediately */
    if (f->datalen == 0)
        return 0;
    
    if (f->frametype != OPBX_FRAME_MODEM)
    {
        opbx_log(OPBX_LOG_WARNING, "UDPTL can only send T.38 data\n");
        return -1;
    }
    /* Cook up the UDPTL packet, with the relevant EC info. */
    len = udptl_build_packet(&s->state, buf, f->data, f->datalen);

    if (len > 0  &&  them->sin_port  &&  them->sin_addr.s_addr)
    {
#if 0
        printf("Sending %d copies of %d bytes of UDPTL data to %s:%d\n", f->state.tx_copies, len, opbx_inet_ntoa(iabuf, sizeof(iabuf), udptl->them.sin_addr), ntohs(udptl->them.sin_port));
#endif
        copies = (f->tx_copies > 0)  ?  f->tx_copies  :  1;
        for (i = 0;  i < copies;  i++)
        {
            if ((res = udp_socket_sendto(s->udptl_sock_info, buf, len, 0)) < 0)
                opbx_log(OPBX_LOG_NOTICE, "UDPTL Transmission error to %s:%d: %s\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), them->sin_addr), ntohs(them->sin_port), strerror(errno));
        }
#if 0
        printf("Sent %d bytes of UDPTL data to %s:%d\n", res, opbx_inet_ntoa(iabuf, sizeof(iabuf), udptl->them.sin_addr), ntohs(udptl->them.sin_port));
#endif
        if (udptl_debug_test_addr(them))
        {
            opbx_verbose("Sent UDPTL packet to %s:%d (seq %d, len %d)\n",
                         opbx_inet_ntoa(iabuf, sizeof(iabuf), them->sin_addr),
                         ntohs(them->sin_port),
                         (s->state.tx_seq_no - 1) & 0xFFFF,
                         len);
        }
    }
    return 0;
}

void opbx_udptl_proto_unregister(struct opbx_udptl_protocol *proto)
{
    struct opbx_udptl_protocol *cur;
    struct opbx_udptl_protocol *prev;

    opbx_log(OPBX_LOG_NOTICE,"Unregistering UDPTL protocol.\n");
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

int opbx_udptl_proto_register(struct opbx_udptl_protocol *proto)
{
    struct opbx_udptl_protocol *cur;

    opbx_log(OPBX_LOG_NOTICE,"Registering UDPTL protocol.\n");
    for (cur = protos;  cur;  cur = cur->next)
    {
        if (cur->type == proto->type)
        {
            opbx_log(OPBX_LOG_WARNING, "Tried to register same protocol '%s' twice\n", cur->type);
            return -1;
        }
    }
    proto->next = protos;
    protos = proto;
    return 0;
}

static struct opbx_udptl_protocol *get_proto(struct opbx_channel *chan)
{
    struct opbx_udptl_protocol *cur;

    for (cur = protos;  cur;  cur = cur->next)
    {
        if (cur->type == chan->type)
            return cur;
    }
    return NULL;
}

enum opbx_bridge_result opbx_udptl_bridge(struct opbx_channel *c0, struct opbx_channel *c1, int flags, struct opbx_frame **fo, struct opbx_channel **rc)
{
    struct opbx_frame *f;
    struct opbx_channel *who;
    struct opbx_channel *cs[3];
    opbx_udptl_t *p0;
    opbx_udptl_t *p1;
    struct opbx_udptl_protocol *pr0;
    struct opbx_udptl_protocol *pr1;
    struct sockaddr_in ac0;
    struct sockaddr_in ac1;
    struct sockaddr_in t0;
    struct sockaddr_in t1;
    char iabuf[INET_ADDRSTRLEN];
    void *pvt0;
    void *pvt1;
    int to;
    
    opbx_mutex_lock(&c0->lock);
    while (opbx_mutex_trylock(&c1->lock))
    {
        opbx_mutex_unlock(&c0->lock);
        usleep(1);
        opbx_mutex_lock(&c0->lock);
    }
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
    pvt0 = c0->tech_pvt;
    pvt1 = c1->tech_pvt;
    p0 = pr0->get_udptl_info(c0);
    p1 = pr1->get_udptl_info(c1);
    if (!p0  ||  !p1)
    {
        /* Somebody doesn't want to play... */
        opbx_mutex_unlock(&c0->lock);
        opbx_mutex_unlock(&c1->lock);
        return OPBX_BRIDGE_FAILED_NOWARN;
    }
    if (pr0->set_udptl_peer(c0, p1))
    {
        opbx_log(OPBX_LOG_WARNING, "Channel '%s' failed to talk to '%s'\n", c0->name, c1->name);
    }
    else
    {
        /* Store UDPTL peer */
        opbx_udptl_get_peer(p1, &ac1);
    }
    if (pr1->set_udptl_peer(c1, p0))
    {
        opbx_log(OPBX_LOG_WARNING, "Channel '%s' failed to talk back to '%s'\n", c1->name, c0->name);
    }
    else
    {
        /* Store UDPTL peer */
        opbx_udptl_get_peer(p0, &ac0);
    }
    opbx_mutex_unlock(&c0->lock);
    opbx_mutex_unlock(&c1->lock);
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
            opbx_log(OPBX_LOG_DEBUG, "Oooh, something is weird, backing out\n");
            /* Tell it to try again later */
            return OPBX_BRIDGE_RETRY;
        }
        to = -1;
        opbx_udptl_get_peer(p1, &t1);
        opbx_udptl_get_peer(p0, &t0);
        if (inaddrcmp(&t1, &ac1))
        {
            opbx_log(OPBX_LOG_DEBUG, "Oooh, '%s' changed end address to %s:%d\n", 
                c1->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), t1.sin_addr), ntohs(t1.sin_port));
            opbx_log(OPBX_LOG_DEBUG, "Oooh, '%s' was %s:%d\n", 
                c1->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), ac1.sin_addr), ntohs(ac1.sin_port));
            memcpy(&ac1, &t1, sizeof(ac1));
        }
        if (inaddrcmp(&t0, &ac0))
        {
            opbx_log(OPBX_LOG_DEBUG, "Oooh, '%s' changed end address to %s:%d\n", 
                c0->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), t0.sin_addr), ntohs(t0.sin_port));
            opbx_log(OPBX_LOG_DEBUG, "Oooh, '%s' was %s:%d\n", 
                c0->name, opbx_inet_ntoa(iabuf, sizeof(iabuf), ac0.sin_addr), ntohs(ac0.sin_port));
            memcpy(&ac0, &t0, sizeof(ac0));
        }
        if ((who = opbx_waitfor_n(cs, 2, &to)) == 0)
        {
            opbx_log(OPBX_LOG_DEBUG, "Ooh, empty read...\n");
            /* Check for hangup / whentohangup */
            if (opbx_check_hangup(c0)  ||  opbx_check_hangup(c1))
                break;
            continue;
        }
        if ((f = opbx_read(who)) == 0)
        {
            *fo = f;
            *rc = who;
            opbx_log(OPBX_LOG_DEBUG, "Oooh, got a %s\n", (f)  ?  "digit"  :  "hangup");
            /* That's all we needed */
            return OPBX_BRIDGE_COMPLETE;
        }
        if (f->frametype == OPBX_FRAME_MODEM)
        {
            /* Forward T.38 frames if they happen upon us */
            if (who == c0)
                opbx_write(c1, f);
            else if (who == c1)
                opbx_write(c0, f);
        }
        opbx_fr_free(f);
        /* Swap priority. Not that it's a big deal at this point */
        cs[2] = cs[0];
        cs[0] = cs[1];
        cs[1] = cs[2];
    }
    return OPBX_BRIDGE_FAILED;
}

static int udptl_do_debug_ip(int fd, int argc, char *argv[])
{
    struct hostent *hp;
    struct opbx_hostent ahp;
    char iabuf[INET_ADDRSTRLEN];
    int port;
    char *p;
    char *arg;

    port = 0;
    if (argc != 4)
        return RESULT_SHOWUSAGE;
    arg = argv[3];
    p = strstr(arg, ":");
    if (p)
    {
        *p = '\0';
        p++;
        port = atoi(p);
    }
    hp = opbx_gethostbyname(arg, &ahp);
    if (hp == NULL)
        return RESULT_SHOWUSAGE;
    udptldebugaddr.sin_family = AF_INET;
    memcpy(&udptldebugaddr.sin_addr, hp->h_addr, sizeof(udptldebugaddr.sin_addr));
    udptldebugaddr.sin_port = htons(port);
    if (port == 0)
        opbx_cli(fd, "UDPTL Debugging Enabled for IP: %s\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), udptldebugaddr.sin_addr));
    else
        opbx_cli(fd, "UDPTL Debugging Enabled for IP: %s:%d\n", opbx_inet_ntoa(iabuf, sizeof(iabuf), udptldebugaddr.sin_addr), port);
    udptldebug = 1;
    return RESULT_SUCCESS;
}

static int udptl_do_debug(int fd, int argc, char *argv[])
{
    if (argc != 2)
    {
        if (argc != 4)
            return RESULT_SHOWUSAGE;
        return udptl_do_debug_ip(fd, argc, argv);
    }
    udptldebug = 1;
    memset(&udptldebugaddr, 0, sizeof(udptldebugaddr));
    opbx_cli(fd, "UDPTL Debugging Enabled\n");
    return RESULT_SUCCESS;
}
   
static int udptl_no_debug(int fd, int argc, char *argv[])
{
    if (argc != 3)
        return RESULT_SHOWUSAGE;
    udptldebug = 0;
    opbx_cli(fd, "UDPTL Debugging Disabled\n");
    return RESULT_SUCCESS;
}

static char debug_usage[] =
    "Usage: udptl debug [ip host[:port]]\n"
    "       Enable dumping of all UDPTL packets to and from host.\n";

static char no_debug_usage[] =
    "Usage: udptl no debug\n"
    "       Disable all UDPTL debugging\n";

static struct opbx_clicmd  cli_debug_ip =
{
	.cmda = { "udptl", "debug", "ip", NULL },
	.handler = udptl_do_debug,
	.summary = "Enable UDPTL debugging on IP",
	.usage = debug_usage,
};

static struct opbx_clicmd  cli_debug =
{
	.cmda = { "udptl", "debug", NULL },
	.handler = udptl_do_debug,
	.summary = "Enable UDPTL debugging",
	.usage = debug_usage,
};

static struct opbx_clicmd  cli_no_debug =
{
	.cmda = { "udptl", "no", "debug", NULL },
	.handler = udptl_no_debug,
	.summary = "Disable UDPTL debugging",
	.usage = no_debug_usage,
};

void opbx_udptl_reload(void)
{
    struct opbx_config *cfg;
    char *s;

    udptlfectype = 0;
    udptlfecentries = 0;
    udptlfecspan = 0;
    udptlmaxdatagram = 0;

    if ((cfg = opbx_config_load("udptl.conf")))
    {
        if ((s = opbx_variable_retrieve(cfg, "general", "udptlchecksums")))
        {
#ifdef SO_NO_CHECK
            if (opbx_false(s))
                nochecksums = 1;
            else
                nochecksums = 0;
#else
            if (opbx_false(s))
                opbx_log(OPBX_LOG_WARNING, "Disabling UDPTL checksums is not supported on this operating system!\n");
#endif
        }
        if ((s = opbx_variable_retrieve(cfg, "general", "T38FaxUdpEC")))
        {
            if (strcmp(s, "t38UDPFEC") == 0)
                udptlfectype = 2;
            else if (strcmp(s, "t38UDPRedundancy") == 0)
                udptlfectype = 1;
        }
        if ((s = opbx_variable_retrieve(cfg, "general", "T38FaxMaxDatagram")))
        {
            udptlmaxdatagram = atoi(s);
            if (udptlmaxdatagram < 0)
                udptlmaxdatagram = 0;
            if (udptlmaxdatagram > LOCAL_FAX_MAX_DATAGRAM)
                udptlmaxdatagram = LOCAL_FAX_MAX_DATAGRAM;
        }
        if ((s = opbx_variable_retrieve(cfg, "general", "UDPTLFECentries")))
        {
            udptlfecentries = atoi(s);
            if (udptlfecentries < 0)
                udptlfecentries = 0;
            if (udptlfecentries > LOCAL_FAX_MAX_FEC_PACKETS)
                udptlfecentries = LOCAL_FAX_MAX_FEC_PACKETS;
        }
        if ((s = opbx_variable_retrieve(cfg, "general", "UDPTLFECspan")))
        {
            udptlfecspan = atoi(s);
            if (udptlfecspan < 0)
                udptlfecspan = 0;
            if (udptlfecspan > LOCAL_FAX_MAX_FEC_SPAN)
                udptlfecspan = LOCAL_FAX_MAX_FEC_SPAN;
        }
        opbx_config_destroy(cfg);
    }
}

void opbx_udptl_init(void)
{
    opbx_cli_register(&cli_debug);
    opbx_cli_register(&cli_debug_ip);
    opbx_cli_register(&cli_no_debug);
    opbx_udptl_reload();
}
