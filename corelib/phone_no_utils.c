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
 *
 * Phone number checking and manipulation support 
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>
#include <spandsp.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL: svn://svn.openpbx.org/openpbx/trunk/corelib/phone_no_utils.c $", "$Revision: 922 $")

#include "openpbx/ulaw.h"
#include "openpbx/alaw.h"
#include "openpbx/frame.h"
#include "openpbx/channel.h"
#include "openpbx/phone_no_utils.h"
#include "openpbx/logger.h"
#include "openpbx/fskmodem.h"
#include "openpbx/utils.h"

static struct {
	int val;
	char *name;
	char *description;
} pres_types[] = {
	{  OPBX_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED, "allowed_not_screened", "Presentation Allowed, Not Screened"},
	{  OPBX_PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN, "allowed_passed_screen", "Presentation Allowed, Passed Screen"},
	{  OPBX_PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN, "allowed_failed_screen", "Presentation Allowed, Failed Screen"},
	{  OPBX_PRES_ALLOWED_NETWORK_NUMBER, "allowed", "Presentation Allowed, Network Number"},
	{  OPBX_PRES_PROHIB_USER_NUMBER_NOT_SCREENED, "prohib_not_screened", "Presentation Prohibited, Not Screened"},
	{  OPBX_PRES_PROHIB_USER_NUMBER_PASSED_SCREEN, "prohib_passed_screen", "Presentation Prohibited, Passed Screen"},
	{  OPBX_PRES_PROHIB_USER_NUMBER_FAILED_SCREEN, "prohib_failed_screen", "Presentation Prohibited, Failed Screen"},
	{  OPBX_PRES_PROHIB_NETWORK_NUMBER, "prohib", "Presentation Prohibited, Network Number"},
	{  OPBX_PRES_NUMBER_NOT_AVAILABLE, "unavailable", "Number Unavailable"},
};

int opbx_parse_caller_presentation(const char *data)
{
	int i;

	for (i = 0; i < ((sizeof(pres_types) / sizeof(pres_types[0]))); i++) {
		if (!strcasecmp(pres_types[i].name, data))
			return pres_types[i].val;
	}

	return -1;
}

const char *opbx_describe_caller_presentation(int data)
{
	int i;

	for (i = 0; i < ((sizeof(pres_types) / sizeof(pres_types[0]))); i++) {
		if (pres_types[i].val == data)
			return pres_types[i].description;
	}

	return "unknown";
}

void opbx_shrink_phone_number(char *n)
{
	int x,y=0;
	int bracketed=0;
	for (x=0;n[x];x++) {
		switch(n[x]) {
		case '[':
			bracketed++;
			n[y++] = n[x];
			break;
		case ']':
			bracketed--;
			n[y++] = n[x];
			break;
		case '-':
			if (bracketed)
				n[y++] = n[x];
			break;
		case '.':
			if (!n[x+1])
				n[y++] = n[x];
			break;
		default:
			if (!strchr("( )", n[x]))
				n[y++] = n[x];
		}
	}
	n[y] = '\0';
}

int opbx_isphonenumber(const char *n)
{
	int x;
	if (!n || opbx_strlen_zero(n))
		return 0;
	for (x=0;n[x];x++)
		if (!strchr("0123456789*#+", n[x]))
			return 0;
	return 1;
}
