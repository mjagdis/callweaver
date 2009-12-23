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


int cw_address_parse(cw_address_t *addr, char *spec)
{
	char *port;
	int portno;
	int ret = -1;

	if (spec[0] == '/') {
		addr->sun.sun_family = AF_LOCAL;
		strncpy(addr->sun.sun_path, spec, sizeof(addr->sun.sun_path));
		unlink(spec);
		ret = 0;
	} else if (!strncmp(spec, "ipv6:[", sizeof("ipv6:[") - 1)) {
		addr->sin6.sin6_family = AF_INET6;
		memset(&addr->sin6.sin6_addr, 0, sizeof(addr->sin6.sin6_addr));
		addr->sin6.sin6_port = 0;

		if ((port = strrchr(spec, ']')) && port[1] == ':') {
			if (sscanf(port + 2, "%d", &portno) != 1) {
				cw_log(CW_LOG_ERROR, "Invalid port number '%s' in '%s'\n", port + 2, spec);
				goto err;
			}
			addr->sin6.sin6_port = htons(portno);
			*port = '\0';
		}

		if (inet_pton(AF_INET6, spec + sizeof("ipv6:[") - 1, &addr->sin6.sin6_addr) <= 0) {
			cw_log(CW_LOG_ERROR, "Invalid IPv6 address '%s' specified\n", spec + sizeof("ipv6:[") - 1);
			goto err;
		}
		ret = 0;
	} else {
		addr->sin.sin_family = AF_INET;
		memset(&addr->sin.sin_addr, 0, sizeof(addr->sin.sin_addr));
		addr->sin.sin_port = 0;

		if ((port = strrchr(spec, ':'))) {
			if (sscanf(port + 1, "%d", &portno) != 1) {
				cw_log(CW_LOG_ERROR, "Invalid port number '%s' in '%s'\n", port + 1, spec);
				goto err;
			}
			addr->sin.sin_port = htons(portno);
			*port = '\0';
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


int cw_address_print(char *buf, ssize_t buflen, const cw_address_t *addr)
{
	char *p = buf;
	int n;

	switch (addr->sa.sa_family) {
		case AF_INET: {
			if (buflen)
				memcpy(p, "ipv4:", (buflen > sizeof("ipv4:") - 1 ? sizeof("ipv4:") - 1 : buflen));
			p += sizeof("ipv4:") - 1;
			buflen -= sizeof("ipv4:") - 1;
			if (buflen >= INET_ADDRSTRLEN && inet_ntop(addr->sa.sa_family, &addr->sin.sin_addr, p, buflen)) {
				n = strlen(p);
				p += n;
				buflen -= n;
			} else {
				p += INET_ADDRSTRLEN - 1;
				buflen -= INET_ADDRSTRLEN - 1;
			}
			p += snprintf(p, (buflen > 0 ? buflen : 0), ":%u", ntohs(addr->sin.sin_port)) + 1;
			break;
		}

		case AF_INET6: {
			if (buflen)
				memcpy(p, "ipv6:[", (buflen > sizeof("ipv6:[") - 1 ? sizeof("ipv6:[") - 1 : buflen));
			p += sizeof("ipv6:[") - 1;
			buflen -= sizeof("ipv6:[") - 1;
			if (buflen >= INET6_ADDRSTRLEN && inet_ntop(addr->sa.sa_family, &addr->sin6.sin6_addr, p, buflen)) {
				n = strlen(p);
				p += n;
				buflen -= n;
			} else {
				p += INET6_ADDRSTRLEN - 1;
				buflen -= INET6_ADDRSTRLEN - 1;
			}
			p += snprintf(p, (buflen > 0 ? buflen : 0), "]:%u", ntohs(addr->sin6.sin6_port)) + 1;
			break;
		}

		case AF_LOCAL: {
			p += snprintf(p, buflen, "local:%s", addr->sun.sun_path) + 1;
			break;
		}

		case AF_PATHNAME:
			p += snprintf(p, buflen, "file:%s", addr->sun.sun_path) + 1;
			break;

		case AF_INTERNAL:
			p += snprintf(p, buflen, "internal:%s", addr->sun.sun_path) + 1;
			break;
	}

	return p - buf;
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
	char buf[1024];

	cw_address_print(buf, sizeof(buf), &conn->addr);
	buf[sizeof(buf) - 1] = '\0';

	if (!listen(conn->sock, 1024)) {
		conn->state = LISTENING;

		if ((conn->reg_entry = cw_registry_add(&cw_connection_registry, 0, &conn->obj))) {
			if (!cw_pthread_create(&conn->tid, &global_attr_default, service_thread, cw_object_dup(conn))) {
				if (option_verbose)
					cw_verbose("CallWeaver listening on '%s'\n", buf);
				return 0;
			}

			cw_log(CW_LOG_ERROR, "Failed to start accept thread for %s: %s\n", buf, strerror(errno));
			cw_registry_del(&cw_connection_registry, conn->reg_entry);
			cw_object_put(conn);
		}
	} else
		cw_log(CW_LOG_ERROR, "Unable to listen on '%s': %s\n", buf, strerror(errno));

	return -1;
}
