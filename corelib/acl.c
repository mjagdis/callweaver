/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2010 - 2011, Eris Associates Limited, UK
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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

/*! \file
 *
 * \brief Various sorts of access control
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
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>

#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__)
#include <fcntl.h>
#include <net/route.h>
#endif

#if defined(SOLARIS)
#include <sys/sockio.h>
#endif

/* netinet/ip.h may not define the following (See RFCs 791 and 1349) */
#if !defined(IPTOS_LOWCOST)
#define       IPTOS_LOWCOST           0x02
#endif

#if !defined(IPTOS_MINCOST)
#define       IPTOS_MINCOST           IPTOS_LOWCOST
#endif

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/acl.h"
#include "callweaver/connection.h"
#include "callweaver/registry.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/options.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/srv.h"


struct cw_acl {
	/* Host access rule */
	int sense;
	int masklen;
	struct cw_list list;
	/* This must be last */
	struct sockaddr addr;
};


void cw_acl_free(struct cw_acl *acl)
{
	struct cw_acl *ace;

	if ((ace = acl)) {
		do {
			struct cw_acl *tmp = ace;
			ace = container_of(ace->list.next, struct cw_acl, list);
			free(tmp);
		} while (ace != acl);
	}
}


int cw_acl_add_addr(struct cw_acl **acl_p, const char *sense, const struct sockaddr *addr, socklen_t addrlen, int masklen)
{
	struct cw_acl *ace;
	int err = EAI_MEMORY;
	int is_mapped;
	
	if ((is_mapped = cw_sockaddr_is_mapped(addr)))
		addrlen = sizeof(struct sockaddr_in);

	if ((ace = malloc(sizeof(*ace) - sizeof(ace->addr) + addrlen))) {
		ace->sense = (sense[0] == 'p' || sense[0] == 'P');

		if (is_mapped) {
			ace->masklen = masklen - (sizeof(((struct sockaddr_in6 *)0)->sin6_addr.s6_addr) - sizeof(((struct sockaddr_in *)0)->sin_addr.s_addr)) * 8;
			cw_sockaddr_unmap((struct sockaddr_in *)&ace->addr, (struct sockaddr_in6 *)addr);
		} else {
			ace->masklen = masklen;
			memcpy(&ace->addr, addr, addrlen);
		}

		if (*acl_p)
			cw_list_add((*acl_p)->list.prev, &ace->list);
		else {
			cw_list_init(&ace->list);
			*acl_p = ace;
		}

		err = 0;
	}

	return err;
}


int cw_acl_add(struct cw_acl **acl_p, const char *sense, const char *spec)
{
	static const struct addrinfo hints = {
		.ai_flags = AI_V4MAPPED | AI_PASSIVE | AI_IDN,
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = 0,
	};
	struct addrinfo *addrs;
	int err, masklen;

	if (!(err = cw_getaddrinfo(spec, "0", &hints, &addrs, &masklen))) {
		struct addrinfo *addr;

		for (addr = addrs; addr; addr = addr->ai_next)
			cw_acl_add_addr(acl_p, sense, addr->ai_addr, addr->ai_addrlen, masklen);

		freeaddrinfo(addrs);
	}

	return err;
}


int cw_acl_check(struct cw_acl *acl, struct sockaddr *addr, int defsense)
{
	int res = defsense;

	if (acl) {
		struct sockaddr_in sinbuf;
		struct cw_list *l = acl->list.prev;

		if (cw_sockaddr_is_mapped(addr)) {
			cw_sockaddr_unmap(&sinbuf, (struct sockaddr_in6 *)addr);
			addr = (struct sockaddr *)&sinbuf;
		}

		do {
			struct cw_acl *ace = container_of(l, struct cw_acl, list);

			if (!cw_sockaddr_cmp(addr, &ace->addr, ace->masklen, 1)) {
				if (option_debug > 5)
					cw_log(CW_LOG_DEBUG, "%l@ matches %.*l@ => %s\n", addr, ace->masklen, &ace->addr, (ace->sense ? "permit" : "deny"));

				res = ace->sense;
				break;
			}

			l = l->prev;
		} while (l != acl->list.prev);
	}

	return res;
}


