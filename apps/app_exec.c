/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (c) 2004 - 2005, Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <app_exec__v001@the-tilghman.com>
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
 *
 * \brief Exec application
 *
 * \author Tilghman Lesher <app_exec__v001 at the-tilghman.com>
 */
#include <errno.h>
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

static const char tdesc[] = "Executes applications";

static void *exec_app;
static const char name_exec[] = "Exec";
static const char exec_synopsis[] = "Executes internal application";
static const char exec_syntax[] = "Exec(appname(arguments))";
static const char exec_descrip[] =
"Allows an arbitrary application to be invoked even when not\n"
"hardcoded into the dialplan. To invoke external applications\n"
"see the application System. Returns whatever value the\n"
"app returns or a non-zero value if the app cannot be found.\n";


static int exec_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct localuser *u;
	char *s, *appname, *endargs;
	int res = 0;

	CW_UNUSED(result);

	LOCAL_USER_ADD(u);

	/* Check and parse arguments */
	if (argc > 0) {
		struct cw_dynstr ds = CW_DYNSTR_INIT;

		s = cw_strdupa(argv[0]);
		appname = strsep(&s, "(");
		if (s) {
			endargs = strrchr(s, ')');
			if (endargs)
				*endargs = '\0';
			pbx_substitute_variables(chan, NULL, s, &ds);
		}
		if (appname) {
			res = cw_function_exec_str(chan, cw_hash_string(appname), appname, ds.data, NULL);
			if (res && errno == ENOENT)
				cw_log(CW_LOG_ERROR, "No such function \"%s\"\n", appname);
		}

		cw_dynstr_free(&ds);
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(exec_app);
	return res;
}

static int load_module(void)
{
	exec_app = cw_register_function(name_exec, exec_exec, exec_synopsis, exec_syntax, exec_descrip);
	return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
