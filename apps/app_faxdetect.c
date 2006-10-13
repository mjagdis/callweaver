/*
 * Openpbx.org -- A telephony toolkit for Linux.
 *
 * Fax detection application for all channel types. 
 * Copyright (C) 2004-2005, Newman Telecom, Inc. and Newman Ventures, Inc.
 *
 * Justin Newman <jnewman@newmantelecom.com>
 *
 * We would like to thank Newman Ventures, Inc. for funding this
 * Asterisk project.
 * Newman Ventures <info@newmanventures.com>
 *
 * Modified and ported to openpbx.org by
 * Massimo CtRiX Cetra <devel@navynet.it>
 * Thanks to Navynet SRL for funding this project
 *
 * Portions Copyright:
 * Copyright (C) 2001, Linux Support Services, Inc.
 * Copyright (C) 2004, Digium, Inc.
 *
 * Matthew Fredrickson <creslin@linux-support.net>
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "openpbx.h"

#include <openpbx/lock.h>
#include <openpbx/file.h>
#include <openpbx/logger.h>
#include <openpbx/channel.h>
#include <openpbx/pbx.h>
#include <openpbx/module.h>
#include <openpbx/translate.h>
#include <openpbx/dsp.h>
#include <openpbx/utils.h>

static char *tdesc = "Fax detection application";

static char *app = "FaxDetect";

static char *synopsis = "Detects fax sounds on all channel types (IAX and SIP too)";

static char *descrip = 
"  FaxDetect([waitdur[|options[|sildur[|mindur[|maxdur]]]]]):\n"
"This application listens for fax tones (on IAX and SIP channels too)\n"
"for waitdur seconds of time. In addition, it can be interrupted by digits,\n"
"or non-silence. Audio is only monitored in the receive direction. If\n"
"digits interrupt, they must be the start of a valid extension unless the\n"
"option is included to ignore. If fax is detected, it will jump to the\n"
"'fax' extension. If a period of non-silence greater than 'mindur' ms,\n"
"yet less than 'maxdur' ms is followed by silence at least 'sildur' ms\n"
"then the app is aborted and processing jumps to the 'talk' extension.\n"
"if you have entered some DTMF digits, the TALK_DTMF_DID channel variable\n"
"will be set. This allows interaction with OGI scripts.\n"
"If all undetected, control will continue at the next priority.\n"
"      waitdur:  Maximum number of seconds to wait (default=4)\n"
"      options:\n"
"        'n':  Attempt on-hook if unanswered (default=no)\n"
"        'x':  DTMF digits terminate without extension (default=no)\n"
"        'd':  Ignore DTMF digit detection (default=no)\n"
"        'f':  Ignore fax detection (default=no)\n"
"        't':  Ignore talk detection (default=no)\n"
"      sildur:  Silence ms after mindur/maxdur before aborting (default=1000)\n"
"      mindur:  Minimum non-silence ms needed (default=100)\n"
"      maxdur:  Maximum non-silence ms allowed (default=0/forever)\n"
"Returns -1 on hangup, and 0 on successful completion with no exit conditions.\n\n"
"For questions or comments, please e-mail support@newmantelecom.com.\n";

// Use the second one for recent Asterisk releases
#define CALLERID_FIELD cid.cid_num
//#define CALLERID_FIELD callerid

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int detectfax_exec(struct opbx_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char tmp[256] = "\0";
	char dtmf_did[256] = "\0";
	char *p = NULL;
	char *waitstr = NULL;
	char *options = NULL;
	char *silstr = NULL;
	char *minstr = NULL;
	char *maxstr = NULL;
	struct opbx_frame *fr = NULL;
	struct opbx_frame *fr2 = NULL;
	struct opbx_frame *fr3 = NULL;
	int notsilent = 0;
	struct timeval start = {0, 0}, end = {0, 0};
	int waitdur = 4;
	int sildur = 1000;
	int mindur = 100;
	int maxdur = -1;
	int skipanswer = 0;
	int noextneeded = 0;
	int ignoredtmf = 0;
	int ignorefax = 0;
	int ignoretalk = 0;
	int x = 0;
	int origrformat = 0;
	int origwformat = 0;
	int features = 0;
	time_t timeout = 0;
	struct opbx_dsp *dsp = NULL;
	
	pbx_builtin_setvar_helper(chan, "FAX_DETECTED", "");
	pbx_builtin_setvar_helper(chan, "FAXEXTEN", "");
	pbx_builtin_setvar_helper(chan, "DTMF_DETECTED", "");
	pbx_builtin_setvar_helper(chan, "TALK_DETECTED", "");
        pbx_builtin_setvar_helper(chan, "TALK_DTMF_DID", "");

	if (data || !opbx_strlen_zero((char *)data)) {
		strncpy(tmp, (char *)data, sizeof(tmp)-1);
	}	
	
	p = tmp;
	
	waitstr = strsep(&p, "|");
	options = strsep(&p, "|");
	silstr = strsep(&p, "|");
	minstr = strsep(&p, "|");	
	maxstr = strsep(&p, "|");	
	
	if (waitstr) {
		if ((sscanf(waitstr, "%d", &x) == 1) && (x > 0))
			waitdur = x;
	}
	
	if (options) {
		if (strchr(options, 'n'))
			skipanswer = 1;
		if (strchr(options, 'x'))
			noextneeded = 1;
		if (strchr(options, 'd'))
			ignoredtmf = 1;
		if (strchr(options, 'f'))
			ignorefax = 1;
		if (strchr(options, 't'))
			ignoretalk = 1;
	}
	
	if (silstr) {
		if ((sscanf(silstr, "%d", &x) == 1) && (x > 0))
			sildur = x;
	}
	
	if (minstr) {
		if ((sscanf(minstr, "%d", &x) == 1) && (x > 0))
			mindur = x;
	}
	
	if (maxstr) {
		if ((sscanf(maxstr, "%d", &x) == 1) && (x > 0))
			maxdur = x;
	}
	
	opbx_log(LOG_DEBUG, "Preparing detect of fax (waitdur=%dms, sildur=%dms, mindur=%dms, maxdur=%dms)\n", 
						waitdur, sildur, mindur, maxdur);
						
	LOCAL_USER_ADD(u);
	if (chan->_state != OPBX_STATE_UP && !skipanswer) {
		/* Otherwise answer unless we're supposed to send this while on-hook */
		res = opbx_answer(chan);
	}
	if (!res) {
		origrformat = chan->readformat;
		if ((res = opbx_set_read_format(chan, OPBX_FORMAT_SLINEAR))) 
			opbx_log(LOG_WARNING, "Unable to set read format to linear!\n");
		origwformat = chan->writeformat;
		if ((res = opbx_set_write_format(chan, OPBX_FORMAT_SLINEAR))) 
			opbx_log(LOG_WARNING, "Unable to set write format to linear!\n");
	}
	if (!(dsp = opbx_dsp_new())) {
		opbx_log(LOG_WARNING, "Unable to allocate DSP!\n");
		res = -1;
	}
	
	if (dsp) {	
		if (!ignoretalk)
			; /* features |= DSP_FEATURE_SILENCE_SUPPRESS; */
		if (!ignorefax)
			features |= DSP_FEATURE_FAX_DETECT;
		//if (!ignoredtmf)
			features |= DSP_FEATURE_DTMF_DETECT;
			
		opbx_dsp_set_threshold(dsp, 256); 
		opbx_dsp_set_features(dsp, features | DSP_DIGITMODE_RELAXDTMF);
		opbx_dsp_digitmode(dsp, DSP_DIGITMODE_DTMF);
	}

	if (!res) {
		if (waitdur > 0)
			timeout = time(NULL) + (time_t)waitdur;

		while(opbx_waitfor(chan, -1) > -1) {
			if (waitdur > 0 && time(NULL) > timeout) {
				res = 0;
				break;
			}

			fr = opbx_read(chan);
			if (!fr) {
				opbx_log(LOG_DEBUG, "Got hangup\n");
				res = -1;
				break;
			}

			/* Check for a T38 switchover */
			if (chan->t38mode_enabled==1) {
			    opbx_log(LOG_DEBUG, "Fax detected on %s. T38 switchover completed.\n", chan->name);
			    if (strcmp(chan->exten, "fax")) {
				opbx_log(LOG_NOTICE, "Redirecting %s to fax extension\n", chan->name);
				pbx_builtin_setvar_helper(chan, "FAX_DETECTED", "1");
				pbx_builtin_setvar_helper(chan,"FAXEXTEN",chan->exten);								
				if (opbx_exists_extension(chan, chan->context, "fax", 1, chan->CALLERID_FIELD)) {
				    /* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
				    strncpy(chan->exten, "fax", sizeof(chan->exten)-1);
				    chan->priority = 0;									
				} else
				    opbx_log(LOG_WARNING, "Fax detected, but no fax extension\n");
				} else
				    opbx_log(LOG_WARNING, "Already in a fax extension, not redirecting\n");
				res = 0;
				opbx_frfree(fr);
				break;
			}


			fr2 = opbx_dsp_process(chan, dsp, fr);
			if (!fr2) {
				opbx_log(LOG_WARNING, "Bad DSP received (what happened?)\n");
				fr2 = fr;
			} 

			if (fr2->frametype == OPBX_FRAME_DTMF) {
				if (fr2->subclass == 'f' && !ignorefax) {
					/* Fax tone -- Handle and return NULL */
					opbx_log(LOG_DEBUG, "Fax detected on %s\n", chan->name);
					if (strcmp(chan->exten, "fax")) {
						opbx_log(LOG_NOTICE, "Redirecting %s to fax extension\n", chan->name);
						pbx_builtin_setvar_helper(chan, "FAX_DETECTED", "1");
						pbx_builtin_setvar_helper(chan,"FAXEXTEN",chan->exten);								
						if (opbx_exists_extension(chan, chan->context, "fax", 1, chan->CALLERID_FIELD)) {
							/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
							strncpy(chan->exten, "fax", sizeof(chan->exten)-1);
							chan->priority = 0;									
						} else
							opbx_log(LOG_WARNING, "Fax detected, but no fax extension\n");
					} else
						opbx_log(LOG_WARNING, "Already in a fax extension, not redirecting\n");

					res = 0;
					opbx_frfree(fr);
					break;
				} else if (!ignoredtmf) {
					opbx_log(LOG_DEBUG, "DTMF detected on %s\n", chan->name);
					char t[2];
					t[0] = fr2->subclass;
					t[1] = '\0';
					if (noextneeded || opbx_canmatch_extension(chan, chan->context, t, 1, chan->CALLERID_FIELD)) {
						pbx_builtin_setvar_helper(chan, "DTMF_DETECTED", "1");
						/* They entered a valid extension, or might be anyhow */
						if (noextneeded) {
							opbx_log(LOG_NOTICE, "DTMF received (not matching to exten)\n");
							res = 0;
						} else {
							opbx_log(LOG_NOTICE, "DTMF received (matching to exten)\n");
							res = fr2->subclass;
						}
						opbx_frfree(fr);
						break;
					} else {
						strncat(dtmf_did,t,sizeof(dtmf_did)-strlen(dtmf_did)-1 );
						opbx_log(LOG_DEBUG, "Valid extension requested and DTMF did not match [%s]\n",t);
					}
				}
			} else if ((fr->frametype == OPBX_FRAME_VOICE) && (fr->subclass == OPBX_FORMAT_SLINEAR) && !ignoretalk) {
				int totalsilence;
				int ms;

				//The following piece of code enables this application
				//to send empty frames. This solves fax detection problem
				//When a fax gets in with RTP. 
				//The CNG is detected, faxdetect gotos to fax extension
				//and if we are on a SIP channel the T38 switchover is done.
    				fr3=opbx_frdup(fr);
				memset(fr3->data,0,fr3->datalen);
				opbx_write(chan,fr3);
				opbx_frfree(fr3);

				res = opbx_dsp_silence(dsp, fr, &totalsilence);
				if (res && (totalsilence > sildur)) {
					/* We've been quiet a little while */
					if (notsilent) {
						/* We had heard some talking */
						gettimeofday(&end, NULL);
						ms = (end.tv_sec - start.tv_sec) * 1000;
						ms += (end.tv_usec - start.tv_usec) / 1000;
						ms -= sildur;
						if (ms < 0)
							ms = 0;
						if ((ms > mindur) && ((maxdur < 0) || (ms < maxdur))) {
							char ms_str[10];
							opbx_log(LOG_DEBUG, "Found qualified token of %d ms\n", ms);
							opbx_log(LOG_NOTICE, "Redirecting %s to talk extension\n", chan->name);

							/* Save detected talk time (in milliseconds) */ 
							sprintf(ms_str, "%d", ms);	
							pbx_builtin_setvar_helper(chan, "TALK_DETECTED", ms_str);
							pbx_builtin_setvar_helper(chan, "TALK_DTMF_DID", dtmf_did);

							if (opbx_exists_extension(chan, chan->context, "talk", 1, chan->CALLERID_FIELD)) {
								strncpy(chan->exten, "talk", sizeof(chan->exten) - 1);
								chan->priority = 0;
							} else
								opbx_log(LOG_WARNING, "Talk detected, but no talk extension\n");
							res = 0;
							opbx_frfree(fr);
							break;
						} else
							opbx_log(LOG_DEBUG, "Found unqualified token of %d ms\n", ms);
						notsilent = 0;
					}
				} else {
					if (!notsilent) {
						/* Heard some audio, mark the begining of the token */
						gettimeofday(&start, NULL);
						opbx_log(LOG_DEBUG, "Start of voice token!\n");
						notsilent = 1;
					}
				}						
			}
			opbx_frfree(fr);
		}
	} else
		opbx_log(LOG_WARNING, "Could not answer channel '%s'\n", chan->name);
	
	if (res > -1) {
		if (origrformat && opbx_set_read_format(chan, origrformat)) {
			opbx_log(LOG_WARNING, "Failed to restore read format for %s to %s\n", 
				chan->name, opbx_getformatname(origrformat));
		}
		if (origwformat && opbx_set_write_format(chan, origwformat)) {
			opbx_log(LOG_WARNING, "Failed to restore write format for %s to %s\n", 
				chan->name, opbx_getformatname(origwformat));
		}
	}
	
	if (dsp)
		opbx_dsp_free(dsp);
	
	LOCAL_USER_REMOVE(u);
	
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return opbx_unregister_application(app);
}

int load_module(void)
{
	return opbx_register_application(app, detectfax_exec, synopsis, descrip);
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

