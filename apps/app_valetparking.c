/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * Valet Parking derived from original asterisk Parking
 * Copyright (C) 2004, Anthony Minessale
 * Anthony Minessale <anthmct@yahoo.com>
 *
 * Come To ClueCon Aug-3-5 (http://www.cluecon.com)
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>

#include <pthread.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision: 2643 $")

#include "callweaver/lock.h"
#include "callweaver/utils.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/options.h"
#include "callweaver/causes.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/utils.h"
#include "callweaver/say.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/features.h"
#include "callweaver/musiconhold.h"
#include "callweaver/config.h"
#include "callweaver/cli.h"
#include "callweaver/app.h"
#include "callweaver/manager.h"
#include "callweaver/devicestate.h"

#define DEFAULT_VALETPARK_TIME 45000

static struct opbx_channel *valet_request(const char *type, int format, void *data, int *cause);
static int valetparked_devicestate(void *data);

static const struct opbx_channel_tech valet_tech = {
	.type = "Valet",
	.description = "Valet Unpark Come To ClueCon Aug-3-5 (http://www.cluecon.com)",
	.requester = valet_request,
	.capabilities = OPBX_FORMAT_SLINEAR,
 	.devicestate = valetparked_devicestate,
};

static void *valetparking_app;
static void *valetparkedcall_app;
static void *valetunparkedcall_app;
static void *valetparklist_app;

static char *valetparking = "ValetParking";
static char *valetparkedcall = "ValetParkCall";
static char *valetunparkedcall = "ValetUnparkCall";
static char *valetparklist = "ValetParkList";

/* No more than 45 seconds valetparked before you do something with them */
static int valetparkingtime = DEFAULT_VALETPARK_TIME;

/* First available extension for valetparking */
static int valetparking_start = 1;

/* Lopbx available extension for valetparking */
static int valetparking_stop = 10000;

static char *vpsynopsis = "Valet Parking";

static char *vpcsynopsis = "Valet Park Call";

static char *vupsynopsis = "Valet UnPark Call";

static char *vlsynopsis = "ValetParkList";

static char *vpsyntax = "ValetParking(exten, lotname, timeout[, return_ext][, return_pri][, return_context])";
static char *vpcsyntax = "ValetParkCall(exten, lotname, timeout[, return_ext][, return_pri][, return_context])";
static char *vupsyntax = "ValetUnparkCall(exten, lotname)";
static char *vlsyntax = "ValetParkList(lotname)";

static char *vpdesc =
"Auto-Sense Valet Parking: if <exten> is not occupied, park it, if it is already parked, bridge to it.\n\n";

static char *vpcdesc =
"Park Call at <exten> in <lotname> until someone calls ValetUnparkCall on the same <exten> + <lotname>\n"
"set <exten> to 'auto' to auto-choose the slot.\n\n"
"Come To ClueCon Aug-3-5 (http://www.cluecon.com)"
;

static char *vupdesc =
"Un-Park the call at <exten> in lot <lotname> use 'fifo' or 'filo' for auto-ordered Un-Park.\n\n"
"Come To ClueCon Aug-3-5 (http://www.cluecon.com)"
;

static char *vldesc =
"Audibly list the slot number of all the calls in <lotname> press * to unpark it.\n\n"
"Come To ClueCon Aug-3-5 (http://www.cluecon.com)"
;



struct valetparkeduser {
	struct opbx_channel *chan;
	struct timeval start;
	int valetparkingnum;
	/* Where to go if our valetparking time expires */
	char context[OPBX_MAX_EXTENSION];
	char exten[OPBX_MAX_EXTENSION];
	char lotname[OPBX_MAX_EXTENSION];
	char channame[OPBX_MAX_EXTENSION];
	int priority;
	int valetparkingtime;
	int old;
	struct valetparkeduser *next;
};

static struct valetparkeduser *valetparkinglot;

OPBX_MUTEX_DEFINE_STATIC(valetparking_lock);

static pthread_t valetparking_thread;

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int valetparking_count(void)
{
	struct valetparkeduser *cur;
	int x=0;
	opbx_mutex_lock(&valetparking_lock);
	for(cur = valetparkinglot;cur;cur = cur->next)
		x++;
	opbx_mutex_unlock(&valetparking_lock);
	return x;
}

static int valetparking_say(struct opbx_channel *chan,char *lotname)
{
	struct valetparkeduser *cur;
	int x=0,y=0,res=0;
	int list[1024];
	if(!lotname)
		return 0;
	opbx_mutex_lock(&valetparking_lock);
	for(cur = valetparkinglot;cur;cur = cur->next)
		if(cur->lotname && !strcmp(lotname,cur->lotname))
			list[y++] = cur->valetparkingnum;
	opbx_mutex_unlock(&valetparking_lock);
	for(x=0;x<y;x++) {
		opbx_say_digits(chan,list[x], "", chan->language);
		res = opbx_waitfordigit(chan,1500);
		if(res != 0) {
			res = list[x];
			break;
		}
	}
	return res;
}

static int opbx_pop_valetparking_top(char *lotname)
{
	struct valetparkeduser *cur;

	opbx_mutex_lock(&valetparking_lock);
	for(cur = valetparkinglot;cur;cur = cur->next)
		if(cur->lotname && !strcmp(lotname,cur->lotname))
			break;

	opbx_mutex_unlock(&valetparking_lock);
	return cur ? cur->valetparkingnum : 0;
}

static int opbx_pop_valetparking_bot(char *lotname)
{
	struct valetparkeduser *cur,*lopbx=NULL;
	opbx_mutex_lock(&valetparking_lock);
	for(cur = valetparkinglot;cur;cur = cur->next) {
		if(cur->lotname && !strcmp(lotname,cur->lotname)) {
			lopbx = cur;
		}
	}
	opbx_mutex_unlock(&valetparking_lock);
	return lopbx ? lopbx->valetparkingnum : 0;
}

static int opbx_is_valetparked(char *exten,char *lotname)
{
	struct valetparkeduser *cur;
	int ext=0;
	int ret = 0;
	ext = atoi(exten);
	if(! ext > 0) {
		return ret;
	}
	opbx_mutex_lock(&valetparking_lock);
	cur = valetparkinglot;
	while(cur) {
		if (cur->valetparkingnum == ext && lotname && cur->lotname && !strcmp(lotname,cur->lotname)) {
			ret = 1;
			break;
		}
		cur = cur->next;
	}
	opbx_mutex_unlock(&valetparking_lock);
	return ret;
}

static int opbx_valetpark_call(struct opbx_channel *chan, int timeout, int *extout,char *lotname)
{
	/* We put the user in the valetparking list, then wake up the valetparking thread to be sure it looks
	   after these channels too */
	struct valetparkeduser *pu, *cur;
	int x;

	x = *extout;
	pu = malloc(sizeof(struct valetparkeduser));
	if (pu) {
		int res = 0;

		memset(pu,0,sizeof(struct valetparkeduser));
		
		opbx_mutex_lock(&valetparking_lock);
		if(lotname) {
			strncpy(pu->lotname,lotname,sizeof(pu->lotname));
			if(chan->exten) 
				strncpy(pu->exten,chan->exten,sizeof(pu->exten)-1);
			if(chan->context)
				strncpy(pu->context,chan->context,sizeof(pu->context)-1);

			if(chan->name)
				strncpy(pu->channame,chan->name,sizeof(pu->channame)-1);
			
			pu->priority = chan->priority;

			x = *extout;
			if(x == -1) {
				for (x=valetparking_start;x<=valetparking_stop;x++) {
					for(cur = valetparkinglot;cur;cur=cur->next) {
						if (cur->valetparkingnum == x && cur->lotname && !strcmp(cur->lotname,lotname))
							break;
					}
					if (!cur)
						break;
				}
			}
		}
		if (x <= valetparking_stop) {
			char lopbxname[256];

			chan->appl = "Valet Parked Call";

			pu->chan = chan;
			/* Start music on hold */
			opbx_moh_start(pu->chan, opbx_strlen_zero(chan->musicclass) ? "default" : chan->musicclass);
			gettimeofday(&pu->start, NULL);
			pu->valetparkingnum = x;
			if (timeout >= 0) {
				pu->valetparkingtime = timeout;
			} else {
				pu->valetparkingtime = valetparkingtime;
			}
			*extout = x;
			/* Remember what had been dialed, so that if the valetparking
			   expires, we try to come back to the same place */
			if (strlen(chan->proc_context)) {
				strncpy(pu->context, chan->proc_context, sizeof(pu->context)-1);
			} else {
				strncpy(pu->context, chan->context, sizeof(pu->context)-1);
			}
			if (strlen(chan->proc_exten)) {
				strncpy(pu->exten, chan->proc_exten, sizeof(pu->exten)-1);
			} else {
				strncpy(pu->exten, chan->exten, sizeof(pu->exten)-1);
			}
			if (chan->proc_priority) {
				pu->priority = chan->proc_priority;
			} else {
				pu->priority = chan->priority;
			}
			pu->next = valetparkinglot;
			valetparkinglot = pu;
			opbx_mutex_unlock(&valetparking_lock);
			if (chan && !pbx_builtin_getvar_helper(chan, "BLINDTRANSFER")) {
				time_t now = 0, then = 0;
				time(&then);
				opbx_moh_stop(chan);
				strncpy(lopbxname, chan->name, sizeof(lopbxname) - 1);
				then -= 2;
				while(chan && !opbx_check_hangup(chan) && !strcmp(chan->name, lopbxname)) {
					time(&now);
					if (now - then > 2) {
						if(! (res = opbx_streamfile(chan, "vm-extension", chan->language))) {
							if (! (res = opbx_waitstream(chan, ""))) {
								res = opbx_say_digits(chan, pu->valetparkingnum, "", chan->language);
							}
						}
						time(&then);
					}
					opbx_safe_sleep(chan, 100);
				}
			}
			/* Wake up the (presumably select()ing) thread */
			pthread_kill(valetparking_thread, SIGURG);
			if (option_verbose > 1) 
				opbx_verbose(VERBOSE_PREFIX_2 "Valet Parked %s on slot %d\n", pu->chan->name, pu->valetparkingnum);

			pbx_builtin_setvar_helper(pu->chan,"Parker","Yes");
			manager_event(EVENT_FLAG_CALL, "VirtualValetparkedCall",
						  "Exten: %d\r\n"
						  "Channel: %s\r\n"
						  "LotName: %s\r\n"
						  "Timeout: %ld\r\n"
						  "CallerID: %s\r\n"
						  "CallerIDName: %s\r\n\r\n"
						  ,pu->valetparkingnum, pu->chan->name, lotname
						  ,(long)pu->start.tv_sec + (long)(pu->valetparkingtime/1000) - (long)time(NULL)
						  ,(pu->chan->cid.cid_num ? pu->chan->cid.cid_num : "")
						  ,(pu->chan->cid.cid_name ? pu->chan->cid.cid_name : "")
						  );
			opbx_device_state_changed("Valet/%d@%s", pu->valetparkingnum, lotname);

			return 0;
		} else {
			opbx_log(LOG_WARNING, "No more valetparking spaces\n");
			free(pu);
			opbx_mutex_unlock(&valetparking_lock);
			return -1;
		}
	} else {
		opbx_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	return 0;
}

static int opbx_masq_valetpark_call(struct opbx_channel *rchan,int timeout, int *extout,char *lotname)
{
	struct opbx_channel *chan;
	struct opbx_frame *f;

	/* Make a new, fake channel that we'll use to masquerade in the real one */
	chan = opbx_channel_alloc(0);
	if (chan) {
		/* Let us keep track of the channel name */
		snprintf(chan->name, sizeof (chan->name), "ValetParked/%s",rchan->name);
		/* Make formats okay */
		chan->readformat = rchan->readformat;
		chan->writeformat = rchan->writeformat;
		opbx_channel_masquerade(chan, rchan);
		/* Setup the extensions and such */
		strncpy(chan->context, rchan->context, sizeof(chan->context) - 1);
		strncpy(chan->exten, rchan->exten, sizeof(chan->exten) - 1);
		chan->priority = rchan->priority;
		/* Make the masq execute */
		if((f = opbx_read(chan)))
			opbx_fr_free(f);
		opbx_valetpark_call(chan, timeout, extout, lotname);
	} else {
		opbx_log(LOG_WARNING, "Unable to create Valet Parked channel\n");
		return -1;
	}
	return 0;
}

static void *do_valetparking_thread(void *ignore)
{
	int ms, tms, max;
	struct valetparkeduser *pu, *pl, *pt = NULL;
	struct timeval tv;
	struct opbx_frame *f;
	int x;
	int gc=0;
	fd_set rfds, efds;
	fd_set nrfds, nefds;
	FD_ZERO(&rfds);
	FD_ZERO(&efds);
	for (;;) {
		ms = -1;
		max = -1;
		opbx_mutex_lock(&valetparking_lock);
		pl = NULL;
		pu = valetparkinglot;
		gettimeofday(&tv, NULL);
		FD_ZERO(&nrfds);
		FD_ZERO(&nefds);
		while(pu) {
			if (pbx_builtin_getvar_helper(pu->chan,"BLINDTRANSFER") && !pu->old) {
				gc = 0;
				opbx_indicate(pu->chan, -1);
				pu->old++;
			}
			tms = (tv.tv_sec - pu->start.tv_sec) * 1000 + (tv.tv_usec - pu->start.tv_usec) / 1000;
			if(gc < 5 && !opbx_generator_is_active(pu->chan)) {
				gc++;
				opbx_moh_start(pu->chan, opbx_strlen_zero(pu->chan->musicclass) ? "default" : pu->chan->musicclass);
			}
			if(pu->valetparkingtime > 0 && tms > pu->valetparkingtime) {
				/* They've been waiting too long, send them back to where they came.  Theoretically they
				   should have their original extensions and such, but we copy to be on the safe side */
				strncpy(pu->chan->exten, pu->exten, sizeof(pu->chan->exten)-1);
				strncpy(pu->chan->context, pu->context, sizeof(pu->chan->context)-1);
				pu->chan->priority = pu->priority;
				/* Stop music on hold */
				opbx_moh_stop(pu->chan);
				/* Start up the PBX, or hang them up */
				if (opbx_pbx_start(pu->chan))  {
					opbx_log(LOG_WARNING, "Unable to restart the PBX for user on '%s', hanging them up...\n", pu->chan->name);
					opbx_hangup(pu->chan);
				}
				/* And take them out of the valetparking lot */
				if (pl) 
					pl->next = pu->next;
				else
					valetparkinglot = pu->next;
				pt = pu;
				opbx_device_state_changed("Valet/%d@%s", pu->valetparkingnum, pu->lotname);
				pu = pu->next;
				free(pt);
			} else {
				for (x=0;x<OPBX_MAX_FDS;x++) {
					if ((pu->chan->fds[x] > -1) && (FD_ISSET(pu->chan->fds[x], &rfds) || FD_ISSET(pu->chan->fds[x], &efds))) {
						if (FD_ISSET(pu->chan->fds[x], &efds))
							opbx_set_flag(pu->chan, OPBX_FLAG_EXCEPTION);

						pu->chan->fdno = x;
						/* See if they need servicing */
						f = opbx_read(pu->chan);
						if (!f || ((f->frametype == OPBX_FRAME_CONTROL) && (f->subclass ==  OPBX_CONTROL_HANGUP))) {
							/* There's a problem, hang them up*/
							if (option_verbose > 1) 
								opbx_verbose(VERBOSE_PREFIX_2 "%s got tired of being Valet Parked\n", pu->chan->name);
							opbx_hangup(pu->chan);
							/* And take them out of the valetparking lot */
							if (pl) 
								pl->next = pu->next;
							else
								valetparkinglot = pu->next;
							pt = pu;
							opbx_device_state_changed("Valet/%d@%s", pu->valetparkingnum, pu->lotname);
							pu = pu->next;
							free(pt);
							break;
						} else {
							/* XXX Maybe we could do something with packets, like dial "0" for operator or something XXX */
							opbx_fr_free(f);
							goto std;	/* XXX Ick: jumping into an else statement??? XXX */
						}
					}
				}
				if (x >= OPBX_MAX_FDS) {
std:					for (x=0;x<OPBX_MAX_FDS;x++) {
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
		opbx_mutex_unlock(&valetparking_lock);
		rfds = nrfds;
		efds = nefds;
		tv.tv_sec = ms / 1000;
		tv.tv_usec = (ms % 1000) * 1000;
		/* Wait for something to happen */
		opbx_select(max + 1, &rfds, NULL, &efds, (ms > -1) ? &tv : NULL);
		pthread_testcancel();
	}
	return NULL;	/* Never reached */
}

static int valetpark_call(struct opbx_channel *chan, int argc, char **argv)
{
	char tmp[80];
	struct localuser *u;
	int timeout;
	int ext = 0, res = 0;

	if (argc < 2 || argc > 6 || !argv[0][0] || !argv[1][0]) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", vpcsyntax);
		return -1;
	}

	timeout = (argc > 2 && argv[2][0] ? atoi(argv[2]) : DEFAULT_VALETPARK_TIME);

	if (opbx_is_valetparked(argv[0], argv[1])) {
		opbx_log(LOG_WARNING,"Call is already Valet Parked Here [%s]\n", argv[0]);

		if (opbx_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num)) {
			opbx_explicit_goto(chan, chan->context, chan->exten, chan->priority + 100);
			return 0;
		}

		return -1;
	}
	LOCAL_USER_ADD(u);

	if (chan->_state != OPBX_STATE_UP)
		opbx_answer(chan);

	if (!strcmp(argv[0],"auto"))
		ext = -1;
	else if (!strcmp(argv[0],"query")) {
		opbx_waitfor(chan,-1);
		memset(&tmp, '\0', sizeof(tmp));
		opbx_streamfile(chan, "vm-extension", chan->language);
		res = opbx_waitstream(chan, OPBX_DIGIT_ANY);
		if (res)
			return -1;

		opbx_app_getdata(chan, "vm-then-pound", tmp, arraysize(tmp), 5000);
		if (tmp[0])
			ext = atoi(tmp);
	} else { 
		ext = atoi(argv[0]);
	}
	if (ext == 0)
		ext = -1;


	if (argc > 3 && argv[3][0])
		strncpy(chan->exten, argv[3], sizeof(chan->exten)-1);
	if (argc > 4 && argv[4][0]) {
		chan->priority = atoi(argv[4]);
		if(!chan->priority)
			chan->priority = 1;
	}
	if (argc > 5 && argv[5][0])
		strncpy(chan->context, argv[5], sizeof(chan->context)-1);

	opbx_masq_valetpark_call(chan, timeout, &ext, argv[1]);

	LOCAL_USER_REMOVE(u);
	return 1;
}


static struct opbx_channel *do_valetunpark(struct opbx_channel *chan, char *exten, char *lotname)
{
	int res=0;
	struct opbx_channel *peer=NULL;
	struct valetparkeduser *pu, *pl=NULL;
	int valetpark=-1;
	struct opbx_channel *rchan = NULL;
	char tmp[80];

	if(exten) {
		if(!strcmp(exten,"fifo")) {
			valetpark = opbx_pop_valetparking_top(lotname);
		}
		else if(!strcmp(exten,"filo")) {
			valetpark = opbx_pop_valetparking_bot(lotname);
		}
		else if(chan && !strcmp(exten,"query")) {
			opbx_waitfor(chan,-1);
			memset(&tmp,0,80);
			opbx_streamfile(chan, "vm-extension", chan->language);
			res = opbx_waitstream(chan, OPBX_DIGIT_ANY);
			if(res)
				return NULL;
			opbx_app_getdata(chan,"vm-then-pound",tmp,80,5000);
			if(tmp[0])
				valetpark = atoi(tmp);
		}
		else {
			valetpark = atoi(exten);
		}

		if(valetpark == 0) {
			opbx_log(LOG_WARNING, "Nobody Valet Parked in %s",lotname);
			return NULL;
		}
		
	}

	opbx_mutex_lock(&valetparking_lock);
	pu = valetparkinglot;
	while(pu) {
		if ((lotname && pu->valetparkingnum == valetpark && pu->lotname && !strcmp(pu->lotname,lotname)) 
			|| (! lotname && pu->valetparkingnum == valetpark)) {
			if (pl)
				pl->next = pu->next;
			else
				valetparkinglot = pu->next;
			break;
		}
		pl = pu;
		pu = pu->next;
	}
	opbx_mutex_unlock(&valetparking_lock);
	opbx_device_state_changed("Valet/%s@%s", exten, lotname);
	if (pu) {
		rchan = pu->chan;
		peer = pu->chan;
		free(pu);
	}
	
	return rchan;
}

static struct opbx_channel *valet_request(const char *type, int format, void *data, int *cause)
{
	char *exten = NULL, *lotname = NULL;
	struct opbx_channel *peer;

	exten = opbx_strdupa(data);
	if((lotname=strchr(exten,':'))) {
        *lotname = '\0';
        *lotname++;
    }
	if(!lotname) {
        opbx_log(LOG_WARNING,"Please specify a lotname in the dialplan.");
		*cause = OPBX_CAUSE_UNALLOCATED;
        return NULL;
    }
	if((peer = do_valetunpark(NULL, exten, lotname))) {
	    if(opbx_test_flag(peer, OPBX_FLAG_MOH)) {
			opbx_moh_stop(peer);
		}
		if(opbx_set_read_format(peer, format) ||
		   opbx_set_write_format(peer, format)) {
			opbx_log(LOG_WARNING,"Hanging up on %s because I cant make it the requested format.\n",peer->name);
			opbx_hangup(peer);
			*cause = OPBX_CAUSE_UNALLOCATED;
			return NULL;
		}
		/* We return the chan we have been protecting which is already up but
		   be vewy vewy qwiet we will trick callweaver into thinking it's a new channel
		*/
		opbx_setstate(peer, OPBX_STATE_RESERVED);
	}

	return peer;
}


static int valetunpark_call(struct opbx_channel *chan, int argc, char **argv)
{
	int res=0;
	struct localuser *u;
	struct opbx_channel *peer=NULL;
	int valetpark=-1;
	int dres;
	struct opbx_bridge_config config;
	char *exten,*lotname;

	if (argc != 2 || !argv[0][0] || !argv[1][0]) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", vupsyntax);
		return -1;
	}

	LOCAL_USER_ADD(u);

	if (chan->_state != OPBX_STATE_UP)
		opbx_answer(chan);

	peer = do_valetunpark(chan, argv[0], argv[1]);

	if (peer) {
		opbx_moh_stop(peer);
		res = opbx_channel_make_compatible(chan, peer);
		if (res < 0) {
			opbx_log(LOG_WARNING, "Could not make channels %s and %s compatible for bridge\n", chan->name, peer->name);
			opbx_hangup(peer);
			LOCAL_USER_REMOVE(u);
			return -1;
		}
		/* This runs sorta backwards, since we give the incoming channel control, as if it
		   were the person called. */

		if (option_verbose > 2) 
			opbx_verbose(VERBOSE_PREFIX_3 "Channel %s connected to Valet Parked call %d in lot %s\n", chan->name, valetpark, argv[1]);

		memset(&config,0,sizeof(struct opbx_bridge_config));
		opbx_set_flag(&(config.features_caller) , OPBX_FEATURE_REDIRECT);
		opbx_set_flag(&(config.features_callee) , OPBX_FEATURE_REDIRECT);
		res = opbx_bridge_call(chan,peer,&config);

		if (res != OPBX_PBX_NO_HANGUP_PEER)
			opbx_hangup(peer);
		LOCAL_USER_REMOVE(u);
		return res;
	} else {
		/* XXX Play a message XXX */
		dres = opbx_streamfile(chan, "pbx-invalidpark", chan->language);
		if (!dres) {
			dres = opbx_waitstream(chan, "");
	 	} else {
			opbx_log(LOG_WARNING, "opbx_streamfile of %s failed on %s\n", "pbx-invalidpark", chan->name);
			res = 0;
		}
		if (option_verbose > 2) 
			opbx_verbose(VERBOSE_PREFIX_3 "Channel %s tried to talk to non-existant Valet Parked call %d\n", chan->name, valetpark);
		res = -1;
	}
	LOCAL_USER_REMOVE(u);
	return res;
}


static int opbx_valetparking(struct opbx_channel *chan, int argc, char **argv) 
{
	struct localuser *u;
	struct opbx_app *app;
	char *appname;	
	int res=0;

	if (argc < 2 || argc > 6 || !argv[0][0] || !argv[1][0]) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", vpsyntax);
		return -1;
	}

	if (!isdigit(argv[0][0])) {
		opbx_log(LOG_WARNING, "ValetParking requires a numeric extension.\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	res = (!opbx_is_valetparked(argv[0], argv[1]) ? valetpark_call(chan, argc, argv) : valetunpark_call(chan, argc, argv));

	LOCAL_USER_REMOVE(u);
	return res;
}


static int valetpark_list(struct opbx_channel *chan, int argc, char **argv)
{
	char buf[64];
	char *nargv[3];
	struct localuser *u;
	struct opbx_app *app;
	int res = 0;

	if (argc != 1 || !argv[0][0]) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", vlsyntax);
		return -1;
	}

	LOCAL_USER_ADD(u);

	res = valetparking_say(chan, argv[0]);

	if (res > 0) {
		snprintf(buf, sizeof(buf), "%d", res);
		nargv[0] = buf;
		nargv[1] = argv[0];
		nargv[2] = NULL;
		res = valetunpark_call(chan, 2, nargv);
	}

	LOCAL_USER_REMOVE(u);
	return 1;
}


static int handle_valetparkedcalls(int fd, int argc, char *argv[])
{
	struct valetparkeduser *cur;

	opbx_cli(fd, "%4s %25s (%-15s %-12s %-4s) %-6s %-6s %-15s\n", "Num", "Channel"
		, "Context", "Extension", "Pri", "Elapsed","Timeout","LotName");

	opbx_mutex_lock(&valetparking_lock);

	cur=valetparkinglot;
	while(cur) {
		opbx_cli(fd, "%4d %25s (%-15s %-12s %-4d) %6lds %6lds %-15s\n"
			,cur->valetparkingnum, cur->chan->name, cur->context, cur->exten
			,cur->priority,(time(NULL) - cur->start.tv_sec),cur->valetparkingtime ? (cur->start.tv_sec +  (cur->valetparkingtime/1000) - time(NULL)) : 0,cur->lotname);

		cur = cur->next;
	}

	opbx_mutex_unlock(&valetparking_lock);

	return RESULT_SUCCESS;
}

/* Pseudo-devices like Valet/<SLOT>@<LOT> */
static int valetparked_devicestate(void *data)
{
	char *slot;
	char *lot;
	struct valetparkeduser *cur;
	int res = OPBX_DEVICE_INVALID;
	int slotnum;

	slot = opbx_strdupa(data);
	if ((lot = strchr(slot, '@')))
		*lot++ = 0;
	else
		return (res);
	slotnum = atoi(slot);
	
	if (option_debug > 2)
		opbx_log(LOG_DEBUG, "Checking device state for lot %s, slot %s\n", lot, slot);

	opbx_mutex_lock(&valetparking_lock);

	cur=valetparkinglot;
	while(cur) {
		if (!strcmp(lot, cur->lotname) && slotnum == cur->valetparkingnum)
			res = OPBX_DEVICE_BUSY;
		cur = cur->next;
	}

	opbx_mutex_unlock(&valetparking_lock);

	return (res);
}



static char showvaletparked_help[] =
"Usage: show valetparkedcalls\n"
"       Lists currently Valet Parked calls.\n";

static struct opbx_cli_entry showvaletparked =
{ { "show", "valetparkedcalls", NULL }, handle_valetparkedcalls, "Lists valetparked calls", showvaletparked_help };
/* Dump lot status */
static int manager_valetparking_status( struct mansession *s, struct message *m )
{
	struct valetparkeduser *cur;

	astman_send_ack(s, m, "Valet Parked calls will follow");

        opbx_mutex_lock(&valetparking_lock);

        cur=valetparkinglot;
        while(cur) {
                opbx_cli(s->fd, "Event: ValetParkedCall\r\n"
						"Exten: %d\r\n"
						"Channel: %s\r\n"
						"Timeout: %ld\r\n"
						"CallerID: %s\r\n"
						"CallerIDName: %s\r\n"
						"\r\n"
                        ,cur->valetparkingnum, cur->chan->name
                        ,(long)cur->start.tv_sec + (long)(cur->valetparkingtime/1000) - (long)time(NULL)
						,(cur->chan->cid.cid_num ? cur->chan->cid.cid_num : "")
						,(cur->chan->cid.cid_name ? cur->chan->cid.cid_name : "")

			);

                cur = cur->next;
        }

        opbx_mutex_unlock(&valetparking_lock);

        return RESULT_SUCCESS;
}




int load_module(void)
{
	opbx_cli_register(&showvaletparked);
	valetparkingtime = DEFAULT_VALETPARK_TIME;
	opbx_pthread_create(&valetparking_thread, NULL, do_valetparking_thread, NULL);
	valetunparkedcall_app = opbx_register_application(valetunparkedcall, valetunpark_call, vupsynopsis, vupsyntax, vupdesc);
	valetparkedcall_app = opbx_register_application(valetparkedcall, valetpark_call, vpcsynopsis, vpcsyntax, vpcdesc);
	valetparking_app = opbx_register_application(valetparking, opbx_valetparking, vpsynopsis, vpsyntax, vpdesc);
	valetparklist_app = opbx_register_application(valetparklist,valetpark_list, vlsynopsis, vlsyntax, vldesc);
	opbx_channel_register(&valet_tech);
	opbx_manager_register( "ValetparkedCalls", 0, manager_valetparking_status, "List valetparked calls" );
	return 0;
}

int unload_module(void)
{
	int res = 0;

	STANDARD_HANGUP_LOCALUSERS;

	if (!opbx_mutex_lock(&valetparking_lock)) {
        if (valetparking_thread && (valetparking_thread != OPBX_PTHREADT_STOP)) {
            pthread_cancel(valetparking_thread);
            pthread_kill(valetparking_thread, SIGURG);
            pthread_join(valetparking_thread, NULL);
        }
        valetparking_thread = OPBX_PTHREADT_STOP;
        opbx_mutex_unlock(&valetparking_lock);
    } else {
        opbx_log(LOG_WARNING, "Unable to lock the valet\n");
        return -1;
    }

	opbx_channel_unregister(&valet_tech);
	opbx_manager_unregister( "ValetparkedCalls" );
	opbx_cli_unregister(&showvaletparked);
	res |= opbx_unregister_application(valetunparkedcall_app);
	res |= opbx_unregister_application(valetparkedcall_app);
	res |= opbx_unregister_application(valetparking_app);
	res |= opbx_unregister_application(valetparklist_app);
	return res;
}

char *description(void)
{
	return "Valet Parking Application (http://www.cluecon.com)";
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	res += valetparking_count();
	return res;
}

