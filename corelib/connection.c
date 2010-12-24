/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Eris Associates Limited, UK
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

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/callweaver_hash.h"
#include "callweaver/connection.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/utils.h"


#ifndef AI_IDN
#  define AI_IDN 0
#endif


const char *cw_connection_state_name[] = {
	[INIT]       = "INIT",
	[LISTENING]  = "LISTENING",
	[CONNECTING] = "CONNECTING",
	[CONNECTED]  = "CONNECTED",
	[SHUTDOWN]   = "SHUTDOWN",
	[CLOSED]     = "CLOSED",
};


int cw_getaddrinfo(const char *spec, const struct addrinfo *hints, struct addrinfo **res)
{
	char *node = strdupa(spec);
	const char *service = "0";
	char *p;

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

	return getaddrinfo(node, service, hints, res);
}


void cw_address_print(struct cw_dynstr *ds_p, const struct sockaddr *addr, const char *portfmt)
{
	switch (addr->sa_family) {
		case AF_INET: {
			struct sockaddr_in *sin = (struct sockaddr_in *)addr;

			cw_dynstr_need(ds_p, INET_ADDRSTRLEN);
			/* If the need failed above then we're already in error */
			if (!ds_p->error) {
				inet_ntop(sin->sin_family, &sin->sin_addr, &ds_p->data[ds_p->used], ds_p->size - ds_p->used);
				ds_p->used += strlen(&ds_p->data[ds_p->used]);
			}
			if (portfmt && sin->sin_port)
				cw_dynstr_printf(ds_p, portfmt, ntohs(sin->sin_port));
			break;
		}

		case AF_INET6: {
			struct sockaddr_in6 *sin = (struct sockaddr_in6 *)addr;

			if (sin->sin6_port && portfmt && portfmt[0] == ':')
				cw_dynstr_printf(ds_p, "[");
			cw_dynstr_need(ds_p, INET6_ADDRSTRLEN);
			/* If the need failed above then we're already in error */
			if (!ds_p->error) {
				inet_ntop(sin->sin6_family, &sin->sin6_addr, &ds_p->data[ds_p->used], ds_p->size - ds_p->used);
				ds_p->used += strlen(&ds_p->data[ds_p->used]);
			}
			if (sin->sin6_port && portfmt) {
				if (portfmt[0] == ':')
					cw_dynstr_printf(ds_p, "]");
				cw_dynstr_printf(ds_p, portfmt, ntohs(sin->sin6_port));
			}
			break;
		}

		case AF_LOCAL:
		case AF_PATHNAME:
		case AF_INTERNAL: {
			struct sockaddr_un *sun = (struct sockaddr_un *)addr;
			const char *domain;

			switch (addr->sa_family) {
				case AF_LOCAL:
					domain = "local";
					break;
				case AF_PATHNAME:
					domain = "file";
					break;
				case AF_INTERNAL:
				default:
					domain = "internal";
					break;
			}

			cw_dynstr_printf(ds_p, "%s:%s", domain, sun->sun_path);
			break;
		}
	}
}


unsigned int cw_address_hash(const struct sockaddr *addr, int withport)
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


