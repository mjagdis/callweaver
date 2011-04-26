/*
 * Vale - a library for media streaming.
 *
 * udp.c - A simple abstraction of UDP ports.
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *
 * Copyright (C) 2006 Steve Underwood
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id: udp.c,v 1.2 2007/04/13 12:24:49 steveu Exp $
 */

/*! \file */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
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

#include "callweaver/udp.h"
#include "callweaver/rfc3489.h"

/*
 *
 * A simple abstration of UDP ports so they can be handed between streaming
 * protocols, such as when RTP switches to UDPTL on the same IP port.
 *
 * RFC3489 STUN is integrated for NAT traversal.
 * 
 */

struct udp_state_s
{
    int fd;
    struct sockaddr_in local;
    struct sockaddr_in far;
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
/*- End of function --------------------------------------------------------*/

udp_state_t *udp_socket_create(int nochecksums)
{
    int fd;
    long flags;
    udp_state_t *s;
    
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        return NULL;
    flags = fcntl(fd, F_GETFL);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        close(fd);
        return NULL;
    }
#ifdef SO_NO_CHECK
    if (nochecksums)
        setsockopt(fd, SOL_SOCKET, SO_NO_CHECK, &nochecksums, sizeof(nochecksums));
#endif
    if ((s = malloc(sizeof(*s))) == NULL)
    {
        close(fd);
        return NULL;
    }
    memset(s, 0, sizeof(*s));
    s->far.sin_family = AF_INET;
    s->local.sin_family = AF_INET;
    s->nochecksums = nochecksums;
    s->fd = fd;
    s->rfc3489_state = RFC3489_STATE_IDLE;
    s->next = NULL;
    s->prev = NULL;
    return s;
}
/*- End of function --------------------------------------------------------*/

int udp_socket_destroy(udp_state_t *s)
{
    if (s == NULL)
        return -1;
    if (s->fd >= 0)
        close(s->fd);
    free(s);
    return 0;
}
/*- End of function --------------------------------------------------------*/

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
/*- End of function --------------------------------------------------------*/

udp_state_t *udp_socket_create_and_bind(int nochecksums, struct in_addr *addr, int lowest_port, int highest_port)
{
    udp_state_t *s;
    struct sockaddr_in sockaddr;
    int port;
    int starting_point;

    port = lowest_port;
    if (highest_port != lowest_port)
        port += (rand()%(highest_port - lowest_port + 1));
    starting_point = port;

    if ((s = udp_socket_create(nochecksums)) == NULL)
        return NULL;

    for (;;)
    {
        sockaddr.sin_family = AF_INET;
        if (addr)
            sockaddr.sin_addr = *addr;
        else
            sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        sockaddr.sin_port = htons(port);
        if (udp_socket_set_local(s, &sockaddr) == 0)
        {
            /* We must have bound the port OK. */
            return s;
        }
        if (errno != EADDRINUSE)
        {
            udp_socket_destroy(s);
            return NULL;
        }
        if (++port > highest_port)
            port = lowest_port;
        if (port == starting_point)
            break;
    }
    /* Unravel what we did so far, and give up */
    udp_socket_destroy(s);
    return NULL;
}
/*- End of function --------------------------------------------------------*/

udp_state_t *udp_socket_group_create_and_bind(int group, int nochecksums, struct in_addr *addr, int lowest_port, int highest_port)
{
    udp_state_t *s;
    udp_state_t *s_extra;
    struct sockaddr_in sockaddr;
    int i;
    int base;
    int port;
    int port_mask;
    int starting_point;

    /* Find a port or group of ports we can bind to, within a specified numeric range */
    port_mask = make_mask16(group - 1);
    /* Trim the port range to suitable multiples of the size of the blocks of ports being used. */
    lowest_port = (lowest_port + port_mask) & ~port_mask;
    if (highest_port < lowest_port + group - 1)
        return NULL;
    highest_port &= ~port_mask;
    base = lowest_port;
    if (highest_port != lowest_port)
        base += ((rand()%(highest_port - lowest_port + 1)) & ~port_mask);
    starting_point = base;

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

    for (;;)
    {
        port = base;
        sockaddr.sin_family = AF_INET;
        if (addr)
            sockaddr.sin_addr = *addr;
        else
            sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        sockaddr.sin_port = htons(port);
        s_extra = s;
        while (s_extra)
        {
            if (udp_socket_set_local(s_extra, &sockaddr))
                break;
            sockaddr.sin_port = htons(++port);
            s_extra = s_extra->next;
        }
        if (s_extra == NULL)
        {
            /* We must have bound all ports OK. */
            return s;
        }
        if (errno != EADDRINUSE)
        {
            udp_socket_destroy_group(s);
            return NULL;
        }
        base += (port_mask + 1);
        if (base > highest_port)
            base = lowest_port;
        if (base == starting_point)
            break;
    }
    /* Unravel what we did so far, and give up */
    udp_socket_destroy_group(s);
    return NULL;
}
/*- End of function --------------------------------------------------------*/

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
/*- End of function --------------------------------------------------------*/

