/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Eris Associates Limited, UK
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Authors:
 *     Mike Jagdis <mjagdis@eris-associates.co.uk>
 *     Mark Spencer <markster@digium.com>
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
 * \brief Call Detail Record API 
 * 
 * Includes code and algorithms from the Zapata library.
 *
 */
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/registry.h"
#include "callweaver/channel.h"
#include "callweaver/cdr.h"
#include "callweaver/logger.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/causes.h"
#include "callweaver/options.h"
#include "callweaver/linkedlists.h"
#include "callweaver/utils.h"
#include "callweaver/sched.h"
#include "callweaver/config.h"
#include "callweaver/cli.h"
#include "callweaver/atexit.h"


static int cw_cdrbe_qsort_compare_by_name(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct cw_cdrbe *cdrbe_a = container_of(*objp_a, struct cw_cdrbe, obj);
	const struct cw_cdrbe *cdrbe_b = container_of(*objp_b, struct cw_cdrbe, obj);

	return strcmp(cdrbe_a->name, cdrbe_b->name);
}

struct cw_registry cdrbe_registry = {
	.name = "CDR back-end",
	.qsort_compare = cw_cdrbe_qsort_compare_by_name,
};


int cw_default_amaflags = CW_CDR_DOCUMENTATION;
int cw_end_cdr_before_h_exten;
char cw_default_accountcode[CW_MAX_ACCOUNT_CODE] = "";


static struct {
	int size;
	struct cw_cdr *head;
	struct cw_cdr **tail;
} curbatch;

static pthread_t cdr_thread = CW_PTHREADT_NULL;

#define BATCH_SIZE_DEFAULT 100
#define BATCH_TIME_DEFAULT 300
#define BATCH_SCHEDULER_ONLY_DEFAULT 0

static int enabled;

pthread_mutex_t cdr_batch_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cdr_batch_cond = PTHREAD_COND_INITIALIZER;


struct cw_cdr *cw_cdr_dup(struct cw_cdr *cdr) 
{
	struct cw_cdr *newcdr;

	if ((newcdr = malloc(sizeof(*newcdr)))) {
		memcpy(newcdr, cdr, sizeof(*newcdr));
		cw_var_registry_init(&newcdr->vars, 256);
		cw_var_copy(&newcdr->vars, &cdr->vars);
	} else
		cw_log(CW_LOG_ERROR, "Out of memory\n");

	return newcdr;
}

static struct cw_var_t *cw_cdr_getvar_internal(struct cw_cdr *cdr, const char *name, int recur)
{
	struct cw_object *obj;
	unsigned int hash;

	if (cdr && !cw_strlen_zero(name)) {
		hash = cw_hash_var_name(name);

		do {
			if ((obj = cw_registry_find(&cdr->vars, 1, hash, name)))
				return container_of(obj, struct cw_var_t, obj);
			cdr = cdr->next;
		} while (!obj && recur && cdr);
	}

	return NULL;
}

