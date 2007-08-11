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
 * \brief Dial plan proc Implementation
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/options.h"
#include "callweaver/config.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"

#define MAX_ARGS 80

/* special result value used to force proc exit */
#define PROC_EXIT_RESULT 1024

static char *tdesc = "Extension Procs";

static void *proc_app;
static const char *proc_name = "Proc";
static const char *proc_synopsis = "Proc Implementation";
static const char *proc_syntax = "Proc(procname, arg1, arg2 ...)";
static const char *proc_descrip =
"Executes a procedure using the context\n"
"'proc-<procname>', jumping to the 's' extension of that context and\n"
"executing each step, then returning when the steps end. \n"
"The calling extension, context, and priority are stored in ${PROC_EXTEN}, \n"
"${PROC_CONTEXT} and ${PROC_PRIORITY} respectively.  Arguments become\n"
"${ARG1}, ${ARG2}, etc in the proc context.\n"
"If you Goto out of the Proc context, the Proc will terminate and control\n"
"will be returned at the location of the Goto.\n"
"Proc returns -1 if any step in the proc returns -1, and 0 otherwise.\n" 
"If ${PROC_OFFSET} is set at termination, Proc will attempt to continue\n"
"at priority PROC_OFFSET + N + 1 if such a step exists, and N + 1 otherwise.\n";

static void *if_app;
static const char *if_name = "ProcIf";
static const char *if_synopsis = "Conditional Proc Implementation";
static const char *if_syntax = "ProcIf(expr ? procname_a[, arg ...] [: procname_b[, arg ...]])";
static const char *if_descrip =
"Executes proc defined in <procname_a> if <expr> is true\n"
"(otherwise <procname_b> if provided)\n"
"Arguments and return values as in application proc()\n";

