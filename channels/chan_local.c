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

/*
 *
 * Local Proxy Channel
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/channel.h"
#include "callweaver/config.h"
#include "callweaver/logger.h"
#include "callweaver/module.h"
#include "callweaver/pbx.h"
#include "callweaver/options.h"
#include "callweaver/lock.h"
#include "callweaver/sched.h"
#include "callweaver/io.h"
#include "callweaver/acl.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/file.h"
#include "callweaver/cli.h"
#include "callweaver/app.h"
#include "callweaver/musiconhold.h"
#include "callweaver/manager.h"

static const char desc[] = "Local Proxy Channel";
static const char type[] = "Local";
static const char tdesc[] = "Local Proxy Channel Driver";

static int usecnt =0;
CW_MUTEX_DEFINE_STATIC(usecnt_lock);

#define IS_OUTBOUND(a,b) (a == b->chan ? 1 : 0)

/* Protect the interface list (of sip_pvt's) */
CW_MUTEX_DEFINE_STATIC(locallock);

static struct cw_channel *local_request(const char *type, int format, void *data, int *cause);
static int local_digit(struct cw_channel *ast, char digit);
static int local_call(struct cw_channel *ast, char *dest);
static int local_hangup(struct cw_channel *ast);
static int local_answer(struct cw_channel *ast);
static struct cw_frame *local_read(struct cw_channel *ast);
static int local_write(struct cw_channel *ast, struct cw_frame *f);
static int local_indicate(struct cw_channel *ast, int condition);
static int local_fixup(struct cw_channel *oldchan, struct cw_channel *newchan);
static int local_sendhtml(struct cw_channel *ast, int subclass, const char *data, int datalen);

/* PBX interface structure for channel registration */
static const struct cw_channel_tech local_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = -1,
	.requester = local_request,
	.send_digit = local_digit,
	.call = local_call,
	.hangup = local_hangup,
	.answer = local_answer,
	.read = local_read,
	.write = local_write,
	.exception = local_read,
	.indicate = local_indicate,
	.fixup = local_fixup,
	.send_html = local_sendhtml,
};

static struct local_pvt {
	cw_mutex_t lock;			/* Channel private lock */
	char context[CW_MAX_CONTEXT];		/* Context to call */
	char exten[CW_MAX_EXTENSION];		/* Extension to call */
	int reqformat;				/* Requested format */
	int glaredetect;			/* Detect glare on hangup */
	int cancelqueue;			/* Cancel queue */
	int alreadymasqed;			/* Already masqueraded */
	int launchedpbx;			/* Did we launch the PBX */
	int nooptimization;			/* Don't leave masq state */
	struct cw_channel *owner;		/* Master Channel */
	struct cw_channel *chan;		/* Outbound channel */
	struct local_pvt *next;			/* Next entity */
} *locals = NULL;

static int local_queue_frame(struct local_pvt *p, int isoutbound, struct cw_frame *f, struct cw_channel *us)
{
	struct cw_channel *other;
retrylock:		
	/* Recalculate outbound channel */
	if (isoutbound) {
		other = p->owner;
	} else {
		other = p->chan;
	}
	/* Set glare detection */
	p->glaredetect = 1;
	if (p->cancelqueue) {
		/* We had a glare on the hangup.  Forget all this business,
		return and destroy p.  */
		cw_mutex_unlock(&p->lock);
		cw_mutex_destroy(&p->lock);
		free(p);
		return -1;
	}
	if (!other) {
		p->glaredetect = 0;
		return 0;
	}
	if (cw_channel_trylock(other)) {
		/* Failed to lock.  Release main lock and try again */
		cw_mutex_unlock(&p->lock);
		if (us) {
			if (cw_channel_unlock(us)) {
				cw_log(CW_LOG_WARNING, "%s wasn't locked while sending %d/%d\n",
					us->name, f->frametype, f->subclass);
				us = NULL;
			}
		}
		/* Wait just a bit */
		usleep(1);
		/* Only we can destroy ourselves, so we can't disappear here */
		if (us)
			cw_channel_lock(us);
		cw_mutex_lock(&p->lock);
		goto retrylock;
	}
	cw_queue_frame(other, f);
	cw_channel_unlock(other);
	p->glaredetect = 0;
	return 0;
}

