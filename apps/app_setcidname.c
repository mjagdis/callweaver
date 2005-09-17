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
 * App to set callerid
 *
 */
 
#include <string.h>
#include <stdlib.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

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

static char *tdesc = "Set CallerID Name";

static char *app = "SetCIDName";

static char *synopsis = "Set CallerID Name";

static char *descrip = 
"  SetCIDName(cname[|a]): Set Caller*ID Name on a call to a new\n"
"value, while preserving the original Caller*ID number.  This is\n"
"useful for providing additional information to the called\n"
"party. Always returns 0\n"
"SetCIDName has been deprecated in favor of the function\n"
"CALLERID(name)\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int setcallerid_exec(struct opbx_channel *chan, void *data)
{
	int res = 0;
	char tmp[256] = "";
	struct localuser *u;
	char *opt;
	static int deprecation_warning = 0;

	if (!deprecation_warning) {
		opbx_log(LOG_WARNING, "SetCIDName is deprecated, please use Set(CALLERID(name)=value) instead.\n");
		deprecation_warning = 1;
	}

	if (data)
		opbx_copy_string(tmp, (char *)data, sizeof(tmp));
	opt = strchr(tmp, '|');
	if (opt) {
		*opt = '\0';
	}
	LOCAL_USER_ADD(u);
	opbx_set_callerid(chan, NULL, tmp, NULL);
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
	return opbx_register_application(app, setcallerid_exec, synopsis, descrip);
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
