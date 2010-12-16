/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Eris Associates Limited, UK
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

/*! \file
 * \brief Blacklisting support
 *
 */

#ifndef _CALLWEAVER_BLACKLIST_H
#define _CALLWEAVER_BLACKLIST_H

#include <sys/types.h>
#include <sys/socket.h>


extern CW_API_PUBLIC int cw_blacklist_check(struct sockaddr_in *sa);
extern CW_API_PUBLIC void cw_blacklist_add(struct sockaddr_in *sa);

extern int cw_blacklist_init(void);

#endif /* _CALLWEAVER_BLACKLIST_H */
