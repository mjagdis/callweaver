/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Portions Copyright (C) 2005, Anthony Minessale II
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
 * \brief  Call Detail Record related dialplan functions
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"

static void *cdr_function;
static const char cdr_func_name[] = "CDR";
static const char cdr_func_synopsis[] = "Gets or sets a CDR variable";
static const char cdr_func_syntax[] = "CDR(name[, options[, value]])";
static const char cdr_func_desc[] = "Option 'r' searches the entire stack of CDRs on the channel\n";


static int builtin_function_cdr_rw(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	int recursive = 0;

	if (argc < 1 || argc > 3 || !argv[0][0])
		return cw_function_syntax(cdr_func_syntax);

	/* check for a trailing flags argument */
	if (argc > 1) {
		if (strchr(argv[1], 'r'))
			recursive = 1;
	}

	if (argc > 2) {
		if (!strcasecmp(argv[0], "accountcode"))
			cw_cdr_setaccount(chan, argv[2]);
		else if (!strcasecmp(argv[0], "userfield"))
			cw_cdr_setuserfield(chan, argv[2]);
		else if (chan->cdr)
			cw_cdr_setvar(chan->cdr, argv[0], argv[2], recursive);
	}

	if (result && chan->cdr)
		cw_cdr_getvar(chan->cdr, argv[0], result, recursive);

	return 0;
}


static int unload_module(void)
{
        return cw_unregister_function(cdr_function);
}

static int load_module(void)
{
        cdr_function = cw_register_function(cdr_func_name, builtin_function_cdr_rw, cdr_func_synopsis, cdr_func_syntax, cdr_func_desc);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, "CDR related dialplan function")

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
