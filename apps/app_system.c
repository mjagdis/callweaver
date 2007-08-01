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
 * \brief Execute arbitrary system commands
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
#include <errno.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/app.h"
#include "callweaver/options.h"

static char *tdesc = "Generic System() application";

static void *app;

static const char *name = "System";

static const char *synopsis = "Execute a system command";

static const char *chanvar = "SYSTEMSTATUS";

static const char *syntax = "System(command)";

static const char *descrip =
"Executes a command  by  using  system(). Returns 0\n"
"on any situation.\n"
"Result of execution is returned in the SYSTEMSTATUS channel variable:\n"
"   FAILURE	Could not execute the specified command\n"
"   SUCCESS	Specified command successfully executed\n"
"   APPERROR	Specified command successfully executed, but returned error code\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int system_exec_helper(struct opbx_channel *chan, int argc, char **argv, int failmode)
{
	int res=0;
	struct localuser *u;
	
	if (argc != 1 || !argv[0][0]) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", syntax);
		return -1;
	}

	LOCAL_USER_ADD(u);

	/* Do our thing here */
	res = opbx_safe_system(argv[0]);
	if ((res < 0) && (errno != ECHILD)) {
		opbx_log(LOG_WARNING, "Unable to execute '%s'\n", argv[0]);
		pbx_builtin_setvar_helper(chan, chanvar, "FAILURE");
		res = failmode;
	} else if (res == 127) {
		opbx_log(LOG_WARNING, "Unable to execute '%s'\n", argv[0]);
		pbx_builtin_setvar_helper(chan, chanvar, "FAILURE");
		res = failmode;
	} else {
		if (res < 0) 
			res = 0;
		if (res != 0)
			pbx_builtin_setvar_helper(chan, chanvar, "APPERROR");
		else
			pbx_builtin_setvar_helper(chan, chanvar, "SUCCESS");
		res = 0;
	} 

	LOCAL_USER_REMOVE(u);

	return res;
}

static int system_exec(struct opbx_channel *chan, int argc, char **argv)
{
	return system_exec_helper(chan, argc, argv, -1);
}

static int trysystem_exec(struct opbx_channel *chan, int argc, char **argv)
{
	return system_exec_helper(chan, argc, argv, 0);
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= opbx_unregister_application(app);
	return res;
}

int load_module(void)
{
	app = opbx_register_application(name, system_exec, synopsis, syntax, descrip);
	return 0;
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


