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
 * \brief Loopback PBX Module
 *
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
#include "callweaver/file.h"
#include "callweaver/cli.h"
#include "callweaver/lock.h"
#include "callweaver/linkedlists.h"
#include "callweaver/chanvars.h"
#include "callweaver/sched.h"
#include "callweaver/io.h"
#include "callweaver/utils.h"
#include "callweaver/crypto.h"
#include "callweaver/callweaver_db.h"

static const char tdesc[] = "Loopback Switch";

/* Loopback switch substitutes ${EXTEN}, ${CONTEXT}, and ${PRIORITY} into
   the data passed to it to try to get a string of the form:

	[exten]@context[:priority][/extramatch]
   
   Where exten, context, and priority are another extension, context, and priority
   to lookup and "extramatch" is an extra match restriction the *original* number 
   must fit if  specified.  The "extramatch" begins with _ like an exten pattern
   if it is specified.  Note that the search context MUST be a different context
   from the current context or the search will not succeed in an effort to reduce
   the likelihood of loops (they're still possible if you try hard, so be careful!)

*/


static void loopback_parse(cw_dynstr_t *ds_p, const char *data, const char **newexten, const char **newcontext, int *newpriority, const char **newpattern)
{
	char tmp[80];
	struct cw_registry reg;

	cw_var_registry_init(&reg, 8);
	snprintf(tmp, sizeof(tmp), "%d", *newpriority);
	cw_var_assign(&reg, "EXTEN", *newexten);
	cw_var_assign(&reg, "CONTEXT", *newcontext);
	cw_var_assign(&reg, "PRIORITY", tmp);

	pbx_substitute_variables(NULL, &reg, data, ds_p);

	cw_registry_destroy(&reg);

	if (!ds_p->error && ds_p->used) {
		char *con = NULL, *pri = NULL;
		int inquote, i, j;

		/* [exten]@context[:priority][/extramatch] */
		inquote = i = j = 0;
		while (ds_p->data[i]) {
			switch (ds_p->data[i]) {
				case '"':
					inquote = !inquote;
					i++;
					break;

				case '@':
					if (!inquote && !con) {
						i++;
						ds_p->data[j++] = '\0';
						con = &ds_p->data[j];
					}
					break;
				case ':':
					if (!inquote && !pri) {
						i++;
						ds_p->data[j++] = '\0';
						pri = &ds_p->data[j];
					}
					break;
				case '/':
					if (!inquote && !*newpattern) {
						i++;
						ds_p->data[j++] = '\0';
						*newpattern = &ds_p->data[j];
					}
					break;

				case '\\':
					if (ds_p->data[i + 1] && strchr("\"\\", ds_p->data[i + 1]))
						i++;
					/* Fall through */
				default:
					ds_p->data[j++] = ds_p->data[i++];
					break;
			}
		}
		ds_p->data[j] = '\0';

		if (ds_p->data[0])
			*newexten = &ds_p->data[0];

		if (con)
			*newcontext = con;

		if (pri)
			*newpriority = atoi(pri);

		cw_log(CW_LOG_DEBUG, "Parsed into %s @ %s priority %d\n", *newexten, *newcontext, *newpriority);
	}
}


static int loopback_exists(struct cw_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	cw_dynstr_t ds = CW_DYNSTR_INIT;
	const char *newcontext = context;
	const char *newexten = exten;
	const char *newpattern = NULL;
	int newpriority = priority;
	int res = -1;

	loopback_parse(&ds, data, &newexten, &newcontext, &newpriority, &newpattern);

	if (strcasecmp(newcontext, context)) {
		res = cw_exists_extension(chan, newcontext, newexten, newpriority, callerid);

		if (newpattern) {
			switch (cw_extension_pattern_match(exten, newpattern)) {
				case EXTENSION_MATCH_EXACT:
				case EXTENSION_MATCH_STRETCHABLE:
				case EXTENSION_MATCH_POSSIBLE:
					break;
				default:
					res = 0;
					break;
			}
		}
	}

	cw_dynstr_free(&ds);

	return res;
}

static int loopback_canmatch(struct cw_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	cw_dynstr_t ds = CW_DYNSTR_INIT;
	const char *newcontext = context;
	const char *newexten = exten;
	const char *newpattern = NULL;
	int newpriority = priority;
	int res = -1;

	loopback_parse(&ds, data, &newexten, &newcontext, &newpriority, &newpattern);

	if (strcasecmp(newcontext, context)) {
		res = cw_canmatch_extension(chan, newcontext, newexten, newpriority, callerid);
		if (newpattern) {
			switch (cw_extension_pattern_match(exten, newpattern)) {
				case EXTENSION_MATCH_EXACT:
				case EXTENSION_MATCH_STRETCHABLE:
				case EXTENSION_MATCH_POSSIBLE:
					break;
				default:
					res = 0;
					break;
			}
		}
	}

	cw_dynstr_free(&ds);

	return res;
}

static int loopback_exec(struct cw_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	cw_dynstr_t ds = CW_DYNSTR_INIT;
	const char *newcontext = context;
	const char *newexten = exten;
	const char *newpattern = NULL;
	int newpriority = priority;
	int res = -1;

	loopback_parse(&ds, data, &newexten, &newcontext, &newpriority, &newpattern);

	if (strcasecmp(newcontext, context)) {
		res = cw_exec_extension(chan, newcontext, newexten, newpriority, callerid);
		if (newpattern) {
			switch (cw_extension_pattern_match(exten, newpattern)) {
				case EXTENSION_MATCH_EXACT:
				case EXTENSION_MATCH_STRETCHABLE:
				case EXTENSION_MATCH_POSSIBLE:
					break;
				default:
					res = -1;
					break;
			}
		}
	}

	cw_dynstr_free(&ds);

	return res;
}

static int loopback_matchmore(struct cw_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	cw_dynstr_t ds = CW_DYNSTR_INIT;
	const char *newcontext = context;
	const char *newexten = exten;
	const char *newpattern = NULL;
	int newpriority = priority;
	int res = -1;

	loopback_parse(&ds, data, &newexten, &newcontext, &newpriority, &newpattern);

	if (strcasecmp(newcontext, context)) {
		res = cw_matchmore_extension(chan, newcontext, newexten, newpriority, callerid);
		if (newpattern) {
			switch (cw_extension_pattern_match(exten, newpattern)) {
				case EXTENSION_MATCH_EXACT:
				case EXTENSION_MATCH_STRETCHABLE:
				case EXTENSION_MATCH_POSSIBLE:
					break;
				default:
					res = 0;
					break;
			}
		}
	}

	cw_dynstr_free(&ds);

	return res;
}

static struct cw_switch loopback_switch =
{
        name:                   "Loopback",
        description:   		"Loopback Dialplan Switch",
        exists:                 loopback_exists,
        canmatch:               loopback_canmatch,
        exec:                   loopback_exec,
        matchmore:              loopback_matchmore,
};


static int unload_module(void)
{
	cw_switch_unregister(&loopback_switch);
	return 0;
}

static int load_module(void)
{
	cw_switch_register(&loopback_switch);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
