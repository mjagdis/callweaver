/*
 * udpfromto.c Helper functions to get/set addresses of UDP packets 
 *             based on recvfromto by Miquel van Smoorenburg
 *
 * recvfromto	Like recvfrom, but also stores the destination
 *		IP address. Useful on multihomed hosts.
 *
 *		Should work on Linux and BSD.
 *
 *		Copyright (C) 2002 Miquel van Smoorenburg.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU Lesser General Public
 *		License as published by the Free Software Foundation; either
 *		version 2 of the License, or (at your option) any later version.
 *
 * sendfromto	added 18/08/2003, Jan Berkel <jan@sitadelle.com>
 *		Works on Linux and FreeBSD (5.x)
 *
 * sendfromto   modified to fallback to sendto if from hasn't been specified (is NULL)
 *		10/10/2005, Stefan Knoblich <stkn@gentoo.org>
 *
 * updfromto_init   don't return -1 if no support is available
 *		    10/10/2005, Stefan Knoblich <stkn@gentoo.org>
 *
 * Version: $Id$
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "callweaver/logger.h"
#include "callweaver/udpfromto.h"


#ifndef SOL_IP
#  define SOL_IP	IPPROTO_IP
#endif

#ifndef SOL_IPV6
#  define SOL_IPV6	IPPROTO_IPV6
#endif


int cw_udpfromto_init(int s, int family)
{
#if defined(HAVE_IP_PKTINFO) || defined(HAVE_IP_RECVDSTADDR) || defined(IPV6_RECVPKTINFO) || defined(IPV6_2292PKTINFO)
	static const int opt = 1;
	int err;

	errno = EPROTONOSUPPORT;
	err = -1;

	switch (family) {
		case AF_INET6:
#if defined(IPV6_RECVPKTINFO)
			err = setsockopt(s, SOL_IPV6, IPV6_RECVPKTINFO, &opt, sizeof(opt));
#elif defined(IPV6_2292PKTINFO)
			err = setsockopt(s, SOL_IPV6, IPV6_2292PKTINFO, &opt, sizeof(opt));
#endif
			/* Fall through - IPv4-mapped addresses are returned as IP CMSGs on IPv6 sockets */

		case AF_INET:
#if defined(HAVE_IP_PKTINFO)
			err = setsockopt(s, SOL_IP, IP_PKTINFO, &opt, sizeof(opt));
#elif defined(HAVE_IP_RECVDSTADDR)
			err = setsockopt(s, SOL_IP, IP_RECVDSTADDR, &opt, sizeof(opt));
#endif
			break;
	}

	return err;
#else
	CW_UNUSED(s);

	return 0;
#endif
}
	
int cw_recvfromto(int s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen, struct sockaddr *to, socklen_t *tolen)
{
#if defined(HAVE_IP_PKTINFO) || defined(HAVE_IP_RECVDSTADDR) || defined(IPV6_PKTINFO) || defined(IPV6_2292PKTINFO)
	char cbuf[256];
	struct msghdr msgh;
	struct iovec iov;
	struct cmsghdr *cmsg;
	int err;

	iov.iov_base = buf;
	iov.iov_len  = len;
	msgh.msg_iov  = &iov;
	msgh.msg_iovlen = 1;
	msgh.msg_name = from;
	msgh.msg_namelen = (fromlen ? *fromlen : 0);
	msgh.msg_control = NULL;
	msgh.msg_controllen = 0;
	msgh.msg_flags = 0;

	if (to && tolen) {
		msgh.msg_control = cbuf;
		msgh.msg_controllen = sizeof(cbuf);
	}

	if ((err = recvmsg(s, &msgh, flags)) < 0)
		return err;

	if (fromlen)
		*fromlen = msgh.msg_namelen;

	if (to && tolen) {
		for (cmsg = CMSG_FIRSTHDR(&msgh); cmsg != NULL; cmsg = CMSG_NXTHDR(&msgh,cmsg)) {
#if defined(HAVE_IP_PKTINFO)
			if (cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_PKTINFO) {
				struct in_pktinfo *pktinfo = (struct in_pktinfo *)CMSG_DATA(cmsg);
				to->sa_family = AF_INET;
				memcpy(&((struct sockaddr_in *)to)->sin_addr, &pktinfo->ipi_addr, sizeof(pktinfo->ipi_addr));
				*tolen = sizeof(struct sockaddr_in);
				break;
			}
#elif defined(HAVE_IP_RECVDSTADDR)
			if (cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVDSTADDR) {
				struct in_addr *inaddr = (struct in_addr *)CMSG_DATA(cmsg);
				to->sa_family = AF_INET;
				memcpy(&((struct sockaddr_in *)to)->sin_addr, inaddr, sizeof(*inaddr));
				*tolen = sizeof(struct sockaddr_in);
				break;
			}
#endif
#if defined(IPV6_PKTINFO) || defined(IPV6_2292PKTINFO)
#if defined(IPV6_PKTINFO)
			if (cmsg->cmsg_level == SOL_IPV6 && cmsg->cmsg_type == IPV6_PKTINFO)
#elif defined(IPV6_2292PKTINFO)
			if (cmsg->cmsg_level == SOL_IPV6 && cmsg->cmsg_type == IPV6_2292PKTINFO)
#endif
			{
				struct in6_pktinfo *pktinfo = (struct in6_pktinfo *)CMSG_DATA(cmsg);
				to->sa_family = AF_INET6;
				memcpy(&((struct sockaddr_in6 *)to)->sin6_addr, &pktinfo->ipi6_addr, sizeof(pktinfo->ipi6_addr));
				*tolen = sizeof(struct sockaddr_in6);
				break;
			}
#endif
		}
	}

	return err;
#else 
	/* fallback: call recvfrom */
	CW_UNUSED(to);
	CW_UNUSED(tolen);

	return recvfrom(s, buf, len, flags, from, fromlen);
#endif
}

