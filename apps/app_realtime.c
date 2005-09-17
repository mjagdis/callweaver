/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Anthony Minessale <anthmct@yahoo.com>
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 *
 * RealTime App
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/options.h"
#include "openpbx/pbx.h"
#include "openpbx/config.h"
#include "openpbx/module.h"
#include "openpbx/lock.h"
#include "openpbx/cli.h"

#define next_one(var) var = var->next
#define crop_data(str) { *(str) = '\0' ; (str)++; }

static char *tdesc = "Realtime Data Lookup/Rewrite";
static char *app = "RealTime";
static char *uapp = "RealTimeUpdate";
static char *synopsis = "Realtime Data Lookup";
static char *usynopsis = "Realtime Data Rewrite";
static char *USAGE = "RealTime(<family>|<colmatch>|<value>[|<prefix>])";
static char *UUSAGE = "RealTimeUpdate(<family>|<colmatch>|<value>|<newcol>|<newval>)";
static char *desc = "Use the RealTime config handler system to read data into channel variables.\n"
"RealTime(<family>|<colmatch>|<value>[|<prefix>])\n\n"
"All unique column names will be set as channel variables with optional prefix to the name.\n"
"e.g. prefix of 'var_' would make the column 'name' become the variable ${var_name}\n\n";
static char *udesc = "Use the RealTime config handler system to update a value\n"
"RealTimeUpdate(<family>|<colmatch>|<value>|<newcol>|<newval>)\n\n"
"The column <newcol> in 'family' matching column <colmatch>=<value> will be updated to <newval>\n";

STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

static int cli_load_realtime(int fd, int argc, char **argv) 
{
	char *header_format = "%30s  %-30s\n";
	struct opbx_variable *var=NULL;

	if(argc<5) {
		opbx_cli(fd, "You must supply a family name, a column to match on, and a value to match to.\n");
		return RESULT_FAILURE;
	}

	var = opbx_load_realtime(argv[2], argv[3], argv[4], NULL);

	if(var) {
		opbx_cli(fd, header_format, "Column Name", "Column Value");
		opbx_cli(fd, header_format, "--------------------", "--------------------");
		while(var) {
			opbx_cli(fd, header_format, var->name, var->value);
			var = var->next;
		}
	} else {
		opbx_cli(fd, "No rows found matching search criteria.\n");
	}
	return RESULT_SUCCESS;
}

static int cli_update_realtime(int fd, int argc, char **argv) {
	int res = 0;

	if(argc<7) {
		opbx_cli(fd, "You must supply a family name, a column to update on, a new value, column to match, and value to to match.\n");
		opbx_cli(fd, "Ex: realtime update sipfriends name bobsphone port 4343\n will execute SQL as UPDATE sipfriends SET port = 4343 WHERE name = bobsphone\n");
		return RESULT_FAILURE;
	}

	res = opbx_update_realtime(argv[2], argv[3], argv[4], argv[5], argv[6], NULL);

	if(res < 0) {
		opbx_cli(fd, "Failed to update. Check the debug log for possible SQL related entries.\n");
		return RESULT_SUCCESS;
	}

       opbx_cli(fd, "Updated %d RealTime record%s.\n", res, (res != 1) ? "s" : "");

	return RESULT_SUCCESS;
}

static char cli_load_realtime_usage[] =
"Usage: realtime load <family> <colmatch> <value>\n"
"       Prints out a list of variables using the RealTime driver.\n";

static struct opbx_cli_entry cli_load_realtime_cmd = {
        { "realtime", "load", NULL, NULL }, cli_load_realtime,
        "Used to print out RealTime variables.", cli_load_realtime_usage, NULL };

static char cli_update_realtime_usage[] =
"Usage: realtime update <family> <colmatch> <value>\n"
"       Update a single variable using the RealTime driver.\n";

static struct opbx_cli_entry cli_update_realtime_cmd = {
        { "realtime", "update", NULL, NULL }, cli_update_realtime,
        "Used to update RealTime variables.", cli_update_realtime_usage, NULL };

static int realtime_update_exec(struct opbx_channel *chan, void *data) 
{
	char *family=NULL, *colmatch=NULL, *value=NULL, *newcol=NULL, *newval=NULL;
	struct localuser *u;
	int res = 0;
	if (!data) {
        opbx_log(LOG_ERROR,"Invalid input %s\n",UUSAGE);
        return -1;
    }
	LOCAL_USER_ADD(u);
	if ((family = opbx_strdupa(data))) {
		if ((colmatch = strchr(family,'|'))) {
			crop_data(colmatch);
			if ((value = strchr(colmatch,'|'))) {
				crop_data(value);
				if ((newcol = strchr(value,'|'))) {
					crop_data(newcol);
					if ((newval = strchr(newcol,'|'))) 
						crop_data(newval);
				}
			}
		}
	}
	if (! (family && value && colmatch && newcol && newval) ) {
		opbx_log(LOG_ERROR,"Invalid input: usage %s\n",UUSAGE);
		res = -1;
	} else {
		opbx_update_realtime(family,colmatch,value,newcol,newval,NULL);
	}

	LOCAL_USER_REMOVE(u);
	return res;

}


static int realtime_exec(struct opbx_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	struct opbx_variable *var, *itt;
	char *family=NULL, *colmatch=NULL, *value=NULL, *prefix=NULL, *vname=NULL;
	size_t len;

	if (!data) {
		opbx_log(LOG_ERROR,"Invalid input: usage %s\n",USAGE);
		return -1;
	}
	LOCAL_USER_ADD(u);
	if ((family = opbx_strdupa(data))) {
		if ((colmatch = strchr(family,'|'))) {
			crop_data(colmatch);
			if ((value = strchr(colmatch,'|'))) {
				crop_data(value);
				if ((prefix = strchr(value,'|')))
					crop_data(prefix);
			}
		}
	}
	if (! (family && value && colmatch) ) {
		opbx_log(LOG_ERROR,"Invalid input: usage %s\n",USAGE);
		res = -1;
	} else {
		if (option_verbose > 3)
			opbx_verbose(VERBOSE_PREFIX_4"Realtime Lookup: family:'%s' colmatch:'%s' value:'%s'\n",family,colmatch,value);
		if ((var = opbx_load_realtime(family, colmatch, value, NULL))) {
			for (itt = var; itt; itt = itt->next) {
				if(prefix) {
					len = strlen(prefix) + strlen(itt->name) + 2;
					vname = alloca(len);
					snprintf(vname,len,"%s%s",prefix,itt->name);
					
				} else 
					vname = itt->name;

				pbx_builtin_setvar_helper(chan, vname, itt->value);
			}
			opbx_variables_destroy(var);
		} else if (option_verbose > 3)
			opbx_verbose(VERBOSE_PREFIX_4"No Realtime Matches Found.\n");
	}
	
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	opbx_cli_unregister(&cli_load_realtime_cmd);
	opbx_cli_unregister(&cli_update_realtime_cmd);
	opbx_unregister_application(uapp);
	return opbx_unregister_application(app);
}

int load_module(void)
{
	opbx_cli_register(&cli_load_realtime_cmd);
	opbx_cli_register(&cli_update_realtime_cmd);
	opbx_register_application(uapp, realtime_update_exec, usynopsis, udesc);
	return opbx_register_application(app, realtime_exec, synopsis, desc);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return OPENPBX_GPL_KEY;
}

