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
 * \brief Realtime PBX Module
 *
 * \arg See also: \ref cwARA
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/config.h"
#include "callweaver/options.h"
#include "callweaver/switch.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/frame.h"
#include "callweaver/manager.h"
#include "callweaver/cli.h"
#include "callweaver/linkedlists.h"
#include "callweaver/chanvars.h"
#include "callweaver/sched.h"
#include "callweaver/io.h"
#include "callweaver/utils.h"
#include "callweaver/crypto.h"
#include "callweaver/callweaver_db.h"

#define MODE_MATCH 		0
#define MODE_MATCHMORE 	1
#define MODE_CANMATCH 	2

#define EXT_DATA_SIZE 256

static const char tdesc[] = "Realtime Switch";

/* Realtime switch looks up extensions in the supplied realtime table.

	[context@][realtimetable][/options]

	If the realtimetable is omitted it is assumed to be "extensions".  If no context is 
	specified the context is assumed to be whatever is the container.

	The realtime table should have entries for context,exten,priority,app,args
	
	The realtime table currently does not support callerid fields.

*/


#define REALTIME_COMMON(mode) \
	char *buf; \
	char *opts; \
	const char *cxt; \
	char *table; \
	int res=-1; \
	struct cw_variable *var=NULL; \
	buf = cw_strdupa(data); \
	opts = strchr(buf, '/'); \
	if (opts) { \
		*opts='\0'; \
		opts++; \
	} else \
		opts = (char *)""; \
	table = strchr(buf, '@'); \
	if (table) { \
		*table = '\0'; \
		table++;\
		cxt = buf; \
	} else cxt = NULL; \
	if (!cxt || cw_strlen_zero(cxt)) \
		cxt = context;\
	if (!table || cw_strlen_zero(table)) \
		table = (char *)"extensions"; \
	var = realtime_switch_common(table, cxt, exten, priority, mode);

static struct cw_variable *realtime_switch_common(const char *table, const char *context, const char *exten, int priority, int mode)
{
	char rexten[CW_MAX_EXTENSION + 20]="";
	char pri[20];
	struct cw_variable *var;
	struct cw_config *cfg;
	const char *ematch;
	int match;

	snprintf(pri, sizeof(pri), "%d", priority);
	switch (mode)
    {
	case MODE_MATCHMORE:
		ematch = "exten LIKE";
		snprintf(rexten, sizeof(rexten), "%s_%%", exten);
		break;
	case MODE_CANMATCH:
		ematch = "exten LIKE";
		snprintf(rexten, sizeof(rexten), "%s%%", exten);
		break;
	case MODE_MATCH:
	default:
		ematch = "exten";
		strncpy(rexten, exten, sizeof(rexten) - 1);
	}
	var = cw_load_realtime(table, ematch, rexten, "context", context, "priority", pri, NULL);
	if (!var)
    {
		cfg = cw_load_realtime_multientry(table, "exten LIKE", "\\_%", "context", context, "priority", pri, NULL);	
		if (cfg)
        {
			char *cat = cw_category_browse(cfg, NULL);

			while (cat)
            {
                match = cw_extension_pattern_match(exten, cat);
				switch (mode)
                {
				case MODE_MATCHMORE:
                    match = (match == EXTENSION_MATCH_STRETCHABLE  ||  match == EXTENSION_MATCH_INCOMPLETE  ||  match == EXTENSION_MATCH_POSSIBLE);
					break;
				case MODE_CANMATCH:
                    match = (match == EXTENSION_MATCH_EXACT  ||  match == EXTENSION_MATCH_STRETCHABLE  ||  match == EXTENSION_MATCH_INCOMPLETE  ||  match == EXTENSION_MATCH_POSSIBLE);
					break;
				case MODE_MATCH:
				default:
                    match = (match == EXTENSION_MATCH_EXACT  ||  match == EXTENSION_MATCH_STRETCHABLE  ||  match == EXTENSION_MATCH_POSSIBLE);
				}
				if (match)
                {
					var = cw_category_detach_variables(cw_category_get(cfg, cat));
					break;
				}
				cat = cw_category_browse(cfg, cat);
			}
			cw_config_destroy(cfg);
		}
	}
	return var;
}

static int realtime_exists(struct cw_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	CW_UNUSED(chan);
	CW_UNUSED(callerid);

	REALTIME_COMMON(MODE_MATCH);
	if (var) cw_variables_destroy(var);
	if (var)
		res = 1;
	return res > 0 ? res : 0;
}

static int realtime_canmatch(struct cw_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	CW_UNUSED(chan);
	CW_UNUSED(callerid);

	REALTIME_COMMON(MODE_CANMATCH);
	if (var) cw_variables_destroy(var);
	if (var)
		res = 1;
	return res > 0 ? res : 0;
}

static int realtime_exec(struct cw_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	char app[256];
	char appdata[512]="";
	char *tmp = NULL;
	struct cw_variable *v;

	CW_UNUSED(callerid);

	REALTIME_COMMON(MODE_MATCH);
	if (var) {
		v = var;
		while(v) {
			if (!strcasecmp(v->name, "app"))
				strncpy(app, v->value, sizeof(app) -1 );
			else if (!strcasecmp(v->name, "appdata"))
				tmp = cw_strdupa(v->value);
			v = v->next;
		}
		cw_variables_destroy(var);
		if (!cw_strlen_zero(app)) {
			if(!cw_strlen_zero(tmp))
			   pbx_substitute_variables(chan, &chan->vars, tmp, appdata, sizeof(appdata));
			if (option_verbose > 2)
	 		    cw_verbose( VERBOSE_PREFIX_3 "Executing [%s@%s:%d] %s(\"%s\", \"%s\")\n",
				    exten, context, priority,
			            app,
				    chan->name,
				    appdata
			    );
			cw_manager_event(EVENT_FLAG_CALL, "Newexten",
				7,
				cw_msg_tuple("Channel",     "%s\r\n", chan->name),
				cw_msg_tuple("Context",     "%s\r\n", chan->context),
				cw_msg_tuple("Extension",   "%s\r\n", chan->exten),
				cw_msg_tuple("Priority",    "%d\r\n", chan->priority),
				cw_msg_tuple("Application", "%s\r\n", app),
				cw_msg_tuple("AppData",     "%s\r\n", appdata),
				cw_msg_tuple("Uniqueid",    "%s\r\n", chan->uniqueid)
			);
			res = cw_function_exec_str(chan, cw_hash_string(app), app, appdata, NULL, 0);
		}
	}
	return res;
}

static int realtime_matchmore(struct cw_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	CW_UNUSED(chan);
	CW_UNUSED(callerid);

	REALTIME_COMMON(MODE_MATCHMORE);
	if (var) cw_variables_destroy(var);
	if (var)
		res = 1;
	return res > 0 ? res : 0;
}

static struct cw_switch realtime_switch =
{
        name:                   "Realtime",
        description:   		"Realtime Dialplan Switch",
        exists:                 realtime_exists,
        canmatch:               realtime_canmatch,
        exec:                   realtime_exec,
        matchmore:              realtime_matchmore,
};


static int unload_module(void)
{
	cw_switch_unregister(&realtime_switch);
	return 0;
}

static int load_module(void)
{
	cw_switch_register(&realtime_switch);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
