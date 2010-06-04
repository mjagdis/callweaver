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

/*! \royk
 *
 * \brief App to set caller presentation
 * 
 * \ingroup applications
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/image.h"
#include "callweaver/callerid.h"
#include "callweaver/phone_no_utils.h"

static const char tdesc[] = "SetCallerPres Application";

static void *setcallerid_pres_app;
static const char setcallerid_pres_name[] = "SetCallerPres";
static const char setcallerid_pres_synopsis[] = "Set CallerID Presentation";
static const char setcallerid_pres_syntax[] = "SetCallerPres(presentation)";
static const char setcallerid_pres_descrip[] = 
"Set Caller*ID presentation on a call.\n"
"  Valid presentations are:\n"
"\n"
"      allowed_not_screened    : Presentation Allowed, Not Screened\n"
"      allowed_passed_screen   : Presentation Allowed, Passed Screen\n" 
"      allowed_failed_screen   : Presentation Allowed, Failed Screen\n" 
"      allowed                 : Presentation Allowed, Network Number\n"
"      prohib_not_screened     : Presentation Prohibited, Not Screened\n" 
"      prohib_passed_screen    : Presentation Prohibited, Passed Screen\n"
"      prohib_failed_screen    : Presentation Prohibited, Failed Screen\n"
"      prohib                  : Presentation Prohibited, Network Number\n"
"      unavailable             : Number Unavailable\n"
"\n"
;


static int setcallerid_pres_exec(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	struct localuser *u;
	int pres = -1;

	CW_UNUSED(result);

	if (argc != 1)
		return cw_function_syntax(setcallerid_pres_syntax);

	LOCAL_USER_ADD(u);

	pres = cw_parse_caller_presentation(argv[0]);

	if (pres < 0) {
		cw_log(CW_LOG_WARNING, "'%s' is not a valid presentation (see 'show application SetCallerPres')\n", argv[0]);
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	
	chan->cid.cid_pres = pres;
	LOCAL_USER_REMOVE(u);
	return 0;
}


static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(setcallerid_pres_app);
	return res;
}

static int load_module(void)
{
	setcallerid_pres_app = cw_register_function(setcallerid_pres_name, setcallerid_pres_exec, setcallerid_pres_synopsis, setcallerid_pres_syntax, setcallerid_pres_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
