/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Russell Bryant <russelb@clemson.edu> 
 *
 * func_db.c adapted from the old app_db.c, copyright by the following people 
 * Copyright (C) 2005, Mark Spencer <markster@digium.com>
 * Copyright (C) 2003, Jefferson Noxon <jeff@debian.org>
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
 * \brief Functions for interaction with the CallWeaver database
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/module.h"


static void *db_exists_function;
static const char db_exists_func_name[] = "DB_EXISTS";
static const char db_exists_func_synopsis[] = "Check to see if a key exists in the CallWeaver database";
static const char db_exists_func_syntax[] = "DB_EXISTS(family/key)";
static const char db_exists_func_desc[] =
	"This function will check to see if a key exists in the CallWeaver\n"
	"database. If it exists, the function will return \"1\". If not,\n"
	"it will return \"0\".  Checking for existence of a database key will\n"
	"also set the variable DB_RESULT to the key's value if it exists.\n";


static void *db_function;
static const char db_func_name[] = "DB";
static const char db_func_synopsis[] = "Read or Write from/to the CallWeaver database";
static const char db_func_syntax[] = "DB(family/key[, value])";
static const char db_func_desc[] =
	"This function will read or write a value from/to the CallWeaver database.\n"
	"DB(family/key) will read a value from the database, while DB(family/key, value)\n"
	"will write a value to the database.  On a read, this function\n"
	"returns the value from the database, or NULL if it does not exist.\n"
	"On a write, this function will always return NULL.  Reading a database value\n"
	"will also set the variable DB_RESULT.\n";


static int function_db_rw(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	char *key;

	if (argc < 1 || argc > 2 || !argv[0][0] || !(key = strrchr(argv[0], '/')))
		return cw_function_syntax(db_func_syntax);

	*(key++) = '\0';

	if (argc > 1) {
		if (cw_db_put(argv[0], key, argv[1]))
			cw_log(CW_LOG_WARNING, "DB: Error setting %s/%s to %s\n", argv[0], key, argv[1]);
	}

	if (result) {
		size_t mark = result->used;

		if (cw_db_get(argv[0], key, result))
			cw_log(CW_LOG_DEBUG, "DB: %s/%s not found in database.\n", argv[0], key);
		else {
			/* FIXME: Why do we set a variable as well as fill the result buffer?
			 * Why do we leave the variable unchanged if the key does not exist?
			 */
			pbx_builtin_setvar_helper(chan, "DB_RESULT", &result->data[mark]);
		}
	}

	return 0;
}


static int function_db_exists(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	char *key;

	if (argc != 1 || !argv[0][0] || !(key = strrchr(argv[0], '/')))
		return cw_function_syntax(db_exists_func_syntax);

	*(key++) = '\0';

	if (result) {
		size_t mark = result->used;

		cw_dynstr_printf(result, "%s", (cw_db_get(argv[0], key, NULL) ? "0" : "1"));
		pbx_builtin_setvar_helper(chan, "DB_RESULT", &result->data[mark]);
	}

	return 0;
}


static int unload_module(void)
{
        int res = 0;

        res |= cw_unregister_function(db_exists_function);
        res |= cw_unregister_function(db_function);

        return res;
}

static int load_module(void)
{
        db_exists_function = cw_register_function(db_exists_func_name, function_db_exists, db_exists_func_synopsis, db_exists_func_syntax, db_exists_func_desc);
        db_function = cw_register_function(db_func_name, function_db_rw, db_func_synopsis, db_func_syntax, db_func_desc);

        return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, "database functions")

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
