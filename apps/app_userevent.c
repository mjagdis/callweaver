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


#define MAX_KEY_VAL_PAIRS	20
#define STR(x)			# x


static const char tdesc[] = "Custom User Event Application";

static void *userevent_app;
static const char userevent_name[] = "UserEvent";
static const char userevent_synopsis[] = "Send an arbitrary event to the manager interface";
static const char userevent_syntax[] = "UserEvent(eventname[, key1, value1[, key2, value2[, ..., key" STR(MAX_KEY_VAL_PAIRS) ", value" STR(MAX_KEY_VAL_PAIRS) "]]])";
static const char userevent_descrip[] =
"Sends an arbitrary event to the manager interface, with an optional body\n"
"representing additional arguments.\n"
"\n"
"The format of the event will be:\n"
"\n"
"    Event: UserEvent<specified event name>\n"
"    Channel: <channel name>\n"
"    Uniqueid: <call uniqueid>\n"
"    <key1>: <value1>\n"
"    <key2>: <value2>\n"
"    ...\n"
"    <key" STR(MAX_KEY_VAL_PAIRS) ">: <value" STR(MAX_KEY_VAL_PAIRS) ">\n"
"\n"
"A maximum of " STR(MAX_KEY_VAL_PAIRS) " key/value pairs may be specified. Any that are not specified\n"
"will be sent as:\n"
"\n"
"    nokey: \n"
"\n"
"Returns 0.";


static int userevent_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_len)
{
	char eventname[512];
	int i;

	if (argc < 1 || argc > MAX_KEY_VAL_PAIRS * 2 + 1 || !(argc & 1) || !argv[0][0])
		return cw_function_syntax(userevent_syntax);

	snprintf(eventname, sizeof(eventname), "UserEvent%s", argv[0]);

	for (i = (argc - 1) / 2; i < MAX_KEY_VAL_PAIRS; i++) {
		argv[1 + argc * 2 + 0] = "nokey";
		argv[1 + argc * 2 + 1] = "";
	}

	cw_manager_event(EVENT_FLAG_USER, eventname,
		22, /* MAX_KEY_VAL_PAIRS + 2 */
		cw_me_field("Channel",  "%s", chan->name),
		cw_me_field("Uniqueid", "%s", chan->uniqueid),
		cw_me_field(argv[ 1],   "%s", argv[ 2]),
		cw_me_field(argv[ 3],   "%s", argv[ 4]),
		cw_me_field(argv[ 5],   "%s", argv[ 6]),
		cw_me_field(argv[ 7],   "%s", argv[ 8]),
		cw_me_field(argv[ 9],   "%s", argv[10]),
		cw_me_field(argv[11],   "%s", argv[12]),
		cw_me_field(argv[13],   "%s", argv[14]),
		cw_me_field(argv[15],   "%s", argv[16]),
		cw_me_field(argv[17],   "%s", argv[18]),
		cw_me_field(argv[19],   "%s", argv[20]),
		cw_me_field(argv[21],   "%s", argv[22]),
		cw_me_field(argv[23],   "%s", argv[24]),
		cw_me_field(argv[25],   "%s", argv[26]),
		cw_me_field(argv[27],   "%s", argv[28]),
		cw_me_field(argv[29],   "%s", argv[30]),
		cw_me_field(argv[31],   "%s", argv[32]),
		cw_me_field(argv[33],   "%s", argv[34]),
		cw_me_field(argv[35],   "%s", argv[36]),
		cw_me_field(argv[37],   "%s", argv[38]),
		cw_me_field(argv[39],   "%s", argv[40])
	);

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
