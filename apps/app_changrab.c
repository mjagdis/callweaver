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

/*uncomment below or build with -DOPBX_10_COMPAT for 1.0 */ 
//#define OPBX_10_COMPAT

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

static char *tdesc = "Take over an existing channel and bridge to it.";

static void *changrab_app;
static const char *changrab_name = "ChanGrab";
static const char *changrab_syntax = "ChanGrab(channel[, flags])";
static const char *changrab_description =
"Take over the specified channel (ending any call it is currently\n"
"involved in.) and bridge that channel to the caller.\n\n"
"Flags:\n\n"
"   -- 'b' Indicates that you want to grab the channel that the\n"
"          specified channel is Bridged to.\n\n"
"   -- 'r' Only incercept the channel if the channel has not\n"
"          been answered yet\n";

STANDARD_LOCAL_USER;
LOCAL_USER_DECL;


static struct opbx_channel *my_opbx_get_channel_by_name_locked(char *channame) {
	struct opbx_channel *chan;
	chan = opbx_channel_walk_locked(NULL);
	while(chan) {
		if (!strncasecmp(chan->name, channame, strlen(channame)))
			return chan;
		opbx_mutex_unlock(&chan->lock);
		chan = opbx_channel_walk_locked(chan);
	}
	return NULL;
}

static int changrab_exec(struct opbx_channel *chan, int argc, char **argv)
{
	int res=0;
	struct localuser *u;
	struct opbx_channel *newchan;
	struct opbx_channel *oldchan;
	struct opbx_frame *f;
	struct opbx_bridge_config config;

	if (argc < 1 || argc > 2) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", changrab_syntax);
		return -1;
	}

	if ((oldchan = my_opbx_get_channel_by_name_locked(argv[0]))) {
		opbx_mutex_unlock(&oldchan->lock);
	} else {
		opbx_log(LOG_WARNING, "No Such Channel: %s\n", argv[0]);
		return -1;
	}
	
	if (argc > 1) {
		if (oldchan->_bridge && strchr(argv[1], 'b'))
			oldchan = oldchan->_bridge;
		if (strchr(argv[1],'r') && oldchan->_state == OPBX_STATE_UP)
			return -1;
	}
	
	LOCAL_USER_ADD(u);
	newchan = opbx_channel_alloc(0);
	snprintf(newchan->name, sizeof (newchan->name), "ChanGrab/%s",oldchan->name);
	newchan->readformat = oldchan->readformat;
	newchan->writeformat = oldchan->writeformat;
	opbx_channel_masquerade(newchan, oldchan);
	if((f = opbx_read(newchan))) {
		opbx_fr_free(f);
		memset(&config,0,sizeof(struct opbx_bridge_config));
		opbx_set_flag(&(config.features_callee), OPBX_FEATURE_REDIRECT);
		opbx_set_flag(&(config.features_caller), OPBX_FEATURE_REDIRECT);

		if(newchan->_state != OPBX_STATE_UP) {
			opbx_answer(newchan);
		}
		
		chan->appl = "Bridged Call";
		res = opbx_bridge_call(chan, newchan, &config);
		opbx_hangup(newchan);
	}

	LOCAL_USER_REMOVE(u);
	return res ? 0 : -1;
}


struct opbx_bridge_thread_obj 
{
	struct opbx_bridge_config bconfig;
	struct opbx_channel *chan;
	struct opbx_channel *peer;
};

static void *opbx_bridge_call_thread(void *data) 
{
	struct opbx_bridge_thread_obj *tobj = data;
	tobj->chan->appl = "Redirected Call";
	tobj->peer->appl = "Redirected Call";
	if (tobj->chan->cdr) {
		opbx_cdr_reset(tobj->chan->cdr,0);
		opbx_cdr_setdestchan(tobj->chan->cdr, tobj->peer->name);
	}
	if (tobj->peer->cdr) {
		opbx_cdr_reset(tobj->peer->cdr,0);
		opbx_cdr_setdestchan(tobj->peer->cdr, tobj->chan->name);
	}


	opbx_bridge_call(tobj->peer, tobj->chan, &tobj->bconfig);
	opbx_hangup(tobj->chan);
	opbx_hangup(tobj->peer);
	tobj->chan = tobj->peer = NULL;
	free(tobj);
	tobj=NULL;
	return NULL;
}

