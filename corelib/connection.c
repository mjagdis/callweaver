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


int cw_address_parse(cw_address_t *addr, const char *spec)
{
	char *p;
	int portno;
	int ret = -1;

	/* DEPRECATED:
	 * Historically we supported an "ipv6:" prefix on IPv6 addresses. It isn't
	 * really necessary.
	 */
	if (!strncmp(spec, "ipv6:", sizeof("ipv6:") - 1))
		spec += sizeof("ipv6:") - 1;

	/* A file path is always absolute */
	if (spec[0] == '/') {
		int l = strlen(spec);
		if (l + 1 < sizeof(addr->sun.sun_path)) {
			addr->sun.sun_family = AF_LOCAL;
			memcpy(addr->sun.sun_path, spec, l + 1);
			unlink(spec);
			ret = 0;
		}

	/* An IPv6 address may be bare, in which case it contains at least two colons, or it
	 * may be enclosed in square brackets with an optional ":<portno>" suffix.
	 */
	} else if (spec[0] == '[' || ((p = strchr(spec, ':')) && strchr(p + 1, ':'))) {
		memset(addr, 0, sizeof(*addr));
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

err:
	return ret;
}


void cw_address_print(struct cw_dynstr *ds_p, const cw_address_t *addr)
{
	switch (addr->sa.sa_family) {
		case AF_INET: {
			cw_dynstr_need(ds_p, sizeof("ipv4:") + INET_ADDRSTRLEN);
			cw_dynstr_printf(ds_p, "ipv4:");
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
			cw_dynstr_need(ds_p, sizeof("ipv6:[") + INET6_ADDRSTRLEN);
			cw_dynstr_printf(ds_p, "ipv6:[");
			/* If the need failed above then we're already in error */
			if (!ds_p->error) {
				inet_ntop(addr->sa.sa_family, &addr->sin6.sin6_addr, &ds_p->data[ds_p->used], ds_p->size - ds_p->used);
				ds_p->used += strlen(&ds_p->data[ds_p->used]);
			}
			if (addr->sin6.sin6_port)
				cw_dynstr_printf(ds_p, "]:%hu", ntohs(addr->sin6.sin6_port));
			break;
		}

		case AF_LOCAL: {
			cw_dynstr_printf(ds_p, "local:%s", addr->sun.sun_path);
			break;
		}

		case AF_PATHNAME:
			cw_dynstr_printf(ds_p, "file:%s", addr->sun.sun_path);
			break;

		case AF_INTERNAL:
			cw_dynstr_printf(ds_p, "internal:%s", addr->sun.sun_path);
			break;
	}
}


static int addrcmp(const cw_address_t *a, const cw_address_t *b)
{
	int ret;

	if (!(ret = a->sa.sa_family - b->sa.sa_family)) {
		switch (a->sa.sa_family) {
			case AF_INET:
				if (!(ret = memcmp(&a->sin.sin_addr, &b->sin.sin_addr, sizeof(a->sin.sin_addr))))
					ret = memcmp(&a->sin.sin_port, &b->sin.sin_port, sizeof(a->sin.sin_port));
				break;

			case AF_INET6:
				if (!(ret = memcmp(&a->sin6.sin6_addr, &b->sin6.sin6_addr, sizeof(a->sin6.sin6_addr))))
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

	return addrcmp(&conn_a->addr, &conn_b->addr);
}

static int cw_connection_object_match(struct cw_object *obj, const void *pattern)
{
	struct cw_connection *conn = container_of(obj, struct cw_connection, obj);
	return addrcmp(&conn->addr, pattern);
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


struct cw_connection *cw_connection_new(const struct cw_connection_tech *tech, struct cw_object *pvt_obj, int domain)
{
	struct cw_connection *conn = NULL;
	int sock;
	const int arg = 1;

	if ((sock = socket(domain, SOCK_STREAM, 0)) >= 0) {
		if ((conn = malloc(sizeof(*conn)))) {
			cw_object_init(conn, NULL, 1);
			conn->obj.release = cw_connection_release;
			conn->reg_entry = NULL;
			conn->state = INIT;
			conn->salen = 0;
			conn->addr.sa.sa_family = domain;
			conn->tid = CW_PTHREADT_NULL;
			conn->sock = sock;
			conn->tech = tech;
			conn->pvt_obj = pvt_obj;

			fcntl(sock, F_SETFD, fcntl(sock, F_GETFD, 0) | FD_CLOEXEC);
			setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &arg, sizeof(arg));
			goto out;
		}

		close(sock);
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		goto out;
	}

	cw_log(CW_LOG_ERROR, "Unable to create socket: %s\n", strerror(errno));

out:
	return conn;
}


int cw_connection_bind(struct cw_connection *conn, const cw_address_t *addr)
{
	if (addr->sa.sa_family < arraysize(salen)) {
		if (!bind(conn->sock, &addr->sa, salen[addr->sa.sa_family])) {
			memcpy(&conn->addr.sa, &addr->sa, salen[addr->sa.sa_family]);
			return 0;
		}
	} else
		errno = EINVAL;

	return -1;
}


int cw_connection_listen(struct cw_connection *conn)
{
	struct cw_dynstr ds = CW_DYNSTR_INIT;
	int res = -1;

	cw_address_print(&ds, &conn->addr);

	if (!listen(conn->sock, 1024)) {
		conn->state = LISTENING;

		if ((conn->reg_entry = cw_registry_add(&cw_connection_registry, 0, &conn->obj))) {
			if (!cw_pthread_create(&conn->tid, &global_attr_default, service_thread, cw_object_dup(conn))) {
				if (option_verbose)
					cw_verbose("CallWeaver listening on '%s'\n", ds.data);
				res = 0;
				goto out;
			}

			cw_log(CW_LOG_ERROR, "Failed to start accept thread for %s: %s\n", ds.data, strerror(errno));
			cw_registry_del(&cw_connection_registry, conn->reg_entry);
			cw_object_put(conn);
		}
	} else
		cw_log(CW_LOG_ERROR, "Unable to listen on '%s': %s\n", ds.data, strerror(errno));

out:
	cw_dynstr_free(&ds);
	return res;
}
