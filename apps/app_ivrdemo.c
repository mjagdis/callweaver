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
 * IVR Demo application
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/lock.h"
#include "openpbx/app.h"

static char *tdesc = "IVR Demo Application";
static char *app = "IVRDemo";
static char *synopsis = 
"  This is a skeleton application that shows you the basic structure to create your\n"
"own openpbx applications and demonstrates the IVR demo.\n";

static int ivr_demo_func(struct opbx_channel *chan, void *data)
{
	opbx_verbose("IVR Demo, data is %s!\n", (char *)data);
	return 0;
}

OPBX_IVR_DECLARE_MENU(ivr_submenu, "IVR Demo Sub Menu", 0, 
{
	{ "s", OPBX_ACTION_BACKGROUND, "demo-abouttotry" },
	{ "s", OPBX_ACTION_WAITOPTION },
	{ "1", OPBX_ACTION_PLAYBACK, "digits/1" },
	{ "1", OPBX_ACTION_PLAYBACK, "digits/1" },
	{ "1", OPBX_ACTION_RESTART },
	{ "2", OPBX_ACTION_PLAYLIST, "digits/2;digits/3" },
	{ "3", OPBX_ACTION_CALLBACK, ivr_demo_func },
	{ "4", OPBX_ACTION_TRANSFER, "demo|s|1" },
	{ "*", OPBX_ACTION_REPEAT },
	{ "#", OPBX_ACTION_UPONE  },
	{ NULL }
});

OPBX_IVR_DECLARE_MENU(ivr_demo, "IVR Demo Main Menu", 0, 
{
	{ "s", OPBX_ACTION_BACKGROUND, "demo-congrats" },
	{ "g", OPBX_ACTION_BACKGROUND, "demo-instruct" },
	{ "g", OPBX_ACTION_WAITOPTION },
	{ "1", OPBX_ACTION_PLAYBACK, "digits/1" },
	{ "1", OPBX_ACTION_RESTART },
	{ "2", OPBX_ACTION_MENU, &ivr_submenu },
	{ "2", OPBX_ACTION_RESTART },
	{ "i", OPBX_ACTION_PLAYBACK, "invalid" },
	{ "i", OPBX_ACTION_REPEAT, (void *)(unsigned long)2 },
	{ "#", OPBX_ACTION_EXIT },
	{ NULL },
});

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int skel_exec(struct opbx_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	if (!data) {
		opbx_log(LOG_WARNING, "skel requires an argument (filename)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	/* Do our thing here */
	if (chan->_state != OPBX_STATE_UP)
		res = opbx_answer(chan);
	if (!res)
		res = opbx_ivr_menu_run(chan, &ivr_demo, data);
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
	return opbx_register_application(app, skel_exec, tdesc, synopsis);
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


