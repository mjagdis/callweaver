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
	struct cw_variable *vars;
};


static int displayconnects;


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
	void *(*handler)(void *);
	int readperm, writeperm, send_events;
	char spec[0];
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

	[CW_EVENT_NUM_SYSTEM]	= STR_LEN("system"),
	[CW_EVENT_NUM_CALL]	= STR_LEN("call"),
	[CW_EVENT_NUM_COMMAND]	= STR_LEN("command"),
	[CW_EVENT_NUM_AGENT]	= STR_LEN("agent"),
	[CW_EVENT_NUM_USER]	= STR_LEN("user"),
#undef STR_LEN
};


#define MANAGER_AMI_HELLO	"CallWeaver Call Manager/1.0\r\n"


static int authority_to_str(int authority, char *res, int reslen)
{
	char *p;
	int i;

	p = res;

	if (reslen > 0) {
		*res = '\0';

		for (i = 0; i < arraysize(perms); i++) {
			if ((authority & (1 << i)) && perms[i].label) {
				if (p != res) {
					if (reslen > 1) {
						p[0] = ',';
						p[1] = '\0';
					}
					p++;
					reslen--;
				}
				if (reslen > 0)
					cw_copy_string(p, perms[i].label, reslen);
				p += perms[i].len;
				reslen -= perms[i].len;
			}
		}

		if (p == res) {
			cw_copy_string(res, "<none>", reslen);
			p += sizeof("<none>") - 1;
		}
	}

	return p - res;
}


int manager_str_to_eventmask(char *instr)
{
	int ret = 0;

	if (instr) {
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
	struct eventqent *eqe;

	if ((eqe = malloc(sizeof(struct eventqent)))) {
		eqe->next = NULL;
		eqe->event = cw_object_dup(event);

		pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &sess->lock);
		pthread_mutex_lock(&sess->lock);

		if (!sess->eventq)
			pthread_cond_signal(&sess->activity);

		*sess->eventq_tail = eqe;

		pthread_cleanup_pop(1);
	}
}


static const char *manager_listener_registry_obj_name(struct cw_object *obj)
{
	struct manager_listener *it = container_of(obj, struct manager_listener, obj);
	return it->spec;
}

static int manager_listener_registry_obj_cmp(struct cw_object *a, struct cw_object *b)
{
	struct manager_listener *item_a = container_of(a, struct manager_listener, obj);
	struct manager_listener *item_b = container_of(b, struct manager_listener, obj);

	return strcmp(item_a->spec, item_b->spec);
}

static int manager_listener_registry_obj_match(struct cw_object *obj, const void *pattern)
{
	struct manager_listener *item = container_of(obj, struct manager_listener, obj);
	return strcmp(item->spec, pattern);
}

struct cw_registry manager_listener_registry = {
	.name = "Manager Listener",
	.obj_name = manager_listener_registry_obj_name,
	.obj_cmp = manager_listener_registry_obj_cmp,
	.obj_match = manager_listener_registry_obj_match,
	.lock = CW_MUTEX_INIT_VALUE,
};


static const char *manager_session_registry_obj_name(struct cw_object *obj)
{
	// struct mansession *it = container_of(obj, struct mansession, obj);
	return "[manager session]";
}

static int manager_session_registry_obj_cmp(struct cw_object *a, struct cw_object *b)
{
	struct mansession *item_a = container_of(a, struct mansession, obj);
	struct mansession *item_b = container_of(b, struct mansession, obj);

	return item_a->fd - item_b->fd;
}

static int manager_session_registry_obj_match(struct cw_object *obj, const void *pattern)
{
	struct mansession *item = container_of(obj, struct mansession, obj);
	return item->fd == (int)pattern;
}

struct cw_registry manager_session_registry = {
	.name = "Manager Session",
	.obj_name = manager_session_registry_obj_name,
	.obj_cmp = manager_session_registry_obj_cmp,
	.obj_match = manager_session_registry_obj_match,
	.lock = CW_MUTEX_INIT_VALUE,
};


static const char *manager_action_registry_obj_name(struct cw_object *obj)
{
	struct manager_action *it = container_of(obj, struct manager_action, obj);
	return it->action;
}

static int manager_action_registry_obj_cmp(struct cw_object *a, struct cw_object *b)
{
	struct manager_action *item_a = container_of(a, struct manager_action, obj);
	struct manager_action *item_b = container_of(b, struct manager_action, obj);

	return strcasecmp(item_a->action, item_b->action);
}

static int manager_action_registry_obj_match(struct cw_object *obj, const void *pattern)
{
	struct manager_action *item = container_of(obj, struct manager_action, obj);
	return (!strcasecmp(item->action, pattern));
}

struct cw_registry manager_action_registry = {
	.name = "Manager Action",
	.obj_name = manager_action_registry_obj_name,
	.obj_cmp = manager_action_registry_obj_cmp,
	.obj_match = manager_action_registry_obj_match,
	.lock = CW_MUTEX_INIT_VALUE,
};


struct complete_show_manact_args {
	char *word;
	char *ret;
	int exact;
	int len;
	int which;
	int state;
};

static int complete_show_manact_one(struct cw_object *obj, void *data)
{
	struct manager_action *it = container_of(obj, struct manager_action, obj);
	struct complete_show_manact_args *args = data;

	if (((args->exact && !strncmp(args->word, it->action, args->len))
	|| (!args->exact && !strncasecmp(args->word, it->action, args->len)))
	&& ++args->which > args->state) {
		args->ret = strdup(it->action);
		return 1;
	}
	return 0;
}

static char *complete_show_manact(char *line, char *word, int pos, int state)
{
	struct complete_show_manact_args args = {
		.word = word,
		.len = strlen(word),
		.which = 0,
		.state = state,
		.ret = NULL,
	};

	/* Pass 1: Look for exact case */
	args.exact = 1;
	cw_registry_iterate(&manager_action_registry, complete_show_manact_one, &args);

	if (!args.ret) {
		/* Pass 2: Look for any case */
		args.exact = 0;
		cw_registry_iterate(&manager_action_registry, complete_show_manact_one, &args);
	}

	return args.ret;
}


