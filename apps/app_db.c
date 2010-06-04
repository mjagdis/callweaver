/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Copyright (C) 2003, Jefferson Noxon
 *
 * Mark Spencer <markster@digium.com>
 * Jefferson Noxon <jeff@debian.org>
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
 * \brief Database access functions
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/options.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/lock.h"

static const char tdesc[] = "Database access functions for CallWeaver extension logic";

static void *g_app;
static void *p_app;
static void *d_app;
static void *dt_app;

static const char g_name[] = "DBget";
static const char p_name[] = "DBput";
static const char d_name[] = "DBdel";
static const char dt_name[] = "DBdelTree";

static const char g_synopsis[] = "Retrieve a value from the database";
static const char p_synopsis[] = "Store a value in the database";
static const char d_synopsis[] = "Delete a key from the database";
static const char dt_synopsis[] = "Delete a family or keytree from the database";

static const char g_syntax[] = "DBget(varname=family/key)";
static const char p_syntax[] = "DBput(family/key=value)";
static const char d_syntax[] = "DBdel(family/key)";
static const char dt_syntax[] = "DBdelTree(family[/keytree])";

static const char g_descrip[] =
  	"Retrieves a value from the CallWeaver\n"
	"database and stores it in the given variable. Always returns 0.\n"
	"Sets DBSTATUS to SUCCESS if the key is found and FAIL on error.\n";

static const char p_descrip[] =
	"Stores the given value in the CallWeaver\n"
	"database.  Always returns 0.\n"
	"Sets DBSTATUS to SUCCESS if the key is found and FAIL on error.\n";

static const char d_descrip[] =
	"Deletes a key from the CallWeaver database.  Always\n"
	"returns 0.\n"
	"Sets DBSTATUS to SUCCESS if the key is found and FAIL on error.\n";

static const char dt_descrip[] =
	"Deletes a family or keytree from the CallWeaver\n"
	"database. Always returns 0.\n"
	"Sets DBSTATUS to SUCCESS if the key is found and FAIL on error.\n";


static int deltree_exec(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	char *family, *keytree;
	struct localuser *u;

	CW_UNUSED(result);

	if (argc != 1)
		return cw_function_syntax(dt_syntax);

	LOCAL_USER_ADD(u);

	if (strchr(argv[0], '/')) {
		family = strsep(&argv[0], "/");
		keytree = strsep(&argv[0], "\0");
			if (!family || !keytree) {
				cw_log(CW_LOG_DEBUG, "Ignoring; Syntax error in argument\n");
				LOCAL_USER_REMOVE(u);
				return 0;
			}
		if (cw_strlen_zero(keytree))
			keytree = 0;
	} else {
		family = argv[0];
		keytree = 0;
	}

	if (option_verbose > 2)	{
		if (keytree)
			cw_verbose(VERBOSE_PREFIX_3 "DBdeltree: family=%s, keytree=%s\n", family, keytree);
		else
			cw_verbose(VERBOSE_PREFIX_3 "DBdeltree: family=%s\n", family);
	}

	if (cw_db_deltree(family, keytree)) {
		if (option_verbose > 2)
			cw_verbose(VERBOSE_PREFIX_3 "DBdeltree: Error deleting key from database.\n");
			pbx_builtin_setvar_helper(chan, "DBSTATUS", "FAIL");
		} else {
			pbx_builtin_setvar_helper(chan, "DBSTATUS", "SUCCESS");
	}

	LOCAL_USER_REMOVE(u);

	return 0;
}

static int del_exec(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	char *family, *key;
	struct localuser *u;

	CW_UNUSED(result);

	if (argc != 1)
		return cw_function_syntax(d_syntax);

	LOCAL_USER_ADD(u);

	if (strchr(argv[0], '/')) {
		family = strsep(&argv[0], "/");
		key = strsep(&argv[0], "\0");
		if (!family || !key) {
			cw_log(CW_LOG_DEBUG, "Ignoring; Syntax error in argument\n");
			LOCAL_USER_REMOVE(u);
			return 0;
		}
		if (option_verbose > 2)
			cw_verbose(VERBOSE_PREFIX_3 "DBdel: family=%s, key=%s\n", family, key);
		if (cw_db_del(family, key)) {
			if (option_verbose > 2)
				cw_verbose(VERBOSE_PREFIX_3 "DBdel: Error deleting key from database.\n");
			pbx_builtin_setvar_helper(chan, "DBSTATUS", "FAIL");
		} else {
			pbx_builtin_setvar_helper(chan, "DBSTATUS", "SUCCESS");
		}
	} else {
		cw_log(CW_LOG_DEBUG, "Ignoring, no parameters\n");
	}

	LOCAL_USER_REMOVE(u);
	
	return 0;
}

