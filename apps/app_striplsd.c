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
 * Skeleton application (?)
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/lock.h"

static char *tdesc = "Strip trailing digits";

static char *descrip =
"  StripLSD(count): Strips the trailing  'count'  digits  from  the  channel's\n"
"associated extension. For example, the  number  5551212 when stripped with a\n"
"count of 4 would be changed to 555.  This app always returns 0, and the PBX\n"
"will continue processing at the next priority for the *new* extension.\n"
"  So, for  example, if  priority 3 of 5551212  is  StripLSD 4, the next step\n"
"executed will be priority 4 of 555.  If you switch into an  extension which\n"
"has no first step, the PBX will treat it as though the user dialed an\n"
"invalid extension.\n";

static char *app = "StripLSD";

static char *synopsis = "Strip Least Significant Digits";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int striplsd_exec(struct opbx_channel *chan, void *data)
{
	char newexten[OPBX_MAX_EXTENSION] = "";
	int maxbytes = 0;
	int stripcount = 0;
	int extlen = strlen(chan->exten);

	maxbytes = sizeof(newexten) - 1;
	if (data) {
		stripcount = atoi(data);
	}
	if (!stripcount) {
		opbx_log(LOG_DEBUG, "Ignoring, since number of digits to strip is 0\n");
		return 0;
	}
	if (extlen > stripcount) {
		if (extlen - stripcount <= maxbytes) {
			maxbytes = extlen - stripcount;
		}
		strncpy(newexten, chan->exten, maxbytes);
	}
	strncpy(chan->exten, newexten, sizeof(chan->exten)-1);
	return 0;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return opbx_unregister_application(app);
}

int load_module(void)
{
	return opbx_register_application(app, striplsd_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}