static void opbx_bridge_call_thread_launch(struct opbx_channel *chan, struct opbx_channel *peer) 
{
	pthread_t thread;
	pthread_attr_t attr;
	int result;
	struct opbx_bridge_thread_obj *tobj;
	
	if((tobj = malloc(sizeof(struct opbx_bridge_thread_obj)))) {
		memset(tobj,0,sizeof(struct opbx_bridge_thread_obj));
		tobj->chan = chan;
		tobj->peer = peer;
		

		result = pthread_attr_init(&attr);
		pthread_attr_setschedpolicy(&attr, SCHED_RR);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		result = opbx_pthread_create(&thread, &attr,opbx_bridge_call_thread, tobj);
		result = pthread_attr_destroy(&attr);
	}
}


#define CGUSAGE "Usage: changrab [-[bB]] <channel> <exten>@<context> [pri]\n"
static int changrab_cli(int fd, int argc, char *argv[]) {
	char *chan_name_1, *chan_name_2 = NULL, *context,*exten,*flags=NULL;
	char *pria = NULL;
    struct opbx_channel *xferchan_1, *xferchan_2;
	int pri=0,x=1;

	if(argc < 3) {
		opbx_cli(fd,CGUSAGE);
		return -1;
	}
	chan_name_1 = argv[x++];
	if(chan_name_1[0] == '-') {
		flags = opbx_strdupa(chan_name_1);
		if (strchr(flags,'h')) {
			chan_name_1 = argv[x++];
			if((xferchan_1 = my_opbx_get_channel_by_name_locked(chan_name_1))) {
				opbx_mutex_unlock(&xferchan_1->lock);
				opbx_hangup(xferchan_1);
				opbx_verbose("OK, good luck!\n");
				return 0;
			} else 
				return -1;
		} else if (strchr(flags,'m') || strchr(flags,'M')) {
			chan_name_1 = argv[x++];
			if((xferchan_1 = my_opbx_get_channel_by_name_locked(chan_name_1))) {
				opbx_mutex_unlock(&xferchan_1->lock);
				strchr(flags,'m') ? opbx_moh_start(xferchan_1,NULL) : opbx_moh_stop(xferchan_1);
			} else 
				return 1;
			return 0;
		}
		if(argc < 4) {
			opbx_cli(fd,CGUSAGE);
			return -1;
		}
		chan_name_1 = argv[x++];
	}

	exten = opbx_strdupa(argv[x++]);
	if((context = strchr(exten,'@'))) {
		*context = 0;
		context++;
		if(!(context && exten)) {
			opbx_cli(fd,CGUSAGE);
			return -1;
		}
		if((pria = strchr(context,':'))) {
			*pria = '\0';
			pria++;
			pri = atoi(pria);
		} else {
			pri = argv[x] ? atoi(argv[x++]) : 1;
		}
		if(!pri)
			pri = 1;
	} else if (strchr(exten,'/')) {
		chan_name_2 = exten;
	}

	
	xferchan_1 = my_opbx_get_channel_by_name_locked(chan_name_1);

	if(!xferchan_1) {
		opbx_log(LOG_WARNING, "No Such Channel: %s\n",chan_name_1);
		return -1;
	} 

	opbx_mutex_unlock(&xferchan_1->lock);
	if(flags && strchr(flags,'b')) {
		if(opbx_bridged_channel(xferchan_1)) {
			xferchan_1 = opbx_bridged_channel(xferchan_1);
		}
	}

	if(chan_name_2) {
		struct opbx_frame *f;
		struct opbx_channel *newchan_1, *newchan_2;
		
		if (!(newchan_1 = opbx_channel_alloc(0))) {
			opbx_log(LOG_WARNING, "Memory Error!\n");
			opbx_hangup(newchan_1);
			return -1;
		} else {
			snprintf(newchan_1->name, sizeof (newchan_1->name), "ChanGrab/%s", xferchan_1->name);
			newchan_1->readformat = xferchan_1->readformat;
			newchan_1->writeformat = xferchan_1->writeformat;
			opbx_channel_masquerade(newchan_1, xferchan_1);
			if ((f = opbx_read(newchan_1))) {
				opbx_fr_free(f);
			} else {
				opbx_hangup(newchan_1);
				return -1;
			}
		}

		if(!(xferchan_2 = my_opbx_get_channel_by_name_locked(chan_name_2))) {
			opbx_log(LOG_WARNING, "No Such Channel: %s\n",chan_name_2);
			opbx_hangup(newchan_1);
			return -1;
		}

		opbx_mutex_unlock(&xferchan_2->lock);		

		if(flags && strchr(flags, 'B')) {
			if(opbx_bridged_channel(xferchan_2)) {
				xferchan_2 = opbx_bridged_channel(xferchan_2);
			}
		}

		if(!(newchan_2 = opbx_channel_alloc(0))) {
			opbx_log(LOG_WARNING, "Memory Error!\n");
			opbx_hangup(newchan_1);
			return -1;
		} else {
			snprintf(newchan_2->name, sizeof (newchan_2->name), "ChanGrab/%s", xferchan_2->name);
			newchan_2->readformat = xferchan_2->readformat;
			newchan_2->writeformat = xferchan_2->writeformat;
			opbx_channel_masquerade(newchan_2, xferchan_2);

			if ((f = opbx_read(newchan_2))) {
				opbx_fr_free(f);
			} else {
				opbx_hangup(newchan_1);
				opbx_hangup(newchan_2);
				return -1;
			}
		}
		
		opbx_bridge_call_thread_launch(newchan_1, newchan_2);
		
	} else {
		opbx_verbose("Transferring_to context %s, extension %s, priority %d\n", context, exten, pri);
		opbx_async_goto(xferchan_1, context, exten, pri);

		if(xferchan_1)
			opbx_mutex_unlock(&xferchan_1->lock);
	}
	return 0;
}