int cw_sendfromto(int s, const void *buf, size_t len, int flags, const struct sockaddr *from, socklen_t fromlen, const struct sockaddr *to, socklen_t tolen)
{
#if defined(HAVE_IP_PKTINFO) || defined(HAVE_IP_SENDSRCADDR) || defined(IPV6_PKTINFO) || defined(IPV6_2292PKTINFO)
	if (from  &&  fromlen)
	{
		/* N.B. CMSG_SPACE for size of in6_pktinfo >= in_pktinfo >= in_addr. cmsgbuf may be used for all three */
		char cmsgbuf[CMSG_SPACE(sizeof(struct in6_pktinfo))];
		struct msghdr msgh;
		struct cmsghdr *cmsg;
		struct iovec iov;

		iov.iov_base = (char *)buf;
		iov.iov_len = len;
		msgh.msg_iov = &iov;
		msgh.msg_iovlen = 1;
		msgh.msg_name = (char *)to;
		msgh.msg_namelen = tolen;
		msgh.msg_flags = 0;
	
		switch (from->sa_family) {
			case AF_INET: {
#if defined(HAVE_IP_PKTINFO)
				struct in_pktinfo *pktinfo;

				msgh.msg_controllen = sizeof(cmsgbuf);
				msgh.msg_control = cmsgbuf;

				cmsg = CMSG_FIRSTHDR(&msgh);
				cmsg->cmsg_level = IPPROTO_IP;
				cmsg->cmsg_type = IP_PKTINFO;
				cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_pktinfo));

				memset(CMSG_DATA(cmsg), 0, sizeof(struct in_pktinfo));
				pktinfo = (struct in_pktinfo *)CMSG_DATA(cmsg);
				memcpy(&pktinfo->ipi_spec_dst, &((struct sockaddr_in *)from)->sin_addr, sizeof(struct in_addr));
				msgh.msg_controllen = CMSG_SPACE(sizeof(struct in_pktinfo));
#elif defined(HAVE_IP_SENDSRCADDR)
				msgh.msg_controllen = sizeof(cmsgbuf);
				msgh.msg_control = cmsgbuf;

				cmsg = CMSG_FIRSTHDR(&msgh);
				cmsg->cmsg_level = SOL_IP;
				cmsg->cmsg_type = IP_SENDSRCADDR;
				cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_addr));
				memcpy(CMSG_DATA(cmsg), &((struct sockaddr_in *)from)->sin_addr, sizeof(struct in_addr));
				msgh.msg_controllen = CMSG_SPACE(sizeof(struct in_addr));
#endif
				break;
			}

			case AF_INET6: {
#if defined(IPV6_PKTINFO) || defined(IPV6_2292PKTINFO)
				struct in6_pktinfo *pktinfo;

				msgh.msg_controllen = sizeof(cmsgbuf);
				msgh.msg_control = cmsgbuf;

				cmsg = CMSG_FIRSTHDR(&msgh);
				cmsg->cmsg_level = IPPROTO_IPV6;
#ifdef IPV6_PKTINFO
				cmsg->cmsg_type = IPV6_PKTINFO;
#else
				cmsg->cmsg_type = IPV6_2292PKTINFO;
#endif
				cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));

				memset(CMSG_DATA(cmsg), 0, sizeof(struct in6_pktinfo));
				pktinfo = (struct in6_pktinfo *)CMSG_DATA(cmsg);
				memcpy(&pktinfo->ipi6_addr, &((struct sockaddr_in6 *)from)->sin6_addr, sizeof(struct in6_addr));
				msgh.msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));
#endif
				break;
			}
		}

		return sendmsg(s, &msgh, flags);
	}
#else
	/* fallback: call sendto() */
	CW_UNUSED(from);
	CW_UNUSED(fromlen);
#endif

	return sendto(s, buf, len, flags, to, tolen);
}
