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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

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
		return OPBX_CAUSE_BUSY;
	if (!strcasecmp(cause, "CONGESTION"))
		return OPBX_CAUSE_CONGESTION;
	if (!strcasecmp(cause, "ANSWER"))
		return OPBX_CAUSE_NORMAL;
	if (!strcasecmp(cause, "CANCEL"))
		return OPBX_CAUSE_NORMAL;
	if (!strcasecmp(cause, "NOANSWER"))
		return OPBX_CAUSE_NOANSWER;
	if (!strcasecmp(cause, "NOCHANAVAIL"))
		return OPBX_CAUSE_CONGESTION;
	opbx_log(OPBX_LOG_WARNING, "Unknown cause '%s', using NORMAL\n", cause);
	return OPBX_CAUSE_NORMAL;
}

static int osplookup_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct opbx_osp_result ospres;
	struct localuser *u;
	char *provider;
	int res = 0;

	if (argc < 1 || argc > 3)
		return opbx_function_syntax(syntax);

	LOCAL_USER_ADD(u);

	provider = (argc > 1 && argv[1][0] ? argv[1] : NULL);
	/* There are no options supported?!? */

	opbx_log(OPBX_LOG_DEBUG, "Whoo hoo, looking up OSP on '%s' via '%s'\n", argv[0], provider ? provider : "<default>");
	if ((res = opbx_osp_lookup(chan, provider, argv[0], chan->cid.cid_num, &ospres)) > 0) {
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
			opbx_log(OPBX_LOG_NOTICE, "OSP Lookup failed for '%s' (provider '%s')\n", argv[0], provider ? provider : "<default>");
		else
			opbx_log(OPBX_LOG_DEBUG, "Got hangup on '%s' while doing OSP Lookup for '%s' (provider '%s')!\n", chan->name, argv[0], provider ? provider : "<default>" );
	}
	if (!res) {
		/* Look for a "busy" place */
		opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
	} else if (res > 0)
		res = 0;
	LOCAL_USER_REMOVE(u);
	return res;
}

static int ospnext_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct opbx_osp_result ospres;
	struct localuser *u;
	char *temp;
	int cause;
	int res = 0;

	if (argc != 1)
		return opbx_function_syntax(syntax2);

	LOCAL_USER_ADD(u);

	cause = str2cause(argv[0]);
	temp = pbx_builtin_getvar_helper(chan, "OSPHANDLE");
	ospres.handle = -1;
	if (!opbx_strlen_zero(temp) && (sscanf(temp, "%d", &ospres.handle) == 1) && (ospres.handle > -1)) {
		if ((res = opbx_osp_next(&ospres, cause)) > 0) {
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
				opbx_log(OPBX_LOG_NOTICE, "OSP Lookup Next failed for handle '%d'\n", ospres.handle);
			else
				opbx_log(OPBX_LOG_DEBUG, "No OSP handle specified\n");
		} else
			opbx_log(OPBX_LOG_DEBUG, "Got hangup on '%s' while doing OSP Next!\n", chan->name);
	}
	if (!res) {
		/* Look for a "busy" place */
		opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
	} else if (res > 0)
		res = 0;
	LOCAL_USER_REMOVE(u);
	return res;
}

static int ospfinished_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct opbx_osp_result ospres;
	struct localuser *u;
	char *temp;
	time_t start=0, duration=0;
	int cause;
	int res=0;

	if (argc != 1)
		return opbx_function_syntax(syntax3);

	LOCAL_USER_ADD(u);	

	if (chan->cdr) {
		start = chan->cdr->answer.tv_sec;
		if (start)
			duration = time(NULL) - start;
		else
			duration = 0;
	} else
		opbx_log(OPBX_LOG_WARNING, "OSPFinish called on channel '%s' with no CDR!\n", chan->name);
	
	cause = str2cause(argv[0]);
	temp = pbx_builtin_getvar_helper(chan, "OSPHANDLE");
	ospres.handle = -1;
	if (!opbx_strlen_zero(temp) && (sscanf(temp, "%d", &ospres.handle) == 1) && (ospres.handle > -1)) {
		if (!opbx_osp_terminate(ospres.handle, cause, start, duration)) {
			pbx_builtin_setvar_helper(chan, "_OSPHANDLE", "");
			res = 1;
		}
	} else {
		if (!res) {
			if (ospres.handle > -1)
				opbx_log(OPBX_LOG_NOTICE, "OSP Finish failed for handle '%d'\n", ospres.handle);
			else
				opbx_log(OPBX_LOG_DEBUG, "No OSP handle specified\n");
		} else
			opbx_log(OPBX_LOG_DEBUG, "Got hangup on '%s' while doing OSP Terminate!\n", chan->name);
	}
	if (!res) {
		/* Look for a "busy" place */
		opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
	} else if (res > 0)
		res = 0;
	LOCAL_USER_REMOVE(u);
	return res;
}


static int unload_module(void)
{
	int res = 0;

	res |= opbx_unregister_function(app3);
	res |= opbx_unregister_function(app2);
	res |= opbx_unregister_function(app);
	return res;
}

static int load_module(void)
{
	app = opbx_register_function(name, osplookup_exec, synopsis, syntax, descrip);
	app2 = opbx_register_function(name2, ospnext_exec, synopsis2, syntax2, descrip2);
	app3 = opbx_register_function(name3, ospfinished_exec, synopsis3, syntax3, descrip3);
	return 0;
}

int reload(void)
{
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