struct fopbx_originate_helper {
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
	struct opbx_variable *vars;
};



#define USAGE "Usage: originate <channel> <exten>@<context> [pri] [callerid] [timeout]\n"

static void *originate(void *arg) {
	struct fopbx_originate_helper *in = arg;
	int reason=0;
	int res;
	struct opbx_channel *chan = NULL;

	res = opbx_pbx_outgoing_exten(in->tech, OPBX_FORMAT_SLINEAR, in->data, in->timeout, in->context, in->exten, in->priority, &reason, 1, !opbx_strlen_zero(in->cid_num) ? in->cid_num : NULL, !opbx_strlen_zero(in->cid_name) ? in->cid_name : NULL, NULL, &chan);
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
			opbx_control2str(reason)
	);

	/* Locked by opbx_pbx_outgoing_exten or opbx_pbx_outgoing_app */
	if (chan) {
		opbx_mutex_unlock(&chan->lock);
	}
	free(in);
	return NULL;
}


static int originate_cli(int fd, int argc, char *argv[]) {
	char *chan_name_1,*context,*exten,*tech,*data,*callerid;
	int pri=0,to=60000;
	struct fopbx_originate_helper *in;
	pthread_t thread;
	pthread_attr_t attr;
	int result;
	char *num = NULL;

	if(argc < 3) {
		opbx_cli(fd,USAGE);
		return -1;
	}
	chan_name_1 = argv[1];

	exten = opbx_strdupa(argv[2]);
	if((context = strchr(exten,'@'))) {
		*context = 0;
		context++;
	}
	if(! (context && exten)) {
		opbx_cli(fd,CGUSAGE);
        return -1;
	}


	pri = argv[3] ? atoi(argv[3]) : 1;
	if(!pri)
		pri = 1;

	tech = opbx_strdupa(chan_name_1);
	if((data = strchr(tech,'/'))) {
		*data = '\0';
		data++;
	}
	if(!(tech && data)) {
		opbx_cli(fd,USAGE);
        return -1;
	}
	in = malloc(sizeof(struct fopbx_originate_helper));
	if(!in) {
		opbx_cli(fd,"No Memory!\n");
        return -1;
	}
	memset(in,0,sizeof(struct fopbx_originate_helper));
	
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

	opbx_cli(fd,"Originating Call %s/%s %s %s %d\n",in->tech,in->data,in->context,in->exten,in->priority);


	result = pthread_attr_init(&attr);
	pthread_attr_setschedpolicy(&attr, SCHED_RR);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	result = opbx_pthread_create(&thread, &attr,originate,in);
	result = pthread_attr_destroy(&attr);	
	return 0;
}