static int local_answer(struct cw_channel *ast)
{
	struct local_pvt *p = ast->tech_pvt;
	int isoutbound;
	int res = -1;

	cw_mutex_lock(&p->lock);
	isoutbound = IS_OUTBOUND(ast, p);
	if (isoutbound) {
		/* Pass along answer since somebody answered us */
		struct cw_frame answer = { CW_FRAME_CONTROL, CW_CONTROL_ANSWER };
		res = local_queue_frame(p, isoutbound, &answer, ast);
	} else
		cw_log(CW_LOG_WARNING, "Huh?  Local is being asked to answer?\n");
	cw_mutex_unlock(&p->lock);
	return res;
}

static void check_bridge(struct local_pvt *p, int isoutbound)
{
	if (p->alreadymasqed || p->nooptimization)
		return;
	if (!p->chan || !p->owner)
		return;

	/* only do the masquerade if we are being called on the outbound channel,
	   if it has been bridged to another channel and if there are no pending
	   frames on the owner channel (because they would be transferred to the
	   outbound channel during the masquerade)
	*/
	if (isoutbound && p->chan->_bridge /* Not cw_bridged_channel!  Only go one step! */ && !p->owner->readq) {
		/* Masquerade bridged channel into owner */
		/* Lock everything we need, one by one, and give up if
		   we can't get everything.  Remember, we'll get another
		   chance in just a little bit */

		if (!cw_channel_trylock(p->chan->_bridge)) {
			if (!p->chan->_bridge->_softhangup) {
				if (!cw_channel_trylock(p->owner)) {
					if (!p->owner->_softhangup) {
						cw_channel_masquerade(p->owner, p->chan->_bridge);
						p->alreadymasqed = 1;
					}
					cw_channel_unlock(p->owner);
				}
				cw_channel_unlock(p->chan->_bridge);
			}
		}
	/* We only allow masquerading in one 'direction'... it's important to preserve the state
	   (group variables, etc.) that live on p->chan->_bridge (and were put there by the dialplan)
	   when the local channels go away.
	*/
#if 0
	} else if (!isoutbound && p->owner && p->owner->_bridge && p->chan && !p->chan->readq) {
		/* Masquerade bridged channel into chan */
		if (!cw_channel_trylock(p->owner->_bridge)) {
			if (!p->owner->_bridge->_softhangup) {
				if (!cw_channel_trylock(p->chan)) {
					if (!p->chan->_softhangup) {
						cw_channel_masquerade(p->chan, p->owner->_bridge);
						p->alreadymasqed = 1;
					}
					cw_channel_unlock(p->chan);
				}
			}
			cw_channel_unlock(p->owner->_bridge);
		}
#endif
	}
}

static struct cw_frame  *local_read(struct cw_channel *ast)
{
	static struct cw_frame null = { CW_FRAME_NULL, };

	return &null;
}

static int local_write(struct cw_channel *ast, struct cw_frame *f)
{
	struct local_pvt *p = ast->tech_pvt;
	int res = -1;
	int isoutbound;

	/* Just queue for delivery to the other side */
	cw_mutex_lock(&p->lock);
	isoutbound = IS_OUTBOUND(ast, p);
	if (f && (f->frametype == CW_FRAME_VOICE)) 
		check_bridge(p, isoutbound);
	if (!p->alreadymasqed)
		res = local_queue_frame(p, isoutbound, f, ast);
	else {
		cw_log(CW_LOG_DEBUG, "Not posting to queue since already masked on '%s'\n", ast->name);
		res = 0;
	}
	cw_mutex_unlock(&p->lock);
	return res;
}

static int local_fixup(struct cw_channel *oldchan, struct cw_channel *newchan)
{
	struct local_pvt *p = newchan->tech_pvt;
	cw_mutex_lock(&p->lock);

	if ((p->owner != oldchan) && (p->chan != oldchan)) {
		cw_log(CW_LOG_WARNING, "Old channel wasn't %p but was %p/%p\n", oldchan, p->owner, p->chan);
		cw_mutex_unlock(&p->lock);
		return -1;
	}
	if (p->owner == oldchan)
		p->owner = newchan;
	else
		p->chan = newchan;
	cw_mutex_unlock(&p->lock);
	return 0;
}