static int put_exec(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	static int dep_warning = 0;
	char *val, *family, *key;
	struct localuser *u;

	CW_UNUSED(result);

	if (!dep_warning) {
		cw_log(CW_LOG_WARNING, "This application has been deprecated, please use the ${DB(family/key)} function instead.\n");
		dep_warning = 1;
	}
	
	if (argc != 1)
		return cw_function_syntax(p_syntax);

	LOCAL_USER_ADD(u);

	if (strchr(argv[0], '/') && strchr(argv[0], '=')) {
		family = strsep(&argv[0], "/");
		key = strsep(&argv[0], "=");
		val = strsep(&argv[0], "\0");
		if (!val || !family || !key) {
			cw_log(CW_LOG_DEBUG, "Ignoring; Syntax error in argument\n");
			LOCAL_USER_REMOVE(u);
			return 0;
		}
		if (option_verbose > 2)
			cw_verbose(VERBOSE_PREFIX_3 "DBput: family=%s, key=%s, value=%s\n", family, key, val);
		if (cw_db_put(family, key, val)) {
			if (option_verbose > 2)
				cw_verbose(VERBOSE_PREFIX_3 "DBput: Error writing value to database.\n");
			pbx_builtin_setvar_helper(chan, "DBSTATUS", "FAIL");
		} else {
			pbx_builtin_setvar_helper(chan, "DBSTATUS", "SUCCESS");
		}

	} else	{
		cw_log(CW_LOG_DEBUG, "Ignoring, no parameters\n");
	}

	LOCAL_USER_REMOVE(u);

	return 0;
}

static int get_exec(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	static int dep_warning = 0;
	cw_dynstr_t ds = CW_DYNSTR_INIT;
	char *varname, *family, *key;
	struct localuser *u;
	int ret = 0;

	CW_UNUSED(result);

	if (!dep_warning) {
		cw_log(CW_LOG_WARNING, "This application has been deprecated, please use the ${DB(family/key)} function instead.\n");
		dep_warning = 1;
	}

	if (argc != 1)
		return cw_function_syntax(g_syntax);

	LOCAL_USER_ADD(u);

	if (strchr(argv[0], '=') && strchr(argv[0], '/')) {
		varname = strsep(&argv[0], "=");
		family = strsep(&argv[0], "/");
		key = strsep(&argv[0], "\0");
		if (!varname || !family || !key) {
			cw_log(CW_LOG_DEBUG, "Ignoring; Syntax error in argument\n");
			LOCAL_USER_REMOVE(u);
			return 0;
		}

		if (option_verbose > 2)
			cw_verbose(VERBOSE_PREFIX_3 "DBget: varname=%s, family=%s, key=%s\n", varname, family, key);

		if (!cw_db_get(family, key, &ds)) {
			ret = -1;
			if (!ds.error) {
				pbx_builtin_setvar_helper(chan, varname, ds.data);
				if (option_verbose > 2)
					cw_verbose(VERBOSE_PREFIX_3 "DBget: set variable %s to %s\n", varname, ds.data);
				cw_dynstr_free(&ds);
				pbx_builtin_setvar_helper(chan, "DBSTATUS", "SUCCESS");
				ret = 0;
			}
		} else {
			if (option_verbose > 2)
				cw_verbose(VERBOSE_PREFIX_3 "DBget: Value not found in database.\n");
			pbx_builtin_setvar_helper(chan, "DBSTATUS", "FAIL");
		}
	} else {
		cw_log(CW_LOG_DEBUG, "Ignoring, no parameters\n");
	}

	LOCAL_USER_REMOVE(u);

	return ret;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(dt_app);
	res |= cw_unregister_function(d_app);
	res |= cw_unregister_function(p_app);
	res |= cw_unregister_function(g_app);
	return res;
}

static int load_module(void)
{
	g_app = cw_register_function(g_name, get_exec, g_synopsis, g_syntax, g_descrip);
	p_app = cw_register_function(p_name, put_exec, p_synopsis, p_syntax, p_descrip);
	d_app = cw_register_function(d_name, del_exec, d_synopsis, d_syntax, d_descrip);
	dt_app = cw_register_function(dt_name, deltree_exec, dt_synopsis, dt_syntax, dt_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
