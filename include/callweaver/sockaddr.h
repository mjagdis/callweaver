/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009 - 2010, Eris Associates Limited, UK
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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

#ifndef _CALLWEAVER_SOCKADDR_H
#define _CALLWEAVER_SOCKADDR_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <string.h>


#ifndef AI_IDN
#  define AI_IDN 0
#endif


#define CW_SOCKADDR_UN_SIZE(pathlen) ((size_t)(((struct sockaddr_un *)0)->sun_path + (pathlen)))

#ifndef SUN_LEN
#  include <string.h>
#  define SUN_LEN(ptr) ((size_t)(((struct sockaddr_un *)0)->sun_path) + strlen ((ptr)->sun_path))
#endif


#define CW_MAX_ADDRSTRLEN	(INET6_ADDRSTRLEN + sizeof("[/nnn]:nnnnn") - 1)


struct cw_sockaddr_net {
	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_in6 sin6;
	};
};


static inline __attribute__ (( __pure__, __nonnull__ (1) )) size_t cw_sockaddr_len(const struct sockaddr *addr)
{
	size_t ret = 0;

	switch (addr->sa_family) {
		case AF_INET:
			ret = sizeof(struct sockaddr_in);
			break;

		case AF_INET6:
			ret = sizeof(struct sockaddr_in6);
			break;
	}

	return ret;
}


static inline __attribute__ (( __pure__, __nonnull__ (1) )) int cw_sockaddr_is_specific(const struct sockaddr *addr)
{
	int res = 0;

	switch (addr->sa_family) {
		case AF_INET:
			res = ((const struct sockaddr_in *)addr)->sin_addr.s_addr;
			break;

		case AF_INET6: {
			const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)addr;
			res = memcmp(&sin6->sin6_addr, &in6addr_any, sizeof(in6addr_any));
			break;
		}
	}

	return res;
}


static inline __attribute__ (( __pure__, __nonnull__ (1) )) uint16_t cw_sockaddr_get_port(const struct sockaddr *addr)
{
	uint16_t port = 0;

	switch (addr->sa_family) {
		case AF_INET:
			port = ((const struct sockaddr_in *)addr)->sin_port;
			break;

		case AF_INET6:
			port = ((const struct sockaddr_in6 *)addr)->sin6_port;
			break;
	}

	return ntohs(port);
}


static inline __attribute__ (( __nonnull__ (1) )) void cw_sockaddr_set_port(struct sockaddr *addr, uint16_t port)
{
	port = htons(port);

	switch (addr->sa_family) {
		case AF_INET:
			((struct sockaddr_in *)addr)->sin_port = port;
			break;

		case AF_INET6:
			((struct sockaddr_in6 *)addr)->sin6_port = port;
			break;
	}
}


static inline __attribute__ (( __nonnull__ (1) )) int cw_sockaddr_is_mapped(const struct sockaddr *sa)
{
	int ret;

	if ((ret = (sa->sa_family == AF_INET6))) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
		int i;

		for (i = 0; i < 10; i++)
			if (!(ret = (sin6->sin6_addr.s6_addr[i] == 0x00)))
				goto out;

		ret = (sin6->sin6_addr.s6_addr[10] == 0xff && sin6->sin6_addr.s6_addr[11] == 0xff);
	}

out:
	return ret;
}


static inline __attribute__ (( __nonnull__ (1,2) )) void cw_sockaddr_map(struct sockaddr_in6 * __restrict__ dst, const struct sockaddr_in * __restrict__ src)
{
	memset(dst, 0, sizeof(*dst));
	dst->sin6_family = AF_INET6;
	dst->sin6_addr.s6_addr[10] = dst->sin6_addr.s6_addr[11] = 0xff;
	memcpy(&dst->sin6_addr.s6_addr[12], &src->sin_addr.s_addr, sizeof(src->sin_addr.s_addr));
	dst->sin6_port = src->sin_port;
}


static inline __attribute__ (( __nonnull__ (1,2) )) void cw_sockaddr_unmap(struct sockaddr_in * __restrict__ dst, const struct sockaddr_in6 * __restrict__ src)
{
	memset(dst, 0, sizeof(*dst));
	dst->sin_family = AF_INET;
	memcpy(&dst->sin_addr.s_addr, &src->sin6_addr.s6_addr[12], sizeof(dst->sin_addr.s_addr));
	dst->sin_port = src->sin6_port;
}


/* As getaddrinfo(3) but takes a combined host & service with an optional masklen (as /n or /a.b.c.d) after the host part.
 * If a service is specified as part of the spec it overrides any service specified separately.
 */
extern CW_API_PUBLIC int cw_getaddrinfo(const char *spec, const char *service, const struct addrinfo *hints, struct addrinfo **res, int *masklen);

extern CW_API_PUBLIC unsigned int cw_sockaddr_hash(const struct sockaddr *addr, int withport);
extern CW_API_PUBLIC int cw_sockaddr_cmp(const struct sockaddr *a, const struct sockaddr *b, int masklen, int withport);


#endif /* _CALLWEAVER_SOCKADDR_H */
