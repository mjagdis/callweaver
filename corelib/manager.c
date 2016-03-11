/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009 - 2010, Eris Associates Limited, UK
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

/*
 *
 * The CallWeaver Management Interface
 *
 * Channel Management and more
 * 
 */
#include <sys/types.h>
#include <sys/time.h>
#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/connection.h"
#include "callweaver/file.h"
#include "callweaver/manager.h"
#include "callweaver/config.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/lock.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/cli.h"
#include "callweaver/app.h"
#include "callweaver/pbx.h"
#include "callweaver/acl.h"
#include "callweaver/dynstr.h"
#include "callweaver/utils.h"


#define MKSTR(X)	# X


#define DEFAULT_MANAGER_PORT	5038
#define DEFAULT_QUEUE_SIZE	1024


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
	char actionid[256];
	int priority;
	struct cw_registry vars;
};


static int displayconnects;
static int queuesize;


struct manager_listener_pvt {
	struct cw_object obj;
	int (*handler)(struct mansession *, const struct cw_manager_message *);
	int readperm, writeperm, send_events;
	char banner[0];
};


static struct {
	const char *label;
	int len;
} perms[] = {
#define STR_LEN(s)	{ s, sizeof(s) - 1 }
	[CW_LOG_ERROR]    = STR_LEN("error"),
	[CW_LOG_WARNING]  = STR_LEN("warning"),
	[CW_LOG_NOTICE]   = STR_LEN("notice"),
	[CW_LOG_VERBOSE]  = STR_LEN("verbose"),
	[CW_LOG_EVENT]    = STR_LEN("event"),
	[CW_LOG_DTMF]     = STR_LEN("dtmf"),
	[CW_LOG_DEBUG]    = STR_LEN("debug"),
	[CW_LOG_PROGRESS] = STR_LEN("progress"),

	[CW_EVENT_NUM_SYSTEM]  = STR_LEN("system"),
	[CW_EVENT_NUM_CALL]    = STR_LEN("call"),
	[CW_EVENT_NUM_COMMAND] = STR_LEN("command"),
	[CW_EVENT_NUM_AGENT]   = STR_LEN("agent"),
	[CW_EVENT_NUM_USER]    = STR_LEN("user"),
#undef STR_LEN
};


#define MANAGER_AMI_HELLO	"CallWeaver Call Manager/1.0\r\n"


static int manager_listener_read(struct cw_connection *conn);

static const struct cw_connection_tech tech_ami = {
	.name = "AMI",
	.read = manager_listener_read,
};


static int snprintf_authority(char *buf, size_t buflen, int authority)
{
	int i, used, sep = 0;

	used = 0;
	for (i = 0; i < arraysize(perms); i++) {
		if ((authority & (1 << i)) && perms[i].label) {
			if (used < buflen)
				snprintf(buf + used, buflen - used, (sep ? ", %s" : "%s"), perms[i].label);
			used += perms[i].len + sep;
			sep = 2;
		}
	}

	if (!sep) {
		snprintf(buf, buflen, "<none>");
		used = sizeof("<none>") - 1;
	}

	buf[used < buflen ? used : buflen - 1] = '\0';

	return used;
}


static int printf_authority(struct cw_dynstr *ds_p, int authority)
{
	int i, used, sep = 0;

	used = 0;
	for (i = 0; i < arraysize(perms); i++) {
		if ((authority & (1 << i)) && perms[i].label) {
			cw_dynstr_printf(ds_p, (sep ? ", %s" : "%s"), perms[i].label);
			used += perms[i].len + sep;
			sep = 2;
		}
	}

	if (!sep) {
		cw_dynstr_printf(ds_p, "<none>");
		used = sizeof("<none>") - 1;
	}

	return used;
}


int manager_str_to_eventmask(char *instr)
{
	int ret = 0;

	/* Logically you might expect an empty mask to mean nothing
	 * however it has historically meant everything. It's too
	 * late to risk changing it now.
	 */
	if (!instr || !*instr || cw_true(instr) || !strcasecmp(instr, "all") || (instr[0] == '-' && instr[1] == '1'))
		ret = -1 & (~CW_EVENT_FLAG_LOG_ALL);
	else if (cw_false(instr))
		ret = 0;
	else if (isdigit(*instr))
		ret = atoi(instr);
	else {
		char *p = instr;

		while (*p && isspace(*p)) p++;
		while (*p) {
			char *q;
			int n, i;

			for (q = p; *q && *q != ','; q++);
			n = q - p;

			if (n == sizeof("log") - 1 && !memcmp(p, "log", sizeof("log") - 1)) {
				ret |= CW_EVENT_FLAG_LOG_ALL;
			} else {
				for (i = 0; i < arraysize(perms) && (!perms[i].label || strncmp(p, perms[i].label, n)); i++);
				if (i < arraysize(perms))
					ret |= (1 << i);
				else
					cw_log(CW_LOG_ERROR, "unknown manager permission %.*s in %s\n", n, p, instr);
			}

			p = q;
			while (*p && (*p == ',' || isspace(*p))) p++;
		}
	}

	return ret;
}


int cw_manager_send(struct mansession *sess, const struct message *req, struct cw_manager_message **resp_p)
{
	int q_w_next;
	int ret = -1;

	if (*resp_p) {
		if (!(*resp_p)->ds.error && (!req || !req->actionid || !cw_manager_msg(resp_p, 1, cw_msg_tuple("ActionID", "%s", req->actionid)))) {
			pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &sess->lock);
			pthread_mutex_lock(&sess->lock);

			q_w_next = (sess->q_w + 1) % sess->q_size;

			if (q_w_next != sess->q_r) {
				if (++sess->q_count > sess->q_max)
					sess->q_max = sess->q_count;

				sess->q[sess->q_w] = *resp_p;

				if (sess->q_w == sess->q_r)
					pthread_cond_signal(&sess->activity);

				sess->q_w = q_w_next;
				ret = 0;
			} else {
				sess->q_overflow++;
				cw_object_put(*resp_p);
				ret = 1;
			}

			pthread_cleanup_pop(1);
		} else
			cw_object_put(*resp_p);

		*resp_p = NULL;
	}

	return ret;
}


static int cw_mansession_qsort_compare_by_addr(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct mansession *item_a = container_of(*objp_a, struct mansession, obj);
	const struct mansession *item_b = container_of(*objp_b, struct mansession, obj);

	return cw_sockaddr_cmp(&item_a->addr,  &item_b->addr, -1, 1);
}

static int manager_session_object_match(struct cw_object *obj, const void *pattern)
{
	struct mansession *item = container_of(obj, struct mansession, obj);
	return !cw_sockaddr_cmp(&item->addr, pattern, -1, 1);
}

struct cw_registry manager_session_registry = {
	.name = "Manager Session",
	.qsort_compare = cw_mansession_qsort_compare_by_addr,
	.match = manager_session_object_match,
};


static int cw_manager_action_qsort_compare_by_name(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct manager_action *item_a = container_of(*objp_a, struct manager_action, obj);
	const struct manager_action *item_b = container_of(*objp_b, struct manager_action, obj);

	return strcasecmp(item_a->action, item_b->action);
}

static int manager_action_object_match(struct cw_object *obj, const void *pattern)
{
	struct manager_action *item = container_of(obj, struct manager_action, obj);
	return (!strcasecmp(item->action, pattern));
}

struct cw_registry manager_action_registry = {
	.name = "Manager Action",
	.qsort_compare = cw_manager_action_qsort_compare_by_name,
	.match = manager_action_object_match,
};


static const char showmancmd_help[] =
"Usage: show manager command <actionname>\n"
"	Shows the detailed description for a specific CallWeaver manager interface command.\n";


struct complete_show_manact_args {
	struct cw_dynstr *ds_p;
	char *word;
	int word_len;
};

static int complete_show_manact_one(struct cw_object *obj, void *data)
{
	struct manager_action *it = container_of(obj, struct manager_action, obj);
	struct complete_show_manact_args *args = data;

	if (!strncasecmp(args->word, it->action, args->word_len))
		cw_dynstr_printf(args->ds_p, "%s\n", it->action);

	return 0;
}

static void complete_show_manact(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	struct complete_show_manact_args args = {
		.ds_p = ds_p,
		.word = argv[lastarg],
		.word_len = lastarg_len,
	};

	cw_registry_iterate(&manager_action_registry, complete_show_manact_one, &args);
}


static int handle_show_manact(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct cw_object *it;
	struct manager_action *act;

	if (argc < 4)
		return RESULT_SHOWUSAGE;

	if (!(it = cw_registry_find(&manager_action_registry, 0, 0, argv[3]))) {
		cw_dynstr_printf(ds_p, "No manager action by that name registered.\n");
		return RESULT_FAILURE;
	}
	act = container_of(it, struct manager_action, obj);

	/* FIXME: Tidy up this output and make it more like function output */
	cw_dynstr_printf(ds_p, "Action: %s\nSynopsis: %s\nPrivilege: ", act->action, act->synopsis);
	printf_authority(ds_p, act->authority);
	cw_dynstr_printf(ds_p, "\n%s\n", (act->description ? act->description : ""));

	cw_object_put(act);
	return RESULT_SUCCESS;
}


static const char showmancmds_help[] =
"Usage: show manager commands\n"
"	Prints a listing of all the available CallWeaver manager interface commands.\n";