void cw_cdr_getvar(struct cw_cdr *cdr, const char *name, char **ret, char *workspace, int workspacelen, int recur) 
{
	struct tm tm;
	time_t t;
	struct cw_var_t *var;
	const char *fmt = "%Y-%m-%d %T";

	*ret = NULL;
	/* special vars (the ones from the struct cw_cdr when requested by name) 
	   I'd almost say we should convert all the stringed vals to vars */

	if (!strcasecmp(name, "clid"))
		cw_copy_string(workspace, cdr->clid, workspacelen);
	else if (!strcasecmp(name, "src"))
		cw_copy_string(workspace, cdr->src, workspacelen);
	else if (!strcasecmp(name, "dst"))
		cw_copy_string(workspace, cdr->dst, workspacelen);
	else if (!strcasecmp(name, "dcontext"))
		cw_copy_string(workspace, cdr->dcontext, workspacelen);
	else if (!strcasecmp(name, "channel"))
		cw_copy_string(workspace, cdr->channel, workspacelen);
	else if (!strcasecmp(name, "dstchannel"))
		cw_copy_string(workspace, cdr->dstchannel, workspacelen);
	else if (!strcasecmp(name, "lastapp"))
		cw_copy_string(workspace, cdr->lastapp, workspacelen);
	else if (!strcasecmp(name, "lastdata"))
		cw_copy_string(workspace, cdr->lastdata, workspacelen);
	else if (!strcasecmp(name, "start")) {
		t = cdr->start.tv_sec;
		if (t) {
			localtime_r(&t, &tm);
			strftime(workspace, workspacelen, fmt, &tm);
		}
	} else if (!strcasecmp(name, "answer")) {
		t = cdr->answer.tv_sec;
		if (t) {
			localtime_r(&t, &tm);
			strftime(workspace, workspacelen, fmt, &tm);
		}
	} else if (!strcasecmp(name, "end")) {
		t = cdr->end.tv_sec;
		if (t) {
			localtime_r(&t, &tm);
			strftime(workspace, workspacelen, fmt, &tm);
		}
	} else if (!strcasecmp(name, "duration"))
		snprintf(workspace, workspacelen, "%d", cdr->duration);
	else if (!strcasecmp(name, "billsec"))
		snprintf(workspace, workspacelen, "%d", cdr->billsec);
	else if (!strcasecmp(name, "disposition"))
		cw_copy_string(workspace, cw_cdr_disp2str(cdr->disposition), workspacelen);
	else if (!strcasecmp(name, "amaflags"))
		cw_copy_string(workspace, cw_cdr_flags2str(cdr->amaflags), workspacelen);
	else if (!strcasecmp(name, "accountcode"))
		cw_copy_string(workspace, cdr->accountcode, workspacelen);
	else if (!strcasecmp(name, "uniqueid"))
		cw_copy_string(workspace, cdr->uniqueid, workspacelen);
	else if (!strcasecmp(name, "userfield"))
		cw_copy_string(workspace, cdr->userfield, workspacelen);
	else if ((var = cw_cdr_getvar_internal(cdr, name, recur))) {
		cw_copy_string(workspace, var->value, workspacelen);
		cw_object_put(var);
	}

	if (!cw_strlen_zero(workspace))
		*ret = workspace;
}

int cw_cdr_setvar(struct cw_cdr *cdr, const char *name, const char *value, int recur) 
{
	static const char *read_only[] = { "clid", "src", "dst", "dcontext", "channel", "dstchannel",
				    "lastapp", "lastdata", "start", "answer", "end", "duration",
				    "billsec", "disposition", "amaflags", "accountcode", "uniqueid",
				    "userfield", NULL };
	struct cw_var_t *var;
	unsigned int hash;
	int x;

	for (x = 0; read_only[x]; x++) {
		if (!strcasecmp(name, read_only[x])) {
			cw_log(CW_LOG_ERROR, "Attempt to set a read-only variable!.\n");
			return -1;
		}
	}

	if (cdr) {
		if (value) {
			var = cw_var_new(name, value, 1);
			hash = var->hash;
		} else {
			var = NULL;
			hash = cw_hash_var_name(name);
		}

		do {
			cw_registry_replace(&cdr->vars, hash, name, (var ? &var->obj : NULL));
			cdr = cdr->next;
		} while (recur && cdr);

		if (var)
			cw_object_put(var);
	} else {
		cw_log(CW_LOG_ERROR, "Attempt to set a variable on a nonexistent CDR record.\n");
		return -1;
	}

	return 0;
}


struct cdr_serialize_args {
	char **buf_p;
	size_t *size_p;
	char delim;
	char sep;
	int x, total;
};

static int cdr_serialize_one(struct cw_object *obj, void *data)
{
	struct cw_var_t *var = container_of(obj, struct cw_var_t, obj);
	struct cdr_serialize_args *args = data;
	int ret = 0;

	if (!cw_build_string(args->buf_p, args->size_p, "level %d: %s%c%s%c", args->x, cw_var_name(var), args->delim, var->value, args->sep))
		args->total++;
	else {
		cw_log(CW_LOG_ERROR, "Data Buffer Size Exceeded!\n");
		ret = 1;
	}

	return ret;
}

