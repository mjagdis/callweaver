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
 * \brief Group Manipulation Applications
 *
 */
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/utils.h"
#include "callweaver/cli.h"
#include "callweaver/app.h"


static const char tdesc[] = "Group Management Routines";

static void *group_count_app;
static void *group_set_app;
static void *group_check_app;
static void *group_match_count_app;

static const char group_count_name[] = "GetGroupCount";
static const char group_set_name[] = "SetGroup";
static const char group_check_name[] = "CheckGroup";
static const char group_match_count_name[] = "GetGroupMatchCount";

static const char group_count_synopsis[] = "Get the channel count of a group";
static const char group_set_synopsis[] = "Set the channel's group";
static const char group_check_synopsis[] = "Check the channel count of a group against a limit";
static const char group_match_count_synopsis[] = "Get the channel count of all groups that match a pattern";

static const char group_count_syntax[] = "GetGroupCount([groupname][@category])";
static const char group_set_syntax[] = "SetGroup(groupname[@category])";
static const char group_check_syntax[] = "CheckGroup(max[@category])";
static const char group_match_count_syntax[] = "GetGroupMatchCount(groupmatch[@category])";

static const char group_count_descrip[] =
"Usage: GetGroupCount([groupname][@category])\n"
"  Calculates the group count for the specified group, or uses\n"
"the current channel's group if not specifed (and non-empty).\n"
"Stores result in GROUPCOUNT.  Always returns 0.\n"
"This application has been deprecated, please use the function\n"
"GroupCount.\n";

static const char group_set_descrip[] =
"Usage: SetGroup(groupname[@category])\n"
"  Sets the channel group to the specified value.  Equivalent to\n"
"Set(GROUP=group).  Always returns 0.\n";

static const char group_check_descrip[] =
"Usage: CheckGroup(max[@category])\n"
"  Checks that the current number of total channels in the\n"
"current channel's group does not exceed 'max'.  If the number\n"
"does not exceed 'max', set the GROUPSTATUS variable to OK.\n"
"Otherwise set GROUPSTATUS to MAX_EXCEEDED.\n"
"Always return 0\n";

static const char group_match_count_descrip[] =
"Usage: GetGroupMatchCount(groupmatch[@category])\n"
"  Calculates the group count for all groups that match the specified\n"
"pattern. Uses standard regular expression matching (see regex(7)).\n"
"Stores result in GROUPCOUNT.  Always returns 0.\n"
"This application has been deprecated, please use the function\n"
"GroupMatchCount.\n";


static int group_count_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	static int deprecation_warning = 0;
	char group[80] = "";
	char category[80] = "";
	char ret[80] = "";
	struct localuser *u;
	struct cw_var_t *var;
	int res = 0;
	int count;

	CW_UNUSED(result);

	if (!deprecation_warning) {
	        cw_log(CW_LOG_WARNING, "The GetGroupCount application has been deprecated, please use the GROUP_COUNT function.\n");
		deprecation_warning = 1;
	}

	if (argc != 1)
		return cw_function_syntax(group_count_syntax);

	LOCAL_USER_ADD(u);

	cw_app_group_split_group(argv[0], group, sizeof(group), category, sizeof(category));

	if (cw_strlen_zero(group)) {
		if ((var = pbx_builtin_getvar_helper(chan, cw_hash_var_name(category), category))) {
			strncpy(group, var->value, sizeof(group) - 1);
			cw_object_put(var);
		}
	}

	count = cw_app_group_get_count(group, category);
	snprintf(ret, sizeof(ret), "%d", count);
	pbx_builtin_setvar_helper(chan, "GROUPCOUNT", ret);

	LOCAL_USER_REMOVE(u);

	return res;
}

static int group_match_count_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	static int deprecation_warning = 0;
	char group[80] = "";
	char category[80] = "";
	char ret[80] = "";
	struct localuser *u;
	int res = 0;
	int count;

	CW_UNUSED(result);

	if (!deprecation_warning) {
	        cw_log(CW_LOG_WARNING, "The GetGroupMatchCount application has been deprecated, please use the GROUP_MATCH_COUNT function.\n");
		deprecation_warning = 1;
	}

	if (argc != 1)
		return cw_function_syntax(group_match_count_syntax);

	LOCAL_USER_ADD(u);

	cw_app_group_split_group(argv[0], group, sizeof(group), category, sizeof(category));

	if (!cw_strlen_zero(group)) {
		count = cw_app_group_match_get_count(group, category);
		snprintf(ret, sizeof(ret), "%d", count);
		pbx_builtin_setvar_helper(chan, "GROUPCOUNT", ret);
	}

	LOCAL_USER_REMOVE(u);

	return res;
}