static void complete_show_manacts(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	if (lastarg == 3) {
		if (!strncasecmp(argv[3], "like", lastarg_len))
			cw_dynstr_printf(ds_p, "like\n");
		if (!strncasecmp(argv[3], "describing", lastarg_len))
			cw_dynstr_printf(ds_p, "describing\n");
	}
}


struct manacts_print_args {
	struct cw_dynstr *ds_p;
	int like, describing, matches;
	int argc;
	char **argv;
};

#define MANACTS_FORMAT_A	"  %-15.15s  "
#define MANACTS_FORMAT_B	"%-15.15s"
#define MANACTS_FORMAT_C	"  %s\n"

static int manacts_print(struct cw_object *obj, void *data)
{
	struct manager_action *it = container_of(obj, struct manager_action, obj);
	struct manacts_print_args *args = data;
	int printapp = 1;
	int n;

	if (args->like) {
		if (!strcasestr(it->action, args->argv[4]))
			printapp = 0;
	} else if (args->describing) {
		/* Match all words on command line */
		int i;
		for (i = 4;  i < args->argc;  i++) {
			if ((!it->synopsis || !strcasestr(it->synopsis, args->argv[i]))
			&& (!it->description || !strcasestr(it->description, args->argv[i]))) {
				printapp = 0;
				break;
			}
		}
	}

	if (printapp) {
		args->matches++;
		cw_dynstr_printf(args->ds_p, MANACTS_FORMAT_A, it->action);
		n = printf_authority(args->ds_p, it->authority);
		if (n < 15)
			cw_dynstr_printf(args->ds_p, "%.*s", 15 - n, "               ");
		cw_dynstr_printf(args->ds_p, MANACTS_FORMAT_C, it->synopsis);
	}

	return 0;
}

static int handle_show_manacts(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct manacts_print_args args = {
		.ds_p = ds_p,
		.matches = 0,
		.argc = argc,
		.argv = argv,
	};

	if ((argc == 5) && (!strcmp(argv[3], "like")))
		args.like = 1;
	else if ((argc > 4) && (!strcmp(argv[3], "describing")))
		args.describing = 1;

	cw_dynstr_printf(ds_p, "    -= %s Manager Actions =-\n"
		MANACTS_FORMAT_A MANACTS_FORMAT_B MANACTS_FORMAT_C
		MANACTS_FORMAT_A MANACTS_FORMAT_B MANACTS_FORMAT_C,
		(args.like || args.describing ? "Matching" : "Registered"),
		"Action", "Privilege", "Synopsis",
		"------", "---------", "--------");

	cw_registry_iterate_ordered(&manager_action_registry, manacts_print, &args);

	cw_dynstr_printf(ds_p, "    -= %d Actions %s =-\n", args.matches, (args.like || args.describing ? "Matching" : "Registered"));
	return RESULT_SUCCESS;
}


static const char showlistener_help[] =
"Usage: show manager listen\n"
"	Prints a listing of the sockets the manager is listening on.\n";


struct listener_print_args {
	struct cw_dynstr *ds_p;
};

#define MANLISTEN_FORMAT_HEADER "%-10s %s\n"
#define MANLISTEN_FORMAT_DETAIL "%-10s %l@\n"

static int listener_print(struct cw_object *obj, void *data)
{
	struct cw_connection *conn = container_of(obj, struct cw_connection, obj);
	struct listener_print_args *args = data;

	if (conn->tech == &tech_ami && (conn->state == INIT || conn->state == LISTENING))
		cw_dynstr_printf(args->ds_p, MANLISTEN_FORMAT_DETAIL, cw_connection_state_name[conn->state], &conn->addr);

	return 0;
}

static int handle_show_listener(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct listener_print_args args = {
		.ds_p = ds_p,
	};

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_dynstr_tprintf(ds_p, 2,
		cw_fmtval(MANLISTEN_FORMAT_HEADER, "State", "Address"),
		cw_fmtval(MANLISTEN_FORMAT_HEADER, "-----", "-------")
	);

	cw_registry_iterate_ordered(&cw_connection_registry, listener_print, &args);

	return RESULT_SUCCESS;
}


static const char showmanconn_help[] =
"Usage: show manager connected\n"
"	Prints a listing of the users that are currently connected to the\n"
"CallWeaver manager interface.\n";


struct mansess_print_args {
	struct cw_dynstr *ds_p;
};

#define MANSESS_FORMAT_HEADER	"%-40s %-15s %-6s %-9s %-8s\n"
#define MANSESS_FORMAT_DETAIL	"%-40l@ %-15s %6u %9u %8u\n"

static int mansess_print(struct cw_object *obj, void *data)
{
	struct mansession *it = container_of(obj, struct mansession, obj);
	struct mansess_print_args *args = data;

	cw_dynstr_printf(args->ds_p, MANSESS_FORMAT_DETAIL, &it->addr, it->username, it->q_count, it->q_max, it->q_overflow);
	return 0;
}

static int handle_show_mansess(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct mansess_print_args args = {
		.ds_p = ds_p,
	};

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_dynstr_tprintf(ds_p, 2,
		cw_fmtval(MANSESS_FORMAT_HEADER, "Address", "Username", "Queued", "Max Queue", "Overflow"),
		cw_fmtval(MANSESS_FORMAT_HEADER, "--------", "-------", "------", "---------", "--------")
	);

	cw_registry_iterate_ordered(&manager_session_registry, mansess_print, &args);

	return RESULT_SUCCESS;
}


static struct cw_clicmd clicmds[] = {
	{
		.cmda = { "show", "manager", "command", NULL },
		.handler = handle_show_manact,
		.generator = complete_show_manact,
		.summary = "Show a manager interface command",
		.usage = showmancmd_help,
	},
	{
		.cmda = { "show", "manager", "commands", NULL }, /* FIXME: should be actions */
		.handler = handle_show_manacts,
		.generator = complete_show_manacts,
		.summary = "List manager interface commands",
		.usage = showmancmds_help,
	},
	{
		.cmda = { "show", "manager", "listen", NULL },
		.handler = handle_show_listener,
		.summary = "Show manager listen sockets",
		.usage = showlistener_help,
	},
	{
		.cmda = { "show", "manager", "connected", NULL },
		.handler = handle_show_mansess,
		.summary = "Show connected manager interface users",
		.usage = showmanconn_help,
	},
};


static void mansession_release(struct cw_object *obj)
{
	struct mansession *sess = container_of(obj, struct mansession, obj);

	if (sess->fd > -1)
		close(sess->fd);

	while (sess->q_r != sess->q_w) {
		cw_object_put(sess->q[sess->q_r]);
		sess->q_r = (sess->q_r + 1) % sess->q_size;
	}

	if (sess->pvt_obj)
		cw_object_put_obj(sess->pvt_obj);

	pthread_mutex_destroy(&sess->lock);
	pthread_cond_destroy(&sess->ack);
	pthread_cond_destroy(&sess->activity);
	free(sess->q);
	cw_object_destroy(sess);
	free(sess);
}


char *cw_manager_msg_header(const struct message *msg, const char *key)
{
	int x;

	for (x = 0;  x < msg->hdrcount;  x++) {
		if (!strcasecmp(key, msg->header[x].key))
			return msg->header[x].val;
	}
	return NULL;
}


struct cw_manager_message *cw_manager_response(const char *resp, const char *msgstr)
{
	struct cw_manager_message *msg = NULL;

	if (!cw_manager_msg(&msg, 1, cw_msg_tuple("Response", "%s", resp))) {
		if (msgstr)
			if (cw_manager_msg(&msg, 1, cw_msg_tuple("Message", "%s", msgstr)))
				goto error;
	}

	return msg;

error:
	cw_object_put(msg);
	return NULL;
}


