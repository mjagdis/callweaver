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

static const char tdesc[] = "Generic System() application";

static void *app;
static void *app2;

static const char name[] = "System";
static const char name2[] = "TrySystem";

static const char synopsis[] = "Execute a system command";
static const char synopsis2[] = "Try executing a system command";

static const char chanvar[] = "SYSTEMSTATUS";

static const char syntax[] = "System(command)";
static const char syntax2[] = "TrySystem(command)";

static const char descrip[] =
"Executes a command  by  using  system(). Returns -1 on\n"
"failure to execute the specified command. \n"
"Result of execution is returned in the SYSTEMSTATUS channel variable:\n"
"   FAILURE	Could not execute the specified command\n"
"   SUCCESS	Specified command successfully executed\n";

static const char descrip2[] =
"Executes a command  by  using  system(). Returns 0\n"
"on any situation.\n"
"Result of execution is returned in the SYSTEMSTATUS channel variable:\n"
"   FAILURE	Could not execute the specified command\n"
"   SUCCESS	Specified command successfully executed\n"
"   APPERROR	Specified command successfully executed, but returned error code\n";


static int system_exec_helper(struct cw_channel *chan, int argc, char **argv)
{
	int res=0;
	struct localuser *u;
	
	if (argc != 1 || !argv[0][0])
		return cw_function_syntax(syntax);

	LOCAL_USER_ADD(u);

	/* Do our thing here */
	res = cw_safe_system(argv[0]);
	if ((res < 0) && (errno != ECHILD)) {
		cw_log(CW_LOG_WARNING, "Unable to execute '%s'\n", argv[0]);
		pbx_builtin_setvar_helper(chan, chanvar, "FAILURE");
	} else if (res == 127) {
		cw_log(CW_LOG_WARNING, "Unable to execute '%s'\n", argv[0]);
		pbx_builtin_setvar_helper(chan, chanvar, "FAILURE");
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

static int system_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(result);

	return system_exec_helper(chan, argc, argv);
}

static int trysystem_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(result);

	cw_log(CW_LOG_WARNING, "TrySystem is depricated. Please use System - it's the same thing!");
	return system_exec_helper(chan, argc, argv);
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(app2);
	res |= cw_unregister_function(app);
	return res;
}

static int load_module(void)
{
	app2 = cw_register_function(name2, trysystem_exec, synopsis2, syntax2, descrip2);
	app = cw_register_function(name, system_exec, synopsis, syntax, descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
