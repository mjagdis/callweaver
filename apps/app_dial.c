/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Eris Associates Limited, UK
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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
 * \brief dial() & retrydial() - Trivial application to dial a channel and send an URL on answer
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/app.h"
#include "callweaver/pbx.h"
#include "callweaver/options.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/say.h"
#include "callweaver/config.h"
#include "callweaver/features.h"
#include "callweaver/musiconhold.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"
#include "callweaver/causes.h"
#include "callweaver/manager.h"
#include "callweaver/privacy.h"
#include "callweaver/keywords.h"

static const char tdesc[] = "Dialing Application";

static void *dial_app;
static const char dial_name[] = "Dial";
static const char dial_synopsis[] = "Place a call and connect to the current channel";
static const char dial_syntax[] = "Dial({ Technology/resource[, Technology2/reseource2...] }[, timeout[, [option|URL]...]])\n"
				"or Dial(Technology/resource[&Technology2/resource2...][, timeout][, options][, URL])";
static const char dial_descrip[] =
"Requests one or more channels and places specified outgoing calls on them.\n"
"As soon as a channel answers, the Dial app will answer the originating\n"
"channel (if it needs to be answered) and will bridge a call with the channel\n"
"which first answered. All other calls placed by the Dial app will be hung up.\n"
"\n"
"If a timeout is not specified, the Dial application will wait indefinitely\n"
"until either one of the called channels answers, the user hangs up, or all\n"
"channels return busy or error. The dialer will return 0 if it was unable to\n"
"place the call, or the timeout expired.\n"
"\n"
"For the Privacy and Screening Modes, the DIALSTATUS variable will be set to DONTCALL, \n"
"if the called party chooses to send the calling party to the 'Go Away' script, and \n"
"the DIALSTATUS variable will be set to TORTURE, if the called party wants to send the\n"
"caller to the TORTURE scripts\n"
"\n"
"This application returns -1 if the originating channel hangs up, or if the call is\n"
"bridged and either of the parties in the bridge terminate the call.\n"
"\n"
"Options may be expressed as individual arguments or may be grouped in a single argument,\n"
"the only rule being that, if you are using the (recommended) Dial({...}, ...) syntax, an\n"
"option that takes a (...) MUST always end a group. That is to say: \"abc\", \"a, b, c\",\n"
"\"a, b(x), c\", \"ab(x), c\" are all legal but \"ab(x)c\" and \"a, b(x)c\" are only legal\n"
"when you use the Dial(...&..., ...) syntax.\n"
"\n"
"A URL is simply an option that starts with \"http\" and ends at the end of the current\n"
"argument.\n"
"\n"
"The option may contain zero or more of the following:\n"
"    A(x)\n"
"        - Play an announcement to the called party, using x as file\n"
"    C\n"
"        - Reset call detail record for this call.\n"
"    D([called][:calling])\n"
"        - Send DTMF strings *after* called party has answered, but before the\n"
"          call gets bridged. The 'called' DTMF string is sent to the called party, and the\n"
"          'calling' DTMF string is sent to the calling party. Both parameters can be used alone.\n"
"    d\n"
"        - Allow the calling user to dial a 1 digit extension while waiting for a call to\n"
"          be answered exiting to that extension if it exists in the context defined by\n"
"          ${EXITCONTEXT} or the current context.\n"
"    f\n"
"        - Forces callerid to be set as the extension of the line \n"
"          making/redirecting the outgoing call. For example, some PSTNs\n"
"          don't allow callerids from other extensions then the ones\n"
"          that are assigned to you.\n"
"    G(context^exten^pri)\n"
"        - If the call is answered transfer both parties to the specified exten.\n"
"    g\n"
"        - Goes on in context if the destination channel hangs up\n"
"    H\n"
"        - Allow caller to hang up by hitting *.\n"
"    h\n"
"        - Allow callee to hang up by hitting *.\n"
"    J(context^exten^pri)\n"
"        - When the caller hangs up, send the destination channel to the specified extension.\n"
"    L(x[:y][:z])\n"
"        - Limit the call to 'x' ms warning when 'y' ms are left repeated every 'z' ms)\n"
"          Only 'x' is required, 'y' and 'z' are optional.\n"
"          The following special variables are optional:\n"
"              * LIMIT_PLAYAUDIO_CALLER    yes|no (default yes)\n"
"                                          Play sounds to the caller.\n"
"              * LIMIT_PLAYAUDIO_CALLEE    yes|no\n"
"                                          Play sounds to the callee.\n"
"              * LIMIT_TIMEOUT_FILE        File to play when time is up.\n"
"              * LIMIT_CONNECT_FILE        File to play when call begins.\n"
"              * LIMIT_WARNING_FILE        File to play as warning if 'y' is defined.\n"
"                                          'timeleft' is a special sound macro to auto-say\n"
"                                          the time left and is the default.\n"
"    m[(class)]\n"
"        - provide hold music to the calling party until answered (optionally with the\n"
"          specified class.\n"
"    M(x[^arg])\n"
"        - Executes the Proc (x with ^ delim arg list) upon connect of the call.\n"
"          Also, the Proc can set the PROC_RESULT variable to do the following:\n"
"              * ABORT\n"
"                    - Hangup both legs of the call.\n"
"              * CONGESTION\n"
"                    - Behave as if line congestion was encountered.\n"
"              * BUSY\n"
"                    - Behave as if a busy signal was encountered.\n"
"              * CONTINUE\n"
"                    - Hangup the called party and continue on in the dialplan.\n"
"              * GOTO:<context>^<exten>^<priority>\n"
"                    - Transfer the call.\n"
"    N\n"
"        - modifier for screen/privacy mode. if callerID is present, do not screen the call.\n"
"    n\n"
"        - modifier for screen/privacy mode. No intros are to be saved in the priv-callerintros dir.\n"
"    o\n"
"        - Original (inbound) Caller*ID should be placed on the outbound leg of the call\n"
"          instead of using the destination extension (old style callweaver behavior)\n"
"    P[(x)]\n"
"        - privacy mode, using 'x' as database if provided, or the extension is used if not provided.\n"
"    p\n"
"        - screening mode.  Basically Privacy mode without memory.\n"
"    R\n"
"        - wait for # to be pressed before bridging the call\n"
"    r\n"
"        - indicate ringing to the calling party, pass no audio until answered.\n"
"    S(x)\n"
"        - hangup the call after x seconds AFTER called party picked up\n"
"    t\n"
"        - allow the called user to transfer the calling user by hitting #.\n"
"    T\n"
"        - allow the calling user to transfer the call by hitting #.\n"
"    w\n"
"        - allow the called user to write the conversation to disk via Monitor\n"
"    W\n"
"        - allow the calling user to write the conversation to disk via Monitor\n"
"\n"
"In addition to transferring the call, a call may be parked and then picked\n"
"up by another user.\n"
"\n"
"The optional URL will be sent to the called party if the channel supports it.\n"
"\n"
"If the OUTBOUND_GROUP variable is set, all peer channels created by this\n"
"application will be put into that group (as in SetGroup).\n"
"\n"
"This application sets the following channel variables upon completion:\n"
"      DIALEDTIME    Time from dial to answer\n" 
"      ANSWEREDTIME  Time for actual call\n"
"      DIALSTATUS    The status of the call as a text string, one of:\n"
"                    CHANUNAVAIL, CONGESTION, NOANSWER, BUSY, ANSWER\n"
"                    CANCEL, DONTCALL or TORTURE\n"
"";

/* RetryDial App by Anthony Minessale II <anthmct@yahoo.com> Jan/2005 */
static void *retrydial_app;
static const char retrydial_name[] = "RetryDial";
static const char retrydial_synopsis[] = "Place a call, retrying on failure allowing optional exit extension.";
static const char retrydial_syntax[] = "RetryDial(announce, sleep, loops, Technology/resource[&Technology2/resource2...][, timeout][, options][, URL])";
static const char retrydial_descrip[] =
"Attempt to place a call.  If no channel can be reached, play the file defined by 'announce'\n"
"waiting 'sleep' seconds to retry the call.  If the specified number of attempts matches \n"
"'loops' the call will continue in the dialplan.  If 'loops' is set to 0, the call will retry endlessly.\n\n"
"While waiting, a 1 digit extension may be dialed.  If that extension exists in either\n"
"the context defined in ${EXITCONTEXT} or the current one, The call will transfer\n"
"to that extension immmediately.\n\n"
"All arguments after 'loops' are passed directly to the Dial() application.\n"
"";


#define DIAL_STILLGOING			(1 << 0)
#define DIAL_ALLOWREDIRECT_IN		(1 << 1)
#define DIAL_ALLOWREDIRECT_OUT		(1 << 2)
#define DIAL_ALLOWDISCONNECT_IN		(1 << 3)
#define DIAL_ALLOWDISCONNECT_OUT	(1 << 4)
#define DIAL_RINGBACKONLY		(1 << 5)
#define DIAL_MUSICONHOLD		(1 << 6)
#define DIAL_FORCECALLERID		(1 << 7)
#define DIAL_MONITOR_IN			(1 << 8)
#define DIAL_MONITOR_OUT		(1 << 9)
#define DIAL_GO_ON			(1 << 10)
#define DIAL_HALT_ON_DTMF		(1 << 11)
#define DIAL_PRESERVE_CALLERID		(1 << 12)
#define DIAL_NOFORWARDHTML		(1 << 13)