static int handle_show_manact(int fd, int argc, char *argv[])
{
	char buf[80];
	struct cw_object *it;
	struct manager_action *act;

	if (argc < 4)
		return RESULT_SHOWUSAGE;

	if (!(it = cw_registry_find(&manager_action_registry, argv[3]))) {
		cw_cli(fd, "No manager action by that name registered.\n");
		return RESULT_FAILURE;
	}
	act = container_of(it, struct manager_action, obj);

	/* FIXME: Tidy up this output and make it more like function output */
	authority_to_str(act->authority, buf, sizeof(buf) - 1);
	cw_cli(fd, "Action: %s\nSynopsis: %s\nPrivilege: %s\n%s\n", act->action, act->synopsis, buf, (act->description ? act->description : ""));

	cw_object_put(act);
	return RESULT_SUCCESS;
}


static char *complete_show_manacts(char *line, char *word, int pos, int state)
{
	if (pos == 3) {
		if (cw_strlen_zero(word)) {
			switch (state) {
				case 0: return strdup("like");
				case 1: return strdup("describing");
				default: return NULL;
			}
		} else if (! strncasecmp(word, "like", strlen(word))) {
			if (state == 0)
				return strdup("like");
			return NULL;
		} else if (! strncasecmp(word, "describing", strlen(word))) {
			if (state == 0)
				return strdup("describing");
			return NULL;
		}
	}
	return NULL;
}


struct manacts_print_args {
	int fd;
	int like, describing, matches;
	int argc;
	char **argv;
};

#define MANACTS_FORMAT	"  %-15.15s  %-15.15s  %-55.55s\n"

static int manacts_print(struct cw_object *obj, void *data)
{
	char buf[80];
	struct manager_action *it = container_of(obj, struct manager_action, obj);
	struct manacts_print_args *args = data;
	int printapp = 1;

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
		authority_to_str(it->authority, buf, sizeof(buf) - 1);
		cw_cli(args->fd, MANACTS_FORMAT, it->action, buf, it->synopsis);
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

	cw_cli(fd, "    -= %s Manager Actions =-\n", (args.like || args.describing ? "Matching" : "Registered"));

	cw_cli(fd, MANACTS_FORMAT, "Action", "Privilege", "Synopsis");
	cw_cli(fd, MANACTS_FORMAT, "------", "---------", "--------");
	cw_registry_iterate(&manager_action_registry, manacts_print, &args);

	cw_cli(fd, "    -= %d Actions %s =-\n", args.matches, (args.like || args.describing ? "Matching" : "Registered"));
	return RESULT_SUCCESS;
}


struct listener_print_args {
	int fd;
};

static int listener_print(struct cw_object *obj, void *data)
{
	struct manager_listener *it = container_of(obj, struct manager_listener, obj);
	struct listener_print_args *args = data;

	cw_cli(args->fd, "%s %s\n", (!pthread_equal(it->tid, CW_PTHREADT_NULL) && it->sock >= 0 ? "LISTEN" : "DOWN   "), it->spec);
	return 0;
}

static int handle_show_listener(int fd, int argc, char *argv[])
{
	struct listener_print_args args = {
		.fd = fd,
	};

	cw_registry_iterate(&manager_listener_registry, listener_print, &args);

	return RESULT_SUCCESS;
}


struct mansess_print_args {
	int fd;
};

#define MANSESS_FORMAT	"  %-15.15s  %s\n"

static int mansess_print(struct cw_object *obj, void *data)
{
	struct mansession *it = container_of(obj, struct mansession, obj);
	struct mansess_print_args *args = data;

	cw_cli(args->fd, MANSESS_FORMAT, it->username, it->name);
	return 0;
}

static int handle_show_mansess(int fd, int argc, char *argv[])
{
	struct mansess_print_args args = {
		.fd = fd,
	};

	cw_cli(fd, MANSESS_FORMAT, "Username", "Address");
	cw_cli(fd, MANSESS_FORMAT, "--------", "-------");
	cw_registry_iterate(&manager_session_registry, mansess_print, &args);

	return RESULT_SUCCESS;
}


static char showmancmd_help[] = 
"Usage: show manager command <actionname>\n"
"	Shows the detailed description for a specific CallWeaver manager interface command.\n";

static char showmancmds_help[] = 
"Usage: show manager commands\n"
"	Prints a listing of all the available CallWeaver manager interface commands.\n";

static char showlistener_help[] =
"Usage: show manager listen\n"
"	Prints a listing of the sockets the manager is listening on.\n";

static char showmanconn_help[] = 
"Usage: show manager connected\n"
"	Prints a listing of the users that are currently connected to the\n"
"CallWeaver manager interface.\n";

static struct cw_clicmd show_mancmd_cli = {
	.cmda = { "show", "manager", "command", NULL },
	.handler = handle_show_manact,
	.generator = complete_show_manact,
	.summary = "Show a manager interface command",
	.usage = showmancmd_help,
};

static struct cw_clicmd show_mancmds_cli = {
	.cmda = { "show", "manager", "commands", NULL }, /* FIXME: should be actions */
	.handler = handle_show_manacts,
	.generator = complete_show_manacts,
	.summary = "List manager interface commands",
	.usage = showmancmds_help,
};

static struct cw_clicmd show_listener_cli = {
	.cmda = { "show", "manager", "listen", NULL },
	.handler = handle_show_listener,
	.summary = "Show manager listen sockets",
	.usage = showlistener_help,
};

static struct cw_clicmd show_manconn_cli = {
	.cmda = { "show", "manager", "connected", NULL },
	.handler = handle_show_mansess,
	.summary = "Show connected manager interface users",
	.usage = showmanconn_help,
};


static void mansession_release(struct cw_object *obj)
{
	struct mansession *sess = container_of(obj, struct mansession, obj);
	struct eventqent *eqe;

	if (sess->fd > -1)
		close(sess->fd);

	while (sess->eventq) {
		eqe = sess->eventq;
		sess->eventq = sess->eventq->next;
		cw_object_put(eqe->event);
		free(eqe);
	}

	pthread_mutex_destroy(&sess->lock);
	pthread_cond_destroy(&sess->ack);
	pthread_cond_destroy(&sess->activity);
	free(sess);
}


char *astman_get_header(struct message *m, char *key)
{
	int x;

	for (x = 0;  x < m->hdrcount;  x++) {
		if (!strcasecmp(key, m->header[x].key))
			return m->header[x].val;
	}
	return "";
}

struct cw_variable *astman_get_variables(struct message *m)
{
	struct cw_variable *head = NULL, *cur;
	char *var, *val;
	int x;

	for (x = 0;  x < m->hdrcount;  x++) {
		if (!strcasecmp("Variable", m->header[x].key)) {
			var = val = cw_strdupa(m->header[x].val);
			strsep(&val, "=");
			if (!val || cw_strlen_zero(var))
				continue;
			cur = cw_variable_new(var, val);
			if (head) {
				cur->next = head;
				head = cur;
			} else {
				head = cur;
			}
		}
	}

