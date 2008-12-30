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
 * The CallWeaver Management Interface
 *
 * Channel Management and more
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <netdb.h>
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
	char idtext[256];
	int priority;
	struct cw_registry vars;
};


static int displayconnects;
static int queuesize;


struct message {
	char *actionid;
	char *action;
	int hdrcount;
	struct {
		char *key;
		char *val;
	} header[80];
};


struct manager_listener {
	struct cw_object obj;
	struct cw_registry_entry *reg_entry;
	int sock;
	pthread_t tid;
	int (*handler)(struct mansession *, const struct manager_event *);
	int readperm, writeperm, send_events;
	char name[0];
};


static struct {
	const char *label;
	int len;
} perms[] = {
#define STR_LEN(s)	{ s, sizeof(s) - 1 }
	[CW_EVENT_NUM_ERROR]	= STR_LEN("error"),
	[CW_EVENT_NUM_WARNING]	= STR_LEN("warning"),
	[CW_EVENT_NUM_NOTICE]	= STR_LEN("notice"),
	[CW_EVENT_NUM_VERBOSE]	= STR_LEN("verbose"),
	[CW_EVENT_NUM_EVENT]	= STR_LEN("event"),
	[CW_EVENT_NUM_DTMF]	= STR_LEN("dtmf"),
	[CW_EVENT_NUM_DEBUG]	= STR_LEN("debug"),
	[CW_EVENT_NUM_PROGRESS]	= STR_LEN("progress"),

	[CW_EVENT_NUM_SYSTEM]	= STR_LEN("system"),
	[CW_EVENT_NUM_CALL]	= STR_LEN("call"),
	[CW_EVENT_NUM_COMMAND]	= STR_LEN("command"),
	[CW_EVENT_NUM_AGENT]	= STR_LEN("agent"),
	[CW_EVENT_NUM_USER]	= STR_LEN("user"),
#undef STR_LEN
};


#define MANAGER_AMI_HELLO	"CallWeaver Call Manager/1.0\r\n"


static int authority_to_str(int fd, int authority)
{
	int i, n, sep = 0;

	n = 0;
	for (i = 0; i < arraysize(perms); i++) {
		if ((authority & (1 << i)) && perms[i].label) {
			cw_cli(fd, (sep ? ", %s" : "%s"), perms[i].label);
			n += perms[i].len + sep;
			sep = 2;
		}
	}

	if (!sep) {
		cw_cli(fd, "<none>");
		n = sizeof("<none>") - 1;
	}

	return n;
}


int manager_str_to_eventmask(char *instr)
{
	int ret = 0;

	/* Logically you might expect an empty mask to mean nothing
	 * however it has historically meant everything. It's too
	 * late to risk changing it now.
	 */
	if (!instr || !*instr || cw_true(instr) || !strcasecmp(instr, "all") || (instr[0] == '-' && instr[1] == '1'))
		ret = -1;
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
				ret |= EVENT_FLAG_LOG_ALL;
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


static void append_event(struct mansession *sess, struct manager_event *event)
{
	int q_w_next;

	pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &sess->lock);
	pthread_mutex_lock(&sess->lock);

	q_w_next = (sess->q_w + 1) % sess->q_size;

	if (q_w_next != sess->q_r) {
		if (++sess->q_count > sess->q_max)
			sess->q_max = sess->q_count;

		sess->q[sess->q_w] = cw_object_dup(event);

		if (sess->q_w == sess->q_r)
			pthread_cond_signal(&sess->activity);

		sess->q_w = q_w_next;
	} else
		sess->q_overflow++;

	pthread_cleanup_pop(1);
}


static const char *manager_listener_object_name(struct cw_object *obj)
{
	struct manager_listener *it = container_of(obj, struct manager_listener, obj);
	return it->name;
}

static int manager_listener_object_cmp(struct cw_object *a, struct cw_object *b)
{
	struct manager_listener *item_a = container_of(a, struct manager_listener, obj);
	struct manager_listener *item_b = container_of(b, struct manager_listener, obj);

	return strcmp(item_a->name, item_b->name);
}

static int manager_listener_object_match(struct cw_object *obj, const void *pattern)
{
	struct manager_listener *item = container_of(obj, struct manager_listener, obj);
	return strcmp(item->name, pattern);
}

const struct cw_object_isa cw_object_isa_listener = {
	.name = manager_listener_object_name,
};

struct cw_registry manager_listener_registry = {
	.name = "Manager Listener",
	.lock = CW_MUTEX_INIT_VALUE,
	.cmp = manager_listener_object_cmp,
	.match = manager_listener_object_match,
};


static const char *manager_session_object_name(struct cw_object *obj)
{
	struct mansession *it = container_of(obj, struct mansession, obj);
	return it->name;
}

static int manager_session_object_cmp(struct cw_object *a, struct cw_object *b)
{
	struct mansession *item_a = container_of(a, struct mansession, obj);
	struct mansession *item_b = container_of(b, struct mansession, obj);

	return strcmp(item_a->name,  item_b->name);
}

static int manager_session_object_match(struct cw_object *obj, const void *pattern)
{
	struct mansession *item = container_of(obj, struct mansession, obj);
	return !strcmp(item->name, pattern);
}

const struct cw_object_isa cw_object_isa_session = {
	.name = manager_session_object_name,
};

struct cw_registry manager_session_registry = {
	.name = "Manager Session",
	.lock = CW_MUTEX_INIT_VALUE,
	.cmp = manager_session_object_cmp,
	.match = manager_session_object_match,
};


static const char *manager_action_object_name(struct cw_object *obj)
{
	struct manager_action *it = container_of(obj, struct manager_action, obj);
	return it->action;
}

static int manager_action_object_cmp(struct cw_object *a, struct cw_object *b)
{
	struct manager_action *item_a = container_of(a, struct manager_action, obj);
	struct manager_action *item_b = container_of(b, struct manager_action, obj);

	return strcasecmp(item_a->action, item_b->action);
}

static int manager_action_object_match(struct cw_object *obj, const void *pattern)
{
	struct manager_action *item = container_of(obj, struct manager_action, obj);
	return (!strcasecmp(item->action, pattern));
}

const struct cw_object_isa cw_object_isa_manager_action = {
	.name = manager_action_object_name,
};

struct cw_registry manager_action_registry = {
	.name = "Manager Action",
	.lock = CW_MUTEX_INIT_VALUE,
	.cmp = manager_action_object_cmp,
	.match = manager_action_object_match,
};


static const char showmancmd_help[] =
"Usage: show manager command <actionname>\n"
"	Shows the detailed description for a specific CallWeaver manager interface command.\n";


struct complete_show_manact_args {
	int fd;
	char *word;
	int word_len;
};

static int complete_show_manact_one(struct cw_registry_entry *entry, void *data)
{
	struct manager_action *it = container_of(entry->obj, struct manager_action, obj);
	struct complete_show_manact_args *args = data;

	if (!strncasecmp(args->word, it->action, args->word_len))
		cw_cli(args->fd, "%s\n", it->action);

	return 0;
}