struct outchan {
	struct cw_channel *chan;
	unsigned int flags;
	int forwards;
	struct outchan *next;
};


static void hanguptree(struct outchan *outgoing, struct cw_channel *exception)
{
	/* Hang up a tree of stuff */
	struct outchan *oo;
	while (outgoing) {
		/* Hangup any existing lines we have open */
		if (outgoing->chan && (outgoing->chan != exception))
			cw_hangup(outgoing->chan);
		oo = outgoing;
		outgoing=outgoing->next;
		free(oo);
	}
}

#define CW_MAX_FORWARDS   8

#define CW_MAX_WATCHERS 256

#define HANDLE_CAUSE(cause, chan) do { \
	switch(cause) { \
	case CW_CAUSE_BUSY: \
		if (chan->cdr) \
			cw_cdr_busy(chan->cdr); \
		numbusy++; \
		break; \
	case CW_CAUSE_CONGESTION: \
		if (chan->cdr) \
			cw_cdr_busy(chan->cdr); \
		numcongestion++; \
		break; \
	case CW_CAUSE_UNREGISTERED: \
		if (chan->cdr) \
			cw_cdr_busy(chan->cdr); \
		numnochan++; \
		break; \
	case CW_CAUSE_NORMAL_CLEARING: \
		break; \
	default: \
		numnochan++; \
		break; \
	} \
} while (0)


static int onedigit_goto(struct cw_channel *chan, const char *context, const char exten)
{
	char rexten[2] = { exten, '\0' };

	if (context) {
		if (!cw_goto_if_exists_n(chan, context, rexten, 1))
			return 1;
	} else {
		if (!cw_goto_if_exists_n(chan, chan->context, rexten, 1))
			return 1;
		else if (!cw_strlen_zero(chan->proc_context)) {
			if (!cw_goto_if_exists_n(chan, chan->proc_context, rexten, 1))
				return 1;
		}
	}
	return 0;
}


static char *get_cid_name(struct cw_dynstr *name, struct cw_channel *chan)
{
	cw_get_hint(NULL, name, chan,
		(!cw_strlen_zero(chan->proc_context) ? chan->proc_context : chan->context),
		(!cw_strlen_zero(chan->proc_exten) ? chan->proc_exten : chan->exten));

	return name->data;
}

static void senddialevent(struct cw_channel *src, struct cw_channel *dst)
{
	cw_manager_event(EVENT_FLAG_CALL, "Dial",
		6,
		cw_msg_tuple("Source",       "%s", src->name),
		cw_msg_tuple("Destination",  "%s", dst->name),
		cw_msg_tuple("CallerID",     "%s", (src->cid.cid_num ? src->cid.cid_num : "<unknown>")),
		cw_msg_tuple("CallerIDName", "%s", (src->cid.cid_name ? src->cid.cid_name : "<unknown>")),
		cw_msg_tuple("SrcUniqueID",  "%s", src->uniqueid),
		cw_msg_tuple("DestUniqueID", "%s", dst->uniqueid)
	);
}

static struct cw_channel *wait_for_answer(struct cw_channel *in, struct outchan *outgoing, int *to, struct cw_flags *peerflags, int *sentringing, char *status, size_t statussize, int busystart, int nochanstart, int congestionstart, int *result)
{
	struct cw_channel *watchers[CW_MAX_WATCHERS];
	struct outchan *o;
	struct cw_frame *f;
	struct cw_channel *peer = NULL;
	struct cw_channel *winner;
	struct cw_var_t *context = NULL;
	int found;
	int numlines;
	int numbusy = busystart;
	int numcongestion = congestionstart;
	int numnochan = nochanstart;
	int prestart = busystart + congestionstart + nochanstart;
	int cause;
	int orig = *to;
	int pos;
	int single;

	CW_UNUSED(statussize);

	single = (outgoing && !outgoing->next && !cw_test_flag(outgoing, DIAL_MUSICONHOLD | DIAL_RINGBACKONLY));
	
