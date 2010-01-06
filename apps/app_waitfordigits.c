/*
 * vim:ts=4:sw=4:autoindent:smartindent:cindent
 *
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Trivial application to playback a sound file
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <pty.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include <callweaver/lock.h>
#include <callweaver/file.h>
#include <callweaver/logger.h>
#include <callweaver/channel.h>
#include <callweaver/pbx.h>
#include <callweaver/module.h>
#include <callweaver/translate.h>
#include <callweaver/utils.h>
#include <callweaver/cli.h>
#include <callweaver/keywords.h>

#ifdef CW_MODULE_INFO
#define CW_VERSION_1_4
#else
#define CW_VERSION_1_2
#endif

static const char tdesc[] = "Waits for overlap dialed digits";

static void *waitfordigits_app;
static const char waitfordigits_name[] = "WaitForDigits";
static const char waitfordigits_synopsis[] = "Wait for digits";
static const char waitfordigits_syntax[] = "WaitForDigits(milliseconds,[maxnum],addexten,[control],[priority]):\n";
static const char waitfordigits_descrip[] =
" WaitForDigits(milliseconds,[maxnum],addexten,[control],[priority]):\n"
" Waits for given milliseconds for digits which are dialed in overlap mode.\n" 
" Upon exit, sets the variable NEWEXTEN to the new extension (old + new digits)\n"
" The original EXTEN is not touched\n"
"\n"
" [maxnum] -	waitfordigits waits only until [maxnum] digits are dialed\n"
" 		then it exits immediately\n"
"\n"
" 'addexten' -	indicates that the dialed digits should be added\n"
" 		to the predialed extension. Note that this is not necessary\n"
"		for mISDN channels, they add overlap digits automatically\n"
"\n"
" [control] -	this option allows you to send a CW_CONTROL_XX frame\n"
" 		after the timeout has expired.\n" 
"		see (include/callweaver/frame.h) for possible values\n"
" 		this is useful for mISDN channels to send a PROCEEDING\n"
"		which is the number 15.\n"
"\n"
" [priority] -	Jumps to the given Priority in the current context. Note\n"
"		that the extension might have changed in waitfordigits!\n"
"		This option is especially useful if you use waitfordigits\n"
"		in the 's' extension to jump to the 1. priority of the\n"
"		post-dialed extenions.\n"
"\n"
"Channel Variables:\n"
" ALREADY_WAITED indicates if waitfordigits has already waited. If this\n"
" 		 Variable is set, the application exits immediatly, without\n"
"		 further waiting.\n"
"\n"
" WAIT_STOPKEY	 Set this to a Digit which you want to use for immediate\n"
"		 exit of waitfordigits before the timeout has expired.\n"
"		 WAIT_STOPKEY=# would be a typical value\n"
;

//#include "compat.h"


#define CHANNEL_INFO " (%s,%s)"
#ifdef ASTERISK_STATBLE
#define CHANNEL_INFO_PARTS(a) , a->callerid, a->exten
#else
#define CHANNEL_INFO_PARTS(a) , a->cid.cid_num, a->exten
#endif



static int waitfordigits_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	char numsubst[255];
	struct cw_var_t *var;
	struct localuser *u;

	/** Default values **/
	int timeout=3500;
	int maxnum=20;
	int addexten=0;
	
	int control=0;
	int nextprio=0;

	char aw=0;

	char dig=0;
	int res = 0;

	CW_UNUSED(result);
	CW_UNUSED(result_max);

	if (argc < 1 || argc > 5)
	{
		cw_log(CW_LOG_ERROR, "Syntax: %s\n", waitfordigits_syntax);
		return -1;
	}

	LOCAL_USER_ADD(u);
	
	if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_ALREADY_WAITED, "ALREADY_WAITED")))
	{
		aw = atoi(var->value);
		cw_object_put(var);
	}

// waitfordigits(milliseconds,[maxnum],addexten,[control],[priority]):\n"
	if (argv[0])
	{
		timeout=atoi(argv[0]);
	}
	else
	{
		cw_log(CW_LOG_WARNING, "WaitForDigits was passed invalid data '%s'. The timeout must be a positive integer.\n", argv[0]);
	}
	if (argv[0] == NULL || timeout <= 0)

	if (argc >= 2 && argv[0])
	{
		maxnum = atoi(argv[0]);
	}

	if (argc >= 3 && argv[2])
	{
		if (strcmp(argv[2], "addexten") == 0 || strcmp(argv[2], "true"))
		{
			addexten = 1;
		}
	}

	if (argc >= 4 && argv[3])
	{
		control=atoi(argv[3]);
	}

	if (argc >= 5 && argv[4])
	{
		nextprio=atoi(argv[4]);
	}

	cw_verbose("You passed timeout:%d maxnum:%d addexten:%d control:%d\n",
	      timeout, maxnum, addexten, control);

	/** Check wether we have something to do **/
	if (chan->_state == CW_STATE_UP || aw > 0 )
	{
	  LOCAL_USER_REMOVE(u);
	  return 0;
	}

	pbx_builtin_setvar_helper(chan,"ALREADY_WAITED","1");

	/** Saving predialed Extension **/
	var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_WAIT_STOPKEY, "WAIT_STOPKEY") ;
	strcpy(numsubst, chan->exten);
	while ( (strlen(numsubst)< maxnum) && (dig=cw_waitfordigit(chan, timeout)))
	{
		int l=strlen(numsubst); 
		if (dig<=0)
		{
			if (dig ==-1)
			{
				cw_log(CW_LOG_NOTICE, "Timeout reached.\n ");
			}
			else
			{
				cw_log(CW_LOG_NOTICE, "Error at adding dig: %c!\n",dig);
				res=-1;
			}
			break;
		}

		if (var && (dig == var->value[0])) {
			break;
		}

		numsubst[l]=dig;
		numsubst[l+1]=0;
		//cw_log(CW_LOG_NOTICE, "Adding Dig to Chan: %c\n",dig);
	}
	if (var)
		cw_object_put(var);

	/** Restoring Extension if requested **/
	if (addexten)
	{
		cw_verbose("Overwriting extension:%s with new Number: %s\n",chan->exten, numsubst);
		strcpy(chan->exten, numsubst);
	}
	else
	{
		cw_verbose( "Not Overwriting extension:%s with new Number: %s\n",chan->exten, numsubst);
	}
	  
	/** Posting Exten to Var: NEWEXTEN **/
	pbx_builtin_setvar_helper(chan,"NEWEXTEN",numsubst);
  
	if (chan->_state != CW_STATE_UP && (control>0) ) {
		cw_verbose( "Sending CONTROL: %d  to Channel %s\n",control, chan->exten);
		cw_indicate(chan, control);
	}
	else
	{
		cw_verbose( "Not Sending any control to Channel %s state is %d\n",chan->exten, chan->_state);
	}
	
	if (nextprio>0)
	{ 
		chan->priority=--nextprio;
	}

	LOCAL_USER_REMOVE(u);

	return res;
}

static int unload_module(void)
{
	return cw_unregister_function(waitfordigits_app);
}

static int load_module(void)
{
	waitfordigits_app = cw_register_function(waitfordigits_name, waitfordigits_exec, waitfordigits_synopsis, waitfordigits_syntax, waitfordigits_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
