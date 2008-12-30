/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Justin Huff <jjhuff@mspin.net>
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
 * \brief Applictions connected with CDR engine
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/config.h"
#include "callweaver/manager.h"
#include "callweaver/utils.h"


static const char tdesc[] = "CDR user field apps";

static void *setcdruserfield_app;
static const char setcdruserfield_name[] = "SetCDRUserField";
static const char setcdruserfield_synopsis[] = "Set the CDR user field";
static const char setcdruserfield_syntax[] = "SetCDRUserField(value)";
static const char setcdruserfield_descrip[] = 
               "Set the CDR 'user field' to value\n"
               "       The Call Data Record (CDR) user field is an extra field you\n"
               "       can use for data not stored anywhere else in the record.\n"
               "       CDR records can be used for billing or storing other arbitrary data\n"
               "       (I.E. telephone survey responses)\n"
               "       Also see AppendCDRUserField().\n"
               "       Always returns 0\n";

		

static void *appendcdruserfield_app;
static const char appendcdruserfield_name[] = "AppendCDRUserField";
static const char appendcdruserfield_synopsis[] = "Append to the CDR user field";
static const char appendcdruserfield_syntax[] = "AppendCDRUserField(value)";
static const char appendcdruserfield_descrip[] = 
               "Append value to the CDR user field\n"
               "       The Call Data Record (CDR) user field is an extra field you\n"
               "       can use for data not stored anywhere else in the record.\n"
               "       CDR records can be used for billing or storing other arbitrary data\n"
               "       (I.E. telephone survey responses)\n"
               "       Also see SetCDRUserField().\n"
               "       Always returns 0\n";
		

static int action_setcdruserfield(struct mansession *s, struct message *m)
{
	struct cw_channel *c = NULL;
	char *userfield = astman_get_header(m, "UserField");
	char *channel = astman_get_header(m, "Channel");
	char *append = astman_get_header(m, "Append");

	if (cw_strlen_zero(channel)) {
		astman_send_error(s, m, "No Channel specified");
		return 0;
	}

	if (cw_strlen_zero(userfield)) {
		astman_send_error(s, m, "No UserField specified");
		return 0;
	}

	if ((c = cw_get_channel_by_name_locked(channel))) {
		if (cw_true(append))
			cw_cdr_appenduserfield(c, userfield);
		else
			cw_cdr_setuserfield(c, userfield);
		cw_channel_unlock(c);
		astman_send_ack(s, m, "CDR Userfield Set");
		cw_object_put(c);
		return 0;
	}
	astman_send_error(s, m, "No such channel");
	return 0;
}


static int setcdruserfield_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct localuser *u;
	int res = 0;
	
	LOCAL_USER_ADD(u);

	if (chan->cdr && argc && argv[0][0]) {
		cw_cdr_setuserfield(chan, argv[0]);
	}

	LOCAL_USER_REMOVE(u);
	
	return res;
}

static int appendcdruserfield_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct localuser *u;
	int res = 0;
	
	LOCAL_USER_ADD(u);

	if (chan->cdr && argc && argv[0][0]) {
		cw_cdr_appenduserfield(chan, argv[0]);
	}

	LOCAL_USER_REMOVE(u);
	
	return res;
}


static struct manager_action manager_actions[] = {
	{
		.action = "SetCDRUserField",
		.authority = EVENT_FLAG_CALL,
		.func = action_setcdruserfield,
		.synopsis = "Set the CDR UserField",
	},
};


static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(setcdruserfield_app);
	res |= cw_unregister_function(appendcdruserfield_app);
	cw_manager_action_unregister_multiple(manager_actions, arraysize(manager_actions));
	return res;
}

static int load_module(void)
{
	setcdruserfield_app = cw_register_function(setcdruserfield_name, setcdruserfield_exec, setcdruserfield_synopsis, setcdruserfield_syntax, setcdruserfield_descrip);
	appendcdruserfield_app = cw_register_function(appendcdruserfield_name, appendcdruserfield_exec, appendcdruserfield_synopsis, appendcdruserfield_syntax, appendcdruserfield_descrip);
	cw_manager_action_register_multiple(manager_actions, arraysize(manager_actions));
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