	if (single) {
		/* Turn off hold music, etc */
		cw_generator_deactivate(&in->generator);
		/* If we are calling a single channel, make them compatible for in-band tone purpose */
		cw_channel_make_compatible(outgoing->chan, in);
	}
	
	
	while (*to && !peer) {
		o = outgoing;
		found = -1;
		pos = 1;
		numlines = prestart;
		watchers[0] = in;
		while (o) {
			/* Keep track of important channels */
			if (cw_test_flag(o, DIAL_STILLGOING) && o->chan) {
				watchers[pos++] = o->chan;
				found = 1;
			}
			o = o->next;
			numlines++;
		}
		if (found < 0) {
			if (numlines == (numbusy + numcongestion + numnochan)) {
				if (option_verbose > 2)
					cw_verbose( VERBOSE_PREFIX_2 "Everyone is busy/congested at this time (%d lines, %d busy, %d congestion, %d unavailable)\n", numlines, numbusy, numcongestion, numnochan);
				if (numbusy)
					strcpy(status, "BUSY");	
				else if (numcongestion)
					strcpy(status, "CONGESTION");
				else if (numnochan)
					strcpy(status, "CHANUNAVAIL");
			} else {
				if (option_verbose > 2)
					cw_verbose( VERBOSE_PREFIX_2 "No one is available to answer at this time (%d lines, %d busy, %d congestion, %d unavailable)\n", numlines, numbusy, numcongestion, numnochan);
			}
			*to = 0;
			return NULL;
		}
		winner = cw_waitfor_n(watchers, pos, to);
		o = outgoing;
		while (o) {
			if (cw_test_flag(o, DIAL_STILLGOING) && o->chan && (o->chan->_state == CW_STATE_UP)) {
				if (!peer) {
					if (option_verbose > 2)
						cw_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
					peer = o->chan;
					cw_copy_flags(peerflags, o, DIAL_ALLOWREDIRECT_IN|DIAL_ALLOWREDIRECT_OUT|DIAL_ALLOWDISCONNECT_IN|DIAL_ALLOWDISCONNECT_OUT|DIAL_NOFORWARDHTML);
				}
			} else if (o->chan && (o->chan == winner)) {
				if (!cw_strlen_zero(o->chan->call_forward)) {
					char tmpchan[256];
					char *stuff;
					const char *tech;
					cw_copy_string(tmpchan, o->chan->call_forward, sizeof(tmpchan));
					if ((stuff = strchr(tmpchan, '/'))) {
						*stuff = '\0';
						stuff++;
						tech = tmpchan;
					} else {
						snprintf(tmpchan, sizeof(tmpchan), "%s@%s", o->chan->call_forward, o->chan->context);
						stuff = tmpchan;
						tech = "Local";
					}
					/* Before processing channel, go ahead and check for forwarding */
					o->forwards++;
					if (o->forwards < CW_MAX_FORWARDS) {
						if (option_verbose > 2)
							cw_verbose(VERBOSE_PREFIX_3 "Now forwarding %s to '%s/%s' (thanks to %s)\n", in->name, tech, stuff, o->chan->name);
						/* Setup parameters */
						o->chan = cw_request(tech, in->nativeformats, stuff, &cause);
						if (!o->chan)
							cw_log(CW_LOG_NOTICE, "Unable to create local channel for call forward to '%s/%s' (cause = %d)\n", tech, stuff, cause);
					} else {
						if (option_verbose > 2)
							cw_verbose(VERBOSE_PREFIX_3 "Too many forwards from %s\n", o->chan->name);
						cause = CW_CAUSE_CONGESTION;
						o->chan = NULL;
					}
					if (!o->chan) {
						cw_clear_flag(o, DIAL_STILLGOING);	
						HANDLE_CAUSE(cause, in);
					} else {
						if (o->chan->cid.cid_num)
							free(o->chan->cid.cid_num);
						o->chan->cid.cid_num = NULL;
						if (o->chan->cid.cid_name)
							free(o->chan->cid.cid_name);
						o->chan->cid.cid_name = NULL;

						if (cw_test_flag(o, DIAL_FORCECALLERID)) {
							char *newcid = NULL;

							if (!cw_strlen_zero(in->proc_exten))
								newcid = in->proc_exten;
							else
								newcid = in->exten;
							o->chan->cid.cid_num = strdup(newcid);
							cw_copy_string(o->chan->accountcode, winner->accountcode, sizeof(o->chan->accountcode));
							o->chan->cdrflags = winner->cdrflags;
							if (!o->chan->cid.cid_num)
								cw_log(CW_LOG_WARNING, "Out of memory\n");
						} else {
							if (in->cid.cid_num) {
								o->chan->cid.cid_num = strdup(in->cid.cid_num);
								if (!o->chan->cid.cid_num)
									cw_log(CW_LOG_WARNING, "Out of memory\n");	
							}
							if (in->cid.cid_name) {
								o->chan->cid.cid_name = strdup(in->cid.cid_name);
								if (!o->chan->cid.cid_name)
									cw_log(CW_LOG_WARNING, "Out of memory\n");	
							}
							cw_copy_string(o->chan->accountcode, in->accountcode, sizeof(o->chan->accountcode));
							o->chan->cdrflags = in->cdrflags;
						}

						if (in->cid.cid_ani) {
							if (o->chan->cid.cid_ani)
								free(o->chan->cid.cid_ani);
								o->chan->cid.cid_ani = strdup(in->cid.cid_ani);
								if (!o->chan->cid.cid_ani)
									cw_log(CW_LOG_WARNING, "Out of memory\n");
						}
						if (o->chan->cid.cid_rdnis) 
							free(o->chan->cid.cid_rdnis);
						if (!cw_strlen_zero(in->proc_exten))
							o->chan->cid.cid_rdnis = strdup(in->proc_exten);
						else
							o->chan->cid.cid_rdnis = strdup(in->exten);
						if (cw_call(o->chan, tmpchan)) {
							cw_log(CW_LOG_NOTICE, "Failed to dial on local channel for call forward to '%s'\n", tmpchan);
							cw_clear_flag(o, DIAL_STILLGOING);	
							cw_hangup(o->chan);
							o->chan = NULL;
							numnochan++;
						} else {
							senddialevent(in, o->chan);
							/* After calling, set callerid to extension */
							if (!cw_test_flag(peerflags, DIAL_PRESERVE_CALLERID)) {
								struct cw_dynstr ds = CW_DYNSTR_INIT;

								cw_set_callerid(o->chan, cw_strlen_zero(in->proc_exten) ? in->exten : in->proc_exten, get_cid_name(&ds, in), NULL);
								cw_dynstr_free(&ds);
							}
						}
					}
					/* Hangup the original channel now, in case we needed it */
					cw_hangup(winner);
					continue;
				}
				f = cw_read(winner);
				if (f) {
					if (f->frametype == CW_FRAME_CONTROL) {
						switch(f->subclass) {
						case CW_CONTROL_ANSWER:
							/* This is our guy if someone answered. */
							if (!peer) {
								if (option_verbose > 2)
									cw_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
								peer = o->chan;
								cw_copy_flags(peerflags, o, DIAL_ALLOWREDIRECT_IN|DIAL_ALLOWREDIRECT_OUT|DIAL_ALLOWDISCONNECT_IN|DIAL_ALLOWDISCONNECT_OUT|DIAL_NOFORWARDHTML);
							}
							/* If call has been answered, then the eventual hangup is likely to be normal hangup */
							in->hangupcause = CW_CAUSE_NORMAL_CLEARING;
							o->chan->hangupcause = CW_CAUSE_NORMAL_CLEARING;
							break;
						case CW_CONTROL_BUSY:
							if (option_verbose > 2)
								cw_verbose( VERBOSE_PREFIX_3 "%s is busy\n", o->chan->name);
							in->hangupcause = o->chan->hangupcause;
							cw_hangup(o->chan);
							o->chan = NULL;
							cw_clear_flag(o, DIAL_STILLGOING);	
							HANDLE_CAUSE(CW_CAUSE_BUSY, in);
							break;
						case CW_CONTROL_CONGESTION:
							if (option_verbose > 2)
								cw_verbose( VERBOSE_PREFIX_3 "%s is circuit-busy\n", o->chan->name);
							in->hangupcause = o->chan->hangupcause;
							cw_hangup(o->chan);
							o->chan = NULL;
							cw_clear_flag(o, DIAL_STILLGOING);
							HANDLE_CAUSE(CW_CAUSE_CONGESTION, in);
							break;
						case CW_CONTROL_RINGING:
							if (option_verbose > 2)
								cw_verbose( VERBOSE_PREFIX_3 "%s is ringing\n", o->chan->name);
							if (!(*sentringing) && !cw_test_flag(outgoing, DIAL_MUSICONHOLD)) {
								cw_indicate(in, CW_CONTROL_RINGING);
								(*sentringing)++;
							}
							break;
						case CW_CONTROL_PROGRESS:
							if (option_verbose > 2)
								cw_verbose ( VERBOSE_PREFIX_3 "%s is making progress passing it to %s\n", o->chan->name,in->name);
							if (!cw_test_flag(outgoing, DIAL_RINGBACKONLY))
								cw_indicate(in, CW_CONTROL_PROGRESS);
							break;
						case CW_CONTROL_VIDUPDATE:
							if (option_verbose > 2)
								cw_verbose ( VERBOSE_PREFIX_3 "%s requested a video update, passing it to %s\n", o->chan->name,in->name);
							cw_indicate(in, CW_CONTROL_VIDUPDATE);
							break;
						case CW_CONTROL_PROCEEDING:
							if (option_verbose > 2)
								cw_verbose ( VERBOSE_PREFIX_3 "%s is proceeding passing it to %s\n", o->chan->name,in->name);
							if (!cw_test_flag(outgoing, DIAL_RINGBACKONLY))
								cw_indicate(in, CW_CONTROL_PROCEEDING);
							break;
						case CW_CONTROL_HOLD:
							if (option_verbose > 2)
								cw_verbose(VERBOSE_PREFIX_3 "Call on %s placed on hold\n", o->chan->name);
							cw_indicate(in, CW_CONTROL_HOLD);
							break;
						case CW_CONTROL_UNHOLD:
							if (option_verbose > 2)
								cw_verbose(VERBOSE_PREFIX_3 "Call on %s left from hold\n", o->chan->name);
							cw_indicate(in, CW_CONTROL_UNHOLD);
							break;
						case CW_CONTROL_OFFHOOK:
						case CW_CONTROL_FLASH:
							/* Ignore going off hook and flash */
							break;
						case -1:
							if (!cw_test_flag(outgoing, DIAL_RINGBACKONLY | DIAL_MUSICONHOLD)) {
								if (option_verbose > 2)
									cw_verbose( VERBOSE_PREFIX_3 "%s stopped sounds\n", o->chan->name);
								cw_indicate(in, -1);
								(*sentringing) = 0;
							}
							break;
						default:
							cw_log(CW_LOG_DEBUG, "Dunno what to do with control type %d\n", f->subclass);
						}
					} else if (single && (f->frametype == CW_FRAME_VOICE) && 
								!(cw_test_flag(outgoing, DIAL_RINGBACKONLY|DIAL_MUSICONHOLD))) {
						if (cw_write(in, &f)) 
							cw_log(CW_LOG_DEBUG, "Unable to forward frame\n");
					} else if (single && (f->frametype == CW_FRAME_IMAGE) && 
								!(cw_test_flag(outgoing, DIAL_RINGBACKONLY|DIAL_MUSICONHOLD))) {
						if (cw_write(in, &f))
							cw_log(CW_LOG_DEBUG, "Unable to forward image\n");
					} else if (single && (f->frametype == CW_FRAME_TEXT) && 
								!(cw_test_flag(outgoing, DIAL_RINGBACKONLY|DIAL_MUSICONHOLD))) {
						if (cw_write(in, &f))
							cw_log(CW_LOG_DEBUG, "Unable to text\n");
					} else if (single && (f->frametype == CW_FRAME_HTML) && !cw_test_flag(outgoing, DIAL_NOFORWARDHTML))
						cw_channel_sendhtml(in, f->subclass, f->data, f->datalen);

					cw_fr_free(f);
				} else {
					in->hangupcause = o->chan->hangupcause;
					cw_hangup(o->chan);
					o->chan = NULL;
					cw_clear_flag(o, DIAL_STILLGOING);
					HANDLE_CAUSE(in->hangupcause, in);
				}
			}
			o = o->next;
		}
		if (winner == in) {
			f = cw_read(in);
#if 0
			if (f && (f->frametype != CW_FRAME_VOICE))
				printf("Frame type: %d, %d\n", f->frametype, f->subclass);
			else if (!f || (f->frametype != CW_FRAME_VOICE))
				printf("Hangup received on %s\n", in->name);
#endif
			if (!f || ((f->frametype == CW_FRAME_CONTROL) && (f->subclass == CW_CONTROL_HANGUP))) {
				/* Got hung up */
				*to=-1;
				strcpy(status, "CANCEL");
				if (f)
					cw_fr_free(f);
				return NULL;
			}

			if (f && (f->frametype == CW_FRAME_DTMF)) {
				if (cw_test_flag(peerflags, DIAL_HALT_ON_DTMF)) {
					context = pbx_builtin_getvar_helper(in, CW_KEYWORD_EXITCONTEXT, "EXITCONTEXT");
					found = onedigit_goto(in, (context ? context->value : NULL), (char) f->subclass);
					if (context)
						cw_object_put(context);
					if (found) {
						if (option_verbose > 3)
							cw_verbose(VERBOSE_PREFIX_3 "User hit %c to disconnect call.\n", f->subclass);
						*to=0;
						*result = f->subclass;
						strcpy(status, "CANCEL");
						cw_fr_free(f);
						return NULL;
					}
				}

				if (cw_test_flag(peerflags, DIAL_ALLOWDISCONNECT_OUT) && 
						  (f->subclass == '*')) { /* hmm it it not guarenteed to be '*' anymore. */
					if (option_verbose > 3)
						cw_verbose(VERBOSE_PREFIX_3 "User hit %c to disconnect call.\n", f->subclass);
					*to=0;
					strcpy(status, "CANCEL");
					cw_fr_free(f);
					return NULL;
				}
			}

			/* Forward HTML stuff */
			if (single && f && (f->frametype == CW_FRAME_HTML) && !cw_test_flag(outgoing, DIAL_NOFORWARDHTML)) 
				cw_channel_sendhtml(outgoing->chan, f->subclass, f->data, f->datalen);
			

			if (single && ((f->frametype == CW_FRAME_VOICE) || (f->frametype == CW_FRAME_DTMF)))  {
				if (cw_write(outgoing->chan, &f))
					cw_log(CW_LOG_WARNING, "Unable to forward voice\n");
			}
			if (single && (f->frametype == CW_FRAME_CONTROL) && (f->subclass == CW_CONTROL_VIDUPDATE)) {
				if (option_verbose > 2)
					cw_verbose ( VERBOSE_PREFIX_3 "%s requested a video update, passing it to %s\n", in->name,outgoing->chan->name);
				cw_indicate(outgoing->chan, CW_CONTROL_VIDUPDATE);
			}
			cw_fr_free(f);
		}
		if (!*to && (option_verbose > 2))
			cw_verbose( VERBOSE_PREFIX_3 "Nobody picked up in %d ms\n", orig);
	}

