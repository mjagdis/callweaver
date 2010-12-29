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
#include "callweaver/dynstr.h"


#define CW_SOCKADDR_UN_SIZE(pathlen) ((size_t)(((struct sockaddr_un *)0)->sun_path + (pathlen)))

#ifndef SUN_LEN
#  include <string.h>
#  define SUN_LEN(ptr) ((size_t)(((struct sockaddr_un *) 0)->sun_path) + strlen ((ptr)->sun_path))
#endif


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
	socklen_t addrlen;
	/* This must be last */
	struct sockaddr addr;
};


extern CW_API_PUBLIC const char *cw_connection_state_name[];


/* As getaddrinfo(3) but takes a combined host & service rather than separate
 * strings with an optional masklen (as /n or /a.b.c.d) after the host part.
 */
extern CW_API_PUBLIC int cw_getaddrinfo(const char *spec, const struct addrinfo *hints, struct addrinfo **res, int *masklen);

extern CW_API_PUBLIC void cw_address_print(struct cw_dynstr *ds_p, const struct sockaddr *addr, int masklen, const char *portfmt);

extern CW_API_PUBLIC unsigned int cw_address_hash(const struct sockaddr *addr, int withport);
extern CW_API_PUBLIC int cw_address_cmp(const struct sockaddr *a, const struct sockaddr *b, int masklen, int withport);

extern CW_API_PUBLIC void cw_connection_close(struct cw_connection *conn);

extern CW_API_PUBLIC int cw_connection_listen(int type, struct sockaddr *addr, socklen_t addrlen, const struct cw_connection_tech *tech, struct cw_object *pvt_obj);

extern CW_API_PUBLIC struct cw_registry cw_connection_registry;

#endif /* _CALLWEAVER_CONNECTION_H */
