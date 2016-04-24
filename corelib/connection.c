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

#include <errno.h>
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


static int cw_connection_qsort_by_addr(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct cw_connection *conn_a = container_of(*objp_a, struct cw_connection, obj);
	const struct cw_connection *conn_b = container_of(*objp_b, struct cw_connection, obj);

	return cw_sockaddr_cmp(&conn_a->addr, &conn_b->addr, -1, 1);
}

static int cw_connection_object_match(struct cw_object *obj, const void *pattern)
{
	struct cw_connection *conn = container_of(obj, struct cw_connection, obj);
	const struct cw_connection_match_args *args = pattern;

	return !cw_sockaddr_cmp(&conn->addr, args->sa, -1, args->withport) && conn->tech == args->tech;
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


struct cw_connection *cw_connection_listen(int type, struct sockaddr *addr, socklen_t addrlen, const struct cw_connection_tech *tech, struct cw_object *pvt_obj)
{
	struct cw_connection *conn = NULL;
	int sock;
	const int on = 1;

	if ((sock = socket_cloexec(addr->sa_family, type, 0)) < 0)
		goto out;

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

#if 0
#ifdef IPV6_V6ONLY
	if (addr->sa_family == AF_INET6)
		setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
#endif
#endif

	if (bind(sock, addr, addrlen) || (type == SOCK_STREAM && listen(sock, 1024)))
		goto out_close;

#ifdef __linux__
	/* FIXME: the default is supposed to already be IP_PMTUDISC_DONT for all but SOCK_STREAM
	 * sockets (which use the ip_no_pmtu_disc sysctl setting).
	 */
	if (type == SOCK_DGRAM) {
		const int val = IP_PMTUDISC_DONT;

		setsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val));
	}
#endif

	if (!(conn = malloc(sizeof(*conn) - sizeof (conn->addr) + addrlen)))
		goto out_close;

	cw_object_init(conn, NULL, 1);
	conn->obj.release = cw_connection_release;
	conn->reliable = (type == SOCK_STREAM || type == SOCK_SEQPACKET);
	conn->state = LISTENING;
	conn->sock = sock;
	conn->tech = tech;
	conn->pvt_obj = (pvt_obj ? cw_object_dup_obj(pvt_obj) : NULL);

	conn->addrlen = addrlen;
	memcpy(&conn->addr, addr, addrlen);

	conn->reg_entry = NULL;
	conn->tid = CW_PTHREADT_NULL;

	if (!(conn->reg_entry = cw_registry_add(&cw_connection_registry, cw_sockaddr_hash(addr, 0), &conn->obj)))
		goto out_release;

	if (!(errno = cw_pthread_create(&conn->tid, &global_attr_default, service_thread, cw_object_dup(conn))))
		goto out;

	cw_object_put(conn);
	cw_registry_del(&cw_connection_registry, conn->reg_entry);

out_release:
	cw_object_put(conn);
	conn = NULL;
out_close:
	close(sock);
out:
	return conn;
}


#define FORMAT_HEAD "%5.5s %5.5s %5.5s %-12.12s %s\n"
#define FORMAT_BODY "%5d %5d %5d %-12.12s %#l@\n"

static int show_connection_one(struct cw_object *obj, void *data)
{
	struct cw_connection *conn = container_of(obj, struct cw_connection, obj);
	struct cw_dynstr *ds_p = data;

	cw_dynstr_printf(ds_p, FORMAT_BODY,
			conn->sock,
			conn->state,
			cw_object_refs(conn),
			conn->tech->name,
			&conn->addr
	);

	return 0;
}


static int show_connections(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_dynstr_tprintf(ds_p, 2,
		cw_fmtval(FORMAT_HEAD, "File", "State", "Refs", "Tech", "Local addr"),
		cw_fmtval(FORMAT_HEAD, "----", "-----", "----", "----", "----------")
	);

	cw_registry_iterate(&cw_connection_registry, show_connection_one, ds_p);

	return RESULT_SUCCESS;
}

#undef FORMAT_HEAD
#undef FORMAT_BODY


static const char show_connections_usage[] =
"Usage: show connections\n"
"       Show current connections.\n";

static struct cw_clicmd cli_connections[] = {
	{
		.cmda = { "show", "connections", NULL },
		.handler = show_connections,
		.summary = "Show current connections",
		.usage = show_connections_usage,
	},
};


int cw_connection_init(void)
{
	cw_cli_register_multiple(cli_connections, arraysize(cli_connections));

	return 0;
}
