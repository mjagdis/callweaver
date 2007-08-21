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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

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


static int softhangup_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct localuser *u;
	struct opbx_channel *c=NULL;
	char *cut;
	char name[OPBX_CHANNEL_NAME] = "";
	int all = 0;
	
	if (argc == 0) {
		if (chan){
			opbx_log(LOG_WARNING, "Soft hanging %s up.\n",chan->name);
			opbx_softhangup(chan, OPBX_SOFTHANGUP_EXPLICIT);
			/* To allow other possible threads finish their work */
			/*usleep(50000);*/
		}
		return 0;
	}
	
	LOCAL_USER_ADD(u);

	all = (argc > 1 && strchr(argv[1], 'a'));

	c = opbx_channel_walk_locked(NULL);
	while (c) {
		strncpy(name, c->name, sizeof(name)-1);
		if (all) {
			/* CAPI is set up like CAPI[foo/bar]/clcnt */ 
			if (!strcmp(c->type,"CAPI")) 
				cut = strrchr(name,'/');
			/* Basically everything else is Foo/Bar-Z */
			else
				cut = strchr(name,'-');
			/* Get rid of what we've cut */
			if (cut)
				*cut = 0;
		}
		opbx_mutex_unlock(&c->lock);
		if (!strcasecmp(name, argv[0])) {
			opbx_log(LOG_WARNING, "Soft hanging %s up.\n",c->name);
			opbx_softhangup(c, OPBX_SOFTHANGUP_EXPLICIT);
			if(!all)
				break;
		}
		c = opbx_channel_walk_locked(c);
	}
	
	LOCAL_USER_REMOVE(u);

	return 0;
}

static int unload_module(void)
{
	int res = 0;

	res |= opbx_unregister_function(softhangup_app);
	return res;
}

static int load_module(void)
{
	softhangup_app = opbx_register_function(softhangup_name, softhangup_exec, softhangup_synopsis, softhangup_syntax, softhangup_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
