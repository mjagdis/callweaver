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


static const char *cdrbe_object_name(struct cw_object *obj)
{
	struct cw_cdrbe *it = container_of(obj, struct cw_cdrbe, obj);
	return it->name;
}

static int cw_cdrbe_qsort_compare_by_name(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct cw_cdrbe *cdrbe_a = container_of(*objp_a, struct cw_cdrbe, obj);
	const struct cw_cdrbe *cdrbe_b = container_of(*objp_b, struct cw_cdrbe, obj);

	return strcmp(cdrbe_a->name, cdrbe_b->name);
}


const struct cw_object_isa cw_object_isa_cdrbe = {
	.name = cdrbe_object_name,
};


struct cw_registry cdrbe_registry = {
	.name = "CDR back-end",
	.qsort_compare = cw_cdrbe_qsort_compare_by_name,
};


int cw_default_amaflags = CW_CDR_DOCUMENTATION;
int cw_end_cdr_before_h_exten;
char cw_default_accountcode[CW_MAX_ACCOUNT_CODE] = "";


struct cw_cdr_batch_item {
	struct cw_cdr *cdr;
	struct cw_cdr_batch_item *next;
};

static struct cw_cdr_batch {
	int size;
	struct cw_cdr_batch_item *head;
	struct cw_cdr_batch_item *tail;
} *batch = NULL;

static struct sched_context *sched;
static int cdr_sched = -1;
static pthread_t cdr_thread = CW_PTHREADT_NULL;

#define BATCH_SIZE_DEFAULT 100
#define BATCH_TIME_DEFAULT 300
#define BATCH_SCHEDULER_ONLY_DEFAULT 0
#define BATCH_SAFE_SHUTDOWN_DEFAULT 1

static int enabled;
static int batchmode;
static int batchsize;
static int batchtime;
static int batchscheduleronly;
static int batchsafeshutdown;

CW_MUTEX_DEFINE_STATIC(cdr_batch_lock);

/* these are used to wake up the CDR thread when there's work to do */
CW_MUTEX_DEFINE_STATIC(cdr_pending_lock);


struct cw_cdr *cw_cdr_dup(struct cw_cdr *cdr) 
{
	struct cw_cdr *newcdr;

