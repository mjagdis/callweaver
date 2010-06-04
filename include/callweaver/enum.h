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

/*!	\file enum.h
	\brief DNS and ENUM functions
*/

#ifndef _CALLWEAVER_ENUM_H
#define _CALLWEAVER_ENUM_H

#include "callweaver/channel.h"

/*! \brief Lookup entry in ENUM Returns 1 if found, 0 if not found, -1 on hangup
 *
 *  \param chan     Channel
 *  \param number   E164 number with or without the leading +
 *  \param result   Dynstr to write the number (or SIP uri) to
 *  \param tech     Technology (from url scheme in response)
 *                  You can set it to get particular answer RR, if there are many techs in DNS response, example: "sip"
 *                  If you need any record, then set it to empty string
 *  \param maxtech  Max length
 *  \param suffix   Zone suffix (if is NULL then use enum.conf 'search' variable)
 *  \param options  Options ('c' to count number of NAPTR RR, or number - the position of required RR in the answer list)
 */
extern CW_API_PUBLIC int cw_get_enum(struct cw_channel *chan, const char *number, struct cw_dynstr *result, char *technology, int maxtech, const char *suffix, const char *options);

/*! \brief Lookup DNS TXT record (used by app TXTCIDnum
 *
 *  \param chan      Channel
 *  \param number    E164 number with or without the leading +
 *  \param result    Dynstr to write the result text to
 *  \param tech      Technology (not used in TXT records)
 *  \param maxtech   Max length
 */
extern CW_API_PUBLIC int cw_get_txt(struct cw_channel *chan, const char *number, struct cw_dynstr *result, char *technology, int maxtech);

extern int cw_enum_init(void);
extern int cw_enum_reload(void);

#endif /* _CALLWEAVER_ENUM_H */
