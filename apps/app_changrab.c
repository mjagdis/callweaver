/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 * app_changrab.c 
 * Copyright Anthony C Minessale II <anthmct@yahoo.com>
 * 
 * Thanks to Claude Patry <cpatry@gmail.com> for his help.
 */

/*uncomment below or build with -DCW_10_COMPAT for 1.0 */ 
//#define CW_10_COMPAT

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/utils.h"
#include "callweaver/musiconhold.h"
#include "callweaver/module.h"
#include "callweaver/features.h"
#include "callweaver/cli.h"
#include "callweaver/manager.h"


static char tdesc[] = "Take over an existing channel and bridge to it.";

static void *changrab_app;
static const char changrab_name[] = "ChanGrab";
static const char changrab_syntax[] = "ChanGrab(channel[, flags])";
static const char changrab_description[] =
"Take over the specified channel (ending any call it is currently\n"
"involved in.) and bridge that channel to the caller.\n\n"
"Flags:\n\n"
"   -- 'b' Indicates that you want to grab the channel that the\n"
"          specified channel is Bridged to.\n\n"
"   -- 'r' Only incercept the channel if the channel has not\n"
"          been answered yet\n";


static int changrab_exec(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	int res=0;
	struct localuser *u;
	struct cw_channel *newchan;
	struct cw_channel *oldchan;
	struct cw_frame *f;
	struct cw_bridge_config config;

	if (argc < 1 || argc > 2)
		return cw_function_syntax(changrab_syntax);

	if (!(oldchan = cw_get_channel_by_name_prefix_locked(argv[0], strlen(argv[0])))) {
		cw_log(CW_LOG_WARNING, "No Such Channel: %s\n", argv[0]);
		return -1;
	}

	if (argc > 1) {
		if (oldchan->_bridge && strchr(argv[1], 'b')) {
			newchan = oldchan;
			oldchan = cw_object_get(oldchan->_bridge);
			cw_mutex_unlock(&newchan->lock);
			cw_object_put(newchan);
			cw_mutex_lock(&oldchan->lock);
		}
		if (strchr(argv[1],'r') && oldchan->_state == CW_STATE_UP) {
			cw_mutex_unlock(&oldchan->lock);
			cw_object_put(oldchan);
			return -1;
		}
	}

	cw_mutex_unlock(&oldchan->lock);
	
	LOCAL_USER_ADD(u);

	if ((newchan = cw_channel_alloc(0, "ChanGrab/%s", oldchan->name))) {
		newchan->readformat = oldchan->readformat;
		newchan->writeformat = oldchan->writeformat;
		cw_channel_masquerade(newchan, oldchan);
		if((f = cw_read(newchan))) {
			cw_fr_free(f);
			memset(&config,0,sizeof(struct cw_bridge_config));
			cw_set_flag(&(config.features_callee), CW_FEATURE_REDIRECT);
			cw_set_flag(&(config.features_caller), CW_FEATURE_REDIRECT);

			if (newchan->_state != CW_STATE_UP)
				cw_answer(newchan);

			chan->appl = "Bridged Call";
			res = cw_bridge_call(chan, newchan, &config);
			cw_hangup(newchan);
		}
	}

	cw_object_put(oldchan);

	LOCAL_USER_REMOVE(u);
	return res ? 0 : -1;
}


struct cw_bridge_thread_obj 
{
	struct cw_bridge_config bconfig;
	struct cw_channel *chan;
	struct cw_channel *peer;
};

static void *cw_bridge_call_thread(void *data) 
{
	struct cw_bridge_thread_obj *tobj = data;
	tobj->chan->appl = "Redirected Call";
	tobj->peer->appl = "Redirected Call";
	if (tobj->chan->cdr) {
		cw_cdr_reset(tobj->chan->cdr,0);
		cw_cdr_setdestchan(tobj->chan->cdr, tobj->peer->name);
	}
	if (tobj->peer->cdr) {
		cw_cdr_reset(tobj->peer->cdr,0);
		cw_cdr_setdestchan(tobj->peer->cdr, tobj->chan->name);
	}


	cw_bridge_call(tobj->peer, tobj->chan, &tobj->bconfig);
	cw_hangup(tobj->chan);
	cw_hangup(tobj->peer);
	cw_object_put(tobj->chan);
	cw_object_put(tobj->peer);
	free(tobj);
	return NULL;
}

