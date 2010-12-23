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


const char *cw_connection_state_name[] = {
	[INIT]       = "INIT",
	[LISTENING]  = "LISTENING",
	[CONNECTING] = "CONNECTING",
	[CONNECTED]  = "CONNECTED",
	[SHUTDOWN]   = "SHUTDOWN",
	[CLOSED]     = "CLOSED",
};


static const socklen_t salen[] = {
	[AF_LOCAL] = sizeof(struct sockaddr_un),
	[AF_INTERNAL] = sizeof(struct sockaddr_un),
	[AF_INET] = sizeof(struct sockaddr_in),
	[AF_INET6] = sizeof(struct sockaddr_in6),
	[AF_PATHNAME] = sizeof(struct sockaddr_un),
#if AF_LOCAL != AF_UNIX
	[AF_UNIX] = sizeof(struct sockaddr_un),
#endif
};


int cw_address_parse(const char *spec, cw_sockaddr_t *addr, socklen_t *addrlen)
{
	char *p;
	int portno;
	int ret = -1;

	/* An IPv6 address may be bare, in which case it contains at least two colons, or it
	 * may be enclosed in square brackets with an optional ":<portno>" suffix.
	 */
	if (spec[0] == '[' || ((p = strchr(spec, ':')) && strchr(p + 1, ':'))) {
		memset(addr, 0, sizeof(addr->sin6));
		addr->sin6.sin6_family = AF_INET6;
		memset(&addr->sin6.sin6_addr, 0, sizeof(addr->sin6.sin6_addr));
		addr->sin6.sin6_port = 0;

		/* You can only have a portno if the IPv6 address is enclosed in [...] otherwise
		 * there's no way to tell where the IPv6 address ends and the port suffix starts.
		 */
		if (spec[0] == '[' && (p = strrchr(spec, ']'))) {
			if (p[1] == ':') {
				if (sscanf(p + 2, "%d", &portno) != 1) {
					cw_log(CW_LOG_ERROR, "Invalid port number '%s' in '%s'\n", p + 2, spec);
					goto err;
				}
				addr->sin6.sin6_port = htons(portno);
			}
			spec++;
			*p = '\0';
		}

		if (inet_pton(AF_INET6, spec, &addr->sin6.sin6_addr) <= 0) {
			cw_log(CW_LOG_ERROR, "Invalid IPv6 address '%s' specified\n", spec);
			goto err;
		}
		ret = 0;

	/* Consider anything else to be an IPv4 address. */
	} else {
		addr->sin.sin_family = AF_INET;
		memset(&addr->sin.sin_addr, 0, sizeof(addr->sin.sin_addr));
		addr->sin.sin_port = 0;

		if ((p = strrchr(spec, ':'))) {
			if (sscanf(p + 1, "%d", &portno) != 1) {
				cw_log(CW_LOG_ERROR, "Invalid port number '%s' in '%s'\n", p + 1, spec);
				goto err;
			}
			addr->sin.sin_port = htons(portno);
			*p = '\0';
		}

		if (!inet_aton(spec, &addr->sin.sin_addr)) {
			cw_log(CW_LOG_ERROR, "Invalid IPv4 address '%s' specified\n", spec);
			goto err;
		}

		ret = 0;
	}

	*addrlen = salen[addr->sa.sa_family];

err:
	return ret;
}


