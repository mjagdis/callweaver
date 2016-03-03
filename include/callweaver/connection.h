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

#ifndef _CALLWEAVER_CONNECTION_H
#define _CALLWEAVER_CONNECTION_H

#include <pthread.h>

#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/dynstr.h"
#include "callweaver/sockaddr.h"
#include "callweaver/utils.h"


enum cw_connection_state {
	INIT = 0, LISTENING, CONNECTING, CONNECTED, SHUTDOWN, CLOSED
};


struct cw_connection;


struct cw_connection_tech {
	const char *name;
	int (*read)(struct cw_connection *conn);
};


struct cw_connection {
	struct cw_object obj;
	struct cw_registry_entry *reg_entry;
	enum cw_connection_state state;
	unsigned int reliable:1;
	int sock;
	pthread_t tid;
	const struct cw_connection_tech *tech;
	struct cw_object *pvt_obj;
	socklen_t addrlen;
	/* This must be last */
	struct sockaddr addr;
};


struct cw_connection_match_args {
	const struct cw_connection_tech *tech;
	int withport;
	const struct sockaddr *sa;
};


extern CW_API_PUBLIC const char *cw_connection_state_name[];

extern CW_API_PUBLIC struct cw_registry cw_connection_registry;


static inline struct cw_connection *cw_connection_find(const struct cw_connection_tech *tech, const struct sockaddr *sa, int withport)
{
	struct cw_connection_match_args args = {
		.tech = tech,
		.sa = sa,
		.withport = withport,
	};
	struct cw_connection *conn = NULL;
	struct cw_object *obj;

	if ((obj = cw_registry_find(&cw_connection_registry, 1, cw_sockaddr_hash(sa, 0), &args)))
		conn = container_of(obj, struct cw_connection, obj);

	return conn;
}


extern CW_API_PUBLIC void cw_connection_close(struct cw_connection *conn);

extern CW_API_PUBLIC struct cw_connection *cw_connection_listen(int type, struct sockaddr *addr, socklen_t addrlen, const struct cw_connection_tech *tech, struct cw_object *pvt_obj);

#endif /* _CALLWEAVER_CONNECTION_H */