static struct cw_manager_message *authenticate(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg = NULL;
	struct cw_config *cfg;
	char *cat;
	char *user = cw_manager_msg_header(req, "Username");
	char *pass = cw_manager_msg_header(req, "Secret");
	char *authtype = cw_manager_msg_header(req, "AuthType");
	char *key = cw_manager_msg_header(req, "Key");
	char *events = cw_manager_msg_header(req, "Events");
	int ret = -1;

	if (!user)
		return cw_manager_response("Error", "Required header \"Username\" missing");

	if ((cfg = cw_config_load("manager.conf"))) {
		for (cat = cw_category_browse(cfg, NULL); cat; cat = cw_category_browse(cfg, cat)) {
			if (strcasecmp(cat, "general")) {
				/* This is a user */
				if (!strcasecmp(cat, user)) {
					struct cw_variable *v;
					struct cw_acl *acl = NULL;
					char *password = NULL;

					for (v = cw_variable_browse(cfg, cat); v; v = v->next) {
						if (!strcasecmp(v->name, "secret")) {
							password = v->value;
						} else if (!strcasecmp(v->name, "permit") || !strcasecmp(v->name, "deny")) {
							int err;

							if ((err = cw_acl_add(&acl, v->name, v->value)))
								cw_log(CW_LOG_ERROR, "%s = %s: %s\n", v->name, v->value, gai_strerror(err));
						} else if (!strcasecmp(v->name, "writetimeout")) {
							cw_log(CW_LOG_WARNING, "writetimeout is deprecated - remove it from manager.conf\n");
						}
					}

					ret = 0;

					if (acl) {
						if (!cw_acl_check(acl, &sess->addr, 1)) {
							cw_log(CW_LOG_NOTICE, "%l@ failed to pass ACL as '%s'\n", &sess->addr, user);
							ret = -1;
						}

						cw_acl_free(acl);
					}

					if (!ret) {
						if (authtype && !strcasecmp(authtype, "MD5")) {
							if (!cw_strlen_zero(key) && !cw_strlen_zero(sess->challenge) && !cw_strlen_zero(password)) {
								char md5key[256] = "";
								cw_md5_hash_two(md5key, sess->challenge, password);
								if (strcmp(md5key, key))
									ret = -1;
							}
						} else if (!pass || !password || strcasecmp(password, pass))
							ret = -1;
					}

					if (!ret)
						break;
				}
			}
		}

		if (cat && !ret) {
			int readperm, writeperm, eventmask = 0;

			readperm = manager_str_to_eventmask(cw_variable_retrieve(cfg, cat, "read"));
			writeperm = manager_str_to_eventmask(cw_variable_retrieve(cfg, cat, "write"));
			if (events)
				eventmask = manager_str_to_eventmask(events);

			pthread_mutex_lock(&sess->lock);

			cw_copy_string(sess->username, cat, sizeof(sess->username));
			sess->readperm = readperm;
			sess->writeperm = writeperm;
			if (events)
				sess->send_events = eventmask;

			pthread_mutex_unlock(&sess->lock);

			sess->authenticated = 1;
			if (option_verbose > 3 && displayconnects)
				cw_verbose(VERBOSE_PREFIX_2 "Manager '%s' logged on from %l@\n", sess->username, &sess->addr);
			cw_log(CW_LOG_EVENT, "Manager '%s' logged on from %l@\n", sess->username, &sess->addr);
			msg = cw_manager_response("Success", "Authentication accepted");
		}

		cw_config_destroy(cfg);
	}

	if (!msg) {
		cw_log(CW_LOG_ERROR, "%l@ failed to authenticate as '%s'\n", &sess->addr, user);
		/* FIXME: this should be handled by a scheduled callback not a sleep */
		sleep(1);
		msg = cw_manager_response("Error", "Authentication failed");
	}

	return msg;
}


static const char mandescr_ping[] =
"Description: A 'Ping' action will ellicit a 'Pong' response.  Used to keep the\n"
"  manager connection open.\n"
"Variables: NONE\n";

static struct cw_manager_message *action_ping(struct mansession *sess, const struct message *req)
{
	CW_UNUSED(sess);
	CW_UNUSED(req);

	return cw_manager_response("Pong", NULL);
}


static const char mandescr_version[] =
"Description: Returns the version, hostname and pid of the running CallWeaver\n";

static struct cw_manager_message *action_version(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg;

	CW_UNUSED(sess);
	CW_UNUSED(req);

	if ((msg = cw_manager_response("Version", NULL))) {
		cw_manager_msg(&msg, 3,
			cw_msg_tuple("Version", "%s", cw_version_string),
			cw_msg_tuple("Hostname", "%s", hostname),
			cw_msg_tuple("Pid", "%u", (unsigned int)getpid())
		);
	}

	return msg;
}


static const char mandescr_listcommands[] =
"Description: Returns the action name and synopsis for every\n"
"  action that is available to the user\n"
"Variables: NONE\n";

struct listcommands_print_args {
	struct cw_manager_message *msg;
};

static int listcommands_print(struct cw_object *obj, void *data)
{
	char buf[1024];
	struct manager_action *it = container_of(obj, struct manager_action, obj);
	struct listcommands_print_args *args = data;

	snprintf_authority(buf, sizeof(buf), it->authority);
	cw_manager_msg(&args->msg, 1, cw_msg_vtuple(it->action, "%s (Priv: %s)", it->synopsis, buf));

	return (args->msg == NULL);
}

static struct cw_manager_message *action_listcommands(struct mansession *sess, const struct message *req)
{
	struct listcommands_print_args args;

	CW_UNUSED(sess);
	CW_UNUSED(req);

	if ((args.msg = cw_manager_response("Success", NULL)))
		cw_registry_iterate_ordered(&manager_action_registry, listcommands_print, &args);

	return args.msg;
}

static const char mandescr_events[] =
"Description: Enable/Disable sending of events to this manager\n"
"  client.\n"
"Variables:\n"
"	EventMask: 'on' if all events should be sent,\n"
"		'off' if no events should be sent,\n"
"		'system,call,log' to select which flags events should have to be sent.\n";

static struct cw_manager_message *action_events(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg;
	char *mask = cw_manager_msg_header(req, "EventMask");

	if (mask) {
		int eventmask = manager_str_to_eventmask(mask);

		pthread_mutex_lock(&sess->lock);

		sess->send_events = eventmask;

		pthread_mutex_unlock(&sess->lock);

		msg = cw_manager_response((eventmask ? "Events On" : "Events Off"), NULL);
	} else
		msg = cw_manager_response("Error", "Required header \"Mask\" missing");

	return msg;
}

static const char mandescr_logoff[] =
"Description: Logoff this manager session\n"
"Variables: NONE\n";

static const char mandescr_hangup[] =
"Description: Hangup a channel\n"
"Variables: \n"
"	Channel: The channel name to be hungup\n";

static struct cw_manager_message *action_hangup(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg = NULL;
	struct cw_channel *chan = NULL;
	char *name = cw_manager_msg_header(req, "Channel");

	CW_UNUSED(sess);

	if (!cw_strlen_zero(name)) {
		if ((chan = cw_get_channel_by_name_locked(name))) {
			cw_softhangup(chan, CW_SOFTHANGUP_EXPLICIT);
			cw_channel_unlock(chan);
			msg = cw_manager_response("Success", "Channel Hungup");
			cw_object_put(chan);
		} else
			msg = cw_manager_response("Error", "No such channel");
	} else
		msg = cw_manager_response("Error", "No channel specified");

	return msg;
}


static const char mandescr_setvar[] =
"Description: Set a local channel variable.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel to set variable for\n"
"	*Variable: Variable name\n"
"	*Value: Value\n";

static struct cw_manager_message *action_setvar(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg = NULL;
        struct cw_channel *chan;
        char *name = cw_manager_msg_header(req, "Channel");
        char *varname;

	CW_UNUSED(sess);

	if (!cw_strlen_zero(name)) {
		varname = cw_manager_msg_header(req, "Variable");
		if (!cw_strlen_zero(varname)) {
			if ((chan = cw_get_channel_by_name_locked(name))) {
				pbx_builtin_setvar_helper(chan, varname, cw_manager_msg_header(req, "Value"));
				cw_channel_unlock(chan);
				msg = cw_manager_response("Success", "Variable Set");
				cw_object_put(chan);
			} else
				msg = cw_manager_response("Error", "No such channel");
		} else
			msg = cw_manager_response("Error", "No variable specified");
	} else
		msg = cw_manager_response("Error", "No channel specified");

	return msg;
}


static const char mandescr_getvar[] =
"Description: Get the value of a local channel variable.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel to read variable from\n"
"	*Variable: Variable name\n";

static struct cw_manager_message *action_getvar(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg;
	struct cw_channel *chan;
	char *name = cw_manager_msg_header(req, "Channel");
	char *varname;
	struct cw_var_t *var;

	CW_UNUSED(sess);

	if (!cw_strlen_zero(name)) {
		varname = cw_manager_msg_header(req, "Variable");
		if (!cw_strlen_zero(varname)) {
			if ((chan = cw_get_channel_by_name_locked(name))) {
				cw_channel_unlock(chan);
				var = pbx_builtin_getvar_helper(chan, cw_hash_var_name(varname), varname);

				if ((msg = cw_manager_response("Success", NULL))) {
					cw_manager_msg(&msg, 2,
						cw_msg_tuple("Variable", "%s", varname),
						cw_msg_tuple("Value", "%s", (var ? var->value : ""))
					);
				}

				cw_object_put(chan);
				if (var)
					cw_object_put(var);
			} else
				msg = cw_manager_response("Error", "No such channel");
		} else
			msg = cw_manager_response("Error", "No variable specified");
	} else
		msg = cw_manager_response("Error", "No channel specified");

	return msg;
}


/*! \brief  action_status: Manager "status" command to show channels */
/* Needs documentation... */
struct action_status_args {
	struct timeval now;
	struct mansession *sess;
	const struct message *req;
};

