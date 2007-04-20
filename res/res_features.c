
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
 * \brief Routines implementing call parking
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>
#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/options.h"
#include "callweaver/causes.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/app.h"
#include "callweaver/say.h"
#include "callweaver/features.h"
#include "callweaver/musiconhold.h"
#include "callweaver/config.h"
#include "callweaver/cli.h"
#include "callweaver/manager.h"
#include "callweaver/utils.h"
#include "callweaver/adsi.h"

#ifdef __OPBX_DEBUG_MALLOC
	 static void FREE(void *ptr)
{
	free(ptr);
}
#else
#define FREE free
#endif

#define DEFAULT_PARK_TIME 45000
#define DEFAULT_TRANSFER_DIGIT_TIMEOUT 3000
#define DEFAULT_FEATURE_DIGIT_TIMEOUT 500

#define OPBX_MAX_WATCHERS 256

static char *parkedcall = "ParkedCall";

/* No more than 45 seconds parked before you do something with them */
static int parkingtime = DEFAULT_PARK_TIME;

/* Context for which parking is made accessible */
static char parking_con[OPBX_MAX_EXTENSION];

/* Context for dialback for parking (KLUDGE) */
static char parking_con_dial[OPBX_MAX_EXTENSION];

/* Extension you type to park the call */
static char parking_ext[OPBX_MAX_EXTENSION];

static char pickup_ext[OPBX_MAX_EXTENSION];

/* Default sounds */
static char courtesytone[256];
static char xfersound[256];
static char xferfailsound[256];

/* First available extension for parking */
static int parking_start;

/* Last available extension for parking */
static int parking_stop;

static int parking_offset;

static int parkfindnext;

static int adsipark;

static int transferdigittimeout;
static int featuredigittimeout;

/* Default courtesy tone played when party joins conference */

/* Registrar for operations */
static char *registrar = "res_features";

static char *synopsis = "Answer a parked call";

static char *descrip = "ParkedCall(exten):"
"Used to connect to a parked call.  This application is always\n"
"registered internally and does not need to be explicitly added\n"
"into the dialplan, although you should include the 'parkedcalls'\n"
"context.\n";

static char *parkcall = "Park";

static char *synopsis2 = "Park yourself";

static char *descrip2 = "Park(exten):"
"Used to park yourself (typically in combination with a supervised\n"
"transfer to know the parking space). This application is always\n"
"registered internally and does not need to be explicitly added\n"
"into the dialplan, although you should include the 'parkedcalls'\n"
"context.\n";

static struct opbx_app *monitor_app=NULL;
static int monitor_ok=1;

struct parkeduser {
	struct opbx_channel *chan;
	struct timeval start;
	int parkingnum;
	/* Where to go if our parking time expires */
	char context[OPBX_MAX_CONTEXT];
	char exten[OPBX_MAX_EXTENSION];
	int priority;
	int parkingtime;
	int notquiteyet;
	char peername[1024];
	unsigned char moh_trys;
	struct parkeduser *next;
};

static struct parkeduser *parkinglot;

OPBX_MUTEX_DEFINE_STATIC(parking_lock);

static pthread_t parking_thread;

/* Predeclare all statics to keep GCC 4.x happy */
static char *__opbx_parking_ext(void);
static void *__opbx_bridge_call_thread(void *);
static void __opbx_bridge_call_thread_launch(void *);
static int __opbx_park_call(struct opbx_channel *, struct opbx_channel *, int, int *);
static int __opbx_masq_park_call(struct opbx_channel *, struct opbx_channel *, int, int *);
static void __opbx_register_feature(struct opbx_call_feature *);
static void __opbx_unregister_feature(struct opbx_call_feature *);
static void __opbx_unregister_features(void);
static int __opbx_bridge_call(struct opbx_channel *,struct opbx_channel *,struct opbx_bridge_config *);


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static char *__opbx_parking_ext(void)
{
	return parking_ext;
}

char *__opbx_pickup_ext(void)
{
	return pickup_ext;
}

struct opbx_bridge_thread_obj 
{
	struct opbx_bridge_config bconfig;
	struct opbx_channel *chan;
	struct opbx_channel *peer;
};

static void check_goto_on_transfer(struct opbx_channel *chan) 
{
	struct opbx_channel *xferchan;
	char *goto_on_transfer;

	goto_on_transfer = pbx_builtin_getvar_helper(chan, "GOTO_ON_BLINDXFR");

	if (goto_on_transfer && !opbx_strlen_zero(goto_on_transfer) && (xferchan = opbx_channel_alloc(0))) {
		char *x;
		struct opbx_frame *f;
		
		for (x = goto_on_transfer; x && *x; x++)
			if (*x == '^')
				*x = '|';

		strcpy(xferchan->name, chan->name);
		/* Make formats okay */
		xferchan->readformat = chan->readformat;
		xferchan->writeformat = chan->writeformat;
		opbx_channel_masquerade(xferchan, chan);
		opbx_parseable_goto(xferchan, goto_on_transfer);
		xferchan->_state = OPBX_STATE_UP;
		opbx_clear_flag(xferchan, OPBX_FLAGS_ALL);	
		xferchan->_softhangup = 0;
		if ((f = opbx_read(xferchan))) {
			opbx_fr_free(f);
			f = NULL;
			opbx_pbx_start(xferchan);
		} else {
			opbx_hangup(xferchan);
		}
	}
}

static struct opbx_channel *opbx_feature_request_and_dial(struct opbx_channel *caller, const char *type, int format, void *data, int timeout, int *outstate, const char *cid_num, const char *cid_name);


static void *__opbx_bridge_call_thread(void *data) 
{
	struct opbx_bridge_thread_obj *tobj = data;
	tobj->chan->appl = "Transferred Call";
	tobj->chan->data = tobj->peer->name;
	tobj->peer->appl = "Transferred Call";
	tobj->peer->data = tobj->chan->name;
	if (tobj->chan->cdr) {
		opbx_cdr_reset(tobj->chan->cdr,0);
		opbx_cdr_setdestchan(tobj->chan->cdr, tobj->peer->name);
	}
	if (tobj->peer->cdr) {
		opbx_cdr_reset(tobj->peer->cdr,0);
		opbx_cdr_setdestchan(tobj->peer->cdr, tobj->chan->name);
	}


	__opbx_bridge_call(tobj->peer, tobj->chan, &tobj->bconfig);
	opbx_hangup(tobj->chan);
	opbx_hangup(tobj->peer);
	tobj->chan = tobj->peer = NULL;
	free(tobj);
	tobj=NULL;
	return NULL;
}

static void __opbx_bridge_call_thread_launch(void *data) 
{
	pthread_t thread;
	pthread_attr_t attr;
	int result;

	result = pthread_attr_init(&attr);
	pthread_attr_setschedpolicy(&attr, SCHED_RR);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	result = opbx_pthread_create(&thread, &attr,__opbx_bridge_call_thread, data);
	result = pthread_attr_destroy(&attr);
}



static int adsi_announce_park(struct opbx_channel *chan, int parkingnum)
{
	int res;
	int justify[5] = {ADSI_JUST_CENT, ADSI_JUST_CENT, ADSI_JUST_CENT, ADSI_JUST_CENT};
	char tmp[256];
	char *message[5] = {NULL, NULL, NULL, NULL, NULL};

	snprintf(tmp, sizeof(tmp), "Parked on %d", parkingnum);
	message[0] = tmp;
	res = adsi_load_session(chan, NULL, 0, 1);
	if (res == -1) {
		return res;
	}
	return adsi_print(chan, message, justify, 1);
}

/*--- opbx_park_call: Park a call */
/* We put the user in the parking list, then wake up the parking thread to be sure it looks
	   after these channels too */