static char *complete_exten_at_context(char *line, char *word, int pos,
	int state)
{
	char *ret = NULL;
	int which = 0;

#ifdef BROKEN_READLINE
	/*
	 * Fix arguments, *word is a new allocated structure, REMEMBER to
	 * free *word when you want to return from this function ...
	 */
	if (fix_complete_args(line, &word, &pos)) {
		opbx_log(LOG_ERROR, "Out of free memory\n");
		return NULL;
	}
#endif

	/*
	 * exten@context completion ... 
	 */
	if (pos == 2) {
		struct opbx_context *c;
		struct opbx_exten *e;
		char *context = NULL, *exten = NULL, *delim = NULL;

		/* now, parse values from word = exten@context */
		if ((delim = strchr(word, (int)'@'))) {
			/* check for duplicity ... */
			if (delim != strrchr(word, (int)'@')) {
#ifdef BROKEN_READLINE
				free(word);
#endif
				return NULL;
			}

			*delim = '\0';
			exten = strdup(word);
			context = strdup(delim + 1);
			*delim = '@';
		} else {
			exten = strdup(word);
		}
#ifdef BROKEN_READLINE
		free(word);
#endif

		if (opbx_lock_contexts()) {
			opbx_log(LOG_ERROR, "Failed to lock context list\n");
			free(context); free(exten);
			return NULL;
		}

		/* find our context ... */
		c = opbx_walk_contexts(NULL); 
		while (c) {
			/* our context? */
			if ( (!context || !strlen(context)) || /* if no input, all contexts ... */
				 (context && !strncmp(opbx_get_context_name(c),
				              context, strlen(context))) ) { /* if input, compare ... */
				/* try to complete extensions ... */
				e = opbx_walk_context_extensions(c, NULL);
				while (e) {

					if(!strncasecmp(opbx_get_context_name(c), "proc-", 5) || !strncasecmp(opbx_get_extension_name(e),"_",1)) {
						e = opbx_walk_context_extensions(c, e);
						continue;
					}

					/* our extension? */
					if ( (!exten || !strlen(exten)) ||  /* if not input, all extensions ... */
						 (exten && !strncmp(opbx_get_extension_name(e), exten,
						                    strlen(exten))) ) { /* if input, compare ... */
								
						if (++which > state) {
							/* If there is an extension then return
							 * exten@context.
							 */


							if (exten) {
								ret = malloc(strlen(opbx_get_extension_name(e)) +
									strlen(opbx_get_context_name(c)) + 2);
								if (ret)
									sprintf(ret, "%s@%s", opbx_get_extension_name(e),
											opbx_get_context_name(c));
								
							}
							free(exten); free(context);

							opbx_unlock_contexts();
	
							return ret;
						}
					}
					e = opbx_walk_context_extensions(c, e);
				}
			}
			c = opbx_walk_contexts(c);
		}

		opbx_unlock_contexts();

		free(exten); free(context);

		return NULL;
	}

	/*
	 * Complete priority ...
	 */
	if (pos == 3) {
		char *delim, *exten, *context, *dupline, *duplinet, *ec;
		struct opbx_context *c;

		dupline = strdup(line);
		if (!dupline) {
#ifdef BROKEN_READLINE
			free(word);
#endif
			return NULL;
		}
		duplinet = dupline;

		strsep(&duplinet, " "); /* skip 'remove' */
		strsep(&duplinet, " "); /* skip 'extension */

		if (!(ec = strsep(&duplinet, " "))) {
			free(dupline);
#ifdef BROKEN_READLINE
			free(word);
#endif
			return NULL;
		}

		/* wrong exten@context format? */
		if (!(delim = strchr(ec, (int)'@')) ||
			(strchr(ec, (int)'@') != strrchr(ec, (int)'@'))) {
#ifdef BROKEN_READLINE
			free(word);
#endif
			free(dupline);
			return NULL;
		}

		/* check if there is exten and context too ... */
		*delim = '\0';
		if ((!strlen(ec)) || (!strlen(delim + 1))) {
#ifdef BROKEN_READLINE
			free(word);
#endif
			free(dupline);
			return NULL;
		}

		exten = strdup(ec);
		context = strdup(delim + 1);
		free(dupline);

		if (opbx_lock_contexts()) {
			opbx_log(LOG_ERROR, "Failed to lock context list\n");
#ifdef BROKEN_READLINE
			free(word);
#endif
			free(exten); free(context);
			return NULL;
		}

		/* walk contexts */
		c = opbx_walk_contexts(NULL); 
		while (c) {
			if (!strcmp(opbx_get_context_name(c), context)) {
				struct opbx_exten *e;

				/* walk extensions */
				free(context);
				e = opbx_walk_context_extensions(c, NULL); 
				while (e) {
					if (!strcmp(opbx_get_extension_name(e), exten)) {
						struct opbx_exten *priority;
						char buffer[10];
					
						free(exten);
						priority = opbx_walk_extension_priorities(e, NULL);
						/* serve priorities */
						do {
							snprintf(buffer, 10, "%u",
								opbx_get_extension_priority(priority));
							if (!strncmp(word, buffer, strlen(word))) {
								if (++which > state) {
#ifdef BROKEN_READLINE
									free(word);
#endif
									opbx_unlock_contexts();
									return strdup(buffer);
								}
							}
							priority = opbx_walk_extension_priorities(e,
								priority);
						} while (priority);

#ifdef BROKEN_READLINE
						free(word);
#endif
						opbx_unlock_contexts();
						return NULL;			
					}
					e = opbx_walk_context_extensions(c, e);
				}
#ifdef BROKEN_READLINE
				free(word);
#endif
				free(exten);
				opbx_unlock_contexts();
				return NULL;
			}
			c = opbx_walk_contexts(c);
		}

#ifdef BROKEN_READLINE
		free(word);
#endif
		free(exten); free(context);

		opbx_unlock_contexts();
		return NULL;
	}

#ifdef BROKEN_READLINE
	free(word);
#endif
	return NULL; 
}


