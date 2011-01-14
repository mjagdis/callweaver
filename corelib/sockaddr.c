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

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/sockaddr.h"
#include "callweaver/callweaver_hash.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/utils.h"


int cw_getaddrinfo(const char *spec, const char *service, const struct addrinfo *hints, struct addrinfo **res, int *masklen)
{
	char *node;
	char *p;
	int n;

	n = strlen(spec) + 1;
	node = alloca(n);
	memcpy(node, spec, n);

	/* A service name / port number follows the last ':' in the spec
	 * _only_ if the character before the ':' is ']' or something
	 * other than a hexdigit or ':' is in the string of characters
	 * before it.
	 * i.e. we support:
	 *     fqdn:port
	 *     fqdn
	 *     a.b.c.d:port
	 *     a.b.c.d
	 *     [aaaa:bbbb::cccc]:port
	 *     aaaa:bbbb::cccc
	 *     [aaaa:bbbb::c.d.e.f]:port
	 *     aaaa:bbbb::c.d.e.f
	 * and also
	 *     [a.b.c.d]:port
	 * which is non-standard but which is maybe useful?
	 */
	if ((p = strrchr(node, ':'))) {
		if (p == node) {
			*p = '\0';
			service = &p[1];
			node = NULL;
		} else if (p[-1] == ']' && node[0] == '[') {
			p[-1] = '\0';
			service = &p[1];
			node++;
		} else {
			char *q;
			int colons = 0;
			for (q = node; q != p; q++) {
				if (!isxdigit(*q) && *q != ':' && (*q != '.' || colons < 2)) {
					*p = '\0';
					service = &p[1];
					break;
				}
				if (*q == ':')
					colons++;
			}
		}
	}

	if (masklen) {
		*masklen = -1;
		if ((p = strrchr(node, '/'))) {
			if (!strchr(p + 1, '.')) {
				char *q;

				n = strtoul(p + 1, &q, 10);
				if (*q == '\0') {
					*p = '\0';
					*masklen = n;
				}
			} else {
				struct in_addr m;

				if (inet_aton(p + 1, &m)) {
					*p = '\0';
					*masklen = 0;
					while (m.s_addr) {
						m.s_addr &= m.s_addr - 1;
						(*masklen)++;
					}
				}
			}
		}
	}

	return getaddrinfo((node[0] ? node : NULL), service, hints, res);
}


unsigned int cw_sockaddr_hash(const struct sockaddr *addr, int withport)
{
	unsigned int hash;

	hash = cw_hash_mem(0, &addr->sa_family, sizeof(addr->sa_family));

	switch (addr->sa_family) {
		case AF_INET: {
			struct sockaddr_in *sin = (struct sockaddr_in *)addr;

			hash = cw_hash_mem(hash, &sin->sin_addr, sizeof(sin->sin_addr));
			if (withport)
				hash = cw_hash_mem(hash, &sin->sin_port, sizeof(sin->sin_port));
			break;
		}
		case AF_INET6: {
			struct sockaddr_in6 *sin = (struct sockaddr_in6 *)addr;

			hash = cw_hash_mem(hash, &sin->sin6_addr, sizeof(sin->sin6_addr));
			if (withport)
				hash = cw_hash_mem(hash, &sin->sin6_port, sizeof(sin->sin6_port));
			break;
		}
		case AF_LOCAL:
#if AF_LOCAL != AF_UNIX
		case AF_UNIX:
#endif
		case AF_PATHNAME:
		case AF_INTERNAL: {
			struct sockaddr_un *sun = (struct sockaddr_un *)addr;

			hash = cw_hash_string(hash, sun->sun_path);
			break;
		}
	}

	return hash;
}


static int memcmpbits(const void *a, const void *b, size_t nbits)
{
	unsigned int filled = nbits / 8;
	int ret;

	if (unlikely(!(ret = memcmp(a, b, filled)))) {
		unsigned char m = 0xff << (8 - (nbits % 8));
		ret = (int)(((const unsigned char *)a)[filled] & m) - (int)(((const unsigned char *)b)[filled] & m);
	}

	return ret;
}


int cw_sockaddr_cmp(const struct sockaddr *a, const struct sockaddr *b, int masklen, int withport)
{
	struct sockaddr_in sinbuf;
	int ret;

	if (b->sa_family == AF_INET) {
		if (cw_sockaddr_is_mapped(a))
			cw_sockaddr_unmap(&sinbuf, (struct sockaddr_in6 *)a);
	} else if (a->sa_family == AF_INET) {
		if (cw_sockaddr_is_mapped(b)) {
			cw_sockaddr_unmap(&sinbuf, (struct sockaddr_in6 *)b);
			masklen -= (sizeof(((struct sockaddr_in6 *)0)->sin6_addr) - sizeof(((struct sockaddr_in *)0)->sin_addr)) * 8;
		}
	}

	if (!(ret = a->sa_family - b->sa_family)) {
		switch (a->sa_family) {
			case AF_INET: {
				struct sockaddr_in *sa = (struct sockaddr_in *)a;
				struct sockaddr_in *sb = (struct sockaddr_in *)b;
				if (!(ret = memcmpbits(&sa->sin_addr, &sb->sin_addr, (masklen < 0 || masklen >= sizeof(sa->sin_addr) * 8 ? sizeof(sa->sin_addr) * 8 : masklen))) && withport && sa->sin_port && sb->sin_port)
					ret = memcmp(&sa->sin_port, &sb->sin_port, sizeof(sa->sin_port));
				break;
			}

			case AF_INET6: {
				struct sockaddr_in6 *sa = (struct sockaddr_in6 *)a;
				struct sockaddr_in6 *sb = (struct sockaddr_in6 *)b;
				if (!(ret = memcmpbits(&sa->sin6_addr, &sb->sin6_addr, (masklen < 0 || masklen >= sizeof(sa->sin6_addr) * 8 ? sizeof(sa->sin6_addr) * 8 : masklen))) && withport && sa->sin6_port && sb->sin6_port)
					ret = memcmp(&sa->sin6_port, &sb->sin6_port, sizeof(sa->sin6_port));
				break;
			}

			case AF_LOCAL:
#if AF_LOCAL != AF_UNIX
			case AF_UNIX:
#endif
			case AF_PATHNAME:
			case AF_INTERNAL: {
				struct sockaddr_un *sa = (struct sockaddr_un *)a;
				struct sockaddr_un *sb = (struct sockaddr_un *)b;
				ret = strcmp(sa->sun_path, sb->sun_path);
				break;
			}
		}
	}

	return ret;
}
