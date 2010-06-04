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
 * \brief Get extension state int 
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

static const char tdesc[] = "Get state for given extension in a context (show hints)";

static void *g_app;
static char g_name[] = "GetExtState";
static char g_synopsis[] = "Get state for given extension in a context (show hints)";
static char g_syntax[] = "GetExtState(extensions1[&extension2], context)";
static char g_descrip[] =
	"Return the extension state for given extension in a contexte.\n"
	"Return values are:\n"
	"    0 = idle, 1 = inuse; 2 = busy, \n"
	"    4 = unavail, 8 = ringing; -1 unknown; \n"
	"Example: Set(EXTSTATE=${GetExtState(715&523, default)}\n";


static int get_extstate(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	static int deprecated_var = 0;
	struct cw_dynstr hint = CW_DYNSTR_INIT;
	struct localuser *u;
	const char *exten;
	char *state = NULL;
	int allunavailable = 1, allbusy = 1, allfree = 1;
	int busy = 0, inuse = 0, ring = 0;
			
	if (argc != 2 || !argv[0][0] || !argv[1][0])
		return cw_function_syntax(g_syntax);

	LOCAL_USER_ADD(u);

	exten = strtok_r(argv[0], "&", &state);
	while (exten) {
		if (cw_get_hint(&hint, NULL, NULL, argv[1], exten) && !hint.error) {
			switch (cw_device_state(hint.data)) {
				case CW_DEVICE_NOT_INUSE:
					allunavailable = 0;
					allbusy = 0;
					break;
				case CW_DEVICE_INUSE:
					inuse = 1;
					allunavailable = 0;
					allfree = 0;
					break;
				case CW_DEVICE_RINGING:
					ring = 1;
					allunavailable = 0;
					allfree = 0;
					break;
				case CW_DEVICE_BUSY:
					allunavailable = 0;
					allfree = 0;
					busy = 1;
					break;
				case CW_DEVICE_UNAVAILABLE:
				case CW_DEVICE_INVALID:
					allbusy = 0;
					allfree = 0;
					break;
				default:
					allunavailable = 0;
					allbusy = 0;
					allfree = 0;
					break;
			}
		}

		cw_dynstr_reset(&hint);

		exten = strtok_r(NULL, "&", &state);
	}

	cw_dynstr_free(&hint);

        // 0-idle; 1-inuse; 2-busy; 4-unavail 8-ringing
	if (!inuse && ring)
		exten = "8";
	else if (inuse && ring)
		exten = "1";
	else if (inuse)
		exten = "1";
	else if (allfree)
		exten = "0";
	else if (allbusy)		
		exten = "2";
	else if (allunavailable)
		exten = "4";
	else if (busy) 
		exten = "2";
	else 	exten = "-1";
	
	if (result) {
		cw_dynstr_printf(result, "%s", exten);
	} else {
		if (!deprecated_var) {
			cw_log(CW_LOG_WARNING, "Deprecated usage. Use Set(varname=${%s(args)}) instead.\n", g_name);
			deprecated_var = 1;
		}
		pbx_builtin_setvar_helper(chan, "EXTSTATE", exten);
	}

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
	g_app = cw_register_function(g_name, get_extstate, g_synopsis, g_syntax, g_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