static int local_indicate(struct cw_channel *ast, int condition)
{
	struct local_pvt *p = ast->tech_pvt;
	int res = -1;
	struct cw_frame f = { CW_FRAME_CONTROL, };
	int isoutbound;

	/* Queue up a frame representing the indication as a control frame */
	cw_mutex_lock(&p->lock);
	isoutbound = IS_OUTBOUND(ast, p);
	f.subclass = condition;
	res = local_queue_frame(p, isoutbound, &f, ast);
	cw_mutex_unlock(&p->lock);
	return res;
}

static int local_digit(struct cw_channel *ast, char digit)
{
	struct local_pvt *p = ast->tech_pvt;
	int res = -1;
	struct cw_frame f = { CW_FRAME_DTMF, };
	int isoutbound;

	cw_mutex_lock(&p->lock);
	isoutbound = IS_OUTBOUND(ast, p);
	f.subclass = digit;
	res = local_queue_frame(p, isoutbound, &f, ast);
	cw_mutex_unlock(&p->lock);
	return res;
}

static int local_sendhtml(struct cw_channel *ast, int subclass, const char *data, int datalen)
{
	struct local_pvt *p = ast->tech_pvt;
	int res = -1;
	struct cw_frame f = { CW_FRAME_HTML, };
	int isoutbound;

	cw_mutex_lock(&p->lock);
	isoutbound = IS_OUTBOUND(ast, p);
	f.subclass = subclass;
	f.data = (char *)data;
	f.datalen = datalen;
	res = local_queue_frame(p, isoutbound, &f, ast);
	cw_mutex_unlock(&p->lock);
	return res;
}

/*--- local_call: Initiate new call, part of PBX interface */
/* 	dest is the dial string */
static int local_call(struct cw_channel *ast, char *dest)
{
	struct local_pvt *p = ast->tech_pvt;
	int res;
	struct cw_var_t *varptr = NULL, *new;
	size_t len, namelen;
	
	cw_mutex_lock(&p->lock);
	if (p->owner->cid.cid_num)
		p->chan->cid.cid_num = strdup(p->owner->cid.cid_num);
	else 
		p->chan->cid.cid_num = NULL;

	if (p->owner->cid.cid_name)
		p->chan->cid.cid_name = strdup(p->owner->cid.cid_name);
	else 
		p->chan->cid.cid_name = NULL;

	if (p->owner->cid.cid_rdnis)
		p->chan->cid.cid_rdnis = strdup(p->owner->cid.cid_rdnis);
	else
		p->chan->cid.cid_rdnis = NULL;

	if (p->owner->cid.cid_ani)
		p->chan->cid.cid_ani = strdup(p->owner->cid.cid_ani);
	else
		p->chan->cid.cid_ani = NULL;

	strncpy(p->chan->language, p->owner->language, sizeof(p->chan->language) - 1);
	strncpy(p->chan->accountcode, p->owner->accountcode, sizeof(p->chan->accountcode) - 1);
	p->chan->cdrflags = p->owner->cdrflags;

	/* copy the channel variables from the incoming channel to the outgoing channel */
	/* Note that due to certain assumptions, they MUST be in the same order */
	cw_var_copy(&p->owner->vars, &p->chan->vars);

	p->launchedpbx = 1;

	/* Start switch on sub channel */
	res = cw_pbx_start(p->chan);
	cw_mutex_unlock(&p->lock);
	return res;
}

