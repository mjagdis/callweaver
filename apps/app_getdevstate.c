/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Copyright (C) 2003, Jefferson Noxon
 *
 * Mark Spencer <markster@digium.com>
 * Jefferson Noxon <jeff@debian.org>
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
 *
 * This application written by Massimo Cetra <devel@navynet.it>
 */

/*! \file
 *
 * \brief Get device state int 
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/options.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/lock.h"
#include "callweaver/devicestate.h"
#include "callweaver/cli.h"	//Needed to have RESULT_SUCCESS and RESULT_FAILURE

static const char tdesc[] = "Gets device state (show hints)";

static void *g_app;
static const char g_name[] = "GetDevState";
static const char g_synopsis[] = "Gets the device state";
static const char g_syntax[] = "GetDevState(device)";
static const char g_descrip[] =
	"Get the device state and saves it in DEVSTATE variable. Valid values are:\n"
	"0 = unknown, 1 = not inuse, 2 = inuse, 3 = busy, 4 = invalid, 5 = unavailable, 6 = ringing"
	"Example: GetDevState(SIP/715)\n";


static int get_devstate(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	char resc[8]="-1";
	struct localuser *u;
	int res=-1;

	CW_UNUSED(result);

	LOCAL_USER_ADD(u);

	if (argc > 0 && argv[0][0])
		res = cw_device_state(argv[0]);	
	else
		cw_log(CW_LOG_DEBUG, "Ignoring, no parameters\n");

        cw_log(CW_LOG_DEBUG, "app_getdevstate setting DEVSTATE to %d for device %s \n",
               res, argv[0]);

	snprintf(resc,sizeof(resc),"%d",res);
	pbx_builtin_setvar_helper(chan, "DEVSTATE", resc );	

	LOCAL_USER_REMOVE(u);

	return RESULT_SUCCESS;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(g_app);
	return res;
}

static int load_module(void)
{
	g_app = cw_register_function(g_name, get_devstate, g_synopsis, g_syntax, g_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
