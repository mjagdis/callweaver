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
 */

/*
 *
 * A simple abstration of UDP ports so they can be handed between streaming
 * protocols, such as when RTP switches to UDPTL on the same IP port.
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
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

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
#include "callweaver/udp.h"
#include "callweaver/stun.h"

struct udp_state_s
{
    int fd;
    struct sockaddr_in us;
    struct sockaddr_in them;
    struct sockaddr_in rfc3489_local;
    int nochecksums;
    int nat;
    int rfc3489_state;
    struct udp_state_s *next;
    struct udp_state_s *prev;
};

static uint16_t make_mask16(uint16_t x)
{
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    return x;
}

int udp_socket_destroy(udp_state_t *s)
{
    if (s == NULL)
        return -1;
    if (s->fd >= 0)
        close(s->fd);
    free(s);
    return 0;
}

udp_state_t *udp_socket_create(int nochecksums)
{
    int fd;
    long flags;
    udp_state_t *s;
    
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        opbx_log(OPBX_LOG_ERROR, "Unable to allocate socket: %s\n", strerror(errno));
        return NULL;
    }
    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#ifdef SO_NO_CHECK
    if (nochecksums)
        setsockopt(fd, SOL_SOCKET, SO_NO_CHECK, &nochecksums, sizeof(nochecksums));
#endif
    if ((s = malloc(sizeof(*s))) == NULL)
    {
        opbx_log(OPBX_LOG_ERROR, "Unable to allocate socket data: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }
    memset(s, 0, sizeof(*s));
    s->them.sin_family = AF_INET;
    s->us.sin_family = AF_INET;
    s->nochecksums = nochecksums;
    s->fd = fd;
    s->rfc3489_state = RFC3489_STATE_IDLE;
    s->next = NULL;
    s->prev = NULL;
    return s;
}

int udp_socket_destroy_group(udp_state_t *s)
{
    udp_state_t *sx;
    udp_state_t *sy;

    /* Assume s could be in the middle of the group, and search both ways when
       destroying */
    sx = s->next;
    while (sx)
    {
        sy = sx->next;
        udp_socket_destroy(sx);
        sx = sy;
    }
    sx = s;
    while (sx)
    {
        sy = sx->prev;
        udp_socket_destroy(sx);
        sx = sy;
    }
    return 0;
}

udp_state_t *udp_socket_create_group_with_bindaddr(int nochecksums, int group, struct in_addr *addr, int startport, int endport)
{
    udp_state_t *s;
    udp_state_t *s_extra;
    struct sockaddr_in sockaddr;
    int i;
    int x;
    int xx;
    int port_mask;
    int startplace;

    if ((s = udp_socket_create(nochecksums)) == NULL)
        return NULL;
    if (group > 1)
    {
        s_extra = s;
        for (i = 1;  i < group;  i++)
        {
            if ((s_extra->next = udp_socket_create(nochecksums)) == NULL)
            {
                /* Unravel what we did so far, and give up */
                udp_socket_destroy_group(s);
                return NULL;
            }
            s_extra->next->prev = s_extra;
            s_extra = s_extra->next;
        }
    }

    /* Find a port or group of ports we can bind to, within a specified numeric range */
    port_mask = make_mask16(group);
    x = ((rand()%(endport - startport)) + startport) & ~port_mask;
    startplace = x;
    for (;;)
    {
        xx = x;
        memset(&sockaddr, 0, sizeof(sockaddr));
        sockaddr.sin_addr = *addr;
        sockaddr.sin_port = htons(xx);
        s_extra = s;
        while (s_extra)
        {
            if (udp_socket_set_us(s_extra, &sockaddr))
                break;
            sockaddr.sin_port = htons(++xx);
            s_extra = s_extra->next;
        }
        if (s_extra == NULL)
        {
            /* We must have bound them all OK. */
            return s;
        }
        if (errno != EADDRINUSE)
        {
            opbx_log(OPBX_LOG_ERROR, "Unexpected bind error: %s\n", strerror(errno));
            udp_socket_destroy_group(s);
            return NULL;
        }
        x += (port_mask + 1);
        if (x > endport)
            x = (startport + port_mask) & ~port_mask;
        if (x == startplace)
            break;
    }
    opbx_log(OPBX_LOG_ERROR, "No ports available within the range %d to %d. Can't setup media stream.\n", startport, endport);
    /* Unravel what we did so far, and give up */
    udp_socket_destroy_group(s);
    return NULL;
}

udp_state_t *udp_socket_find_group_element(udp_state_t *s, int element)
{
    int i;

    /* Find the start of the group */
    while (s->prev)
        s = s->prev;
    /* Now count to the element we want */
    for (i = 0;  i < element  &&  s;  i++)
        s = s->next;
    return s;
}

int udp_socket_set_us(udp_state_t *s, const struct sockaddr_in *us)
{
    int res;
    long flags;

    if (s == NULL  ||  s->fd < 0)
        return -1;

    if (s->us.sin_addr.s_addr  ||  s->us.sin_port)
    {
        /* We are already bound, so we need to re-open the socket to unbind it */
        close(s->fd);
        if ((s->fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
            opbx_log(OPBX_LOG_ERROR, "Unable to re-allocate socket: %s\n", strerror(errno));
            return -1;
        }
        flags = fcntl(s->fd, F_GETFL);
        fcntl(s->fd, F_SETFL, flags | O_NONBLOCK);
#ifdef SO_NO_CHECK
        if (s->nochecksums)
            setsockopt(s->fd, SOL_SOCKET, SO_NO_CHECK, &s->nochecksums, sizeof(s->nochecksums));
#endif
    }
    s->us.sin_port = us->sin_port;
    s->us.sin_addr.s_addr = us->sin_addr.s_addr;
       if ((res = bind(s->fd, (struct sockaddr *) &s->us, sizeof(s->us))) < 0)
    {
        s->us.sin_port = 0;
        s->us.sin_addr.s_addr = 0;
    }
    return res;
}

void udp_socket_set_them(udp_state_t *s, const struct sockaddr_in *them)
{
    s->them.sin_port = them->sin_port;
    s->them.sin_addr.s_addr = them->sin_addr.s_addr;
}

int udp_socket_set_tos(udp_state_t *s, int tos)
{
    int res;

    if (s == NULL)
        return -1;
    if ((res = setsockopt(s->fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)))) 
        opbx_log(OPBX_LOG_WARNING, "Unable to set TOS to %d\n", tos);
    return res;
}