void cw_acl_print(struct cw_dynstr *ds_p, struct cw_acl *acl)
{
	struct cw_acl *ace;
	int first = 1;

	if ((ace = acl)) {
		do {
			cw_dynstr_tprintf(ds_p, 3,
				cw_fmtval("%s",    (!first ? ", " : "")),
				cw_fmtval("%s ",   (ace->sense ? "permit" : "deny")),
				cw_fmtval("%.*l@", ace->masklen, &ace->addr)
			);

			first = 0;

			ace = container_of(ace->list.next, struct cw_acl, list);
		} while (ace != acl);
	}
}


int cw_get_ip_or_srv(int family, struct sockaddr *addr, const char *value, const char *service)
{
	struct addrinfo hints = {
		.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_IDN | AI_NUMERICHOST,
		.ai_family = family,
		.ai_socktype = SOCK_DGRAM,
		.ai_protocol = 0,
	};
	struct addrinfo *addrs;
	int err;
	uint16_t portno;

	portno = cw_sockaddr_get_port(addr);

	/* Try with AI_NUMERICHOST first */
	if ((err = cw_getaddrinfo(value, "0", &hints, &addrs, NULL))) {
		/* So it's a name not an address. If we have a service map the
		 * given name to a suitable server.
		 */
		if (service) {
			char srv[MAXHOSTNAMELEN];
			char host[MAXHOSTNAMELEN];
			int tportno;

			snprintf(srv, sizeof(srv), "%s.%s", service, value);
			if (cw_get_srv(NULL, host, sizeof(host), &tportno, srv) > 0) {
				portno = tportno;
				value = host;
			}
		}

		hints.ai_flags &= (~AI_NUMERICHOST);
		err = cw_getaddrinfo(value, "0", &hints, &addrs, NULL);
	}

	if (!err) {
		memcpy(addr, addrs->ai_addr, addrs->ai_addrlen);

		if (!cw_sockaddr_get_port(addr))
			cw_sockaddr_set_port(addr, portno);

		freeaddrinfo(addrs);
	}

	return err;
}


int cw_str2tos(const char *value, int *tos)
{
	int fval;
	if (sscanf(value, "%i", &fval) == 1)
		*tos = fval & 0xff;
	else if (!strcasecmp(value, "lowdelay"))
		*tos = IPTOS_LOWDELAY;
	else if (!strcasecmp(value, "throughput"))
		*tos = IPTOS_THROUGHPUT;
	else if (!strcasecmp(value, "reliability"))
		*tos = IPTOS_RELIABILITY;
	else if (!strcasecmp(value, "mincost"))
		*tos = IPTOS_MINCOST;
	else if (!strcasecmp(value, "none"))
		*tos = 0;
	else
		return -1;
	return 0;
}


int cw_ouraddrfor(struct in_addr *them, struct in_addr *us)
{
	int s;
	struct sockaddr_in sin;
	socklen_t slen;

	s = socket_cloexec(PF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		cw_log(CW_LOG_WARNING, "Cannot create socket\n");
		return -1;
	}
	sin.sin_family = AF_INET;
	sin.sin_port = 5060;
	sin.sin_addr = *them;
	if (connect(s, (struct sockaddr *)&sin, sizeof(sin))) {
		cw_log(CW_LOG_WARNING, "Cannot connect\n");
		close(s);
		return -1;
	}
	slen = sizeof(sin);
	if (getsockname(s, (struct sockaddr *)&sin, &slen)) {
		cw_log(CW_LOG_WARNING, "Cannot get socket name\n");
		close(s);
		return -1;
	}
	close(s);
	*us = sin.sin_addr;
	return 0;
}