static void cw_bridge_call_thread_launch(struct cw_channel *chan, struct cw_channel *peer) 
{
	pthread_t tid;
	struct cw_bridge_thread_obj *tobj;

	if ((tobj = calloc(1, sizeof(struct cw_bridge_thread_obj)))) {
		tobj->chan = cw_object_dup(chan);
		tobj->peer = cw_object_dup(peer);
		if (cw_pthread_create(&tid, &global_attr_rr_detached, cw_bridge_call_thread, tobj)) {
			cw_object_put(chan);
			cw_object_put(peer);
			free(tobj);
		}
	}
}


#define CGUSAGE "Usage: changrab [-[bB]] <channel> <exten>@<context> [pri]\n"
static int changrab_cli(int fd, int argc, char *argv[]) {
	char *chan_name_1, *chan_name_2 = NULL, *context,*exten,*flags=NULL;
	char *pria = NULL;
	struct cw_channel *chan, *xferchan_1, *xferchan_2;
	int x=1;

	if(argc < 3) {
		cw_cli(fd,CGUSAGE);
		return -1;
	}
	chan_name_1 = argv[x++];
	if(chan_name_1[0] == '-') {
		flags = cw_strdupa(chan_name_1);
		if (strchr(flags,'h')) {
			chan_name_1 = argv[x++];
			if (!(xferchan_1 = cw_get_channel_by_name_prefix_locked(chan_name_1, strlen(chan_name_1))))
				return -1;
			cw_mutex_unlock(&xferchan_1->lock);
			cw_hangup(xferchan_1);
			cw_object_put(xferchan_1);
			cw_verbose("OK, good luck!\n");
			return 0;
		} else if (strchr(flags,'m') || strchr(flags,'M')) {
			chan_name_1 = argv[x++];
			if (!(xferchan_1 = cw_get_channel_by_name_prefix_locked(chan_name_1, strlen(chan_name_1))))
				return 1;
			cw_mutex_unlock(&xferchan_1->lock);
			strchr(flags,'m') ? cw_moh_start(xferchan_1,NULL) : cw_moh_stop(xferchan_1);
			cw_object_put(xferchan_1);
			return 0;
		}
		if(argc < 4) {
			cw_cli(fd,CGUSAGE);
			return -1;
		}
		chan_name_1 = argv[x++];
	}

	exten = cw_strdupa(argv[x++]);
	if((context = strchr(exten,'@'))) {
		*context = 0;
		context++;
		if(!(context && exten)) {
			cw_cli(fd,CGUSAGE);
			return -1;
		}
		if((pria = strchr(context,':'))) {
			*pria = '\0';
			pria++;
		} else {
			pria = argv[x];
		}
	} else if (strchr(exten,'/')) {
		chan_name_2 = exten;
	}

	
	if (!(xferchan_1 = cw_get_channel_by_name_prefix_locked(chan_name_1, strlen(chan_name_1)))) {
		cw_log(CW_LOG_WARNING, "No Such Channel: %s\n", chan_name_1);
		return -1;
	} 

	if (flags && strchr(flags,'b')) {
		if ((chan = cw_bridged_channel(xferchan_1))) {
			cw_mutex_unlock(&xferchan_1->lock);
			cw_object_put(xferchan_1);
			xferchan_1 = chan;
			cw_mutex_lock(&xferchan_1->lock);
		}
	}
	cw_mutex_unlock(&xferchan_1->lock);

	if (chan_name_2) {
		struct cw_frame *f;
		struct cw_channel *newchan_1, *newchan_2;
		
		if (!(newchan_1 = cw_channel_alloc(0, "ChanGrab/%s", xferchan_1->name))) {
			cw_object_put(xferchan_1);
			return -1;
		}

		newchan_1->readformat = xferchan_1->readformat;
		newchan_1->writeformat = xferchan_1->writeformat;
		cw_channel_masquerade(newchan_1, xferchan_1);
		if ((f = cw_read(newchan_1))) {
			cw_fr_free(f);
		} else {
			cw_hangup(newchan_1);
			cw_object_put(xferchan_1);
			return -1;
		}

		if (!(xferchan_2 = cw_get_channel_by_name_prefix_locked(chan_name_2, strlen(chan_name_2)))) {
			cw_log(CW_LOG_WARNING, "No Such Channel: %s\n", chan_name_2);
			cw_hangup(newchan_1);
			cw_object_put(xferchan_1);
			return -1;
		}

		if (flags && strchr(flags, 'B')) {
			if ((chan = cw_bridged_channel(xferchan_2))) {
				cw_mutex_unlock(&xferchan_2->lock);
				cw_object_put(xferchan_2);
				xferchan_2 = chan;
				cw_mutex_lock(&xferchan_2->lock);
			}
		}
		cw_mutex_unlock(&xferchan_2->lock);

		if (!(newchan_2 = cw_channel_alloc(0, "ChanGrab/%s", xferchan_2->name))) {
			cw_hangup(newchan_1);
			cw_object_put(xferchan_1);
			cw_object_put(xferchan_2);
			return -1;
		}

		newchan_2->readformat = xferchan_2->readformat;
		newchan_2->writeformat = xferchan_2->writeformat;
		cw_channel_masquerade(newchan_2, xferchan_2);

		if ((f = cw_read(newchan_2))) {
			cw_fr_free(f);
			cw_bridge_call_thread_launch(newchan_1, newchan_2);
			x = 0;
		} else {
			cw_hangup(newchan_1);
			cw_hangup(newchan_2);
			x = -1;
		}

		cw_object_put(xferchan_2);
		cw_object_put(xferchan_1);
		return x;
	} else {
		cw_verbose("Transferring_to context %s, extension %s, priority %s\n", context, exten, pria);
		if (pria)
			cw_async_goto(xferchan_1, context, exten, pria);
		else
			cw_async_goto_n(xferchan_1, context, exten, 1);
		cw_object_put(xferchan_1);
	}
	return 0;
}