int cw_cdr_serialize_variables(struct cw_cdr *cdr, char *buf, size_t size, char delim, char sep, int recur) 
{
	static const char *cdrcols[] = {
		"clid",
		"src",
		"dst",
		"dcontext",
		"channel",
		"dstchannel",
		"lastapp",
		"lastdata",
		"start",
		"answer",
		"end",
		"duration",
		"billsec",
		"disposition",
		"amaflags",
		"accountcode",
		"uniqueid",
		"userfield"
	};
	char workspace[256];
	struct cdr_serialize_args args = {
		.buf_p = &buf,
		.size_p = &size,
		.delim = delim,
		.sep = sep,
		.x = 0,
		.total = 0,
	};
	char *tmp;
	int i;

	memset(buf, 0, size);

	for (; cdr; cdr = (recur ? cdr->next : NULL)) {
		if (++args.x > 1)
			cw_build_string(&buf, &size, "\n");

		cw_registry_iterate_ordered(&cdr->vars, cdr_serialize_one, &args);

		for (i = 0; i < (sizeof(cdrcols) / sizeof(cdrcols[0])); i++) {
			cw_cdr_getvar(cdr, cdrcols[i], &tmp, workspace, sizeof(workspace), 0);
			if (tmp) {
				if (!cw_build_string(&buf, &size, "level %d: %s%c%s%c", args.x, cdrcols[i], delim, tmp, sep))
					args.total++;
				else {
					cw_log(CW_LOG_ERROR, "Data Buffer Size Exceeded!\n");
					break;
				}
			}
		}
	}

	return args.total;
}


void cw_cdr_free(struct cw_cdr *batch)
{
	struct cw_cdr *cdr, *next;
	const char *chan;

	while ((cdr = batch)) {
		batch = batch->batch_next;

		while (cdr) {
			next = cdr->next;

			chan = !cw_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (!cw_test_flag(cdr, CW_CDR_FLAG_POSTED))
				cw_log(CW_LOG_WARNING, "CDR on channel '%s' not posted\n", chan);
			if (cw_tvzero(cdr->end))
				cw_log(CW_LOG_WARNING, "CDR on channel '%s' lacks end\n", chan);
			if (cw_tvzero(cdr->start))
				cw_log(CW_LOG_WARNING, "CDR on channel '%s' lacks start\n", chan);

			cw_registry_destroy(&cdr->vars);
			free(cdr);
			cdr = next;
		}
	}
}