static char *complete_ch_helper(char *line, char *word, int pos, int state)
{
    struct opbx_channel *c;
    int which=0;
    char *ret;

    c = opbx_channel_walk_locked(NULL);
    while(c) {
        if (!strncasecmp(word, c->name, strlen(word))) {
            if (++which > state)
                break;
        }
        opbx_mutex_unlock(&c->lock);
        c = opbx_channel_walk_locked(c);
    }
    if (c) {
        ret = strdup(c->name);
        opbx_mutex_unlock(&c->lock);
    } else
        ret = NULL;
    return ret;
}

static char *complete_cg(char *line, char *word, int pos, int state)
{

	if(pos == 1) {
		return complete_ch_helper(line, word, pos, state);
	}
	else if(pos >= 2) {
		return complete_exten_at_context(line, word, pos, state);
	}
	return NULL;

}

static char *complete_org(char *line, char *word, int pos, int state)
{

	if(pos >= 2) {
		return complete_exten_at_context(line, word, pos, state);
	}
	return NULL;

}


static struct opbx_cli_entry  cli_changrab = { { "changrab", NULL}, changrab_cli, "ChanGrab", "ChanGrab", complete_cg };
static struct opbx_cli_entry  cli_originate = { { "originate", NULL }, originate_cli, "Originate", "Originate", complete_org};

int unload_module(void)
{
	int res = 0;
	STANDARD_HANGUP_LOCALUSERS;
	opbx_cli_unregister(&cli_changrab);
	opbx_cli_unregister(&cli_originate);
	res |= opbx_unregister_application(changrab_app);
	return res;
}

int load_module(void)
{
	opbx_cli_register(&cli_changrab);
	opbx_cli_register(&cli_originate);
	changrab_app = opbx_register_application(changrab_name, changrab_exec, tdesc, changrab_syntax, changrab_description);
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

