/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 * \brief Environment related dialplan functions
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"


static void *env_function;
static const char env_func_name[] = "ENV";
static const char env_func_synopsis[] = "Gets or sets the environment variable specified";
static const char env_func_syntax[] = "ENV(envname[, value])";
static const char env_func_desc[] = "";


static int builtin_function_env_rw(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	char *ret;

	CW_UNUSED(chan);

	if (argc < 1 || argc > 2 || !argv[0][0])
		return cw_function_syntax(env_func_syntax);

	/* FIXME: getenv/setenv are not reentrant. We should lock... */
	if (argc > 1) {
		if (argv[1][0]) {
			setenv(argv[0], argv[1], 1);
		} else {
			unsetenv(argv[0]);
		}
	}

	if (buf) {
		if ((ret = getenv(argv[0])))
			cw_copy_string(buf, ret, len);
	}

	return 0;
}


static const char tdesc[] = "Get or set environment variables.";

static int unload_module(void)
{
       return cw_unregister_function(env_function);
}

static int load_module(void)
{
       env_function = cw_register_function(env_func_name, builtin_function_env_rw, env_func_synopsis, env_func_syntax, env_func_desc);
       return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
