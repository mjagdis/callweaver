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
 * Time of day - Report the time of day
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/lock.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/say.h"

static char *tdesc = "Date and Time";

static char *app = "DateTime";

static char *synopsis = "Say the date and time";

static char *descrip = 
"  DateTime():  Says the current date and time.  Returns -1 on hangup or 0\n"
"otherwise.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int datetime_exec(struct opbx_channel *chan, void *data)
{
	int res=0;
	time_t t;
	struct localuser *u;
	LOCAL_USER_ADD(u);
	time(&t);
	if (chan->_state != OPBX_STATE_UP)
		res = opbx_answer(chan);
	if (!res)
		res = opbx_say_datetime(chan, t, "", chan->language);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return opbx_unregister_application(app);
}

int load_module(void)
{
	return opbx_register_application(app, datetime_exec, synopsis, descrip);
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


