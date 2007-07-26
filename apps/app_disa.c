/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Jim Dixon <jim@lambdatel.com>
 *
 * Made only slightly more sane by Mark Spencer <markster@digium.com>
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
 * \brief DISA -- Direct Inward System Access Application
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <sys/time.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/app.h"
#include "callweaver/indications.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/ulaw.h"
#include "callweaver/phone_no_utils.h"

static char *tdesc = "DISA (Direct Inward System Access) Application";

static void *disa_app;
static const char *disa_name = "DISA";
static const char *disa_synopsis = "DISA (Direct Inward System Access)";
static const char *disa_syntax = "DISA(numeric passcode[, context]) or disa(filename)";
static const char *disa_descrip = 
	"The DISA, Direct Inward System Access, application allows someone from \n"
	"outside the telephone switch (PBX) to obtain an \"internal\" system \n"
	"dialtone and to place calls from it as if they were placing a call from \n"
	"within the switch.\n"
	"DISA plays a dialtone. The user enters their numeric passcode, followed by\n"
	"the pound sign (#). If the passcode is correct, the user is then given\n"
	"system dialtone on which a call may be placed. Obviously, this type\n"
	"of access has SERIOUS security implications, and GREAT care must be\n"
	"taken NOT to compromise your security.\n\n"
	"There is a possibility of accessing DISA without password. Simply\n"
	"exchange your password with \"no-password\".\n\n"
	"    Example: exten => s,1,DISA(no-password, local)\n\n"
	"Be aware that using this compromises the security of your PBX.\n\n"
	"The arguments to this application (in extensions.conf) allow either\n"
	"specification of a single global passcode (that everyone uses), or\n"
	"individual passcodes contained in a file. It also allow specification\n"
	"of the context on which the user will be dialing. If no context is\n"
	"specified, the DISA application defaults the context to \"disa\".\n"
	"Presumably a normal system will have a special context set up\n"
	"for DISA use with some or a lot of restrictions. \n\n"
	"The file that contains the passcodes (if used) allows specification\n"
	"of either just a passcode (defaulting to the \"disa\" context, or\n"
	"passcode|context on each line of the file. The file may contain blank\n"
	"lines, or comments starting with \"#\" or \";\". In addition, the\n"
	"above arguments may have ,new-callerid-string appended to them, to\n"
	"specify a new (different) callerid to be used for this call, for\n"
	"example: numeric-passcode, context, \"My Phone\" <(234) 123-4567> or \n"
	"full-pathname-of-passcode-file, \"My Phone\" <(234) 123-4567>.  Last\n"
	"but not least, ,mailbox[@context] may be appended, which will cause\n"
	"a stutter-dialtone (indication \"dialrecall\") to be used, if the\n"
	"specified mailbox contains any new messages, for example:\n"
	"numeric-passcode, context, , 1234 (w/a changing callerid).  Note that\n"
	"in the case of specifying the numeric-passcode, the context must be\n"
	"specified if the callerid is specified also.\n\n"
	"If login is successful, the application looks up the dialed number in\n"
	"the specified (or default) context, and executes it if found.\n"
	"If the user enters an invalid extension and extension \"i\" (invalid) \n"
	"exists in the context, it will be used.\n";


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static void play_dialtone(struct opbx_channel *chan, char *mailbox)
{
	const struct tone_zone_sound *ts = NULL;

	if (opbx_app_has_voicemail(mailbox, NULL))
		ts = opbx_get_indication_tone(chan->zone, "dialrecall");
	else
		ts = opbx_get_indication_tone(chan->zone, "dial");
	if (ts)
		opbx_playtones_start(chan, 0, ts->data, 0);
	else
		opbx_tonepair_start(chan, 350, 440, 0, 0);
}