static int __opbx_park_call(struct opbx_channel *chan, struct opbx_channel *peer, int timeout, int *extout)
{
	struct parkeduser *pu, *cur;
	int i,x,parking_range;
	char exten[OPBX_MAX_EXTENSION];
	struct opbx_context *con;

	pu = malloc(sizeof(struct parkeduser));
	if (!pu) {
		opbx_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	memset(pu, 0, sizeof(struct parkeduser));
	opbx_mutex_lock(&parking_lock);
	parking_range = parking_stop - parking_start+1;
	for (i = 0; i < parking_range; i++) {
		x = (i + parking_offset) % parking_range + parking_start;
		cur = parkinglot;
		while(cur) {
			if (cur->parkingnum == x) 
				break;
			cur = cur->next;
		}
		if (!cur)
			break;
	}

	if (!(i < parking_range)) {
		opbx_log(LOG_WARNING, "No more parking spaces\n");
		free(pu);
		opbx_mutex_unlock(&parking_lock);
		return -1;
	}
	if (parkfindnext) 
		parking_offset = x - parking_start + 1;
	chan->appl = "Parked Call";
	chan->data = NULL; 

	pu->chan = chan;
	/* Start music on hold */
	if (chan != peer) {
		opbx_indicate(pu->chan, OPBX_CONTROL_HOLD);
		opbx_moh_start(pu->chan, NULL);
	}
	pu->start = opbx_tvnow();
	pu->parkingnum = x;
	if (timeout > 0)
		pu->parkingtime = timeout;
	else
		pu->parkingtime = parkingtime;
	if (extout)
		*extout = x;
	if (peer) 
		opbx_copy_string(pu->peername, peer->name, sizeof(pu->peername));

	/* Remember what had been dialed, so that if the parking
	   expires, we try to come back to the same place */
	if (!opbx_strlen_zero(chan->macrocontext))
		opbx_copy_string(pu->context, chan->macrocontext, sizeof(pu->context));
	else
		opbx_copy_string(pu->context, chan->context, sizeof(pu->context));
	if (!opbx_strlen_zero(chan->macroexten))
		opbx_copy_string(pu->exten, chan->macroexten, sizeof(pu->exten));
	else
		opbx_copy_string(pu->exten, chan->exten, sizeof(pu->exten));
	if (chan->macropriority)
		pu->priority = chan->macropriority;
	else
		pu->priority = chan->priority;
	pu->next = parkinglot;
	parkinglot = pu;
	/* If parking a channel directly, don't quiet yet get parking running on it */
	if (peer == chan) 
		pu->notquiteyet = 1;
	opbx_mutex_unlock(&parking_lock);
	/* Wake up the (presumably select()ing) thread */
	pthread_kill(parking_thread, SIGURG);
	if (option_verbose > 1) 
		opbx_verbose(VERBOSE_PREFIX_2 "Parked %s on %d. Will timeout back to extension [%s] %s, %d in %d seconds\n", pu->chan->name, pu->parkingnum, pu->context, pu->exten, pu->priority, (pu->parkingtime/1000));

	manager_event(EVENT_FLAG_CALL, "ParkedCall",
		"Exten: %d\r\n"
		"Channel: %s\r\n"
		"From: %s\r\n"
		"Timeout: %ld\r\n"
		"CallerID: %s\r\n"
		"CallerIDName: %s\r\n\r\n"
		,pu->parkingnum, pu->chan->name, peer->name
		,(long)pu->start.tv_sec + (long)(pu->parkingtime/1000) - (long)time(NULL)
		,(pu->chan->cid.cid_num ? pu->chan->cid.cid_num : "<unknown>")
		,(pu->chan->cid.cid_name ? pu->chan->cid.cid_name : "<unknown>")
		);

	if (peer) {
		if (adsipark && adsi_available(peer)) {
			adsi_announce_park(peer, pu->parkingnum);
		}
		if (adsipark && adsi_available(peer)) {
			adsi_unload_session(peer);
		}
	}
	con = opbx_context_find(parking_con);
	if (!con) {
		con = opbx_context_create(NULL, parking_con, registrar);
		if (!con) {
			opbx_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", parking_con);
		}
	}
	if (con) {
		snprintf(exten, sizeof(exten), "%d", x);
		opbx_add_extension2(con, 1, exten, 1, NULL, NULL, parkedcall, strdup(exten), FREE, registrar);
	}
	if (peer) 
		opbx_say_digits(peer, pu->parkingnum, "", peer->language);
	if (pu->notquiteyet) {
		/* Wake up parking thread if we're really done */
		opbx_moh_start(pu->chan, NULL);
		pu->notquiteyet = 0;
		pthread_kill(parking_thread, SIGURG);
	}
	return 0;
}

static int __opbx_masq_park_call(struct opbx_channel *rchan, struct opbx_channel *peer, int timeout, int *extout)
{
	struct opbx_channel *chan;
	struct opbx_frame *f;

	/* Make a new, fake channel that we'll use to masquerade in the real one */
	chan = opbx_channel_alloc(0);
	if (chan) {
		/* Let us keep track of the channel name */
		snprintf(chan->name, sizeof (chan->name), "Parked/%s",rchan->name);

		/* Make formats okay */
		chan->readformat = rchan->readformat;
		chan->writeformat = rchan->writeformat;
		opbx_channel_masquerade(chan, rchan);

		/* Setup the extensions and such */
		opbx_copy_string(chan->context, rchan->context, sizeof(chan->context));
		opbx_copy_string(chan->exten, rchan->exten, sizeof(chan->exten));
		chan->priority = rchan->priority;

		/* Make the masq execute */
		f = opbx_read(chan);
		if (f)
			opbx_fr_free(f);
		__opbx_park_call(chan, peer, timeout, extout);
	} else {
		opbx_log(LOG_WARNING, "Unable to create parked channel\n");
		return -1;
	}
	return 0;
}


#define FEATURE_RETURN_HANGUP		-1
#define FEATURE_RETURN_SUCCESSBREAK	 0
#define FEATURE_RETURN_PBX_KEEPALIVE	OPBX_PBX_KEEPALIVE
#define FEATURE_RETURN_NO_HANGUP_PEER	OPBX_PBX_NO_HANGUP_PEER
#define FEATURE_RETURN_PASSDIGITS	 21
#define FEATURE_RETURN_STOREDIGITS	 22
#define FEATURE_RETURN_SUCCESS	 	 23

#define FEATURE_SENSE_CHAN	(1 << 0)
#define FEATURE_SENSE_PEER	(1 << 1)


static int builtin_automonitor(struct opbx_channel *chan, struct opbx_channel *peer, struct opbx_bridge_config *config, char *code, int sense)
{
	char *touch_monitor = NULL, *caller_chan_id = NULL, *callee_chan_id = NULL, *args = NULL, *touch_format = NULL;
	int x = 0;
	size_t len;
	struct opbx_channel *caller_chan = NULL, *callee_chan = NULL;


	if(sense == 2) {
		caller_chan = peer;
		callee_chan = chan;
	} else {
		callee_chan = peer;
		caller_chan = chan;
	}
	
	if (!monitor_ok) {
		opbx_log(LOG_ERROR,"Cannot record the call. The monitor application is disabled.\n");
		return -1;
	}

	if (!monitor_app) { 
		if (!(monitor_app = pbx_findapp("Monitor"))) {
			monitor_ok=0;
			opbx_log(LOG_ERROR,"Cannot record the call. The monitor application is disabled.\n");
			return -1;
		}
	}
	if (!opbx_strlen_zero(courtesytone)) {
		if (opbx_autoservice_start(callee_chan))
			return -1;
		if (!opbx_streamfile(caller_chan, courtesytone, caller_chan->language)) {
			if (opbx_waitstream(caller_chan, "") < 0) {
				opbx_log(LOG_WARNING, "Failed to play courtesy tone!\n");
				opbx_autoservice_stop(callee_chan);
				return -1;
			}
		}
		if (opbx_autoservice_stop(callee_chan))
			return -1;
	}
	
	if (callee_chan->monitor) {
		if (option_verbose > 3)
			opbx_verbose(VERBOSE_PREFIX_3 "User hit '%s' to stop recording call.\n", code);
		opbx_monitor_stop(callee_chan, 1);
		return FEATURE_RETURN_SUCCESS;
	}

	if (caller_chan && callee_chan) {
		touch_format = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR_FORMAT");
		if (!touch_format)
			touch_format = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR_FORMAT");

		touch_monitor = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR");
		if (!touch_monitor)
			touch_monitor = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR");
		
		if (touch_monitor) {
			len = strlen(touch_monitor) + 50;
			args = alloca(len);
			snprintf(args, len, "%s|auto-%ld-%s|m", (touch_format) ? touch_format : "wav", time(NULL), touch_monitor);
		} else {
			caller_chan_id = opbx_strdupa(caller_chan->cid.cid_num ? caller_chan->cid.cid_num : caller_chan->name);
			callee_chan_id = opbx_strdupa(callee_chan->cid.cid_num ? callee_chan->cid.cid_num : callee_chan->name);
			len = strlen(caller_chan_id) + strlen(callee_chan_id) + 50;
			args = alloca(len);
			snprintf(args, len, "%s|auto-%ld-%s-%s|m", (touch_format) ? touch_format : "wav", time(NULL), caller_chan_id, callee_chan_id);
		}

		for( x = 0; x < strlen(args); x++)
			if (args[x] == '/')
				args[x] = '-';
		
		if (option_verbose > 3)
			opbx_verbose(VERBOSE_PREFIX_3 "User hit '%s' to record call. filename: %s\n", code, args);

		pbx_exec(callee_chan, monitor_app, args, 1);
		
		return FEATURE_RETURN_SUCCESS;
	}
	
	opbx_log(LOG_NOTICE,"Cannot record the call. One or both channels have gone away.\n");	
	return -1;
}

static int builtin_disconnect(struct opbx_channel *chan, struct opbx_channel *peer, struct opbx_bridge_config *config, char *code, int sense)
{
	if (option_verbose > 3)
		opbx_verbose(VERBOSE_PREFIX_3 "User hit '%s' to disconnect call.\n", code);
	return FEATURE_RETURN_HANGUP;
}

static int builtin_blindtransfer(struct opbx_channel *chan, struct opbx_channel *peer, struct opbx_bridge_config *config, char *code, int sense)
{
	struct opbx_channel *transferer;
	struct opbx_channel *transferee;
	char *transferer_real_context;
	char newext[256];
	int res;

	if (sense == FEATURE_SENSE_PEER) {
		transferer = peer;
		transferee = chan;
	} else {
		transferer = chan;
		transferee = peer;
	}
	if (!(transferer_real_context = pbx_builtin_getvar_helper(transferee, "TRANSFER_CONTEXT")) &&
		!(transferer_real_context = pbx_builtin_getvar_helper(transferer, "TRANSFER_CONTEXT"))) {
		/* Use the non-macro context to transfer the call */
		if (!opbx_strlen_zero(transferer->macrocontext))
			transferer_real_context = transferer->macrocontext;
		else
			transferer_real_context = transferer->context;
	}
	/* Start autoservice on chan while we talk
	   to the originator */
	opbx_indicate(transferee, OPBX_CONTROL_HOLD);
	opbx_autoservice_start(transferee);
	opbx_moh_start(transferee, NULL);

	memset(newext, 0, sizeof(newext));
	
	/* Transfer */
	if ((res=opbx_streamfile(transferer, "pbx-transfer", transferer->language))) {
		opbx_moh_stop(transferee);
		opbx_autoservice_stop(transferee);
		opbx_indicate(transferee, OPBX_CONTROL_UNHOLD);
		return res;
	}
	if ((res=opbx_waitstream(transferer, OPBX_DIGIT_ANY)) < 0) {
		opbx_moh_stop(transferee);
		opbx_autoservice_stop(transferee);
		opbx_indicate(transferee, OPBX_CONTROL_UNHOLD);
		return res;
	} else if (res > 0) {
		/* If they've typed a digit already, handle it */
		newext[0] = (char) res;
	}

	opbx_stopstream(transferer);
	res = opbx_app_dtget(transferer, transferer_real_context, newext, sizeof(newext), 100, transferdigittimeout);
	if (res < 0) {
		opbx_moh_stop(transferee);
		opbx_autoservice_stop(transferee);
		opbx_indicate(transferee, OPBX_CONTROL_UNHOLD);
		return res;
	}
	if (!strcmp(newext, __opbx_parking_ext())) {
		opbx_moh_stop(transferee);

		res = opbx_autoservice_stop(transferee);
		opbx_indicate(transferee, OPBX_CONTROL_UNHOLD);
		if (res)
			res = -1;
		else if (!__opbx_park_call(transferee, transferer, 0, NULL)) {
			/* We return non-zero, but tell the PBX not to hang the channel when
			   the thread dies -- We have to be careful now though.  We are responsible for 
			   hanging up the channel, else it will never be hung up! */

			if (transferer == peer)
				res = OPBX_PBX_KEEPALIVE;
			else
				res = OPBX_PBX_NO_HANGUP_PEER;
			return res;
		} else {
			opbx_log(LOG_WARNING, "Unable to park call %s\n", transferee->name);
		}
		/* XXX Maybe we should have another message here instead of invalid extension XXX */
	} else if (opbx_exists_extension(transferee, transferer_real_context, newext, 1, transferer->cid.cid_num)) {
		pbx_builtin_setvar_helper(peer, "BLINDTRANSFER", chan->name);
		pbx_builtin_setvar_helper(chan, "BLINDTRANSFER", peer->name);
		opbx_moh_stop(transferee);
		res=opbx_autoservice_stop(transferee);
		opbx_indicate(transferee, OPBX_CONTROL_UNHOLD);
		if (!transferee->pbx) {
			/* Doh!  Use our handy async_goto functions */
			if (option_verbose > 2) 
				opbx_verbose(VERBOSE_PREFIX_3 "Transferring %s to '%s' (context %s) priority 1\n"
							 ,transferee->name, newext, transferer_real_context);
			if (opbx_async_goto(transferee, transferer_real_context, newext, 1))
				opbx_log(LOG_WARNING, "Async goto failed :-(\n");
			res = -1;
		} else {
			/* Set the channel's new extension, since it exists, using transferer context */
			opbx_copy_string(transferee->exten, newext, sizeof(transferee->exten));
			opbx_copy_string(transferee->context, transferer_real_context, sizeof(transferee->context));
			transferee->priority = 0;
		}
		check_goto_on_transfer(transferer);
		return res;
	} else {
		if (option_verbose > 2)	
			opbx_verbose(VERBOSE_PREFIX_3 "Unable to find extension '%s' in context '%s'\n", newext, transferer_real_context);
	}
	if (!opbx_strlen_zero(xferfailsound))
		res = opbx_streamfile(transferer, xferfailsound, transferee->language);
	else
		res = 0;
	if (res) {
		opbx_moh_stop(transferee);
		opbx_autoservice_stop(transferee);
		opbx_indicate(transferee, OPBX_CONTROL_UNHOLD);
		return res;
	}
	res = opbx_waitstream(transferer, OPBX_DIGIT_ANY);
	opbx_stopstream(transferer);
	opbx_moh_stop(transferee);
	res = opbx_autoservice_stop(transferee);
	opbx_indicate(transferee, OPBX_CONTROL_UNHOLD);
	if (res) {
		if (option_verbose > 1)
			opbx_verbose(VERBOSE_PREFIX_2 "Hungup during autoservice stop on '%s'\n", transferee->name);
		return res;
	}
	return FEATURE_RETURN_SUCCESS;
}

static int builtin_atxfer(struct opbx_channel *chan, struct opbx_channel *peer, struct opbx_bridge_config *config, char *code, int sense)
{
	struct opbx_channel *transferer;
	struct opbx_channel *transferee;
	struct opbx_channel *newchan, *xferchan=NULL;
	int outstate=0;
	struct opbx_bridge_config bconfig;
	char *transferer_real_context;
	char xferto[256],dialstr[265];
	char *cid_num;
	char *cid_name;
	int res;
	struct opbx_frame *f = NULL;
	struct opbx_bridge_thread_obj *tobj;

	opbx_log(LOG_DEBUG, "Executing Attended Transfer %s, %s (sense=%d) XXX\n", chan->name, peer->name, sense);
	if (sense == FEATURE_SENSE_PEER) {
		transferer = peer;
		transferee = chan;
	} else {
		transferer = chan;
		transferee = peer;
	}
	if (!(transferer_real_context=pbx_builtin_getvar_helper(transferee, "TRANSFER_CONTEXT")) &&
		!(transferer_real_context=pbx_builtin_getvar_helper(transferer, "TRANSFER_CONTEXT"))) {
		/* Use the non-macro context to transfer the call */
		if (!opbx_strlen_zero(transferer->macrocontext))
			transferer_real_context = transferer->macrocontext;
		else
			transferer_real_context = transferer->context;
	}
	/* Start autoservice on chan while we talk
	   to the originator */
	opbx_indicate(transferee, OPBX_CONTROL_HOLD);
	opbx_autoservice_start(transferee);
	opbx_moh_start(transferee, NULL);
	memset(xferto, 0, sizeof(xferto));
	/* Transfer */
	if ((res = opbx_streamfile(transferer, "pbx-transfer", transferer->language))) {
		opbx_moh_stop(transferee);
		opbx_autoservice_stop(transferee);
		opbx_indicate(transferee, OPBX_CONTROL_UNHOLD);
		return res;
	}
	if ((res=opbx_waitstream(transferer, OPBX_DIGIT_ANY)) < 0) {
		opbx_moh_stop(transferee);
		opbx_autoservice_stop(transferee);
		opbx_indicate(transferee, OPBX_CONTROL_UNHOLD);
		return res;
	} else if(res > 0) {
		/* If they've typed a digit already, handle it */
		xferto[0] = (char) res;
	}
	if ((opbx_app_dtget(transferer, transferer_real_context, xferto, sizeof(xferto), 100, transferdigittimeout))) {
		cid_num = transferer->cid.cid_num;
		cid_name = transferer->cid.cid_name;
		if (opbx_exists_extension(transferer, transferer_real_context,xferto, 1, cid_num)) {
			snprintf(dialstr, sizeof(dialstr), "%s@%s/n", xferto, transferer_real_context);
			newchan = opbx_feature_request_and_dial(transferer, "Local", opbx_best_codec(transferer->nativeformats), dialstr, 15000, &outstate, cid_num, cid_name);
			opbx_indicate(transferer, -1);
			if (newchan) {
				res = opbx_channel_make_compatible(transferer, newchan);
				if (res < 0) {
					opbx_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", transferer->name, newchan->name);
					opbx_hangup(newchan);
					return -1;
				}
				memset(&bconfig,0,sizeof(struct opbx_bridge_config));
				opbx_set_flag(&(bconfig.features_caller), OPBX_FEATURE_DISCONNECT);
				opbx_set_flag(&(bconfig.features_callee), OPBX_FEATURE_DISCONNECT);
				res = __opbx_bridge_call(transferer,newchan,&bconfig);
				if (newchan->_softhangup || newchan->_state != OPBX_STATE_UP || !transferer->_softhangup) {
					opbx_hangup(newchan);
					if (f) {
						opbx_fr_free(f);
						f = NULL;
					}
					if (!opbx_strlen_zero(xfersound) && !opbx_streamfile(transferer, xfersound, transferer->language)) {
						if (opbx_waitstream(transferer, "") < 0) {
							opbx_log(LOG_WARNING, "Failed to play courtesy tone!\n");
						}
					}
					opbx_moh_stop(transferee);
					opbx_autoservice_stop(transferee);
					opbx_indicate(transferee, OPBX_CONTROL_UNHOLD);
					transferer->_softhangup = 0;
					return FEATURE_RETURN_SUCCESS;
				}
				
				res = opbx_channel_make_compatible(transferee, newchan);
				if (res < 0) {
					opbx_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", transferee->name, newchan->name);
					opbx_hangup(newchan);
					return -1;
				}
				
				
				opbx_moh_stop(transferee);
				
				if ((opbx_autoservice_stop(transferee) < 0)
					|| (opbx_waitfordigit(transferee, 100) < 0)
					|| (opbx_waitfordigit(newchan, 100) < 0) 
					|| opbx_check_hangup(transferee) 
					|| opbx_check_hangup(newchan)) {
					opbx_hangup(newchan);
					res = -1;
					return -1;
				}

				if ((xferchan = opbx_channel_alloc(0))) {
					snprintf(xferchan->name, sizeof (xferchan->name), "Transfered/%s",transferee->name);
					/* Make formats okay */
					xferchan->readformat = transferee->readformat;
					xferchan->writeformat = transferee->writeformat;
					opbx_channel_masquerade(xferchan, transferee);
					opbx_explicit_goto(xferchan, transferee->context, transferee->exten, transferee->priority);
					xferchan->_state = OPBX_STATE_UP;
					opbx_clear_flag(xferchan, OPBX_FLAGS_ALL);	
					xferchan->_softhangup = 0;

					if ((f = opbx_read(xferchan))) {
						opbx_fr_free(f);
						f = NULL;
					}
					
				} else {
					opbx_hangup(newchan);
					return -1;
				}

				newchan->_state = OPBX_STATE_UP;
				opbx_clear_flag(newchan, OPBX_FLAGS_ALL);	
				newchan->_softhangup = 0;

				tobj = malloc(sizeof(struct opbx_bridge_thread_obj));
				if (tobj) {
					memset(tobj,0,sizeof(struct opbx_bridge_thread_obj));
					tobj->chan = xferchan;
					tobj->peer = newchan;
					tobj->bconfig = *config;
	
					if (!opbx_strlen_zero(xfersound) && !opbx_streamfile(newchan, xfersound, newchan->language)) {
						if (opbx_waitstream(newchan, "") < 0) {
							opbx_log(LOG_WARNING, "Failed to play courtesy tone!\n");
						}
					}
					__opbx_bridge_call_thread_launch(tobj);
				} else {
					opbx_log(LOG_WARNING, "Out of memory!\n");
					opbx_hangup(xferchan);
					opbx_hangup(newchan);
				}
				return -1;
				
			} else {
				opbx_moh_stop(transferee);
				opbx_autoservice_stop(transferee);
				opbx_indicate(transferee, OPBX_CONTROL_UNHOLD);
				/* any reason besides user requested cancel and busy triggers the failed sound */
				if (outstate != OPBX_CONTROL_UNHOLD && outstate != OPBX_CONTROL_BUSY && !opbx_strlen_zero(xferfailsound)) {
					res = opbx_streamfile(transferer, xferfailsound, transferer->language);
					if (!res && (opbx_waitstream(transferer, "") < 0)) {
						return -1;
					}
				}
				return FEATURE_RETURN_SUCCESS;
			}
		} else {
			opbx_log(LOG_WARNING, "Extension %s does not exist in context %s\n",xferto,transferer_real_context);
			opbx_moh_stop(transferee);
			opbx_autoservice_stop(transferee);
			opbx_indicate(transferee, OPBX_CONTROL_UNHOLD);
			res = opbx_streamfile(transferer, "beeperr", transferer->language);
			if (!res && (opbx_waitstream(transferer, "") < 0)) {
				return -1;
			}
		}
	}  else {
		opbx_log(LOG_WARNING, "Did not read data.\n");
		res = opbx_streamfile(transferer, "beeperr", transferer->language);
		if (opbx_waitstream(transferer, "") < 0) {
			return -1;
		}
	}
	opbx_moh_stop(transferee);
	opbx_autoservice_stop(transferee);
	opbx_indicate(transferee, OPBX_CONTROL_UNHOLD);

	return FEATURE_RETURN_SUCCESS;
}


/* add atxfer and automon as undefined so you can only use em if you configure them */
#define FEATURES_COUNT (sizeof(builtin_features) / sizeof(builtin_features[0]))
struct opbx_call_feature builtin_features[] = 
	{
		{ OPBX_FEATURE_REDIRECT, "Blind Transfer", "blindxfer", "#", "#", builtin_blindtransfer, OPBX_FEATURE_FLAG_NEEDSDTMF },
		{ OPBX_FEATURE_REDIRECT, "Attended Transfer", "atxfer", "", "", builtin_atxfer, OPBX_FEATURE_FLAG_NEEDSDTMF },
		{ OPBX_FEATURE_AUTOMON, "One Touch Monitor", "automon", "", "", builtin_automonitor, OPBX_FEATURE_FLAG_NEEDSDTMF },
		{ OPBX_FEATURE_DISCONNECT, "Disconnect Call", "disconnect", "*", "*", builtin_disconnect, OPBX_FEATURE_FLAG_NEEDSDTMF },
	};


static OPBX_LIST_HEAD(feature_list,opbx_call_feature) feature_list;

/* register new feature into feature_list*/
static void __opbx_register_feature(struct opbx_call_feature *feature)
{
	if (!feature) {
		opbx_log(LOG_NOTICE,"You didn't pass a feature!\n");
		return;
	}
  
	OPBX_LIST_LOCK(&feature_list);
	OPBX_LIST_INSERT_HEAD(&feature_list,feature,feature_entry);
	OPBX_LIST_UNLOCK(&feature_list);

	if (option_verbose >= 2) 
		opbx_verbose(VERBOSE_PREFIX_2 "Registered Feature '%s'\n",feature->sname);
}

/* unregister feature from feature_list */
static void __opbx_unregister_feature(struct opbx_call_feature *feature)
{
	if (!feature) return;

	OPBX_LIST_LOCK(&feature_list);
	OPBX_LIST_REMOVE(&feature_list,feature,feature_entry);
	OPBX_LIST_UNLOCK(&feature_list);
	free(feature);
}

static void __opbx_unregister_features(void)
{
	struct opbx_call_feature *feature;

	OPBX_LIST_LOCK(&feature_list);
	while ((feature = OPBX_LIST_REMOVE_HEAD(&feature_list,feature_entry)))
		free(feature);
	OPBX_LIST_UNLOCK(&feature_list);
}

/* find a feature by name */
static struct opbx_call_feature *find_feature(char *name)
{
	struct opbx_call_feature *tmp;

	OPBX_LIST_LOCK(&feature_list);
	OPBX_LIST_TRAVERSE(&feature_list, tmp, feature_entry) {
		if (!strcasecmp(tmp->sname, name))
			break;
	}
	OPBX_LIST_UNLOCK(&feature_list);

	return tmp;
}

/* exec an app by feature */
static int feature_exec_app(struct opbx_channel *chan, struct opbx_channel *peer, struct opbx_bridge_config *config, char *code, int sense)
{
	struct opbx_app *app;
	struct opbx_call_feature *feature;
	int res;

	OPBX_LIST_LOCK(&feature_list);
	OPBX_LIST_TRAVERSE(&feature_list,feature,feature_entry) {
		if (!strcasecmp(feature->exten,code)) break;
	}
	OPBX_LIST_UNLOCK(&feature_list);

	if (!feature) { /* shouldn't ever happen! */
		opbx_log(LOG_NOTICE, "Found feature before, but at execing we've lost it??\n");
		return -1; 
	}
	
	app = pbx_findapp(feature->app);
	if (app) {
		struct opbx_channel *work=chan;
		if (opbx_test_flag(feature,OPBX_FEATURE_FLAG_CALLEE)) work=peer;
		res = pbx_exec(work, app, feature->app_args, 1);
		if (res<0) return res; 
	} else {
		opbx_log(LOG_WARNING, "Could not find application (%s)\n", feature->app);
		res = -2;
	}
	
	return FEATURE_RETURN_SUCCESS;
}

static void unmap_features(void)
{
	int x;
	for (x = 0; x < FEATURES_COUNT; x++)
		strcpy(builtin_features[x].exten, builtin_features[x].default_exten);
}

static int remap_feature(const char *name, const char *value)
{
	int x;
	int res = -1;
	for (x = 0; x < FEATURES_COUNT; x++) {
		if (!strcasecmp(name, builtin_features[x].sname)) {
			opbx_copy_string(builtin_features[x].exten, value, sizeof(builtin_features[x].exten));
			if (option_verbose > 1)
				opbx_verbose(VERBOSE_PREFIX_2 "Remapping feature %s (%s) to sequence '%s'\n", builtin_features[x].fname, builtin_features[x].sname, builtin_features[x].exten);
			res = 0;
		} else if (!strcmp(value, builtin_features[x].exten)) 
			opbx_log(LOG_WARNING, "Sequence '%s' already mapped to function %s (%s) while assigning to %s\n", value, builtin_features[x].fname, builtin_features[x].sname, name);
	}
	return res;
}

static int opbx_feature_interpret(struct opbx_channel *chan, struct opbx_channel *peer, struct opbx_bridge_config *config, char *code, int sense)
{
	int x;
	struct opbx_flags features;
	int res = FEATURE_RETURN_PASSDIGITS;
	struct opbx_call_feature *feature;
	char *dynamic_features=pbx_builtin_getvar_helper(chan,"DYNAMIC_FEATURES");

	if (sense == FEATURE_SENSE_CHAN)
		opbx_copy_flags(&features, &(config->features_caller), OPBX_FLAGS_ALL);	
	else
		opbx_copy_flags(&features, &(config->features_callee), OPBX_FLAGS_ALL);	
	opbx_log(LOG_DEBUG, "Feature interpret: chan=%s, peer=%s, sense=%d, features=%d\n", chan->name, peer->name, sense, features.flags);

	for (x=0; x < FEATURES_COUNT; x++) {
		if ((opbx_test_flag(&features, builtin_features[x].feature_mask)) &&
		    !opbx_strlen_zero(builtin_features[x].exten)) {
			/* Feature is up for consideration */
			if (!strcmp(builtin_features[x].exten, code)) {
				res = builtin_features[x].operation(chan, peer, config, code, sense);
				break;
			} else if (!strncmp(builtin_features[x].exten, code, strlen(code))) {
				if (res == FEATURE_RETURN_PASSDIGITS)
					res = FEATURE_RETURN_STOREDIGITS;
			}
		}
	}


	if (dynamic_features && !opbx_strlen_zero(dynamic_features)) {
		char *tmp = opbx_strdupa(dynamic_features);
		char *tok;

		if (!tmp)
			return res;
		
		while ((tok = strsep(&tmp, "#")) != NULL) {
			feature = find_feature(tok);
			
			if (feature) {
				/* Feature is up for consideration */
				if (!strcmp(feature->exten, code)) {
					if (option_verbose > 2)
						opbx_verbose(VERBOSE_PREFIX_3 " Feature Found: %s exten: %s\n",feature->sname, tok);
					res = feature->operation(chan, peer, config, code, sense);
					break;
				} else if (!strncmp(feature->exten, code, strlen(code))) {
					res = FEATURE_RETURN_STOREDIGITS;
				}
			}
		}
	}
	
	return res;
}

static void set_config_flags(struct opbx_channel *chan, struct opbx_channel *peer, struct opbx_bridge_config *config)
{
	int x;
	
	opbx_clear_flag(config, OPBX_FLAGS_ALL);	
	for (x = 0; x < FEATURES_COUNT; x++) {
		if (opbx_test_flag(builtin_features + x, OPBX_FEATURE_FLAG_NEEDSDTMF)) {
			if (opbx_test_flag(&(config->features_caller), builtin_features[x].feature_mask))
				opbx_set_flag(config, OPBX_BRIDGE_DTMF_CHANNEL_0);

			if (opbx_test_flag(&(config->features_callee), builtin_features[x].feature_mask))
				opbx_set_flag(config, OPBX_BRIDGE_DTMF_CHANNEL_1);
		}
	}
	
	if (chan && peer && !(opbx_test_flag(config, OPBX_BRIDGE_DTMF_CHANNEL_0) && opbx_test_flag(config, OPBX_BRIDGE_DTMF_CHANNEL_1))) {
		char *dynamic_features;

		dynamic_features = pbx_builtin_getvar_helper(chan, "DYNAMIC_FEATURES");

		if (dynamic_features) {
			char *tmp = opbx_strdupa(dynamic_features);
			char *tok;
			struct opbx_call_feature *feature;

			if (!tmp) {
				return;
			}

			/* while we have a feature */
			while (NULL != (tok = strsep(&tmp, "#"))) {
				if ((feature = find_feature(tok))) {
					if (opbx_test_flag(feature, OPBX_FEATURE_FLAG_NEEDSDTMF)) {
						if (opbx_test_flag(feature, OPBX_FEATURE_FLAG_CALLER))
							opbx_set_flag(config, OPBX_BRIDGE_DTMF_CHANNEL_0);
						if (opbx_test_flag(feature, OPBX_FEATURE_FLAG_CALLEE))
							opbx_set_flag(config, OPBX_BRIDGE_DTMF_CHANNEL_1);
					}
				}
			}
		}
	}
}


static struct opbx_channel *opbx_feature_request_and_dial(struct opbx_channel *caller, const char *type, int format, void *data, int timeout, int *outstate, const char *cid_num, const char *cid_name)
{
	int state = 0;
	int cause = 0;
	int to;
	struct opbx_channel *chan;
	struct opbx_channel *monitor_chans[2];
	struct opbx_channel *active_channel;
	struct opbx_frame *f = NULL;
	int res = 0, ready = 0;
	
	if ((chan = opbx_request(type, format, data, &cause))) {
		opbx_set_callerid(chan, cid_num, cid_name, cid_num);
		
		if (!opbx_call(chan, data, timeout)) {
			struct timeval started;
			int x, len = 0;
			char *disconnect_code = NULL, *dialed_code = NULL;

			opbx_indicate(caller, OPBX_CONTROL_RINGING);
			/* support dialing of the featuremap disconnect code while performing an attended tranfer */
			for (x=0; x < FEATURES_COUNT; x++) {
				if (strcasecmp(builtin_features[x].sname, "disconnect"))
					continue;

				disconnect_code = builtin_features[x].exten;
				len = strlen(disconnect_code) + 1;
				dialed_code = alloca(len);
				memset(dialed_code, 0, len);
				break;
			}
			x = 0;
			started = opbx_tvnow();
			to = timeout;
			while (!opbx_check_hangup(caller) && timeout && (chan->_state != OPBX_STATE_UP)) {
				monitor_chans[0] = caller;
				monitor_chans[1] = chan;
				active_channel = opbx_waitfor_n(monitor_chans, 2, &to);

				/* see if the timeout has been violated */
				if(opbx_tvdiff_ms(opbx_tvnow(), started) > timeout) {
					state = OPBX_CONTROL_UNHOLD;
					opbx_log(LOG_NOTICE, "We exceeded our AT-timeout\n");
					break; /*doh! timeout*/
				}

				if (!active_channel) {
					continue;
				}

				if (chan && (chan == active_channel)){
					f = opbx_read(chan);
					if (f == NULL) { /*doh! where'd he go?*/
						state = OPBX_CONTROL_HANGUP;
						res = 0;
						break;
					}
					
					if (f->frametype == OPBX_FRAME_CONTROL || f->frametype == OPBX_FRAME_DTMF || f->frametype == OPBX_FRAME_TEXT) {
						if (f->subclass == OPBX_CONTROL_RINGING) {
							state = f->subclass;
							if (option_verbose > 2)
								opbx_verbose( VERBOSE_PREFIX_3 "%s is ringing\n", chan->name);
							opbx_indicate(caller, OPBX_CONTROL_RINGING);
						} else if ((f->subclass == OPBX_CONTROL_BUSY) || (f->subclass == OPBX_CONTROL_CONGESTION)) {
							state = f->subclass;
							opbx_fr_free(f);
							f = NULL;
							break;
						} else if (f->subclass == OPBX_CONTROL_ANSWER) {
							/* This is what we are hoping for */
							state = f->subclass;
							opbx_fr_free(f);
							f = NULL;
							ready=1;
							break;
						} else {
							opbx_log(LOG_NOTICE, "Don't know what to do about control frame: %d\n", f->subclass);
						}
						/* else who cares */
					}

				} else if (caller && (active_channel == caller)) {
					f = opbx_read(caller);
					if (f == NULL) { /*doh! where'd he go?*/
						if (caller->_softhangup && !chan->_softhangup) {
							/* make this a blind transfer */
							ready = 1;
							break;
						}
						state = OPBX_CONTROL_HANGUP;
						res = 0;
						break;
					}
					
					if (f->frametype == OPBX_FRAME_DTMF) {
						dialed_code[x++] = f->subclass;
						dialed_code[x] = '\0';
						if (strlen(dialed_code) == len) {
							x = 0;
						} else if (x && strncmp(dialed_code, disconnect_code, x)) {
							x = 0;
							dialed_code[x] = '\0';
						}
						if (*dialed_code && !strcmp(dialed_code, disconnect_code)) {
							/* Caller Canceled the call */
							state = OPBX_CONTROL_UNHOLD;
							opbx_fr_free(f);
							f = NULL;
							break;
						}
					}
				}
				if (f) {
					opbx_fr_free(f);
				}
			}
		} else
			opbx_log(LOG_NOTICE, "Unable to call channel %s/%s\n", type, (char *)data);
	} else {
		opbx_log(LOG_NOTICE, "Unable to request channel %s/%s\n", type, (char *)data);
		switch(cause) {
		case OPBX_CAUSE_BUSY:
			state = OPBX_CONTROL_BUSY;
			break;
		case OPBX_CAUSE_CONGESTION:
			state = OPBX_CONTROL_CONGESTION;
			break;
		}
	}
	
	opbx_indicate(caller, -1);
	if (chan && ready) {
		if (chan->_state == OPBX_STATE_UP) 
			state = OPBX_CONTROL_ANSWER;
		res = 0;
	} else if(chan) {
		res = -1;
		opbx_hangup(chan);
		chan = NULL;
	} else {
		res = -1;
	}
	
	if (outstate)
		*outstate = state;

	if (chan && res <= 0) {
		if (!chan->cdr) {
			chan->cdr = opbx_cdr_alloc();
		}
		if (chan->cdr) {
			char tmp[256];
			opbx_cdr_init(chan->cdr, chan);
			snprintf(tmp, 256, "%s/%s", type, (char *)data);
			opbx_cdr_setapp(chan->cdr,"Dial",tmp);
			opbx_cdr_update(chan);
			opbx_cdr_start(chan->cdr);
			opbx_cdr_end(chan->cdr);
			/* If the cause wasn't handled properly */
			if (opbx_cdr_disposition(chan->cdr,chan->hangupcause))
				opbx_cdr_failed(chan->cdr);
		} else {
			opbx_log(LOG_WARNING, "Unable to create Call Detail Record\n");
		}
	}
	
	return chan;
}

static int __opbx_bridge_call(struct opbx_channel *chan,struct opbx_channel *peer,struct opbx_bridge_config *config)
{
	/* Copy voice back and forth between the two channels.  Give the peer
	   the ability to transfer calls with '#<extension' syntax. */
	struct opbx_frame *f;
	struct opbx_channel *who;
	char chan_featurecode[FEATURE_MAX_LEN + 1]="";
	char peer_featurecode[FEATURE_MAX_LEN + 1]="";
	int res;
	int diff;
	int hasfeatures=0;
	int hadfeatures=0;
	struct opbx_option_header *aoh;
	struct timeval start = { 0 , 0 };
	struct opbx_bridge_config backup_config;
	int allowdisconnect_in, allowdisconnect_out, allowredirect_in, allowredirect_out;
	char *monitor_exec;

	memset(&backup_config, 0, sizeof(backup_config));

	config->start_time = opbx_tvnow();

	if (chan && peer) {
		pbx_builtin_setvar_helper(chan, "BRIDGEPEER", peer->name);
		pbx_builtin_setvar_helper(peer, "BRIDGEPEER", chan->name);
	} else if (chan)
		pbx_builtin_setvar_helper(chan, "BLINDTRANSFER", NULL);

	if (monitor_ok) {
		if (!monitor_app) { 
			if (!(monitor_app = pbx_findapp("Monitor")))
				monitor_ok=0;
		}
		if ((monitor_exec = pbx_builtin_getvar_helper(chan, "AUTO_MONITOR"))) 
			pbx_exec(chan, monitor_app, monitor_exec, 1);
		else if ((monitor_exec = pbx_builtin_getvar_helper(peer, "AUTO_MONITOR")))
			pbx_exec(peer, monitor_app, monitor_exec, 1);
	}
	
	allowdisconnect_in = opbx_test_flag(&(config->features_callee), OPBX_FEATURE_DISCONNECT);
	allowdisconnect_out = opbx_test_flag(&(config->features_caller), OPBX_FEATURE_DISCONNECT);
	allowredirect_in = opbx_test_flag(&(config->features_callee), OPBX_FEATURE_REDIRECT);
	allowredirect_out = opbx_test_flag(&(config->features_caller), OPBX_FEATURE_REDIRECT);
	set_config_flags(chan, peer, config);
	config->firstpass = 1;

	/* Answer if need be */
	if (opbx_answer(chan))
		return -1;
	peer->appl = "Bridged Call";
	peer->data = chan->name;

	/* copy the userfield from the B-leg to A-leg if applicable */
	if (chan->cdr && peer->cdr && !opbx_strlen_zero(peer->cdr->userfield)) {
		char tmp[256];
		if (!opbx_strlen_zero(chan->cdr->userfield)) {
			snprintf(tmp, sizeof(tmp), "%s;%s", chan->cdr->userfield, peer->cdr->userfield);
			opbx_cdr_appenduserfield(chan, tmp);
		} else
			opbx_cdr_setuserfield(chan, peer->cdr->userfield);
		/* free the peer's cdr without opbx_cdr_free complaining */
		free(peer->cdr);
		peer->cdr = NULL;
	}
	for (;;) {
		if (config->feature_timer)
			start = opbx_tvnow();

		res = opbx_channel_bridge(chan, peer, config, &f, &who);

		if (config->feature_timer) {
			/* Update time limit for next pass */
			diff = opbx_tvdiff_ms(opbx_tvnow(), start);
			config->feature_timer -= diff;
			if (hasfeatures) {
				/* Running on backup config, meaning a feature might be being
				   activated, but that's no excuse to keep things going 
				   indefinitely! */
				if (backup_config.feature_timer && ((backup_config.feature_timer -= diff) <= 0)) {
					opbx_log(LOG_DEBUG, "Timed out, realtime this time!\n");
					config->feature_timer = 0;
					who = chan;
					if (f)
						opbx_fr_free(f);
					f = NULL;
					res = 0;
				} else if (config->feature_timer <= 0) {
					/* Not *really* out of time, just out of time for
					   digits to come in for features. */
					opbx_log(LOG_DEBUG, "Timed out for feature!\n");
					if (!opbx_strlen_zero(peer_featurecode)) {
						opbx_dtmf_stream(chan, peer, peer_featurecode, 0);
						memset(peer_featurecode, 0, sizeof(peer_featurecode));
					}
					if (!opbx_strlen_zero(chan_featurecode)) {
						opbx_dtmf_stream(peer, chan, chan_featurecode, 0);
						memset(chan_featurecode, 0, sizeof(chan_featurecode));
					}
					if (f)
						opbx_fr_free(f);
					hasfeatures = !opbx_strlen_zero(chan_featurecode) || !opbx_strlen_zero(peer_featurecode);
					if (!hasfeatures) {
						/* Restore original (possibly time modified) bridge config */
						memcpy(config, &backup_config, sizeof(struct opbx_bridge_config));
						memset(&backup_config, 0, sizeof(backup_config));
					}
					hadfeatures = hasfeatures;
					/* Continue as we were */
					continue;
				}
			} else {
				if (config->feature_timer <=0) {
					/* We ran out of time */
					config->feature_timer = 0;
					who = chan;
					if (f)
						opbx_fr_free(f);
					f = NULL;
					res = 0;
				}
			}
		}
		if (res < 0) {
			opbx_log(LOG_WARNING, "Bridge failed on channels %s and %s\n", chan->name, peer->name);
			return -1;
		}
		
		if (!f || ((f->frametype == OPBX_FRAME_CONTROL) && ((f->subclass == OPBX_CONTROL_HANGUP) || (f->subclass == OPBX_CONTROL_BUSY) || 
															(f->subclass == OPBX_CONTROL_CONGESTION)))) {
			res = -1;
			break;
		}
		if ((f->frametype == OPBX_FRAME_CONTROL) && (f->subclass == OPBX_CONTROL_RINGING)) {
			if (who == chan)
				opbx_indicate(peer, OPBX_CONTROL_RINGING);
			else
				opbx_indicate(chan, OPBX_CONTROL_RINGING);
		}
		if ((f->frametype == OPBX_FRAME_CONTROL) && (f->subclass == -1)) {
			if (who == chan)
				opbx_indicate(peer, -1);
			else
				opbx_indicate(chan, -1);
		}
		if ((f->frametype == OPBX_FRAME_CONTROL) && (f->subclass == OPBX_CONTROL_FLASH)) {
			if (who == chan)
				opbx_indicate(peer, OPBX_CONTROL_FLASH);
			else
				opbx_indicate(chan, OPBX_CONTROL_FLASH);
		}
		if ((f->frametype == OPBX_FRAME_CONTROL) && (f->subclass == OPBX_CONTROL_OPTION)) {
			aoh = f->data;
			/* Forward option Requests */
			if (aoh && (aoh->flag == OPBX_OPTION_FLAG_REQUEST)) {
				if (who == chan)
					opbx_channel_setoption(peer, ntohs(aoh->option), aoh->data, f->datalen - sizeof(struct opbx_option_header), 0);
				else
					opbx_channel_setoption(chan, ntohs(aoh->option), aoh->data, f->datalen - sizeof(struct opbx_option_header), 0);
			}
		}
		/* check for '*', if we find it it's time to disconnect */
		if (f && (f->frametype == OPBX_FRAME_DTMF)) {
			char *featurecode;
			int sense;
			struct opbx_channel *other;

			hadfeatures = hasfeatures;
			/* This cannot overrun because the longest feature is one shorter than our buffer */
			if (who == chan) {
				other = peer;
				sense = FEATURE_SENSE_CHAN;
				featurecode = chan_featurecode;
			} else  {
				other = chan;
				sense = FEATURE_SENSE_PEER;
				featurecode = peer_featurecode;
			}
			featurecode[strlen(featurecode)] = f->subclass;
			config->feature_timer = backup_config.feature_timer;
			res = opbx_feature_interpret(chan, peer, config, featurecode, sense);
			switch(res) {
			case FEATURE_RETURN_PASSDIGITS:
				opbx_dtmf_stream(other, who, featurecode, 0);
				/* Fall through */
			case FEATURE_RETURN_SUCCESS:
				memset(featurecode, 0, sizeof(chan_featurecode));
				break;
			}
			if (res >= FEATURE_RETURN_PASSDIGITS) {
				res = 0;
			} else {
				opbx_fr_free(f);
				break;
			}
			hasfeatures = !opbx_strlen_zero(chan_featurecode) || !opbx_strlen_zero(peer_featurecode);
			if (hadfeatures && !hasfeatures) {
				/* Restore backup */
				memcpy(config, &backup_config, sizeof(struct opbx_bridge_config));
				memset(&backup_config, 0, sizeof(struct opbx_bridge_config));
			} else if (hasfeatures) {
				if (!hadfeatures) {
					/* Backup configuration */
					memcpy(&backup_config, config, sizeof(struct opbx_bridge_config));
					/* Setup temporary config options */
					config->play_warning = 0;
					opbx_clear_flag(&(config->features_caller), OPBX_FEATURE_PLAY_WARNING);
					opbx_clear_flag(&(config->features_callee), OPBX_FEATURE_PLAY_WARNING);
					config->warning_freq = 0;
					config->warning_sound = NULL;
					config->end_sound = NULL;
					config->start_sound = NULL;
					config->firstpass = 0;
				}
				config->feature_timer = featuredigittimeout;
				opbx_log(LOG_DEBUG, "Set time limit to %ld\n", config->feature_timer);
			}
		}
		if (f)
			opbx_fr_free(f);
	}
	return res;
}

static void *do_parking_thread(void *ignore)
{
	int ms, tms, max;
	struct parkeduser *pu, *pl, *pt = NULL;
	struct timeval tv;
	struct opbx_frame *f;
	char exten[OPBX_MAX_EXTENSION];
	char *peername,*cp;
	char returnexten[OPBX_MAX_EXTENSION];
	struct opbx_context *con;
	int x;
	fd_set rfds, efds;
	fd_set nrfds, nefds;
	FD_ZERO(&rfds);
	FD_ZERO(&efds);

	for (;;) {
		ms = -1;
		max = -1;
		opbx_mutex_lock(&parking_lock);
		pl = NULL;
		pu = parkinglot;
		FD_ZERO(&nrfds);
		FD_ZERO(&nefds);
		while(pu) {
			if (pu->notquiteyet) {
				/* Pretend this one isn't here yet */
				pl = pu;
				pu = pu->next;
				continue;
			}
			tms = opbx_tvdiff_ms(opbx_tvnow(), pu->start);
			if (tms > pu->parkingtime) {
				/* Stop music on hold */
				opbx_moh_stop(pu->chan);
				opbx_indicate(pu->chan, OPBX_CONTROL_UNHOLD);
				/* Get chan, exten from derived kludge */
				if (pu->peername[0]) {
					peername = opbx_strdupa(pu->peername);
					cp = strrchr(peername, '-');
					if (cp) 
						*cp = 0;
					con = opbx_context_find(parking_con_dial);
					if (!con) {
						con = opbx_context_create(NULL, parking_con_dial, registrar);
						if (!con) {
							opbx_log(LOG_ERROR, "Parking dial context '%s' does not exist and unable to create\n", parking_con_dial);
						}
					}
					if (con) {
						snprintf(returnexten, sizeof(returnexten), "%s||t", peername);
						opbx_add_extension2(con, 1, peername, 1, NULL, NULL, "Dial", strdup(returnexten), FREE, registrar);
					}
					opbx_copy_string(pu->chan->exten, peername, sizeof(pu->chan->exten));
					opbx_copy_string(pu->chan->context, parking_con_dial, sizeof(pu->chan->context));
					pu->chan->priority = 1;

				} else {
					/* They've been waiting too long, send them back to where they came.  Theoretically they
					   should have their original extensions and such, but we copy to be on the safe side */
					opbx_copy_string(pu->chan->exten, pu->exten, sizeof(pu->chan->exten));
					opbx_copy_string(pu->chan->context, pu->context, sizeof(pu->chan->context));
					pu->chan->priority = pu->priority;
				}

				manager_event(EVENT_FLAG_CALL, "ParkedCallTimeOut",
					"Exten: %d\r\n"
					"Channel: %s\r\n"
					"CallerID: %s\r\n"
					"CallerIDName: %s\r\n\r\n"
					,pu->parkingnum, pu->chan->name
					,(pu->chan->cid.cid_num ? pu->chan->cid.cid_num : "<unknown>")
					,(pu->chan->cid.cid_name ? pu->chan->cid.cid_name : "<unknown>")
					);

				if (option_verbose > 1) 
					opbx_verbose(VERBOSE_PREFIX_2 "Timeout for %s parked on %d. Returning to %s,%s,%d\n", pu->chan->name, pu->parkingnum, pu->chan->context, pu->chan->exten, pu->chan->priority);
				/* Start up the PBX, or hang them up */
				if (opbx_pbx_start(pu->chan))  {
					opbx_log(LOG_WARNING, "Unable to restart the PBX for user on '%s', hanging them up...\n", pu->chan->name);
					opbx_hangup(pu->chan);
				}
				/* And take them out of the parking lot */
				if (pl) 
					pl->next = pu->next;
				else
					parkinglot = pu->next;
				pt = pu;
				pu = pu->next;
				con = opbx_context_find(parking_con);
				if (con) {
					snprintf(exten, sizeof(exten), "%d", pt->parkingnum);
					if (opbx_context_remove_extension2(con, exten, 1, NULL))
						opbx_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
				} else
					opbx_log(LOG_WARNING, "Whoa, no parking context?\n");
				free(pt);
			} else {
				for (x = 0; x < OPBX_MAX_FDS; x++) {
					if ((pu->chan->fds[x] > -1) && (FD_ISSET(pu->chan->fds[x], &rfds) || FD_ISSET(pu->chan->fds[x], &efds))) {
						if (FD_ISSET(pu->chan->fds[x], &efds))
							opbx_set_flag(pu->chan, OPBX_FLAG_EXCEPTION);
						else
							opbx_clear_flag(pu->chan, OPBX_FLAG_EXCEPTION);
						pu->chan->fdno = x;
						/* See if they need servicing */
						f = opbx_read(pu->chan);
						if (!f || ((f->frametype == OPBX_FRAME_CONTROL) && (f->subclass ==  OPBX_CONTROL_HANGUP))) {

							manager_event(EVENT_FLAG_CALL, "ParkedCallGiveUp",
								"Exten: %d\r\n"
								"Channel: %s\r\n"
								"CallerID: %s\r\n"
								"CallerIDName: %s\r\n\r\n"
								,pu->parkingnum, pu->chan->name
								,(pu->chan->cid.cid_num ? pu->chan->cid.cid_num : "<unknown>")
								,(pu->chan->cid.cid_name ? pu->chan->cid.cid_name : "<unknown>")
								);

							/* There's a problem, hang them up*/
							if (option_verbose > 1) 
								opbx_verbose(VERBOSE_PREFIX_2 "%s got tired of being parked\n", pu->chan->name);
							opbx_hangup(pu->chan);
							/* And take them out of the parking lot */
							if (pl) 
								pl->next = pu->next;
							else
								parkinglot = pu->next;
							pt = pu;
							pu = pu->next;
							con = opbx_context_find(parking_con);
							if (con) {
								snprintf(exten, sizeof(exten), "%d", pt->parkingnum);
								if (opbx_context_remove_extension2(con, exten, 1, NULL))
									opbx_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
							} else
								opbx_log(LOG_WARNING, "Whoa, no parking context?\n");
							free(pt);
							break;
						} else {
							/* XXX Maybe we could do something with packets, like dial "0" for operator or something XXX */
							opbx_fr_free(f);
							if (pu->moh_trys < 3 && !opbx_generator_is_active(pu->chan)) {
								opbx_log(LOG_DEBUG, "MOH on parked call stopped by outside source.  Restarting.\n");
								opbx_moh_start(pu->chan, NULL);
								pu->moh_trys++;
							}
							goto std;	/* XXX Ick: jumping into an else statement??? XXX */
						}
					}
				}
				if (x >= OPBX_MAX_FDS) {
std:					for (x=0; x<OPBX_MAX_FDS; x++) {
						/* Keep this one for next one */
						if (pu->chan->fds[x] > -1) {
							FD_SET(pu->chan->fds[x], &nrfds);
							FD_SET(pu->chan->fds[x], &nefds);
							if (pu->chan->fds[x] > max)
								max = pu->chan->fds[x];
						}
					}
					/* Keep track of our longest wait */
					if ((tms < ms) || (ms < 0))
						ms = tms;
					pl = pu;
					pu = pu->next;
				}
			}
		}
		opbx_mutex_unlock(&parking_lock);
		rfds = nrfds;
		efds = nefds;
		tv = opbx_samp2tv(ms, 1000);
		/* Wait for something to happen */
		opbx_select(max + 1, &rfds, NULL, &efds, (ms > -1) ? &tv : NULL);
		pthread_testcancel();
	}
	return NULL;	/* Never reached */
}

static int park_call_exec(struct opbx_channel *chan, void *data)
{
	/* Data is unused at the moment but could contain a parking
	   lot context eventually */
	int res=0;
	struct localuser *u;
	LOCAL_USER_ADD(u);
	/* Setup the exten/priority to be s/1 since we don't know
	   where this call should return */
	strcpy(chan->exten, "s");
	chan->priority = 1;
	if (chan->_state != OPBX_STATE_UP)
		res = opbx_answer(chan);
	if (!res)
		res = opbx_safe_sleep(chan, 1000);
	if (!res)
		res = __opbx_park_call(chan, chan, 0, NULL);
	LOCAL_USER_REMOVE(u);
	if (!res)
		res = OPBX_PBX_KEEPALIVE;
	return res;
}

static int park_exec(struct opbx_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	struct opbx_channel *peer=NULL;
	struct parkeduser *pu, *pl=NULL;
	char exten[OPBX_MAX_EXTENSION];
	struct opbx_context *con;
	int park;
	int dres;
	struct opbx_bridge_config config;

	if (!data) {
		opbx_log(LOG_WARNING, "Park requires an argument (extension number)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	park = atoi((char *)data);
	opbx_mutex_lock(&parking_lock);
	pu = parkinglot;
	while(pu) {
		if (pu->parkingnum == park) {
			if (pl)
				pl->next = pu->next;
			else
				parkinglot = pu->next;
			break;
		}
		pl = pu;
		pu = pu->next;
	}
	opbx_mutex_unlock(&parking_lock);
	if (pu) {
		peer = pu->chan;
		con = opbx_context_find(parking_con);
		if (con) {
			snprintf(exten, sizeof(exten), "%d", pu->parkingnum);
			if (opbx_context_remove_extension2(con, exten, 1, NULL))
				opbx_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
		} else
			opbx_log(LOG_WARNING, "Whoa, no parking context?\n");

		manager_event(EVENT_FLAG_CALL, "UnParkedCall",
			"Exten: %d\r\n"
			"Channel: %s\r\n"
			"From: %s\r\n"
			"CallerID: %s\r\n"
			"CallerIDName: %s\r\n\r\n"
			,pu->parkingnum, pu->chan->name, chan->name
			,(pu->chan->cid.cid_num ? pu->chan->cid.cid_num : "<unknown>")
			,(pu->chan->cid.cid_name ? pu->chan->cid.cid_name : "<unknown>")
			);

		free(pu);
	}
	/* JK02: it helps to answer the channel if not already up */
	if (chan->_state != OPBX_STATE_UP) {
		opbx_answer(chan);
	}

	if (peer) {
		/* Play a courtesy beep in the calling channel to prefix the bridge connecting */	
		if (!opbx_strlen_zero(courtesytone)) {
			if (!opbx_streamfile(chan, courtesytone, chan->language)) {
				if (opbx_waitstream(chan, "") < 0) {
					opbx_log(LOG_WARNING, "Failed to play courtesy tone!\n");
					opbx_hangup(peer);
					return -1;
				}
			}
		}
 
		opbx_moh_stop(peer);
		opbx_indicate(peer, OPBX_CONTROL_UNHOLD);
		res = opbx_channel_make_compatible(chan, peer);
		if (res < 0) {
			opbx_log(LOG_WARNING, "Could not make channels %s and %s compatible for bridge\n", chan->name, peer->name);
			opbx_hangup(peer);
			return -1;
		}
		/* This runs sorta backwards, since we give the incoming channel control, as if it
		   were the person called. */
		if (option_verbose > 2) 
			opbx_verbose(VERBOSE_PREFIX_3 "Channel %s connected to parked call %d\n", chan->name, park);

		memset(&config, 0, sizeof(struct opbx_bridge_config));
		opbx_set_flag(&(config.features_callee), OPBX_FEATURE_REDIRECT);
		opbx_set_flag(&(config.features_caller), OPBX_FEATURE_REDIRECT);
		config.timelimit = 0;
		config.play_warning = 0;
		config.warning_freq = 0;
		config.warning_sound=NULL;
		res = __opbx_bridge_call(chan, peer, &config);

		/* Simulate the PBX hanging up */
		if (res != OPBX_PBX_NO_HANGUP_PEER)
			opbx_hangup(peer);
		return res;
	} else {
		/* XXX Play a message XXX */
		dres = opbx_streamfile(chan, "pbx-invalidpark", chan->language);
		if (!dres)
	    		dres = opbx_waitstream(chan, "");
		else {
			opbx_log(LOG_WARNING, "opbx_streamfile of %s failed on %s\n", "pbx-invalidpark", chan->name);
			dres = 0;
		}
		if (option_verbose > 2) 
			opbx_verbose(VERBOSE_PREFIX_3 "Channel %s tried to talk to nonexistent parked call %d\n", chan->name, park);
		res = -1;
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

static int handle_showfeatures(int fd, int argc, char *argv[])
{
	int i;
	int fcount;
	struct opbx_call_feature *feature;
	char format[] = "%-25s %-7s %-7s\n";

	opbx_cli(fd, format, "Builtin Feature", "Default", "Current");
	opbx_cli(fd, format, "---------------", "-------", "-------");

	opbx_cli(fd, format, "Pickup", "*8", __opbx_pickup_ext());		/* default hardcoded above, so we'll hardcode it here */

	fcount = sizeof(builtin_features) / sizeof(builtin_features[0]);

	for (i = 0; i < fcount; i++)
		{
			opbx_cli(fd, format, builtin_features[i].fname, builtin_features[i].default_exten, builtin_features[i].exten);
		}
	opbx_cli(fd, "\n");
	opbx_cli(fd, format, "Dynamic Feature", "Default", "Current");
	opbx_cli(fd, format, "---------------", "-------", "-------");
	if (OPBX_LIST_EMPTY(&feature_list)) {
		opbx_cli(fd, "(none)\n");
	}
	else {
		OPBX_LIST_LOCK(&feature_list);
		OPBX_LIST_TRAVERSE(&feature_list, feature, feature_entry) {
			opbx_cli(fd, format, feature->sname, "no def", feature->exten);	
		}
		OPBX_LIST_UNLOCK(&feature_list);
	}
	opbx_cli(fd, "\nCall parking\n");
	opbx_cli(fd, "------------\n");
	opbx_cli(fd,"%-20s:	%s\n", "Parking extension", parking_ext);
	opbx_cli(fd,"%-20s:	%s\n", "Parking context", parking_con);
	opbx_cli(fd,"%-20s:	%d-%d\n", "Parked call extensions", parking_start, parking_stop);
	opbx_cli(fd,"\n");
	
	return RESULT_SUCCESS;
}

static char showfeatures_help[] =
"Usage: show features\n"
"       Lists currently configured features.\n";

static struct opbx_cli_entry showfeatures =
	{ { "show", "features", NULL }, handle_showfeatures, "Lists configured features", showfeatures_help };

static int handle_parkedcalls(int fd, int argc, char *argv[])
{
	struct parkeduser *cur;
	int numparked = 0;

	opbx_cli(fd, "%4s %25s (%-15s %-12s %-4s) %-6s \n", "Num", "Channel"
		, "Context", "Extension", "Pri", "Timeout");

	opbx_mutex_lock(&parking_lock);

	cur = parkinglot;
	while(cur) {
		opbx_cli(fd, "%4d %25s (%-15s %-12s %-4d) %6lds\n"
			,cur->parkingnum, cur->chan->name, cur->context, cur->exten
			,cur->priority, cur->start.tv_sec + (cur->parkingtime/1000) - time(NULL));

		cur = cur->next;
		numparked++;
	}
	opbx_cli(fd, "%d parked call%s.\n", numparked, (numparked != 1) ? "s" : "");

	opbx_mutex_unlock(&parking_lock);

	return RESULT_SUCCESS;
}

static char showparked_help[] =
"Usage: show parkedcalls\n"
"       Lists currently parked calls.\n";

static struct opbx_cli_entry showparked =
{ { "show", "parkedcalls", NULL }, handle_parkedcalls, "Lists parked calls", showparked_help };

/* Dump lot status */
static int manager_parking_status( struct mansession *s, struct message *m )
{
	struct parkeduser *cur;
	char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";

	if (id && !opbx_strlen_zero(id))
		snprintf(idText,256,"ActionID: %s\r\n",id);

	astman_send_ack(s, m, "Parked calls will follow");

        opbx_mutex_lock(&parking_lock);

        cur=parkinglot;
        while(cur) {
			opbx_cli(s->fd, "Event: ParkedCall\r\n"
			"Exten: %d\r\n"
			"Channel: %s\r\n"
			"Timeout: %ld\r\n"
			"CallerID: %s\r\n"
			"CallerIDName: %s\r\n"
			"%s"
			"\r\n"
                        ,cur->parkingnum, cur->chan->name
                        ,(long)cur->start.tv_sec + (long)(cur->parkingtime/1000) - (long)time(NULL)
			,(cur->chan->cid.cid_num ? cur->chan->cid.cid_num : "")
			,(cur->chan->cid.cid_name ? cur->chan->cid.cid_name : "")
			,idText);

            cur = cur->next;
        }

	opbx_cli(s->fd,
	"Event: ParkedCallsComplete\r\n"
	"%s"
	"\r\n",idText);

        opbx_mutex_unlock(&parking_lock);

        return RESULT_SUCCESS;
}


int __opbx_pickup_call(struct opbx_channel *chan)
{
	struct opbx_channel *cur = NULL;
	int res = -1;

	while ( (cur = opbx_channel_walk_locked(cur)) != NULL) {
		if (!cur->pbx && 
			(cur != chan) &&
			(chan->pickupgroup & cur->callgroup) &&
			((cur->_state == OPBX_STATE_RINGING) ||
			 (cur->_state == OPBX_STATE_RING))) {
			break;
		}
		opbx_mutex_unlock(&cur->lock);
	}
	if (cur) {
		if (option_debug)
			opbx_log(LOG_DEBUG, "Call pickup on chan '%s' by '%s'\n",cur->name, chan->name);
		res = opbx_answer(chan);
		if (res)
			opbx_log(LOG_WARNING, "Unable to answer '%s'\n", chan->name);
		res = opbx_queue_control(chan, OPBX_CONTROL_ANSWER);
		if (res)
			opbx_log(LOG_WARNING, "Unable to queue answer on '%s'\n", chan->name);
		res = opbx_channel_masquerade(cur, chan);
		if (res)
			opbx_log(LOG_WARNING, "Unable to masquerade '%s' into '%s'\n", chan->name, cur->name);		/* Done */
		opbx_mutex_unlock(&cur->lock);
	} else	{
		if (option_debug)
			opbx_log(LOG_DEBUG, "No call pickup possible...\n");
	}
	return res;
}

static int load_config(void) 
{
	int start = 0, end = 0;
	struct opbx_context *con = NULL;
	struct opbx_config *cfg = NULL;
	struct opbx_variable *var = NULL;
	char old_parking_ext[OPBX_MAX_EXTENSION] = "";
	char old_parking_con[OPBX_MAX_EXTENSION] = "";

	if (!opbx_strlen_zero(parking_con)) {
		strcpy(old_parking_ext, parking_ext);
		strcpy(old_parking_con, parking_con);
	} 

	/* Reset to defaults */
	strcpy(parking_con, "parkedcalls");
	strcpy(parking_con_dial, "park-dial");
	strcpy(parking_ext, "700");
	strcpy(pickup_ext, "*8");
	courtesytone[0] = '\0';
	strcpy(xfersound, "beep");
	strcpy(xferfailsound, "pbx-invalid");
	parking_start = 701;
	parking_stop = 750;
	parkfindnext = 0;

	transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;
	featuredigittimeout = DEFAULT_FEATURE_DIGIT_TIMEOUT;

	cfg = opbx_config_load("features.conf");
	if (!cfg) {
		cfg = opbx_config_load("parking.conf");
		if (cfg)
			opbx_log(LOG_NOTICE, "parking.conf is deprecated in favor of 'features.conf'.  Please rename it.\n");
	}
	if (cfg) {
		var = opbx_variable_browse(cfg, "general");
		while(var) {
			if (!strcasecmp(var->name, "parkext")) {
				opbx_copy_string(parking_ext, var->value, sizeof(parking_ext));
			} else if (!strcasecmp(var->name, "context")) {
				opbx_copy_string(parking_con, var->value, sizeof(parking_con));
			} else if (!strcasecmp(var->name, "parkingtime")) {
				if ((sscanf(var->value, "%d", &parkingtime) != 1) || (parkingtime < 1)) {
					opbx_log(LOG_WARNING, "%s is not a valid parkingtime\n", var->value);
					parkingtime = DEFAULT_PARK_TIME;
				} else
					parkingtime = parkingtime * 1000;
			} else if (!strcasecmp(var->name, "parkpos")) {
				if (sscanf(var->value, "%d-%d", &start, &end) != 2) {
					opbx_log(LOG_WARNING, "Format for parking positions is a-b, where a and b are numbers at line %d of parking.conf\n", var->lineno);
				} else {
					parking_start = start;
					parking_stop = end;
				}
			} else if (!strcasecmp(var->name, "findslot")) {
				parkfindnext = (!strcasecmp(var->value, "next"));
			} else if (!strcasecmp(var->name, "adsipark")) {
				adsipark = opbx_true(var->value);
			} else if (!strcasecmp(var->name, "transferdigittimeout")) {
				if ((sscanf(var->value, "%d", &transferdigittimeout) != 1) || (transferdigittimeout < 1)) {
					opbx_log(LOG_WARNING, "%s is not a valid transferdigittimeout\n", var->value);
					transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;
				} else
					transferdigittimeout = transferdigittimeout * 1000;
			} else if (!strcasecmp(var->name, "featuredigittimeout")) {
				if ((sscanf(var->value, "%d", &featuredigittimeout) != 1) || (featuredigittimeout < 1)) {
					opbx_log(LOG_WARNING, "%s is not a valid featuredigittimeout\n", var->value);
					featuredigittimeout = DEFAULT_FEATURE_DIGIT_TIMEOUT;
				}
			} else if (!strcasecmp(var->name, "courtesytone")) {
				opbx_copy_string(courtesytone, var->value, sizeof(courtesytone));
			} else if (!strcasecmp(var->name, "xfersound")) {
				opbx_copy_string(xfersound, var->value, sizeof(xfersound));
			} else if (!strcasecmp(var->name, "xferfailsound")) {
				opbx_copy_string(xferfailsound, var->value, sizeof(xferfailsound));
			} else if (!strcasecmp(var->name, "pickupexten")) {
				opbx_copy_string(pickup_ext, var->value, sizeof(pickup_ext));
			}
			var = var->next;
		}

		unmap_features();
		var = opbx_variable_browse(cfg, "featuremap");
		while(var) {
			if (remap_feature(var->name, var->value))
				opbx_log(LOG_NOTICE, "Unknown feature '%s'\n", var->name);
			var = var->next;
		}

		/* Map a key combination to an application*/
		__opbx_unregister_features();
		var = opbx_variable_browse(cfg, "applicationmap");
		while(var) {
			char *tmp_val=strdup(var->value);
			char *exten, *party=NULL, *app=NULL, *app_args=NULL; 

			if (!tmp_val) { 
				opbx_log(LOG_ERROR, "res_features: strdup failed\n");
				continue;
			}
			

			exten=strsep(&tmp_val,",");
			if (exten) party=strsep(&tmp_val,",");
			if (party) app=strsep(&tmp_val,",");

			if (app) app_args=strsep(&tmp_val,",");

			if (!(app && strlen(app)) || !(exten && strlen(exten)) || !(party && strlen(party)) || !(var->name && strlen(var->name))) {
				opbx_log(LOG_NOTICE, "Please check the feature Mapping Syntax, either extension, name, or app aren't provided %s %s %s %s\n",app,exten,party,var->name);
				free(tmp_val);
				var = var->next;
				continue;
			}

			{
				struct opbx_call_feature *feature=find_feature(var->name);
				int mallocd=0;
				
				if (!feature) {
					feature=malloc(sizeof(struct opbx_call_feature));
					mallocd=1;
				}
				if (!feature) {
					opbx_log(LOG_NOTICE, "Malloc failed at feature mapping\n");
					free(tmp_val);
					var = var->next;
					continue;
				}

				memset(feature,0,sizeof(struct opbx_call_feature));
				opbx_copy_string(feature->sname,var->name,FEATURE_SNAME_LEN);
				opbx_copy_string(feature->app,app,FEATURE_APP_LEN);
				opbx_copy_string(feature->exten, exten,FEATURE_EXTEN_LEN);
				free(tmp_val);
				
				if (app_args) 
					opbx_copy_string(feature->app_args,app_args,FEATURE_APP_ARGS_LEN);
				
				opbx_copy_string(feature->exten, exten,sizeof(feature->exten));
				feature->operation=feature_exec_app;
				opbx_set_flag(feature,OPBX_FEATURE_FLAG_NEEDSDTMF);
				
				if (!strcasecmp(party,"caller"))
					opbx_set_flag(feature,OPBX_FEATURE_FLAG_CALLER);
				else if (!strcasecmp(party, "callee"))
					opbx_set_flag(feature,OPBX_FEATURE_FLAG_CALLEE);
				else {
					opbx_log(LOG_NOTICE, "Invalid party specification for feature '%s', must be caller, or callee\n", var->name);
					var = var->next;
					continue;
				}

				__opbx_register_feature(feature);
				
				if (option_verbose >=1) opbx_verbose(VERBOSE_PREFIX_2 "Mapping Feature '%s' to app '%s' with code '%s'\n", var->name, app, exten);  
			}
			var = var->next;
		}	 
	}
	opbx_config_destroy(cfg);

	/* Remove the old parking extension */
	if (!opbx_strlen_zero(old_parking_con) && (con = opbx_context_find(old_parking_con)))   {
		opbx_context_remove_extension2(con, old_parking_ext, 1, registrar);
		opbx_log(LOG_DEBUG, "Removed old parking extension %s@%s\n", old_parking_ext, old_parking_con);
	}

	if (!(con = opbx_context_find(parking_con))) {
		if (!(con = opbx_context_create(NULL, parking_con, registrar))) {
			opbx_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", parking_con);
			return -1;
		}
	}
	return opbx_add_extension2(con, 1, __opbx_parking_ext(), 1, NULL, NULL, parkcall, strdup(""), FREE, registrar);
}

int reload(void) {
	return load_config();
}

int load_module(void)
{
	int res;
	
	OPBX_LIST_HEAD_INIT(&feature_list);
	memset(parking_ext, 0, sizeof(parking_ext));
	memset(parking_con, 0, sizeof(parking_con));

	if ((res = load_config()))
		return res;
	opbx_cli_register(&showparked);
	opbx_cli_register(&showfeatures);
	opbx_pthread_create(&parking_thread, NULL, do_parking_thread, NULL);
	res = opbx_register_application(parkedcall, park_exec, synopsis, descrip);
	if (!res)
		res = opbx_register_application(parkcall, park_call_exec, synopsis2, descrip2);
	if (!res) {
		opbx_manager_register("ParkedCalls", 0, manager_parking_status, "List parked calls" );
	}
	/* Install our functions into stubs */
	opbx_park_call = __opbx_park_call;
	opbx_masq_park_call = __opbx_masq_park_call;
	opbx_parking_ext = __opbx_parking_ext;
	opbx_pickup_ext = __opbx_pickup_ext;
	opbx_bridge_call = __opbx_bridge_call;
	opbx_pickup_call = __opbx_pickup_call;
	opbx_register_feature = __opbx_register_feature;
	opbx_unregister_feature = __opbx_unregister_feature;

	return res;
}


int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;

	opbx_manager_unregister("ParkedCalls");
	opbx_cli_unregister(&showfeatures);
	opbx_cli_unregister(&showparked);
	opbx_unregister_application(parkcall);
	return opbx_unregister_application(parkedcall);
}

char *description(void)
{
	return "Call Features Resource";
}

int usecount(void)
{
	/* Never allow parking to be unloaded because it will
	   unresolve needed symbols in the dialer */
#if 0
	int res;
	STANDARD_USECOUNT(res);
	return res;
#else
	return 1;
#endif
}
