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
 * \brief SoftHangup application
 * 
 */
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"

static const char tdesc[] = "Hangs up the requested channel";

static void *softhangup_app;
static const char softhangup_name[] = "SoftHangup";
static const char softhangup_synopsis[] = "Soft Hangup Application";
static const char softhangup_syntax[] = "SoftHangup([Technology/resource[, options]])";
static const char softhangup_descrip[] =
"Hangs up the requested channel.  Always returns 0\n"
"- 'options' may contain the following letter:\n"
"     'a' : hang up all channels on a specified device instead of a single resource\n";


struct softhangup_args {
	char *name;
	int all;
};

static int softhangup_one(struct cw_object *obj, void *data)
{
	char name[CW_CHANNEL_NAME];
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);
	struct softhangup_args *args = data;
	char *cut;

	cw_channel_lock(chan);

	strncpy(name, chan->name, sizeof(name)-1);
	if (args->all) {
		/* CAPI is set up like CAPI[foo/bar]/clcnt */
		if (!strcmp(chan->type, "CAPI"))
			cut = strrchr(name, '/');
		/* Basically everything else is Foo/Bar-Z */
		else
			cut = strchr(name, '-');
		/* Get rid of what we've cut */
		if (cut)
			*cut = 0;
	}

	cw_channel_unlock(chan);

	if (!strcasecmp(name, args->name)) {
		cw_log(CW_LOG_WARNING, "Soft hanging %s up.\n", chan->name);
		cw_softhangup(chan, CW_SOFTHANGUP_EXPLICIT);
	}

	return !args->all;
}

static int softhangup_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct softhangup_args args;
	struct localuser *u;

	CW_UNUSED(result);
	CW_UNUSED(result_max);

	if (argc == 0) {
		if (chan){
			cw_log(CW_LOG_WARNING, "Soft hanging %s up.\n",chan->name);
			cw_softhangup(chan, CW_SOFTHANGUP_EXPLICIT);
			/* To allow other possible threads finish their work */
			/*usleep(50000);*/
		}
		return 0;
	}
	
	LOCAL_USER_ADD(u);

	args.name = argv[0];
	args.all = (argc > 1 && strchr(argv[1], 'a'));

	cw_registry_iterate_ordered(&channel_registry, softhangup_one, &args);

	LOCAL_USER_REMOVE(u);

	return 0;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(softhangup_app);
	return res;
}

static int load_module(void)
{
	softhangup_app = cw_register_function(softhangup_name, softhangup_exec, softhangup_synopsis, softhangup_syntax, softhangup_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
