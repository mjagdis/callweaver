/*
 * vim:ts=4:sw=4
 *
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
 *
 * \brief App to lookup the caller ID number, and see if it is blacklisted
 * 
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/image.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/callweaver_db.h"

static const char tdesc[] = "Look up Caller*ID name/number from blacklist database";

static void *lookupblacklist_app;
static const char lookupblacklist_name[] = "LookupBlacklist";
static const char lookupblacklist_synopsis[] = "Look up Caller*ID name/number from blacklist database";
static const char lookupblacklist_syntax[] = "LookupBlacklist()";
static const char lookupblacklist_descrip[] =
  "Looks up the Caller*ID number on the active channel in the CallWeaver database\n"
  "(family 'blacklist'). Sets the variable BLACKLISTED to either TRUE if the\n"
  "number was found, or FALSE otherwise.\n"
  "Example: database put blacklist <name/number> 1\n";

static int lookupblacklist_exec (struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	char blacklist[1];
	struct localuser *u;
	int bl = 0;
	char var[16] = "FALSE";

	LOCAL_USER_ADD (u);

	if (chan->cid.cid_num)
	{
		if (!cw_db_get ("blacklist", chan->cid.cid_num, blacklist, sizeof (blacklist)))
		{
			if (option_verbose > 2)
				cw_log(CW_LOG_NOTICE, "Blacklisted number %s found\n",chan->cid.cid_num);
			bl = 1;
		}
	}
	if (chan->cid.cid_name) {
		if (!cw_db_get ("blacklist", chan->cid.cid_name, blacklist, sizeof (blacklist))) 
		{
			if (option_verbose > 2)
				cw_log(CW_LOG_NOTICE,"Blacklisted name \"%s\" found\n",chan->cid.cid_name);
			bl = 1;
		}
	}
	
	if (bl) {
		strcpy(var, "TRUE");
	}

	pbx_builtin_setvar_helper(chan, "BLACKLISTED", var);

	LOCAL_USER_REMOVE (u);
	return 0;
}

static int unload_module (void)
{
	int res = 0;

	res |= cw_unregister_function(lookupblacklist_app);
	return res;
}

static int load_module (void)
{
	lookupblacklist_app = cw_register_function(lookupblacklist_name, lookupblacklist_exec, lookupblacklist_synopsis, lookupblacklist_syntax, lookupblacklist_descrip);
	return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