	return peer;
	
}


static int dial_exec_full(struct cw_channel *chan, int argc, char **argv, struct cw_flags *peerflags)
{
	struct cw_dynargs args = CW_DYNARRAY_INIT;
	struct cw_var_t *tmpvar;
	int res=-1;
	struct localuser *u;
	char **peers, *timeout, *tech, *number, *cur;
	char *privdb = NULL;
	char privcid[256];
	char privintro[1024];
	char *announcemsg = NULL;
	int npeers;
	int inputkey;
	struct outchan *outgoing=NULL, *tmp;
	struct cw_channel *peer;
	int to;
	struct {
		unsigned int newsyntax:1;
		unsigned int privacy:1;
		unsigned int screen:1;
		unsigned int no_save_intros:1;
		unsigned int no_screen_callerid:1;
		unsigned int resetcdr:1;
		unsigned int waitpound:1;
	} opt;
	unsigned int flags = 0;
	int numbusy = 0;
	int numcongestion = 0;
	int numnochan = 0;
	int cause;
	char numsubst[CW_MAX_EXTENSION];
	char restofit[CW_MAX_EXTENSION];
	char *newnum;
	char *l;
	char *url=NULL; /* JDG */
	int privdb_val=0;
	unsigned int calldurationlimit=0;
	time_t now;
	struct cw_bridge_config config;
	long timelimit = 0;
	long play_warning = 0;
	long warning_freq=0;
	char *dtmfcalled=NULL, *dtmfcalling=NULL;
	char *mac = NULL, *proc_name = NULL;
	char status[256];
	char toast[80];
	int playargs=0, sentringing=0, moh=0;
	char *mohclass = NULL;
	struct cw_var_t *outbound_group;
	char *proc_transfer_dest = NULL;
	int digit = 0, result = 0;
	time_t start_time, answer_time, end_time;
	char *dblgoto = NULL;
	char *jumpdst = NULL;

	peers = NULL;

	if (argv > 0) {
		/* If the first argument starts with '{' we have a channel group. The last
		 * member is the last argument that ends with '}' and all the arguments in
		 * between are the already separated channels that are members of this group.
		 * Note that by making the end marker the _last_ argument ending with '}'
		 * we avoid issues with a possibly channel spec expanding to something ending
		 * with '}' but we also require that none of the timeout, options or url
		 * optional trailing arguments can end in '}'. This is currently the case.
		 */
		if ((opt.newsyntax = (argv[0][0] == '{'))) {
			int i;

			for (i = argc - 1; i >= 0; i--) {
				int j = strlen(argv[i]) - 1;
				if (argv[i][j] == '}') {
					argv[i][j--] = '\0';
					while (j && isspace(argv[i][j]))
						argv[i][j--] = '\0';

					argv[0]++;
					while (argv[0][0] && isspace(argv[0][0]))
						argv[0]++;

					peers = argv;
					npeers = i + 1;
					argc -= (i + 1);
					argv += (i + 1);
					break;
				}
			}
		}

		if (!peers) {
			/* Old style. There is one argument. If there is a channel group
			 * all the channels are packed into this one argument with '&'
			 * separating them. In this case there is no way to protect against
			 * an expansion that contains '&' itself.
			 */
			char *p = argv[0];
			args.used = 0;
			do {
				cw_dynargs_need(&args, 1);
				args.data[args.used++] = p;
				if ((p = strchr(p, '&')))
					*(p++) = '\0';
			} while (p);

			peers = args.data;
			npeers = args.used;
			argc--;
			argv++;
		}
	}

	if (!peers)
		return cw_function_syntax(dial_syntax);

	LOCAL_USER_ADD(u);

	timeout = NULL;
	if (argc)
		timeout = *(argv++);

	memset(&opt, 0, sizeof(opt));
	memset(&config, 0, sizeof(config));

	while (argv[0] && argv[0][0]) {
		char *end;

		if (argv[0][1] == '(') {
			if (opt.newsyntax) {
				if (*(end = argv[0] + strlen(argv[0]) - 1) != ')')
					end = NULL;
			} else
				end = strchr(argv[0], ')');

			if (end)
				*end = '\0';
			else {
				cw_log(CW_LOG_ERROR, "Missing ')' after %s\n", argv[0]);
				goto err;
			}
		} else
			end = &argv[0][0];

		if (argv[0][1] == '(') {
			switch (argv[0][0]) {
				case 'A':
					announcemsg = &argv[0][2];
					break;

				case 'D':
					dtmfcalled = &argv[0][2];
					if ((dtmfcalling = strchr(&argv[0][2], ':')))
						*(dtmfcalling++) = '\0';
					break;

				case 'G':
					dblgoto = &argv[0][2];
					break;

				case 'H':
					flags |= DIAL_ALLOWDISCONNECT_OUT;
					break;

				case 'J':
					jumpdst = &argv[0][2];
					break;

				case 'L':
					if ((tmpvar = pbx_builtin_getvar_helper(chan, CW_KEYWORD_LIMIT_PLAYAUDIO_CALLER, "LIMIT_PLAYAUDIO_CALLER"))) {
						if (cw_true(tmpvar->value))
							cw_set_flag(&config.features_caller, CW_FEATURE_PLAY_WARNING);
						cw_object_put(tmpvar);
					} else
						cw_set_flag(&config.features_caller, CW_FEATURE_PLAY_WARNING);

					if ((tmpvar = pbx_builtin_getvar_helper(chan, CW_KEYWORD_LIMIT_PLAYAUDIO_CALLEE, "LIMIT_PLAYAUDIO_CALLEE"))) {
						if (cw_true(tmpvar->value))
							cw_set_flag(&config.features_callee, CW_FEATURE_PLAY_WARNING);
						cw_object_put(tmpvar);
					}

					if (!cw_test_flag(&config.features_caller, CW_FEATURE_PLAY_WARNING)
					&& !cw_test_flag(&config.features_callee, CW_FEATURE_PLAY_WARNING))
						cw_set_flag(&config.features_caller, CW_FEATURE_PLAY_WARNING);

					if ((tmpvar = pbx_builtin_getvar_helper(chan, CW_KEYWORD_LIMIT_CONNECT_FILE, "LIMIT_CONNECT_FILE"))) {
						config.start_sound = strdup(tmpvar->value);
						cw_object_put(tmpvar);
					} else
						config.start_sound = NULL;

					if ((tmpvar = pbx_builtin_getvar_helper(chan, CW_KEYWORD_LIMIT_TIMEOUT_FILE, "LIMIT_TIMEOUT_FILE"))) {
						config.end_sound = strdup(tmpvar->value);
						cw_object_put(tmpvar);
					}

					if ((tmpvar = pbx_builtin_getvar_helper(chan, CW_KEYWORD_LIMIT_WARNING_FILE, "LIMIT_WARNING_FILE"))) {
						config.warning_sound = strdup(tmpvar->value);
						cw_object_put(tmpvar);
					} else
						config.warning_sound = strdup("timeleft");

					playargs = sscanf(&argv[0][2], "%ld:%ld:%ld", &timelimit, &play_warning, &warning_freq);

					if (!timelimit) {
						timelimit = play_warning = warning_freq = 0;
						cw_clear_flag(&config.features_caller, CW_FEATURE_PLAY_WARNING);
						cw_clear_flag(&config.features_callee, CW_FEATURE_PLAY_WARNING);
						free(config.warning_sound);
						config.warning_sound = NULL;
					}
					/* undo effect of S(x) in case they are both used */
					calldurationlimit = 0;
					/* more efficient do it like S(x) does since no advanced opts*/
					if (!play_warning && !config.start_sound && !config.end_sound && timelimit) {
						calldurationlimit = timelimit/1000;
						timelimit = play_warning = warning_freq = 0;
						cw_clear_flag(&config.features_caller, CW_FEATURE_PLAY_WARNING);
						cw_clear_flag(&config.features_callee, CW_FEATURE_PLAY_WARNING);
						if (option_verbose > 2)
							cw_verbose(VERBOSE_PREFIX_3 "Setting call duration limit to %u seconds.\n", calldurationlimit);
					} else if (option_verbose > 2) {
						cw_verbose(VERBOSE_PREFIX_3 "Limit Data for this call:\n");
						cw_verbose(VERBOSE_PREFIX_3 "- timelimit     = %ld\n", timelimit);
						cw_verbose(VERBOSE_PREFIX_3 "- play_warning  = %ld\n", play_warning);
						cw_verbose(VERBOSE_PREFIX_3 "- play_to_caller= %s\n", cw_test_flag(&config.features_caller, CW_FEATURE_PLAY_WARNING) ? "yes" : "no");
						cw_verbose(VERBOSE_PREFIX_3 "- play_to_callee= %s\n", cw_test_flag(&config.features_callee, CW_FEATURE_PLAY_WARNING) ? "yes" : "no");
						cw_verbose(VERBOSE_PREFIX_3 "- warning_freq  = %ld\n", warning_freq);
						cw_verbose(VERBOSE_PREFIX_3 "- start_sound   = %s\n", config.start_sound ? config.start_sound : "UNDEF");
						cw_verbose(VERBOSE_PREFIX_3 "- warning_sound = %s\n", config.warning_sound ? config.warning_sound : "UNDEF");
						cw_verbose(VERBOSE_PREFIX_3 "- end_sound     = %s\n", config.end_sound ? config.end_sound : "UNDEF");
					}
					break;

				case 'M':
					proc_name = &argv[0][2];
					break;

				case 'm':
					mohclass = &argv[0][2];
					break;

				case 'P':
					opt.privacy = 1;
					privdb = &argv[0][2];
					break;

				case 'S':
					calldurationlimit = atoi(&argv[0][2]);
					if (option_verbose > 2)
						cw_verbose(VERBOSE_PREFIX_3 "Setting call duration limit to %u seconds.\n", calldurationlimit);
					break;

				default:
					cw_log(CW_LOG_ERROR, "Unknown option %s)\n", argv[0]);
					goto err;
			}
		} else {
			switch (argv[0][0]) {
				case 'C':
					opt.resetcdr = 1;
					break;

				case 'd':
					cw_set_flag(peerflags, DIAL_HALT_ON_DTMF);
					break;

				case 'f':
					flags |= DIAL_FORCECALLERID;
					break;

				case 'g':
					cw_set_flag(peerflags, DIAL_GO_ON);
					break;

				case 'H':
					cw_set_flag(peerflags, DIAL_ALLOWDISCONNECT_OUT);
					break;

				case 'h':
					if (!strcmp(&argv[0][1], "ttp")) {
						flags = DIAL_NOFORWARDHTML;
						url = argv[0];
						end = argv[0] + strlen(argv[0]);
					} else {
						flags |= DIAL_ALLOWDISCONNECT_IN;
						cw_set_flag(peerflags, DIAL_ALLOWDISCONNECT_IN);
					}
					break;

				case 'm':
					flags |= DIAL_MUSICONHOLD;
					break;

				case 'N':
					opt.no_screen_callerid = 1;
					break;

				case 'n':
					opt.no_save_intros = 1;
					break;

				case 'o':
					cw_set_flag(peerflags, DIAL_PRESERVE_CALLERID);
					break;

				case 'P':
					opt.privacy = 1;
					break;

				case 'p':
					opt.screen = 1;
					break;

				case 'r':
					flags |= DIAL_RINGBACKONLY;
					break;

				case 'R':
					opt.waitpound = 1;
					break;

				case 'T':
					flags |= DIAL_ALLOWREDIRECT_OUT;
					break;

				case 't':
					flags |= DIAL_ALLOWREDIRECT_IN;
					break;

				case 'W':
					cw_set_flag(peerflags, DIAL_MONITOR_OUT);
					break;

				case 'w':
					cw_set_flag(peerflags, DIAL_MONITOR_IN);
					break;

				case 'A':
				case 'D':
				case 'G':
				case 'J':
				case 'L':
				case 'M':
				case 'S':
					cw_log(CW_LOG_ERROR, "Missing '(' after %c\n", argv[0][0]);
					goto err;

				default:
					cw_log(CW_LOG_ERROR, "Unknown option %c at %s\n", argv[0][0], argv[0]);
					goto err;
			}
		}

		if (end)
			argv[0] = end + 1;
		if (!argv[0][0])
			argv++;
	}

	if (option_debug) {
		if (url)
			cw_log(CW_LOG_DEBUG, "DIAL WITH URL=%s\n", url);
		else
			cw_log(CW_LOG_DEBUG, "SIMPLE DIAL (NO URL)\n");
	}

	if (opt.resetcdr && chan->cdr)
		cw_cdr_reset(chan->cdr, 0);
	if (opt.privacy && cw_strlen_zero(privdb)) {
		/* If privdb is not specified and we are using privacy, use extension */
		privdb = chan->exten;
	}
	if (opt.privacy || opt.screen) {
		char callerid[60];

		l = chan->cid.cid_num;
		if (!cw_strlen_zero(l)) {
			cw_shrink_phone_number(l);
			if (opt.privacy) {
				if (option_verbose > 2)
					cw_verbose( VERBOSE_PREFIX_3  "Privacy DB is '%s', privacy is %d, clid is '%s'\n", privdb, opt.privacy, l);
				privdb_val = cw_privacy_check(privdb, l);
			}
			else {
				if (option_verbose > 2)
					cw_verbose( VERBOSE_PREFIX_3  "Privacy Screening, clid is '%s'\n", l);
				privdb_val = CW_PRIVACY_UNKNOWN;
			}
		} else {
			char *tnam, *tn2;

			tnam = cw_strdupa(chan->name);
			/* clean the channel name so slashes don't try to end up in disk file name */
			for(tn2 = tnam; *tn2; tn2++) {
				if( *tn2=='/')
					*tn2 = '=';  /* any other chars to be afraid of? */
			}
			if (option_verbose > 2)
				cw_verbose( VERBOSE_PREFIX_3  "Privacy-- callerid is empty\n");

			snprintf(callerid, sizeof(callerid), "NOCALLERID_%s%s", chan->exten, tnam);
			l = callerid;
			privdb_val = CW_PRIVACY_UNKNOWN;
		}
		
		cw_copy_string(privcid,l,sizeof(privcid));

		if( strncmp(privcid,"NOCALLERID",10) != 0 && opt.no_screen_callerid ) {
			if (option_verbose > 2)
				cw_verbose( VERBOSE_PREFIX_3  "CallerID set (%s); N option set; Screening should be off\n", privcid);
			privdb_val = CW_PRIVACY_ALLOW;
		}
		else if (opt.no_screen_callerid && strncmp(privcid,"NOCALLERID",10) == 0) {
			if (option_verbose > 2)
				cw_verbose( VERBOSE_PREFIX_3  "CallerID blank; N option set; Screening should happen; dbval is %d\n", privdb_val);
		}
		
		if( privdb_val == CW_PRIVACY_DENY ) {
			cw_verbose( VERBOSE_PREFIX_3  "Privacy DB reports PRIVACY_DENY for this callerid. Dial reports unavailable\n");
			res=0;
			goto out;
		}
		else if( privdb_val == CW_PRIVACY_UNKNOWN ) {
			/* Get the user's intro, store it in priv-callerintros/$CID, 
			   unless it is already there-- this should be done before the 
			   call is actually dialed  */

			/* make sure the priv-callerintros dir exists? */

			snprintf(privintro,sizeof(privintro),"priv-callerintros/%s", privcid);
			if( cw_fileexists(privintro,NULL,NULL ) && strncmp(privcid,"NOCALLERID",10) != 0) {
				/* the DELUX version of this code would allow this caller the
				   option to hear and retape their previously recorded intro.
				*/
			}
			else {
				int duration; /* for feedback from play_and_wait */
				/* the file doesn't exist yet. Let the caller submit his
				   vocal intro for posterity */
				/* priv-recordintro script:

				   "At the tone, please say your name:"

				*/

				res = cw_play_and_record(chan, "priv-recordintro", privintro, 4, "gsm", &duration, 128, 2000, 0);  /* NOTE: I've reduced the total time to */
															/* 4 sec don't think we'll need a lock removed, we 
															   took care of conflicts by naming the privintro file */
				if (res == -1) {
					/* Delete the file regardless since they hung up during recording */
                                        cw_filedelete(privintro, NULL);
                                        if( cw_fileexists(privintro,NULL,NULL ) )
                                                cw_log(CW_LOG_NOTICE,"privacy: ast_filedelete didn't do its job on %s\n", privintro);
                                        else if (option_verbose > 2)
                                                cw_verbose( VERBOSE_PREFIX_3 "Successfully deleted %s intro file\n", privintro);
					goto out;
				}
															/* don't think we'll need a lock removed, we took care of
															   conflicts by naming the privintro file */
			}
		}
	}

	/* If a channel group has been specified, get it for use when we create peer channels */
	outbound_group = pbx_builtin_getvar_helper(chan, CW_KEYWORD_OUTBOUND_GROUP, "OUTBOUND_GROUP");

	while (npeers) {
		cur = peers[0];
		peers++;
		npeers--;

		tech = cur;
		number = strchr(tech, '/');
		if (!number) {
			cw_log(CW_LOG_WARNING, "Dial argument takes format (technology1/[device:]number1&technology2/[device:]number2...,optional timeout)\n");
			goto out;
		}
		*number = '\0';
		number++;

		if (!(tmp = calloc(1, sizeof(struct outchan)))) {
			cw_log(CW_LOG_ERROR, "Out of memory!\n");
			goto out;
		}

		tmp->flags = flags;

		cw_copy_string(numsubst, number, sizeof(numsubst));
		/* If we're dialing by extension, look at the extension to know what to dial */
		if ((newnum = strstr(numsubst, "BYEXTENSION"))) {
			/* strlen("BYEXTENSION") == 11 */
			cw_copy_string(restofit, newnum + 11, sizeof(restofit));
			snprintf(newnum, sizeof(numsubst) - (newnum - numsubst), "%s%s", chan->exten,restofit);
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "Dialing by extension %s\n", numsubst);
		}
		/* Request the peer */
		tmp->chan = cw_request(tech, chan->nativeformats, numsubst, &cause);
		if (!tmp->chan) {
			/* If we can't, just go on to the next call */
			cw_log(CW_LOG_NOTICE, "Unable to create channel of type '%s/%s' (cause %d - %s)\n", tech, numsubst, cause, cw_cause2str(cause));
			free(tmp);
			HANDLE_CAUSE(cause, chan);
			if (!npeers)
				chan->hangupcause = cause;
			continue;
		}
		pbx_builtin_setvar_helper(tmp->chan, "DIALEDPEERNUMBER", numsubst);
		if (!cw_strlen_zero(tmp->chan->call_forward)) {
			char tmpchan[256];
			char *stuff;
			const char *fwdtech;
			cw_copy_string(tmpchan, tmp->chan->call_forward, sizeof(tmpchan));
			if ((stuff = strchr(tmpchan, '/'))) {
				*stuff = '\0';
				stuff++;
				fwdtech = tmpchan;
			} else {
				snprintf(tmpchan, sizeof(tmpchan), "%s@%s", tmp->chan->call_forward, tmp->chan->context);
				stuff = tmpchan;
				fwdtech = "Local";
			}
			tmp->forwards++;
			if (tmp->forwards < CW_MAX_FORWARDS) {
				if (option_verbose > 2)
					cw_verbose(VERBOSE_PREFIX_3 "Now forwarding %s to '%s/%s' (thanks to %s)\n", chan->name, fwdtech, stuff, tmp->chan->name);
				cw_hangup(tmp->chan);
				/* Setup parameters */
				tmp->chan = cw_request(fwdtech, chan->nativeformats, stuff, &cause);
				if (!tmp->chan)
					cw_log(CW_LOG_NOTICE, "Unable to create local channel for call forward to '%s/%s' (cause = %d)\n", fwdtech, stuff, cause);
			} else {
				if (option_verbose > 2)
					cw_verbose(VERBOSE_PREFIX_3 "Too many forwards from %s\n", tmp->chan->name);
				cw_hangup(tmp->chan);
				tmp->chan = NULL;
				cause = CW_CAUSE_CONGESTION;
			}
			if (!tmp->chan) {
				free(tmp);
				HANDLE_CAUSE(cause, chan);
				continue;
			}
		}

		/* Inherit specially named variables from parent channel */
		cw_var_inherit(&tmp->chan->vars, &chan->vars);

		tmp->chan->appl = "AppDial (Outgoing Line)";
		tmp->chan->whentohangup = 0;
		if (tmp->chan->cid.cid_num)
			free(tmp->chan->cid.cid_num);
		tmp->chan->cid.cid_num = NULL;
		if (tmp->chan->cid.cid_name)
			free(tmp->chan->cid.cid_name);
		tmp->chan->cid.cid_name = NULL;
		if (tmp->chan->cid.cid_ani)
			free(tmp->chan->cid.cid_ani);
		tmp->chan->cid.cid_ani = NULL;

		if (chan->cid.cid_num) 
			tmp->chan->cid.cid_num = strdup(chan->cid.cid_num);
		if (chan->cid.cid_name) 
			tmp->chan->cid.cid_name = strdup(chan->cid.cid_name);
		if (chan->cid.cid_ani) 
			tmp->chan->cid.cid_ani = strdup(chan->cid.cid_ani);
		
		/* Copy language from incoming to outgoing */
		cw_copy_string(tmp->chan->language, chan->language, sizeof(tmp->chan->language));
		cw_copy_string(tmp->chan->accountcode, chan->accountcode, sizeof(tmp->chan->accountcode));
		tmp->chan->cdrflags = chan->cdrflags;
		if (cw_strlen_zero(tmp->chan->musicclass))
			cw_copy_string(tmp->chan->musicclass, chan->musicclass, sizeof(tmp->chan->musicclass));
		if (chan->cid.cid_rdnis)
			tmp->chan->cid.cid_rdnis = strdup(chan->cid.cid_rdnis);
		/* Pass callingpres setting */
		tmp->chan->cid.cid_pres = chan->cid.cid_pres;
		/* Pass type of number */
		tmp->chan->cid.cid_ton = chan->cid.cid_ton;
		/* Pass type of tns */
		tmp->chan->cid.cid_tns = chan->cid.cid_tns;
		/* Presense of ADSI CPE on outgoing channel follows ours */
		tmp->chan->adsicpe = chan->adsicpe;
		/* Pass the transfer capability */
		tmp->chan->transfercapability = chan->transfercapability;

		/* If we have an outbound group, set this peer channel to it */
		if (outbound_group)
			cw_app_group_set_channel(tmp->chan, outbound_group->value);

		res = cw_call(tmp->chan, numsubst);

		/* Save the info in cdr's that we called them */
		if (chan->cdr)
			cw_cdr_setdestchan(chan->cdr, tmp->chan->name);

		/* check the results of cw_call */
		if (res) {
			/* Again, keep going even if there's an error */
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "CW call on peer returned %d\n", res);
			if (option_verbose > 2)
				cw_verbose(VERBOSE_PREFIX_3 "Couldn't call %s\n", numsubst);
			cw_hangup(tmp->chan);
			free(tmp);
			continue;
		} else {
			senddialevent(chan, tmp->chan);
			if (option_verbose > 2)
				cw_verbose(VERBOSE_PREFIX_3 "Called %s\n", numsubst);
			if (!cw_test_flag(peerflags, DIAL_PRESERVE_CALLERID)) {
				struct cw_dynstr ds = CW_DYNSTR_INIT;

				cw_set_callerid(tmp->chan, cw_strlen_zero(chan->proc_exten) ? chan->exten : chan->proc_exten, get_cid_name(&ds, chan), NULL);
				cw_dynstr_free(&ds);
			}
		}
		/* Put them in the list of outgoing thingies...  We're ready now. 
		   XXX If we're forcibly removed, these outgoing calls won't get
		   hung up XXX */
		cw_set_flag(tmp, DIAL_STILLGOING);
		tmp->next = outgoing;
		outgoing = tmp;
		/* If this line is up, don't try anybody else */
		if (outgoing->chan->_state == CW_STATE_UP)
			break;
	}

	if (outbound_group)
		cw_object_put(outbound_group);
	
	if (!cw_strlen_zero(timeout)) {
		to = atoi(timeout);
		if (to > 0)
			to *= 1000;
		else
			cw_log(CW_LOG_WARNING, "Invalid timeout specified: '%s'\n", timeout);
	} else
		to = -1;

	if (outgoing) {
		/* Our status will at least be NOANSWER */
		strcpy(status, "NOANSWER");
		if (cw_test_flag(outgoing, DIAL_MUSICONHOLD)) {
			moh=1;
			cw_moh_start(chan, mohclass);
		} else if (cw_test_flag(outgoing, DIAL_RINGBACKONLY)) {
			cw_indicate(chan, CW_CONTROL_RINGING);
			sentringing++;
		}
	} else
		strcpy(status, "CHANUNAVAIL");

	time(&start_time);
	peer = wait_for_answer(chan, outgoing, &to, peerflags, &sentringing, status, sizeof(status), numbusy, numnochan, numcongestion, &result);
	
	if (!peer) {
		if (result) {
			res = result;
		} else if (to) 
			/* Musta gotten hung up */
			res = -1;
		else 
		 	/* Nobody answered, next please? */
			res = 0;
		
		goto out;
	}
	if (peer) {
		time(&answer_time);
#ifdef OSP_SUPPORT
		/* Once call is answered, ditch the OSP Handle */
		pbx_builtin_setvar_helper(chan, "_OSPHANDLE", "");
#endif
		strcpy(status, "ANSWER");
		/* Ah ha!  Someone answered within the desired timeframe.  Of course after this
		   we will always return with -1 so that it is hung up properly after the 
		   conversation.  */
		hanguptree(outgoing, peer);
		outgoing = NULL;
		/* If appropriate, log that we have a destination channel */
		if (chan->cdr)
			cw_cdr_setdestchan(chan->cdr, peer->name);
		if (peer->name)
			pbx_builtin_setvar_helper(chan, "DIALEDPEERNAME", peer->name);

		if ((tmpvar = pbx_builtin_getvar_helper(peer, CW_KEYWORD_DIALEDPEERNUMBER, "DIALEDPEERNUMBER"))) {
			cw_registry_add(&chan->vars, tmpvar->hash, &tmpvar->obj);
			cw_object_put(tmpvar);
		} else
			pbx_builtin_setvar_helper(chan, "DIALEDPEERNUMBER", numsubst);

 		if (!cw_strlen_zero(url) && cw_channel_supports_html(peer) ) {
 			cw_log(CW_LOG_DEBUG, "app_dial: sendurl=%s.\n", url);
 			cw_channel_sendurl( peer, url );
 		}
		if (opt.privacy || opt.screen) {
			int res2;
			int loopcount = 0;
			if( privdb_val == CW_PRIVACY_UNKNOWN ) {

				/* Get the user's intro, store it in priv-callerintros/$CID, 
				   unless it is already there-- this should be done before the 
				   call is actually dialed  */

				/* all ring indications and moh for the caller has been halted as soon as the 
				   target extension was picked up. We are going to have to kill some
				   time and make the caller believe the peer hasn't picked up yet */

				if (flags & DIAL_MUSICONHOLD) {
					cw_indicate(chan, -1);
					cw_moh_start(chan, mohclass);
				} else if (flags & DIAL_RINGBACKONLY) {
					sentringing++;
					cw_indicate(chan, CW_CONTROL_RINGING);
				}

				/* Start autoservice on the other chan ?? */
				res2 = cw_autoservice_start(chan);
				/* Now Stream the File */
				if (!res2) {
					do {
						if (!res2)
							res2 = cw_play_and_wait(peer,"priv-callpending");
						if( res2 < '1' || (opt.privacy && res2>'5') || (opt.screen && res2 > '4') ) /* uh, interrupting with a bad answer is ... ignorable! */
							res2 = 0;
						
						/* priv-callpending script: 
						   "I have a caller waiting, who introduces themselves as:"
						*/
						if (!res2)
							res2 = cw_play_and_wait(peer,privintro);
						if( res2 < '1' || (opt.privacy && res2>'5') || (opt.screen && res2 > '4') ) /* uh, interrupting with a bad answer is ... ignorable! */
							res2 = 0;
						/* now get input from the called party, as to their choice */
						if( !res2 ) {
							if (opt.privacy)
								res2 = cw_play_and_wait(peer,"priv-callee-options");
							if (opt.screen)
								res2 = cw_play_and_wait(peer,"screen-callee-options");
						}
						/* priv-callee-options script:
							"Dial 1 if you wish this caller to reach you directly in the future,
								and immediately connect to their incoming call
							 Dial 2 if you wish to send this caller to voicemail now and 
								forevermore.
							 Dial 3 to send this callerr to the torture menus, now and forevermore.
							 Dial 4 to send this caller to a simple "go away" menu, now and forevermore.
							 Dial 5 to allow this caller to come straight thru to you in the future,
						but right now, just this once, send them to voicemail."
						*/
				
						/* screen-callee-options script:
							"Dial 1 if you wish to immediately connect to the incoming call
							 Dial 2 if you wish to send this caller to voicemail.
							 Dial 3 to send this callerr to the torture menus.
							 Dial 4 to send this caller to a simple "go away" menu.
						*/
						if( !res2 || res2 < '1' || (opt.privacy && res2 > '5') || (opt.screen && res2 > '4') ) {
							/* invalid option */
							res2 = cw_play_and_wait(peer,"vm-sorry");
						}
						loopcount++; /* give the callee a couple chances to make a choice */
					} while( (!res2 || res2 < '1' || (opt.privacy && res2 > '5') || (opt.screen && res2 > '4')) && loopcount < 2 );
				}

				switch(res2) {
				case '1':
					if (opt.privacy) {
						if (option_verbose > 2)
							cw_verbose( VERBOSE_PREFIX_3 "--Set privacy database entry %s/%s to ALLOW\n", privdb, privcid);
						cw_privacy_set(privdb, privcid, CW_PRIVACY_ALLOW);
					}
					break;
				case '2':
					if (opt.privacy) {
						if (option_verbose > 2)
							cw_verbose( VERBOSE_PREFIX_3 "--Set privacy database entry %s/%s to DENY\n", privdb, privcid);
						cw_privacy_set(privdb, privcid, CW_PRIVACY_DENY);
					}
					if (flags & DIAL_MUSICONHOLD) {
						cw_moh_stop(chan);
					} else if (sentringing) {
						sentringing = 0;
						cw_indicate(chan, -1);
					}
					res2 = cw_autoservice_stop(chan);
					cw_hangup(peer); /* hang up on the callee -- he didn't want to talk anyway! */
					res=0;
					goto out;
					break;
				case '3':
					if (opt.privacy) {
						cw_privacy_set(privdb, privcid, CW_PRIVACY_TORTURE);
						if (option_verbose > 2)
							cw_verbose( VERBOSE_PREFIX_3 "--Set privacy database entry %s/%s to TORTURE\n", privdb, privcid);
					}
					cw_copy_string(status, "TORTURE", sizeof(status));
					
					res = 0;
					if (flags & DIAL_MUSICONHOLD) {
						cw_moh_stop(chan);
					} else if (sentringing) {
						sentringing = 0;
						cw_indicate(chan, -1);
					}
					res2 = cw_autoservice_stop(chan);
					cw_hangup(peer); /* hang up on the caller -- he didn't want to talk anyway! */
					goto out; /* Is this right? */
					break;
				case '4':
					if (opt.privacy) {
						if (option_verbose > 2)
							cw_verbose( VERBOSE_PREFIX_3 "--Set privacy database entry %s/%s to KILL\n", privdb, privcid);
						cw_privacy_set(privdb, privcid, CW_PRIVACY_KILL);
					}

					cw_copy_string(status, "DONTCALL", sizeof(status));
					res = 0;
					if (flags & DIAL_MUSICONHOLD) {
						cw_moh_stop(chan);
					} else if (sentringing) {
						sentringing = 0;
						cw_indicate(chan, -1);
					}
					res2 = cw_autoservice_stop(chan);
					cw_hangup(peer); /* hang up on the caller -- he didn't want to talk anyway! */
					goto out; /* Is this right? */
					break;
				case '5':
					if (opt.privacy) {
						if (option_verbose > 2)
							cw_verbose( VERBOSE_PREFIX_3 "--Set privacy database entry %s/%s to ALLOW\n", privdb, privcid);
						cw_privacy_set(privdb, privcid, CW_PRIVACY_ALLOW);
					
						if (flags & DIAL_MUSICONHOLD) {
							cw_moh_stop(chan);
						} else if (sentringing) {
							sentringing = 0;
							cw_indicate(chan, -1);
						}
						res2 = cw_autoservice_stop(chan);
						cw_hangup(peer); /* hang up on the caller -- he didn't want to talk anyway! */
						res=0;
						goto out;
					} /* if not privacy, then 5 is the same as "default" case */
				default:
					/* well, if the user messes up, ... he had his chance... What Is The Best Thing To Do?  */
					/* well, there seems basically two choices. Just patch the caller thru immediately,
				                  or,... put 'em thru to voicemail. */
					/* since the callee may have hung up, let's do the voicemail thing, no database decision */
					if (option_verbose > 2)
						cw_log(CW_LOG_NOTICE,"privacy: no valid response from the callee. Sending the caller to voicemail, the callee isn't responding\n");
					if (flags & DIAL_MUSICONHOLD) {
						cw_moh_stop(chan);
					} else if (sentringing) {
						sentringing = 0;
						cw_indicate(chan, -1);
					}
					res2 = cw_autoservice_stop(chan);
					cw_hangup(peer); /* hang up on the callee -- he didn't want to talk anyway! */
					res=0;
					goto out;
					break;
				}
				if (flags & DIAL_MUSICONHOLD) {
					cw_moh_stop(chan);
				} else if (sentringing) {
					sentringing = 0;
					cw_indicate(chan, -1);
				}
				res2 = cw_autoservice_stop(chan);
				/* if the intro is NOCALLERID, then there's no reason to leave it on disk, it'll 
				   just clog things up, and it's not useful information, not being tied to a CID */
				if( strncmp(privcid,"NOCALLERID",10) == 0 || opt.no_save_intros ) {
					cw_filedelete(privintro, NULL);
					if( cw_fileexists(privintro,NULL,NULL ) )
						cw_log(CW_LOG_NOTICE,"privacy: cw_filedelete didn't do its job on %s\n", privintro);
					else if (option_verbose > 2)
						cw_verbose( VERBOSE_PREFIX_3 "Successfully deleted %s intro file\n", privintro);
				}
			}
		}
		if (announcemsg && announcemsg[0]) {
			/* Start autoservice on the other chan */
			res = cw_autoservice_start(chan);
			/* Now Stream the File */
			if (!res)
				res = cw_streamfile(peer, announcemsg, peer->language);
			if (!res) {
				digit = cw_waitstream(peer, CW_DIGIT_ANY); 
			}
			/* Ok, done. stop autoservice */
			res = cw_autoservice_stop(chan);
			if (digit > 0 && !res)
				res = cw_senddigit(chan, digit); 
			else
				res = digit;

		} else
			res = 0;

		if (chan && peer && dblgoto) {
			for (mac = dblgoto; *mac; mac++) {
				if(*mac == '^') {
					*mac = ',';
				}
			}
			cw_parseable_goto(chan, dblgoto);
			cw_parseable_goto(peer, dblgoto);
			peer->priority++;
			cw_pbx_start(peer);
			goto out;
		}

		if (proc_name) {
			res = cw_autoservice_start(chan);
			if (res) {
				cw_log(CW_LOG_ERROR, "Unable to start autoservice on calling channel\n");
				res = -1;
			}

			if (!res) {
				for (res = 0;  res < strlen(proc_name);  res++)
					if (proc_name[res] == '^')
						proc_name[res] = ',';
				res = cw_function_exec_str(peer, CW_KEYWORD_Proc, "Proc", proc_name, NULL);
				res = 0;
			}

			if (cw_autoservice_stop(chan) < 0) {
				cw_log(CW_LOG_ERROR, "Could not stop autoservice on calling channel\n");
				res = -1;
			}

			if (!res) {
				if ((tmpvar = pbx_builtin_getvar_helper(peer, CW_KEYWORD_PROC_RESULT, "PROC_RESULT"))) {
					if (!strcasecmp(tmpvar->value, "BUSY")) {
						cw_copy_string(status, tmpvar->value, sizeof(status));
						res = -1;
					}
					else if (!strcasecmp(tmpvar->value, "CONGESTION") || !strcasecmp(tmpvar->value, "CHANUNAVAIL")) {
						cw_copy_string(status, tmpvar->value, sizeof(status));
						cw_set_flag(peerflags, DIAL_GO_ON);
						res = -1;
					}
					else if (!strcasecmp(tmpvar->value, "CONTINUE")) {
						/* hangup peer and keep chan alive assuming the proc has changed 
						   the context / exten / priority or perhaps 
						   the next priority in the current exten is desired.
						*/
						cw_set_flag(peerflags, DIAL_GO_ON);
						res = -1;
					} else if (!strcasecmp(tmpvar->value, "ABORT")) {
						/* Hangup both ends unless the caller has the g flag */
						res = -1;
					} else if (!strncasecmp(tmpvar->value, "GOTO:",5) && (proc_transfer_dest = cw_strdupa(tmpvar->value + 5))) {
						res = -1;
						/* perform a transfer to a new extension */
						if (strchr(proc_transfer_dest,'^')) { /* context^exten^priority*/
							/* no brainer mode... substitute ^ with , and feed it to builtin goto */
							for (res=0;res<strlen(proc_transfer_dest);res++)
								if (proc_transfer_dest[res] == '^')
									proc_transfer_dest[res] = ',';

							if (!cw_parseable_goto(chan, proc_transfer_dest))
								cw_set_flag(peerflags, DIAL_GO_ON);

						}
					}

					cw_object_put(tmpvar);
				}
			}
		}
		if (opt.waitpound) {
			cw_indicate(chan, CW_CONTROL_RINGING);
			inputkey = cw_waitfordigit(peer, 6000);
			if (inputkey != '#') {
				strncpy(status, "NOANSWER", sizeof(status) - 1);
				cw_hangup(peer);
				goto out;
			}
		}		
		if (!res) {
			if (calldurationlimit > 0) {
				time(&now);
				peer->whentohangup = now + calldurationlimit;
			}
			if (!cw_strlen_zero(dtmfcalled)) { 
				if (option_verbose > 2)
					cw_verbose(VERBOSE_PREFIX_3 "Sending DTMF '%s' to the called party.\n",dtmfcalled);
				res = cw_dtmf_stream(peer,chan,dtmfcalled,250);
			}
			if (!cw_strlen_zero(dtmfcalling)) {
				if (option_verbose > 2)
					cw_verbose(VERBOSE_PREFIX_3 "Sending DTMF '%s' to the calling party.\n",dtmfcalling);
				res = cw_dtmf_stream(chan,peer,dtmfcalling,250);
			}
		}
		
		if (!res) {
			if (cw_test_flag(peerflags, DIAL_ALLOWREDIRECT_IN))
				cw_set_flag(&(config.features_callee), CW_FEATURE_REDIRECT);
			if (cw_test_flag(peerflags, DIAL_ALLOWREDIRECT_OUT))
				cw_set_flag(&(config.features_caller), CW_FEATURE_REDIRECT);
			if (cw_test_flag(peerflags, DIAL_ALLOWDISCONNECT_IN))
				cw_set_flag(&(config.features_callee), CW_FEATURE_DISCONNECT);
			if (cw_test_flag(peerflags, DIAL_ALLOWDISCONNECT_OUT))
				cw_set_flag(&(config.features_caller), CW_FEATURE_DISCONNECT);
			if (cw_test_flag(peerflags, DIAL_MONITOR_IN))
				cw_set_flag(&(config.features_callee), CW_FEATURE_AUTOMON);
			if (cw_test_flag(peerflags, DIAL_MONITOR_OUT)) 
				cw_set_flag(&(config.features_caller), CW_FEATURE_AUTOMON);

			config.timelimit = timelimit;
			config.play_warning = play_warning;
			config.warning_freq = warning_freq;
			if (moh) {
				moh = 0;
				cw_moh_stop(chan);
			} else if (sentringing) {
				sentringing = 0;
				cw_indicate(chan, -1);
			}
			/* Be sure no generators are left on it */
			cw_generator_deactivate(&chan->generator);
			/* Make sure channels are compatible */
			res = cw_channel_make_compatible(chan, peer);
			if (res < 0) {
				cw_log(CW_LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", chan->name, peer->name);
				cw_hangup(peer);
				goto out;
			}
			res = cw_bridge_call(chan,peer,&config);
			time(&end_time);
			snprintf(toast, sizeof(toast), "%ld", (long)(end_time - start_time));
			pbx_builtin_setvar_helper(chan, "DIALEDTIME", toast);
			snprintf(toast, sizeof(toast), "%ld", (long)(end_time - answer_time));
			pbx_builtin_setvar_helper(chan, "ANSWEREDTIME", toast);
			
		} else 
			res = -1;
		
		if (res != CW_PBX_NO_HANGUP_PEER) {
			if (!chan->_softhangup)
				chan->hangupcause = peer->hangupcause;
			if (!cw_check_hangup(peer) && jumpdst) {
				for (mac = jumpdst; *mac; mac++) {
					if(*mac == '^') {
						*mac = ',';
					}
				}
				cw_parseable_goto(peer, jumpdst);
				cw_pbx_start(peer);
			} else
				cw_hangup(peer);
		}
	}
out:
	if (moh) {
		moh = 0;
		cw_moh_stop(chan);
	} else if (sentringing) {
		sentringing = 0;
		cw_indicate(chan, -1);
	}

	hanguptree(outgoing, NULL);

	pbx_builtin_setvar_helper(chan, "DIALSTATUS", status);
	cw_log(CW_LOG_DEBUG, "Exiting with DIALSTATUS=%s.\n", status);

	if ((cw_test_flag(peerflags, DIAL_GO_ON)) && (!chan->_softhangup) && (res != CW_PBX_KEEPALIVE))
		res = 0;

err:
	free(config.start_sound);
	free(config.end_sound);
	free(config.warning_sound);

	cw_dynargs_free(&args);

	LOCAL_USER_REMOVE(u);
	return res;
}