	return head;
}


void astman_send_error(struct mansession *s, struct message *m, char *error)
{
	cw_cli(s->fd, "Response: Error\r\n");
	if (!cw_strlen_zero(m->actionid))
		cw_cli(s->fd, "ActionID: %s\r\n", m->actionid);
	cw_cli(s->fd, "Message: %s\r\n\r\n", error);
}


void astman_send_response(struct mansession *s, struct message *m, char *resp, char *msg)
{
	cw_cli(s->fd, "Response: %s\r\n", resp);
	if (!cw_strlen_zero(m->actionid))
		cw_cli(s->fd, "ActionID: %s\r\n", m->actionid);
	if (msg)
		cw_cli(s->fd, "Message: %s\r\n\r\n", msg);
	else
		cw_cli(s->fd, "\r\n");
}


void astman_send_ack(struct mansession *s, struct message *m, char *msg)
{
	astman_send_response(s, m, "Success", msg);
}


static int set_eventmask(struct mansession *sess, char *eventmask)
{
	int maskint = -1;

	if (cw_strlen_zero(eventmask) || cw_true(eventmask))
		maskint = -1;
	else if (cw_false(eventmask))
		maskint = 0;
	else if (isdigit(*eventmask))
		maskint = atoi(eventmask);
	else
		maskint = manager_str_to_eventmask(eventmask);

	pthread_mutex_lock(&sess->lock);

	if (maskint >= 0)	
		sess->send_events = maskint;

	pthread_mutex_unlock(&sess->lock);

	return maskint;
}

static int authenticate(struct mansession *s, struct message *m)
{
	struct cw_config *cfg;
	char *cat;
	char *user = astman_get_header(m, "Username");
	char *pass = astman_get_header(m, "Secret");
	char *authtype = astman_get_header(m, "AuthType");
	char *key = astman_get_header(m, "Key");
	char *events = astman_get_header(m, "Events");
	
	cfg = cw_config_load("manager.conf");
	if (!cfg)
		return -1;
	cat = cw_category_browse(cfg, NULL);
	while (cat) {
		if (strcasecmp(cat, "general")) {
			/* This is a user */
			if (!strcasecmp(cat, user)) {
				struct cw_variable *v;
				struct cw_ha *ha = NULL;
				char *password = NULL;

				v = cw_variable_browse(cfg, cat);
				while (v) {
					if (!strcasecmp(v->name, "secret")) {
						password = v->value;
					} else if (!strcasecmp(v->name, "permit") || !strcasecmp(v->name, "deny")) {
						ha = cw_append_ha(v->name, v->value, ha);
					} else if (!strcasecmp(v->name, "writetimeout")) {
						cw_log(CW_LOG_WARNING, "writetimeout is deprecated - remove it from manager.conf\n");
					}
					v = v->next;
				}
				if (ha && (s->u.sa.sa_family != AF_INET || !cw_apply_ha(ha, &s->u.sin))) {
					cw_log(CW_LOG_NOTICE, "%s failed to pass IP ACL as '%s'\n", s->name, user);
					cw_free_ha(ha);
					cw_config_destroy(cfg);
					return -1;
				} else if (ha) {
					cw_free_ha(ha);
				}
				if (!strcasecmp(authtype, "MD5")) {
					if (!cw_strlen_zero(key) && !cw_strlen_zero(s->challenge) && !cw_strlen_zero(password)) {
						char md5key[256] = "";
						cw_md5_hash_two(md5key, s->challenge, password);
						if (!strcmp(md5key, key))
							break;
						cw_config_destroy(cfg);
						return -1;
					}
				} else if (password && !strcasecmp(password, pass)) {
					break;
				} else {
					cw_log(CW_LOG_NOTICE, "%s failed to authenticate as '%s'\n", s->name, user);
					cw_config_destroy(cfg);
					return -1;
				}	
			}
		}
		cat = cw_category_browse(cfg, cat);
	}
	if (cat) {
		cw_copy_string(s->username, cat, sizeof(s->username));
		s->readperm = manager_str_to_eventmask(cw_variable_retrieve(cfg, cat, "read"));
		s->writeperm = manager_str_to_eventmask(cw_variable_retrieve(cfg, cat, "write"));
		cw_config_destroy(cfg);
		if (events)
			set_eventmask(s, events);
		return 0;
	}
	cw_log(CW_LOG_NOTICE, "%s tried to authenticate with nonexistent user '%s'\n", s->name, user);
	cw_config_destroy(cfg);
	return -1;
}

static char mandescr_ping[] = 
"Description: A 'Ping' action will ellicit a 'Pong' response.  Used to keep the "
"  manager connection open.\n"
"Variables: NONE\n";

static int action_ping(struct mansession *s, struct message *m)
{
	astman_send_response(s, m, "Pong", NULL);
	return 0;
}

static char mandescr_listcommands[] = 
"Description: Returns the action name and synopsis for every\n"
"  action that is available to the user\n"
"Variables: NONE\n";

struct listcommands_print_args {
	struct mansession *s;
};

static int listcommands_print(struct cw_object *obj, void *data)
{
	char buf[80];
	struct manager_action *it = container_of(obj, struct manager_action, obj);
	struct listcommands_print_args *args = data;

	authority_to_str(it->authority, buf, sizeof(buf));
	cw_cli(args->s->fd, "%s: %s (Priv: %s)\r\n", it->action, it->synopsis, buf);

	return 0;
}

static int action_listcommands(struct mansession *s, struct message *m)
{
	struct listcommands_print_args args = {
		.s = s,
	};

	if (!cw_strlen_zero(m->actionid))
		cw_cli(s->fd, "Response: Success\r\nActionID: %s\r\n", m->actionid);
	else
		cw_cli(s->fd, "Response: Success\r\n");
	cw_registry_iterate(&manager_action_registry, listcommands_print, &args);
	cw_cli(s->fd, "\r\n");

	return RESULT_SUCCESS;
}

static char mandescr_events[] = 
"Description: Enable/Disable sending of events to this manager\n"
"  client.\n"
"Variables:\n"
"	EventMask: 'on' if all events should be sent,\n"
"		'off' if no events should be sent,\n"
"		'system,call,log' to select which flags events should have to be sent.\n";

static int action_events(struct mansession *s, struct message *m)
{
	char *mask = astman_get_header(m, "EventMask");
	int res;

	res = set_eventmask(s, mask);
	if (res > 0)
		astman_send_response(s, m, "Events On", NULL);
	else if (res == 0)
		astman_send_response(s, m, "Events Off", NULL);

	return 0;
}