void cw_address_print(struct cw_dynstr *ds_p, const cw_sockaddr_t *addr)
{
	switch (addr->sa.sa_family) {
		case AF_INET: {
			cw_dynstr_need(ds_p, INET_ADDRSTRLEN);
			/* If the need failed above then we're already in error */
			if (!ds_p->error) {
				inet_ntop(addr->sa.sa_family, &addr->sin.sin_addr, &ds_p->data[ds_p->used], ds_p->size - ds_p->used);
				ds_p->used += strlen(&ds_p->data[ds_p->used]);
			}
			if (addr->sin.sin_port)
				cw_dynstr_printf(ds_p, ":%hu", ntohs(addr->sin.sin_port));
			break;
		}

		case AF_INET6: {
			if (addr->sin6.sin6_port)
				cw_dynstr_printf(ds_p, "[");
			cw_dynstr_need(ds_p, INET6_ADDRSTRLEN);
			/* If the need failed above then we're already in error */
			if (!ds_p->error) {
				inet_ntop(addr->sa.sa_family, &addr->sin6.sin6_addr, &ds_p->data[ds_p->used], ds_p->size - ds_p->used);
				ds_p->used += strlen(&ds_p->data[ds_p->used]);
			}
			if (addr->sin6.sin6_port)
				cw_dynstr_printf(ds_p, "]:%hu", ntohs(addr->sin6.sin6_port));
			break;
		}

		case AF_LOCAL:
			cw_dynstr_printf(ds_p, "local:%s", addr->sun.sun_path);
			break;

		case AF_PATHNAME:
			cw_dynstr_printf(ds_p, "file:%s", addr->sun.sun_path);
			break;

		case AF_INTERNAL:
			cw_dynstr_printf(ds_p, "internal:%s", addr->sun.sun_path);
			break;
	}
}


unsigned int cw_address_hash(const cw_sockaddr_t *addr, int withport)
{
	unsigned int hash;

	hash = cw_hash_mem(0, &addr->sa.sa_family, sizeof(addr->sa.sa_family));

	switch (addr->sa.sa_family) {
		case AF_INET:
			hash = cw_hash_mem(hash, &addr->sin.sin_addr, sizeof(addr->sin.sin_addr));
			if (withport)
				hash = cw_hash_mem(hash, &addr->sin.sin_port, sizeof(addr->sin.sin_port));
			break;
		case AF_INET6:
			hash = cw_hash_mem(hash, &addr->sin6.sin6_addr, sizeof(addr->sin6.sin6_addr));
			if (withport)
				hash = cw_hash_mem(hash, &addr->sin6.sin6_port, sizeof(addr->sin6.sin6_port));
			break;
		case AF_LOCAL:
#if AF_LOCAL != AF_UNIX
		case AF_UNIX:
#endif
		case AF_PATHNAME:
		case AF_INTERNAL:
			hash = cw_hash_string(hash, addr->sun.sun_path);
			break;
	}

	return hash;
}


int cw_address_cmp(const cw_sockaddr_t *a, const cw_sockaddr_t *b, int withport)
{
	int ret;

	if (!(ret = a->sa.sa_family - b->sa.sa_family)) {
		switch (a->sa.sa_family) {
			case AF_INET:
				if (!(ret = memcmp(&a->sin.sin_addr, &b->sin.sin_addr, sizeof(a->sin.sin_addr))) && withport)
					ret = memcmp(&a->sin.sin_port, &b->sin.sin_port, sizeof(a->sin.sin_port));
				break;

			case AF_INET6:
				if (!(ret = memcmp(&a->sin6.sin6_addr, &b->sin6.sin6_addr, sizeof(a->sin6.sin6_addr))) && withport)
					ret = memcmp(&a->sin6.sin6_port, &b->sin6.sin6_port, sizeof(a->sin6.sin6_port));
				break;

			case AF_LOCAL:
#if AF_LOCAL != AF_UNIX
			case AF_UNIX:
#endif
			case AF_PATHNAME:
			case AF_INTERNAL:
				ret = strcmp(a->sun.sun_path, b->sun.sun_path);
				break;
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

		if ((conn->state == INIT || conn->state == LISTENING) && conn->addr.sa.sa_family == AF_LOCAL)
			unlink(conn->addr.sun.sun_path);
	}

	conn->state = CLOSED;

	cw_registry_del(&cw_connection_registry, conn->reg_entry);
}


int cw_connection_listen(int type, cw_sockaddr_t *addr, socklen_t addrlen, const struct cw_connection_tech *tech, struct cw_object *pvt_obj)
{
	struct cw_connection *conn = NULL;
	int sock;
	const int arg = 1;
	int res = 0;

	if ((sock = socket_cloexec(addr->sa.sa_family, type, 0)) < 0)
		goto out_err;

	if (bind(sock, &addr->sa, addrlen) || listen(sock, 1024))
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
