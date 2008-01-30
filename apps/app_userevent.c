/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 *
 * \brief UserEvent application -- send manager event
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/manager.h"

static const char tdesc[] = "Custom User Event Application";

static void *userevent_app;
static const char userevent_name[] = "UserEvent";
static const char userevent_synopsis[] = "Send an arbitrary event to the manager interface";
static const char userevent_syntax[] = "UserEvent(eventname[, body])";
static const char userevent_descrip[] = 
"Sends an arbitrary event to the\n"
"manager interface, with an optional body representing additional\n"
"arguments.  The format of the event will be:\n"
"    Event: UserEvent<specified event name>\n"
"    Channel: <channel name>\n"
"    Uniqueid: <call uniqueid>\n"
"    [body]\n"
"If the body is not specified, only Event, Channel, and Uniqueid fields\n"
"will be present.  Returns 0.";


static int userevent_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_len)
{
	char eventname[512];
	struct localuser *u;

	if (argc < 1 || argc > 2 || !argv[0][0])
		return cw_function_syntax(userevent_syntax);

	LOCAL_USER_ADD(u);

	snprintf(eventname, sizeof(eventname), "UserEvent%s", argv[0]);

	if (argc > 1 && argv[1][0]) {
            cw_log(CW_LOG_DEBUG, "Sending user event: %s, %s\n", eventname, argv[1]);
            manager_event(EVENT_FLAG_USER, eventname, 
			"Channel: %s\r\nUniqueid: %s\r\n%s\r\n",
			chan->name, chan->uniqueid, argv[1]);
	} else {
            cw_log(CW_LOG_DEBUG, "Sending user event: %s\n", eventname);
            manager_event(EVENT_FLAG_USER, eventname, 
			"Channel: %s\r\nUniqueid: %s\r\n", chan->name, chan->uniqueid);
	}

	LOCAL_USER_REMOVE(u);
	return 0;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(userevent_app);
	return res;
}

static int load_module(void)
{
	userevent_app = cw_register_function(userevent_name, userevent_exec, userevent_synopsis, userevent_syntax, userevent_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
