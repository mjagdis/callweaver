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
	struct cw_acl *next;
	/* This must be last */
	struct sockaddr addr;
};

/* Default IP - if not otherwise set, don't breathe garbage */
static struct in_addr __ourip = { 0x00000000 };

struct my_ifreq {
	char ifrn_name[IFNAMSIZ];	/* Interface name, e.g. "eth0", "ppp0", etc.  */
	struct sockaddr_in ifru_addr;
};


void cw_acl_free(struct cw_acl *acl)
{
	struct cw_acl *ace;

	while ((ace = acl)) {
		acl = ace->next;
		free(ace);
	}
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

	if (!(err = cw_getaddrinfo(spec, &hints, &addrs, &masklen))) {
		struct cw_dynstr ds = CW_DYNSTR_INIT;
		struct addrinfo *addr;

		for (addr = addrs; addr; addr = addr->ai_next) {
			struct cw_acl *ace;

			if ((ace = malloc(sizeof(*ace) - sizeof(ace->addr) + addr->ai_addrlen))) {
				ace->sense = (sense[0] == 'p' || sense[0] == 'P');
				ace->masklen = masklen;
				ace->next = *acl_p;
				memcpy(&ace->addr, addr->ai_addr, addr->ai_addrlen);
				*acl_p = ace;

				if (option_debug) {
					cw_dynstr_reset(&ds);
					cw_address_print(&ds, addr->ai_addr, masklen, ":%hu");
					cw_log(CW_LOG_DEBUG, "%s (%s) appended to acl\n", ds.data, spec);
				}
			}
		}

		freeaddrinfo(addrs);
	}

	return err;
}


int cw_acl_check(struct cw_acl *acl, struct sockaddr *addr)
{
	int res = -1;

	while (acl) {
		if (!cw_address_cmp(addr, &acl->addr, acl->masklen, 1)) {
			if (option_debug > 5) {
				struct cw_dynstr ds = CW_DYNSTR_INIT;

				cw_address_print(&ds, addr, -1, ":%hu");
				cw_dynstr_printf(&ds, " matches ");
				cw_address_print(&ds, &acl->addr, acl->masklen, ":%hu");

				cw_log(CW_LOG_DEBUG, "%s => %s\n", ds.data, (acl->sense ? "permit" : "deny"));

				cw_dynstr_free(&ds);
			}

			res = acl->sense;
			break;
		}

		acl = acl->next;
	}

	return res;
}


void cw_acl_print(struct cw_dynstr *ds_p, struct cw_acl *acl)
{
	int first = 1;

	while (acl) {
		cw_dynstr_tprintf(ds_p, 2,
			cw_fmtval("%s", (!first ? ", " : "")),
			cw_fmtval("%s ", (acl->sense ? "permit" : "deny"))
		);
		cw_address_print(ds_p, &acl->addr, acl->masklen, ":%hu");

		first = 0;
		acl = acl->next;
	}
}


int cw_get_ip_or_srv(struct sockaddr_in *sin, const char *value, const char *service)
{
	struct hostent *hp;
	struct cw_hostent ahp;
	char srv[256];
	char host[256];
	int tportno = ntohs(sin->sin_port);

	sin->sin_family = AF_INET;

	if (inet_aton(value, &sin->sin_addr))
		return 0;

	if (service) {
		snprintf(srv, sizeof(srv), "%s.%s", service, value);
		if (cw_get_srv(NULL, host, sizeof(host), &tportno, srv) > 0) {
			sin->sin_port = htons(tportno);
			value = host;
		}
	}

	hp = cw_gethostbyname(value, &ahp);
	if (hp) {
		memcpy(&sin->sin_addr, hp->h_addr, sizeof(sin->sin_addr));
		return 0;
	}

	return -1;
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

/* iface is the interface (e.g. eth0); address is the return value */
int cw_lookup_iface(char *iface, struct in_addr *address) 
{
	int mysock, res = -1;
	struct my_ifreq ifreq;

	if ((mysock = socket_cloexec(PF_INET, SOCK_DGRAM, IPPROTO_IP)) >= 0) {
		memset(&ifreq, 0, sizeof(ifreq));
		cw_copy_string(ifreq.ifrn_name, iface, sizeof(ifreq.ifrn_name));
		res = ioctl(mysock, SIOCGIFADDR, &ifreq);
		close(mysock);
	}

	if (res < 0) {
		cw_log(CW_LOG_WARNING, "Unable to get IP of %s: %s\n", iface, strerror(errno));
		memcpy((char *)address, (char *)&__ourip, sizeof(__ourip));
	} else {
		memcpy((char *)address, (char *)&ifreq.ifru_addr.sin_addr, sizeof(ifreq.ifru_addr.sin_addr));
	}

	return res;
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

int cw_find_ourip(struct in_addr *ourip, struct sockaddr_in bindaddr)
{
	char ourhost[MAXHOSTNAMELEN] = "";
	struct cw_hostent ahp;
	struct hostent *hp;
	struct in_addr saddr;

	/* just use the bind address if it is nonzero */
	if (ntohl(bindaddr.sin_addr.s_addr)) {
		memcpy(ourip, &bindaddr.sin_addr, sizeof(*ourip));
		return 0;
	}
	/* try to use our hostname */
	if (gethostname(ourhost, sizeof(ourhost) - 1)) {
		cw_log(CW_LOG_WARNING, "Unable to get hostname\n");
	} else {
		hp = cw_gethostbyname(ourhost, &ahp);
		if (hp) {
			memcpy(ourip, hp->h_addr, sizeof(*ourip));
			return 0;
		}
	}
	/* A.ROOT-SERVERS.NET. */
	if (inet_aton("198.41.0.4", &saddr) && !cw_ouraddrfor(&saddr, ourip))
		return 0;
	return -1;
}

