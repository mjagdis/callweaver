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
 * \brief Open Settlement Protocol Lookup
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/options.h"
#include "callweaver/config.h"
#include "callweaver/module.h"
#include "callweaver/utils.h"
#include "callweaver/causes.h"
#include "callweaver/astosp.h"

static const char tdesc[] = "OSP Lookup";

static void *app;
static void *app2;
static void *app3;

static const char name[] = "OSPLookup";
static const char name2[] = "OSPNext";
static const char name3[] = "OSPFinish";

static const char synopsis[] = "Lookup number in OSP";
static const char synopsis2[] = "Lookup next OSP entry";
static const char synopsis3[] = "Record OSP entry";

static const char syntax[] = "OSPLookup(exten[, provider[, options]])";
static const char syntax2[] = "OSPNext";
static const char syntax3[] = "OSPFinish(status)";

static const char descrip[] = 
"Looks up an extension via OSP and sets\n"
"the variables, where 'n' is the number of the result beginning with 1:\n"
" ${OSPTECH}:   The technology to use for the call\n"
" ${OSPDEST}:   The destination to use for the call\n"
" ${OSPTOKEN}:  The actual OSP token as a string\n"
" ${OSPHANDLE}: The OSP Handle for anything remaining\n"
" ${OSPRESULTS}: The number of OSP results total remaining\n"
"\n"
"If the lookup was *not* successful and there exists a priority n + 101,\n"
"then that priority will be taken next.\n" ;

static const char descrip2[] = 
"Looks up the next OSP Destination for ${OSPHANDLE}\n"
"See OSPLookup for more information\n"
"\n"
"If the lookup was *not* successful and there exists a priority n + 101,\n"
"then that priority will be taken next.\n" ;

static const char descrip3[] = 
"Records call state for ${OSPHANDLE}, according to\n"
"status, which should be one of BUSY, CONGESTION, ANSWER, NOANSWER, or NOCHANAVAIL\n"
"or coincidentally, just what the Dial application stores in its ${DIALSTATUS}\n"
"\n"
"If the finishing was *not* successful and there exists a priority n + 101,\n"
"then that priority will be taken next.\n" ;


static int str2cause(char *cause)
{
	if (!strcasecmp(cause, "BUSY"))
		return CW_CAUSE_BUSY;
	if (!strcasecmp(cause, "CONGESTION"))
		return CW_CAUSE_CONGESTION;
	if (!strcasecmp(cause, "ANSWER"))
		return CW_CAUSE_NORMAL;
	if (!strcasecmp(cause, "CANCEL"))
		return CW_CAUSE_NORMAL;
	if (!strcasecmp(cause, "NOANSWER"))
		return CW_CAUSE_NOANSWER;
	if (!strcasecmp(cause, "NOCHANAVAIL"))
		return CW_CAUSE_CONGESTION;
	cw_log(CW_LOG_WARNING, "Unknown cause '%s', using NORMAL\n", cause);
	return CW_CAUSE_NORMAL;
}

static int osplookup_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct cw_osp_result ospres;
	struct localuser *u;
	char *provider;
	int res = 0;

	CW_UNUSED(result);

	if (argc < 1 || argc > 3)
		return cw_function_syntax(syntax);

	LOCAL_USER_ADD(u);

	provider = (argc > 1 && argv[1][0] ? argv[1] : NULL);
	/* There are no options supported?!? */

	cw_log(CW_LOG_DEBUG, "Whoo hoo, looking up OSP on '%s' via '%s'\n", argv[0], provider ? provider : "<default>");
	if ((res = cw_osp_lookup(chan, provider, argv[0], chan->cid.cid_num, &ospres)) > 0) {
		char tmp[80];
		snprintf(tmp, sizeof(tmp), "%d", ospres.handle);
		pbx_builtin_setvar_helper(chan, "_OSPHANDLE", tmp);
		pbx_builtin_setvar_helper(chan, "_OSPTECH", ospres.tech);
		pbx_builtin_setvar_helper(chan, "_OSPDEST", ospres.dest);
		pbx_builtin_setvar_helper(chan, "_OSPTOKEN", ospres.token);
		snprintf(tmp, sizeof(tmp), "%d", ospres.numresults);
		pbx_builtin_setvar_helper(chan, "_OSPRESULTS", tmp);

	} else {
		if (!res)
			cw_log(CW_LOG_NOTICE, "OSP Lookup failed for '%s' (provider '%s')\n", argv[0], provider ? provider : "<default>");
		else
			cw_log(CW_LOG_DEBUG, "Got hangup on '%s' while doing OSP Lookup for '%s' (provider '%s')!\n", chan->name, argv[0], provider ? provider : "<default>" );
	}
	if (!res) {
		/* Look for a "busy" place */
		cw_goto_if_exists_n(chan, chan->context, chan->exten, chan->priority + 101);
	} else if (res > 0)
		res = 0;
	LOCAL_USER_REMOVE(u);
	return res;
}