static int action_status_one(struct cw_object *obj, void *data)
{
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);
	struct action_status_args *args = data;
	struct cw_manager_message *msg = NULL;
	long elapsed_seconds = 0;
	long billable_seconds = 0;

	cw_channel_lock(chan);

	cw_manager_msg(&msg, 8,
		cw_msg_tuple("Event",        "%s", "Status"),
		cw_msg_tuple("Privilege",    "%s", "Call"),
		cw_msg_tuple("Channel",      "%s", chan->name),
		cw_msg_tuple("Uniqueid",     "%s", chan->uniqueid),
		cw_msg_tuple("CallerID",     "%s", (chan->cid.cid_num ? chan->cid.cid_num : "<unknown>")),
		cw_msg_tuple("CallerIDName", "%s", (chan->cid.cid_name ? chan->cid.cid_name : "<unknown>")),
		cw_msg_tuple("Account",      "%s", chan->accountcode),
		cw_msg_tuple("State",        "%s", cw_state2str(chan->_state))
	);

	if (msg && chan->pbx) {
		cw_manager_msg(&msg, 3,
				cw_msg_tuple("Context",   "%s", chan->context),
				cw_msg_tuple("Extension", "%s", chan->exten),
				cw_msg_tuple("Priority",  "%d", chan->priority)
		);
	}

	if (msg && chan->cdr) {
		elapsed_seconds = args->now.tv_sec - chan->cdr->start.tv_sec;
		if (chan->cdr->answer.tv_sec > 0)
			billable_seconds = args->now.tv_sec - chan->cdr->answer.tv_sec;

		cw_manager_msg(&msg, 2,
				cw_msg_tuple("Seconds",         "%ld", elapsed_seconds),
				cw_msg_tuple("BillableSeconds", "%ld", billable_seconds)
		);
	}

	if (msg && chan->_bridge) {
		cw_manager_msg(&msg, 1,
				cw_msg_tuple("Link", "%s", chan->_bridge->name)
		);
	}

	cw_channel_unlock(chan);

	if (msg)
		return cw_manager_send(args->sess, args->req, &msg);

	return -1;
}

static struct cw_manager_message *action_status(struct mansession *sess, const struct message *req)
{
	struct action_status_args args;
	struct cw_manager_message *msg = NULL;
	char *name = cw_manager_msg_header(req, "Channel");
	struct cw_channel *chan;
	int err = 0;

	if ((msg = cw_manager_response("Success", "Channel status will follow")) && !cw_manager_send(sess, req, &msg)) {
		args.sess = sess;
		args.req = req;
		args.now = cw_tvnow();

		if (!cw_strlen_zero(name)) {
			if ((chan = cw_get_channel_by_name_locked(name))) {
				err = action_status_one(&chan->obj, &args);
				cw_channel_unlock(chan);
				cw_object_put(chan);
			} else {
				if (!(msg = cw_manager_response("Error", "No such channel")) || cw_manager_send(sess, req, &msg))
					err = -1;
			}
		} else
			err = cw_registry_iterate(&channel_registry, action_status_one, &args);

		if (!err)
			cw_manager_msg(&msg, 1, cw_msg_tuple("Event", "%s", "StatusComplete"));
	}

	return msg;
}


static const char mandescr_redirect[] =
"Description: Redirect (transfer) a call.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel to redirect\n"
"	ExtraChannel: Second call leg to transfer (optional)\n"
"	*Exten: Extension to transfer to\n"
"	*Context: Context to transfer to\n"
"	*Priority: Priority to transfer to\n";

/*! \brief  action_redirect: The redirect manager command */
static struct cw_manager_message *action_redirect(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg = NULL;
	char *name = cw_manager_msg_header(req, "Channel");
	struct cw_channel *chan;
	char *context;
	char *exten;
	char *priority;
	int res;

	CW_UNUSED(sess);

	if (!cw_strlen_zero(name)) {
		if ((chan = cw_get_channel_by_name_locked(name))) {
			context = cw_manager_msg_header(req, "Context");
			exten = cw_manager_msg_header(req, "Exten");
			priority = cw_manager_msg_header(req, "Priority");

			res = cw_async_goto(chan, context, exten, priority);
			cw_channel_unlock(chan);
			cw_object_put(chan);

			if (!res) {
				name = cw_manager_msg_header(req, "ExtraChannel");
				if (!cw_strlen_zero(name)) {
					if ((chan = cw_get_channel_by_name_locked(name))) {
						if (!cw_async_goto(chan, context, exten, priority))
							msg = cw_manager_response("Success", "Dual Redirect successful");
						else
							msg = cw_manager_response("Error", "Secondary redirect failed");

						cw_channel_unlock(chan);
						cw_object_put(chan);
					} else
						msg = cw_manager_response("Error", "Secondary channel does not exist");
				}

				if (!msg)
					msg = cw_manager_response("Success", "Redirect successful");
			} else
				msg = cw_manager_response("Error", "Redirect failed");
		} else
			msg = cw_manager_response("Error", "Channel does not exist");
	} else
		msg = cw_manager_response("Error", "Channel not specified");

	return msg;
}


static const char mandescr_command[] =
"Description: Run a CLI command.\n"
"Variables: (Names marked with * are required)\n"
"	*Command: CallWeaver CLI command to run\n";

/*! \brief  action_command: Manager command "command" - execute CLI command */
static struct cw_manager_message *action_command(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg = NULL;
	char *cmd = cw_manager_msg_header(req, "Command");

	CW_UNUSED(sess);

	if (cmd && *cmd != '?') {
		if ((msg = cw_manager_response("Follows", NULL))) {
			msg->ds.used -= 2;

			cw_cli_command(&msg->ds, cmd);
			cw_dynstr_printf(&msg->ds, "%s--END COMMAND--\r\n\r\n", (msg->ds.data[msg->ds.used - 1] != '\n' ? "\n" : ""));
		}
	} else
		msg = cw_manager_response("Error", NULL);

	return msg;
}


static const char mandescr_complete[] =
"Description: Return possible completions for a CallWeaver CLI command.\n"
"	*Command: CallWeaver CLI command to complete\n";

static struct cw_manager_message *action_complete(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg = NULL;
	char *cmd = cw_manager_msg_header(req, "Command");

	CW_UNUSED(sess);

	if (cmd) {
		if ((msg = cw_manager_response("Completion", NULL))) {
			msg->ds.used -= 2;

			cw_cli_generator(&msg->ds, cmd);
			cw_dynstr_printf(&msg->ds, "--END COMMAND--\r\n\r\n");
		}
	} else
		msg = cw_manager_response("Error", NULL);

	return msg;
}


static void *fast_originate(void *data)
{
	struct fast_originate_helper *in = data;
	int res;
	int reason = 0;
	struct cw_channel *chan = NULL;

	if (!cw_strlen_zero(in->app)) {
		res = cw_pbx_outgoing_app(in->tech, CW_FORMAT_SLINEAR, in->data, in->timeout, in->app, in->appdata, &reason, 1, 
			!cw_strlen_zero(in->cid_num) ? in->cid_num : NULL, 
			!cw_strlen_zero(in->cid_name) ? in->cid_name : NULL,
			&in->vars, &chan);
	} else {
		res = cw_pbx_outgoing_exten(in->tech, CW_FORMAT_SLINEAR, in->data, in->timeout, in->context, in->exten, in->priority, &reason, 1, 
			!cw_strlen_zero(in->cid_num) ? in->cid_num : NULL, 
			!cw_strlen_zero(in->cid_name) ? in->cid_name : NULL,
			&in->vars, &chan);
	}

	cw_manager_event(CW_EVENT_FLAG_CALL, (res ? "OriginateFailure" : "OriginateSuccess"),
		6,
		cw_msg_tuple("ActionID", "%s",    in->actionid),
		cw_msg_tuple("Channel",  "%s/%s", in->tech, in->data),
		cw_msg_tuple("Context",  "%s",    in->context),
		cw_msg_tuple("Exten",    "%s",    in->exten),
		cw_msg_tuple("Reason",   "%d",    reason),
		cw_msg_tuple("Uniqueid", "%s",    (chan ? chan->uniqueid : "<null>"))
	);

	/* Locked by cw_pbx_outgoing_exten or cw_pbx_outgoing_app */
	if (chan)
		cw_channel_unlock(chan);
	cw_registry_destroy(&in->vars);
	free(in);
	return NULL;
}

static const char mandescr_originate[] =
"Description: Generates an outgoing call to an Extension/Context/Priority or\n"
"  Application/Data\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel name to call\n"
"	Exten: Extension to use (requires 'Context' and 'Priority')\n"
"	Context: Context to use (requires 'Exten' and 'Priority')\n"
"	Priority: Priority to use (requires 'Exten' and 'Context')\n"
"	Application: Application to use\n"
"	Data: Data to use (requires 'Application')\n"
"	Timeout: How long to wait for call to be answered (in ms)\n"
"	CallerID: Caller ID to be set on the outgoing channel\n"
"	Variable: Channel variable to set, multiple Variable: headers are allowed\n"
"	Account: Account code\n"
"	Async: Set to 'true' for fast origination\n";

static struct cw_manager_message *action_originate(struct mansession *sess, const struct message *req)
{
	char tmp[256];
	char tmp2[256];
	struct cw_manager_message *msg = NULL;
	struct fast_originate_helper *fast;
	char *name = cw_manager_msg_header(req, "Channel");
	char *exten = cw_manager_msg_header(req, "Exten");
	char *context = cw_manager_msg_header(req, "Context");
	char *priority = cw_manager_msg_header(req, "Priority");
	char *timeout = cw_manager_msg_header(req, "Timeout");
	char *callerid = cw_manager_msg_header(req, "CallerID");
	char *account = cw_manager_msg_header(req, "Account");
	char *app = cw_manager_msg_header(req, "Application");
	char *appdata = cw_manager_msg_header(req, "Data");
	char *async = cw_manager_msg_header(req, "Async");
	char *tech, *data;
	char *l=NULL, *n=NULL;
	pthread_t th;
	int pi = 0;
	int res;
	int to = 30000;
	int reason = 0;

	CW_UNUSED(sess);

	if (!name)
		return cw_manager_response("Error", "Channel not specified");

