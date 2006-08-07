/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Steve Underwood
 *
 * Steve Underwood <steveu@coppice.org>
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
#ifdef ENABLE_SRTP
#include <srtp/srtp.h>
#endif	/* ENABLE_SRTP */

#include "openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL: svn://svn.openpbx.org/openpbx/trunk/corelib/udp.c $", "$Revision: 1600 $")

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
#include "openpbx/udp.h"
#include "openpbx/stun.h"

struct udp_socket_info_s {
    int fd;
    struct sockaddr_in us;
    struct sockaddr_in them;
    int nochecksums;
    int nat;
    struct sockaddr_in stun_me;
    int stun_state;  // 0 = no action - 1 requested - 2 got response
};

udp_socket_info_t *udp_socket_create(int nochecksums)
{
    int fd;
    long flags;
    udp_socket_info_t *info;
    
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	opbx_log(LOG_ERROR, "Unable to allocate socket: %s\n", strerror(errno));
        return NULL;
    }
	flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#ifdef SO_NO_CHECK
	if (nochecksums)
		setsockopt(fd, SOL_SOCKET, SO_NO_CHECK, &nochecksums, sizeof(nochecksums));
#endif
	if ((info = malloc(sizeof(*info))) == NULL) {
		opbx_log(LOG_ERROR, "Unable to allocate socket data: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }
    memset(info, 0, sizeof(*info));
    info->them.sin_family = AF_INET;
    info->us.sin_family = AF_INET;
    info->nochecksums = nochecksums;
    info->fd  = fd;
    info->stun_state=0;
    return info;
}

int udp_socket_set_us(udp_socket_info_t *info, struct sockaddr_in *us)
{
    int res;
	long flags;

    if (info == NULL  ||  info->fd < 0)
        return -1;

    if (info->us.sin_addr.s_addr  ||  info->us.sin_port) {
        /* We are already bound, so we need to re-open the socket to unbind it */
        close(info->fd);
    	if ((info->fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	    	opbx_log(LOG_ERROR, "Unable to re-allocate socket: %s\n", strerror(errno));
            return -1;
        }
    	flags = fcntl(info->fd, F_GETFL);
	    fcntl(info->fd, F_SETFL, flags | O_NONBLOCK);
#ifdef SO_NO_CHECK
    	if (info->nochecksums)
	    	setsockopt(info->fd, SOL_SOCKET, SO_NO_CHECK, &info->nochecksums, sizeof(info->nochecksums));
#endif
    }
	info->us.sin_port = us->sin_port;
	info->us.sin_addr.s_addr = us->sin_addr.s_addr;
    	if ((res = bind(info->fd, (struct sockaddr *) &info->us, sizeof(info->us))) < 0) {
    	info->us.sin_port = 0;
	    info->us.sin_addr.s_addr = 0;
    }
    return res;
}

void udp_socket_set_them(udp_socket_info_t *info, struct sockaddr_in *them)
{
	info->them.sin_port = them->sin_port;
	info->them.sin_addr.s_addr = them->sin_addr.s_addr;
}

int udp_socket_set_tos(udp_socket_info_t *info, int tos)
{
	int res;

    if (info == NULL)
        return -1;
	if ((res = setsockopt(info->fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)))) 
		opbx_log(LOG_WARNING, "Unable to set TOS to %d\n", tos);
	return res;
}

void udp_socket_set_nat(udp_socket_info_t *info, int nat_mode)
{
    if (info == NULL)
        return;
    info->nat = nat_mode;

    if (nat_mode && info->stun_state==0 && stun_active) {
	if (stundebug)
	opbx_log(LOG_WARNING, "Sending stun request on this UDP channel (port %d) cause NAT is on\n",ntohs(info->us.sin_port) );
	opbx_udp_stun_bindrequest(info->fd, &stunserver_ip, NULL, NULL);
	info->stun_state=1;
    }
}

int udp_socket_destroy(udp_socket_info_t *info)
{
    if (info == NULL)
        return -1;
    if (info->fd >= 0)
        close(info->fd);
    free(info);
    return 0;
}

int udp_socket_restart(udp_socket_info_t *info)
{
    if (info == NULL)
        return -1;
	memset(&info->them.sin_addr.s_addr, 0, sizeof(info->them.sin_addr.s_addr));
	info->them.sin_port = 0;
    return 0;
}

int udp_socket_fd(udp_socket_info_t *info)
{
    if (info == NULL)
        return -1;
	return info->fd;
}

const struct sockaddr_in *udp_socket_get_us(udp_socket_info_t *info)
{
    static const struct sockaddr_in dummy = {0};

    if (info)
    	return &info->us;
    return &dummy;
}

const struct sockaddr_in *udp_socket_get_them(udp_socket_info_t *info)
{
    static const struct sockaddr_in dummy = {0};

    if (info)
    	return &info->them;
    return &dummy;
}

int udp_socket_recvfrom(udp_socket_info_t *info, void *buf, size_t size,
			int flags, struct sockaddr *sa, socklen_t *salen, int *action)
{
    int res;

    *action = 0;
    if (info == NULL  ||  info->fd < 0)
        return 0;
	res = recvfrom(info->fd, buf, size, flags, sa, salen);

    if ( 
	 ( info->nat && !stun_active ) || 
	 ( info->nat && stun_active && info->stun_state==0 )
       ) {
        /* Send to whoever sent to us */
		if (info->them.sin_addr.s_addr != ((struct sockaddr_in *)sa)->sin_addr.s_addr || info->them.sin_port != ((struct sockaddr_in *)sa)->sin_port) {
			memcpy(&info->them, &sa, sizeof(info->them));
			*action |= 1;
		}
    }
    return res;
}

int udp_socket_sendto(udp_socket_info_t *info, void *buf, size_t size, int flags)
{
    if (info == NULL  ||  info->fd < 0)
        return 0;
    if (info->them.sin_port == 0)
        return 0;
	return sendto(info->fd, buf, size, flags, &info->them, sizeof(info->them));
}

int udp_socket_get_stunstate(udp_socket_info_t *info)
{
    if (info)
    	return info->stun_state;
    return 0;
}

void udp_socket_set_stunstate(udp_socket_info_t *info, int state)
{
	info->stun_state = state;
}


struct sockaddr_in *udp_socket_get_stun(udp_socket_info_t *info)
{
    if (info)
    	return &info->stun_me;
    return NULL;
}

void udp_socket_set_stun(udp_socket_info_t *info, struct sockaddr_in *stun)
{
    memcpy(&info->stun_me,stun,sizeof(struct sockaddr_in) );
}