static int dial_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct cw_flags peerflags;

	CW_UNUSED(result);

	memset(&peerflags, 0, sizeof(peerflags));
	return dial_exec_full(chan, argc, argv, &peerflags);
}

static int retrydial_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct cw_flags peerflags;
	struct cw_var_t *context;
	char *announce = NULL;
	struct localuser *u;
	int retryinterval = 0, loops = 0, res = 0;

	CW_UNUSED(result);

	if (argc < 4 || argc > 7)
		return cw_function_syntax(retrydial_syntax);

	LOCAL_USER_ADD(u);

	announce = argv[0];
	retryinterval = atoi(argv[1]) * 1000;
	if (retryinterval < 1000) retryinterval = 10000;
	loops = atoi(argv[2]);
	if (!loops) loops = -1;

	argv += 3;
	argc -= 3;

	context = pbx_builtin_getvar_helper(chan, CW_KEYWORD_EXITCONTEXT, "EXITCONTEXT");

	while (loops) {
		if (cw_test_flag(chan, CW_FLAG_MOH))
			cw_moh_stop(chan);

		if ((res = dial_exec_full(chan, argc, argv, &peerflags)) == 0) {
			if (cw_test_flag(&peerflags, DIAL_HALT_ON_DTMF)) {
				if (!(res = cw_streamfile(chan, announce, chan->language)))
					res = cw_waitstream(chan, CW_DIGIT_ANY);
				if (!res && retryinterval) {
					if (!cw_test_flag(chan, CW_FLAG_MOH))
						cw_moh_start(chan, NULL);
					res = cw_waitfordigit(chan, retryinterval);
				}
			} else {
				if (!(res = cw_streamfile(chan, announce, chan->language)))
					res = cw_waitstream(chan, "");
				if (retryinterval) {
					if (!cw_test_flag(chan, CW_FLAG_MOH))
						cw_moh_start(chan, NULL);
					if (!res) 
						res = cw_waitfordigit(chan, retryinterval);
				}
			}
		}

		if (res < 0)
			break;
		else if (res > 0) { /* Trying to send the call elsewhere (1 digit ext) */
			if (onedigit_goto(chan, context->value, (char) res)) {
				res = 0;
				break;
			}
		}
		loops--;
	}

	if (context)
		cw_object_put(context);

	if (cw_test_flag(chan, CW_FLAG_MOH))
		cw_moh_stop(chan);

	LOCAL_USER_REMOVE(u);
	return loops ? res : 0;

}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(dial_app);
	res |= cw_unregister_function(retrydial_app);
	return res;
}

static int load_module(void)
{
	dial_app = cw_register_function(dial_name, dial_exec, dial_synopsis, dial_syntax, dial_descrip);
	retrydial_app = cw_register_function(retrydial_name, retrydial_exec, retrydial_synopsis, retrydial_syntax, retrydial_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
