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

/*! \file
 *
 * \brief App to lookup the caller ID number, and see if it is blacklisted
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

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
  "Looks up the Caller*ID number on the active\n"
  "channel in the CallWeaver database (family 'blacklist').  If the\n"
  "number is found, and if there exists a priority n + 101,\n"
  "where 'n' is the priority of the current instance, then  the\n"
  "channel  will  be  setup  to continue at that priority level.\n"
  "Otherwise, it returns 0.  Does nothing if no Caller*ID was received on the\n"
  "channel.\n"
  "Example: database put blacklist <name/number> 1\n";


static int lookupblacklist_exec (struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	char blacklist[1];
	struct localuser *u;
	int bl = 0;

	LOCAL_USER_ADD (u);

	if (chan->cid.cid_num)
	{
		if (!opbx_db_get ("blacklist", chan->cid.cid_num, blacklist, sizeof (blacklist)))
		{
			if (option_verbose > 2)
				opbx_log(OPBX_LOG_NOTICE, "Blacklisted number %s found\n",chan->cid.cid_num);
			bl = 1;
		}
	}
	if (chan->cid.cid_name) {
		if (!opbx_db_get ("blacklist", chan->cid.cid_name, blacklist, sizeof (blacklist))) 
		{
			if (option_verbose > 2)
				opbx_log(OPBX_LOG_NOTICE,"Blacklisted name \"%s\" found\n",chan->cid.cid_name);
			bl = 1;
		}
	}
	
	if (bl)
		opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);

	LOCAL_USER_REMOVE (u);
	return 0;
}

static int unload_module (void)
{
	int res = 0;

	res |= opbx_unregister_function(lookupblacklist_app);
	return res;
}

static int load_module (void)
{
	lookupblacklist_app = opbx_register_function(lookupblacklist_name, lookupblacklist_exec, lookupblacklist_synopsis, lookupblacklist_syntax, lookupblacklist_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