struct fast_originate_helper {
	char tech[256];
	char data[256];
	int timeout;
	char app[256];
	char appdata[256];
	char cid_name[256];
	char cid_num[256];
	char context[256];
	char exten[256];
	char idtext[256];
	int priority;
};



#define USAGE "Usage: originate <channel> <exten>@<context> [pri] [callerid] [timeout]\n"

static void *originate(void *arg) {
	struct fast_originate_helper *in = arg;
	int reason=0;
	int res;
	struct cw_channel *chan = NULL;

	res = cw_pbx_outgoing_exten(in->tech, CW_FORMAT_SLINEAR, in->data, in->timeout, in->context, in->exten, in->priority, &reason, 1, !cw_strlen_zero(in->cid_num) ? in->cid_num : NULL, !cw_strlen_zero(in->cid_name) ? in->cid_name : NULL, NULL, &chan);
	manager_event(EVENT_FLAG_CALL, "Originate", 
			"ChannelRequested: %s/%s\r\n"
			"Context: %s\r\n"
			"Extension: %s\r\n"
			"Priority: %d\r\n"
			"Result: %d\r\n"
			"Reason: %d\r\n"
			"Reason-txt: %s\r\n",
			in->tech,
			in->data,
			in->context, 
			in->exten, 
			in->priority, 
			res,
			reason,
			cw_control2str(reason)
	);

	/* Locked by cw_pbx_outgoing_exten or cw_pbx_outgoing_app */
	if (chan) {
		cw_mutex_unlock(&chan->lock);
	}
	free(in);
	return NULL;
}


static int originate_cli(int fd, int argc, char *argv[]) {
	pthread_t tid;
	char *chan_name_1,*context,*exten,*tech,*data,*callerid;
	int pri=0,to=60000;
	struct fast_originate_helper *in;
	char *num = NULL;

	if(argc < 3) {
		cw_cli(fd,USAGE);
		return -1;
	}
	chan_name_1 = argv[1];

	exten = cw_strdupa(argv[2]);
	if((context = strchr(exten,'@'))) {
		*context = 0;
		context++;
	}
	if(! (context && exten)) {
		cw_cli(fd,CGUSAGE);
        return -1;
	}


	pri = argv[3] ? atoi(argv[3]) : 1;
	if(!pri)
		pri = 1;

	tech = cw_strdupa(chan_name_1);
	if((data = strchr(tech,'/'))) {
		*data = '\0';
		data++;
	}
	if(!(tech && data)) {
		cw_cli(fd,USAGE);
        return -1;
	}
	in = malloc(sizeof(struct fast_originate_helper));
	if(!in) {
		cw_cli(fd,"Out of memory\n");
		return -1;
	}
	memset(in,0,sizeof(struct fast_originate_helper));
	
	callerid = (argc > 4)  ? argv[4] : NULL;
	to = (argc > 5) ? atoi(argv[5]) : 60000;

	strncpy(in->tech,tech,sizeof(in->tech));
	strncpy(in->data,data,sizeof(in->data));
	in->timeout=to;
	if(callerid) {
		if((num = strchr(callerid,':'))) {
			*num = '\0';
			num++;
			strncpy(in->cid_num,num,sizeof(in->cid_num));
		}
		strncpy(in->cid_name,callerid,sizeof(in->cid_name));
	}
	strncpy(in->context,context,sizeof(in->context));
	strncpy(in->exten,exten,sizeof(in->exten));
	in->priority = pri;

	cw_cli(fd,"Originating Call %s/%s %s %s %d\n",in->tech,in->data,in->context,in->exten,in->priority);

	cw_pthread_create(&tid, &global_attr_rr_detached, originate, in);
	return 0;
}



