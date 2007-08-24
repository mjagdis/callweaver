/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright 2004 - 2005, Anthony Minessale <anthmct@yahoo.com>
 *
 * Anthony Minessale <anthmct@yahoo.com>
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
 * \brief While Loop and ExecIf Implementations
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/utils.h"
#include "callweaver/config.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"
#include "callweaver/options.h"

#define ALL_DONE(u,ret) {LOCAL_USER_REMOVE(u); return ret;}


static void *execif_app;
static const char execif_name[] = "ExecIf";
static const char execif_synopsis[] = "Conditional exec";
static const char execif_syntax[] = "ExecIF (expr, app, arg, ...)";
static const char execif_desc[] = 
"If <expr> is true, execute and return the result of <app>(<arg>, ...).\n"
"If <expr> is true, but <app> is not found, then the application\n"
"will return a non-zero value.";

static void *while_app;
static const char while_name[] = "While";
static const char while_synopsis[] = "Start A While Loop";
static const char while_syntax[] = "While(expr)";
static const char while_desc[] = 
"Start a While Loop.  Execution will return to this point when\n"
"EndWhile is called until expr is no longer true.\n";


static void *endwhile_app;
static const char endwhile_name[] = "EndWhile";
static const char endwhile_synopsis[] = "End A While Loop";
static const char endwhile_syntax[] = "EndWhile()";
static const char endwhile_desc[] = 
"Return to the previous called While\n\n";

static const char tdesc[] = "While Loops and Conditional Execution";


static int execif_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	int res=0;
	struct localuser *u;

	if (argc < 2 || !argv[0][0] || !argv[1][0])
		return opbx_function_syntax(execif_syntax);

	LOCAL_USER_ADD(u);

	if (opbx_true(argv[0]))
		res = opbx_function_exec(chan, opbx_hash_app_name(argv[1]), argv[1], argc - 2, argv + 2, NULL, 0);
		
	ALL_DONE(u,res);
}

#define VAR_SIZE 64


static char *get_index(struct opbx_channel *chan, const char *prefix, int index) {
	char varname[VAR_SIZE];

	snprintf(varname, VAR_SIZE, "%s_%d", prefix, index);
	return pbx_builtin_getvar_helper(chan, varname);
}

static struct opbx_exten *find_matching_priority(struct opbx_context *c, const char *exten, int priority, const char *callerid)
{
	struct opbx_exten *e;
	struct opbx_include *i;
	struct opbx_context *c2;
	struct opbx_exten *p;
    int hit;

	for (e = opbx_walk_context_extensions(c, NULL);  e;  e = opbx_walk_context_extensions(c, e))
    {
        switch (opbx_extension_pattern_match(exten, opbx_get_extension_name(e)))
        {
        case EXTENSION_MATCH_EXACT:
        case EXTENSION_MATCH_STRETCHABLE:
        case EXTENSION_MATCH_POSSIBLE:
			if (opbx_get_extension_matchcid(e))
            {
                switch (opbx_extension_pattern_match(callerid, opbx_get_extension_cidmatch(e)))
                {
                case EXTENSION_MATCH_EXACT:
                case EXTENSION_MATCH_STRETCHABLE:
                case EXTENSION_MATCH_POSSIBLE:
                    hit = 1;
                    break;
                default:
                    hit = 0;
                    break;
                }
            }
            else
            {
                hit = 1;
            }
			if (hit)
            {
				/* This is the matching extension we want */
				for (p = opbx_walk_extension_priorities(e, NULL);  p;  p = opbx_walk_extension_priorities(e, p))
                {
					if (priority != opbx_get_extension_priority(p))
						continue;
					return p;
				}
			}
            break;
		}
	}

	/* No match; run through includes */
	for (i = opbx_walk_context_includes(c, NULL);  i;  i = opbx_walk_context_includes(c, i))
    {
		for (c2 = opbx_walk_contexts(NULL);  c2;  c2 = opbx_walk_contexts(c2))
        {
			if (!strcmp(opbx_get_context_name(c2), opbx_get_include_name(i)))
            {
				if ((e = find_matching_priority(c2, exten, priority, callerid)))
					return e;
			}
		}
	}
	return NULL;
}

static int find_matching_endwhile(struct opbx_channel *chan)
{
	struct opbx_context *c;
	int res = -1;

	if (opbx_lock_contexts())
    {
		opbx_log(OPBX_LOG_ERROR, "Failed to lock contexts list\n");
		return -1;
	}

	for (c=opbx_walk_contexts(NULL); c; c=opbx_walk_contexts(c)) {
		struct opbx_exten *e;

		if (!opbx_lock_context(c)) {
			if (!strcmp(opbx_get_context_name(c), chan->context)) {
				/* This is the matching context we want */
				int cur_priority = chan->priority + 1, level=1;

				for (e = find_matching_priority(c, chan->exten, cur_priority, chan->cid.cid_num); e; e = find_matching_priority(c, chan->exten, ++cur_priority, chan->cid.cid_num)) {
					if (!strcasecmp(opbx_get_extension_app(e), "WHILE")) {
						level++;
					} else if (!strcasecmp(opbx_get_extension_app(e), "ENDWHILE")) {
						level--;
					}

					if (level == 0) {
						res = cur_priority;
						break;
					}
				}
			}
			opbx_unlock_context(c);
			if (res > 0) {
				break;
			}
		}
	}
	opbx_unlock_contexts();
	return res;
}

