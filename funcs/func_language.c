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
 * \brief Language related dialplan functions
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


static void *language_function;
static const char language_func_name[] = "LANGUAGE";
static const char language_func_synopsis[] = "Gets or sets the channel's language.";
static const char language_func_syntax[] = "LANGUAGE([value])";
static const char language_func_desc[] =
	"Gets or sets the channel language.  This information is used for the\n"
	"syntax in generation of numbers, and to choose a natural language file\n"
	"when available.  For example, if language is set to 'fr' and the file\n"
	"'demo-congrats' is requested to be played, if the file\n"
	"'fr/demo-congrats' exists, then it will play that file, and if not\n"
	"will play the normal 'demo-congrats'.  For some language codes,\n"
	"changing the language also changes the syntax of some CallWeaver\n"
	"functions, like SayNumber.\n";


static int builtin_function_language_rw(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	if (argc > 1)
		return cw_function_syntax(language_func_syntax);

	if (chan) {
		if (argc > 0)
			cw_copy_string(chan->language, argv[0], sizeof(chan->language));

		if (result)
			cw_dynstr_printf(result, "%s", chan->language);
	}

	return 0;
}

static const char tdesc[] = "language functions";

static int unload_module(void)
{
        return cw_unregister_function(language_function);
}

static int load_module(void)
{
        language_function =  cw_register_function(language_func_name, builtin_function_language_rw, language_func_synopsis, language_func_syntax, language_func_desc);
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