int udp_socket_set_local(udp_state_t *s, const struct sockaddr_in *us)
{
    int res;
    long flags;

    if (s == NULL  ||  s->fd < 0)
        return -1;

    if (s->local.sin_addr.s_addr  ||  s->local.sin_port)
    {
        /* We are already bound, so we need to re-open the socket to unbind it */
        close(s->fd);
        if ((s->fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            return -1;
        flags = fcntl(s->fd, F_GETFL);
        if (fcntl(s->fd, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            close(s->fd);
            return -1;
        }
#ifdef SO_NO_CHECK
        if (s->nochecksums)
            setsockopt(s->fd, SOL_SOCKET, SO_NO_CHECK, &s->nochecksums, sizeof(s->nochecksums));
#endif
    }
    s->local.sin_port = us->sin_port;
    s->local.sin_addr.s_addr = us->sin_addr.s_addr;
    if ((res = bind(s->fd, (struct sockaddr *) &s->local, sizeof(s->local))) < 0)
    {
        s->local.sin_port = 0;
        s->local.sin_addr.s_addr = 0;
    }
    return res;
}
/*- End of function --------------------------------------------------------*/

void udp_socket_set_far(udp_state_t *s, const struct sockaddr_in *far)
{
    s->far.sin_port = far->sin_port;
    s->far.sin_addr.s_addr = far->sin_addr.s_addr;
}
/*- End of function --------------------------------------------------------*/

void udp_socket_set_rfc3489(udp_state_t *s, const struct sockaddr_in *rfc3489)
{
    memcpy(&s->rfc3489_local, rfc3489, sizeof(struct sockaddr_in));
}
/*- End of function --------------------------------------------------------*/

int udp_socket_set_tos(udp_state_t *s, int tos)
{
    if (s == NULL)
        return -1;
    return setsockopt(s->fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
}
/*- End of function --------------------------------------------------------*/

void udp_socket_set_nat(udp_state_t *s, int nat_mode)
{
    if (s == NULL)
        return;
    s->nat = nat_mode;
    if (nat_mode  &&  s->rfc3489_state == RFC3489_STATE_IDLE  &&  rfc3489_active)
    {
        rfc3489_udp_binding_request(s->fd, NULL, NULL, NULL);
        s->rfc3489_state = RFC3489_STATE_REQUEST_PENDING;
    }
}
/*- End of function --------------------------------------------------------*/

int udp_socket_restart(udp_state_t *s)
{
    if (s == NULL)
        return -1;
    memset(&s->far.sin_addr.s_addr, 0, sizeof(s->far.sin_addr.s_addr));
    s->far.sin_port = 0;
    return 0;
}
/*- End of function --------------------------------------------------------*/

int udp_socket_fd(udp_state_t *s)
{
    if (s == NULL)
        return -1;
    return s->fd;
}
/*- End of function --------------------------------------------------------*/

int udp_socket_get_rfc3489_state(udp_state_t *s)
{
    if (s)
        return s->rfc3489_state;
    return 0;
}
/*- End of function --------------------------------------------------------*/

void udp_socket_set_rfc3489_state(udp_state_t *s, int state)
{
    s->rfc3489_state = state;
}
/*- End of function --------------------------------------------------------*/

const struct sockaddr_in *udp_socket_get_local(udp_state_t *s)
{
    static const struct sockaddr_in dummy = {0};

    if (s)
        return &s->local;
    return &dummy;
}
/*- End of function --------------------------------------------------------*/

const struct sockaddr_in *udp_socket_get_apparent_local(udp_state_t *s)
{
    static const struct sockaddr_in dummy = {0};

    if (s)
    {
        if (s->rfc3489_state == RFC3489_STATE_RESPONSE_RECEIVED)
            return &s->rfc3489_local;
        return &s->local;
    }
    return &dummy;
}
/*- End of function --------------------------------------------------------*/

const struct sockaddr_in *udp_socket_get_far(udp_state_t *s)
{
    static const struct sockaddr_in dummy = {0};

    if (s)
        return &s->far;
    return &dummy;
}
/*- End of function --------------------------------------------------------*/

const struct sockaddr_in *udp_socket_get_rfc3489(udp_state_t *s)
{
    static const struct sockaddr_in dummy = {0};

    if (s)
        return &s->rfc3489_local;
    return &dummy;
}
/*- End of function --------------------------------------------------------*/

int udp_socket_recvfrom(udp_state_t *s,
                        void *buf,
                        size_t size,
                        int flags,
                        struct sockaddr *sa,
                        socklen_t *salen,
                        int *action)
{
    struct sockaddr_in rfc3489_sin;
    rfc3489_state_t stunning;
    int res;

    *action = 0;
    if (s == NULL  ||  s->fd < 0)
        return 0;
    if ((res = recvfrom(s->fd, buf, size, flags, sa, salen)) >= 0)
    {
        if ((s->nat  &&  !rfc3489_active)
            ||
            (s->nat  &&  rfc3489_active  &&  s->rfc3489_state == RFC3489_STATE_IDLE))
        {
            /* Send to whoever sent to us */
            if (s->far.sin_addr.s_addr != ((struct sockaddr_in *) sa)->sin_addr.s_addr
                || 
                s->far.sin_port != ((struct sockaddr_in *) sa)->sin_port)
            {
                memcpy(&s->far, sa, sizeof(s->far));
                *action |= 1;
            }
        }
        if (s->rfc3489_state == RFC3489_STATE_REQUEST_PENDING)
        {
            rfc3489_handle_packet(s->rfc3489_state, (struct sockaddr_in *) sa, buf, res, &stunning);
            if (stunning.msgtype == RFC3489_MSG_TYPE_BINDING_RESPONSE)
            {
                s->rfc3489_state = RFC3489_STATE_RESPONSE_RECEIVED;
                if (rfc3489_addr_to_sockaddr(&rfc3489_sin, stunning.mapped_addr) == 0)
                    memcpy(&s->rfc3489_local, &rfc3489_sin, sizeof(struct sockaddr_in));
                rfc3489_delete_request(&stunning.id);
                *action |= 2;
                errno = EAGAIN;
                return -1;
            }
        }
    }
    return res;
}
/*- End of function --------------------------------------------------------*/

int udp_socket_recv(udp_state_t *s,
                    void *buf,
                    size_t size,
                    int flags,
                    int *action)
{
    struct sockaddr sa;
    socklen_t salen;
    int res;

    salen = sizeof(sa);
    if ((res = udp_socket_recvfrom(s, buf, size, flags, &sa, &salen, action)) >= 0)
    {
        if (salen != sizeof(s->far)
            ||
            memcmp(&sa, &s->far, sizeof(s->far)))
        {
            errno = EAGAIN;
            return -1;
        }
    }
    return res;
}
/*- End of function --------------------------------------------------------*/

int udp_socket_send(udp_state_t *s, const void *buf, size_t size, int flags)
{
    if (s == NULL  ||  s->fd < 0)
        return 0;
    if (s->far.sin_port == 0)
        return 0;
    return sendto(s->fd, buf, size, flags, (struct sockaddr *) &s->far, sizeof(s->far));
}
/*- End of function --------------------------------------------------------*/

int udp_socket_sendto(udp_state_t *s,
                      const void *buf,
                      size_t size,
                      int flags,
                      struct sockaddr *sa,
                      socklen_t salen)
{
    if (s == NULL  ||  s->fd < 0)
        return 0;
    if (s->far.sin_port == 0)
        return 0;
    return sendto(s->fd, buf, size, flags, sa, salen);
}
/*- End of function --------------------------------------------------------*/
/*- End of file ------------------------------------------------------------*/
