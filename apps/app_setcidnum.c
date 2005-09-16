/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Oliver Daudey <traveler@xs4all.nl>
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
 * App to set callerid
 * 
 */
 
#include <string.h>
#include <stdlib.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision: 1.12 $")

#include "openpbx/lock.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/translate.h"
#include "openpbx/image.h"
#include "openpbx/callerid.h"
#include "openpbx/utils.h"

static char *tdesc = "Set CallerID Number";

static char *app = "SetCIDNum";

static char *synopsis = "Set CallerID Number";

static char *descrip = 
"  SetCIDNum(cnum[|a]): Set Caller*ID Number on a call to a new\n"
"value, while preserving the original Caller*ID name.  This is\n"
"useful for providing additional information to the called\n"
"party. Sets ANI as well if a flag is used.  Always returns 0\n"
"SetCIDNum has been deprecated in favor of the function\n"
"CALLERID(number)\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int setcallerid_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char *opt;
	int anitoo = 0;
	char tmp[256];
	static int deprecation_warning = 0;

	if (!deprecation_warning) {
		ast_log(LOG_WARNING, "SetCIDNum is deprecated, please use Set(CALLERID(number)=value) instead.\n");
		deprecation_warning = 1;
	}

	if (data)
		ast_copy_string(tmp, (char *)data, sizeof(tmp));
	opt = strchr(tmp, '|');
	if (opt) {
		*opt = '\0';
		opt++;
		if (*opt == 'a')
			anitoo = 1;
	}
	LOCAL_USER_ADD(u);
	ast_set_callerid(chan, tmp, NULL, anitoo ? tmp : NULL);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, setcallerid_exec, synopsis, descrip);
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

char *key()
{
	return OPENPBX_GPL_KEY;
}