	if (!cw_strlen_zero(priority) && (sscanf(priority, "%d", &pi) != 1))
		return cw_manager_response("Error", "Invalid priority");

	if (!cw_strlen_zero(timeout) && (sscanf(timeout, "%d", &to) != 1))
		return cw_manager_response("Error", "Invalid timeout");

	cw_copy_string(tmp, name, sizeof(tmp));
	tech = tmp;
	data = strchr(tmp, '/');
	if (!data)
		return cw_manager_response("Error", "Invalid channel");

	*data++ = '\0';
	cw_copy_string(tmp2, callerid, sizeof(tmp2));
	cw_callerid_parse(tmp2, &n, &l);
	if (n) {
		if (cw_strlen_zero(n))
			n = NULL;
	}
	if (l) {
		cw_shrink_phone_number(l);
		if (cw_strlen_zero(l))
			l = NULL;
	}

	if ((fast = malloc(sizeof(struct fast_originate_helper)))) {
		int x;

		cw_var_registry_init(&fast->vars, 1024);

		for (x = 0; x < req->hdrcount; x++) {
			if (!strcasecmp("Variable", req->header[x].key)) {
				char *varname, *varval;
				varname = varval = cw_strdupa(req->header[x].val);
				strsep(&varval, "=");
				if (varval && !cw_strlen_zero(varname))
					cw_var_assign(&fast->vars, varname, varval);
			}
		}

		if (account) {
			/* FIXME: this is rubbish, surely? */
			cw_var_assign(&fast->vars, "CDR(accountcode|r)", account);
		}

		if (cw_true(async)) {
			memset(fast, 0, sizeof(struct fast_originate_helper));
			if (!cw_strlen_zero(req->actionid))
				cw_copy_string(fast->actionid, req->actionid, sizeof(fast->actionid));
			cw_copy_string(fast->tech, tech, sizeof(fast->tech));
   			cw_copy_string(fast->data, data, sizeof(fast->data));
			cw_copy_string(fast->app, app, sizeof(fast->app));
			cw_copy_string(fast->appdata, appdata, sizeof(fast->appdata));
			if (l)
				cw_copy_string(fast->cid_num, l, sizeof(fast->cid_num));
			if (n)
				cw_copy_string(fast->cid_name, n, sizeof(fast->cid_name));
			cw_copy_string(fast->context, context, sizeof(fast->context));
			cw_copy_string(fast->exten, exten, sizeof(fast->exten));
			fast->timeout = to;
			fast->priority = pi;
			if (cw_pthread_create(&th, &global_attr_detached, fast_originate, fast)) {
				cw_registry_destroy(&fast->vars);
				free(fast);
				res = -1;
			} else {
				res = 0;
			}
		} else if (!cw_strlen_zero(app)) {
			res = cw_pbx_outgoing_app(tech, CW_FORMAT_SLINEAR, data, to, app, appdata, &reason, 1, l, n, &fast->vars, NULL);
			cw_registry_destroy(&fast->vars);
			free(fast);
		} else {
			if (exten && context && pi)
				res = cw_pbx_outgoing_exten(tech, CW_FORMAT_SLINEAR, data, to, context, exten, pi, &reason, 1, l, n, &fast->vars, NULL);
			else {
				msg = cw_manager_response("Error", "Originate with 'Exten' requires 'Context' and 'Priority'");
				res = -1;
			}
			cw_registry_destroy(&fast->vars);
			free(fast);
		}
	} else
		res = -1;

	if (!msg) {
		if (!res)
			cw_manager_response("Success", "Originate successfully queued");
		else
			cw_manager_response("Error", "Originate failed");
	}

	return msg;
}

static const char mandescr_mailboxstatus[] =
"Description: Checks a voicemail account for status.\n"
"Variables: (Names marked with * are required)\n"
"	*Mailbox: Full mailbox ID <mailbox>@<vm-context>\n"
"Returns number of messages.\n"
"	Message: Mailbox Status\n"
"	Mailbox: <mailboxid>\n"
"	Waiting: <count>\n"
"\n";
static struct cw_manager_message *action_mailboxstatus(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg = NULL;
	char *mailbox = cw_manager_msg_header(req, "Mailbox");

	CW_UNUSED(sess);

	if (!cw_strlen_zero(mailbox)) {
		if ((msg = cw_manager_response("Success", "Mailbox Status"))) {
			cw_manager_msg(&msg, 2,
				cw_msg_tuple("Mailbox", "%s", mailbox),
				cw_msg_tuple("Waiting", "%d", cw_app_has_voicemail(mailbox, NULL))
			);
		}
	} else
		msg = cw_manager_response("Error", "Mailbox not specified");

	return msg;
}

static const char mandescr_mailboxcount[] =
"Description: Checks a voicemail account for new messages.\n"
"Variables: (Names marked with * are required)\n"
"	*Mailbox: Full mailbox ID <mailbox>@<vm-context>\n"
"Returns number of new and old messages.\n"
"	Message: Mailbox Message Count\n"
"	Mailbox: <mailboxid>\n"
"	NewMessages: <count>\n"
"	OldMessages: <count>\n"
"\n";
static struct cw_manager_message *action_mailboxcount(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg = NULL;
	char *mailbox = cw_manager_msg_header(req, "Mailbox");
	int newmsgs = 0, oldmsgs = 0;

	CW_UNUSED(sess);

	if (!cw_strlen_zero(mailbox)) {
		if ((msg = cw_manager_response("Success", "Mailbox Message Count"))) {
			cw_app_messagecount(mailbox, &newmsgs, &oldmsgs);
			cw_manager_msg(&msg, 3,
				cw_msg_tuple("Mailbox", "%s", mailbox),
				cw_msg_tuple("NewMessages", "%d", newmsgs),
				cw_msg_tuple("OldMessages", "%d", oldmsgs)
			);
		}
	} else
		msg = cw_manager_response("Error", "Mailbox not specified");

	return msg;
}

static const char mandescr_extensionstate[] =
"Description: Report the extension state for given extension.\n"
"  If the extension has a hint, will use devicestate to check\n"
"  the status of the device connected to the extension.\n"
"Variables: (Names marked with * are required)\n"
"	*Exten: Extension to check state on\n"
"	*Context: Context for extension\n"
"Will return an \"Extension Status\" message.\n"
"The response will include the hint for the extension and the status.\n";

static struct cw_manager_message *action_extensionstate(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg = NULL;
	const char *exten = cw_manager_msg_header(req, "Exten");
	const char *context = cw_manager_msg_header(req, "Context");
	int status;

	CW_UNUSED(sess);

	if (!cw_strlen_zero(exten)) {
		if ((msg = cw_manager_response("Success", "Extension Status"))) {
			struct cw_dynstr hint = CW_DYNSTR_INIT;

			if (cw_strlen_zero(context))
				context = "default";

			status = cw_extension_state(NULL, context, exten);
			cw_get_hint(&hint, NULL, NULL, context, exten);

			cw_manager_msg(&msg, 4,
				cw_msg_tuple("Exten", "%s", exten),
				cw_msg_tuple("Context", "%s", context),
				cw_msg_tuple("Hint", "%s", hint.data),
				cw_msg_tuple("Status", "%d", status)
			);

			cw_dynstr_free(&hint);
		}
	} else
		msg = cw_manager_response("Error", "Extension not specified");

	return msg;
}


static const char mandescr_timeout[] =
"Description: Hangup a channel after a certain time.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel name to hangup\n"
"	*Timeout: Maximum duration of the call (sec)\n"
"Acknowledges set time with 'Timeout Set' message\n";

static struct cw_manager_message *action_timeout(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg = NULL;
	struct cw_channel *chan = NULL;
	char *name = cw_manager_msg_header(req, "Channel");
	int timeout;

	CW_UNUSED(sess);

	if (!cw_strlen_zero(name)) {
		if ((timeout = atoi(cw_manager_msg_header(req, "Timeout")))) {
			if ((chan = cw_get_channel_by_name_locked(name))) {
				cw_channel_setwhentohangup(chan, timeout);
				cw_channel_unlock(chan);
				cw_object_put(chan);
				msg = cw_manager_response("Success", "Timeout Set");
			} else
				msg = cw_manager_response("Error", "No such channel");
		} else
			msg = cw_manager_response("Error", "No timeout specified");
	} else
		msg = cw_manager_response("Error", "No channel specified");

	return msg;
}