static int group_set_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	static int deprecation_warning = 0;
	struct localuser *u;
	int res = 0;

	CW_UNUSED(result);

	if (!deprecation_warning) {
	        cw_log(CW_LOG_WARNING, "The SetGroup application has been deprecated, please use the GROUP() function.\n");
		deprecation_warning = 1;
	}

	if (argc != 1)
		return cw_function_syntax(group_set_syntax);

	LOCAL_USER_ADD(u);
	
	if (cw_app_group_set_channel(chan, argv[0]))
		cw_log(CW_LOG_WARNING, "SetGroup requires an argument (group name)\n");

	LOCAL_USER_REMOVE(u);
	return res;
}

static int group_check_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	static int deprecation_warning = 0;
	char limit[80]="";
	char category[80]="";
	struct localuser *u;
	struct cw_var_t *var;
	int res = 0;
	int max, count;

	CW_UNUSED(result);

	if (!deprecation_warning) {
	        cw_log(CW_LOG_WARNING, "The CheckGroup application has been deprecated, please use a combination of the GotoIf application and the GROUP_COUNT() function.\n");
		deprecation_warning = 1;
	}

	if (argc != 1)
		return cw_function_syntax(group_check_syntax);

	LOCAL_USER_ADD(u);

  	cw_app_group_split_group(argv[0], limit, sizeof(limit), category, sizeof(category));

 	if ((sscanf(limit, "%d", &max) == 1) && (max > -1)) {
		count = 0;
		if ((var = pbx_builtin_getvar_helper(chan, cw_hash_var_name(category), category))) {
			count = cw_app_group_get_count(var->value, category);
			cw_object_put(var);
		}
		if (count > max) {
			pbx_builtin_setvar_helper(chan, "GROUPSTATUS", "OK");
		} else {
			pbx_builtin_setvar_helper(chan, "GROUPSTATUS", "MAX_EXCEEDED");
		}
	} else
		cw_log(CW_LOG_WARNING, "CheckGroup requires a positive integer argument (max)\n");

	LOCAL_USER_REMOVE(u);
	return res;
}


#define FORMAT_STRING  "%-25s  %-20s  %-20s\n"

static int group_show_channels(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	regex_t regexbuf;
	struct cw_group_info *gi = NULL;
	int numchans = 0;
	int havepattern;

	if (argc < 3 || argc > 4)
		return RESULT_SHOWUSAGE;

	havepattern = 0;
	if (argc == 4) {
		if (regcomp(&regexbuf, argv[3], REG_EXTENDED | REG_NOSUB))
			return RESULT_SHOWUSAGE;
		havepattern = 1;
	}

	cw_dynstr_printf(ds_p, FORMAT_STRING, "Channel", "Group", "Category");

	cw_app_group_list_lock();

	for (gi = cw_app_group_list_head(); gi; gi = CW_LIST_NEXT(gi, list)) {
		if (!havepattern || !regexec(&regexbuf, gi->group, 0, NULL, 0)) {
			cw_dynstr_printf(ds_p, FORMAT_STRING, gi->chan->name, gi->group, (cw_strlen_zero(gi->category) ? "(default)" : gi->category));
			numchans++;
		}
	}

	cw_app_group_list_unlock();

	if (havepattern)
		regfree(&regexbuf);

	cw_dynstr_printf(ds_p, "%d active channel%s\n", numchans, (numchans != 1) ? "s" : "");
	return RESULT_SUCCESS;
}
#undef FORMAT_STRING


static const char show_channels_usage[] =
"Usage: group show channels [pattern]\n"
"       Lists all currently active channels with channel group(s) specified.\n"
"       Optional regular expression pattern is matched to group names for each channel.\n";

static struct cw_clicmd cli_show_channels = {
	.cmda = { "group", "show", "channels", NULL },
	.handler = group_show_channels,
	.summary = "Show active channels with group(s)",
	.usage = show_channels_usage,
};

static int unload_module(void)
{
	int res = 0;

	cw_cli_unregister(&cli_show_channels);
	res |= cw_unregister_function(group_count_app);
	res |= cw_unregister_function(group_set_app);
	res |= cw_unregister_function(group_check_app);
	res |= cw_unregister_function(group_match_count_app);
	return res;
}

static int load_module(void)
{
	group_count_app = cw_register_function(group_count_name, group_count_exec, group_count_synopsis, group_count_syntax, group_count_descrip);
	group_set_app = cw_register_function(group_set_name, group_set_exec, group_set_synopsis, group_set_syntax, group_set_descrip);
	group_check_app = cw_register_function(group_check_name, group_check_exec, group_check_synopsis, group_check_syntax, group_check_descrip);
	group_match_count_app = cw_register_function(group_match_count_name, group_match_count_exec, group_match_count_syntax, group_match_count_synopsis, group_match_count_descrip);
	cw_cli_register(&cli_show_channels);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
