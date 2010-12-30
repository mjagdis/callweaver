/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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

/*! \file
 * \brief Access Control of various sorts
 */

#ifndef _CALLWEAVER_ACL_H
#define _CALLWEAVER_ACL_H


#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <netinet/in.h>
#include "callweaver/dynstr.h"
#include "callweaver/io.h"


/* Host based access control */

struct cw_acl;

extern CW_API_PUBLIC void cw_acl_free(struct cw_acl *acl);
extern CW_API_PUBLIC int cw_acl_add_addr(struct cw_acl **acl_p, const char *sense, const struct sockaddr *addr, socklen_t addrlen, int masklen);
extern CW_API_PUBLIC int cw_acl_add(struct cw_acl **acl_p, const char *sense, const char *spec);
extern CW_API_PUBLIC int cw_acl_check(struct cw_acl *acl, struct sockaddr *addr, int defsense);
extern CW_API_PUBLIC void cw_acl_print(struct cw_dynstr *ds_p, struct cw_acl *acl);

extern CW_API_PUBLIC int cw_get_ip_or_srv(struct sockaddr_in *sin, const char *value, const char *service);
#define cw_get_ip(sin, value) cw_get_ip_or_srv((sin), (value), NULL)
extern CW_API_PUBLIC int cw_ouraddrfor(struct in_addr *them, struct in_addr *us);
extern CW_API_PUBLIC int cw_lookup_iface(char *iface, struct in_addr *address);
extern CW_API_PUBLIC int cw_find_ourip(struct in_addr *ourip, struct sockaddr_in bindaddr);
extern CW_API_PUBLIC int cw_str2tos(const char *value, int *tos);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_ACL_H */