static int ospnext_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct cw_osp_result ospres;
	struct localuser *u;
	struct cw_var_t *var;
	int cause;
	int res = 0;

	CW_UNUSED(result);

	if (argc != 1)
		return cw_function_syntax(syntax2);

	LOCAL_USER_ADD(u);

	cause = str2cause(argv[0]);
	ospres.handle = -1;

	if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_OSPHANDLE, "OSPHANDLE"))) {
		if (sscanf(var->value, "%d", &ospres.handle) == 1 && ospres.handle > -1) {
			if ((res = cw_osp_next(&ospres, cause)) > 0) {
				char tmp[80];
				snprintf(tmp, sizeof(tmp), "%d", ospres.handle);
				pbx_builtin_setvar_helper(chan, "_OSPHANDLE", tmp);
				pbx_builtin_setvar_helper(chan, "_OSPTECH", ospres.tech);
				pbx_builtin_setvar_helper(chan, "_OSPDEST", ospres.dest);
				pbx_builtin_setvar_helper(chan, "_OSPTOKEN", ospres.token);
				snprintf(tmp, sizeof(tmp), "%d", ospres.numresults);
				pbx_builtin_setvar_helper(chan, "_OSPRESULTS", tmp);
			}
		} else {
			if (!res) {
				if (ospres.handle < 0)
					cw_log(CW_LOG_NOTICE, "OSP Lookup Next failed for handle '%d'\n", ospres.handle);
				else
					cw_log(CW_LOG_DEBUG, "No OSP handle specified\n");
			} else
				cw_log(CW_LOG_DEBUG, "Got hangup on '%s' while doing OSP Next!\n", chan->name);
		}
		cw_object_put(var);
	}
	if (!res) {
		/* Look for a "busy" place */
		cw_goto_if_exists_n(chan, chan->context, chan->exten, chan->priority + 101);
	} else if (res > 0)
		res = 0;

	LOCAL_USER_REMOVE(u);
	return res;
}

static int ospfinished_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct cw_osp_result ospres;
	struct localuser *u;
	struct cw_var_t *var;
	time_t start=0, duration=0;
	int cause;
	int res=0;

	CW_UNUSED(result);

	if (argc != 1)
		return cw_function_syntax(syntax3);

	LOCAL_USER_ADD(u);	

	if (chan->cdr) {
		start = chan->cdr->answer.tv_sec;
		if (start)
			duration = time(NULL) - start;
		else
			duration = 0;
	} else
		cw_log(CW_LOG_WARNING, "OSPFinish called on channel '%s' with no CDR!\n", chan->name);
	
	cause = str2cause(argv[0]);
	ospres.handle = -1;

	if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_OSPHANDLE, "OSPHANDLE"))) {
		if (sscanf(var->value, "%d", &ospres.handle) == 1 && ospres.handle > -1) {
			if (!cw_osp_terminate(ospres.handle, cause, start, duration)) {
				pbx_builtin_setvar_helper(chan, "_OSPHANDLE", "");
				res = 1;
			}
		} else {
			if (!res) {
				if (ospres.handle > -1)
					cw_log(CW_LOG_NOTICE, "OSP Finish failed for handle '%d'\n", ospres.handle);
				else
					cw_log(CW_LOG_DEBUG, "No OSP handle specified\n");
			} else
				cw_log(CW_LOG_DEBUG, "Got hangup on '%s' while doing OSP Terminate!\n", chan->name);
		}
		cw_object_put(var);
	}

	if (!res) {
		/* Look for a "busy" place */
		cw_goto_if_exists_n(chan, chan->context, chan->exten, chan->priority + 101);
	} else if (res > 0)
		res = 0;

	LOCAL_USER_REMOVE(u);
	return res;
}


static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(app3);
	res |= cw_unregister_function(app2);
	res |= cw_unregister_function(app);
	return res;
}

static int load_module(void)
{
	app = cw_register_function(name, osplookup_exec, synopsis, syntax, descrip);
	app2 = cw_register_function(name2, ospnext_exec, synopsis2, syntax2, descrip2);
	app3 = cw_register_function(name3, ospfinished_exec, synopsis3, syntax3, descrip3);
	return 0;
}

int reload(void)
{
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