static char mandescr_logoff[] = 
"Description: Logoff this manager session\n"
"Variables: NONE\n";

static int action_logoff(struct mansession *s, struct message *m)
{
	astman_send_response(s, m, "Goodbye", "Thanks for all the fish.");
	return -1;
}

static char mandescr_hangup[] = 
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

static char mandescr_setvar[] = 
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
	
	if (!strlen(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	if (!strlen(varname)) {
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

static char mandescr_getvar[] = 
"Description: Get the value of a local channel variable.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel to read variable from\n"
"	*Variable: Variable name\n"
"	ActionID: Optional Action id for message matching.\n";

static int action_getvar(struct mansession *s, struct message *m)
{
        struct cw_channel *c = NULL;
        char *name = astman_get_header(m, "Channel");
        char *varname = astman_get_header(m, "Variable");
	char *varval;
	char *varval2=NULL;

	if (!strlen(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	if (!strlen(varname)) {
		astman_send_error(s, m, "No variable specified");
		return 0;
	}

	c = cw_get_channel_by_name_locked(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	
	varval=pbx_builtin_getvar_helper(c,varname);
	if (varval)
		varval2 = cw_strdupa(varval);
	if (!varval2)
		varval2 = "";
	cw_mutex_unlock(&c->lock);
	cw_cli(s->fd, "Response: Success\r\n"
		"Variable: %s\r\nValue: %s\r\n" ,varname,varval2);
	if (!cw_strlen_zero(m->actionid))
		cw_cli(s->fd, "ActionID: %s\r\n", m->actionid);
	cw_cli(s->fd, "\r\n");

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

static char mandescr_redirect[] = 
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

static char mandescr_command[] = 
"Description: Run a CLI command.\n"
"Variables: (Names marked with * are required)\n"
"	*Command: CallWeaver CLI command to run\n"
"	ActionID: Optional Action id for message matching.\n";

/*! \brief  action_command: Manager command "command" - execute CLI command */
static int action_command(struct mansession *s, struct message *m)
{
	char *cmd = astman_get_header(m, "Command");

	if (s->handler == manager_session_ami) {
		cw_cli(s->fd, "Response: Follows\r\nPrivilege: Command\r\n");
		if (!cw_strlen_zero(m->actionid))
			cw_cli(s->fd, "ActionID: %s\r\n", m->actionid);
	}

	cw_cli_command(s->fd, cmd);

	if (s->handler == manager_session_ami)
		cw_cli(s->fd, "--END COMMAND--\r\n\r\n");

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
			in->vars, &chan);
	} else {
		res = cw_pbx_outgoing_exten(in->tech, CW_FORMAT_SLINEAR, in->data, in->timeout, in->context, in->exten, in->priority, &reason, 1, 
			!cw_strlen_zero(in->cid_num) ? in->cid_num : NULL, 
			!cw_strlen_zero(in->cid_name) ? in->cid_name : NULL,
			in->vars, &chan);
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
	free(in);
	return NULL;
}

static char mandescr_originate[] = 
"Description: Generates an outgoing call to a Extension/Context/Priority or\n"
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
	struct cw_variable *vars = astman_get_variables(m);
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
		astman_send_error(s, m, "Invalid priority\n");
		return 0;
	}
	if (!cw_strlen_zero(timeout) && (sscanf(timeout, "%d", &to) != 1)) {
		astman_send_error(s, m, "Invalid timeout\n");
		return 0;
	}
	cw_copy_string(tmp, name, sizeof(tmp));
	tech = tmp;
	data = strchr(tmp, '/');
	if (!data) {
		astman_send_error(s, m, "Invalid channel\n");
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
	if (account) {
		struct cw_variable *newvar;
		newvar = cw_variable_new("CDR(accountcode|r)", account);
		newvar->next = vars;
		vars = newvar;
	}
	if (cw_true(async)) {
		struct fast_originate_helper *fast = malloc(sizeof(struct fast_originate_helper));

		if (!fast) {
			res = -1;
		} else {
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
			fast->vars = vars;	
			cw_copy_string(fast->context, context, sizeof(fast->context));
			cw_copy_string(fast->exten, exten, sizeof(fast->exten));
			fast->timeout = to;
			fast->priority = pi;
			if (cw_pthread_create(&th, &global_attr_detached, fast_originate, fast)) {
				free(fast);
				res = -1;
			} else {
				res = 0;
			}
		}
	} else if (!cw_strlen_zero(app)) {
        	res = cw_pbx_outgoing_app(tech, CW_FORMAT_SLINEAR, data, to, app, appdata, &reason, 1, l, n, vars, NULL);
    	} else {
		if (exten && context && pi)
	        	res = cw_pbx_outgoing_exten(tech, CW_FORMAT_SLINEAR, data, to, context, exten, pi, &reason, 1, l, n, vars, NULL);
		else {
			astman_send_error(s, m, "Originate with 'Exten' requires 'Context' and 'Priority'");
			return 0;
		}
	}   
	if (!res)
		astman_send_ack(s, m, "Originate successfully queued");
	else
		astman_send_error(s, m, "Originate failed");
	return 0;
}

static char mandescr_mailboxstatus[] = 
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
	char idText[256] = "";
	char *mailbox = astman_get_header(m, "Mailbox");
	int ret;

	if (cw_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified");
		return 0;
	}
	if (!cw_strlen_zero(m->actionid))
		snprintf(idText, 256, "ActionID: %s\r\n", m->actionid);
	ret = cw_app_has_voicemail(mailbox, NULL);
	cw_cli(s->fd, "Response: Success\r\n"
				   "%s"
				   "Message: Mailbox Status\r\n"
				   "Mailbox: %s\r\n"
		 		   "Waiting: %d\r\n\r\n", idText, mailbox, ret);
	return 0;
}

static char mandescr_mailboxcount[] = 
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
	char idText[256] = "";
	char *mailbox = astman_get_header(m, "Mailbox");
	int newmsgs = 0, oldmsgs = 0;

	if (cw_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified");
		return 0;
	}
	cw_app_messagecount(mailbox, &newmsgs, &oldmsgs);
	if (!cw_strlen_zero(m->actionid))
		snprintf(idText, 256, "ActionID: %s\r\n", m->actionid);
	cw_cli(s->fd, "Response: Success\r\n"
				   "%s"
				   "Message: Mailbox Message Count\r\n"
				   "Mailbox: %s\r\n"
		 		   "NewMessages: %d\r\n"
				   "OldMessages: %d\r\n" 
				   "\r\n",
				    idText,mailbox, newmsgs, oldmsgs);
	return 0;
}

static char mandescr_extensionstate[] = 
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
	char idText[256] = "";
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
	if (!cw_strlen_zero(m->actionid))
		snprintf(idText, 256, "ActionID: %s\r\n", m->actionid);
	cw_cli(s->fd, "Response: Success\r\n"
			           "%s"
				   "Message: Extension Status\r\n"
				   "Exten: %s\r\n"
				   "Context: %s\r\n"
				   "Hint: %s\r\n"
		 		   "Status: %d\r\n\r\n",
				   idText,exten, context, hint, status);
	return 0;
}

static char mandescr_timeout[] = 
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

	cw_log(CW_LOG_DEBUG, "Manager received command '%s'\n", m->action);

	if (cw_strlen_zero(m->action))
		astman_send_error(s, m, "Missing action in request");
	else if (s->authenticated) {
		struct cw_object *it;

		if ((it = cw_registry_find(&manager_action_registry, m->action))) {
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

		authtype = astman_get_header(m, "AuthType");
		if (!strcasecmp(authtype, "MD5")) {
			if (cw_strlen_zero(s->challenge))
				snprintf(s->challenge, sizeof(s->challenge), "%lu", cw_random());
			if (!cw_strlen_zero(m->actionid))
				cw_cli(s->fd, "Response: Success\r\n"
						"ActionID: %s\r\n"
						"Challenge: %s\r\n\r\n",
						m->actionid, s->challenge);
			else
				cw_cli(s->fd, "Response: Success\r\n"
						"Challenge: %s\r\n\r\n",
						s->challenge);
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
						memcpy(buf, &buf[pos + 1], res - 1);
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


static void manager_session_ami_cleanup(void *data)
{
	struct mansession *sess = data;

	if (sess->reg_entry)
		cw_registry_del(&manager_session_registry, sess->reg_entry);

	if (sess->authenticated) {
		if (option_verbose > 3 && displayconnects)
			cw_verbose(VERBOSE_PREFIX_2 "Manager '%s' logged off from %s\n", sess->username, sess->name);
		cw_log(CW_LOG_EVENT, "Manager '%s' logged off from %s\n", sess->username, sess->name);
	} else {
		if (option_verbose > 2 && displayconnects)
			cw_verbose(VERBOSE_PREFIX_2 "Connect attempt from '%s' unable to authenticate\n", sess->name);
		cw_log(CW_LOG_EVENT, "Failed attempt from %s\n", sess->name);
	}

	if (!pthread_equal(sess->reader_tid, CW_PTHREADT_NULL)) {
		pthread_cancel(sess->reader_tid);
		pthread_join(sess->reader_tid, NULL);
	}

	cw_object_put(sess);
}


void *manager_session_ami(void *data)
{
	struct mansession *sess = data;
	int res;

	write(sess->fd, MANAGER_AMI_HELLO, sizeof(MANAGER_AMI_HELLO) - 1);

	sess->reader_tid = CW_PTHREADT_NULL;

	pthread_cleanup_push(manager_session_ami_cleanup, sess);

	sess->reg_entry = cw_registry_add(&manager_session_registry, &sess->obj);

	if ((res = cw_pthread_create(&sess->reader_tid, &global_attr_default, manager_session_ami_read, sess))) {
		cw_log(CW_LOG_ERROR, "session reader thread creation failed: %s\n", strerror(res));
		return NULL;
	}

	for (;;) {
		struct eventqent *eqe = NULL;

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &sess->lock);
		pthread_mutex_lock(&sess->lock);

		/* If there's no request message and no queued events
		 * we have to wait for activity.
		 */
		if (!sess->m && !sess->eventq)
			pthread_cond_wait(&sess->activity, &sess->lock);

		/* Unhook the top event (if any) now. Once we have that
		 * we can unlock the session.
		 */
		if ((eqe = sess->eventq))
			if (!(sess->eventq = sess->eventq->next))
				sess->eventq_tail = &sess->eventq;

		pthread_cleanup_pop(1);

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (sess->m) {
			if (process_message(sess, sess->m))
				break;

			/* Remove the queued message and signal completion to the reader */
			pthread_mutex_lock(&sess->lock);
			sess->m = NULL;
			pthread_cond_signal(&sess->ack);
			pthread_mutex_unlock(&sess->lock);
		}

		if (eqe) {
			const char *data = eqe->event->data;
			int len = eqe->event->len;

			res = 0;
			while (len > 0) {
				int n = write(sess->fd, data, len);
				if (n >= 0) {
					data += n;
					len -= n;
				} else {
					cw_log(CW_LOG_WARNING, "Disconnecting manager session %s, write gave: %s\n", sess->name, strerror(errno));
					res = -1;
				}
			}

			cw_object_put(eqe->event);
			free(eqe);
			if (res < 0)
				break;
		}
	}

	pthread_cleanup_pop(1);
	return NULL;
}


static void *manager_session_console_read(void *data)
{
	char buf[1024];
	struct message m;
	struct mansession *sess = data;
	int pos;
	int res;

	memset(&m, 0, sizeof(m));
	m.action = "Command";
	m.hdrcount = 1;
	m.header[0].key = "Command";
	m.header[0].val = buf;

	pos = 0;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	for (;;) {
		if ((res = read(sess->fd, buf + pos, sizeof(buf) - pos)) <= 0) {
			pthread_cancel(sess->writer_tid);
			break;
		}

		for (; res; pos++, res--) {
			if (buf[pos] == '\0' || buf[pos] == '\r' || buf[pos] == '\n') {
				if (pos) {
					/* End of message, go do it */
					buf[pos] = '\0';
					if (buf[0] == '\020') {
						if (pos - 1 == sizeof("events") - 1 && !strcmp(buf + 1, "events"))
							sess->send_events = EVENT_FLAG_LOG_ALL | EVENT_FLAG_PROGRESS;
					} else {
						pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &sess->lock);
						pthread_mutex_lock(&sess->lock);
						sess->m = &m;
						pthread_cond_signal(&sess->activity);
						pthread_cond_wait(&sess->ack, &sess->lock);
						pthread_cleanup_pop(1);
					}
				}
				memcpy(buf, &buf[pos + 1], res - 1);
				pos = -1;
			}
		}

		if (pos == sizeof(buf)) {
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
			cw_log(CW_LOG_ERROR, "Console session %s dropped due to oversize message\n", sess->name);
			break;
		}
	}

	return NULL;
}


static void manager_session_console_cleanup(void *data)
{
	struct mansession *sess = data;

	if (sess->reg_entry)
		cw_registry_del(&manager_session_registry, sess->reg_entry);

	if (option_verbose > 3 && displayconnects)
		cw_verbose(VERBOSE_PREFIX_2 "Console disconnected from %s\n", sess->name);
	cw_log(CW_LOG_EVENT, "Console disconnected from %s\n", sess->name);

	if (!pthread_equal(sess->reader_tid, CW_PTHREADT_NULL)) {
		pthread_cancel(sess->reader_tid);
		pthread_join(sess->reader_tid, NULL);
	}

	cw_object_put(sess);
}


void *manager_session_console(void *data)
{
	char buf[11];
	struct mansession *sess = data;
	int res;

	if (option_verbose > 3 && displayconnects)
		cw_verbose(VERBOSE_PREFIX_2 "Console connected from %s\n", sess->name);
	cw_log(CW_LOG_EVENT, "Console connected from %s\n", sess->name);

	write(sess->fd, hostname, strlen(hostname));
	write(sess->fd, "/", 1);
	res = snprintf(buf, sizeof(buf), "%d", cw_mainpid);
	write(sess->fd, buf, res);
	write(sess->fd, "/", 1);
	write(sess->fd, cw_version_string, strlen(cw_version_string));
	write(sess->fd, "\n", 1);

	/* Ok, we're ready. Tell the core to boot if it hasn't already */
	if (!fully_booted)
		kill(cw_mainpid, SIGHUP);

	sess->reader_tid = CW_PTHREADT_NULL;

	pthread_cleanup_push(manager_session_console_cleanup, sess);

	sess->reg_entry = cw_registry_add(&manager_session_registry, &sess->obj);

	if ((res = cw_pthread_create(&sess->reader_tid, &global_attr_default, manager_session_console_read, sess))) {
		cw_log(CW_LOG_ERROR, "Console session reader thread creation failed: %s\n", strerror(res));
		return NULL;
	}

	for (;;) {
		struct eventqent *eqe = NULL;

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &sess->lock);
		pthread_mutex_lock(&sess->lock);

		/* If there's no request message and no queued events
		 * we have to wait for activity.
		 */
		if (!sess->m && !sess->eventq)
			pthread_cond_wait(&sess->activity, &sess->lock);

		/* Unhook the top event (if any) now. Once we have that
		 * we can unlock the session.
		 */
		if ((eqe = sess->eventq))
			if (!(sess->eventq = sess->eventq->next))
				sess->eventq_tail = &sess->eventq;

		pthread_cleanup_pop(1);

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (sess->m) {
			if (process_message(sess, sess->m))
				break;

			/* Remove the queued message and signal completion to the reader */
			pthread_mutex_lock(&sess->lock);
			sess->m = NULL;
			pthread_cond_signal(&sess->ack);
			pthread_mutex_unlock(&sess->lock);
		}

		if (eqe) {
			const char *data = eqe->event->data;
			const char *msg;
			int len = eqe->event->len;
			int logevent, level, msglen, msgtail, l;

			msg = NULL;
			res = logevent = level = msglen = msgtail = 0;

			while (!res && len > 0) {
				const char *key = data;
				int lkey;

				for (l = 0; l < len && data[l] != ':'; l++);
				if (l == len)
					break;

				lkey = l;

				data += l + 1;
				len -= l + 1;
				while (len && *data == ' ') data++,len--;

				for (l = 0; l < len && data[l] != '\r' && data[l] != '\n'; l++);

				switch (lkey) {
					case sizeof("Event") - 1:
						if (!strncmp(key, "Event", sizeof("Event") - 1)) {
							if (!strncmp(data, "Log", sizeof("Log") - 1))
								logevent = 1;
						} else if (!strncmp(key, "Level", sizeof("Level") - 1))
							level = atol(data);
						break;
					case sizeof("Message Len") - 1:
						if (!strncmp(key, "Message Len", sizeof("Message Len") - 1))
							msgtail = atol(data);
						break;
					case sizeof("Message") - 1:
						if (!strncmp(key, "Message", sizeof("Message") - 1)) {
							msg = data;
							msglen = l;
						}
						break;
				}

				data += l;
				len -= l;
				while (len && (*data == '\r' || *data == '\n')) data++,len--;
			}

			if (logevent && msg) {
				if (level != CW_EVENT_NUM_VERBOSE && level != CW_EVENT_NUM_PROGRESS) {
					if (msglen && write(sess->fd, msg, msglen) <= 0)
						res = -1;
				} else if (msgtail && write(sess->fd, msg + msglen - msgtail, msgtail) <= 0)
					res = -1;

				if ((level != CW_EVENT_NUM_PROGRESS || !msgtail) && !res && write(sess->fd, "\r\n", 2) <= 0)
					res = -1;
			}

			cw_object_put(eqe->event);
			free(eqe);
			if (res < 0)
				break;
		}
	}

	pthread_cleanup_pop(1);
	return NULL;
}


static void manager_session_log_cleanup(void *data)
{
	struct mansession *sess = data;

	if (sess->reg_entry)
		cw_registry_del(&manager_session_registry, sess->reg_entry);

	cw_object_put(sess);
}


void *manager_session_log(void *data)
{
	struct mansession *sess = data;
	int res;

	pthread_cleanup_push(manager_session_log_cleanup, sess);

	sess->reg_entry = cw_registry_add(&manager_session_registry, &sess->obj);

	for (;;) {
		struct eventqent *eqe = NULL;

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &sess->lock);
		pthread_mutex_lock(&sess->lock);

		/* If there are no queued events we have to wait for activity. */
		if (!sess->m && !sess->eventq)
			pthread_cond_wait(&sess->activity, &sess->lock);

		/* Unhook the top event (if any) now. Once we have that
		 * we can unlock the session.
		 */
		if ((eqe = sess->eventq))
			if (!(sess->eventq = sess->eventq->next))
				sess->eventq_tail = &sess->eventq;

		pthread_cleanup_pop(1);

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		/* There _should_ be an event. Why else were we woken up? */
		if (eqe) {
			static const int priorities[] = {
				[CW_EVENT_NUM_ERROR]	= LOG_ERR,
				[CW_EVENT_NUM_WARNING]	= LOG_WARNING,
				[CW_EVENT_NUM_NOTICE]	= LOG_NOTICE,
				[CW_EVENT_NUM_VERBOSE]	= LOG_INFO,
				[CW_EVENT_NUM_EVENT]	= LOG_INFO,
				[CW_EVENT_NUM_DTMF]	= LOG_INFO,
				[CW_EVENT_NUM_DEBUG]	= LOG_DEBUG,
			};
			enum { F_LEVEL = 0, F_DATELEN, F_MESSAGE };
			static struct {
				int l;
				const char *s;
			} keys[] = {
#define LENSTR(x)	sizeof(x) - 1, x
				[F_LEVEL]   = { LENSTR("Level") },
				[F_DATELEN] = { LENSTR("Date Len") },
				[F_MESSAGE] = { LENSTR("Message") },
#undef LENSTR
			};
			struct {
				int l;
				char *s;
			} vals[arraysize(keys)];
			char *key, *ekey;
			char *val, *eval;
			int lkey, lval;
			int level, i;

			memset(vals, 0, sizeof(vals));

			key = eqe->event->data;
			while (*key) {
				for (ekey = key; *ekey && *ekey != ':' && *ekey != '\r' && *ekey != '\n'; ekey++);
				if (!*ekey)
					break;

				for (val = ekey + 1; *val && *val == ' '; val++);
				for (eval = val; *eval && *eval != '\r' && *eval != '\n'; eval++);

				lkey = ekey - key;
				lval = eval - val;

				/* We shouldn't get anything other than log events. */
				if (unlikely(lkey == sizeof("Event") - 1 && !memcmp(key, "Event", sizeof("Event") - 1)
				&& (lval != sizeof("Log") - 1 || memcmp(val, "Log", sizeof("Log") - 1))))
					return 0;

				for (i = 0; i < arraysize(keys); i++) {
					if (lkey == keys[i].l && !strncmp(key, keys[i].s, lkey)) {
						vals[i].l = lval;
						vals[i].s = val;
						break;
					}
				}

				if (!*eval)
					break;

				for (key = eval + 1; *key && (*key == '\r' || *key == '\n'); key++);
			}

			res = 0;

			if (vals[F_MESSAGE].s) {
				if (sess->fd >= 0) {
					vals[F_MESSAGE].s[vals[F_MESSAGE].l++] = '\n';
					while (vals[F_MESSAGE].l > 0 && (res = write(sess->fd, vals[F_MESSAGE].s, vals[F_MESSAGE].l)) > 0) {
						vals[F_MESSAGE].s += res;
						vals[F_MESSAGE].l -= res;
					}
					if (res <= 0) {
						cw_log(CW_LOG_WARNING, "Disconnecting manager session %s, write gave: %s\n", sess->name, strerror(errno));
						res = -1;
					}
				} else {
					level = (vals[F_LEVEL].s ? atol(vals[F_LEVEL].s) : 0);
					if (vals[F_DATELEN].s) {
						lkey = atol(vals[F_DATELEN].s);
						vals[F_MESSAGE].s += lkey;
						vals[F_MESSAGE].l -= lkey;
						/* FIXME: this seems unnecessary. Why not leave the priority string in */
						if (priorities[level] != LOG_INFO && (key = strchr(vals[F_MESSAGE].s, '['))) {
							vals[F_MESSAGE].l -= key - vals[F_MESSAGE].s;
							vals[F_MESSAGE].s = key;
						}
					}
					syslog(priorities[level], "%.*s", vals[F_MESSAGE].l, vals[F_MESSAGE].s);
				}
			}

			cw_object_put(eqe->event);
			free(eqe);
			if (res < 0)
				break;
		}
	}

	pthread_cleanup_pop(1);
	return NULL;
}


struct mansession *manager_session_start(void *(* const handler)(void *), int fd, int family, void *addr, size_t addr_len, int readperm, int writeperm, int send_events)
{
	char buf[1];
	struct mansession *sess;
	int namelen;

	namelen = addr_to_str(family, addr, buf, sizeof(buf)) + 1;

	if ((sess = calloc(1, sizeof(struct mansession) + namelen)) == NULL) {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		return NULL;
	}

	addr_to_str(family, addr, sess->name, namelen);

	/* Only copy the address into the session if it is representable by
	 * a sockaddr_* type. The address is needed for ACL checks on connection
	 * so strictly we only need IPv4 addresses at the moment (that's all
	 * ACLs support)
	 */
	if (family == AF_INET || family == AF_INET6 || family == AF_LOCAL)
		memcpy(&sess->u, addr, addr_len);
	sess->u.sa.sa_family = family;

	sess->eventq_tail = &sess->eventq;
	sess->fd = fd;
	sess->handler = handler;
	sess->readperm = readperm;
	if ((sess->writeperm = writeperm))
		sess->authenticated = 1;
	sess->send_events = send_events;

	cw_object_init(sess, NULL, CW_OBJECT_NO_REFS);
	cw_object_get(sess);
	pthread_mutex_init(&sess->lock, NULL);
	pthread_cond_init(&sess->activity, NULL);
	pthread_cond_init(&sess->ack, NULL);
	sess->obj.release = mansession_release;

	cw_object_dup(sess);
	if (cw_pthread_create(&sess->writer_tid, &global_attr_detached, handler, sess)) {
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

		if (listener->spec[0] == '/')
			unlink(listener->spec);
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
	const int arg = 1;

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
		setsockopt(fd, SOL_TCP, TCP_NODELAY, &arg, sizeof(arg));

		if ((sess = manager_session_start(listener->handler, fd, u.sa.sa_family, &u, salen, listener->readperm, listener->writeperm, listener->send_events)))
			cw_object_put(sess);
	}

	pthread_cleanup_pop(1);
	return NULL;
}


struct manager_event_args {
	int ret;
	int category;
	struct manager_event *me;
	char *event;
	char *fmt;
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
		used = snprintf(args->me->data, alloc, "Event: %s\r\nPrivilege: ", args->event);
		used += authority_to_str(args->category, args->me->data + used, alloc - used);
		if (alloc - used > 2)
			strcpy(args->me->data + used, "\r\n");
		used += 2;
		va_copy(aq, args->ap);
		n = vsnprintf(args->me->data + used, (alloc < used ? 0 : alloc - used), args->fmt, aq);
		va_end(aq);
		if (n >= 0)
			used += n;
		if (alloc - used > 2)
			strcpy(args->me->data + used, "\r\n");
		used += 2;

		if (used < alloc) {
			args->me->len = used;
			args->me->obj.release = manager_event_free;
			cw_object_init(args->me, NULL, CW_OBJECT_NO_REFS);
			cw_object_get(args->me);
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

static int manager_event_print(struct cw_object *obj, void *data)
{
	struct mansession *it = container_of(obj, struct mansession, obj);
	struct manager_event_args *args = data;

	if (!args->ret && (it->readperm & args->category) == args->category && (it->send_events & args->category) == args->category) {
		if (args->me || !(args->ret = make_event(args)))
			append_event(it, args->me);
	}

	return args->ret;
}

int manager_event(int category, char *event, char *fmt, ...)
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


static void manager_listen(const char *buf, void *(* const handler)(void *), int readperm, int writeperm, int send_events)
{
	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_un sun;
	} u;
	struct manager_listener *listener;
	socklen_t salen;
	const int arg = 1;

	if (!(listener = malloc(sizeof(*listener) + strlen(buf) + 1))) {
		cw_log(CW_LOG_ERROR, "Out of memory!\n");
		return;
	}

	cw_object_init(listener, NULL, CW_OBJECT_NO_REFS);
	cw_object_get(listener);
	listener->obj.release = listener_free;
	listener->reg_entry = NULL;
	listener->tid = CW_PTHREADT_NULL;
	listener->sock = -1;
	listener->handler = handler;
	listener->readperm = readperm;
	listener->writeperm = writeperm;
	listener->send_events = send_events;
	strcpy(listener->spec, buf);

	if (listener->spec[0] == '/') {
		salen = sizeof(u.sun);
		u.sun.sun_family = AF_LOCAL;
		strncpy(u.sun.sun_path, listener->spec, sizeof(u.sun.sun_path));
		unlink(listener->spec);
	} else {
		char *port;
		int portno;

		salen = sizeof(u.sin);
		u.sin.sin_family = AF_INET;
		memset(&u.sin.sin_addr, 0, sizeof(u.sin.sin_addr));

		if (!(port = strrchr(listener->spec, ':'))) {
			u.sin.sin_port = htons(DEFAULT_MANAGER_PORT);
		} else {
			if (sscanf(port + 1, "%d", &portno) != 1) {
				cw_log(CW_LOG_ERROR, "Invalid port number '%s' in '%s'\n", port + 1, listener->spec);
				return;
			}
			u.sin.sin_port = htons(portno);
			*port = '\0';
		}

		if (!inet_aton(listener->spec, &u.sin.sin_addr)) {
			cw_log(CW_LOG_ERROR, "Invalid address '%s' specified\n", listener->spec);
			return;
		}

		if (port)
			*port = ':';
	}

	if ((listener->sock = socket(u.sa.sa_family, SOCK_STREAM, 0)) < 0) {
		cw_log(CW_LOG_ERROR, "Unable to create socket: %s\n", strerror(errno));
		return;
	}

	fcntl(listener->sock, F_SETFD, fcntl(listener->sock, F_GETFD, 0) | FD_CLOEXEC);
	setsockopt(listener->sock, SOL_SOCKET, SO_REUSEADDR, &arg, sizeof(arg));

	if (bind(listener->sock, &u.sa, salen)) {
		cw_log(CW_LOG_ERROR, "Unable to bind to '%s': %s\n", listener->spec, strerror(errno));
		return;
	}

	if (listen(listener->sock, 1024)) {
		cw_log(CW_LOG_ERROR, "Unable to listen on '%s': %s\n", listener->spec, strerror(errno));
		return;
	}

	if (u.sa.sa_family == AF_LOCAL) {
		if (handler == manager_session_ami)
			chmod(u.sun.sun_path, 0666);
		else if (handler == manager_session_console) {
			uid_t uid = -1;
			gid_t gid = -1;

			if (!cw_strlen_zero(cw_config_CW_CTL_PERMISSIONS)) {
				mode_t p;
				sscanf(cw_config_CW_CTL_PERMISSIONS, "%o", (int *) &p);
				if ((chmod(u.sun.sun_path, p)) < 0)
					cw_log(CW_LOG_WARNING, "Unable to change file permissions of %s: %s\n", u.sun.sun_path, strerror(errno));
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

			if (chown(u.sun.sun_path, uid, gid) < 0)
				cw_log(CW_LOG_WARNING, "Unable to change ownership of %s: %s\n", u.sun.sun_path, strerror(errno));
		}
	}

	if ((listener->reg_entry = cw_registry_add(&manager_listener_registry, &listener->obj))) {
		cw_object_dup(listener);
		if (!cw_pthread_create(&listener->tid, &global_attr_default, accept_thread, listener)) {
			if (option_verbose)
				cw_verbose("CallWeaver Management interface listening on '%s'\n", listener->spec);
		} else {
			cw_log(CW_LOG_ERROR, "Failed to start manager thread for %s: %s\n", listener->spec, strerror(errno));
			cw_registry_del(&manager_listener_registry, listener->reg_entry);
			cw_object_put(listener);
		}
	}

	cw_object_put(listener);
}


static int listener_cancel(struct cw_object *obj, void *data)
{
	struct manager_listener *it = container_of(obj, struct manager_listener, obj);

	if (!pthread_equal(it->tid, CW_PTHREADT_NULL))
		pthread_cancel(it->tid);
	return 0;
}


static int listener_join(struct cw_object *obj, void *data)
{
	struct manager_listener *it = container_of(obj, struct manager_listener, obj);

	if (!pthread_equal(it->tid, CW_PTHREADT_NULL))
		pthread_join(it->tid, NULL);

	cw_registry_del(&manager_listener_registry, it->reg_entry);
	cw_object_put(it);
	return 0;
}


int manager_reload(void)
{
	struct cw_config *cfg;
	struct cw_variable *v;
	char *bindaddr, *portno;

	/* Shut down any existing listeners */
	cw_registry_iterate(&manager_listener_registry, listener_cancel, NULL);
	cw_registry_iterate(&manager_listener_registry, listener_join, NULL);

	/* Reset to hard coded defaults */
	bindaddr = NULL;
	portno = NULL;
	displayconnects = 1;

	manager_listen(cw_config_CW_SOCKET, manager_session_console, EVENT_FLAG_LOG_ALL | EVENT_FLAG_PROGRESS, EVENT_FLAG_COMMAND, 0);

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

	cw_cli_register(&show_mancmd_cli);
	cw_cli_register(&show_mancmds_cli);
	cw_cli_register(&show_listener_cli);
	cw_cli_register(&show_manconn_cli);
	cw_extension_state_add(NULL, NULL, manager_state_cb, NULL);

	return 0;
}
