/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (c) 2004 - 2005 Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <app_verbose_v001@the-tilghman.com>
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
 * \brief Verbose logging application
 *
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/options.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"


static const char tdesc[] = "Send verbose output";

static void *verbose_app;
static const char verbose_name[] = "Verbose";
static const char verbose_synopsis[] = "Send arbitrary text to verbose output";
static const char verbose_syntax[] = "Verbose([level, ]message)";
static const char verbose_descrip[] =
"  level must be an integer value.  If not specified, defaults to 0."
"  Always returns 0.\n";


static int verbose_exec(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	static const char *prefix[] = {
		"",
		VERBOSE_PREFIX_1,
		VERBOSE_PREFIX_2,
		VERBOSE_PREFIX_3,
		VERBOSE_PREFIX_4,
	};
	int level;
	struct localuser *u;

	CW_UNUSED(result);

	level = 0;
	if (argc == 2 && isdigit(argv[0][0])) {
		level = atoi(argv[0]);
		argv++, argc--;
	}

	if (argc != 1 || level < 0 || level >= arraysize(prefix))
		return cw_function_syntax(verbose_syntax);

	LOCAL_USER_ADD(u);
	cw_verbose("%s%s\n", prefix[level], argv[0]);
	LOCAL_USER_REMOVE(u);

	return 0;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(verbose_app);
	return res;
}

static int load_module(void)
{
	verbose_app = cw_register_function(verbose_name, verbose_exec, verbose_synopsis, verbose_syntax, verbose_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
