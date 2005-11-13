/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 * CallerID (and other GR30) Generation support 
 * Includes code and algorithms from the Zapata library.
 */

#ifndef _OPENPBX_PHONE_NO_UTILS_H
#define _OPENPBX_PHONE_NO_UTILS_H

/*! Shrink a phone number in place to just digits (more accurately it just removes ()'s, .'s, and -'s... */
/*!
 * \param n The number to be stripped/shrunk
 */
extern void opbx_shrink_phone_number(char *n);

/*! Check if a string consists only of digits.  Returns non-zero if so */
/*!
 * \param n number to be checked.
 * \return 0 if n is a number, 1 if it's not.
 */
extern int opbx_isphonenumber(const char *n);

/* Various defines and bits for handling PRI- and SS7-type restriction */

#define OPBX_PRES_NUMBER_TYPE				0x03
#define OPBX_PRES_USER_NUMBER_UNSCREENED			0x00
#define OPBX_PRES_USER_NUMBER_PASSED_SCREEN		0x01
#define OPBX_PRES_USER_NUMBER_FAILED_SCREEN		0x02
#define OPBX_PRES_NETWORK_NUMBER				0x03

#define OPBX_PRES_RESTRICTION				0x60
#define OPBX_PRES_ALLOWED				0x00
#define OPBX_PRES_RESTRICTED				0x20
#define OPBX_PRES_UNAVAILABLE				0x40
#define OPBX_PRES_RESERVED				0x60

#define OPBX_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED \
	OPBX_PRES_USER_NUMBER_UNSCREENED + OPBX_PRES_ALLOWED

#define OPBX_PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN \
	OPBX_PRES_USER_NUMBER_PASSED_SCREEN + OPBX_PRES_ALLOWED

#define OPBX_PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN \
	OPBX_PRES_USER_NUMBER_FAILED_SCREEN + OPBX_PRES_ALLOWED

#define OPBX_PRES_ALLOWED_NETWORK_NUMBER	\
	OPBX_PRES_NETWORK_NUMBER + OPBX_PRES_ALLOWED

#define OPBX_PRES_PROHIB_USER_NUMBER_NOT_SCREENED \
	OPBX_PRES_USER_NUMBER_UNSCREENED + OPBX_PRES_RESTRICTED

#define OPBX_PRES_PROHIB_USER_NUMBER_PASSED_SCREEN \
	OPBX_PRES_USER_NUMBER_PASSED_SCREEN + OPBX_PRES_RESTRICTED

#define OPBX_PRES_PROHIB_USER_NUMBER_FAILED_SCREEN \
	OPBX_PRES_USER_NUMBER_FAILED_SCREEN + OPBX_PRES_RESTRICTED

#define OPBX_PRES_PROHIB_NETWORK_NUMBER \
	OPBX_PRES_NETWORK_NUMBER + OPBX_PRES_RESTRICTED

#define OPBX_PRES_NUMBER_NOT_AVAILABLE \
	OPBX_PRES_NETWORK_NUMBER + OPBX_PRES_UNAVAILABLE

int opbx_parse_caller_presentation(const char *data);
const char *opbx_describe_caller_presentation(int data);

#endif /* _OPENPBX_PHONE_NO_UTILS_H */