void cw_cdr_start(struct cw_cdr *cdr)
{
	const char *chan;

	while (cdr) {
		if (!cw_test_flag(cdr, CW_CDR_FLAG_LOCKED)) {
			chan = !cw_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (cw_test_flag(cdr, CW_CDR_FLAG_POSTED))
				cw_log(CW_LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
			if (!cw_tvzero(cdr->start))
				cw_log(CW_LOG_WARNING, "CDR on channel '%s' already started\n", chan);
			cdr->start = cw_tvnow();
		}
		cdr = cdr->next;
	}
}

void cw_cdr_answer(struct cw_cdr *cdr)
{
	const char *chan;

	while (cdr) {
		chan = !cw_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (cw_test_flag(cdr, CW_CDR_FLAG_POSTED))
			cw_log(CW_LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (cdr->disposition < CW_CDR_ANSWERED)
			cdr->disposition = CW_CDR_ANSWERED;
		if (cw_tvzero(cdr->answer))
			cdr->answer = cw_tvnow();
		cdr = cdr->next;
	}
}

void cw_cdr_busy(struct cw_cdr *cdr)
{
	const char *chan;

	while (cdr) {
		if (!cw_test_flag(cdr, CW_CDR_FLAG_LOCKED)) {
			chan = !cw_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (cw_test_flag(cdr, CW_CDR_FLAG_POSTED))
				cw_log(CW_LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
			if (cdr->disposition < CW_CDR_BUSY)
				cdr->disposition = CW_CDR_BUSY;
		}
		cdr = cdr->next;
	}
}

void cw_cdr_failed(struct cw_cdr *cdr)
{
	const char *chan;

	while (cdr) {
		chan = !cw_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (cw_test_flag(cdr, CW_CDR_FLAG_POSTED))
			cw_log(CW_LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (!cw_test_flag(cdr, CW_CDR_FLAG_LOCKED))
			cdr->disposition = CW_CDR_FAILED;
		cdr = cdr->next;
	}
}

int cw_cdr_disposition(struct cw_cdr *cdr, int cause)
{
	int res = 0;

	while (cdr) {
		switch(cause) {
		case CW_CAUSE_BUSY:
			cw_cdr_busy(cdr);
			break;
		case CW_CAUSE_FAILURE:
			cw_cdr_failed(cdr);
			break;
		case CW_CAUSE_NORMAL:
			break;
		case CW_CAUSE_NOTDEFINED:
			res = -1;
			break;
		default:
			res = -1;
			cw_log(CW_LOG_WARNING, "Cause not handled\n");
		}
		cdr = cdr->next;
	}
	return res;
}

void cw_cdr_setdestchan(struct cw_cdr *cdr, const char *chann)
{
	const char *chan;

	while (cdr) {
		chan = !cw_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (cw_test_flag(cdr, CW_CDR_FLAG_POSTED))
			cw_log(CW_LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (!cw_test_flag(cdr, CW_CDR_FLAG_LOCKED))
			cw_copy_string(cdr->dstchannel, chann, sizeof(cdr->dstchannel));
		cdr = cdr->next;
	}
}

void cw_cdr_setapp(struct cw_cdr *cdr, const char *app, const char *data)
{
	const char *chan;

	while (cdr) {
		if (!cw_test_flag(cdr, CW_CDR_FLAG_LOCKED)) {
			chan = !cw_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (cw_test_flag(cdr, CW_CDR_FLAG_POSTED))
				cw_log(CW_LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
			if (!app)
				app = "";
			cw_copy_string(cdr->lastapp, app, sizeof(cdr->lastapp));
			if (!data)
				data = "";
			cw_copy_string(cdr->lastdata, data, sizeof(cdr->lastdata));
		}
		cdr = cdr->next;
	}
}

int cw_cdr_setcid(struct cw_cdr *cdr, struct cw_channel *c)
{
	char tmp[CW_MAX_EXTENSION] = "";
	char *num;

	while (cdr) {
		if (!cw_test_flag(cdr, CW_CDR_FLAG_LOCKED)) {
			/* Grab source from ANI or normal Caller*ID */
			num = c->cid.cid_ani ? c->cid.cid_ani : c->cid.cid_num;

			if (c->cid.cid_name && num)
				snprintf(tmp, sizeof(tmp), "\"%s\" <%s>", c->cid.cid_name, num);
			else if (c->cid.cid_name)
				cw_copy_string(tmp, c->cid.cid_name, sizeof(tmp));
			else if (num)
				cw_copy_string(tmp, num, sizeof(tmp));
			cw_copy_string(cdr->clid, tmp, sizeof(cdr->clid));
			cw_copy_string(cdr->src, num ? num : "", sizeof(cdr->src));
		}
		cdr = cdr->next;
	}

	return 0;
}


int cw_cdr_alloc(struct cw_channel *chan)
{
	char *num;

	if ((chan->cdr = calloc(1, sizeof(*chan->cdr)))) {
		cw_var_registry_init(&chan->cdr->vars, 256);

		cw_copy_string(chan->cdr->channel, chan->name, sizeof(chan->cdr->channel));

		/* Grab source from ANI or normal Caller*ID */
		num = (chan->cid.cid_ani ? chan->cid.cid_ani : chan->cid.cid_num);

		if (chan->cid.cid_name) {
			if (num)
				snprintf(chan->cdr->clid, sizeof(chan->cdr->clid), "\"%s\" <%s>", chan->cid.cid_name, num);
			else
				cw_copy_string(chan->cdr->clid, chan->cid.cid_name, sizeof(chan->cdr->clid));
		} else if (num)
			cw_copy_string(chan->cdr->clid, num, sizeof(chan->cdr->clid));

		cw_copy_string(chan->cdr->src, (num ? num : ""), sizeof(chan->cdr->src));

		chan->cdr->disposition = (chan->_state == CW_STATE_UP ?  CW_CDR_ANSWERED : CW_CDR_NOANSWER);
		chan->cdr->amaflags = (chan->amaflags ? chan->amaflags : cw_default_amaflags);
		cw_copy_string(chan->cdr->accountcode, chan->accountcode, sizeof(chan->cdr->accountcode));
		cw_copy_string(chan->cdr->dcontext, chan->context, sizeof(chan->cdr->dcontext));
		cw_copy_string(chan->cdr->dst, chan->exten, sizeof(chan->cdr->dst));
		cw_copy_string(chan->cdr->uniqueid, chan->uniqueid, sizeof(chan->cdr->uniqueid));

		return 0;
	}

	cw_log(CW_LOG_ERROR, "Out of memory\n");
	return -1;
}

void cw_cdr_end(struct cw_cdr *cdr)
{
	const char *chan;

	while (cdr) {
		chan = !cw_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (cw_test_flag(cdr, CW_CDR_FLAG_POSTED))
			cw_log(CW_LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (cw_tvzero(cdr->start))
			cw_log(CW_LOG_WARNING, "CDR on channel '%s' has not started\n", chan);
		if (cw_tvzero(cdr->end))
			cdr->end = cw_tvnow();
		cdr->duration = cdr->end.tv_sec - cdr->start.tv_sec + (cdr->end.tv_usec - cdr->start.tv_usec) / 1000000;
		if (!cw_tvzero(cdr->answer)) {
			cdr->billsec = cdr->end.tv_sec - cdr->answer.tv_sec + (cdr->end.tv_usec - cdr->answer.tv_usec) / 1000000;
                }
		else {
			cdr->billsec = 0;
			cw_log(CW_LOG_DEBUG, "CDR on channel '%s' has not been answered [billsec => 0]\n", chan);
                }
		cdr = cdr->next;
	}
}

const char *cw_cdr_disp2str(int disposition)
{
	switch (disposition) {
	case CW_CDR_NOANSWER:
		return "NO ANSWER";
	case CW_CDR_FAILED:
		return "FAILED";		
	case CW_CDR_BUSY:
		return "BUSY";		
	case CW_CDR_ANSWERED:
		return "ANSWERED";
	}
	return "UNKNOWN";
}

const char *cw_cdr_flags2str(int flag)
{
	switch(flag) {
	case CW_CDR_OMIT:
		return "OMIT";
	case CW_CDR_BILLING:
		return "BILLING";
	case CW_CDR_DOCUMENTATION:
		return "DOCUMENTATION";
	}
	return "Unknown";
}

int cw_cdr_setaccount(struct cw_channel *chan, const char *account)
{
	struct cw_cdr *cdr = chan->cdr;

	cw_copy_string(chan->accountcode, account, sizeof(chan->accountcode));
	while (cdr) {
		if (!cw_test_flag(cdr, CW_CDR_FLAG_LOCKED))
			cw_copy_string(cdr->accountcode, chan->accountcode, sizeof(cdr->accountcode));
		cdr = cdr->next;
	}
	return 0;
}

int cw_cdr_setamaflags(struct cw_channel *chan, const char *flag)
{
	struct cw_cdr *cdr = chan->cdr;
	int newflag;

	newflag = cw_cdr_amaflags2int(flag);
	if (newflag)
		cdr->amaflags = newflag;

	return 0;
}

int cw_cdr_setuserfield(struct cw_channel *chan, const char *userfield)
{
	struct cw_cdr *cdr = chan->cdr;

	while (cdr) {
		if (!cw_test_flag(cdr, CW_CDR_FLAG_LOCKED)) 
			cw_copy_string(cdr->userfield, userfield, sizeof(cdr->userfield));
		cdr = cdr->next;
	}

	return 0;
}

int cw_cdr_appenduserfield(struct cw_channel *chan, const char *userfield)
{
	struct cw_cdr *cdr = chan->cdr;

	while (cdr) {
		int len = strlen(cdr->userfield);

		if (!cw_test_flag(cdr, CW_CDR_FLAG_LOCKED))
			strncpy(cdr->userfield+len, userfield, sizeof(cdr->userfield) - len - 1);

		cdr = cdr->next;
	}

	return 0;
}

int cw_cdr_update(struct cw_channel *c)
{
	struct cw_cdr *cdr = c->cdr;
	char *num;
	char tmp[CW_MAX_EXTENSION] = "";

	while (cdr) {
		if (!cw_test_flag(cdr, CW_CDR_FLAG_LOCKED)) {
			num = c->cid.cid_ani ? c->cid.cid_ani : c->cid.cid_num;
			
			if (c->cid.cid_name && num)
				snprintf(tmp, sizeof(tmp), "\"%s\" <%s>", c->cid.cid_name, num);
			else if (c->cid.cid_name)
				cw_copy_string(tmp, c->cid.cid_name, sizeof(tmp));
			else if (num)
				cw_copy_string(tmp, num, sizeof(tmp));
			cw_copy_string(cdr->clid, tmp, sizeof(cdr->clid));
			cw_copy_string(cdr->src, num ? num : "", sizeof(cdr->src));

			/* Copy account code et-al */	
			cw_copy_string(cdr->accountcode, c->accountcode, sizeof(cdr->accountcode));
			/* Destination information */
			cw_copy_string(cdr->dst, (cw_strlen_zero(c->proc_exten)) ? c->exten : c->proc_exten, sizeof(cdr->dst));
			cw_copy_string(cdr->dcontext, (cw_strlen_zero(c->proc_context)) ? c->context : c->proc_context, sizeof(cdr->dcontext));
		}
		cdr = cdr->next;
	}

	return 0;
}

int cw_cdr_amaflags2int(const char *flag)
{
	if (!strcasecmp(flag, "default"))
		return 0;
	if (!strcasecmp(flag, "omit"))
		return CW_CDR_OMIT;
	if (!strcasecmp(flag, "billing"))
		return CW_CDR_BILLING;
	if (!strcasecmp(flag, "documentation"))
		return CW_CDR_DOCUMENTATION;
	return -1;
}


static int post_cdrbe(struct cw_object *obj, void *data)
{
	struct cw_cdrbe *cdrbe = container_of(obj, struct cw_cdrbe, obj);
	struct cw_cdr *batch = data;

	cdrbe->handler(batch);
	return 0;
}

static void post_cdr(struct cw_cdr *submission)
{
	struct cw_cdr *batch, *cdrset, *cdr;
	const char *chan;

	batch = submission;
	while ((cdrset = batch)) {
		batch = batch->batch_next;

		while ((cdr = cdrset)) {
			cdrset = cdrset->next;

			chan = !cw_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (cw_test_flag(cdr, CW_CDR_FLAG_POSTED))
				cw_log(CW_LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
			if (cw_tvzero(cdr->end))
				cw_log(CW_LOG_WARNING, "CDR on channel '%s' lacks end\n", chan);
			if (cw_tvzero(cdr->start))
				cw_log(CW_LOG_WARNING, "CDR on channel '%s' lacks start\n", chan);
			cw_set_flag(cdr, CW_CDR_FLAG_POSTED);
		}
	}

	cw_registry_iterate(&cdrbe_registry, post_cdrbe, submission);
}

void cw_cdr_reset(struct cw_cdr *cdr, unsigned int flags)
{
	struct cw_cdr *dupcdr;


	while (cdr) {
		/* Detach if post is requested */
		if ((flags & CW_CDR_FLAG_LOCKED) || !cw_test_flag(cdr, CW_CDR_FLAG_LOCKED)) {
			if ((flags & CW_CDR_FLAG_POSTED)) {
				cw_cdr_end(cdr);
				if ((dupcdr = cw_cdr_dup(cdr)))
					cw_cdr_detach(dupcdr);
				cw_set_flag(cdr, CW_CDR_FLAG_POSTED);
			}

			/* clear variables */
			if (!(flags & CW_CDR_FLAG_KEEP_VARS))
				cw_registry_flush(&cdr->vars);

			/* Reset to initial state */
			cw_clear_flag(cdr, CW_FLAGS_ALL);	
			memset(&cdr->start, 0, sizeof(cdr->start));
			memset(&cdr->end, 0, sizeof(cdr->end));
			memset(&cdr->answer, 0, sizeof(cdr->answer));
			cdr->billsec = 0;
			cdr->duration = 0;
			cw_cdr_start(cdr);
			cdr->disposition = CW_CDR_NOANSWER;
		}
			
		cdr = cdr->next;
	}
}

struct cw_cdr *cw_cdr_append(struct cw_cdr *cdr, struct cw_cdr *newcdr) 
{
	struct cw_cdr *ret;

	if (cdr) {
		ret = cdr;

		while (cdr->next)
			cdr = cdr->next;
		cdr->next = newcdr;
	} else {
		ret = newcdr;
	}

	return ret;
}


static void *cw_cdr_submit(void *data)
{
	struct cw_cdr *oldbatchitems;

	for (;;) {
		pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &cdr_batch_lock);
		pthread_mutex_lock(&cdr_batch_lock);

		if (!curbatch.head) {
			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
			pthread_cond_wait(&cdr_batch_cond, &cdr_batch_lock);
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		}

		oldbatchitems = curbatch.head;
		curbatch.size = 0;
		curbatch.head = NULL;
		curbatch.tail = &curbatch.head;

		pthread_cleanup_pop(1);

		if (oldbatchitems)
			post_cdr(oldbatchitems);
	}

	return NULL;
}


void cw_cdr_detach(struct cw_cdr *cdr)
{
	if (enabled) {
		pthread_mutex_lock(&cdr_batch_lock);

		*curbatch.tail = cdr;
		curbatch.tail = &cdr->batch_next;
		curbatch.size++;
		pthread_cond_signal(&cdr_batch_cond);

		pthread_mutex_unlock(&cdr_batch_lock);

		return;
	}

	cw_set_flag(cdr, CW_CDR_FLAG_POSTED);
	cw_cdr_free(cdr);
	return;
}


static int cdrbe_print(struct cw_object *obj, void *data)
{
	struct cw_cdrbe *cdrbe = container_of(obj, struct cw_cdrbe, obj);
	struct cw_dynstr **ds_p = data;

	cw_dynstr_printf(ds_p, "CDR registered backend: %s\n", cdrbe->name);
	return 0;
}


static int handle_cli_status(struct cw_dynstr **ds_p, int argc, char *argv[])
{
	if (argc > 2)
		return RESULT_SHOWUSAGE;

	cw_dynstr_printf(ds_p, "CDR logging: %s\n", enabled ? "enabled" : "disabled");
	if (enabled)
		cw_registry_iterate_ordered(&cdrbe_registry, cdrbe_print, ds_p);

	return 0;
}


static struct cw_clicmd cli_status = {
	.cmda = { "cdr", "status", NULL },
	.handler = handle_cli_status,
	.summary = "Display the CDR status",
	.usage =
	"Usage: cdr status\n"
	"	Displays the Call Detail Record engine system status.\n"
};


static void cw_cdr_engine_term(void)
{
	if (!pthread_equal(cdr_thread, CW_PTHREADT_NULL)) {
		pthread_cancel(cdr_thread);
		pthread_join(cdr_thread, NULL);
	}
}

static struct cw_atexit cdr_atexit = {
	.name = "CDR Engine Terminate",
	.function = cw_cdr_engine_term,
};


static int do_reload(void)
{
	struct cw_config *config = NULL;
	const char *value = NULL;
	int new_enabled, new_cw_end_cdr_before_h_exten;

	new_enabled = 1;
	new_cw_end_cdr_before_h_exten = 0;

	if ((config = cw_config_load("cdr.conf"))) {
		if ((value = cw_variable_retrieve(config, "general", "enable")))
			new_enabled = cw_true(value);
		if ((value = cw_variable_retrieve(config, "general", "endbeforehexten")))
			new_cw_end_cdr_before_h_exten = cw_true(value);

		/* DEPRECATED */
		if (cw_variable_retrieve(config, "general", "batch"))
			cw_log(CW_LOG_NOTICE, "batch option in cdr.conf is deprecated and should be removed\n");
		if (cw_variable_retrieve(config, "general", "safeshutdown"))
			cw_log(CW_LOG_NOTICE, "safeshutdown option in cdr.conf is deprecated and should be removed\n");
		if (cw_variable_retrieve(config, "general", "scheduleronly"))
			cw_log(CW_LOG_NOTICE, "scheduleronly option in cdr.conf is deprecated and should be removed\n");
		if (cw_variable_retrieve(config, "general", "size"))
			cw_log(CW_LOG_NOTICE, "size option in cdr.conf is deprecated and should be removed\n");
		if (cw_variable_retrieve(config, "general", "time"))
			cw_log(CW_LOG_NOTICE, "time option in cdr.conf is deprecated and should be removed\n");
	}

	enabled = new_enabled;
	cw_end_cdr_before_h_exten = new_cw_end_cdr_before_h_exten;

	if (enabled)
		cw_log(CW_LOG_NOTICE, "CDR logging enabled.\n");
	else
		cw_log(CW_LOG_NOTICE, "CDR logging disabled, data will be discarded.\n");

	cw_config_destroy(config);

	return 0;
}


int cw_cdr_engine_init(void)
{
	int res = 0;

	curbatch.tail = &curbatch.head;

	cw_atexit_register(&cdr_atexit);

	if (!(res = cw_pthread_create(&cdr_thread, &global_attr_default, cw_cdr_submit, NULL))) {
		cw_cli_register(&cli_status);
		res = do_reload();
	} else
		cw_log(CW_LOG_ERROR, "Failed to create CDR posting thread: %s\n", strerror(res));

	return res;
}


void cw_cdr_engine_reload(void)
{
	do_reload();
}