static int process_message(struct mansession *sess, const struct message *req)
{
	char *action = cw_manager_msg_header(req, "Action");
	struct cw_manager_message *msg = NULL;
	int islogoff = 0;

	if (cw_strlen_zero(action))
		msg = cw_manager_response("Error", "Missing action in request");
	else if (!strcasecmp(action, "Challenge")) {
		char *authtype;

		if ((authtype = cw_manager_msg_header(req, "AuthType")) && !strcasecmp(authtype, "MD5")) {
			if ((msg = cw_manager_response("Success", NULL))) {
				if (cw_strlen_zero(sess->challenge))
					snprintf(sess->challenge, sizeof(sess->challenge), "%lu", (unsigned long)cw_random());

				cw_manager_msg(&msg, 1, cw_msg_tuple("Challenge", "%s", sess->challenge));
			}
		} else
			msg = cw_manager_response("Error", "Must specify AuthType");
	} else if (!strcasecmp(action, "Login")) {
		msg = authenticate(sess, req);
	} else if (!strcasecmp(action, "Logoff")) {
		msg = cw_manager_response("Success", "See ya");
		islogoff = 1;
	} else if (sess->authenticated) {
		struct cw_object *it;

		if ((it = cw_registry_find(&manager_action_registry, 0, 0, action))) {
			struct manager_action *act = container_of(it, struct manager_action, obj);
			if ((sess->writeperm & act->authority) == act->authority)
				msg = act->func(sess, req);
			else
				msg = cw_manager_response("Error", "Permission denied");
			cw_object_put(act);
		} else
			msg = cw_manager_response("Error", "Invalid/unknown command");
	} else
		msg = cw_manager_response("Error", "Authentication Required");

	if (msg) {
		/* Yes, we _could_ carry on if it overflowed rather than failed but dropping
		 * a response message leaves the client out-of-state and potentially hung,
		 * leaking memory or just plain confused and upset. Responses to action
		 * requests are important.
		 */
		if (!cw_manager_send(sess, req, &msg)) {
			if (!islogoff)
				return 0;
		} else
			cw_log(CW_LOG_ERROR, "failed to send response to %l@ - session will be closed\n", &sess->addr);
	}

	return -1;
}


static void *manager_session_ami_read(void *data)
{
	char buf[32768];
	struct message m;
	struct mansession *sess = data;
	char **hval;
	int pos, state;
	int res;

	memset(&m, 0, sizeof(m));

	pos = 0;
	state = 0;
	hval = NULL;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	for (;;) {
		if ((res = read(sess->fd, buf + pos, sizeof(buf) - pos)) <= 0) {
			pthread_cancel(sess->writer_tid);
			break;
		}

		for (; res; pos++, res--) {
			switch (state) {
				case 0: /* Start of header line */
					if (buf[pos] == '\r') {
						buf[pos] = '\0';
					} else if (buf[pos] == '\n') {
						/* End of message, go do it */
						pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &sess->lock);
						pthread_mutex_lock(&sess->lock);
						sess->m = &m;
						pthread_cond_signal(&sess->activity);
						pthread_cond_wait(&sess->ack, &sess->lock);
						pthread_cleanup_pop(1);
						m.actionid = NULL;
						m.hdrcount = 0;
						memmove(buf, &buf[pos + 1], res - 1);
						pos = -1;
					} else if (buf[pos] == ' ' || buf[pos] == '\t') {
						/* Continuation of the previous header, backtrack replacing nulls with spaces */
						char *p = buf + pos - 1;
						while (p >= buf && *p == '\0') *(p--) = ' ';
					} else {
						if (m.hdrcount < arraysize(m.header))
							m.header[m.hdrcount].key = &buf[pos];
						state = 1;
					}
					break;
				case 1: /* In header name, looking for ':' */
					if (buf[pos] == ':') {
						/* End of header name, skip spaces to value */
						state = 2;
						buf[pos] = '\0';
						switch (&buf[pos] - m.header[m.hdrcount].key) {
							case sizeof("ActionID")-1:
								if (!strcasecmp(m.header[m.hdrcount].key, "ActionID"))
									hval = &m.actionid;
								break;
						}
						break;
					} else if (buf[pos] != '\r' && buf[pos] != '\n')
						break;
					/* Fall through all the way - no colon, no value */
				case 2: /* Skipping spaces before value */
					if (buf[pos] == ' ' || buf[pos] == '\t')
						break;
					else {
						if (hval)
							*hval = &buf[pos];
						else if (m.hdrcount < arraysize(m.header))
							m.header[m.hdrcount].val = &buf[pos];
						state = 3;
					}
					/* Fall through - we are on the start of the value and it may be blank */
				case 3: /* In value, looking for end of line */
					if (buf[pos] == '\r')
						buf[pos] = '\0';
					else if (buf[pos] == '\n') {
						if (hval)
							hval = NULL;
						else if (m.hdrcount < arraysize(m.header))
							m.hdrcount++;
						state = 0;
					}
					break;
			}
		}

		if (pos == sizeof(buf)) {
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
			cw_log(CW_LOG_ERROR, "Manager session %l@ dropped due to oversize message\n", &sess->addr);
			break;
		}
	}

	return NULL;
}


int manager_session_ami(struct mansession *sess, const struct cw_manager_message *event)
{
	return cw_write_all(sess->fd, event->ds.data, event->ds.used);
}


static void manager_session_cleanup(void *data)
{
	struct mansession *sess = data;

	if (sess->reg_entry)
		cw_registry_del(&manager_session_registry, sess->reg_entry);

	if (sess->authenticated) {
		if (sess->username[0]) {
			if (option_verbose > 3 && displayconnects)
				cw_verbose(VERBOSE_PREFIX_2 "Manager '%s' logged off from %l@\n", sess->username, &sess->addr);
			cw_log(CW_LOG_EVENT, "Manager '%s' logged off from %l@\n", sess->username, &sess->addr);
		}
	}

	if (!pthread_equal(sess->reader_tid, CW_PTHREADT_NULL)) {
		pthread_cancel(sess->reader_tid);
		pthread_join(sess->reader_tid, NULL);
	}

	cw_object_put(sess);
}


static void *manager_session(void *data)
{
	static const int on = 1;
	static const int off = 0;
	struct mansession *sess = data;
	struct manager_listener_pvt *pvt;
	int res;

	sess->reader_tid = CW_PTHREADT_NULL;

	pthread_cleanup_push(manager_session_cleanup, sess);

	sess->reg_entry = cw_registry_add(&manager_session_registry, 0, &sess->obj);

	/* If there is an fd already supplied we will read AMI requests from it */
	if (sess->fd >= 0) {
		setsockopt(sess->fd, SOL_TCP, TCP_NODELAY, &on, sizeof(on));
		setsockopt(sess->fd, SOL_TCP, TCP_CORK, &on, sizeof(on));

		if (sess->pvt_obj && (pvt = container_of(sess->pvt_obj, struct manager_listener_pvt, obj)) && pvt->banner[0]) {
			cw_write_all(sess->fd, pvt->banner, strlen(pvt->banner));
			cw_write_all(sess->fd, "\r\n", sizeof("\r\n") - 1);
			sess->pvt_obj = NULL;
			cw_object_put(pvt);
		} else
			cw_write_all(sess->fd, MANAGER_AMI_HELLO, sizeof(MANAGER_AMI_HELLO) - 1);

		if ((res = cw_pthread_create(&sess->reader_tid, &global_attr_default, manager_session_ami_read, sess))) {
			cw_log(CW_LOG_ERROR, "session reader thread creation failed: %s\n", strerror(res));
			return NULL;
		}
	}

	for (;;) {
		struct cw_manager_message *event = NULL;

		pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &sess->lock);
		pthread_mutex_lock(&sess->lock);

		/* If there's no request message and no queued events
		 * we have to wait for activity.
		 */
		if (!sess->m && sess->q_r == sess->q_w) {
			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
#ifdef TCP_CORK
			if (sess->fd >= 0)
				setsockopt(sess->fd, SOL_TCP, TCP_CORK, &off, sizeof(off));
#endif

			pthread_cond_wait(&sess->activity, &sess->lock);

#ifdef TCP_CORK
			if (sess->fd >= 0)
				setsockopt(sess->fd, SOL_TCP, TCP_CORK, &on, sizeof(on));
#endif
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		}

		/* Fetch the next event (if any) now. Once we have that
		 * we can unlock the session.
		 */
		if (sess->q_r != sess->q_w) {
			event = sess->q[sess->q_r];
			sess->q_r = (sess->q_r + 1) % sess->q_size;
			sess->q_count--;
		}

		pthread_cleanup_pop(1);

		if (sess->m) {
			if (process_message(sess, sess->m))
				break;

			/* Remove the queued message and signal completion to the reader */
			pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &sess->lock);
			pthread_mutex_lock(&sess->lock);
			sess->m = NULL;
			pthread_cond_signal(&sess->ack);
			pthread_cleanup_pop(1);
		}

		if (event) {
			if ((res = sess->handler(sess, event)) < 0)
				cw_log(CW_LOG_WARNING, "Disconnecting manager session %l@, handler gave: %s\n", &sess->addr, strerror(errno));
			cw_object_put(event);
			if (res < 0)
				break;
		}
	}

	pthread_cleanup_pop(1);
	return NULL;
}


struct mansession *manager_session_start(int (* const handler)(struct mansession *, const struct cw_manager_message *), int fd, const struct sockaddr *addr, socklen_t addrlen, struct cw_object *pvt_obj, int readperm, int writeperm, int send_events)
{
	struct mansession *sess = NULL;

	if (!(sess = calloc(1, sizeof(*sess) - sizeof(sess->addr) + addrlen)))
		goto out_of_memory;

	if (!(sess->q = malloc(queuesize * sizeof(*sess->q))))
		goto out_of_memory;
	sess->q_size = queuesize;

	memcpy(&sess->addr, addr, addrlen);

	sess->fd = fd;
	sess->readperm = readperm;
	if ((sess->writeperm = writeperm))
		sess->authenticated = 1;
	sess->send_events = send_events;
	sess->handler = handler;
	if (pvt_obj)
		sess->pvt_obj = cw_object_dup_obj(pvt_obj);