static void complete_exten_at_context(int fd, char *argv[], int lastarg, int lastarg_len)
{
	struct cw_context *c;
	struct cw_exten *e;
	char *context = NULL, *exten = NULL, *delim = NULL;

	/*
	 * exten@context completion ... 
	 */
	if (lastarg == 2) {
		/* now, parse values from word = exten@context */
		if ((delim = strchr(argv[2], '@'))) {
			/* check for duplicity ... */
			if (delim != strrchr(argv[2], '@'))
				return;

			*delim = '\0';
			exten = argv[2];
			context = delim + 1;
		} else {
			exten = argv[2];
		}

		if (!cw_lock_contexts()) {
			/* find our context ... */
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				/* our context? */
				if ( (!context || !strlen(context)) || /* if no input, all contexts ... */
					 (context && !strncmp(cw_get_context_name(c),
					              context, strlen(context))) ) { /* if input, compare ... */
					/* try to complete extensions ... */
					for (e = cw_walk_context_extensions(c, NULL); e; e = cw_walk_context_extensions(c, e)) {
	
						if (!strncasecmp(cw_get_context_name(c), "proc-", 5) || !strncasecmp(cw_get_extension_name(e),"_",1))
							continue;

						/* our extension? */
						if ( (!exten || !strlen(exten)) /* if not input, all extensions ... */
						|| (exten && !strncmp(cw_get_extension_name(e), exten, strlen(exten))) ) { /* if input, compare ... */
							/* If there is an extension then return
							 * exten@context.
							 */
							if (exten)
								cw_cli(fd, "%s@%s\n", cw_get_extension_name(e), cw_get_context_name(c));
						}
					}
				}
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");

		if (delim)
			*delim = '@';
	}

	/*
	 * Complete priority ...
	 */
	else if (lastarg == 3) {
		/* wrong exten@context format? */
		if (!(delim = strchr(argv[2], '@')) || delim != strrchr(argv[2], '@') || delim == argv[2] || !delim[1])
			return;

		*delim = '\0';

		if (cw_lock_contexts()) {
			/* walk contexts */
			for (c = cw_walk_contexts(NULL); c; c = cw_walk_contexts(c)) {
				if (!strcmp(cw_get_context_name(c), delim + 1)) {
					/* walk extensions */
					for (e = cw_walk_context_extensions(c, NULL); e; e = cw_walk_context_extensions(c, e)) {
						if (!strcmp(cw_get_extension_name(e), argv[2])) {
							struct cw_exten *priority;
							char buffer[10];

							for (priority = cw_walk_extension_priorities(e, NULL); priority; priority = cw_walk_extension_priorities(e, priority)) {
								snprintf(buffer, sizeof(buffer), "%u", cw_get_extension_priority(priority));
								if (!strncmp(argv[3], buffer, lastarg_len))
									cw_cli(fd, "%s\n", buffer);
							}
						}
					}
				}
			}

			cw_unlock_contexts();
		} else
			cw_log(CW_LOG_ERROR, "Failed to lock context list\n");

		*delim = '@';
	}
}


static void complete_cg(int fd, char *argv[], int lastarg, int lastarg_len)
{

	if (lastarg == 1)
		cw_complete_channel(fd, argv[lastarg], lastarg_len);
	else if (lastarg >= 2)
		complete_exten_at_context(fd, argv, lastarg, lastarg_len);
}

static void complete_org(int fd, char *argv[], int lastarg, int lastarg_len)
{
	if (lastarg >= 2)
		complete_exten_at_context(fd, argv, lastarg, lastarg_len);
}


static struct cw_clicmd cli_changrab = {
	.cmda = { "changrab", NULL},
	.handler = changrab_cli,
	.generator = complete_cg,
	.summary = "ChanGrab",
	.usage = "ChanGrab\n",
};
static struct cw_clicmd cli_originate = {
	.cmda = { "originate", NULL },
	.handler = originate_cli,
	.generator = complete_org,
	.summary = "Originate",
	.usage = "Originate\n",
};

static int unload_module(void)
{
	int res = 0;

	cw_cli_unregister(&cli_changrab);
	cw_cli_unregister(&cli_originate);
	res |= cw_unregister_function(changrab_app);
	return res;
}

static int load_module(void)
{
	cw_cli_register(&cli_changrab);
	cw_cli_register(&cli_originate);
	changrab_app = cw_register_function(changrab_name, changrab_exec, tdesc, changrab_syntax, changrab_description);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
