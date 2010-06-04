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
 * \brief App to flash a zap trunk
 * 
 */
#include <stdio.h> 
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/channel.h"
#include "callweaver/module.h"
#include "callweaver/function.h"


static const char tdesc[] = "Send hook flash";

static void *flash_app;
static char flash_name[] = "Flash";
static char flash_synopsis[] = "Send a hook flashes";
static char flash_syntax[] = "Flash()";
static char flash_descrip[] =
"Sends a hook flash as if the handset cradle was momentarily depressed\n"
"or the \"flash\" button on the phone was pressed.\n"
"Always returns 0\n";


static int flash_exec(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);
	CW_UNUSED(result);

	if (chan)
		cw_indicate(chan, CW_CONTROL_FLASH);
	return 0;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(flash_app);
	return res;
}

static int load_module(void)
{
	flash_app = cw_register_function(flash_name, flash_exec, flash_synopsis, flash_syntax, flash_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