	cw_object_init(sess, NULL, 2);
	sess->obj.release = mansession_release;
	pthread_mutex_init(&sess->lock, NULL);
	pthread_cond_init(&sess->activity, NULL);
	pthread_cond_init(&sess->ack, NULL);

	if (cw_pthread_create(&sess->writer_tid, &global_attr_detached, manager_session, sess)) {
		cw_log(CW_LOG_ERROR, "Thread creation failed: %s\n", strerror(errno));
		cw_object_put(sess);
		cw_object_put(sess);
		sess = NULL;
	}

out:
	return sess;

out_of_memory:
	if (sess)
		free(sess);
	cw_log(CW_LOG_ERROR, "Out of memory\n");
	goto out;
}


void manager_session_shutdown(struct mansession *sess)
{
	/* Do not send any more events */
	sess->send_events = 0;

	/* If there is a reader tell it to stop handling incoming requests */
	if (!pthread_equal(sess->reader_tid, CW_PTHREADT_NULL))
		pthread_cancel(sess->reader_tid);

	/* Tell the writer to go down as soon as it as drained the queue */
	pthread_cancel(sess->writer_tid);
}

void manager_session_end(struct mansession *sess)
{
	/* If it wasn't shut down before, it is now */
	manager_session_shutdown(sess);

	/* The writer handles the reader clean up */
	pthread_join(sess->writer_tid, NULL);
	cw_object_put(sess);
}


static void manager_msg_free(struct cw_object *obj)
{
	struct cw_manager_message *it = container_of(obj, struct cw_manager_message, obj);

	cw_object_destroy(it);
	cw_dynstr_free(&it->ds);
	free(it);
}


static int add_msg(struct cw_manager_message **msg_p, size_t count, int map[], const char *fmt, va_list ap)
{
	struct cw_manager_message *msg;
	int o_count, o_len;
	int i;

	o_count = (*msg_p)->count;

	if ((msg = realloc(*msg_p, sizeof(struct cw_manager_message) + sizeof(msg->map[0]) * (((o_count + count) << 1) + 1)))) {
		*msg_p = msg;

		/* Drop the previous blank line termination marker */
		msg->ds.used -= 2;
		o_len = msg->ds.used;

		if (!cw_dynstr_vprintf(&msg->ds, fmt, ap)) {
			for (i = 0; i < count; i++) {
				msg->map[((o_count + i) << 1) + 0] = map[(i << 1) + 0] + o_len;
				msg->map[((o_count + i) << 1) + 1] = map[(i << 1) + 1] + o_len;
			}
			msg->count = o_count + count;
			return 0;
		}
	}

	/* Out of memory to expand the msg or dynstr but we can't log it here because
	 * logging it just generates another event that will ultimately come
	 * here and find it's out of memory and will log the fact causing another
	 * event to be generated that will...
	 */
	cw_object_put(*msg_p);
	*msg_p = NULL;
	return -1;
}


static struct cw_manager_message *make_msg(size_t initsize, size_t chunk, size_t count, int map[], const char *fmt, va_list ap)
{
	struct cw_manager_message *msg;

	if ((msg = malloc(sizeof(struct cw_manager_message) + sizeof(msg->map[0]) * ((count << 1) + 1)))) {
		cw_object_init(msg, NULL, 1);
		msg->obj.release = manager_msg_free;
		cw_dynstr_init(&msg->ds, initsize, chunk);
		cw_dynstr_vprintf(&msg->ds, fmt, ap);
		msg->count = count;
		memcpy(msg->map , map, ((count << 1) + 1) * sizeof(msg->map[0]));
	}

	return msg;
}


int cw_manager_msg_func(struct cw_manager_message **msg_p, size_t count, int map[], const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	if (*msg_p)
		ret = add_msg(msg_p, count, map, fmt, ap);
	else
		ret = !(*msg_p = make_msg(1024, 1024, count, map, fmt, ap));
	va_end(ap);

	return ret;
}


struct manager_event_args {
	int category;
	size_t count;
	struct cw_manager_message *msg;
	int *map;
	const char *fmt;
	va_list ap;
};

static int manager_event_print(struct cw_object *obj, void *data)
{
	struct mansession *it = container_of(obj, struct mansession, obj);
	struct manager_event_args *args = data;

	if ((it->readperm & args->category) == args->category && (it->send_events & args->category) == args->category) {
		if (args->msg || (args->msg = make_msg(0, 1, args->count, args->map, args->fmt, args->ap))) {
			struct cw_manager_message *msg = cw_object_dup(args->msg);
			cw_manager_send(it, NULL, &msg);
		}
	}

	return 0;
}

void cw_manager_event_func(cw_event_flag category, size_t count, int map[], const char *fmt, ...)
{
	struct manager_event_args args = {
		.count = count,
		.msg = NULL,
		.category = category,
		.map = map,
		.fmt = fmt,
	};

	va_start(args.ap, fmt);

	cw_registry_iterate(&manager_session_registry, manager_event_print, &args);

	va_end(args.ap);

	if (args.msg)
		cw_object_put(args.msg);
}

static int manager_state_cb(char *context, char *exten, int state, void *data)
{
	CW_UNUSED(data);

	/* Notify managers of change */
	cw_manager_event(CW_EVENT_FLAG_CALL, "ExtensionStatus",
		3,
		cw_msg_tuple("Exten",   "%s", exten),
		cw_msg_tuple("Context", "%s", context),
		cw_msg_tuple("Status",  "%d", state)
	);
	return 0;
}


static struct manager_action manager_actions[] = {
	{
		.action = "Ping",
		.authority = 0,
		.func = action_ping,
		.synopsis = "Keepalive command",
		.description = mandescr_ping,
	},
	{
		.action = "Version",
		.authority = 0,
		.func = action_version,
		.synopsis = "Return version, hostname and pid of the running CallWeaver",
		.description = mandescr_version,
	},
	{
		.action = "Events",
		.authority = 0,
		.func = action_events,
		.synopsis = "Control Event Flow",
		.description = mandescr_events,
	},
	{
		.action = "Logoff",
		.authority = 0,
		.func = NULL,
		.synopsis = "Logoff Manager",
		.description = mandescr_logoff,
	},
	{
		.action = "Hangup",
		.authority = CW_EVENT_FLAG_CALL,
		.func = action_hangup,
		.synopsis = "Hangup Channel",
		.description = mandescr_hangup,
	},
	{
		.action = "Status",
		.authority = CW_EVENT_FLAG_CALL,
		.func = action_status,
		.synopsis = "Lists channel status",
	},
	{
		.action = "Setvar",
		.authority = CW_EVENT_FLAG_CALL,
		.func = action_setvar,
		.synopsis = "Set Channel Variable",
		.description = mandescr_setvar,
	},
	{
		.action = "Getvar",
		.authority = CW_EVENT_FLAG_CALL,
		.func = action_getvar,
		.synopsis = "Gets a Channel Variable",
		.description = mandescr_getvar,
	},
	{
		.action = "Redirect",
		.authority = CW_EVENT_FLAG_CALL,
		.func = action_redirect,
		.synopsis = "Redirect (transfer) a call",
		.description = mandescr_redirect,
	},
	{
		.action = "Originate",
		.authority = CW_EVENT_FLAG_CALL,
		.func = action_originate,
		.synopsis = "Originate Call",
		.description = mandescr_originate,
	},
	{
		.action = "Command",
		.authority = CW_EVENT_FLAG_COMMAND,
		.func = action_command,
		.synopsis = "Execute CallWeaver CLI Command",
		.description = mandescr_command,
	},
	{
		.action = "Complete",
		.authority = CW_EVENT_FLAG_COMMAND,
		.func = action_complete,
		.synopsis = "Return possible completions for a CallWeaver CLI Command",
		.description = mandescr_complete,
	},
	{
		.action = "ExtensionState",
		.authority = CW_EVENT_FLAG_CALL,
		.func = action_extensionstate,
		.synopsis = "Check Extension Status",
		.description = mandescr_extensionstate,
	},
	{
		.action = "AbsoluteTimeout",
		.authority = CW_EVENT_FLAG_CALL,
		.func = action_timeout,
		.synopsis = "Set Absolute Timeout",
		.description = mandescr_timeout,
	},
	{
		.action = "MailboxStatus",
		.authority = CW_EVENT_FLAG_CALL,
		.func = action_mailboxstatus,
		.synopsis = "Check Mailbox",
		.description = mandescr_mailboxstatus,
	},
	{
		.action = "MailboxCount",
		.authority = CW_EVENT_FLAG_CALL,
		.func = action_mailboxcount,
		.synopsis = "Check Mailbox Message Count",
		.description = mandescr_mailboxcount,
	},
	{
		.action = "ListCommands",
		.authority = 0,
		.func = action_listcommands,
		.synopsis = "List available manager commands",
		.description = mandescr_listcommands,
	},
};