static int _while_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max, int end)
{
	int res=0;
	struct localuser *u;
	char *while_pri = NULL;
	char *goto_str = NULL, *my_name = NULL;
	char *label = NULL;
	char varname[VAR_SIZE], end_varname[VAR_SIZE];
	const char *prefix = "WHILE";
	size_t size=0;
	int used_index_i = -1, x=0;
	char used_index[VAR_SIZE] = "0", new_index[VAR_SIZE] = "0";

	if (!end && argc != 1)
		return opbx_function_syntax(while_syntax);

	if (!chan) {
		/* huh ? */
		return -1;
	}

	LOCAL_USER_ADD(u);

	/* dont want run away loops if the chan isn't even up
	   this is up for debate since it slows things down a tad ......
	*/
	if (opbx_waitfordigit(chan,1) < 0)
		ALL_DONE(u,-1);


	for (x=0;;x++) {
		if (get_index(chan, prefix, x)) {
			used_index_i = x;
		} else 
			break;
	}
	
	snprintf(used_index, VAR_SIZE, "%d", used_index_i);
	snprintf(new_index, VAR_SIZE, "%d", used_index_i + 1);
	
	size = strlen(chan->context) + strlen(chan->exten) + 32;
	my_name = alloca(size);
	snprintf(my_name, size, "%s_%s_%d", chan->context, chan->exten, chan->priority);
	
	if (opbx_strlen_zero(label)) {
		if (end) 
			label = used_index;
		else if (!(label = pbx_builtin_getvar_helper(chan, my_name))) {
			label = new_index;
			pbx_builtin_setvar_helper(chan, my_name, label);
		}
		
	}
	
	snprintf(varname, VAR_SIZE, "%s_%s", prefix, label);
	while_pri = pbx_builtin_getvar_helper(chan, varname);
	
	if ((while_pri = pbx_builtin_getvar_helper(chan, varname)) && !end) {
		snprintf(end_varname, VAR_SIZE, "END_%s", varname);
	}
	

	if (!end && !opbx_true(argv[0])) {
		/* Condition Met (clean up helper vars) */
		pbx_builtin_setvar_helper(chan, varname, NULL);
		pbx_builtin_setvar_helper(chan, my_name, NULL);
		snprintf(end_varname, VAR_SIZE, "END_%s", varname);
		if ((goto_str=pbx_builtin_getvar_helper(chan, end_varname))) {
			pbx_builtin_setvar_helper(chan, end_varname, NULL);
			opbx_parseable_goto(chan, goto_str);
		} else {
			int pri = find_matching_endwhile(chan);
			if (pri > 0) {
				if (option_verbose > 2)
					opbx_verbose(VERBOSE_PREFIX_3 "Jumping to priority %d\n", pri);
				chan->priority = pri;
			} else {
				opbx_log(OPBX_LOG_WARNING, "Couldn't find matching EndWhile? (While at %s@%s priority %d)\n", chan->context, chan->exten, chan->priority);
			}
		}
		ALL_DONE(u,res);
	}

	if (!end && !while_pri) {
		size = strlen(chan->context) + strlen(chan->exten) + 32;
		goto_str = alloca(size);
		snprintf(goto_str, size, "%s,%s,%d", chan->context, chan->exten, chan->priority);
		pbx_builtin_setvar_helper(chan, varname, goto_str);
	} 

	else if (end && while_pri) {
		/* END of loop */
		snprintf(end_varname, VAR_SIZE, "END_%s", varname);
		if (! pbx_builtin_getvar_helper(chan, end_varname)) {
			size = strlen(chan->context) + strlen(chan->exten) + 32;
			goto_str = alloca(size);
			snprintf(goto_str, size, "%s,%s,%d", chan->context, chan->exten, chan->priority+1);
			pbx_builtin_setvar_helper(chan, end_varname, goto_str);
		}
		opbx_parseable_goto(chan, while_pri);
	}
	



	ALL_DONE(u, res);
}

static int while_start_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max) {
	return _while_exec(chan, argc, argv, NULL, 0, 0);
}

static int while_end_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max) {
	return _while_exec(chan, argc, argv, NULL, 0, 1);
}


static int unload_module(void)
{
	int res = 0;

	res |= opbx_unregister_function(while_app);
	res |= opbx_unregister_function(execif_app);
	res |= opbx_unregister_function(endwhile_app);
	return res;
}

static int load_module(void)
{
	while_app = opbx_register_function(while_name, while_start_exec, while_synopsis, while_syntax, while_desc);
	execif_app = opbx_register_function(execif_name, execif_exec, execif_synopsis, execif_syntax, execif_desc);
	endwhile_app = opbx_register_function(endwhile_name, while_end_exec, endwhile_synopsis, endwhile_syntax, endwhile_desc);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