int cw_address_cmp(const struct sockaddr *a, const struct sockaddr *b, int withport)
{
	int ret;

	if (!(ret = a->sa_family - b->sa_family)) {
		switch (a->sa_family) {
			case AF_INET: {
				struct sockaddr_in *sa = (struct sockaddr_in *)a;
				struct sockaddr_in *sb = (struct sockaddr_in *)b;
				if (!(ret = memcmp(&sa->sin_addr, &sb->sin_addr, sizeof(sa->sin_addr))) && withport)
					ret = memcmp(&sa->sin_port, &sb->sin_port, sizeof(sa->sin_port));
				break;
			}

			case AF_INET6: {
				struct sockaddr_in6 *sa = (struct sockaddr_in6 *)a;
				struct sockaddr_in6 *sb = (struct sockaddr_in6 *)b;
				if (!(ret = memcmp(&sa->sin6_addr, &sb->sin6_addr, sizeof(sa->sin6_addr))) && withport)
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


static int cw_connection_qsort_by_addr(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct cw_connection *conn_a = container_of(*objp_a, struct cw_connection, obj);
	const struct cw_connection *conn_b = container_of(*objp_b, struct cw_connection, obj);

	return cw_address_cmp(&conn_a->addr, &conn_b->addr, 1);
}

static int cw_connection_object_match(struct cw_object *obj, const void *pattern)
{
	struct cw_connection *conn = container_of(obj, struct cw_connection, obj);
	return cw_address_cmp(&conn->addr, pattern, 1);
}

struct cw_registry cw_connection_registry = {
	.name = "Connections",
	.qsort_compare = cw_connection_qsort_by_addr,
	.match = cw_connection_object_match,
};


static void service_thread_cleanup(void *data)
{
	struct cw_connection *conn = data;

	cw_object_put(conn);
}


static void *service_thread(void *data)
{
	struct cw_connection *conn = data;
	struct pollfd pfd = {
		.fd = conn->sock,
		.events = POLLIN,
	};
	int n;

	pthread_cleanup_push(service_thread_cleanup, conn);

	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		n = poll(&pfd, 1, -1);
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (n == 1)
			n = conn->tech->read(conn);
		else if (n < 0) {
			if (errno == EINTR)
				continue;
			if (errno != ENOMEM)
				break;
			n = 1000;
		}

		if (unlikely(n > 0))
			sleep(n / 1000);
		if (unlikely(n < 0))
			break;
	}

	pthread_cleanup_pop(1);
	return NULL;
}


static void cw_connection_release(struct cw_object *obj)
{
	struct cw_connection *conn = container_of(obj, struct cw_connection, obj);

	if (conn->pvt_obj)
		cw_object_put_obj(conn->pvt_obj);
	cw_object_destroy(conn);
	free(conn);
}


void cw_connection_close(struct cw_connection *conn)
{
	if (!pthread_equal(conn->tid, CW_PTHREADT_NULL)) {
		pthread_cancel(conn->tid);
		pthread_join(conn->tid, NULL);
	}

	if (conn->sock >= 0) {
		close(conn->sock);
		conn->sock = -1;

		if ((conn->state == INIT || conn->state == LISTENING) && conn->addr.sa_family == AF_LOCAL)
			unlink(((struct sockaddr_un *)&conn->addr)->sun_path);
	}

	conn->state = CLOSED;

	cw_registry_del(&cw_connection_registry, conn->reg_entry);
}


int cw_connection_listen(int type, struct sockaddr *addr, socklen_t addrlen, const struct cw_connection_tech *tech, struct cw_object *pvt_obj)
{
	struct cw_connection *conn = NULL;
	int sock;
	const int arg = 1;
	int res = 0;

	if ((sock = socket_cloexec(addr->sa_family, type, 0)) < 0)
		goto out_err;

	if (bind(sock, addr, addrlen) || listen(sock, 1024))
		goto out_close;

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &arg, sizeof(arg));

	if (!(conn = malloc(sizeof(*conn) - sizeof (conn->addr) + addrlen)))
		goto out_close;

	cw_object_init(conn, NULL, 1);
	conn->obj.release = cw_connection_release;
	conn->state = LISTENING;
	conn->sock = sock;
	conn->tech = tech;
	conn->pvt_obj = pvt_obj;

	conn->addrlen = addrlen;
	memcpy(&conn->addr, addr, addrlen);

	conn->reg_entry = NULL;
	conn->tid = CW_PTHREADT_NULL;


	if (!(conn->reg_entry = cw_registry_add(&cw_connection_registry, cw_address_hash(addr, 1), &conn->obj)))
		goto out_free;

	/* Hand off our reference to conn now */
	if (!(errno = cw_pthread_create(&conn->tid, &global_attr_default, service_thread, conn)))
		goto out_ok;

	cw_registry_del(&cw_connection_registry, conn->reg_entry);
	cw_object_put(conn);

out_free:
	free(conn);
out_close:
	close(sock);
out_err:
	res = errno;
out_ok:
	return res;
}
