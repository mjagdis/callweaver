/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Martin Pycko <martinp@digium.com>
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
 * \brief Applications connected with CDR engine
 *
 */
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/module.h"
#include "callweaver/pbx.h"


static const char tdesc[] = "Make sure callweaver doesn't save CDR for a certain call";

static void *nocdr_app;
static const char nocdr_name[] = "NoCDR";
static const char nocdr_synopsis[] = "Make sure callweaver doesn't save CDR for a certain call";
static const char nocdr_syntax[] = "NoCDR()";
static const char nocdr_descrip[] = "Makes sure there won't be any CDR written for a certain call";


static int nocdr_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct localuser *u;
	
	LOCAL_USER_ADD(u);

	if (chan->cdr) {
		cw_cdr_free(chan->cdr);
		chan->cdr = NULL;
	}

	LOCAL_USER_REMOVE(u);

	return 0;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(nocdr_app);
	return res;
}

static int load_module(void)
{
	nocdr_app = cw_register_function(nocdr_name, nocdr_exec, nocdr_synopsis, nocdr_syntax, nocdr_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
