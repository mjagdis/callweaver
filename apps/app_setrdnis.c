/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Oliver Daudey <traveler@xs4all.nl>
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
 * \brief App to set rdnis
 *
 */
#include <stdio.h> 
#include <string.h>
#include <stdlib.h>

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
#include "callweaver/phone_no_utils.h"
#include "callweaver/utils.h"

static const char tdesc[] = "Set RDNIS Number";

static void *setrdnis_app;
static const char setrdnis_name[] = "SetRDNIS";
static const char setrdnis_synopsis[] = "Set RDNIS Number";
static const char setrdnis_syntax[] = "SetRDNIS(cnum)";
static const char setrdnis_descrip[] = 
"Set RDNIS Number on a call to a new\n"
"value.  Always returns 0\n"
"SetRDNIS has been deprecated in favor of the function\n"
"CALLERID(rdnis)\n";


static int setrdnis_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct localuser *u;
	char *n, *l;
	static int deprecation_warning = 0;

	CW_UNUSED(result);
	CW_UNUSED(result_max);

	if (!deprecation_warning) {
		cw_log(CW_LOG_WARNING, "SetRDNIS is deprecated, please use Set(CALLERID(rdnis)=value) instead.\n");
		deprecation_warning = 1;
	}

	if (argc != 1)
		return cw_function_syntax(setrdnis_syntax);

	LOCAL_USER_ADD(u);

	n = l = NULL;
	cw_callerid_parse(argv[0], &n, &l);
	if (l) {
		cw_shrink_phone_number(l);
		cw_channel_lock(chan);
		if (chan->cid.cid_rdnis)
			free(chan->cid.cid_rdnis);
		chan->cid.cid_rdnis = (l[0]) ? strdup(l) : NULL;
		cw_channel_unlock(chan);
	}

	LOCAL_USER_REMOVE(u);
	
	return 0;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(setrdnis_app);
	return res;
}

static int load_module(void)
{
	setrdnis_app = cw_register_function(setrdnis_name, setrdnis_exec, setrdnis_synopsis, setrdnis_syntax, setrdnis_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