static void complete_show_manact(int fd, char *argv[], int lastarg, int lastarg_len)
{
	struct complete_show_manact_args args = {
		.fd = fd,
		.word = argv[lastarg],
		.word_len = lastarg_len,
	};

	cw_registry_iterate(&manager_action_registry, complete_show_manact_one, &args);
}


static int handle_show_manact(int fd, int argc, char *argv[])
{
	struct cw_object *it;
	struct manager_action *act;

	if (argc < 4)
		return RESULT_SHOWUSAGE;

	if (!(it = cw_registry_find(&manager_action_registry, 0, 0, argv[3]))) {
		cw_cli(fd, "No manager action by that name registered.\n");
		return RESULT_FAILURE;
	}
	act = container_of(it, struct manager_action, obj);

	/* FIXME: Tidy up this output and make it more like function output */
	cw_cli(fd, "Action: %s\nSynopsis: %s\nPrivilege: ", act->action, act->synopsis);
	authority_to_str(fd, act->authority);
	cw_cli(fd, "\n%s\n", (act->description ? act->description : ""));

	cw_object_put(act);
	return RESULT_SUCCESS;
}


static const char showmancmds_help[] =
"Usage: show manager commands\n"
"	Prints a listing of all the available CallWeaver manager interface commands.\n";


static void complete_show_manacts(int fd, char *argv[], int lastarg, int lastarg_len)
{
	if (lastarg == 3) {
		if (!strncasecmp(argv[3], "like", lastarg_len))
			cw_cli(fd, "like\n");
		if (!strncasecmp(argv[3], "describing", lastarg_len))
			cw_cli(fd, "describing\n");
	}
}


struct manacts_print_args {
	int fd;
	int like, describing, matches;
	int argc;
	char **argv;
};

#define MANACTS_FORMAT_A	"  %-15.15s  "
#define MANACTS_FORMAT_B	"%-15.15s"
#define MANACTS_FORMAT_C	"  %s\n"

static int manacts_print(struct cw_registry_entry *entry, void *data)
{
	struct manager_action *it = container_of(entry->obj, struct manager_action, obj);
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
		cw_cli(args->fd, MANACTS_FORMAT_A, it->action);
		n = authority_to_str(args->fd, it->authority);
		if (n < 15)
			cw_cli(args->fd, "%.*s", 15 - n, "               ");
		cw_cli(args->fd, MANACTS_FORMAT_C, it->synopsis);
	}

	return 0;
}

static int handle_show_manacts(int fd, int argc, char *argv[])
{
	struct manacts_print_args args = {
		.fd = fd,
		.matches = 0,
		.argc = argc,
		.argv = argv,
	};

	if ((argc == 5) && (!strcmp(argv[3], "like")))
		args.like = 1;
	else if ((argc > 4) && (!strcmp(argv[3], "describing")))
		args.describing = 1;

	cw_cli(fd, "    -= %s Manager Actions =-\n"
		MANACTS_FORMAT_A MANACTS_FORMAT_B MANACTS_FORMAT_C
		MANACTS_FORMAT_A MANACTS_FORMAT_B MANACTS_FORMAT_C,
		(args.like || args.describing ? "Matching" : "Registered"),
		"Action", "Privilege", "Synopsis",
		"------", "---------", "--------");

	cw_registry_iterate(&manager_action_registry, manacts_print, &args);

	cw_cli(fd, "    -= %d Actions %s =-\n", args.matches, (args.like || args.describing ? "Matching" : "Registered"));
	return RESULT_SUCCESS;
}


static const char showlistener_help[] =
"Usage: show manager listen\n"
"	Prints a listing of the sockets the manager is listening on.\n";


struct listener_print_args {
	int fd;
};

#define MANLISTEN_FORMAT "%-7s %s\n"

static int listener_print(struct cw_registry_entry *entry, void *data)
{
	struct manager_listener *it = container_of(entry->obj, struct manager_listener, obj);
	struct listener_print_args *args = data;

	cw_cli(args->fd, MANLISTEN_FORMAT, (!pthread_equal(it->tid, CW_PTHREADT_NULL) && it->sock >= 0 ? "LISTEN" : "DOWN"), it->name);
	return 0;
}

static int handle_show_listener(int fd, int argc, char *argv[])
{
	struct listener_print_args args = {
		.fd = fd,
	};

	cw_cli(fd, MANLISTEN_FORMAT MANLISTEN_FORMAT,
		"State", "Address",
		"-----", "-------");

	cw_registry_iterate(&manager_listener_registry, listener_print, &args);

	return RESULT_SUCCESS;
}


static const char showmanconn_help[] =
"Usage: show manager connected\n"
"	Prints a listing of the users that are currently connected to the\n"
"CallWeaver manager interface.\n";


struct mansess_print_args {
	int fd;
};

#define MANSESS_FORMAT1	"%-40s %-15s %-6s %-9s %-8s\n"
#define MANSESS_FORMAT2	"%-40s %-15s %6u %9u %8u\n"

static int mansess_print(struct cw_registry_entry *entry, void *data)
{
	struct mansession *it = container_of(entry->obj, struct mansession, obj);
	struct mansess_print_args *args = data;

	cw_cli(args->fd, MANSESS_FORMAT2, it->name, it->username, it->q_count, it->q_max, it->q_overflow);
	return 0;
}

static int handle_show_mansess(int fd, int argc, char *argv[])
{
	struct mansess_print_args args = {
		.fd = fd,
	};

	cw_cli(fd, MANSESS_FORMAT1 MANSESS_FORMAT1,
		"Address", "Username", "Queued", "Max Queue", "Overflow",
		"--------", "-------", "------", "---------", "--------");

	cw_registry_iterate(&manager_session_registry, mansess_print, &args);

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

	pthread_mutex_destroy(&sess->lock);
	pthread_cond_destroy(&sess->ack);
	pthread_cond_destroy(&sess->activity);
	free(sess->q);
	free(sess);
}


char *astman_get_header(struct message *m, char *key)
{
	int x;

	for (x = 0;  x < m->hdrcount;  x++) {
		if (!strcasecmp(key, m->header[x].key))
			return m->header[x].val;
	}
	return NULL;
}

void astman_get_variables(struct cw_registry *vars, struct message *m)
{
	char *name, *val;
	int x;

	for (x = 0;  x < m->hdrcount;  x++) {
		if (!strcasecmp("Variable", m->header[x].key)) {
			name = val = cw_strdupa(m->header[x].val);
			strsep(&val, "=");
			if (!val || cw_strlen_zero(name))
				continue;
			cw_var_assign(vars, name, val);
		}
	}
}


void astman_send_response(struct mansession *s, struct message *m, const char *resp, const char *msg, int complete)
{
	cw_cli(s->fd, "Response: %s\r\n", resp);

	if (!cw_strlen_zero(m->actionid))
		cw_cli(s->fd, "ActionID: %s\r\n", m->actionid);

	if (msg)
		cw_cli(s->fd, "Message: %s\r\n", msg);

	if (complete)
		cw_cli(s->fd, "\r\n");
}


void astman_send_error(struct mansession *s, struct message *m, const char *error)
{
	astman_send_response(s, m, "Error", error, 1);
}