void udp_socket_set_nat(udp_state_t *s, int nat_mode)
{
    if (s == NULL)
        return;
    s->nat = nat_mode;
    if (nat_mode  &&  s->rfc3489_state == RFC3489_STATE_IDLE  &&  stun_active)
    {
        if (stundebug)
            opbx_log(OPBX_LOG_DEBUG, "Sending stun request on this UDP channel (port %d) cause NAT is on\n",ntohs(s->us.sin_port) );
        opbx_udp_stun_bindrequest(s->fd, &stunserver_ip, NULL, NULL);
        s->rfc3489_state = RFC3489_STATE_REQUEST_PENDING;
    }
}

int udp_socket_restart(udp_state_t *s)
{
    if (s == NULL)
        return -1;
    memset(&s->them.sin_addr.s_addr, 0, sizeof(s->them.sin_addr.s_addr));
    s->them.sin_port = 0;
    return 0;
}

int udp_socket_fd(udp_state_t *s)
{
    if (s == NULL)
        return -1;
    return s->fd;
}

int udp_socket_get_stunstate(udp_state_t *s)
{
    if (s)
        return s->rfc3489_state;
    return 0;
}

void udp_socket_set_stunstate(udp_state_t *s, int state)
{
    s->rfc3489_state = state;
}

const struct sockaddr_in *udp_socket_get_stun(udp_state_t *s)
{
    static const struct sockaddr_in dummy = {0};

    if (s)
        return &s->rfc3489_local;
    return &dummy;
}

void udp_socket_set_stun(udp_state_t *s, const struct sockaddr_in *stun)
{
    memcpy(&s->rfc3489_local, stun, sizeof(struct sockaddr_in));
}

const struct sockaddr_in *udp_socket_get_us(udp_state_t *s)
{
    static const struct sockaddr_in dummy = {0};

    if (s)
        return &s->us;
    return &dummy;
}

const struct sockaddr_in *udp_socket_get_apparent_us(udp_state_t *s)
{
    static const struct sockaddr_in dummy = {0};

    if (s)
    {
        if (s->rfc3489_state == RFC3489_STATE_RESPONSE_RECEIVED)
            return &s->rfc3489_local;
        return &s->us;
    }
    return &dummy;
}

const struct sockaddr_in *udp_socket_get_them(udp_state_t *s)
{
    static const struct sockaddr_in dummy = {0};

    if (s)
        return &s->them;
    return &dummy;
}

int udp_socket_recvfrom(udp_state_t *s,
                        void *buf,
                        size_t size,
                        int flags,
                        struct sockaddr *sa,
                        socklen_t *salen,
                        int *action)
{
    struct sockaddr_in stun_sin;
    struct stun_state rfc3489_local;
    int res;

    *action = 0;
    if (s == NULL  ||  s->fd < 0)
        return 0;
    if ((res = recvfrom(s->fd, buf, size, flags, sa, salen)) >= 0)
    {
        if ((s->nat  &&  !stun_active)
            ||
            (s->nat  &&  stun_active  &&  s->rfc3489_state == RFC3489_STATE_IDLE))
        {
            /* Send to whoever sent to us */
            if (s->them.sin_addr.s_addr != ((struct sockaddr_in *) sa)->sin_addr.s_addr
                || 
                s->them.sin_port != ((struct sockaddr_in *) sa)->sin_port)
            {
                memcpy(&s->them, sa, sizeof(s->them));
                *action |= 1;
            }
        }
        if (s->rfc3489_state == RFC3489_STATE_REQUEST_PENDING)
        {
            if (stundebug)
                opbx_log(OPBX_LOG_DEBUG, "Checking if payload it is a stun RESPONSE\n");
            memset(&rfc3489_local, 0, sizeof(struct stun_state));
            stun_handle_packet(s->rfc3489_state, (struct sockaddr_in *) sa, buf, res, &rfc3489_local);
            if (rfc3489_local.msgtype == RFC3489_MSG_TYPE_BINDING_RESPONSE)
            {
                if (stundebug)
                    opbx_log(OPBX_LOG_DEBUG, "Got STUN bind response\n");
                s->rfc3489_state = RFC3489_STATE_RESPONSE_RECEIVED;
                if (stun_addr2sockaddr(&stun_sin, rfc3489_local.mapped_addr))
                {
                    memcpy(&s->rfc3489_local, &stun_sin, sizeof(struct sockaddr_in));
                }
                else
                {
                    if (stundebug)
                        opbx_log(OPBX_LOG_DEBUG, "Stun response did not contain mapped address\n");
                }
                stun_remove_request(&rfc3489_local.id);
                return -1;
            }
        }
    }
    return res;
}

int udp_socket_sendto(udp_state_t *s, const void *buf, size_t size, int flags)
{
    if (s == NULL  ||  s->fd < 0)
        return 0;
    if (s->them.sin_port == 0)
        return 0;
    return sendto(s->fd, buf, size, flags, (struct sockaddr *) &s->them, sizeof(s->them));
}
