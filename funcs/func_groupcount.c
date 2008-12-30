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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

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


static int group_count_function_read(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	char group[80] = "";
	char category[80] = "";
	struct cw_var_t *var;
	int count;

	if (buf) {
		cw_app_group_split_group(argv[0], group, sizeof(group), category, sizeof(category));

		if (cw_strlen_zero(group)) {
			if ((var = pbx_builtin_getvar_helper(chan, cw_hash_var_name(category), category))) {
				cw_copy_string(group, var->value, sizeof(group));
				cw_object_put(var);
			} else
				cw_log(CW_LOG_NOTICE, "No group could be found for channel '%s'\n", chan->name);	
		}

		count = cw_app_group_get_count(group, category);
		snprintf(buf, len, "%d", count);
	}

	return 0;
}

static int group_match_count_function_read(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	int count;
	char group[80] = "";
	char category[80] = "";

	if (buf) {
		cw_app_group_split_group(argv[0], group, sizeof(group), category, sizeof(category));

		if (!cw_strlen_zero(group)) {
			count = cw_app_group_match_get_count(group, category);
			snprintf(buf, len, "%d", count);
		}
	}

	return 0;
}

static int group_function_rw(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	struct cw_var_t *var;

	if (argc > 0) {
		char tmp[256];

		if (argc > 1 && argv[1][0]) {
			snprintf(tmp, sizeof(tmp), "%s@%s", argv[1], argv[0]);
		} else {
			cw_copy_string(tmp, argv[0], sizeof(tmp));
		}

        	if (cw_app_group_set_channel(chan, tmp)) {
                	cw_log(CW_LOG_WARNING, "Setting a group requires an argument (group name)\n");
			return -1;
		}
	}

	if (buf) {
		if (argc > 0 && argv[0][0]) {
			snprintf(buf, len, "%s_%s", GROUP_CATEGORY_PREFIX, argv[0]);
		} else {
			cw_copy_string(buf, GROUP_CATEGORY_PREFIX, len);
		}

		if ((var = pbx_builtin_getvar_helper(chan, cw_hash_var_name(buf), buf))) {
			cw_copy_string(buf, var->value, len);
			cw_object_put(var);
		} else
			*buf = '\0';
	}

	return 0;
}


struct group_list_function_read_args {
	char tmp1[1024];
	char tmp2[1024];
};

static int group_list_function_read_one(struct cw_object *obj, void *data)
{
	struct cw_var_t *var = container_of(obj, struct cw_var_t, obj);
	struct group_list_function_read_args *args = data;

	if (!strncmp(cw_var_name(var), GROUP_CATEGORY_PREFIX "_", sizeof(GROUP_CATEGORY_PREFIX "_"))) {
		if (!cw_strlen_zero(args->tmp1)) {
			cw_copy_string(args->tmp2, args->tmp1, sizeof(args->tmp2));
			snprintf(args->tmp1, sizeof(args->tmp1), "%s %s@%s", args->tmp2, var->value, cw_var_name(var) + sizeof(GROUP_CATEGORY_PREFIX));
		} else {
			snprintf(args->tmp1, sizeof(args->tmp1), "%s@%s", var->value, cw_var_name(var) + sizeof(GROUP_CATEGORY_PREFIX));
		}
	} else if (!strcmp(cw_var_name(var), GROUP_CATEGORY_PREFIX)) {
		if (!cw_strlen_zero(args->tmp1)) {
			cw_copy_string(args->tmp2, args->tmp1, sizeof(args->tmp2));
			snprintf(args->tmp1, sizeof(args->tmp1), "%s %s", args->tmp2, var->value);
		} else {
			snprintf(args->tmp1, sizeof(args->tmp1), "%s", var->value);
		}
	}

	return 0;
}

static int group_list_function_read(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	struct group_list_function_read_args args = {
		.tmp1 = "",
		.tmp2 = "",
	};

	if (buf) {
		cw_registry_iterate(&chan->vars, group_list_function_read_one, &args);
		cw_copy_string(buf, args.tmp1, len);
	}

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