static int disa_exec(struct opbx_channel *chan, int argc, char **argv)
{
	int i;
    int j;
    int k;
    int x;
    int did_ignore;
	int firstdigittimeout = 20000;
	int digittimeout = 10000;
	struct localuser *u;
    char exten[OPBX_MAX_EXTENSION];
    char acctcode[20] = "";
	char *ourcontext;
    char *ourcallerid;
    char ourcidname[256];
    char ourcidnum[256];
    char *mailbox;
	struct opbx_frame *f;
	struct timeval lastdigittime;
	int res;
	time_t rstart;
	FILE *fp;
	char *stringp = NULL;
	char inbuf[128];

	if (argc < 1 || argc > 3) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", disa_syntax);
		return -1;
	}

	LOCAL_USER_ADD(u);
	
	if (chan->pbx) {
		firstdigittimeout = chan->pbx->rtimeout*1000;
		digittimeout = chan->pbx->dtimeout*1000;
	}
	
	if (opbx_set_write_format(chan,OPBX_FORMAT_ULAW)) {
		opbx_log(LOG_WARNING, "Unable to set write format to Mu-law on %s\n",chan->name);
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	if (opbx_set_read_format(chan,OPBX_FORMAT_ULAW)) {
		opbx_log(LOG_WARNING, "Unable to set read format to Mu-law on %s\n",chan->name);
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	
	opbx_log(LOG_DEBUG, "Digittimeout: %d\n", digittimeout);
	opbx_log(LOG_DEBUG, "Responsetimeout: %d\n", firstdigittimeout);

	ourcontext = (argc > 1 && argv[1][0] ? argv[1] : "disa");
	ourcallerid = (argc > 2 && argv[2][0] ? argv[2] : NULL);
	mailbox = (argc > 3 && argv[3][0] ? argv[3] : "");

	if (chan->_state != OPBX_STATE_UP)
    {
		/* answer */
		opbx_answer(chan);
	}
	i = k = x = 0; /* k is 0 for pswd entry, 1 for ext entry */
	did_ignore = 0;
	exten[0] = 0;
	acctcode[0] = 0;
	/* can we access DISA without password? */ 

	opbx_log(LOG_DEBUG, "Context: %s\n",ourcontext);

	if (!strcasecmp(argv[0], "no-password"))
    {
		k |= 1; /* We have the password */
		opbx_log(LOG_DEBUG, "DISA no-password login success\n");
	}
	lastdigittime = opbx_tvnow();

	play_dialtone(chan, mailbox);

	for (;;)
    {
	    /* if outa time, give em reorder */
		if (opbx_tvdiff_ms(opbx_tvnow(), lastdigittime) > 
		    ((k & 2) ? digittimeout : firstdigittimeout))
		{
			opbx_log(LOG_DEBUG,"DISA %s entry timeout on chan %s\n",
				((k&1) ? "extension" : "password"),chan->name);
			break;
		}
		if ((res = opbx_waitfor(chan, -1) < 0))
        {
			opbx_log(LOG_DEBUG, "Waitfor returned %d\n", res);
			continue;
		}
			
		f = opbx_read(chan);
		if (f == NULL) 
		{
			LOCAL_USER_REMOVE(u);
			return -1;
		}
		if ((f->frametype == OPBX_FRAME_CONTROL)
            &&
		    (f->subclass == OPBX_CONTROL_HANGUP))
		{
			opbx_fr_free(f);
			LOCAL_USER_REMOVE(u);
			return -1;
		}
		if (f->frametype == OPBX_FRAME_VOICE)
        {
			opbx_fr_free(f);
			continue;
		}
		  /* if not DTMF, just do it again */
		if (f->frametype != OPBX_FRAME_DTMF) 
		{
			opbx_fr_free(f);
			continue;
		}

		j = f->subclass;  /* save digit */
		opbx_fr_free(f);
		if (i == 0) 
		{
			k|=2; /* We have the first digit */ 
			opbx_playtones_stop(chan);
		}
		lastdigittime = opbx_tvnow();
		/* got a DTMF tone */
		if (i < OPBX_MAX_EXTENSION) /* if still valid number of digits */
		{
			if (!(k & 1)) /* if in password state */
			{
				if (j == '#') /* end of password */
				{
					  /* see if this is an integer */
					if (isdigit(argv[0][0]))
					{
                        /* nope, it must be a filename */
						if ((fp = fopen(argv[0],"r")) == NULL)
						{
							opbx_log(LOG_WARNING,"DISA password file %s not found on chan %s\n",argv[0],chan->name);
							LOCAL_USER_REMOVE(u);
							return -1;
						}
						argv[0][0] = 0;
						while (fgets(inbuf,sizeof(inbuf) - 1,fp))
						{
							char *stringp = NULL;
                            char *stringp2;
                            
							if (!inbuf[0])
                                continue;
							if (inbuf[strlen(inbuf) - 1] == '\n') 
								inbuf[strlen(inbuf) - 1] = 0;
							if (!inbuf[0])
                                continue;
						    /* skip comments */
							if (inbuf[0] == '#')
                                continue;
							if (inbuf[0] == ';')
                                continue;
							stringp = inbuf;
							strsep(&stringp, "|,");
							stringp2 = strsep(&stringp, "|,");
							if (stringp2)
                            {
								ourcontext = stringp2;
								stringp2 = strsep(&stringp, "|,");
								if (stringp2)
                                    ourcallerid = stringp2;
							}
							mailbox = strsep(&stringp, "|,");
							if (!mailbox)
								mailbox = "";

						    /* password must be in valid format (numeric) */
							if (sscanf(inbuf, "%d", &j) < 1)
                                continue;
							/* if we got it */
							if (!strcmp(exten, inbuf))
                                break;
						}
						fclose(fp);
					}
					/* compare the two */
					if (strcmp(exten,inbuf))
					{
						opbx_log(LOG_WARNING,"DISA on chan %s got bad password %s\n",chan->name,exten);
						goto reorder;
					}
					/* password good, set to dial state */
					opbx_log(LOG_DEBUG,"DISA on chan %s password is good\n",chan->name);
					play_dialtone(chan, mailbox);

					k |= 1; /* In number mode */
					i = 0;  /* re-set buffer pointer */
					exten[sizeof(acctcode)] = 0;
					opbx_copy_string(acctcode, exten, sizeof(acctcode));
					exten[0] = 0;
					opbx_log(LOG_DEBUG,"Successful DISA log-in on chan %s\n",chan->name);
					continue;
				}
			}

			exten[i++] = j;  /* save digit */
			exten[i] = 0;
			if (!(k & 1))
                continue; /* if getting password, continue doing it */
			  /* if this exists */

			if (opbx_ignore_pattern(ourcontext, exten))
            {
				play_dialtone(chan, "");
				did_ignore = 1;
			}
            else
            {
				if (did_ignore)
                {
					opbx_playtones_stop(chan);
					did_ignore = 0;
				}
            }
			/* if can do some more, do it */
			if (!opbx_matchmore_extension(chan,ourcontext,exten,1, chan->cid.cid_num))
				break;
		}
	}

	if (k == 3)
    {
		int recheck = 0;

		if (!opbx_exists_extension(chan, ourcontext, exten, 1, chan->cid.cid_num))
        {
			pbx_builtin_setvar_helper(chan, "INVALID_EXTEN", exten);
			exten[0] = 'i';
			exten[1] = '\0';
			recheck = 1;
		}
		if (!recheck || opbx_exists_extension(chan, ourcontext, exten, 1, chan->cid.cid_num))
        {
			opbx_playtones_stop(chan);
			/* We're authenticated and have a target extension */
			if (ourcallerid  &&  *ourcallerid)
			{
				opbx_callerid_split(ourcallerid, ourcidname, sizeof(ourcidname), ourcidnum, sizeof(ourcidnum));
				opbx_set_callerid(chan, ourcidnum, ourcidname, ourcidnum);
			}

			if (!opbx_strlen_zero(acctcode))
				opbx_copy_string(chan->accountcode, acctcode, sizeof(chan->accountcode));

			opbx_cdr_reset(chan->cdr, OPBX_CDR_FLAG_POSTED);
			opbx_explicit_goto(chan, ourcontext, exten, 1);
			LOCAL_USER_REMOVE(u);
			return 0;
		}
	}

	/* Received invalid, but no "i" extension exists in the given context */

reorder:

	opbx_indicate(chan,OPBX_CONTROL_CONGESTION);
	/* something is invalid, give em reorder for several seconds */
	time(&rstart);
	while(time(NULL) < rstart + 10)
	{
		if (opbx_waitfor(chan, -1) < 0)
			break;
		if ((f = opbx_read(chan)) == NULL)
			break;
		opbx_fr_free(f);
	}
	opbx_playtones_stop(chan);
	LOCAL_USER_REMOVE(u);
	return -1;
}

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	res |= opbx_unregister_application(disa_app);
	return res;
}

int load_module(void)
{
	disa_app = opbx_register_application(disa_name, disa_exec, disa_synopsis, disa_syntax, disa_descrip);
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