#if 0
static void local_destroy(struct local_pvt *p)
{
	struct local_pvt *cur, *prev = NULL;
	cw_mutex_lock(&locallock);
	cur = locals;
	while(cur) {
		if (cur == p) {
			if (prev)
				prev->next = cur->next;
			else
				locals = cur->next;
			cw_mutex_destroy(cur);
			free(cur);
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	cw_mutex_unlock(&locallock);
	if (!cur)
		cw_log(CW_LOG_WARNING, "Unable to find local '%s@%s' in local list\n", p->exten, p->context);
}
#endif

/*--- local_hangup: Hangup a call through the local proxy channel */
static int local_hangup(struct cw_channel *ast)
{
	struct local_pvt *p = ast->tech_pvt;
	int isoutbound;
	struct cw_frame f = { CW_FRAME_CONTROL, CW_CONTROL_HANGUP };
	struct local_pvt *cur, *prev=NULL;
	struct cw_channel *ochan = NULL;
	int glaredetect;
	int res=0;
	
	cw_mutex_lock(&p->lock);
	isoutbound = IS_OUTBOUND(ast, p);
	if (isoutbound) {
		p->chan = NULL;
		p->launchedpbx = 0;
	} else
		p->owner = NULL;
	ast->tech_pvt = NULL;
	
	cw_mutex_lock(&usecnt_lock);
	usecnt--;
	cw_mutex_unlock(&usecnt_lock);
	
	if (!p->owner && !p->chan) {
		/* Okay, done with the private part now, too. */
		glaredetect = p->glaredetect;
		/* If we have a queue holding, don't actually destroy p yet, but
		   let local_queue do it. */
		if (p->glaredetect)
			p->cancelqueue = 1;
		cw_mutex_unlock(&p->lock);
		/* Remove from list */
		cw_mutex_lock(&locallock);
		cur = locals;
		while(cur) {
			if (cur == p) {
				if (prev)
					prev->next = cur->next;
				else
					locals = cur->next;
				break;
			}
			prev = cur;
			cur = cur->next;
		}
		cw_mutex_unlock(&locallock);
		/* Grab / release lock just in case */
		cw_mutex_lock(&p->lock);
		cw_mutex_unlock(&p->lock);
		/* And destroy */
		if (!glaredetect) {
			cw_mutex_destroy(&p->lock);
			free(p);
		}
		return 0;
	}
	if (p->chan && !p->launchedpbx)
		/* Need to actually hangup since there is no PBX */
		ochan = p->chan;
	else
		res = local_queue_frame(p, isoutbound, &f, NULL);
	if(res==0)
		cw_mutex_unlock(&p->lock);
	if (ochan)
		cw_hangup(ochan);
	return 0;
}

/*--- local_alloc: Create a call structure */
static struct local_pvt *local_alloc(char *data, int format)
{
	struct local_pvt *tmp;
	char *c;
	char *opts;

	tmp = malloc(sizeof(struct local_pvt));
	if (tmp) {
		memset(tmp, 0, sizeof(struct local_pvt));
		cw_mutex_init(&tmp->lock);
		strncpy(tmp->exten, data, sizeof(tmp->exten) - 1);
		opts = strchr(tmp->exten, '/');
		if (opts) {
			*opts='\0';
			opts++;
			if (strchr(opts, 'n'))
				tmp->nooptimization = 1;
		}
		c = strchr(tmp->exten, '@');
		if (c) {
			*c = '\0';
			c++;
			strncpy(tmp->context, c, sizeof(tmp->context) - 1);
		} else
			strncpy(tmp->context, "default", sizeof(tmp->context) - 1);
		tmp->reqformat = format;
		if (!cw_exists_extension(NULL, tmp->context, tmp->exten, 1, NULL)) {
			cw_log(CW_LOG_NOTICE, "No such extension/context %s@%s creating local channel\n", tmp->exten, tmp->context);
			cw_mutex_destroy(&tmp->lock);
			free(tmp);
			tmp = NULL;
		} else {
			/* Add to list */
			cw_mutex_lock(&locallock);
			tmp->next = locals;
			locals = tmp;
			cw_mutex_unlock(&locallock);
		}
		
	}
	return tmp;
}

/*--- local_new: Start new local channel */
static struct cw_channel *local_new(struct local_pvt *p, int state)
{
	struct cw_channel *tmp, *tmp2;
	int randnum = cw_random() & 0xffff;
	int fmt=0;

	tmp = cw_channel_alloc(1, "Local/%s@%s-%04x,1", p->exten, p->context, randnum);
	tmp2 = cw_channel_alloc(1, "Local/%s@%s-%04x,2", p->exten, p->context, randnum);
	if (!tmp || !tmp2) {
		if (tmp)
			cw_channel_free(tmp);
		if (tmp2)
			cw_channel_free(tmp2);
		cw_log(CW_LOG_WARNING, "Unable to allocate channel structure(s)\n");
		return NULL;
	} 

	tmp2->tech = tmp->tech = &local_tech;
	tmp->nativeformats = p->reqformat;
	tmp2->nativeformats = p->reqformat;
	tmp->type = type;
	tmp2->type = type;
	cw_setstate(tmp, state);
	cw_setstate(tmp2, CW_STATE_RING);

	fmt = cw_best_codec(p->reqformat);
	tmp->writeformat = fmt;
	tmp2->writeformat = fmt;
	tmp->rawwriteformat = fmt;
	tmp2->rawwriteformat = fmt;
	tmp->readformat = fmt;
	tmp2->readformat = fmt;
	tmp->rawreadformat = fmt;
	tmp2->rawreadformat = fmt;

	tmp->tech_pvt = p;
	tmp2->tech_pvt = p;
	p->owner = tmp;
	p->chan = tmp2;
	cw_mutex_lock(&usecnt_lock);
	usecnt++;
	usecnt++;
	cw_mutex_unlock(&usecnt_lock);
	cw_copy_string(tmp->context, p->context, sizeof(tmp->context));
	cw_copy_string(tmp2->context, p->context, sizeof(tmp2->context));
	cw_copy_string(tmp2->exten, p->exten, sizeof(tmp->exten));
	tmp->priority = 1;
	tmp2->priority = 1;

	return tmp;
}


/*--- local_request: Part of PBX interface */
static struct cw_channel *local_request(const char *type, int format, void *data, int *cause)
{
	struct local_pvt *p;
	struct cw_channel *chan = NULL;

	p = local_alloc(data, format);
	if (p)
		chan = local_new(p, CW_STATE_DOWN);
	return chan;
}

/*--- locals_show: CLI command "local show channels" */
static int locals_show(int fd, int argc, char **argv)
{
	struct local_pvt *p;

	if (argc != 3)
		return RESULT_SHOWUSAGE;
	cw_mutex_lock(&locallock);
	p = locals;
	while(p) {
		cw_mutex_lock(&p->lock);
		cw_cli(fd, "%s -- %s@%s\n", p->owner ? p->owner->name : "<unowned>", p->exten, p->context);
		cw_mutex_unlock(&p->lock);
		p = p->next;
	}
	if (!locals)
		cw_cli(fd, "No local channels in use\n");
	cw_mutex_unlock(&locallock);
	return RESULT_SUCCESS;
}

static char show_locals_usage[] = 
"Usage: local show channels\n"
"       Provides summary information on active local proxy channels.\n";

static struct cw_clicmd cli_show_locals = {
	.cmda = { "local", "show", "channels", NULL },
	.handler = locals_show, 
	.summary = "Show status of local channels",
	.usage = show_locals_usage,
};

/*--- load_module: Load module into PBX, register channel */
static int load_module(void)
{
	/* Make sure we can register our channel type */
	if (cw_channel_register(&local_tech)) {
		cw_log(CW_LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	cw_cli_register(&cli_show_locals);
	return 0;
}

/*--- unload_module: Unload the local proxy channel from CallWeaver */
static int unload_module(void)
{
	struct local_pvt *p;

	/* First, take us out of the channel loop */
	cw_cli_unregister(&cli_show_locals);
	cw_channel_unregister(&local_tech);
	if (!cw_mutex_lock(&locallock)) {
		/* Hangup all interfaces if they have an owner */
		p = locals;
		while(p) {
			if (p->owner)
				cw_softhangup(p->owner, CW_SOFTHANGUP_APPUNLOAD);
			p = p->next;
		}
		locals = NULL;
		cw_mutex_unlock(&locallock);
	} else {
		cw_log(CW_LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}		
	return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