	if ((newcdr = malloc(sizeof(*newcdr)))) {
		memcpy(newcdr, cdr, sizeof(*newcdr));
		cw_var_registry_init(&newcdr->vars, 256);
		cw_var_copy(&cdr->vars, &newcdr->vars);
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


void cw_cdr_free(struct cw_cdr *cdr)
{
	char *chan;
	struct cw_cdr *next; 

	while (cdr) {
		next = cdr->next;
		chan = !cw_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (!cw_test_flag(cdr, CW_CDR_FLAG_POSTED) && !cw_test_flag(cdr, CW_CDR_FLAG_POST_DISABLED))
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

struct cw_cdr *cw_cdr_alloc(void)
{
	struct cw_cdr *cdr;

	if ((cdr = calloc(1, sizeof(*cdr)))) {
		cw_var_registry_init(&cdr->vars, 256);
	} else
		cw_log(CW_LOG_ERROR, "Out of memory\n");

	return cdr;
}

void cw_cdr_start(struct cw_cdr *cdr)
{
	char *chan; 

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
	char *chan; 

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
	char *chan; 

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
	char *chan; 

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
	char *chan; 

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
	char *chan; 

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


int cw_cdr_init(struct cw_cdr *cdr, struct cw_channel *c)
{
	char *chan;
	char *num;
	char tmp[CW_MAX_EXTENSION] = "";

	while (cdr) {
		if (!cw_test_flag(cdr, CW_CDR_FLAG_LOCKED)) {
			chan = !cw_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (!cw_strlen_zero(cdr->channel)) 
				cw_log(CW_LOG_WARNING, "CDR already initialized on '%s'\n", chan); 
			cw_copy_string(cdr->channel, c->name, sizeof(cdr->channel));
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

			cdr->disposition = (c->_state == CW_STATE_UP) ?  CW_CDR_ANSWERED : CW_CDR_NOANSWER;
			cdr->amaflags = c->amaflags ? c->amaflags :  cw_default_amaflags;
			cw_copy_string(cdr->accountcode, c->accountcode, sizeof(cdr->accountcode));
			/* Destination information */
			cw_copy_string(cdr->dst, c->exten, sizeof(cdr->dst));
			cw_copy_string(cdr->dcontext, c->context, sizeof(cdr->dcontext));
			/* Unique call identifier */
			cw_copy_string(cdr->uniqueid, c->uniqueid, sizeof(cdr->uniqueid));
		}
		cdr = cdr->next;
	}
	return 0;
}

void cw_cdr_end(struct cw_cdr *cdr)
{
	char *chan;

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

char *cw_cdr_disp2str(int disposition)
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

char *cw_cdr_flags2str(int flag)
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
	struct cw_cdr *cdr = data;

	cdrbe->handler(cdr);
	return 0;
}

static void post_cdr(struct cw_cdr *cdr)
{
	char *chan;

	while (cdr) {
		chan = !cw_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (cw_test_flag(cdr, CW_CDR_FLAG_POSTED))
			cw_log(CW_LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (cw_tvzero(cdr->end))
			cw_log(CW_LOG_WARNING, "CDR on channel '%s' lacks end\n", chan);
		if (cw_tvzero(cdr->start))
			cw_log(CW_LOG_WARNING, "CDR on channel '%s' lacks start\n", chan);
		cw_set_flag(cdr, CW_CDR_FLAG_POSTED);

		cw_registry_iterate(&cdrbe_registry, post_cdrbe, cdr);

		cdr = cdr->next;
	}
}

void cw_cdr_reset(struct cw_cdr *cdr, int flags)
{
	struct cw_flags tmp = {flags};
	struct cw_cdr *dup;


	while (cdr) {
		/* Detach if post is requested */
		if (cw_test_flag(&tmp, CW_CDR_FLAG_LOCKED) || !cw_test_flag(cdr, CW_CDR_FLAG_LOCKED)) {
			if (cw_test_flag(&tmp, CW_CDR_FLAG_POSTED)) {
				cw_cdr_end(cdr);
				if ((dup = cw_cdr_dup(cdr))) {
					cw_cdr_detach(dup);
				}
				cw_set_flag(cdr, CW_CDR_FLAG_POSTED);
			}

			/* clear variables */
			if (!cw_test_flag(&tmp, CW_CDR_FLAG_KEEP_VARS)) {
				cw_registry_flush(&cdr->vars);
			}

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

/* Don't call without cdr_batch_lock */
static void reset_batch(void)
{
	batch->size = 0;
	batch->head = NULL;
	batch->tail = NULL;
}

/* Don't call without cdr_batch_lock */
static int init_batch(void)
{
	/* This is the single meta-batch used to keep track of all CDRs during the entire life of the program */
	batch = malloc(sizeof(*batch));
	if (!batch) {
		cw_log(CW_LOG_WARNING, "CDR: out of memory while trying to handle batched records, data will most likely be lost\n");
		return -1;
	}

	reset_batch();

	return 0;
}

static void *do_batch_backend_process(void *data)
{
	struct cw_cdr_batch_item *processeditem;
	struct cw_cdr_batch_item *batchitem = data;

	/* Push each CDR into storage mechanism(s) and free all the memory */
	while (batchitem) {
		post_cdr(batchitem->cdr);
		cw_cdr_free(batchitem->cdr);
		processeditem = batchitem;
		batchitem = batchitem->next;
		free(processeditem);
	}

	return NULL;
}

static void cw_cdr_submit_batch(int shutdown)
{
	struct cw_cdr_batch_item *oldbatchitems = NULL;
	pthread_t batch_post_thread = CW_PTHREADT_NULL;

	/* if there's no batch, or no CDRs in the batch, then there's nothing to do */
	if (!batch || !batch->head)
		return;

	/* move the old CDRs aside, and prepare a new CDR batch */
	cw_mutex_lock(&cdr_batch_lock);
	oldbatchitems = batch->head;
	reset_batch();
	cw_mutex_unlock(&cdr_batch_lock);

	/* if configured, spawn a new thread to post these CDRs,
	   also try to save as much as possible if we are shutting down safely */
	if (batchscheduleronly || shutdown) {
		if (option_debug)
			cw_log(CW_LOG_DEBUG, "CDR single-threaded batch processing begins now\n");
		do_batch_backend_process(oldbatchitems);
	} else {
		if (cw_pthread_create(&batch_post_thread, &global_attr_detached, do_batch_backend_process, oldbatchitems)) {
			cw_log(CW_LOG_WARNING, "CDR processing thread could not detach, now trying in this thread\n");
			do_batch_backend_process(oldbatchitems);
		} else {
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "CDR multi-threaded batch processing begins now\n");
		}
	}
}

static int submit_scheduled_batch(void *data)
{
	cw_mutex_lock(&cdr_pending_lock);
	cw_cdr_submit_batch(0);
	cw_mutex_unlock(&cdr_pending_lock);

	/* manually reschedule from this point in time */
	cdr_sched = cw_sched_add(sched, batchtime * 1000, submit_scheduled_batch, NULL);
	/* returning zero so the scheduler does not automatically reschedule */
	return 0;
}

static void submit_unscheduled_batch(void)
{
	/* this is okay since we are not being called from within the scheduler */
	if (cdr_sched > -1)
		cw_sched_del(sched, cdr_sched);
	/* schedule the submission to occur ASAP (1 ms) */
	cdr_sched = cw_sched_add(sched, 1, submit_scheduled_batch, NULL);
}

void cw_cdr_detach(struct cw_cdr *cdr)
{
	struct cw_cdr_batch_item *newtail;
	int curr;

	/* maybe they disabled CDR stuff completely, so just drop it */
	if (!enabled) {
		if (option_debug)
			cw_log(CW_LOG_DEBUG, "Dropping CDR !\n");
		cw_set_flag(cdr, CW_CDR_FLAG_POST_DISABLED);
		cw_cdr_free(cdr);
		return;
	}

	/* post stuff immediately if we are not in batch mode, this is legacy behaviour */
	if (!batchmode) {
		post_cdr(cdr);
		cw_cdr_free(cdr);
		return;
	}

	/* otherwise, each CDR gets put into a batch list (at the end) */
	if (option_debug)
		cw_log(CW_LOG_DEBUG, "CDR detaching from this thread\n");

	/* we'll need a new tail for every CDR */
	newtail = malloc(sizeof(*newtail));
	if (!newtail) {
		cw_log(CW_LOG_WARNING, "CDR: out of memory while trying to detach, will try in this thread instead\n");
		post_cdr(cdr);
		cw_cdr_free(cdr);
		return;
	}
	memset(newtail, 0, sizeof(*newtail));

	/* don't traverse a whole list (just keep track of the tail) */
	cw_mutex_lock(&cdr_batch_lock);
	if (!batch)
		init_batch();
	if (!batch->head) {
		/* new batch is empty, so point the head at the new tail */
		batch->head = newtail;
	} else {
		/* already got a batch with something in it, so just append a new tail */
		batch->tail->next = newtail;
	}
	newtail->cdr = cdr;
	batch->tail = newtail;
	curr = batch->size++;
	cw_mutex_unlock(&cdr_batch_lock);

	/* if we have enough stuff to post, then do it */
	if (curr >= (batchsize - 1))
		submit_unscheduled_batch();
}


static int cdrbe_print(struct cw_object *obj, void *data)
{
	struct cw_cdrbe *cdrbe = container_of(obj, struct cw_cdrbe, obj);
	int *fd = data;

	cw_cli(*fd, "CDR registered backend: %s\n", cdrbe->name);
	return 0;
}


static int handle_cli_status(int fd, int argc, char *argv[])
{
	int cnt = 0;
	long nextbatchtime = 0;

	if (argc > 2)
		return RESULT_SHOWUSAGE;

	cw_cli(fd, "CDR logging: %s\n", enabled ? "enabled" : "disabled");
	cw_cli(fd, "CDR mode: %s\n", batchmode ? "batch" : "simple");
	if (enabled) {
		if (batchmode) {
			if (batch)
				cnt = batch->size;
			if (cdr_sched > -1)
				nextbatchtime = cw_sched_when(sched, cdr_sched);
			cw_cli(fd, "CDR safe shut down: %s\n", batchsafeshutdown ? "enabled" : "disabled");
			cw_cli(fd, "CDR batch threading model: %s\n", batchscheduleronly ? "scheduler only" : "scheduler plus separate threads");
			cw_cli(fd, "CDR current batch size: %d record%s\n", cnt, (cnt != 1) ? "s" : "");
			cw_cli(fd, "CDR maximum batch size: %d record%s\n", batchsize, (batchsize != 1) ? "s" : "");
			cw_cli(fd, "CDR maximum batch time: %d second%s\n", batchtime, (batchtime != 1) ? "s" : "");
			cw_cli(fd, "CDR next scheduled batch processing time: %ld second%s\n", nextbatchtime, (nextbatchtime != 1) ? "s" : "");
		}

		cw_registry_iterate_ordered(&cdrbe_registry, cdrbe_print, &fd);
	}

	return 0;
}

static int handle_cli_submit(int fd, int argc, char *argv[])
{
	if (argc > 2)
		return RESULT_SHOWUSAGE;

	submit_unscheduled_batch();
	cw_cli(fd, "Submitted CDRs to backend engines for processing.  This may take a while.\n");

	return 0;
}

static struct cw_clicmd cli_submit = {
	.cmda = { "cdr", "submit", NULL },
	.handler = handle_cli_submit,
	.summary = "Posts all pending batched CDR data",
	.usage =
	"Usage: cdr submit\n"
	"       Posts all pending batched CDR data to the configured CDR backend engine modules.\n"
};

static struct cw_clicmd cli_status = {
	.cmda = { "cdr", "status", NULL },
	.handler = handle_cli_status,
	.summary = "Display the CDR status",
	.usage =
	"Usage: cdr status\n"
	"	Displays the Call Detail Record engine system status.\n"
};

static struct cw_atexit cdr_atexit = {
	.name = "CDR Engine Terminate",
	.function = cw_cdr_engine_term,
};

static int do_reload(void)
{
	struct cw_config *config = NULL;
	const char *enabled_value = NULL;
	const char *batched_value = NULL;
	const char *end_before_h_value = NULL;
	const char *scheduleronly_value = NULL;
	const char *batchsafeshutdown_value = NULL;
	const char *size_value = NULL;
	const char *time_value = NULL;
	int cfg_size;
	int cfg_time;
	int was_enabled;
	int was_batchmode;
	int res=0;

	cw_mutex_lock(&cdr_batch_lock);

	batchsize = BATCH_SIZE_DEFAULT;
	batchtime = BATCH_TIME_DEFAULT;
	batchscheduleronly = BATCH_SCHEDULER_ONLY_DEFAULT;
	batchsafeshutdown = BATCH_SAFE_SHUTDOWN_DEFAULT;
	was_enabled = enabled;
	was_batchmode = batchmode;
	enabled = 1;
	batchmode = 0;
	cw_end_cdr_before_h_exten = 0;

	/* don't run the next scheduled CDR posting while reloading */
	if (cdr_sched > -1)
		cw_sched_del(sched, cdr_sched);

	if ((config = cw_config_load("cdr.conf"))) {
		if ((enabled_value = cw_variable_retrieve(config, "general", "enable"))) {
			enabled = cw_true(enabled_value);
		}
		if ((end_before_h_value = cw_variable_retrieve(config, "general", "endbeforehexten"))) {
			cw_end_cdr_before_h_exten = cw_true(end_before_h_value);
		}
		if ((batched_value = cw_variable_retrieve(config, "general", "batch"))) {
			batchmode = cw_true(batched_value);
		}
		if ((scheduleronly_value = cw_variable_retrieve(config, "general", "scheduleronly"))) {
			batchscheduleronly = cw_true(scheduleronly_value);
		}
		if ((batchsafeshutdown_value = cw_variable_retrieve(config, "general", "safeshutdown"))) {
			batchsafeshutdown = cw_true(batchsafeshutdown_value);
		}
		if ((size_value = cw_variable_retrieve(config, "general", "size"))) {
			if (sscanf(size_value, "%d", &cfg_size) < 1)
				cw_log(CW_LOG_WARNING, "Unable to convert '%s' to a numeric value.\n", size_value);
			else if (cfg_size < 0)
				cw_log(CW_LOG_WARNING, "Invalid maximum batch size '%d' specified, using default\n", cfg_size);
			else
				batchsize = cfg_size;
		}
		if ((time_value = cw_variable_retrieve(config, "general", "time"))) {
			if (sscanf(time_value, "%d", &cfg_time) < 1)
				cw_log(CW_LOG_WARNING, "Unable to convert '%s' to a numeric value.\n", time_value);
			else if (cfg_time < 0)
				cw_log(CW_LOG_WARNING, "Invalid maximum batch time '%d' specified, using default\n", cfg_time);
			else
				batchtime = cfg_time;
		}
	}

	if (enabled && !batchmode) {
		cw_log(CW_LOG_NOTICE, "CDR simple logging enabled.\n");
	} else if (enabled && batchmode) {
		cdr_sched = cw_sched_add(sched, batchtime * 1000, submit_scheduled_batch, NULL);
		cw_log(CW_LOG_NOTICE, "CDR batch mode logging enabled, first of either size %d or time %d seconds.\n", batchsize, batchtime);
	} else {
		cw_log(CW_LOG_NOTICE, "CDR logging disabled, data will be lost.\n");
	}

	/* if this reload enabled the CDR batch mode, create the background thread
	   if it does not exist */
	if (enabled && batchmode && (!was_enabled || !was_batchmode) && (pthread_equal(cdr_thread, CW_PTHREADT_NULL))) {
		cw_cli_register(&cli_submit);
		cw_atexit_register(&cdr_atexit);
		res = 0;
	/* if this reload disabled the CDR and/or batch mode and there is a background thread,
	   kill it */
	}else if (((!enabled && was_enabled) || (!batchmode && was_batchmode)) && !pthread_equal(cdr_thread, CW_PTHREADT_NULL)) {
		cdr_thread = CW_PTHREADT_NULL;
		cw_cli_unregister(&cli_submit);
		cw_atexit_unregister(&cdr_atexit);
		res = 0;
		/* if leaving batch mode, then post the CDRs in the batch,
		   and don't reschedule, since we are stopping CDR logging */
		if (!batchmode && was_batchmode) {
			cw_cdr_engine_term();
		}
	} else {
		res = 0;
	}

	cw_mutex_unlock(&cdr_batch_lock);
	cw_config_destroy(config);

	return res;
}

int cw_cdr_engine_init(void)
{
	int res;

	sched = sched_context_create(1);
	if (!sched) {
		cw_log(CW_LOG_ERROR, "Unable to create schedule context.\n");
		return -1;
	}

	cw_cli_register(&cli_status);

	res = do_reload();
	if (res) {
		cw_mutex_lock(&cdr_batch_lock);
		res = init_batch();
		cw_mutex_unlock(&cdr_batch_lock);
	}

	return res;
}

/* This actually gets called a couple of times at shutdown.  Once, before we start
   hanging up channels, and then again, after the channel hangup timeout expires */
void cw_cdr_engine_term(void)
{
	cw_cdr_submit_batch(batchsafeshutdown);
}

void cw_cdr_engine_reload(void)
{
	do_reload();
}

