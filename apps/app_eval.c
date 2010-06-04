/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (c) 2004 - 2005, Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <app_eval__v001@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage.
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 * \brief Eval application
 *
 * \author Tilghman Lesher <app_eval__v001 at the-tilghman.com>
 */
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"

/* Maximum length of any variable */
#define MAXRESULT	1024

static const char tdesc[] = "Reevaluates strings";

static void *eval_app;
static const char eval_name[] = "Eval";
static const char eval_synopsis[] = "Evaluates a string";
static const char eval_syntax[] = "Eval(newvar=somestring)";
static const char eval_descrip[] =
"Normally CallWeaver evaluates variables inline.  But what if you want to\n"
"store variable offsets in a database, to be evaluated later?  Eval is\n"
"the answer, by allowing a string to be evaluated twice in the dialplan,\n"
"the first time as part of the normal dialplan, and the second using Eval.\n";


static int eval_exec(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	static int dep_warning = 0;
	struct localuser *u;
	char *newvar = NULL;
	int res = -1;

	CW_UNUSED(argv);
	CW_UNUSED(result);

	if (!dep_warning) {
		cw_log(CW_LOG_WARNING, "This application has been deprecated in favor of the dialplan function, EVAL\n");
		dep_warning = 1;
	}

	LOCAL_USER_ADD(u);
	
	/* Check and parse arguments */
	if (argc == 1 && argv[0]) {
		newvar = strsep(&argv[0], "=");
		if (newvar && (newvar[0] != '\0')) {
			cw_dynstr_t ds = CW_DYNSTR_INIT;

			pbx_substitute_variables(chan, NULL, argv[0], &ds);
			if (!ds.error) {
				pbx_builtin_setvar_helper(chan, newvar, ds.data);
				res = 0;
			}

			cw_dynstr_free(&ds);
		}
	}

	LOCAL_USER_REMOVE(u);

	return (res ? cw_function_syntax(eval_syntax) : 0);
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(eval_app);
	return res;
}

static int load_module(void)
{
	eval_app = cw_register_function(eval_name, eval_exec, eval_synopsis, eval_syntax, eval_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