void astman_send_ack(struct mansession *s, struct message *m, const char *msg)
{
	astman_send_response(s, m, "Success", msg, 1);
}


static int authenticate(struct mansession *sess, struct message *m)
{
	struct cw_config *cfg;
	char *cat;
	char *user = astman_get_header(m, "Username");
	char *pass = astman_get_header(m, "Secret");
	char *authtype = astman_get_header(m, "AuthType");
	char *key = astman_get_header(m, "Key");
	char *events = astman_get_header(m, "Events");
	int ret = -1;
	
	if (!user) {
		astman_send_error(sess, m, "Required header \"Username\" missing");
		return -1;
	}

	cfg = cw_config_load("manager.conf");
	if (!cfg)
		return -1;

	for (cat = cw_category_browse(cfg, NULL); cat; cat = cw_category_browse(cfg, cat)) {
		if (strcasecmp(cat, "general")) {
			/* This is a user */
			if (!strcasecmp(cat, user)) {
				struct cw_variable *v;
				struct cw_ha *ha = NULL;
				char *password = NULL;

				for (v = cw_variable_browse(cfg, cat); v; v = v->next) {
					if (!strcasecmp(v->name, "secret")) {
						password = v->value;
					} else if (!strcasecmp(v->name, "permit") || !strcasecmp(v->name, "deny")) {
						ha = cw_append_ha(v->name, v->value, ha);
					} else if (!strcasecmp(v->name, "writetimeout")) {
						cw_log(CW_LOG_WARNING, "writetimeout is deprecated - remove it from manager.conf\n");
					}
				}

				ret = 0;

				if (ha) {
					if (sess->u.sa.sa_family != AF_INET || !cw_apply_ha(ha, &sess->u.sin)) {
						cw_log(CW_LOG_NOTICE, "%s failed to pass IP ACL as '%s'\n", sess->name, user);
						ret = -1;
					}

					cw_free_ha(ha);
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
		ret = 0;
	} else
		cw_log(CW_LOG_ERROR, "%s failed to authenticate as '%s'\n", sess->name, user);

	cw_config_destroy(cfg);
	return ret;
}


static const char mandescr_ping[] =
"Description: A 'Ping' action will ellicit a 'Pong' response.  Used to keep the\n"
"  manager connection open.\n"
"Variables: NONE\n";

static int action_ping(struct mansession *s, struct message *m)
{
	astman_send_response(s, m, "Pong", NULL, 1);
	return 0;
}


static const char mandescr_version[] =
"Description: Returns the version, hostname and pid of the running CallWeaver\n";

static int action_version(struct mansession *s, struct message *m)
{
	astman_send_response(s, m, "Version", NULL, 0);
	cw_cli(s->fd, "Version: %s\r\nHostname: %s\r\nPid: %u\r\n\r\n", cw_version_string, hostname, (unsigned int)getpid());
	return 0;
}


static const char mandescr_listcommands[] =
"Description: Returns the action name and synopsis for every\n"
"  action that is available to the user\n"
"Variables: NONE\n";

struct listcommands_print_args {
	struct mansession *s;
};

static int listcommands_print(struct cw_registry_entry *entry, void *data)
{
	struct manager_action *it = container_of(entry->obj, struct manager_action, obj);
	struct listcommands_print_args *args = data;

	cw_cli(args->s->fd, "%s: %s (Priv: ", it->action, it->synopsis);
	authority_to_str(args->s->fd, it->authority);
	cw_cli(args->s->fd, ")\r\n");

	return 0;
}

static int action_listcommands(struct mansession *s, struct message *m)
{
	struct listcommands_print_args args = {
		.s = s,
	};

	astman_send_response(s, m, "Success", NULL, 0);
	cw_registry_iterate(&manager_action_registry, listcommands_print, &args);
	cw_cli(s->fd, "\r\n");

	return RESULT_SUCCESS;
}

static const char mandescr_events[] =
"Description: Enable/Disable sending of events to this manager\n"
"  client.\n"
"Variables:\n"
"	EventMask: 'on' if all events should be sent,\n"
"		'off' if no events should be sent,\n"
"		'system,call,log' to select which flags events should have to be sent.\n";

static int action_events(struct mansession *sess, struct message *m)
{
	char *mask = astman_get_header(m, "EventMask");

	if (mask) {
		int eventmask = manager_str_to_eventmask(mask);

		pthread_mutex_lock(&sess->lock);

		sess->send_events = eventmask;

		pthread_mutex_unlock(&sess->lock);

		astman_send_response(sess, m, (eventmask ? "Events On" : "Events Off"), NULL, 1);
	} else
		astman_send_error(sess, m, "Required header \"Mask\" missing");

	return 0;
}

static const char mandescr_logoff[] =
"Description: Logoff this manager session\n"
"Variables: NONE\n";

static int action_logoff(struct mansession *s, struct message *m)
{
	astman_send_response(s, m, "Goodbye", "Thanks for all the fish.", 1);
	return -1;
}

static const char mandescr_hangup[] =
"Description: Hangup a channel\n"
"Variables: \n"
"	Channel: The channel name to be hungup\n";

static int action_hangup(struct mansession *s, struct message *m)
{
	struct cw_channel *c = NULL;
	char *name = astman_get_header(m, "Channel");

	if (cw_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	c = cw_get_channel_by_name_locked(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	cw_softhangup(c, CW_SOFTHANGUP_EXPLICIT);
	cw_mutex_unlock(&c->lock);
	astman_send_ack(s, m, "Channel Hungup");
	return 0;
}

static const char mandescr_setvar[] =
"Description: Set a local channel variable.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel to set variable for\n"
"	*Variable: Variable name\n"
"	*Value: Value\n";

static int action_setvar(struct mansession *s, struct message *m)
{
        struct cw_channel *c = NULL;
        char *name = astman_get_header(m, "Channel");
        char *varname = astman_get_header(m, "Variable");
        char *varval = astman_get_header(m, "Value");
	
	if (cw_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	if (cw_strlen_zero(varname)) {
		astman_send_error(s, m, "No variable specified");
		return 0;
	}

	c = cw_get_channel_by_name_locked(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	
	pbx_builtin_setvar_helper(c,varname,varval);
	  
	cw_mutex_unlock(&c->lock);
	astman_send_ack(s, m, "Variable Set");
	return 0;
}

static const char mandescr_getvar[] =
"Description: Get the value of a local channel variable.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel to read variable from\n"
"	*Variable: Variable name\n"
"	ActionID: Optional Action id for message matching.\n";

static int action_getvar(struct mansession *s, struct message *m)
{
        struct cw_channel *chan = NULL;
        char *name = astman_get_header(m, "Channel");
        char *varname = astman_get_header(m, "Variable");
	struct cw_var_t *var;
	unsigned int hash;

	if (cw_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	if (cw_strlen_zero(varname)) {
		astman_send_error(s, m, "No variable specified");
		return 0;
	}

	hash = cw_hash_var_name(name);

	chan = cw_get_channel_by_name_locked(name);
	if (!chan) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	
	var = pbx_builtin_getvar_helper(chan, hash, varname);
	cw_mutex_unlock(&chan->lock);

	astman_send_response(s, m, "Success", NULL, 0);
	cw_cli(s->fd, "Variable: %s\r\nValue: %s\r\n\r\n", varname, (var ? var->value : ""));

	if (var)
		cw_object_put(var);

	return 0;
}

/*! \brief  action_status: Manager "status" command to show channels */
/* Needs documentation... */
static int action_status(struct mansession *s, struct message *m)
{
  	char *name = astman_get_header(m,"Channel");
	char idText[256] = "";
	struct cw_channel *c;
	char bridge[256];
	struct timeval now = cw_tvnow();
	long elapsed_seconds=0;
	long billable_seconds=0;
	int all = cw_strlen_zero(name); /* set if we want all channels */

	astman_send_ack(s, m, "Channel status will follow");
	if (!cw_strlen_zero(m->actionid))
		snprintf(idText, 256, "ActionID: %s\r\n", m->actionid);
	if (all) {
		c = cw_channel_walk_locked(NULL);
	} else {
		c = cw_get_channel_by_name_locked(name);
		if (!c) {
			astman_send_error(s, m, "No such channel");
			return 0;
		}
	}
	/* if we look by name, we break after the first iteration */
	while (c) {
		if (c->_bridge)
			snprintf(bridge, sizeof(bridge), "Link: %s\r\n", c->_bridge->name);
		else
			bridge[0] = '\0';
		if (c->pbx) {
			if (c->cdr)
				elapsed_seconds = now.tv_sec - c->cdr->start.tv_sec;
				if (c->cdr->answer.tv_sec > 0)
					billable_seconds = now.tv_sec - c->cdr->answer.tv_sec;
			cw_cli(s->fd,
        			"Event: Status\r\n"
        			"Privilege: Call\r\n"
        			"Channel: %s\r\n"
        			"CallerID: %s\r\n"
        			"CallerIDName: %s\r\n"
        			"Account: %s\r\n"
        			"State: %s\r\n"
        			"Context: %s\r\n"
        			"Extension: %s\r\n"
        			"Priority: %d\r\n"
        			"Seconds: %ld\r\n"
				"BillableSeconds: %ld\r\n"
        			"%s"
        			"Uniqueid: %s\r\n"
        			"%s"
        			"\r\n",
        			c->name, 
        			c->cid.cid_num ? c->cid.cid_num : "<unknown>", 
        			c->cid.cid_name ? c->cid.cid_name : "<unknown>", 
        			c->accountcode,
        			cw_state2str(c->_state), c->context,
        			c->exten, c->priority, (long)elapsed_seconds, (long)billable_seconds,
				bridge, c->uniqueid, idText);
		} else {
			cw_cli(s->fd,
        			"Event: Status\r\n"
        			"Privilege: Call\r\n"
        			"Channel: %s\r\n"
        			"CallerID: %s\r\n"
        			"CallerIDName: %s\r\n"
        			"Account: %s\r\n"
        			"State: %s\r\n"
        			"%s"
        			"Uniqueid: %s\r\n"
        			"%s"
        			"\r\n",
        			c->name, 
        			c->cid.cid_num ? c->cid.cid_num : "<unknown>", 
        			c->cid.cid_name ? c->cid.cid_name : "<unknown>", 
        			c->accountcode,
        			cw_state2str(c->_state), bridge, c->uniqueid, idText);
		}
		cw_mutex_unlock(&c->lock);
		if (!all)
			break;
		c = cw_channel_walk_locked(c);
	}
	cw_cli(s->fd,
         	 "Event: StatusComplete\r\n"
        	 "%s"
        	 "\r\n",idText);
	return 0;
}

static const char mandescr_redirect[] =
"Description: Redirect (transfer) a call.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel to redirect\n"
"	ExtraChannel: Second call leg to transfer (optional)\n"
"	*Exten: Extension to transfer to\n"
"	*Context: Context to transfer to\n"
"	*Priority: Priority to transfer to\n"
"	ActionID: Optional Action id for message matching.\n";

/*! \brief  action_redirect: The redirect manager command */
static int action_redirect(struct mansession *s, struct message *m)
{
	char *name = astman_get_header(m, "Channel");
	char *name2 = astman_get_header(m, "ExtraChannel");
	char *exten = astman_get_header(m, "Exten");
	char *context = astman_get_header(m, "Context");
	char *priority = astman_get_header(m, "Priority");
	struct cw_channel *chan, *chan2 = NULL;
	int res;

	if (cw_strlen_zero(name)) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}
	chan = cw_get_channel_by_name_locked(name);
	if (!chan) {
		char buf[BUFSIZ];

		snprintf(buf, sizeof(buf), "Channel does not exist: %s", name);
		astman_send_error(s, m, buf);
		return 0;
	}
	if (!cw_strlen_zero(name2))
		chan2 = cw_get_channel_by_name_locked(name2);
	res = cw_async_goto(chan, context, exten, priority);
	if (!res) {
		if (!cw_strlen_zero(name2)) {
			if (chan2)
				res = cw_async_goto(chan2, context, exten, priority);
			else
				res = -1;
			if (!res)
				astman_send_ack(s, m, "Dual Redirect successful");
			else
				astman_send_error(s, m, "Secondary redirect failed");
		} else
			astman_send_ack(s, m, "Redirect successful");
	} else
		astman_send_error(s, m, "Redirect failed");
	if (chan)
		cw_mutex_unlock(&chan->lock);
	if (chan2)
		cw_mutex_unlock(&chan2->lock);
	return 0;
}

static const char mandescr_command[] =
"Description: Run a CLI command.\n"
"Variables: (Names marked with * are required)\n"
"	*Command: CallWeaver CLI command to run\n"
"	ActionID: Optional Action id for message matching.\n";

/*! \brief  action_command: Manager command "command" - execute CLI command */
static int action_command(struct mansession *s, struct message *m)
{
	char *cmd = astman_get_header(m, "Command");

	if (cmd) {
		astman_send_response(s, m, "Follows", NULL, 0);

		cw_cli_command(s->fd, cmd);

		cw_cli(s->fd, "--END COMMAND--\r\n\r\n");
	}

	return 0;
}


static const char mandescr_complete[] =
"Description: Return possible completions for a CallWeaver CLI command.\n"
"	*Command: CallWeaver CLI command to complete\n"
"	ActionID: Optional Action id for message matching.\n";

static int action_complete(struct mansession *s, struct message *m)
{
	char *cmd = astman_get_header(m, "Command");

	if (cmd) {
		astman_send_response(s, m, "Completion", NULL, 0);

		cw_cli_generator(s->fd, cmd);

		cw_cli(s->fd, "--END COMMAND--\r\n\r\n");
	} else {
		astman_send_error(s, m, NULL);
	}

	return 0;
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
	if (!res) {
		manager_event(EVENT_FLAG_CALL,
			"OriginateSuccess",
			"%s"
			"Channel: %s/%s\r\n"
			"Context: %s\r\n"
			"Exten: %s\r\n"
			"Reason: %d\r\n"
			"Uniqueid: %s\r\n",
			in->idtext, in->tech, in->data, in->context, in->exten, reason, chan ? chan->uniqueid : "<null>");
	} else {
		manager_event(EVENT_FLAG_CALL,
			"OriginateFailure",
			"%s"
			"Channel: %s/%s\r\n"
			"Context: %s\r\n"
			"Exten: %s\r\n"
			"Reason: %d\r\n"
			"Uniqueid: %s\r\n",
			in->idtext, in->tech, in->data, in->context, in->exten, reason, chan ? chan->uniqueid : "<null>");
	}
	/* Locked by cw_pbx_outgoing_exten or cw_pbx_outgoing_app */
	if (chan)
		cw_mutex_unlock(&chan->lock);
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

static int action_originate(struct mansession *s, struct message *m)
{
	struct fast_originate_helper *fast;
	char *name = astman_get_header(m, "Channel");
	char *exten = astman_get_header(m, "Exten");
	char *context = astman_get_header(m, "Context");
	char *priority = astman_get_header(m, "Priority");
	char *timeout = astman_get_header(m, "Timeout");
	char *callerid = astman_get_header(m, "CallerID");
	char *account = astman_get_header(m, "Account");
	char *app = astman_get_header(m, "Application");
	char *appdata = astman_get_header(m, "Data");
	char *async = astman_get_header(m, "Async");
	char *tech, *data;
	char *l=NULL, *n=NULL;
	int pi = 0;
	int res;
	int to = 30000;
	int reason = 0;
	char tmp[256];
	char tmp2[256];
	
	pthread_t th;

	if (!name) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}
	if (!cw_strlen_zero(priority) && (sscanf(priority, "%d", &pi) != 1)) {
		astman_send_error(s, m, "Invalid priority");
		return 0;
	}
	if (!cw_strlen_zero(timeout) && (sscanf(timeout, "%d", &to) != 1)) {
		astman_send_error(s, m, "Invalid timeout");
		return 0;
	}
	cw_copy_string(tmp, name, sizeof(tmp));
	tech = tmp;
	data = strchr(tmp, '/');
	if (!data) {
		astman_send_error(s, m, "Invalid channel");
		return 0;
	}
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
		cw_var_registry_init(&fast->vars, 1024);
		astman_get_variables(&fast->vars, m);
		if (account) {
			/* FIXME: this is rubbish, surely? */
			cw_var_assign(&fast->vars, "CDR(accountcode|r)", account);
		}

		if (cw_true(async)) {
			memset(fast, 0, sizeof(struct fast_originate_helper));
			if (!cw_strlen_zero(m->actionid))
				snprintf(fast->idtext, sizeof(fast->idtext), "ActionID: %s\r\n", m->actionid);
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
				astman_send_error(s, m, "Originate with 'Exten' requires 'Context' and 'Priority'");
				res = -1;
			}
			cw_registry_destroy(&fast->vars);
			free(fast);
		}
	} else
		res = -1;

	if (!res)
		astman_send_ack(s, m, "Originate successfully queued");
	else
		astman_send_error(s, m, "Originate failed");
	return 0;
}

static const char mandescr_mailboxstatus[] =
"Description: Checks a voicemail account for status.\n"
"Variables: (Names marked with * are required)\n"
"	*Mailbox: Full mailbox ID <mailbox>@<vm-context>\n"
"	ActionID: Optional ActionID for message matching.\n"
"Returns number of messages.\n"
"	Message: Mailbox Status\n"
"	Mailbox: <mailboxid>\n"
"	Waiting: <count>\n"
"\n";
static int action_mailboxstatus(struct mansession *s, struct message *m)
{
	char *mailbox = astman_get_header(m, "Mailbox");
	int ret;

	if (cw_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified");
		return 0;
	}
	ret = cw_app_has_voicemail(mailbox, NULL);
	astman_send_response(s, m, "Success", "Mailbox Status", 0);
	cw_cli(s->fd, "Mailbox: %s\r\nWaiting: %d\r\n\r\n", mailbox, ret);
	return 0;
}

static const char mandescr_mailboxcount[] =
"Description: Checks a voicemail account for new messages.\n"
"Variables: (Names marked with * are required)\n"
"	*Mailbox: Full mailbox ID <mailbox>@<vm-context>\n"
"	ActionID: Optional ActionID for message matching.\n"
"Returns number of new and old messages.\n"
"	Message: Mailbox Message Count\n"
"	Mailbox: <mailboxid>\n"
"	NewMessages: <count>\n"
"	OldMessages: <count>\n"
"\n";
static int action_mailboxcount(struct mansession *s, struct message *m)
{
	char *mailbox = astman_get_header(m, "Mailbox");
	int newmsgs = 0, oldmsgs = 0;

	if (cw_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified");
		return 0;
	}
	cw_app_messagecount(mailbox, &newmsgs, &oldmsgs);
	astman_send_response(s, m, "Success", "Mailbox Message Count", 0);
	cw_cli(s->fd, "Mailbox: %s\r\nNewMessages: %d\r\nOldMessages: %d\r\n\r\n", mailbox, newmsgs, oldmsgs);
	return 0;
}

static const char mandescr_extensionstate[] =
"Description: Report the extension state for given extension.\n"
"  If the extension has a hint, will use devicestate to check\n"
"  the status of the device connected to the extension.\n"
"Variables: (Names marked with * are required)\n"
"	*Exten: Extension to check state on\n"
"	*Context: Context for extension\n"
"	ActionId: Optional ID for this transaction\n"
"Will return an \"Extension Status\" message.\n"
"The response will include the hint for the extension and the status.\n";

static int action_extensionstate(struct mansession *s, struct message *m)
{
	char *exten = astman_get_header(m, "Exten");
	char *context = astman_get_header(m, "Context");
	char hint[256] = "";
	int status;
	
	if (cw_strlen_zero(exten)) {
		astman_send_error(s, m, "Extension not specified");
		return 0;
	}
	if (cw_strlen_zero(context))
		context = "default";
	status = cw_extension_state(NULL, context, exten);
	cw_get_hint(hint, sizeof(hint) - 1, NULL, 0, NULL, context, exten);

	astman_send_response(s, m, "Success", "Extension Status", 0);
	cw_cli(s->fd, "Exten: %s\r\nContext: %s\r\nHint: %s\r\nStatus: %d\r\n\r\n", exten, context, hint, status);

	return 0;
}

static const char mandescr_timeout[] =
"Description: Hangup a channel after a certain time.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel name to hangup\n"
"	*Timeout: Maximum duration of the call (sec)\n"
"Acknowledges set time with 'Timeout Set' message\n";

static int action_timeout(struct mansession *s, struct message *m)
{
	struct cw_channel *c = NULL;
	char *name = astman_get_header(m, "Channel");
	int timeout = atoi(astman_get_header(m, "Timeout"));
	if (cw_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	if (!timeout) {
		astman_send_error(s, m, "No timeout specified");
		return 0;
	}
	c = cw_get_channel_by_name_locked(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	cw_channel_setwhentohangup(c, timeout);
	cw_mutex_unlock(&c->lock);
	astman_send_ack(s, m, "Timeout Set");
	return 0;
}

static int process_message(struct mansession *s, struct message *m)
{
	int ret = 0;

	if (cw_strlen_zero(m->action))
		astman_send_error(s, m, "Missing action in request");
	else if (s->authenticated) {
		struct cw_object *it;

		if ((it = cw_registry_find(&manager_action_registry, 0, 0, m->action))) {
			struct manager_action *act = container_of(it, struct manager_action, obj);
			if ((s->writeperm & act->authority) == act->authority)
				ret = act->func(s, m);
			else
				astman_send_error(s, m, "Permission denied");
			cw_object_put(act);
		} else
			astman_send_error(s, m, "Invalid/unknown command");
	} else if (!strcasecmp(m->action, "Challenge")) {
		char *authtype;

		if ((authtype = astman_get_header(m, "AuthType")) && !strcasecmp(authtype, "MD5")) {
			if (cw_strlen_zero(s->challenge))
				snprintf(s->challenge, sizeof(s->challenge), "%lu", cw_random());
			astman_send_response(s, m, "Success", NULL, 0);
			cw_cli(s->fd, "Challenge: %s\r\n\r\n", s->challenge);
		} else
			astman_send_error(s, m, "Must specify AuthType");
	} else if (!strcasecmp(m->action, "Login")) {
		if (authenticate(s, m)) {
			sleep(1);
			astman_send_error(s, m, "Authentication failed");
		} else {
			s->authenticated = 1;
			if (option_verbose > 3 && displayconnects)
				cw_verbose(VERBOSE_PREFIX_2 "Manager '%s' logged on from %s\n", s->username, s->name);
			cw_log(CW_LOG_EVENT, "Manager '%s' logged on from %s\n", s->username, s->name);
			astman_send_ack(s, m, "Authentication accepted");
		}
	} else if (!strcasecmp(m->action, "Logoff")) {
		astman_send_ack(s, m, "See ya");
		ret = -1;
	} else
		astman_send_error(s, m, "Authentication Required");

	return ret;
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
						m.action = m.actionid = NULL;
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
							case sizeof("Action")-1:
								if (!strcasecmp(m.header[m.hdrcount].key, "Action"))
									hval = &m.action;
								break;
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
			cw_log(CW_LOG_ERROR, "Manager session %s dropped due to oversize message\n", sess->name);
			break;
		}
	}

	return NULL;
}


int manager_session_ami(struct mansession *sess, const struct manager_event *event)
{
	/* "Event: ..." is necessary, "Privilege: ..." is not.
	 * The privilege header is sent solely because it was sent
	 * in the past and there might be AMI clients out there that
	 * depend on it being there.
	 */
	cw_cli(sess->fd, "Event: %s\r\nPrivilege: ", event->event);
	authority_to_str(sess->fd, event->category);
	cw_cli(sess->fd, "\r\n");
	return cw_write_all(sess->fd, event->data, event->len);
}


static void manager_session_cleanup(void *data)
{
	struct mansession *sess = data;

	if (sess->reg_entry)
		cw_registry_del(&manager_session_registry, sess->reg_entry);

	if (sess->authenticated) {
		if (sess->username[0]) {
			if (option_verbose > 3 && displayconnects)
				cw_verbose(VERBOSE_PREFIX_2 "Manager '%s' logged off from %s\n", sess->username, sess->name);
			cw_log(CW_LOG_EVENT, "Manager '%s' logged off from %s\n", sess->username, sess->name);
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
	int res;

	cw_write_all(sess->fd, MANAGER_AMI_HELLO, sizeof(MANAGER_AMI_HELLO) - 1);

	sess->reader_tid = CW_PTHREADT_NULL;

	pthread_cleanup_push(manager_session_cleanup, sess);

	sess->reg_entry = cw_registry_add(&manager_session_registry, 0, &sess->obj);

	/* If there is an fd already supplied we will read AMI requests from it */
	if (sess->fd >= 0) {
		setsockopt(sess->fd, SOL_TCP, TCP_NODELAY, &on, sizeof(on));
		setsockopt(sess->fd, SOL_TCP, TCP_CORK, &on, sizeof(on));

		if ((res = cw_pthread_create(&sess->reader_tid, &global_attr_default, manager_session_ami_read, sess))) {
			cw_log(CW_LOG_ERROR, "session reader thread creation failed: %s\n", strerror(res));
			return NULL;
		}
	}

	for (;;) {
		struct manager_event *event = NULL;

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &sess->lock);
		pthread_mutex_lock(&sess->lock);

		/* If there's no request message and no queued events
		 * we have to wait for activity.
		 */
		if (!sess->m && sess->q_r == sess->q_w) {
#ifdef TCP_CORK
			if (sess->fd >= 0)
				setsockopt(sess->fd, SOL_TCP, TCP_CORK, &off, sizeof(off));
#endif

			pthread_cond_wait(&sess->activity, &sess->lock);

#ifdef TCP_CORK
			if (sess->fd >= 0)
				setsockopt(sess->fd, SOL_TCP, TCP_CORK, &on, sizeof(on));
#endif
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

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (sess->m) {
			fcntl(sess->fd, F_SETFL, fcntl(sess->fd, F_GETFL, 0) | O_NONBLOCK);

			if (process_message(sess, sess->m))
				break;

			fcntl(sess->fd, F_SETFL, fcntl(sess->fd, F_GETFL, 0) & (~O_NONBLOCK));

			/* Remove the queued message and signal completion to the reader */
			pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &sess->lock);
			pthread_mutex_lock(&sess->lock);
			sess->m = NULL;
			pthread_cond_signal(&sess->ack);
			pthread_cleanup_pop(1);
		}

		if (event) {
			if ((res = sess->handler(sess, event)) < 0)
				cw_log(CW_LOG_WARNING, "Disconnecting manager session %s, handler gave: %s\n", sess->name, strerror(errno));
			cw_object_put(event);
			if (res < 0)
				break;
		}
	}

	pthread_cleanup_pop(1);
	return NULL;
}


struct mansession *manager_session_start(int (* const handler)(struct mansession *, const struct manager_event *), int fd, int family, void *addr, size_t addr_len, int readperm, int writeperm, int send_events)
{
	char buf[1];
	struct mansession *sess;
	int namelen;

	namelen = addr_to_str(family, addr, buf, sizeof(buf)) + 1;

	if ((sess = calloc(1, sizeof(struct mansession) + namelen)) == NULL) {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		return NULL;
	}

	if (!(sess->q = malloc(queuesize * sizeof(*sess->q)))) {
		free(sess);
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		return NULL;
	}
	sess->q_size = queuesize;

	addr_to_str(family, addr, sess->name, namelen);

	/* Only copy the address into the session if it is representable by
	 * a sockaddr_* type. The address is needed for ACL checks on connection
	 * so strictly we only need IPv4 addresses at the moment (that's all
	 * ACLs support)
	 */
	if (family == AF_INET || family == AF_INET6 || family == AF_LOCAL)
		memcpy(&sess->u, addr, addr_len);
	sess->u.sa.sa_family = family;

	sess->fd = fd;
	sess->readperm = readperm;
	if ((sess->writeperm = writeperm))
		sess->authenticated = 1;
	sess->send_events = send_events;
	sess->handler = handler;

	cw_object_init(sess, &cw_object_isa_session, NULL, 2);
	pthread_mutex_init(&sess->lock, NULL);
	pthread_cond_init(&sess->activity, NULL);
	pthread_cond_init(&sess->ack, NULL);
	sess->obj.release = mansession_release;

	if (cw_pthread_create(&sess->writer_tid, &global_attr_detached, manager_session, sess)) {
		cw_log(CW_LOG_ERROR, "Thread creation failed: %s\n", strerror(errno));
		cw_object_put(sess);
		cw_object_put(sess);
		return NULL;
	}

	return sess;
}


void manager_session_end(struct mansession *sess)
{
	pthread_cancel(sess->writer_tid);
	pthread_join(sess->writer_tid, NULL);
	cw_object_put(sess);
}


static void accept_thread_cleanup(void *data)
{
	struct manager_listener *listener = data;

	if (listener->sock >= 0) {
		close(listener->sock);
		listener->sock = -1;

		if (!strncmp(listener->name, "local:/", sizeof("local:/") - 1))
			unlink(listener->name + sizeof("local:") - 1);
	}
}

static void *accept_thread(void *data)
{
	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_un sun;
	} u;
	struct manager_listener *listener = data;
	struct mansession *sess;
	socklen_t salen;
	int fd;

	pthread_cleanup_push(accept_thread_cleanup, listener);

	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		salen = sizeof(u);
		fd = accept(listener->sock, &u.sa, &salen);

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (fd < 0) {
			if (errno == ENFILE || errno == EMFILE || errno == ENOBUFS || errno == ENOMEM) {
				cw_log(CW_LOG_ERROR, "Accept failed: %s\n", strerror(errno));
				sleep(1);
			}
			continue;
		}

		fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);

		if (u.sa.sa_family == AF_LOCAL) {
			/* Local sockets don't return a path in their sockaddr (there isn't
			 * one really). However, if the remote is local so is the address
			 * we were listening on and the listening path is in the listener's
			 * name. We'll use that as a meaningful connection source for the
			 * sake of the connection list command.
			 */
			strcpy(u.sun.sun_path, listener->name + sizeof("local:") - 1);
		}

		if ((sess = manager_session_start(listener->handler, fd, u.sa.sa_family, &u, salen, listener->readperm, listener->writeperm, listener->send_events)))
			cw_object_put(sess);
	}

	pthread_cleanup_pop(1);
	return NULL;
}


static const struct cw_object_isa cw_object_isa_manager_event;


struct manager_event_args {
	int ret;
	int category;
	struct manager_event *me;
	const char *event;
	const char *fmt;
	va_list ap;
};

static void manager_event_free(struct cw_object *obj)
{
	struct manager_event *it = container_of(obj, struct manager_event, obj);

	free(it);
}

static int make_event(struct manager_event_args *args)
{
	struct manager_event *event;
	va_list aq;
	int alloc = 256;
	int used, n;

	if ((args->me = malloc(sizeof(struct manager_event) + alloc))) {
again:
		used = 0;
		va_copy(aq, args->ap);
		n = vsnprintf(args->me->data + used, alloc, args->fmt, aq);
		va_end(aq);
		if (n >= 0)
			used += n;
		if (alloc - used > 2)
			strcpy(args->me->data + used, "\r\n");
		used += 2;

		if (used < alloc) {
			args->me->event = args->event;
			args->me->category = args->category;
			args->me->len = used;
			args->me->obj.release = manager_event_free;
			cw_object_init(args->me, &cw_object_isa_manager_event, NULL, 1);
			return 0;
		}

		alloc = used + 1;
		if ((event = realloc(args->me, sizeof(struct manager_event) + alloc))) {
			args->me = event;
			goto again;
		}

		free(args->me);
		args->me = NULL;
	}

	return -1;
}

static int manager_event_print(struct cw_registry_entry *entry, void *data)
{
	struct mansession *it = container_of(entry->obj, struct mansession, obj);
	struct manager_event_args *args = data;

	if (!args->ret && (it->readperm & args->category) == args->category && (it->send_events & args->category) == args->category) {
		if (args->me || !(args->ret = make_event(args)))
			append_event(it, args->me);
	}

	return args->ret;
}

int manager_event(int category, const char *event, const char *fmt, ...)
{
	struct manager_event_args args = {
		.ret = 0,
		.me = NULL,
		.category = category,
		.event = event,
		.fmt = fmt,
	};

	va_start(args.ap, fmt);

	cw_registry_iterate(&manager_session_registry, manager_event_print, &args);

	va_end(args.ap);

	if (args.me)
		cw_object_put(args.me);

	return 0;
}

static int manager_state_cb(char *context, char *exten, int state, void *data)
{
	/* Notify managers of change */
	manager_event(EVENT_FLAG_CALL, "ExtensionStatus", "Exten: %s\r\nContext: %s\r\nStatus: %d\r\n", exten, context, state);
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
		.func = action_logoff,
		.synopsis = "Logoff Manager",
		.description = mandescr_logoff,
	},
	{
		.action = "Hangup",
		.authority = EVENT_FLAG_CALL,
		.func = action_hangup,
		.synopsis = "Hangup Channel",
		.description = mandescr_hangup,
	},
	{
		.action = "Status",
		.authority = EVENT_FLAG_CALL,
		.func = action_status,
		.synopsis = "Lists channel status",
	},
	{
		.action = "Setvar",
		.authority = EVENT_FLAG_CALL,
		.func = action_setvar,
		.synopsis = "Set Channel Variable",
		.description = mandescr_setvar,
	},
	{
		.action = "Getvar",
		.authority = EVENT_FLAG_CALL,
		.func = action_getvar,
		.synopsis = "Gets a Channel Variable",
		.description = mandescr_getvar,
	},
	{
		.action = "Redirect",
		.authority = EVENT_FLAG_CALL,
		.func = action_redirect,
		.synopsis = "Redirect (transfer) a call",
		.description = mandescr_redirect,
	},
	{
		.action = "Originate",
		.authority = EVENT_FLAG_CALL,
		.func = action_originate,
		.synopsis = "Originate Call",
		.description = mandescr_originate,
	},
	{
		.action = "Command",
		.authority = EVENT_FLAG_COMMAND,
		.func = action_command,
		.synopsis = "Execute CallWeaver CLI Command",
		.description = mandescr_command,
	},
	{
		.action = "Complete",
		.authority = EVENT_FLAG_COMMAND,
		.func = action_complete,
		.synopsis = "Return possible completions for a CallWeaver CLI Command",
		.description = mandescr_complete,
	},
	{
		.action = "ExtensionState",
		.authority = EVENT_FLAG_CALL,
		.func = action_extensionstate,
		.synopsis = "Check Extension Status",
		.description = mandescr_extensionstate,
	},
	{
		.action = "AbsoluteTimeout",
		.authority = EVENT_FLAG_CALL,
		.func = action_timeout,
		.synopsis = "Set Absolute Timeout",
		.description = mandescr_timeout,
	},
	{
		.action = "MailboxStatus",
		.authority = EVENT_FLAG_CALL,
		.func = action_mailboxstatus,
		.synopsis = "Check Mailbox",
		.description = mandescr_mailboxstatus,
	},
	{
		.action = "MailboxCount",
		.authority = EVENT_FLAG_CALL,
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


static void listener_free(struct cw_object *obj)
{
	struct manager_listener *it = container_of(obj, struct manager_listener, obj);

	free(it);
}


static void manager_listen(const char *spec, int (* const handler)(struct mansession *, const struct manager_event *), int readperm, int writeperm, int send_events)
{
	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_un sun;
	} u;
	struct manager_listener *listener;
	socklen_t salen;
	int namelen;
	const int arg = 1;
	char buf[1];

	if (spec[0] == '/') {
		salen = sizeof(u.sun);
		u.sun.sun_family = AF_LOCAL;
		strncpy(u.sun.sun_path, spec, sizeof(u.sun.sun_path));
		unlink(spec);
	} else {
		char *port;
		int portno;

		salen = sizeof(u.sin);
		u.sin.sin_family = AF_INET;
		memset(&u.sin.sin_addr, 0, sizeof(u.sin.sin_addr));

		if (!(port = strrchr(spec, ':'))) {
			u.sin.sin_port = htons(DEFAULT_MANAGER_PORT);
		} else {
			if (sscanf(port + 1, "%d", &portno) != 1) {
				cw_log(CW_LOG_ERROR, "Invalid port number '%s' in '%s'\n", port + 1, spec);
				return;
			}
			u.sin.sin_port = htons(portno);
			*port = '\0';
		}

		if (!inet_aton(spec, &u.sin.sin_addr)) {
			cw_log(CW_LOG_ERROR, "Invalid address '%s' specified\n", spec);
			return;
		}
	}

	namelen = addr_to_str(u.sa.sa_family, &u, buf, sizeof(buf)) + 1;

	if (!(listener = malloc(sizeof(*listener) + namelen))) {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		return;
	}

	addr_to_str(u.sa.sa_family, &u, listener->name, namelen);

	cw_object_init(listener, &cw_object_isa_listener, NULL, 1);
	listener->obj.release = listener_free;
	listener->reg_entry = NULL;
	listener->tid = CW_PTHREADT_NULL;
	listener->sock = -1;
	listener->handler = handler;
	listener->readperm = readperm;
	listener->writeperm = writeperm;
	listener->send_events = send_events;

	if ((listener->sock = socket(u.sa.sa_family, SOCK_STREAM, 0)) < 0) {
		cw_log(CW_LOG_ERROR, "Unable to create socket: %s\n", strerror(errno));
		return;
	}

	fcntl(listener->sock, F_SETFD, fcntl(listener->sock, F_GETFD, 0) | FD_CLOEXEC);
	setsockopt(listener->sock, SOL_SOCKET, SO_REUSEADDR, &arg, sizeof(arg));

	if (bind(listener->sock, &u.sa, salen)) {
		cw_log(CW_LOG_ERROR, "Unable to bind to '%s': %s\n", listener->name, strerror(errno));
		return;
	}

	if (listen(listener->sock, 1024)) {
		cw_log(CW_LOG_ERROR, "Unable to listen on '%s': %s\n", listener->name, strerror(errno));
		return;
	}

	/* Local listener sockets that are not pre-authenticated are public */
	if (u.sa.sa_family == AF_LOCAL && !writeperm)
		chmod(u.sun.sun_path, 0666);

	if ((listener->reg_entry = cw_registry_add(&manager_listener_registry, 0, &listener->obj))) {
		cw_object_dup(listener);
		if (!cw_pthread_create(&listener->tid, &global_attr_default, accept_thread, listener)) {
			if (option_verbose)
				cw_verbose("CallWeaver Management interface listening on '%s'\n", listener->name);
		} else {
			cw_log(CW_LOG_ERROR, "Failed to start manager thread for %s: %s\n", listener->name, strerror(errno));
			cw_registry_del(&manager_listener_registry, listener->reg_entry);
			cw_object_put(listener);
		}
	}

	cw_object_put(listener);
}


static int listener_cancel(struct cw_registry_entry *entry, void *data)
{
	struct manager_listener *it = container_of(entry->obj, struct manager_listener, obj);

	if (!pthread_equal(it->tid, CW_PTHREADT_NULL))
		pthread_cancel(it->tid);
	return 0;
}


static int listener_join(struct cw_registry_entry *entry, void *data)
{
	struct manager_listener *it = container_of(entry->obj, struct manager_listener, obj);

	if (!pthread_equal(it->tid, CW_PTHREADT_NULL))
		pthread_join(it->tid, NULL);

	cw_registry_del(&manager_listener_registry, entry);
	cw_object_put(it);
	return 0;
}


int manager_reload(void)
{
	struct cw_config *cfg;
	struct cw_variable *v;
	char *bindaddr, *portno;
	uid_t uid = -1;
	gid_t gid = -1;

	/* Shut down any existing listeners */
	cw_registry_iterate(&manager_listener_registry, listener_cancel, NULL);
	cw_registry_iterate(&manager_listener_registry, listener_join, NULL);

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
				manager_listen(v->value, manager_session_ami, 0, 0, EVENT_FLAG_CALL | EVENT_FLAG_SYSTEM);
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
	manager_listen(cw_config_CW_SOCKET, manager_session_ami, EVENT_FLAG_LOG_ALL | EVENT_FLAG_PROGRESS, EVENT_FLAG_COMMAND, 0);

	if (!cw_strlen_zero(cw_config_CW_CTL_PERMISSIONS)) {
		mode_t p;
		sscanf(cw_config_CW_CTL_PERMISSIONS, "%o", (int *) &p);
		if ((chmod(cw_config_CW_SOCKET, p)) < 0)
			cw_log(CW_LOG_WARNING, "Unable to change file permissions of %s: %s\n", cw_config_CW_SOCKET, strerror(errno));
	}

	if (!cw_strlen_zero(cw_config_CW_CTL_OWNER)) {
		struct passwd *pw;
		if ((pw = getpwnam(cw_config_CW_CTL_OWNER)) == NULL)
			cw_log(CW_LOG_WARNING, "Unable to find uid of user %s\n", cw_config_CW_CTL_OWNER);
		else
			uid = pw->pw_uid;
	}

	if (!cw_strlen_zero(cw_config_CW_CTL_GROUP)) {
		struct group *grp;
		if ((grp = getgrnam(cw_config_CW_CTL_GROUP)) == NULL)
			cw_log(CW_LOG_WARNING, "Unable to find gid of group %s\n", cw_config_CW_CTL_GROUP);
		else
			gid = grp->gr_gid;
	}

	if (chown(cw_config_CW_SOCKET, uid, gid) < 0)
		cw_log(CW_LOG_WARNING, "Unable to change ownership of %s: %s\n", cw_config_CW_SOCKET, strerror(errno));

	/* DEPRECATED */
	if (bindaddr && portno) {
		char buf[256];

		snprintf(buf, sizeof(buf), "%s:%s", bindaddr, portno);
		manager_listen(buf, manager_session_ami, 0, 0, EVENT_FLAG_CALL | EVENT_FLAG_SYSTEM);
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