static void *exit_app;
static const char *exit_name = "ProcExit";
static const char *exit_synopsis = "Exit From Proc";
static const char *exit_syntax = "ProcExit()";
static const char *exit_descrip =
"Causes the currently running proc to exit as if it had\n"
"ended normally by running out of priorities to execute.\n"
"If used outside a proc, will likely cause unexpected\n"
"behavior.\n";


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int proc_exec(struct opbx_channel *chan, int argc, char **argv)
{
	char *tmp;
	char *cur, *rest;
	char *proc;
	char fullproc[80];
	char varname[80];
	char *oldargs[MAX_ARGS + 1] = { NULL, };
	int x;
	int res=0;
	char oldexten[256]="";
	int oldpriority;
	char pc[80], depthc[12];
	char oldcontext[OPBX_MAX_CONTEXT] = "";
	char *offsets;
	int offset, depth;
	int setproccontext=0;
	int autoloopflag;
  
	char *save_proc_exten;
	char *save_proc_context;
	char *save_proc_priority;
	char *save_proc_offset;
	struct localuser *u;
 
	if (argc < 1) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", proc_syntax);
		return -1;
	}

	LOCAL_USER_ADD(u);

	/* Count how many levels deep the rabbit hole goes */
	tmp = pbx_builtin_getvar_helper(chan, "PROC_DEPTH");
	if (tmp) {
		sscanf(tmp, "%d", &depth);
	} else {
		depth = 0;
	}

	if (depth >= 7) {
		opbx_log(LOG_ERROR, "Proc():  possible infinite loop detected.  Returning early.\n");
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	snprintf(depthc, sizeof(depthc), "%d", depth + 1);
	pbx_builtin_setvar_helper(chan, "PROC_DEPTH", depthc);

	proc = argv[0];
	if (opbx_strlen_zero(proc)) {
		opbx_log(LOG_WARNING, "Invalid proc name specified\n");
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	snprintf(fullproc, sizeof(fullproc), "proc-%s", proc);
	if (!opbx_exists_extension(chan, fullproc, "s", 1, chan->cid.cid_num)) {
  		if (!opbx_context_find(fullproc)) 
			opbx_log(LOG_WARNING, "No such context '%s' for proc '%s'\n", fullproc, proc);
		else
	  		opbx_log(LOG_WARNING, "Context '%s' for proc '%s' lacks 's' extension, priority 1\n", fullproc, proc);
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	
	/* Save old info */
	oldpriority = chan->priority;
	opbx_copy_string(oldexten, chan->exten, sizeof(oldexten));
	opbx_copy_string(oldcontext, chan->context, sizeof(oldcontext));
	if (opbx_strlen_zero(chan->proc_context)) {
		opbx_copy_string(chan->proc_context, chan->context, sizeof(chan->proc_context));
		opbx_copy_string(chan->proc_exten, chan->exten, sizeof(chan->proc_exten));
		chan->proc_priority = chan->priority;
		setproccontext=1;
	}
	/* Save old proc variables */
	save_proc_exten = pbx_builtin_getvar_helper(chan, "PROC_EXTEN");
	if (save_proc_exten) 
		save_proc_exten = strdup(save_proc_exten);
	pbx_builtin_setvar_helper(chan, "PROC_EXTEN", oldexten);

	save_proc_context = pbx_builtin_getvar_helper(chan, "PROC_CONTEXT");
	if (save_proc_context)
		save_proc_context = strdup(save_proc_context);
	pbx_builtin_setvar_helper(chan, "PROC_CONTEXT", oldcontext);

	save_proc_priority = pbx_builtin_getvar_helper(chan, "PROC_PRIORITY");
	if (save_proc_priority) 
		save_proc_priority = strdup(save_proc_priority);
	snprintf(pc, sizeof(pc), "%d", oldpriority);
	pbx_builtin_setvar_helper(chan, "PROC_PRIORITY", pc);
  
	save_proc_offset = pbx_builtin_getvar_helper(chan, "PROC_OFFSET");
	if (save_proc_offset) 
		save_proc_offset = strdup(save_proc_offset);
	pbx_builtin_setvar_helper(chan, "PROC_OFFSET", NULL);

	/* Setup environment for new run */
	chan->exten[0] = 's';
	chan->exten[1] = '\0';
	opbx_copy_string(chan->context, fullproc, sizeof(chan->context));
	chan->priority = 1;

	for (x = 1; x < argc; x++) {
  		/* Save copy of old arguments if we're overwriting some, otherwise
	   	let them pass through to the other macro */
  		snprintf(varname, sizeof(varname), "ARG%d", x);
		oldargs[x] = pbx_builtin_getvar_helper(chan, varname);
		if (oldargs[x])
			oldargs[x] = strdup(oldargs[x]);
		pbx_builtin_setvar_helper(chan, varname, argv[x]);
	}
	autoloopflag = opbx_test_flag(chan, OPBX_FLAG_IN_AUTOLOOP);
	opbx_set_flag(chan, OPBX_FLAG_IN_AUTOLOOP);
	while(opbx_exists_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num)) {
		/* Reset the proc depth, if it was changed in the last iteration */
		pbx_builtin_setvar_helper(chan, "PROC_DEPTH", depthc);
		if ((res = opbx_exec_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num))) {
			/* Something bad happened, or a hangup has been requested. */
			if (((res >= '0') && (res <= '9')) || ((res >= 'A') && (res <= 'F')) ||
		    	(res == '*') || (res == '#')) {
				/* Just return result as to the previous application as if it had been dialed */
				opbx_log(LOG_DEBUG, "Oooh, got something to jump out with ('%c')!\n", res);
				break;
			}
			switch(res) {
	        	case PROC_EXIT_RESULT:
				res = 0;
				goto out;
			case OPBX_PBX_KEEPALIVE:
				if (option_debug)
					opbx_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited KEEPALIVE in proc %s on '%s'\n", chan->context, chan->exten, chan->priority, proc, chan->name);
				if (option_verbose > 1)
					opbx_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited KEEPALIVE in proc '%s' on '%s'\n", chan->context, chan->exten, chan->priority, proc, chan->name);
				goto out;
				break;
			default:
				if (option_debug)
					opbx_log(LOG_DEBUG, "Spawn extension (%s,%s,%d) exited non-zero on '%s' in proc '%s'\n", chan->context, chan->exten, chan->priority, chan->name, proc);
				if (option_verbose > 1)
					opbx_verbose( VERBOSE_PREFIX_2 "Spawn extension (%s, %s, %d) exited non-zero on '%s' in proc '%s'\n", chan->context, chan->exten, chan->priority, chan->name, proc);
				goto out;
			}
		}
		if (strcasecmp(chan->context, fullproc)) {
			if (option_verbose > 1)
				opbx_verbose(VERBOSE_PREFIX_2 "Channel '%s' jumping out of proc '%s'\n", chan->name, proc);
			break;
		}
		/* don't stop executing extensions when we're in "h" */
		if (chan->_softhangup && strcasecmp(oldexten,"h")) {
			opbx_log(LOG_DEBUG, "Extension %s, priority %d returned normally even though call was hung up\n",
				chan->exten, chan->priority);
			goto out;
		}
		chan->priority++;
  	}
	out:
	/* Reset the depth back to what it was when the routine was entered (like if we called Proc recursively) */
	snprintf(depthc, sizeof(depthc), "%d", depth);
	pbx_builtin_setvar_helper(chan, "PROC_DEPTH", depthc);

	opbx_set2_flag(chan, autoloopflag, OPBX_FLAG_IN_AUTOLOOP);
  	for (x=1; x<argc; x++) {
  		/* Restore old arguments and delete ours */
		snprintf(varname, sizeof(varname), "ARG%d", x);
  		if (oldargs[x]) {
			pbx_builtin_setvar_helper(chan, varname, oldargs[x]);
			free(oldargs[x]);
		} else {
			pbx_builtin_setvar_helper(chan, varname, NULL);
		}
  	}

	/* Restore proc variables */
	pbx_builtin_setvar_helper(chan, "PROC_EXTEN", save_proc_exten);
	if (save_proc_exten)
		free(save_proc_exten);
	pbx_builtin_setvar_helper(chan, "PROC_CONTEXT", save_proc_context);
	if (save_proc_context)
		free(save_proc_context);
	pbx_builtin_setvar_helper(chan, "PROC_PRIORITY", save_proc_priority);
	if (save_proc_priority)
		free(save_proc_priority);
	if (setproccontext) {
		chan->proc_context[0] = '\0';
		chan->proc_exten[0] = '\0';
		chan->proc_priority = 0;
	}

	if (!strcasecmp(chan->context, fullproc)) {
  		/* If we're leaving the proc normally, restore original information */
		chan->priority = oldpriority;
		opbx_copy_string(chan->context, oldcontext, sizeof(chan->context));
		if (!(chan->_softhangup & OPBX_SOFTHANGUP_ASYNCGOTO)) {
			/* Copy the extension, so long as we're not in softhangup, where we could be given an asyncgoto */
			opbx_copy_string(chan->exten, oldexten, sizeof(chan->exten));
			if ((offsets = pbx_builtin_getvar_helper(chan, "PROC_OFFSET"))) {
				/* Handle proc offset if it's set by checking the availability of step n + offset + 1, otherwise continue
			   	normally if there is any problem */
				if (sscanf(offsets, "%d", &offset) == 1) {
					if (opbx_exists_extension(chan, chan->context, chan->exten, chan->priority + offset + 1, chan->cid.cid_num)) {
						chan->priority += offset;
					}
				}
			}
		}
	}

	pbx_builtin_setvar_helper(chan, "PROC_OFFSET", save_proc_offset);
	if (save_proc_offset)
		free(save_proc_offset);
	LOCAL_USER_REMOVE(u);
	return res;
}

