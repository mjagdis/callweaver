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

#define CW_SENSE_DENY                  0
#define CW_SENSE_ALLOW                 1

/* Host based access control */

struct cw_ha;

extern CW_API_PUBLIC void cw_free_ha(struct cw_ha *ha);
extern CW_API_PUBLIC struct cw_ha *cw_append_ha(const char *sense, const char *spec, struct cw_ha *path);
extern CW_API_PUBLIC int cw_apply_ha(struct cw_ha *ha, struct sockaddr *addr);
extern CW_API_PUBLIC void cw_print_ha(struct cw_dynstr *ds_p, struct cw_ha *ha);
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