static int manager_listener_read(struct cw_connection *conn)
{
	struct sockaddr *addr;
	struct manager_listener_pvt *pvt = container_of(conn->pvt_obj, struct manager_listener_pvt, obj);
	struct mansession *sess;
	socklen_t addrlen;
	int fd;
	int ret = 0;

	addrlen = conn->addrlen;
	addr = alloca(conn->addrlen);

	fd = accept_cloexec(conn->sock, addr, &addrlen);

	if (fd >= 0) {
		if (addr->sa_family == AF_LOCAL) {
			/* Local sockets don't return a path in their sockaddr (there isn't
			 * one really). However, if the remote is local so is the address
			 * we were listening on and the listening path is in the listener's
			 * name. We'll use that as a meaningful connection source for the
			 * sake of the connection list command.
			 */
			struct sockaddr_un *sun = (struct sockaddr_un *)addr;
			struct sockaddr_un *conn_sun = (struct sockaddr_un *)&conn->addr;
			strcpy(sun->sun_path, conn_sun->sun_path);
			addrlen = SUN_LEN(sun);
		}

		if ((sess = manager_session_start(pvt->handler, fd, addr, addrlen, conn->pvt_obj, pvt->readperm, pvt->writeperm, pvt->send_events)))
			cw_object_put(sess);

		goto out;
	}

	if (errno == ENFILE || errno == EMFILE || errno == ENOBUFS || errno == ENOMEM) {
		cw_log(CW_LOG_ERROR, "Accept failed: %s\n", strerror(errno));
		ret = 1000;
	}

out:
	return ret;
}


static void listener_pvt_free(struct cw_object *obj)
{
	struct manager_listener_pvt *it = container_of(obj, struct manager_listener_pvt, obj);

	free(it);
}


static void manager_listen(const char *spec, int (* const handler)(struct mansession *, const struct cw_manager_message *), int readperm, int writeperm, int send_events)
{
	struct manager_listener_pvt *pvt;
	const char *banner = NULL;
	struct addrinfo *addrs, *addr;
	struct sockaddr_un *sun = NULL;
	int banner_len = 0;
	int err;

	if (spec[0] == '"') {
		int i;

		for (i = 1; spec[i] && spec[i] != '"'; i++) {
			if (spec[i] == '\\')
				if (!spec[++i])
					break;
		}

		banner_len = i - 1;
		banner = spec + 1;
		spec = (spec[i] ? &spec[i + 1] : &spec[i]);
		while (*spec && isspace(*spec))
			spec++;
	}

	/* A file path is always absolute */
	if (spec[0] == '/') {
		int l = strlen(spec);
		addrs = alloca(sizeof(*addrs));
		addrs->ai_next = NULL;
		addrs->ai_addrlen = sizeof(*sun) - sizeof(sun->sun_path) + l + 1;
		sun = alloca(addrs->ai_addrlen);
		addrs->ai_addr = (struct sockaddr *)sun;
		sun->sun_family = AF_LOCAL;
		memcpy(sun->sun_path, spec, l + 1);
		unlink(spec);
		err = 0;
	} else {
		static const struct addrinfo hints = {
			.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_PASSIVE | AI_IDN,
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_STREAM,
			.ai_protocol = 0,
		};

		if ((err = cw_getaddrinfo(spec, "0", &hints, &addrs, NULL)))
			cw_log(CW_LOG_ERROR, "%s: %s\n", spec, gai_strerror(err));
	}

	if (!err) {
		for (addr = addrs; addr; addr = addr->ai_next) {
			if ((pvt = malloc(sizeof(*pvt) + banner_len + 1))) {
				struct cw_connection *conn;

				cw_object_init(pvt, NULL, 1);
				pvt->obj.release = listener_pvt_free;
				pvt->handler = handler;
				pvt->readperm = readperm;
				pvt->writeperm = writeperm;
				pvt->send_events = send_events;
				memcpy(pvt->banner, banner, banner_len);
				pvt->banner[banner_len] = '\0';

				if ((conn = cw_connection_listen(SOCK_STREAM, addr->ai_addr, addr->ai_addrlen, &tech_ami, &pvt->obj))) {
					/* Local listener sockets that are not pre-authenticated are public */
					if (addr->ai_addr->sa_family == AF_LOCAL && !writeperm)
						chmod(((struct sockaddr_un *)addr->ai_addr)->sun_path, 0666);

					if (option_verbose)
						cw_verbose("CallWeaver listening on %l@ (%s)\n", addr->ai_addr, spec);

					cw_object_put(conn);
				} else
					cw_log(CW_LOG_ERROR, "Unable to listen on %l@ (%s): %s\n", addr->ai_addr, spec, strerror(errno));

				cw_object_put(pvt);
			} else
				cw_log(CW_LOG_ERROR, "Out of memory\n");
		}
	}

	if (!sun)
		freeaddrinfo(addrs);
}


static int listener_close(struct cw_object *obj, void *data)
{
	struct cw_connection *conn = container_of(obj, struct cw_connection, obj);

	CW_UNUSED(data);

	if (conn->tech == &tech_ami && (conn->state == INIT || conn->state == LISTENING))
		cw_connection_close(conn);
	return 0;
}


int manager_reload(void)
{
	struct cw_config *cfg;
	struct cw_variable *v;
	const char *bindaddr, *portno;

	/* Shut down any existing listeners */
	cw_registry_iterate(&cw_connection_registry, listener_close, NULL);

	/* Reset to hard coded defaults */
	bindaddr = NULL;
	portno = NULL;
	displayconnects = 1;
	queuesize = DEFAULT_QUEUE_SIZE;

	/* Overlay configured values from the config file */
	cfg = cw_config_load("manager.conf");
	if (!cfg) {
		cw_log(CW_LOG_NOTICE, "Unable to open manager configuration manager.conf. Using defaults.\n");
	} else {
		for (v = cw_variable_browse(cfg, "general"); v; v = v->next) {
			if (!strcmp(v->name, "displayconnects"))
				displayconnects = cw_true(v->value);
			else if (!strcmp(v->name, "listen"))
				manager_listen(v->value, manager_session_ami, 0, 0, CW_EVENT_FLAG_CALL | CW_EVENT_FLAG_SYSTEM);
			else if (!strcmp(v->name, "queuesize"))
				queuesize = atol(v->value);

			/* DEPRECATED */
			else if (!strcmp(v->name, "block-sockets"))
				cw_log(CW_LOG_WARNING, "block_sockets is deprecated - remove it from manager.conf\n");
			else if (!strcmp(v->name, "bindaddr")) {
				cw_log(CW_LOG_WARNING, "Use of \"bindaddr\" in manager.conf is deprecated - use \"listen\" instead\n");
				bindaddr = v->value;
			} else if (!strcmp(v->name, "port")) {
				cw_log(CW_LOG_WARNING, "Use of \"port\" in manager.conf is deprecated - use \"listen\" instead\n");
				portno = v->value;
			} else if (!strcmp(v->name, "enabled")) {
				cw_log(CW_LOG_WARNING, "\"enabled\" is deprecated - remove it from manager.conf and replace \"bindaddr\" and \"port\" with a \"listen\"\n");
				if (cw_true(v->value)) {
					if (!bindaddr)
						bindaddr = "0.0.0.0";
					if (!portno)
						portno = MKSTR(DEFAULT_MANAGER_PORT);
				} else {
					cw_log(CW_LOG_WARNING, "\"enabled\" in manager.conf only controls \"bindaddr\", \"port\" listening. To disable \"listen\" entries just comment them out\n");
					bindaddr = portno = NULL;
				}
			} else if (!strcmp(v->name, "portno")) {
				cw_log(CW_LOG_NOTICE, "Use of portno in manager.conf is deprecated. Use 'port=%s' instead.\n", v->value);
				portno = v->value;
			}
		}
	}

	/* Start the listener for pre-authenticated consoles */
	manager_listen(cw_config[CW_SOCKET], manager_session_ami, CW_EVENT_FLAG_LOG_ALL | CW_EVENT_FLAG_PROGRESS, CW_EVENT_FLAG_COMMAND, 0);

	if (!cw_strlen_zero(cw_config[CW_CTL_PERMISSIONS])) {
		mode_t p;
		sscanf(cw_config[CW_CTL_PERMISSIONS], "%o", (int *) &p);
		if ((chmod(cw_config[CW_SOCKET], p)) < 0)
			cw_log(CW_LOG_WARNING, "Unable to change file permissions of %s: %s\n", cw_config[CW_SOCKET], strerror(errno));
	}

	if (!cw_strlen_zero(cw_config[CW_CTL_GROUP])) {
		struct group *grp;
		if ((grp = getgrnam(cw_config[CW_CTL_GROUP])) == NULL)
			cw_log(CW_LOG_WARNING, "Unable to find gid of group %s\n", cw_config[CW_CTL_GROUP]);
		else if (chown(cw_config[CW_SOCKET], -1, grp->gr_gid) < 0)
			cw_log(CW_LOG_WARNING, "Unable to change group of %s to %s: %s\n", cw_config[CW_SOCKET], cw_config[CW_CTL_GROUP], strerror(errno));
	}

	/* DEPRECATED */
	if (bindaddr && portno) {
		char buf[256];

		snprintf(buf, sizeof(buf), "%s:%s", bindaddr, portno);
		manager_listen(buf, manager_session_ami, 0, 0, CW_EVENT_FLAG_CALL | CW_EVENT_FLAG_SYSTEM);
	}

	if (cfg)
		cw_config_destroy(cfg);

	return 0;
}


int init_manager(void)
{
	manager_reload();

	cw_manager_action_register_multiple(manager_actions, arraysize(manager_actions));

	cw_cli_register_multiple(clicmds, arraysize(clicmds));
	cw_extension_state_add(NULL, NULL, manager_state_cb, NULL);

	return 0;
}