static int procif_exec(struct opbx_channel *chan, int argc, char **argv) 
{
	char *s, *q;
	int i;

	/* First argument is "<condition ? ..." */
	if (argc < 1 || !(s = strchr(argv[0], '?'))) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", if_syntax);
		return -1;
	}

	/* Trim trailing space from the condition */
	q = s;
	do { *(q--) = '\0'; } while (q >= argv[0] && isspace(*q));

	do { *(s++) = '\0'; } while (isspace(*s));

	if (pbx_checkcondition(argv[0])) {
		/* True: we want everything between '?' and ':' */
		argv[0] = s;
		for (i = 0; i < argc; i++) {
			if ((s = strchr(argv[i], ':'))) {
				do { *(s--) = '\0'; } while (s >= argv[i] && isspace(*s));
				argc = i + 1;
				break;
			}
		}
		return proc_exec(chan, argc, argv);
	} else {
		/* False: we want everything after ':' (if anything) */
		argv[0] = s;
		for (i = 0; i < argc; i++) {
			if ((s = strchr(argv[i], ':'))) {
				do { *(s++) = '\0'; } while (isspace(*s));
				argv[i] = s;
				return proc_exec(chan, argc - i, argv + i);
			}
		}
		/* No ": ..." so we just drop through */
		return 0;
	}
}
			
static int proc_exit_exec(struct opbx_channel *chan, int argc, char **argv)
{
	return PROC_EXIT_RESULT;
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= opbx_unregister_application(if_app);
	res |= opbx_unregister_application(exit_app);
	res |= opbx_unregister_application(proc_app);
	return res;
}

int load_module(void)
{
	exit_app = opbx_register_application(exit_name, proc_exit_exec, exit_synopsis, exit_syntax, exit_descrip);
	if_app = opbx_register_application(if_name, procif_exec, if_synopsis, if_syntax, if_descrip);
	proc_app = opbx_register_application(proc_name, proc_exec, proc_synopsis, proc_syntax, proc_descrip);
	return 0;
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
