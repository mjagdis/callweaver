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
 * \brief Channel group related dialplan functions
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


static void *group_count_function;
static const char group_count_func_name[] = "GROUP_COUNT";
static const char group_count_func_syntax[] = "GROUP_COUNT([groupname][@category])";
static const char group_count_func_synopsis[] = "Counts the number of channels in the specified group";
static const char group_count_func_desc[] =
	"Calculates the group count for the specified group, or uses the\n"
	"channel's current group if not specifed (and non-empty).\n";

static void *group_match_count_function;
static const char group_match_count_func_name[] = "GROUP_MATCH_COUNT";
static const char group_match_count_func_syntax[] = "GROUP_MATCH_COUNT(groupmatch[@category])";
static const char group_match_count_func_synopsis[] = "Counts the number of channels in the groups matching the specified pattern";
static const char group_match_count_func_desc[] =
	"Calculates the group count for all groups that match the specified pattern.\n"
	"Uses standard regular expression matching (see regex(7)).\n";

static void *group_function;
static const char group_func_name[] = "GROUP";
static const char group_func_synopsis[] = "Gets or sets the channel group.";
static const char group_func_syntax[] = "GROUP([category])";
static const char group_func_desc[] = "Gets or sets the channel group.\n";

static void *group_list_function;
static const char group_list_func_name[] = "GROUP_LIST";
static const char group_list_func_synopsis[] = "Gets a list of the groups set on a channel.";
static const char group_list_func_syntax[] = "GROUP_LIST()";
static const char group_list_func_desc[] = "Gets a list of the groups set on a channel.\n";


static int group_count_function_read(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	char group[80] = "", category[80] = "";
	int count= -1;

	CW_UNUSED(argc);

	if (result) {
		cw_app_group_split_group(argv[0], group, sizeof(group), category, sizeof(category));

		if ((count = cw_app_group_get_count(group, category)) == -1)
			cw_log(CW_LOG_NOTICE, "No group could be found for channel '%s'\n", chan->name);	
		else
			cw_dynstr_printf(result, "%d", count);
	}

	return 0;
}

static int group_match_count_function_read(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	char group[80] = "";
	char category[80] = "";
	int count;

	CW_UNUSED(chan);
	CW_UNUSED(argc);

	if (result) {
		cw_app_group_split_group(argv[0], group, sizeof(group), category, sizeof(category));

		if (!cw_strlen_zero(group)) {
			count = cw_app_group_match_get_count(group, category);
			cw_dynstr_printf(result, "%d", count);
		}
	}

	return 0;
}

static int group_function_rw(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	if (argc > 0) {
		cw_dynstr_t ds = CW_DYNSTR_INIT;

		if (argc > 1 && argv[1][0]) {
			cw_dynstr_printf(&ds, "%s@%s", argv[1], argv[0]);
		} else {
			cw_dynstr_printf(&ds, "%s", argv[0]);
		}

		if (!ds.error && cw_app_group_set_channel(chan, ds.data)) {
                	cw_log(CW_LOG_WARNING, "Setting a group requires an argument (group name)\n");
			ds.error = 1;
		}

		cw_dynstr_free(&ds);
		if (ds.error)
			return -1;
	}

	if (result) {
		struct cw_group_info *gi;

		cw_app_group_list_lock();

		gi = cw_app_group_list_head();
		while (gi) {
			if (gi->chan != chan)
				continue;
			if (argc <= 0 || !argv[0][0])
				break;
			if (!cw_strlen_zero(gi->category) && !strcasecmp(gi->category, argv[0]))
				break;
			gi = CW_LIST_NEXT(gi, list);
		}

		if (gi)
			cw_dynstr_printf(result, "%s", gi->group);

		cw_app_group_list_unlock();
	}

	return 0;
}


static int group_list_function_read(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	cw_dynstr_t tmp1 = CW_DYNSTR_INIT;
	cw_dynstr_t tmp2 = CW_DYNSTR_INIT;
	struct cw_group_info *gi = NULL;

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_app_group_list_lock();

	for (gi = cw_app_group_list_head(); gi; gi = CW_LIST_NEXT(gi, list)) {
		if (gi->chan != chan)
			continue;
		if (tmp1.used) {
			cw_dynstr_printf(&tmp2, "%s", tmp1.data);

			if (!tmp2.error) {
				if (!cw_strlen_zero(gi->category))
					cw_dynstr_printf(&tmp1, "%s %s@%s", tmp2.data, gi->group, gi->category);
				else
					cw_dynstr_printf(&tmp1, "%s %s", tmp2.data, gi->group);
			} else
				tmp1.error = 1;

			cw_dynstr_reset(&tmp2);
		} else {
			if (!cw_strlen_zero(gi->category))
				cw_dynstr_printf(&tmp1, "%s@%s", gi->group, gi->category);
			else
				cw_dynstr_printf(&tmp1, "%s", gi->group);
		}
	}

	cw_app_group_list_unlock();

	cw_dynstr_free(&tmp2);

	if (!tmp1.error)
		cw_dynstr_printf(result, "%s", tmp1.data);
	else
		result->error = 1;

	cw_dynstr_free(&tmp1);

	return 0;
}

static const char tdesc[] = "database functions";

static int unload_module(void)
{
        int res = 0;

	res |= cw_unregister_function(group_count_function);
	res |= cw_unregister_function(group_match_count_function);
	res |= cw_unregister_function(group_function);
	res |= cw_unregister_function(group_list_function);
        return res;
}

static int load_module(void)
{
	group_count_function = cw_register_function(group_count_func_name, group_count_function_read, group_count_func_synopsis, group_count_func_syntax, group_count_func_desc);
	group_match_count_function = cw_register_function(group_match_count_func_name, group_match_count_function_read, group_match_count_func_synopsis, group_match_count_func_syntax, group_match_count_func_desc);
	group_function = cw_register_function(group_func_name, group_function_rw, group_func_synopsis, group_func_syntax, group_func_desc);
	group_list_function = cw_register_function(group_list_func_name, group_list_function_read, group_list_func_synopsis, group_list_func_syntax, group_list_func_desc);
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
