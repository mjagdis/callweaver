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

#ifndef _CALLWEAVER_CONNECTION_H
#define _CALLWEAVER_CONNECTION_H

#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>

#include "callweaver/object.h"
#include "callweaver/registry.h"


typedef union {
	struct sockaddr sa;
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	struct sockaddr_un sun;
} cw_address_t;


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
	int sock;
	pthread_t tid;
	const struct cw_connection_tech *tech;
	struct cw_object *pvt_obj;
	socklen_t salen;
	cw_address_t addr;
};


extern CW_API_PUBLIC const char *cw_connection_state_name[];

extern CW_API_PUBLIC int cw_address_parse(cw_address_t *addr, char *spec);
extern CW_API_PUBLIC int cw_address_print(char *buf, ssize_t buflen, const cw_address_t *addr);

extern CW_API_PUBLIC struct cw_connection *cw_connection_new(const struct cw_connection_tech *tech, struct cw_object *pvt_obj, int domain);
extern CW_API_PUBLIC void cw_connection_close(struct cw_connection *conn);
extern CW_API_PUBLIC int cw_connection_bind(struct cw_connection *conn, const cw_address_t *addr);
extern CW_API_PUBLIC int cw_connection_listen(struct cw_connection *conn);

extern CW_API_PUBLIC struct cw_registry cw_connection_registry;

#endif /* _CALLWEAVER_CONNECTION_H */
