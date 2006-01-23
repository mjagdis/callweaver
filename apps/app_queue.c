/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
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
 * \brief True call queues with optional send URL on answer
 * 
 * \arg Config in \ref Config_qu queues.conf
 *
 * \par Development notes
 * \note 2004-11-25: Persistent Dynamic Members added by:
 *             NetNation Communications (www.netnation.com)
 *             Kevin Lindsay <kevinl@netnation.com>
 * 
 *             Each dynamic agent in each queue is now stored in the opbxdb.
 *             When openpbx is restarted, each agent will be automatically
 *             readded into their recorded queues. This feature can be
 *             configured with the 'persistent_members=<1|0>' setting in the
 *             '[general]' category in queues.conf. The default is on.
 * 
 * \note 2004-06-04: Priorities in queues added by inAccess Networks (work funded by Hellas On Line (HOL) www.hol.gr).
 *
 * \note These features added by David C. Troy <dave@toad.net>:
 *    - Per-queue holdtime calculation
 *    - Estimated holdtime announcement
 *    - Position announcement
 *    - Abandoned/completed call counters
 *    - Failout timer passed as optional app parameter
 *    - Optional monitoring of calls, started when call is answered
 *
 * Patch Version 1.07 2003-12-24 01
 *
 * Added servicelevel statistic by Michiel Betel <michiel@betel.nl>
 * Added Priority jumping code for adding and removing queue members by Jonathan Stanton <openpbx@doilooklikeicare.com>
 *
 * Fixed to work with CVS as of 2004-02-25 and released as 1.07a
 * by Matthew Enger <m.enger@xi.com.au>
 *
 * \ingroup applications
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

#include "openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL$", "$Revision$")

#include "openpbx/lock.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/options.h"
#include "openpbx/module.h"
#include "openpbx/translate.h"
#include "openpbx/say.h"
#include "openpbx/features.h"
#include "openpbx/musiconhold.h"
#include "openpbx/cli.h"
#include "openpbx/manager.h"
#include "openpbx/config.h"
#include "openpbx/monitor.h"
#include "openpbx/utils.h"
#include "openpbx/causes.h"
#include "openpbx/opbxdb.h"
#include "openpbx/devicestate.h"

#define QUEUE_STRATEGY_RINGALL		0
#define QUEUE_STRATEGY_ROUNDROBIN	1
#define QUEUE_STRATEGY_LEASTRECENT	2
#define QUEUE_STRATEGY_FEWESTCALLS	3
#define QUEUE_STRATEGY_RANDOM		4
#define QUEUE_STRATEGY_RRMEMORY		5

static struct strategy {
	int strategy;
	char *name;
} strategies[] = {
	{ QUEUE_STRATEGY_RINGALL, "ringall" },
	{ QUEUE_STRATEGY_ROUNDROBIN, "roundrobin" },
	{ QUEUE_STRATEGY_LEASTRECENT, "leastrecent" },
	{ QUEUE_STRATEGY_FEWESTCALLS, "fewestcalls" },
	{ QUEUE_STRATEGY_RANDOM, "random" },
	{ QUEUE_STRATEGY_RRMEMORY, "rrmemory" },
};

#define DEFAULT_RETRY		5
#define DEFAULT_TIMEOUT		15
#define RECHECK			1		/* Recheck every second to see we we're at the top yet */

#define	RES_OKAY	0		/* Action completed */
#define	RES_EXISTS	(-1)		/* Entry already exists */
#define	RES_OUTOFMEMORY	(-2)		/* Out of memory */
#define	RES_NOSUCHQUEUE	(-3)		/* No such queue */

static char *tdesc = "True Call Queueing";

static char *app = "Queue";

static char *synopsis = "Queue a call for a call queue";

static char *descrip =
"  Queue(queuename[|options[|URL][|announceoverride][|timeout]]):\n"
"Queues an incoming call in a particular call queue as defined in queues.conf.\n"
"This application will return to the dialplan if the queue does not exist, or\n"
"any of the join options cause the caller not to enter the queue.\n"
"The option string may contain zero or more of the following characters:\n"
"      'd' -- data-quality (modem) call (minimum delay).\n"
"      'h' -- allow callee to hang up by hitting *.\n"
"      'H' -- allow caller to hang up by hitting *.\n"
"      'n' -- no retries on the timeout; will exit this application and \n"
"	      go to the next step.\n"
"      'r' -- ring instead of playing MOH\n"
"      't' -- allow the called user transfer the calling user\n"
"      'T' -- to allow the calling user to transfer the call.\n"
"      'w' -- allow the called user to write the conversation to disk via Monitor\n"
"      'W' -- allow the calling user to write the conversation to disk via Monitor\n"
"  In addition to transferring the call, a call may be parked and then picked\n"
"up by another user.\n"
"  The optional URL will be sent to the called party if the channel supports\n"
"it.\n"
"  The timeout will cause the queue to fail out after a specified number of\n"
"seconds, checked between each queues.conf 'timeout' and 'retry' cycle.\n"
"  This application sets the following channel variable upon completion:\n"
"      QUEUESTATUS    The status of the call as a text string, one of\n"
"             TIMEOUT | FULL | JOINEMPTY | LEAVEEMPTY | JOINUNAVAIL | LEAVEUNAVAIL\n";

static char *app_aqm = "AddQueueMember" ;
static char *app_aqm_synopsis = "Dynamically adds queue members" ;
static char *app_aqm_descrip =
"   AddQueueMember(queuename[|interface[|penalty]]):\n"
"Dynamically adds interface to an existing queue.\n"
"If the interface is already in the queue and there exists an n+101 priority\n"
"then it will then jump to this priority.  Otherwise it will return an error\n"
"Example: AddQueueMember(techsupport|SIP/3000)\n"
"";

static char *app_rqm = "RemoveQueueMember" ;
static char *app_rqm_synopsis = "Dynamically removes queue members" ;
static char *app_rqm_descrip =
"   RemoveQueueMember(queuename[|interface]):\n"
"Dynamically removes interface to an existing queue\n"
"If the interface is NOT in the queue and there exists an n+101 priority\n"
"then it will then jump to this priority.  Otherwise it will return an error\n"
"Example: RemoveQueueMember(techsupport|SIP/3000)\n"
"";

static char *app_pqm = "PauseQueueMember" ;
static char *app_pqm_synopsis = "Pauses a queue member" ;
static char *app_pqm_descrip =
"   PauseQueueMember([queuename]|interface):\n"
"Pauses (blocks calls for) a queue member.\n"
"The given interface will be paused in the given queue.  This prevents\n"
"any calls from being sent from the queue to the interface until it is\n"
"unpaused with UnpauseQueueMember or the manager interface.  If no\n"
"queuename is given, the interface is paused in every queue it is a\n"
"member of.  If the interface is not in the named queue, or if no queue\n"
"is given and the interface is not in any queue, it will jump to\n"
" priority n+101, if it exists.  The application will fail if the interface is not\n"
"found and no extension to jump to exists.\n"
"Example: PauseQueueMember(|SIP/3000)\n";

static char *app_upqm = "UnpauseQueueMember" ;
static char *app_upqm_synopsis = "Unpauses a queue member" ;
static char *app_upqm_descrip =
"   UnpauseQueueMember([queuename]|interface):\n"
"Unpauses (resumes calls to) a queue member.\n"
"This is the counterpart to PauseQueueMember and operates exactly the\n"
"same way, except it unpauses instead of pausing the given interface.\n"
"Example: UnpauseQueueMember(|SIP/3000)\n";

/*! \brief Persistent Members opbxdb family */
static const char *pm_family = "/Queue/PersistentMembers";
/* The maximum lengh of each persistent member queue database entry */
#define PM_MAX_LEN 2048

/*! \brief queues.conf [general] option */
static int queue_persistent_members = 0;

/*! \brief queues.conf per-queue weight option */
static int use_weight = 0;

enum queue_result {
	QUEUE_UNKNOWN = 0,
	QUEUE_TIMEOUT = 1,
	QUEUE_JOINEMPTY = 2,
	QUEUE_LEAVEEMPTY = 3,
	QUEUE_JOINUNAVAIL = 4,
	QUEUE_LEAVEUNAVAIL = 5,
	QUEUE_FULL = 6,
};

const struct { 
	enum queue_result id;
	char *text;
} queue_results[] = {
	{ QUEUE_UNKNOWN, "UNKNOWN" },
	{ QUEUE_TIMEOUT, "TIMEOUT" },
	{ QUEUE_JOINEMPTY,"JOINEMPTY" },
	{ QUEUE_LEAVEEMPTY, "LEAVEEMPTY" },
	{ QUEUE_JOINUNAVAIL, "JOINUNAVAIL" },
	{ QUEUE_LEAVEUNAVAIL, "LEAVEUNAVAIL" },
	{ QUEUE_FULL, "FULL" },
};

/*! \brief We define a custom "local user" structure because we
   use it not only for keeping track of what is in use but
   also for keeping track of who we're dialing. */

struct localuser {
	struct opbx_channel *chan;
	char interface[256];
	int stillgoing;
	int metric;
	int oldstatus;
	time_t lastcall;
	struct member *member;
	struct localuser *next;
};

LOCAL_USER_DECL;

struct queue_ent {
	struct opbx_call_queue *parent;	/*!< What queue is our parent */
	char moh[80];			/*!< Name of musiconhold to be used */
	char announce[80];		/*!< Announcement to play for member when call is answered */
	char context[OPBX_MAX_CONTEXT];	/*!< Context when user exits queue */
	char digits[OPBX_MAX_EXTENSION];	/*!< Digits entered while in queue */
	int pos;			/*!< Where we are in the queue */
	int prio;			/*!< Our priority */
	int lopbx_pos_said;              /*!< Last position we told the user */
	time_t lopbx_periodic_announce_time;	/*!< The last time we played a periodic anouncement */
	time_t lopbx_pos;                /*!< Last time we told the user their position */
	int opos;			/*!< Where we started in the queue */
	int handled;			/*!< Whether our call was handled */
	time_t start;			/*!< When we started holding */
	time_t expire;			/*!< When this entry should expire (time out of queue) */
	struct opbx_channel *chan;	/*!< Our channel */
	struct queue_ent *next;		/*!< The next queue entry */
};

struct member {
	char interface[80];		/*!< Technology/Location */
	int penalty;			/*!< Are we a last resort? */
	int calls;			/*!< Number of calls serviced by this member */
	int dynamic;			/*!< Are we dynamically added? */
	int status;			/*!< Status of queue member */
	int paused;			/*!< Are we paused (not accepting calls)? */
	time_t lastcall;		/*!< When last successful call was hungup */
	int dead;			/*!< Used to detect members deleted in realtime */
	struct member *next;		/*!< Next member */
};

/* values used in multi-bit flags in opbx_call_queue */
#define QUEUE_EMPTY_NORMAL 1
#define QUEUE_EMPTY_STRICT 2
#define ANNOUNCEHOLDTIME_ALWAYS 1
#define ANNOUNCEHOLDTIME_ONCE 2

struct opbx_call_queue {
	opbx_mutex_t lock;	
	char name[80];			/*!< Name */
	char moh[80];			/*!< Music On Hold class to be used */
	char announce[80];		/*!< Announcement to play when call is answered */
	char context[OPBX_MAX_CONTEXT];	/*!< Exit context */
		unsigned int monjoin:1;
		unsigned int dead:1;
		unsigned int joinempty:2;
		unsigned int eventwhencalled:1;
		unsigned int leavewhenempty:2;
		unsigned int reportholdtime:1;
		unsigned int wrapped:1;
		unsigned int timeoutrestart:1;
		unsigned int announceholdtime:2;
		unsigned int strategy:3;
		unsigned int maskmemberstatus:1;
		unsigned int realtime:1;
	int announcefrequency;          /*!< How often to announce their position */
	int periodicannouncefrequency;	/*!< How often to play periodic announcement */
	int roundingseconds;            /*!< How many seconds do we round to? */
	int holdtime;                   /*!< Current avg holdtime, based on recursive boxcar filter */
	int callscompleted;             /*!< Number of queue calls completed */
	int callsabandoned;             /*!< Number of queue calls abandoned */
	int servicelevel;               /*!< seconds setting for servicelevel*/
	int callscompletedinsl;         /*!< Number of calls answered with servicelevel*/
	char monfmt[8];                 /*!< Format to use when recording calls */
	char sound_next[80];            /*!< Sound file: "Your call is now first in line" (def. queue-youarenext) */
	char sound_thereare[80];        /*!< Sound file: "There are currently" (def. queue-thereare) */
	char sound_calls[80];           /*!< Sound file: "calls waiting to speak to a representative." (def. queue-callswaiting)*/
	char sound_holdtime[80];        /*!< Sound file: "The current estimated total holdtime is" (def. queue-holdtime) */
	char sound_minutes[80];         /*!< Sound file: "minutes." (def. queue-minutes) */
	char sound_lessthan[80];        /*!< Sound file: "less-than" (def. queue-lessthan) */
	char sound_seconds[80];         /*!< Sound file: "seconds." (def. queue-seconds) */
	char sound_thanks[80];          /*!< Sound file: "Thank you for your patience." (def. queue-thankyou) */
	char sound_reporthold[80];	/*!< Sound file: "Hold time" (def. queue-reporthold) */
	char sound_periodicannounce[80];/*!< Sound file: Custom announce, no default */

	int count;			/*!< How many entries */
	int maxlen;			/*!< Max number of entries */
	int wrapuptime;			/*!< Wrapup Time */

	int retry;			/*!< Retry calling everyone after this amount of time */
	int timeout;			/*!< How long to wait for an answer */
	int weight;                     /*!< Respective weight */
	
	/* Queue strategy things */
	int rrpos;			/*!< Round Robin - position */
	int memberdelay;		/*!< Seconds to delay connecting member to caller */

	struct member *members;		/*!< Head of the list of members */
	struct queue_ent *head;		/*!< Head of the list of callers */
	struct opbx_call_queue *next;	/*!< Next call queue */
};

static struct opbx_call_queue *queues = NULL;
OPBX_MUTEX_DEFINE_STATIC(qlock);

static void set_queue_result(struct opbx_channel *chan, enum queue_result res)
{
	int i;

	for (i = 0; i < sizeof(queue_results) / sizeof(queue_results[0]); i++) {
		if (queue_results[i].id == res) {
			pbx_builtin_setvar_helper(chan, "QUEUESTATUS", queue_results[i].text);
			return;
		}
	}
}

static char *int2strat(int strategy)
{
	int x;
	for (x=0;x<sizeof(strategies) / sizeof(strategies[0]);x++) {
		if (strategy == strategies[x].strategy)
			return strategies[x].name;
	}
	return "<unknown>";
}

static int strat2int(const char *strategy)
{
	int x;
	for (x=0;x<sizeof(strategies) / sizeof(strategies[0]);x++) {
		if (!strcasecmp(strategy, strategies[x].name))
			return strategies[x].strategy;
	}
	return -1;
}

/*! \brief Insert the 'new' entry after the 'prev' entry of queue 'q' */
static inline void insert_entry(struct opbx_call_queue *q, struct queue_ent *prev, struct queue_ent *new, int *pos)
{
	struct queue_ent *cur;

	if (!q || !new)
		return;
	if (prev) {
		cur = prev->next;
		prev->next = new;
	} else {
		cur = q->head;
		q->head = new;
	}
	new->next = cur;
	new->parent = q;
	new->pos = ++(*pos);
	new->opos = *pos;
}

enum queue_member_status {
	QUEUE_NO_MEMBERS,
	QUEUE_NO_REACHABLE_MEMBERS,
	QUEUE_NORMAL
};

static enum queue_member_status get_member_status(const struct opbx_call_queue *q)
{
	struct member *member;
	enum queue_member_status result = QUEUE_NO_MEMBERS;

	for (member = q->members; member; member = member->next) {
		switch (member->status) {
		case OPBX_DEVICE_INVALID:
			/* nothing to do */
			break;
		case OPBX_DEVICE_UNAVAILABLE:
			result = QUEUE_NO_REACHABLE_MEMBERS;
			break;
		default:
			return QUEUE_NORMAL;
		}
	}
	
	return result;
}

struct statechange {
	int state;
	char dev[0];
};

static void *changethread(void *data)
{
	struct opbx_call_queue *q;
	struct statechange *sc = data;
	struct member *cur;
	char *loc;
	char *technology;

	technology = opbx_strdupa(sc->dev);
	loc = strchr(technology, '/');
	if (loc) {
		*loc = '\0';
		loc++;
	} else {
		free(sc);
		return NULL;
	}
	if (option_debug)
		opbx_log(LOG_DEBUG, "Device '%s/%s' changed to state '%d' (%s)\n", technology, loc, sc->state, devstate2str(sc->state));
	opbx_mutex_lock(&qlock);
	for (q = queues; q; q = q->next) {
		opbx_mutex_lock(&q->lock);
		cur = q->members;
		while(cur) {
			if (!strcasecmp(sc->dev, cur->interface)) {
				if (cur->status != sc->state) {
					cur->status = sc->state;
					if (!q->maskmemberstatus) {
						manager_event(EVENT_FLAG_AGENT, "QueueMemberStatus",
							"Queue: %s\r\n"
							"Location: %s\r\n"
							"Membership: %s\r\n"
							"Penalty: %d\r\n"
							"CallsTaken: %d\r\n"
							"LastCall: %ld\r\n"
							"Status: %d\r\n"
							"Paused: %d\r\n",
						q->name, cur->interface, cur->dynamic ? "dynamic" : "static",
						cur->penalty, cur->calls, cur->lastcall, cur->status, cur->paused);
					}
				}
			}
			cur = cur->next;
		}
		opbx_mutex_unlock(&q->lock);
	}
	opbx_mutex_unlock(&qlock);
	free(sc);
	return NULL;
}

static int statechange_queue(const char *dev, int state, void *ign)
{
	/* Avoid potential for deadlocks by spawning a new thread to handle
	   the event */
	struct statechange *sc;
	pthread_t t;
	pthread_attr_t attr;

	sc = malloc(sizeof(struct statechange) + strlen(dev) + 1);
	if (sc) {
		sc->state = state;
		strcpy(sc->dev, dev);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (opbx_pthread_create(&t, &attr, changethread, sc)) {
			opbx_log(LOG_WARNING, "Failed to create update thread!\n");
			free(sc);
		}
	}
	return 0;
}

static struct member *create_queue_member(char *interface, int penalty, int paused)
{
	struct member *cur;
	
	/* Add a new member */

	cur = malloc(sizeof(struct member));

	if (cur) {
		memset(cur, 0, sizeof(struct member));
		cur->penalty = penalty;
		cur->paused = paused;
		opbx_copy_string(cur->interface, interface, sizeof(cur->interface));
		if (!strchr(cur->interface, '/'))
			opbx_log(LOG_WARNING, "No location at interface '%s'\n", interface);
		cur->status = opbx_device_state(interface);
	}

	return cur;
}

static struct opbx_call_queue *alloc_queue(const char *queuename)
{
	struct opbx_call_queue *q;

	q = malloc(sizeof(*q));
	if (q) {
		memset(q, 0, sizeof(*q));
		opbx_mutex_init(&q->lock);
		opbx_copy_string(q->name, queuename, sizeof(q->name));
	}
	return q;
}

static void init_queue(struct opbx_call_queue *q)
{
	q->dead = 0;
	q->retry = DEFAULT_RETRY;
	q->timeout = -1;
	q->maxlen = 0;
	q->announcefrequency = 0;
	q->announceholdtime = 0;
	q->roundingseconds = 0; /* Default - don't announce seconds */
	q->servicelevel = 0;
	q->moh[0] = '\0';
	q->announce[0] = '\0';
	q->context[0] = '\0';
	q->monfmt[0] = '\0';
	q->periodicannouncefrequency = 0;
	opbx_copy_string(q->sound_next, "queue-youarenext", sizeof(q->sound_next));
	opbx_copy_string(q->sound_thereare, "queue-thereare", sizeof(q->sound_thereare));
	opbx_copy_string(q->sound_calls, "queue-callswaiting", sizeof(q->sound_calls));
	opbx_copy_string(q->sound_holdtime, "queue-holdtime", sizeof(q->sound_holdtime));
	opbx_copy_string(q->sound_minutes, "queue-minutes", sizeof(q->sound_minutes));
	opbx_copy_string(q->sound_seconds, "queue-seconds", sizeof(q->sound_seconds));
	opbx_copy_string(q->sound_thanks, "queue-thankyou", sizeof(q->sound_thanks));
	opbx_copy_string(q->sound_lessthan, "queue-less-than", sizeof(q->sound_lessthan));
	opbx_copy_string(q->sound_reporthold, "queue-reporthold", sizeof(q->sound_reporthold));
	opbx_copy_string(q->sound_periodicannounce, "queue-periodic-announce", sizeof(q->sound_periodicannounce));
}

static void clear_queue(struct opbx_call_queue *q)
{
	q->holdtime = 0;
	q->callscompleted = 0;
	q->callsabandoned = 0;
	q->callscompletedinsl = 0;
	q->wrapuptime = 0;
}

/*! \brief Configure a queue parameter.
\par
   For error reporting, line number is passed for .conf static configuration.
   For Realtime queues, linenum is -1.
   The failunknown flag is set for config files (and static realtime) to show
   errors for unknown parameters. It is cleared for dynamic realtime to allow
   extra fields in the tables. */
static void queue_set_param(struct opbx_call_queue *q, const char *param, const char *val, int linenum, int failunknown)
{
	if (!strcasecmp(param, "music") || !strcasecmp(param, "musiconhold")) {
		opbx_copy_string(q->moh, val, sizeof(q->moh));
	} else if (!strcasecmp(param, "announce")) {
		opbx_copy_string(q->announce, val, sizeof(q->announce));
	} else if (!strcasecmp(param, "context")) {
		opbx_copy_string(q->context, val, sizeof(q->context));
	} else if (!strcasecmp(param, "timeout")) {
		q->timeout = atoi(val);
		if (q->timeout < 0)
			q->timeout = DEFAULT_TIMEOUT;
	} else if (!strcasecmp(param, "monitor-join")) {
		q->monjoin = opbx_true(val);
	} else if (!strcasecmp(param, "monitor-format")) {
		opbx_copy_string(q->monfmt, val, sizeof(q->monfmt));
	} else if (!strcasecmp(param, "queue-youarenext")) {
		opbx_copy_string(q->sound_next, val, sizeof(q->sound_next));
	} else if (!strcasecmp(param, "queue-thereare")) {
		opbx_copy_string(q->sound_thereare, val, sizeof(q->sound_thereare));
	} else if (!strcasecmp(param, "queue-callswaiting")) {
		opbx_copy_string(q->sound_calls, val, sizeof(q->sound_calls));
	} else if (!strcasecmp(param, "queue-holdtime")) {
		opbx_copy_string(q->sound_holdtime, val, sizeof(q->sound_holdtime));
	} else if (!strcasecmp(param, "queue-minutes")) {
		opbx_copy_string(q->sound_minutes, val, sizeof(q->sound_minutes));
	} else if (!strcasecmp(param, "queue-seconds")) {
		opbx_copy_string(q->sound_seconds, val, sizeof(q->sound_seconds));
	} else if (!strcasecmp(param, "queue-lessthan")) {
		opbx_copy_string(q->sound_lessthan, val, sizeof(q->sound_lessthan));
	} else if (!strcasecmp(param, "queue-thankyou")) {
		opbx_copy_string(q->sound_thanks, val, sizeof(q->sound_thanks));
	} else if (!strcasecmp(param, "queue-reporthold")) {
		opbx_copy_string(q->sound_reporthold, val, sizeof(q->sound_reporthold));
	} else if (!strcasecmp(param, "announce-frequency")) {
		q->announcefrequency = atoi(val);
	} else if (!strcasecmp(param, "announce-round-seconds")) {
		q->roundingseconds = atoi(val);
		if (q->roundingseconds>60 || q->roundingseconds<0) {
			if (linenum >= 0) {
				opbx_log(LOG_WARNING, "'%s' isn't a valid value for %s "
					"using 0 instead for queue '%s' at line %d of queues.conf\n",
					val, param, q->name, linenum);
			} else {
				opbx_log(LOG_WARNING, "'%s' isn't a valid value for %s "
					"using 0 instead for queue '%s'\n", val, param, q->name);
			}
			q->roundingseconds=0;
		}
	} else if (!strcasecmp(param, "announce-holdtime")) {
		if (!strcasecmp(val, "once"))
			q->announceholdtime = ANNOUNCEHOLDTIME_ONCE;
		else if (opbx_true(val))
			q->announceholdtime = ANNOUNCEHOLDTIME_ALWAYS;
		else
			q->announceholdtime = 0;
	 } else if (!strcasecmp(param, "periodic-announce")) {
		opbx_copy_string(q->sound_periodicannounce, val, sizeof(q->sound_periodicannounce));
	} else if (!strcasecmp(param, "periodic-announce-frequency")) {
		q->periodicannouncefrequency = atoi(val);
	} else if (!strcasecmp(param, "retry")) {
		q->retry = atoi(val);
		if (q->retry < 0)
			q->retry = DEFAULT_RETRY;
	} else if (!strcasecmp(param, "wrapuptime")) {
		q->wrapuptime = atoi(val);
	} else if (!strcasecmp(param, "maxlen")) {
		q->maxlen = atoi(val);
		if (q->maxlen < 0)
			q->maxlen = 0;
	} else if (!strcasecmp(param, "servicelevel")) {
		q->servicelevel= atoi(val);
	} else if (!strcasecmp(param, "strategy")) {
		q->strategy = strat2int(val);
		if (q->strategy < 0) {
			opbx_log(LOG_WARNING, "'%s' isn't a valid strategy for queue '%s', using ringall instead\n",
				val, q->name);
			q->strategy = 0;
		}
	} else if (!strcasecmp(param, "joinempty")) {
		if (!strcasecmp(val, "strict"))
			q->joinempty = QUEUE_EMPTY_STRICT;
		else if (opbx_true(val))
			q->joinempty = QUEUE_EMPTY_NORMAL;
		else
			q->joinempty = 0;
	} else if (!strcasecmp(param, "leavewhenempty")) {
		if (!strcasecmp(val, "strict"))
			q->leavewhenempty = QUEUE_EMPTY_STRICT;
		else if (opbx_true(val))
			q->leavewhenempty = QUEUE_EMPTY_NORMAL;
		else
			q->leavewhenempty = 0;
	} else if (!strcasecmp(param, "eventmemberstatus")) {
		q->maskmemberstatus = !opbx_true(val);
	} else if (!strcasecmp(param, "eventwhencalled")) {
		q->eventwhencalled = opbx_true(val);
	} else if (!strcasecmp(param, "reportholdtime")) {
		q->reportholdtime = opbx_true(val);
	} else if (!strcasecmp(param, "memberdelay")) {
		q->memberdelay = atoi(val);
	} else if (!strcasecmp(param, "weight")) {
		q->weight = atoi(val);
		if (q->weight)
			use_weight++;
		/* With Realtime queues, if the last queue using weights is deleted in realtime,
		   we will not see any effect on use_weight until next reload. */
	} else if (!strcasecmp(param, "timeoutrestart")) {
		q->timeoutrestart = opbx_true(val);
	} else if(failunknown) {
		if (linenum >= 0) {
			opbx_log(LOG_WARNING, "Unknown keyword in queue '%s': %s at line %d of queues.conf\n",
				q->name, param, linenum);
		} else {
			opbx_log(LOG_WARNING, "Unknown keyword in queue '%s': %s\n", q->name, param);
		}
	}
}

static void rt_handle_member_record(struct opbx_call_queue *q, char *interface, const char *penalty_str)
{
	struct member *m, *prev_m;
	int penalty = 0;

	if(penalty_str) {
		penalty = atoi(penalty_str);
		if(penalty < 0)
			penalty = 0;
	}

	/* Find the member, or the place to put a new one. */
	prev_m = NULL;
	m = q->members;
	while (m && strcmp(m->interface, interface)) {
		prev_m = m;
		m = m->next;
	}

	/* Create a new one if not found, else update penalty */
	if (!m) {
		m = create_queue_member(interface, penalty, 0);
		if (m) {
			m->dead = 0;
			if (prev_m) {
				prev_m->next = m;
			} else {
				q->members = m;
			}
		}
	} else {
		m->dead = 0;	/* Do not delete this one. */
		m->penalty = penalty;
	}
}


/*! \brief Reload a single queue via realtime.
  \return Return the queue, or NULL if it doesn't exist.
  \note Should be called with the global qlock locked.
   When found, the queue is returned with q->lock locked. */
static struct opbx_call_queue *reload_queue_rt(const char *queuename, struct opbx_variable *queue_vars, struct opbx_config *member_config)
{
	struct opbx_variable *v;
	struct opbx_call_queue *q, *prev_q;
	struct member *m, *prev_m, *next_m;
	char *interface;
	char *tmp, *tmp_name;
	char tmpbuf[64];	/* Must be longer than the longest queue param name. */

	/* Find the queue in the in-core list (we will create a new one if not found). */
	q = queues;
	prev_q = NULL;
	while (q) {
		if (!strcasecmp(q->name, queuename)) {
			break;
		}
		q = q->next;
		prev_q = q;
	}

	/* Static queues override realtime. */
	if (q) {
		opbx_mutex_lock(&q->lock);
		if (!q->realtime) {
			if (q->dead) {
				opbx_mutex_unlock(&q->lock);
				return NULL;
			} else {
				return q;
			}
		}
	} else if (!member_config)
		/* Not found in the list, and it's not realtime ... */
		return NULL;

	/* Check if queue is defined in realtime. */
	if (!queue_vars) {
		/* Delete queue from in-core list if it has been deleted in realtime. */
		if (q) {
			/*! \note Hmm, can't seem to distinguish a DB failure from a not
			   found condition... So we might delete an in-core queue
			   in case of DB failure. */
			opbx_log(LOG_DEBUG, "Queue %s not found in realtime.\n", queuename);

			q->dead = 1;
			/* Delete if unused (else will be deleted when last caller leaves). */
			if (!q->count) {
				/* Delete. */
				if (!prev_q) {
					queues = q->next;
				} else {
					prev_q->next = q->next;
				}
				opbx_mutex_unlock(&q->lock);
				free(q);
			} else
				opbx_mutex_unlock(&q->lock);
		}
		return NULL;
	}

	/* Create a new queue if an in-core entry does not exist yet. */
	if (!q) {
		q = alloc_queue(queuename);
		if (!q)
			return NULL;
		opbx_mutex_lock(&q->lock);
		clear_queue(q);
		q->realtime = 1;
		q->next = queues;
		queues = q;
	}
	init_queue(q);		/* Ensure defaults for all parameters not set explicitly. */

	v = queue_vars;
	memset(tmpbuf, 0, sizeof(tmpbuf));
	while(v) {
		/* Convert to dashes `-' from underscores `_' as the latter are more SQL friendly. */
		if((tmp = strchr(v->name, '_')) != NULL) {
			opbx_copy_string(tmpbuf, v->name, sizeof(tmpbuf));
			tmp_name = tmpbuf;
			tmp = tmp_name;
			while((tmp = strchr(tmp, '_')) != NULL)
				*tmp++ = '-';
		} else
			tmp_name = v->name;
		queue_set_param(q, tmp_name, v->value, -1, 0);
		v = v->next;
	}

	/* Temporarily set members dead so we can detect deleted ones. */
	m = q->members;
	while (m) {
		m->dead = 1;
		m = m->next;
	}

	interface = opbx_category_browse(member_config, NULL);
	while (interface) {
		rt_handle_member_record(q, interface, opbx_variable_retrieve(member_config, interface, "penalty"));
		interface = opbx_category_browse(member_config, interface);
	}

	/* Delete all realtime members that have been deleted in DB. */
	m = q->members;
	prev_m = NULL;
	while (m) {
		next_m = m->next;
		if (m->dead) {
			if (prev_m) {
				prev_m->next = next_m;
			} else {
				q->members = next_m;
			}
			free(m);
		} else {
			prev_m = m;
		}
		m = next_m;
	}

	return q;
}

static int join_queue(char *queuename, struct queue_ent *qe, enum queue_result *reason)
{
	struct opbx_variable *queue_vars = NULL;
	struct opbx_config *member_config = NULL;
	struct opbx_call_queue *q;
	struct queue_ent *cur, *prev = NULL;
	int res = -1;
	int pos = 0;
	int inserted = 0;
	enum queue_member_status stat;

	/*! \note Load from realtime before taking the global qlock, to avoid blocking all
	   queue operations while waiting for the DB.

	   This will be two separate database transactions, so we might
	   see queue parameters as they were before another process
	   changed the queue and member list as it was after the change.
	   Thus we might see an empty member list when a queue is
	   deleted. In practise, this is unlikely to cause a problem. */
	queue_vars = opbx_load_realtime("queues", "name", queuename, NULL);
	if (queue_vars) {
		member_config = opbx_load_realtime_multientry("queue_members", "interface LIKE", "%", "queue_name", queuename, NULL);
		if (!member_config) {
			opbx_log(LOG_ERROR, "no queue_members defined in your config (extconfig.conf).\n");
			return res;
		}
	}

	opbx_mutex_lock(&qlock);
	q = reload_queue_rt(queuename, queue_vars, member_config);
	/* Note: If found, reload_queue_rt() returns with q->lock locked. */
	if(member_config)
		opbx_config_destroy(member_config);
	if(queue_vars)
		opbx_variables_destroy(queue_vars);

	if (!q) {
		opbx_mutex_unlock(&qlock);
		return res;
	}

	/* This is our one */
	stat = get_member_status(q);
	if (!q->joinempty && (stat == QUEUE_NO_MEMBERS))
		*reason = QUEUE_JOINEMPTY;
	else if ((q->joinempty == QUEUE_EMPTY_STRICT) && (stat == QUEUE_NO_REACHABLE_MEMBERS))
		*reason = QUEUE_JOINUNAVAIL;
	else if (q->maxlen && (q->count >= q->maxlen))
		*reason = QUEUE_FULL;
	else {
		/* There's space for us, put us at the right position inside
		 * the queue. 
		 * Take into account the priority of the calling user */
		inserted = 0;
		prev = NULL;
		cur = q->head;
		while(cur) {
			/* We have higher priority than the current user, enter
			 * before him, after all the other users with priority
			 * higher or equal to our priority. */
			if ((!inserted) && (qe->prio > cur->prio)) {
				insert_entry(q, prev, qe, &pos);
				inserted = 1;
			}
			cur->pos = ++pos;
			prev = cur;
			cur = cur->next;
		}
		/* No luck, join at the end of the queue */
		if (!inserted)
			insert_entry(q, prev, qe, &pos);
		opbx_copy_string(qe->moh, q->moh, sizeof(qe->moh));
		opbx_copy_string(qe->announce, q->announce, sizeof(qe->announce));
		opbx_copy_string(qe->context, q->context, sizeof(qe->context));
		q->count++;
		res = 0;
		manager_event(EVENT_FLAG_CALL, "Join", 
			      "Channel: %s\r\nCallerID: %s\r\nCallerIDName: %s\r\nQueue: %s\r\nPosition: %d\r\nCount: %d\r\n",
			      qe->chan->name, 
			      qe->chan->cid.cid_num ? qe->chan->cid.cid_num : "unknown",
			      qe->chan->cid.cid_name ? qe->chan->cid.cid_name : "unknown",
			      q->name, qe->pos, q->count );
#if 0
opbx_log(LOG_NOTICE, "Queue '%s' Join, Channel '%s', Position '%d'\n", q->name, qe->chan->name, qe->pos );
#endif
	}
	opbx_mutex_unlock(&q->lock);
	opbx_mutex_unlock(&qlock);
	return res;
}

static void free_members(struct opbx_call_queue *q, int all)
{
	/* Free non-dynamic members */
	struct member *curm, *next, *prev;

	curm = q->members;
	prev = NULL;
	while(curm) {
		next = curm->next;
		if (all || !curm->dynamic) {
			if (prev)
				prev->next = next;
			else
				q->members = next;
			free(curm);
		} else 
			prev = curm;
		curm = next;
	}
}

static void destroy_queue(struct opbx_call_queue *q)
{
	struct opbx_call_queue *cur, *prev = NULL;

	opbx_mutex_lock(&qlock);
	for (cur = queues; cur; cur = cur->next) {
		if (cur == q) {
			if (prev)
				prev->next = cur->next;
			else
				queues = cur->next;
		} else {
			prev = cur;
		}
	}
	opbx_mutex_unlock(&qlock);
	free_members(q, 1);
        opbx_mutex_destroy(&q->lock);
	free(q);
}

static int play_file(struct opbx_channel *chan, char *filename)
{
	int res;

	opbx_stopstream(chan);
	res = opbx_streamfile(chan, filename, chan->language);

	if (!res)
		res = opbx_waitstream(chan, OPBX_DIGIT_ANY);
	else
		res = 0;

	opbx_stopstream(chan);

	return res;
}

static int valid_exit(struct queue_ent *qe, char digit)
{
	int digitlen = strlen(qe->digits);

	/* Prevent possible buffer overflow */
	if (digitlen < sizeof(qe->digits) - 2) {
		qe->digits[digitlen] = digit;
		qe->digits[digitlen + 1] = '\0';
	} else {
		qe->digits[0] = '\0';
		return 0;
	}

 	/* If there's no context to goto, short-circuit */
	if (opbx_strlen_zero(qe->context))
		return 0;

	/* If the extension is bad, then reset the digits to blank */
	if (!opbx_canmatch_extension(qe->chan, qe->context, qe->digits, 1, qe->chan->cid.cid_num)) {
		qe->digits[0] = '\0';
		return 0;
	}

	/* We have an exact match */
	if (!opbx_goto_if_exists(qe->chan, qe->context, qe->digits, 1)) {
		/* Return 1 on a successful goto */
		return 1;
	}
	return 0;
}

static int say_position(struct queue_ent *qe)
{
	int res = 0, avgholdmins, avgholdsecs;
	time_t now;

	/* Check to see if this is ludicrous -- if we just announced position, don't do it again*/
	time(&now);
	if ( (now - qe->lopbx_pos) < 15 )
		return 0;

	/* If either our position has changed, or we are over the freq timer, say position */
	if ( (qe->lopbx_pos_said == qe->pos) && ((now - qe->lopbx_pos) < qe->parent->announcefrequency) )
		return 0;

	opbx_moh_stop(qe->chan);
	/* Say we're next, if we are */
	if (qe->pos == 1) {
		res = play_file(qe->chan, qe->parent->sound_next);
		if (res && valid_exit(qe, res))
			goto playout;
		else
			goto posout;
	} else {
		res = play_file(qe->chan, qe->parent->sound_thereare);
		if (res && valid_exit(qe, res))
			goto playout;
		res = opbx_say_number(qe->chan, qe->pos, OPBX_DIGIT_ANY, qe->chan->language, (char *) NULL); /* Needs gender */
		if (res && valid_exit(qe, res))
			goto playout;
		res = play_file(qe->chan, qe->parent->sound_calls);
		if (res && valid_exit(qe, res))
			goto playout;
	}
	/* Round hold time to nearest minute */
	avgholdmins = abs(( (qe->parent->holdtime + 30) - (now - qe->start) ) / 60);

	/* If they have specified a rounding then round the seconds as well */
	if(qe->parent->roundingseconds) {
		avgholdsecs = (abs(( (qe->parent->holdtime + 30) - (now - qe->start) )) - 60 * avgholdmins) / qe->parent->roundingseconds;
		avgholdsecs*= qe->parent->roundingseconds;
	} else {
		avgholdsecs=0;
	}

	if (option_verbose > 2)
		opbx_verbose(VERBOSE_PREFIX_3 "Hold time for %s is %d minutes %d seconds\n", qe->parent->name, avgholdmins, avgholdsecs);

	/* If the hold time is >1 min, if it's enabled, and if it's not
	   supposed to be only once and we have already said it, say it */
	if ((avgholdmins+avgholdsecs) > 0 && (qe->parent->announceholdtime) &&
	    (!(qe->parent->announceholdtime == ANNOUNCEHOLDTIME_ONCE) && qe->lopbx_pos)) {
		res = play_file(qe->chan, qe->parent->sound_holdtime);
		if (res && valid_exit(qe, res))
			goto playout;

		if (avgholdmins>0) {
			if (avgholdmins < 2) {
				res = play_file(qe->chan, qe->parent->sound_lessthan);
				if (res && valid_exit(qe, res))
					goto playout;

				res = opbx_say_number(qe->chan, 2, OPBX_DIGIT_ANY, qe->chan->language, (char *)NULL);
				if (res && valid_exit(qe, res))
					goto playout;
			} else {
				res = opbx_say_number(qe->chan, avgholdmins, OPBX_DIGIT_ANY, qe->chan->language, (char*) NULL);
				if (res && valid_exit(qe, res))
					goto playout;
			}
			
			res = play_file(qe->chan, qe->parent->sound_minutes);
			if (res && valid_exit(qe, res))
				goto playout;
		}
		if (avgholdsecs>0) {
			res = opbx_say_number(qe->chan, avgholdsecs, OPBX_DIGIT_ANY, qe->chan->language, (char*) NULL);
			if (res && valid_exit(qe, res))
				goto playout;

			res = play_file(qe->chan, qe->parent->sound_seconds);
			if (res && valid_exit(qe, res))
				goto playout;
		}

	}

 posout:
	if (option_verbose > 2)
		opbx_verbose(VERBOSE_PREFIX_3 "Told %s in %s their queue position (which was %d)\n",
			    qe->chan->name, qe->parent->name, qe->pos);
	res = play_file(qe->chan, qe->parent->sound_thanks);

 playout:
	/* Set our lopbx_pos indicators */
 	qe->lopbx_pos = now;
	qe->lopbx_pos_said = qe->pos;
	opbx_moh_start(qe->chan, qe->moh);

	return res;
}

static void recalc_holdtime(struct queue_ent *qe)
{
	int oldvalue, newvalue;

	/* Calculate holdtime using a recursive boxcar filter */
	/* Thanks to SRT for this contribution */
	/* 2^2 (4) is the filter coefficient; a higher exponent would give old entries more weight */

	newvalue = time(NULL) - qe->start;

	opbx_mutex_lock(&qe->parent->lock);
	if (newvalue <= qe->parent->servicelevel)
       		qe->parent->callscompletedinsl++;
	oldvalue = qe->parent->holdtime;
	qe->parent->holdtime = (((oldvalue << 2) - oldvalue) + newvalue) >> 2;
	opbx_mutex_unlock(&qe->parent->lock);
}


static void leave_queue(struct queue_ent *qe)
{
	struct opbx_call_queue *q;
	struct queue_ent *cur, *prev = NULL;
	int pos = 0;

	q = qe->parent;
	if (!q)
		return;
	opbx_mutex_lock(&q->lock);

	prev = NULL;
	cur = q->head;
	while(cur) {
		if (cur == qe) {
			q->count--;

			/* Take us out of the queue */
			manager_event(EVENT_FLAG_CALL, "Leave",
				"Channel: %s\r\nQueue: %s\r\nCount: %d\r\n",
				qe->chan->name, q->name,  q->count);
#if 0
opbx_log(LOG_NOTICE, "Queue '%s' Leave, Channel '%s'\n", q->name, qe->chan->name );
#endif
			/* Take us out of the queue */
			if (prev)
				prev->next = cur->next;
			else
				q->head = cur->next;
		} else {
			/* Renumber the people after us in the queue based on a new count */
			cur->pos = ++pos;
			prev = cur;
		}
		cur = cur->next;
	}
	opbx_mutex_unlock(&q->lock);
	if (q->dead && !q->count) {	
		/* It's dead and nobody is in it, so kill it */
		destroy_queue(q);
	}
}

/* Hang up a list of outgoing calls */
static void hangupcalls(struct localuser *outgoing, struct opbx_channel *exception)
{
	struct localuser *oo;

	while(outgoing) {
		/* Hangup any existing lines we have open */
		if (outgoing->chan && (outgoing->chan != exception))
			opbx_hangup(outgoing->chan);
		oo = outgoing;
		outgoing=outgoing->next;
		free(oo);
	}
}

static int update_status(struct opbx_call_queue *q, struct member *member, int status)
{
	struct member *cur;

	/* Since a reload could have taken place, we have to traverse the list to
		be sure it's still valid */
	opbx_mutex_lock(&q->lock);
	cur = q->members;
	while(cur) {
		if (member == cur) {
			cur->status = status;
			if (!q->maskmemberstatus) {
				manager_event(EVENT_FLAG_AGENT, "QueueMemberStatus",
					"Queue: %s\r\n"
					"Location: %s\r\n"
					"Membership: %s\r\n"
					"Penalty: %d\r\n"
					"CallsTaken: %d\r\n"
					"LastCall: %ld\r\n"
					"Status: %d\r\n"
					"Paused: %d\r\n",
				q->name, cur->interface, cur->dynamic ? "dynamic" : "static",
				cur->penalty, cur->calls, cur->lastcall, cur->status, cur->paused);
			}
			break;
		}
		cur = cur->next;
	}
	opbx_mutex_unlock(&q->lock);
	return 0;
}

static int update_dial_status(struct opbx_call_queue *q, struct member *member, int status)
{
	if (status == OPBX_CAUSE_BUSY)
		status = OPBX_DEVICE_BUSY;
	else if (status == OPBX_CAUSE_UNREGISTERED)
		status = OPBX_DEVICE_UNAVAILABLE;
	else if (status == OPBX_CAUSE_NOSUCHDRIVER)
		status = OPBX_DEVICE_INVALID;
	else
		status = OPBX_DEVICE_UNKNOWN;
	return update_status(q, member, status);
}

/* traverse all defined queues which have calls waiting and contain this member
   return 0 if no other queue has precedence (higher weight) or 1 if found  */
static int compare_weight(struct opbx_call_queue *rq, struct member *member)
{
	struct opbx_call_queue *q;
	struct member *mem;
	int found = 0;
	
	/* &qlock and &rq->lock already set by try_calling()
	 * to solve deadlock */
	for (q = queues; q; q = q->next) {
		if (q == rq) /* don't check myself, could deadlock */
			continue; 
		opbx_mutex_lock(&q->lock);
		if (q->count && q->members) {
			for (mem = q->members; mem; mem = mem->next) {
				if (mem == member) {
					opbx_log(LOG_DEBUG, "Found matching member %s in queue '%s'\n", mem->interface, q->name);
					if (q->weight > rq->weight) {
						opbx_log(LOG_DEBUG, "Queue '%s' (weight %d, calls %d) is preferred over '%s' (weight %d, calls %d)\n", q->name, q->weight, q->count, rq->name, rq->weight, rq->count);
						found = 1;
						break;
					}
				}
			}
		}
		opbx_mutex_unlock(&q->lock);
		if (found) 
			break;
	}
	opbx_mutex_unlock(&qlock);
	return found;
}

static int ring_entry(struct queue_ent *qe, struct localuser *tmp, int *busies)
{
	int res;
	int status;
	char tech[256];
	char *location;

	if (qe->parent->wrapuptime && (time(NULL) - tmp->lastcall < qe->parent->wrapuptime)) {
		if (option_debug)
			opbx_log(LOG_DEBUG, "Wrapuptime not yet expired for %s\n", tmp->interface);
		if (qe->chan->cdr)
			opbx_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		(*busies)++;
		return 0;
	}
	
	if (tmp->member->paused) {
		if (option_debug)
			opbx_log(LOG_DEBUG, "%s paused, can't receive call\n", tmp->interface);
		if (qe->chan->cdr)
			opbx_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		return 0;
	}
	if (use_weight && compare_weight(qe->parent,tmp->member)) {
		opbx_log(LOG_DEBUG, "Priority queue delaying call to %s:%s\n", qe->parent->name, tmp->interface);
		if (qe->chan->cdr)
			opbx_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		(*busies)++;
		return 0;
	}

	opbx_copy_string(tech, tmp->interface, sizeof(tech));
	if ((location = strchr(tech, '/')))
		*location++ = '\0';
	else
		location = "";

	/* Request the peer */
	tmp->chan = opbx_request(tech, qe->chan->nativeformats, location, &status);
	if (!tmp->chan) {			/* If we can't, just go on to the next call */
#if 0
		opbx_log(LOG_NOTICE, "Unable to create channel of type '%s' for Queue\n", cur->tech);
#endif			
		if (qe->chan->cdr)
			opbx_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		update_dial_status(qe->parent, tmp->member, status);
		(*busies)++;
		return 0;
	} else if (status != tmp->oldstatus) 
		update_dial_status(qe->parent, tmp->member, status);
	
	tmp->chan->appl = "AppQueue";
	tmp->chan->data = "(Outgoing Line)";
	tmp->chan->whentohangup = 0;
	if (tmp->chan->cid.cid_num)
		free(tmp->chan->cid.cid_num);
	tmp->chan->cid.cid_num = NULL;
	if (tmp->chan->cid.cid_name)
		free(tmp->chan->cid.cid_name);
	tmp->chan->cid.cid_name = NULL;
	if (tmp->chan->cid.cid_ani)
		free(tmp->chan->cid.cid_ani);
	tmp->chan->cid.cid_ani = NULL;
	if (qe->chan->cid.cid_num)
		tmp->chan->cid.cid_num = strdup(qe->chan->cid.cid_num);
	if (qe->chan->cid.cid_name)
		tmp->chan->cid.cid_name = strdup(qe->chan->cid.cid_name);
	if (qe->chan->cid.cid_ani)
		tmp->chan->cid.cid_ani = strdup(qe->chan->cid.cid_ani);

	/* Inherit specially named variables from parent channel */
	opbx_channel_inherit_variables(qe->chan, tmp->chan);

	/* Presense of ADSI CPE on outgoing channel follows ours */
	tmp->chan->adsicpe = qe->chan->adsicpe;

	/* Place the call, but don't wait on the answer */
	res = opbx_call(tmp->chan, location, 0);
	if (res) {
		/* Again, keep going even if there's an error */
		if (option_debug)
			opbx_log(LOG_DEBUG, "ast call on peer returned %d\n", res);
		else if (option_verbose > 2)
			opbx_verbose(VERBOSE_PREFIX_3 "Couldn't call %s\n", tmp->interface);
		opbx_hangup(tmp->chan);
		tmp->chan = NULL;
		tmp->stillgoing = 0;
		(*busies)++;
		return 0;
	} else {
		if (qe->parent->eventwhencalled) {
			manager_event(EVENT_FLAG_AGENT, "AgentCalled",
						"AgentCalled: %s\r\n"
						"ChannelCalling: %s\r\n"
						"CallerID: %s\r\n"
						"CallerIDName: %s\r\n"
						"Context: %s\r\n"
						"Extension: %s\r\n"
						"Priority: %d\r\n",
						tmp->interface, qe->chan->name,
						tmp->chan->cid.cid_num ? tmp->chan->cid.cid_num : "unknown",
						tmp->chan->cid.cid_name ? tmp->chan->cid.cid_name : "unknown",
						qe->chan->context, qe->chan->exten, qe->chan->priority);
		}
		if (option_verbose > 2)
			opbx_verbose(VERBOSE_PREFIX_3 "Called %s\n", tmp->interface);
	}
	return 1;
}

static int ring_one(struct queue_ent *qe, struct localuser *outgoing, int *busies)
{
	struct localuser *cur;
	struct localuser *best;
	int bestmetric=0;

	do {
		best = NULL;
		cur = outgoing;
		while(cur) {
			if (cur->stillgoing &&					/* Not already done */
				!cur->chan &&					/* Isn't already going */
				(!best || (cur->metric < bestmetric))) {	/* We haven't found one yet, or it's better */
					bestmetric = cur->metric;
					best = cur;
			}
			cur = cur->next;
		}
		if (best) {
			if (!qe->parent->strategy) {
				/* Ring everyone who shares this best metric (for ringall) */
				cur = outgoing;
				while(cur) {
					if (cur->stillgoing && !cur->chan && (cur->metric <= bestmetric)) {
						if (option_debug)
							opbx_log(LOG_DEBUG, "(Parallel) Trying '%s' with metric %d\n", cur->interface, cur->metric);
						ring_entry(qe, cur, busies);
					}
					cur = cur->next;
				}
			} else {
				/* Ring just the best channel */
				if (option_debug)
					opbx_log(LOG_DEBUG, "Trying '%s' with metric %d\n", best->interface, best->metric);
				ring_entry(qe, best, busies);
			}
		}
	} while (best && !best->chan);
	if (!best) {
		if (option_debug)
			opbx_log(LOG_DEBUG, "Nobody left to try ringing in queue\n");
		return 0;
	}
	return 1;
}

static int store_next(struct queue_ent *qe, struct localuser *outgoing)
{
	struct localuser *cur;
	struct localuser *best;
	int bestmetric=0;

	best = NULL;
	cur = outgoing;
	while(cur) {
		if (cur->stillgoing &&					/* Not already done */
			!cur->chan &&					/* Isn't already going */
			(!best || (cur->metric < bestmetric))) {	/* We haven't found one yet, or it's better */
				bestmetric = cur->metric;
				best = cur;
		}
		cur = cur->next;
	}
	if (best) {
		/* Ring just the best channel */
		if (option_debug)
			opbx_log(LOG_DEBUG, "Next is '%s' with metric %d\n", best->interface, best->metric);
		qe->parent->rrpos = best->metric % 1000;
	} else {
		/* Just increment rrpos */
		if (qe->parent->wrapped) {
			/* No more channels, start over */
			qe->parent->rrpos = 0;
		} else {
			/* Prioritize next entry */
			qe->parent->rrpos++;
		}
	}
	qe->parent->wrapped = 0;
	return 0;
}

static int background_file(struct queue_ent *qe, struct opbx_channel *chan, char *filename)
{
	int res;

	opbx_stopstream(chan);
	res = opbx_streamfile(chan, filename, chan->language);

	if (!res) {
		/* Wait for a keypress */
		res = opbx_waitstream(chan, OPBX_DIGIT_ANY);
		if (res <= 0 || !valid_exit(qe, res))
			res = 0;

		/* Stop playback */
		opbx_stopstream(chan);
	} else {
		res = 0;
	}
	
	/*if (res) {
		opbx_log(LOG_WARNING, "opbx_streamfile failed on %s \n", chan->name);
		res = 0;
	}*/

	return res;
}

static int say_periodic_announcement(struct queue_ent *qe)
{
	int res = 0;
	time_t now;

	/* Get the current time */
	time(&now);

	/* Check to see if it is time to announce */
	if ((now - qe->lopbx_periodic_announce_time) < qe->parent->periodicannouncefrequency)
		return 0;

	/* Stop the music on hold so we can play our own file */
	opbx_moh_stop(qe->chan);

	if (option_verbose > 2)
		opbx_verbose(VERBOSE_PREFIX_3 "Playing periodic announcement\n");

	/* play the announcement */
	res = background_file(qe, qe->chan, qe->parent->sound_periodicannounce);

	/* Resume Music on Hold */
	opbx_moh_start(qe->chan, qe->moh);

	/* update lopbx_periodic_announce_time */
	qe->lopbx_periodic_announce_time = now;

	return res;
}

static void record_abandoned(struct queue_ent *qe)
{
	opbx_mutex_lock(&qe->parent->lock);
	qe->parent->callsabandoned++;
	opbx_mutex_unlock(&qe->parent->lock);
}


#define OPBX_MAX_WATCHERS 256

#define BUILD_WATCHERS do { \
		o = outgoing; \
		found = -1; \
		pos = 1; \
		numlines = 0; \
		watchers[0] = in; \
		while(o) { \
			/* Keep track of important channels */ \
			if (o->stillgoing) { \
				stillgoing = 1; \
				if (o->chan) { \
					watchers[pos++] = o->chan; \
					found = 1; \
				} \
			} \
			o = o->next; \
			numlines++; \
		} \
	} while(0)
	
static struct localuser *wait_for_answer(struct queue_ent *qe, struct localuser *outgoing, int *to, char *digit, int prebusies, int caller_disconnect)
{
	char *queue = qe->parent->name;
	struct localuser *o;
	int found;
	int numlines;
	int status;
	int sentringing = 0;
	int numbusies = prebusies;
	int numnochan = 0;
	int stillgoing = 0;
	int orig = *to;
	struct opbx_frame *f;
	struct localuser *peer = NULL;
	struct opbx_channel *watchers[OPBX_MAX_WATCHERS];
	int pos;
	struct opbx_channel *winner;
	struct opbx_channel *in = qe->chan;
	
	while(*to && !peer) {
		BUILD_WATCHERS;
		if ((found < 0) && stillgoing && !qe->parent->strategy) {
			/* On "ringall" strategy we only move to the next penalty level
			   when *all* ringing phones are done in the current penalty level */
			ring_one(qe, outgoing, &numbusies);
			BUILD_WATCHERS;
		}
		if (found < 0) {
			if (numlines == (numbusies + numnochan)) {
				opbx_log(LOG_DEBUG, "Everyone is busy at this time\n");
			} else {
				opbx_log(LOG_NOTICE, "No one is answering queue '%s' (%d/%d/%d)\n", queue, numlines, numbusies, numnochan);
			}
			*to = 0;
			return NULL;
		}
		winner = opbx_waitfor_n(watchers, pos, to);
		o = outgoing;
		while(o) {
			if (o->stillgoing && (o->chan) &&  (o->chan->_state == OPBX_STATE_UP)) {
				if (!peer) {
					if (option_verbose > 2)
						opbx_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
					peer = o;
				}
			} else if (o->chan && (o->chan == winner)) {
				if (!opbx_strlen_zero(o->chan->call_forward)) {
					char tmpchan[256]="";
					char *stuff;
					char *tech;
					opbx_copy_string(tmpchan, o->chan->call_forward, sizeof(tmpchan));
					if ((stuff = strchr(tmpchan, '/'))) {
						*stuff = '\0';
						stuff++;
						tech = tmpchan;
					} else {
						snprintf(tmpchan, sizeof(tmpchan), "%s@%s", o->chan->call_forward, o->chan->context);
						stuff = tmpchan;
						tech = "Local";
					}
					/* Before processing channel, go ahead and check for forwarding */
					if (option_verbose > 2)
						opbx_verbose(VERBOSE_PREFIX_3 "Now forwarding %s to '%s/%s' (thanks to %s)\n", in->name, tech, stuff, o->chan->name);
					/* Setup parameters */
					o->chan = opbx_request(tech, in->nativeformats, stuff, &status);
					if (status != o->oldstatus) 
						update_dial_status(qe->parent, o->member, status);						
					if (!o->chan) {
						opbx_log(LOG_NOTICE, "Unable to create local channel for call forward to '%s/%s'\n", tech, stuff);
						o->stillgoing = 0;
						numnochan++;
					} else {
						if (o->chan->cid.cid_num)
							free(o->chan->cid.cid_num);
						o->chan->cid.cid_num = NULL;
						if (o->chan->cid.cid_name)
							free(o->chan->cid.cid_name);
						o->chan->cid.cid_name = NULL;

						if (in->cid.cid_num) {
							o->chan->cid.cid_num = strdup(in->cid.cid_num);
							if (!o->chan->cid.cid_num)
								opbx_log(LOG_WARNING, "Out of memory\n");	
						}
						if (in->cid.cid_name) {
							o->chan->cid.cid_name = strdup(in->cid.cid_name);
							if (!o->chan->cid.cid_name)
								opbx_log(LOG_WARNING, "Out of memory\n");	
						}
						opbx_copy_string(o->chan->accountcode, in->accountcode, sizeof(o->chan->accountcode));
						o->chan->cdrflags = in->cdrflags;

						if (in->cid.cid_ani) {
							if (o->chan->cid.cid_ani)
								free(o->chan->cid.cid_ani);
							o->chan->cid.cid_ani = malloc(strlen(in->cid.cid_ani) + 1);
							if (o->chan->cid.cid_ani)
								strncpy(o->chan->cid.cid_ani, in->cid.cid_ani, strlen(in->cid.cid_ani) + 1);
							else
								opbx_log(LOG_WARNING, "Out of memory\n");
						}
						if (o->chan->cid.cid_rdnis) 
							free(o->chan->cid.cid_rdnis);
						if (!opbx_strlen_zero(in->macroexten))
							o->chan->cid.cid_rdnis = strdup(in->macroexten);
						else
							o->chan->cid.cid_rdnis = strdup(in->exten);
						if (opbx_call(o->chan, tmpchan, 0)) {
							opbx_log(LOG_NOTICE, "Failed to dial on local channel for call forward to '%s'\n", tmpchan);
							o->stillgoing = 0;
							opbx_hangup(o->chan);
							o->chan = NULL;
							numnochan++;
						}
					}
					/* Hangup the original channel now, in case we needed it */
					opbx_hangup(winner);
					continue;
				}
				f = opbx_read(winner);
				if (f) {
					if (f->frametype == OPBX_FRAME_CONTROL) {
						switch(f->subclass) {
		    			case OPBX_CONTROL_ANSWER:
							/* This is our guy if someone answered. */
							if (!peer) {
								if (option_verbose > 2)
									opbx_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
								peer = o;
							}
							break;
						case OPBX_CONTROL_BUSY:
							if (option_verbose > 2)
								opbx_verbose( VERBOSE_PREFIX_3 "%s is busy\n", o->chan->name);
							o->stillgoing = 0;
							if (in->cdr)
								opbx_cdr_busy(in->cdr);
							opbx_hangup(o->chan);
							o->chan = NULL;
							if (qe->parent->strategy) {
								if (qe->parent->timeoutrestart)
									*to = orig;
								ring_one(qe, outgoing, &numbusies);
							}
							numbusies++;
							break;
						case OPBX_CONTROL_CONGESTION:
							if (option_verbose > 2)
								opbx_verbose( VERBOSE_PREFIX_3 "%s is circuit-busy\n", o->chan->name);
							o->stillgoing = 0;
							if (in->cdr)
								opbx_cdr_busy(in->cdr);
							opbx_hangup(o->chan);
							o->chan = NULL;
							if (qe->parent->strategy) {
								if (qe->parent->timeoutrestart)
									*to = orig;
								ring_one(qe, outgoing, &numbusies);
							}
							numbusies++;
							break;
						case OPBX_CONTROL_RINGING:
							if (option_verbose > 2)
								opbx_verbose( VERBOSE_PREFIX_3 "%s is ringing\n", o->chan->name);
							if (!sentringing) {
#if 0
								opbx_indicate(in, OPBX_CONTROL_RINGING);
#endif								
								sentringing++;
							}
							break;
						case OPBX_CONTROL_OFFHOOK:
							/* Ignore going off hook */
							break;
						default:
							opbx_log(LOG_DEBUG, "Dunno what to do with control type %d\n", f->subclass);
						}
					}
					opbx_frfree(f);
				} else {
					o->stillgoing = 0;
					opbx_hangup(o->chan);
					o->chan = NULL;
					if (qe->parent->strategy) {
						if (qe->parent->timeoutrestart)
							*to = orig;
						ring_one(qe, outgoing, &numbusies);
					}
				}
			}
			o = o->next;
		}
		if (winner == in) {
			f = opbx_read(in);
#if 0
			if (f && (f->frametype != OPBX_FRAME_VOICE))
					printf("Frame type: %d, %d\n", f->frametype, f->subclass);
			else if (!f || (f->frametype != OPBX_FRAME_VOICE))
				printf("Hangup received on %s\n", in->name);
#endif
			if (!f || ((f->frametype == OPBX_FRAME_CONTROL) && (f->subclass == OPBX_CONTROL_HANGUP))) {
				/* Got hung up */
				*to=-1;
				if (f)
					opbx_frfree(f);
				return NULL;
			}
			if ((f->frametype == OPBX_FRAME_DTMF) && caller_disconnect && (f->subclass == '*')) {
				if (option_verbose > 3)
					opbx_verbose(VERBOSE_PREFIX_3 "User hit %c to disconnect call.\n", f->subclass);
				*to=0;
				opbx_frfree(f);
				return NULL;
			}
			if ((f->frametype == OPBX_FRAME_DTMF) && (f->subclass != '*') && valid_exit(qe, f->subclass)) {
				if (option_verbose > 3)
					opbx_verbose(VERBOSE_PREFIX_3 "User pressed digit: %c\n", f->subclass);
				*to=0;
				*digit=f->subclass;
				opbx_frfree(f);
				return NULL;
			}
			opbx_frfree(f);
		}
		if (!*to && (option_verbose > 2))
			opbx_verbose( VERBOSE_PREFIX_3 "Nobody picked up in %d ms\n", orig);
	}

	return peer;
	
}

static int is_our_turn(struct queue_ent *qe)
{
	struct queue_ent *ch;
	int res;

	/* Atomically read the parent head -- does not need a lock */
	ch = qe->parent->head;
	/* If we are now at the top of the head, break out */
	if (ch == qe) {
		if (option_debug)
			opbx_log(LOG_DEBUG, "It's our turn (%s).\n", qe->chan->name);
		res = 1;
	} else {
		if (option_debug)
			opbx_log(LOG_DEBUG, "It's not our turn (%s).\n", qe->chan->name);
		res = 0;
	}
	return res;
}

static int wait_our_turn(struct queue_ent *qe, int ringing, enum queue_result *reason)
{
	int res = 0;

	/* This is the holding pen for callers 2 through maxlen */
	for (;;) {
		enum queue_member_status stat;

		if (is_our_turn(qe))
			break;

		/* If we have timed out, break out */
		if (qe->expire && (time(NULL) > qe->expire)) {
			*reason = QUEUE_TIMEOUT;
			opbx_queue_log(qe->parent->name, qe->chan->uniqueid,"NONE", "EXITWITHTIMEOUT", "%d", qe->pos);
			break;
		}

		stat = get_member_status(qe->parent);

		/* leave the queue if no agents, if enabled */
		if (qe->parent->leavewhenempty && (stat == QUEUE_NO_MEMBERS)) {
			*reason = QUEUE_LEAVEEMPTY;
			leave_queue(qe);
			break;
		}

		/* leave the queue if no reachable agents, if enabled */
		if ((qe->parent->leavewhenempty == QUEUE_EMPTY_STRICT) && (stat == QUEUE_NO_REACHABLE_MEMBERS)) {
			*reason = QUEUE_LEAVEUNAVAIL;
			leave_queue(qe);
			break;
		}

		/* Make a position announcement, if enabled */
		if (qe->parent->announcefrequency && !ringing)
			res = say_position(qe);
		if (res)
			break;

		/* Make a periodic announcement, if enabled */
		if (qe->parent->periodicannouncefrequency && !ringing)
			res = say_periodic_announcement(qe);

		/* Wait a second before checking again */
		if (!res) res = opbx_waitfordigit(qe->chan, RECHECK * 1000);
		if (res)
			break;
	}
	return res;
}

static int update_queue(struct opbx_call_queue *q, struct member *member)
{
	struct member *cur;

	/* Since a reload could have taken place, we have to traverse the list to
		be sure it's still valid */
	opbx_mutex_lock(&q->lock);
	cur = q->members;
	while(cur) {
		if (member == cur) {
			time(&cur->lastcall);
			cur->calls++;
			break;
		}
		cur = cur->next;
	}
	q->callscompleted++;
	opbx_mutex_unlock(&q->lock);
	return 0;
}

static int calc_metric(struct opbx_call_queue *q, struct member *mem, int pos, struct queue_ent *qe, struct localuser *tmp)
{
	switch (q->strategy) {
	case QUEUE_STRATEGY_RINGALL:
		/* Everyone equal, except for penalty */
		tmp->metric = mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_ROUNDROBIN:
		if (!pos) {
			if (!q->wrapped) {
				/* No more channels, start over */
				q->rrpos = 0;
			} else {
				/* Prioritize next entry */
				q->rrpos++;
			}
			q->wrapped = 0;
		}
		/* Fall through */
	case QUEUE_STRATEGY_RRMEMORY:
		if (pos < q->rrpos) {
			tmp->metric = 1000 + pos;
		} else {
			if (pos > q->rrpos)
				/* Indicate there is another priority */
				q->wrapped = 1;
			tmp->metric = pos;
		}
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_RANDOM:
		tmp->metric = rand() % 1000;
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_FEWESTCALLS:
		tmp->metric = mem->calls;
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_LEASTRECENT:
		if (!mem->lastcall)
			tmp->metric = 0;
		else
			tmp->metric = 1000000 - (time(NULL) - mem->lastcall);
		tmp->metric += mem->penalty * 1000000;
		break;
	default:
		opbx_log(LOG_WARNING, "Can't calculate metric for unknown strategy %d\n", q->strategy);
		break;
	}
	return 0;
}

static int try_calling(struct queue_ent *qe, const char *options, char *announceoverride, const char *url, int *go_on)
{
	struct member *cur;
	struct localuser *outgoing=NULL, *tmp = NULL;
	int to;
	char restofit[OPBX_MAX_EXTENSION];
	char oldexten[OPBX_MAX_EXTENSION]="";
	char oldcontext[OPBX_MAX_CONTEXT]="";
	char queuename[256]="";
	char *newnum;
	char *monitorfilename;
	struct opbx_channel *peer;
	struct opbx_channel *which;
	struct localuser *lpeer;
	struct member *member;
	int res = 0, bridge = 0;
	int numbusies = 0;
	int x=0;
	char *announce = NULL;
	char digit = 0;
	time_t callstart;
	time_t now = time(NULL);
	struct opbx_bridge_config bridge_config;
	char nondataquality = 1;

	memset(&bridge_config, 0, sizeof(bridge_config));
	time(&now);
		
	for (; options && *options; options++)
		switch (*options) {
		case 't':
			opbx_set_flag(&(bridge_config.features_callee), OPBX_FEATURE_REDIRECT);
			break;
		case 'T':
			opbx_set_flag(&(bridge_config.features_caller), OPBX_FEATURE_REDIRECT);
			break;
		case 'w':
			opbx_set_flag(&(bridge_config.features_callee), OPBX_FEATURE_AUTOMON);
			break;
		case 'W':
			opbx_set_flag(&(bridge_config.features_caller), OPBX_FEATURE_AUTOMON);
			break;
		case 'd':
			nondataquality = 0;
			break;
		case 'h':
			opbx_set_flag(&(bridge_config.features_callee), OPBX_FEATURE_DISCONNECT);
			break;
		case 'H':
			opbx_set_flag(&(bridge_config.features_caller), OPBX_FEATURE_DISCONNECT);
			break;
		case 'n':
			if ((now - qe->start >= qe->parent->timeout))
				*go_on = 1;
			break;
		}

	/* Hold the lock while we setup the outgoing calls */
	if (use_weight) 
		opbx_mutex_lock(&qlock);
	opbx_mutex_lock(&qe->parent->lock);
	if (option_debug)
		opbx_log(LOG_DEBUG, "%s is trying to call a queue member.\n", 
							qe->chan->name);
	opbx_copy_string(queuename, qe->parent->name, sizeof(queuename));
	cur = qe->parent->members;
	if (!opbx_strlen_zero(qe->announce))
		announce = qe->announce;
	if (announceoverride && !opbx_strlen_zero(announceoverride))
		announce = announceoverride;

	while(cur) {
		tmp = malloc(sizeof(*tmp));
		if (!tmp) {
			opbx_mutex_unlock(&qe->parent->lock);
			if (use_weight) 
				opbx_mutex_unlock(&qlock);
			opbx_log(LOG_WARNING, "Out of memory\n");
			goto out;
		}
		memset(tmp, 0, sizeof(*tmp));
		tmp->stillgoing = -1;
		if (option_debug) {
			if (url)
				opbx_log(LOG_DEBUG, "Queue with URL=%s_\n", url);
			else 
				opbx_log(LOG_DEBUG, "Simple queue (no URL)\n");
		}

		tmp->member = cur;		/* Never directly dereference!  Could change on reload */
		tmp->oldstatus = cur->status;
		tmp->lastcall = cur->lastcall;
		opbx_copy_string(tmp->interface, cur->interface, sizeof(tmp->interface));
		/* If we're dialing by extension, look at the extension to know what to dial */
		if ((newnum = strstr(tmp->interface, "/BYEXTENSION"))) {
			newnum++;
			strncpy(restofit, newnum + strlen("BYEXTENSION"), sizeof(restofit) - 1);
			snprintf(newnum, sizeof(tmp->interface) - (newnum - tmp->interface), "%s%s", qe->chan->exten, restofit);
			if (option_debug)
				opbx_log(LOG_DEBUG, "Dialing by extension %s\n", tmp->interface);
		}
		/* Special case: If we ring everyone, go ahead and ring them, otherwise
		   just calculate their metric for the appropriate strategy */
		calc_metric(qe->parent, cur, x++, qe, tmp);
		/* Put them in the list of outgoing thingies...  We're ready now. 
		   XXX If we're forcibly removed, these outgoing calls won't get
		   hung up XXX */
		tmp->next = outgoing;
		outgoing = tmp;		
		/* If this line is up, don't try anybody else */
		if (outgoing->chan && (outgoing->chan->_state == OPBX_STATE_UP))
			break;

		cur = cur->next;
	}
	if (qe->parent->timeout)
		to = qe->parent->timeout * 1000;
	else
		to = -1;
	ring_one(qe, outgoing, &numbusies);
	opbx_mutex_unlock(&qe->parent->lock);
	if (use_weight) 
		opbx_mutex_unlock(&qlock);
	lpeer = wait_for_answer(qe, outgoing, &to, &digit, numbusies, opbx_test_flag(&(bridge_config.features_caller), OPBX_FEATURE_DISCONNECT));
	opbx_mutex_lock(&qe->parent->lock);
	if (qe->parent->strategy == QUEUE_STRATEGY_RRMEMORY) {
		store_next(qe, outgoing);
	}
	opbx_mutex_unlock(&qe->parent->lock);
	if (lpeer)
		peer = lpeer->chan;
	else
		peer = NULL;
	if (!peer) {
		if (to) {
			/* Musta gotten hung up */
			record_abandoned(qe);
			res = -1;
		} else {
			res = digit;
		}
		if (option_debug)
			opbx_log(LOG_DEBUG, "%s: Nobody answered.\n", qe->chan->name);
		goto out;
	}
	if (peer) {
		/* Ah ha!  Someone answered within the desired timeframe.  Of course after this
		   we will always return with -1 so that it is hung up properly after the 
		   conversation.  */
		qe->handled++;
		if (!strcmp(qe->chan->type,"Zap"))
			opbx_channel_setoption(qe->chan, OPBX_OPTION_TONE_VERIFY, &nondataquality, sizeof(nondataquality), 0);
		if (!strcmp(peer->type,"Zap"))
			opbx_channel_setoption(peer, OPBX_OPTION_TONE_VERIFY, &nondataquality, sizeof(nondataquality), 0);
		/* Update parameters for the queue */
		recalc_holdtime(qe);
		member = lpeer->member;
		hangupcalls(outgoing, peer);
		outgoing = NULL;
		if (announce || qe->parent->reportholdtime || qe->parent->memberdelay) {
			int res2;
			res2 = opbx_autoservice_start(qe->chan);
			if (!res2) {
				if (qe->parent->memberdelay) {
					opbx_log(LOG_NOTICE, "Delaying member connect for %d seconds\n", qe->parent->memberdelay);
					res2 |= opbx_safe_sleep(peer, qe->parent->memberdelay * 1000);
				}
				if (!res2 && announce) {
					if (play_file(peer, announce))
						opbx_log(LOG_WARNING, "Announcement file '%s' is unavailable, continuing anyway...\n", announce);
				}
				if (!res2 && qe->parent->reportholdtime) {
					if (!play_file(peer, qe->parent->sound_reporthold)) {
						int holdtime;

						time(&now);
						holdtime = abs((now - qe->start) / 60);
						if (holdtime < 2) {
							play_file(peer, qe->parent->sound_lessthan);
							opbx_say_number(peer, 2, OPBX_DIGIT_ANY, peer->language, NULL);
						} else 
							opbx_say_number(peer, holdtime, OPBX_DIGIT_ANY, peer->language, NULL);
						play_file(peer, qe->parent->sound_minutes);
					}
				}
			}
			res2 |= opbx_autoservice_stop(qe->chan);
			if (peer->_softhangup) {
				/* Agent must have hung up */
				opbx_log(LOG_WARNING, "Agent on %s hungup on the customer.  They're going to be pissed.\n", peer->name);
				opbx_queue_log(queuename, qe->chan->uniqueid, peer->name, "AGENTDUMP", "%s", "");
                                record_abandoned(qe);
				if (qe->parent->eventwhencalled) {
					manager_event(EVENT_FLAG_AGENT, "AgentDump",
						      "Queue: %s\r\n"
						      "Uniqueid: %s\r\n"
						      "Channel: %s\r\n"
						      "Member: %s\r\n",
						      queuename, qe->chan->uniqueid, peer->name, member->interface);
				}
				opbx_hangup(peer);
				goto out;
			} else if (res2) {
				/* Caller must have hung up just before being connected*/
				opbx_log(LOG_NOTICE, "Caller was about to talk to agent on %s but the caller hungup.\n", peer->name);
				opbx_queue_log(queuename, qe->chan->uniqueid, peer->name, "ABANDON", "%d|%d|%ld", qe->pos, qe->opos, (long)time(NULL) - qe->start);
				record_abandoned(qe);
				opbx_hangup(peer);
				return -1;
			}
		}
		/* Stop music on hold */
		opbx_moh_stop(qe->chan);
		/* If appropriate, log that we have a destination channel */
		if (qe->chan->cdr)
			opbx_cdr_setdestchan(qe->chan->cdr, peer->name);
		/* Make sure channels are compatible */
		res = opbx_channel_make_compatible(qe->chan, peer);
		if (res < 0) {
			opbx_queue_log(queuename, qe->chan->uniqueid, peer->name, "SYSCOMPAT", "%s", "");
			opbx_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", qe->chan->name, peer->name);
                        record_abandoned(qe);
			opbx_hangup(peer);
			return -1;
		}
		/* Begin Monitoring */
		if (qe->parent->monfmt && *qe->parent->monfmt) {
			monitorfilename = pbx_builtin_getvar_helper(qe->chan, "MONITOR_FILENAME");
			if (pbx_builtin_getvar_helper(qe->chan, "MONITOR_EXEC") || pbx_builtin_getvar_helper(qe->chan, "MONITOR_EXEC_ARGS"))
				which = qe->chan;
			else
				which = peer;
			if (monitorfilename)
				opbx_monitor_start(which, qe->parent->monfmt, monitorfilename, 1 );
			else if (qe->chan->cdr) 
				opbx_monitor_start(which, qe->parent->monfmt, qe->chan->cdr->uniqueid, 1 );
			else {
				/* Last ditch effort -- no CDR, make up something */
				char tmpid[256];
				snprintf(tmpid, sizeof(tmpid), "chan-%x", rand());
				opbx_monitor_start(which, qe->parent->monfmt, tmpid, 1 );
			}
			if (qe->parent->monjoin)
				opbx_monitor_setjoinfiles(which, 1);
		}
		/* Drop out of the queue at this point, to prepare for next caller */
		leave_queue(qe);			
 		if (url && !opbx_strlen_zero(url) && opbx_channel_supports_html(peer)) {
			if (option_debug)
	 			opbx_log(LOG_DEBUG, "app_queue: sendurl=%s.\n", url);
 			opbx_channel_sendurl(peer, url);
 		}
		opbx_queue_log(queuename, qe->chan->uniqueid, peer->name, "CONNECT", "%ld", (long)time(NULL) - qe->start);
		if (qe->parent->eventwhencalled)
			manager_event(EVENT_FLAG_AGENT, "AgentConnect",
				      "Queue: %s\r\n"
				      "Uniqueid: %s\r\n"
				      "Channel: %s\r\n"
				      "Member: %s\r\n"
				      "Holdtime: %ld\r\n",
				      queuename, qe->chan->uniqueid, peer->name, member->interface,
				      (long)time(NULL) - qe->start);
		opbx_copy_string(oldcontext, qe->chan->context, sizeof(oldcontext));
		opbx_copy_string(oldexten, qe->chan->exten, sizeof(oldexten));
		time(&callstart);

		bridge = opbx_bridge_call(qe->chan,peer, &bridge_config);

		if (strcasecmp(oldcontext, qe->chan->context) || strcasecmp(oldexten, qe->chan->exten)) {
			opbx_queue_log(queuename, qe->chan->uniqueid, peer->name, "TRANSFER", "%s|%s", qe->chan->exten, qe->chan->context);
		} else if (qe->chan->_softhangup) {
			opbx_queue_log(queuename, qe->chan->uniqueid, peer->name, "COMPLETECALLER", "%ld|%ld",
				      (long)(callstart - qe->start), (long)(time(NULL) - callstart));
			if (qe->parent->eventwhencalled)
				manager_event(EVENT_FLAG_AGENT, "AgentComplete",
					      "Queue: %s\r\n"
					      "Uniqueid: %s\r\n"
					      "Channel: %s\r\n"
					      "Member: %s\r\n"
					      "HoldTime: %ld\r\n"
					      "TalkTime: %ld\r\n"
					      "Reason: caller\r\n",
					      queuename, qe->chan->uniqueid, peer->name, member->interface,
					      (long)(callstart - qe->start), (long)(time(NULL) - callstart));
		} else {
			opbx_queue_log(queuename, qe->chan->uniqueid, peer->name, "COMPLETEAGENT", "%ld|%ld", (long)(callstart - qe->start), (long)(time(NULL) - callstart));
			if (qe->parent->eventwhencalled)
				manager_event(EVENT_FLAG_AGENT, "AgentComplete",
					      "Queue: %s\r\n"
					      "Uniqueid: %s\r\n"
					      "Channel: %s\r\n"
					      "HoldTime: %ld\r\n"
					      "TalkTime: %ld\r\n"
					      "Reason: agent\r\n",
					      queuename, qe->chan->uniqueid, peer->name, (long)(callstart - qe->start),
					      (long)(time(NULL) - callstart));
		}

		if(bridge != OPBX_PBX_NO_HANGUP_PEER)
			opbx_hangup(peer);
		update_queue(qe->parent, member);
		if (bridge == 0) 
			res = 1; /* JDG: bridge successfull, leave app_queue */
		else 
			res = bridge; /* bridge error, stay in the queue */
	}	
out:
	hangupcalls(outgoing, NULL);
	return res;
}

static int wait_a_bit(struct queue_ent *qe)
{
	/* Don't need to hold the lock while we setup the outgoing calls */
	int retrywait = qe->parent->retry * 1000;

	return opbx_waitfordigit(qe->chan, retrywait);
}

static struct member * interface_exists(struct opbx_call_queue *q, char *interface)
{
	struct member *mem;

	if (q)
		for (mem = q->members; mem; mem = mem->next)
			if (!strcasecmp(interface, mem->interface))
				return mem;

	return NULL;
}


/* Dump all members in a specific queue to the databse
 *
 * <pm_family>/<queuename> = <interface>;<penalty>;<paused>[|...]
 *
 */
static void dump_queue_members(struct opbx_call_queue *pm_queue)
{
	struct member *cur_member;
	char value[PM_MAX_LEN];
	int value_len = 0;
	int res;

	memset(value, 0, sizeof(value));

	if (!pm_queue)
		return;

	for (cur_member = pm_queue->members; cur_member; cur_member = cur_member->next) {
		if (!cur_member->dynamic)
			continue;

		res = snprintf(value + value_len, sizeof(value) - value_len, "%s;%d;%d%s",
			       cur_member->interface, cur_member->penalty, cur_member->paused,
			       cur_member->next ? "|" : "");
		if (res != strlen(value + value_len)) {
			opbx_log(LOG_WARNING, "Could not create persistent member string, out of space\n");
			break;
		}
		value_len += res;
	}
	
	if (value_len && !cur_member) {
		if (opbx_db_put(pm_family, pm_queue->name, value))
			opbx_log(LOG_WARNING, "failed to create persistent dynamic entry!\n");
	} else
		/* Delete the entry if the queue is empty or there is an error */
		opbx_db_del(pm_family, pm_queue->name);
}

static int remove_from_queue(char *queuename, char *interface)
{
	struct opbx_call_queue *q;
	struct member *lopbx_member, *look;
	int res = RES_NOSUCHQUEUE;

	opbx_mutex_lock(&qlock);
	for (q = queues ; q ; q = q->next) {
		opbx_mutex_lock(&q->lock);
		if (!strcmp(q->name, queuename)) {
			if ((lopbx_member = interface_exists(q, interface))) {
				if ((look = q->members) == lopbx_member) {
					q->members = lopbx_member->next;
				} else {
					while (look != NULL) {
						if (look->next == lopbx_member) {
							look->next = lopbx_member->next;
							break;
						} else {
							 look = look->next;
						}
					}
				}
				manager_event(EVENT_FLAG_AGENT, "QueueMemberRemoved",
						"Queue: %s\r\n"
						"Location: %s\r\n",
					q->name, lopbx_member->interface);
				free(lopbx_member);

				if (queue_persistent_members)
				    dump_queue_members(q);

				res = RES_OKAY;
			} else {
				res = RES_EXISTS;
			}
			opbx_mutex_unlock(&q->lock);
			break;
		}
		opbx_mutex_unlock(&q->lock);
	}
	opbx_mutex_unlock(&qlock);
	return res;
}

static int add_to_queue(char *queuename, char *interface, int penalty, int paused, int dump)
{
	struct opbx_call_queue *q;
	struct member *new_member;
	int res = RES_NOSUCHQUEUE;

	opbx_mutex_lock(&qlock);
	for (q = queues ; q ; q = q->next) {
		opbx_mutex_lock(&q->lock);
		if (!strcmp(q->name, queuename)) {
			if (interface_exists(q, interface) == NULL) {
				new_member = create_queue_member(interface, penalty, paused);

				if (new_member != NULL) {
					new_member->dynamic = 1;
					new_member->next = q->members;
					q->members = new_member;
					manager_event(EVENT_FLAG_AGENT, "QueueMemberAdded",
						"Queue: %s\r\n"
						"Location: %s\r\n"
						"Membership: %s\r\n"
						"Penalty: %d\r\n"
						"CallsTaken: %d\r\n"
						"LastCall: %ld\r\n"
						"Status: %d\r\n"
						"Paused: %d\r\n",
					q->name, new_member->interface, new_member->dynamic ? "dynamic" : "static",
					new_member->penalty, new_member->calls, new_member->lastcall, new_member->status, new_member->paused);
					
					if (dump)
						dump_queue_members(q);

					res = RES_OKAY;
				} else {
					res = RES_OUTOFMEMORY;
				}
			} else {
				res = RES_EXISTS;
			}
			opbx_mutex_unlock(&q->lock);
			break;
		}
		opbx_mutex_unlock(&q->lock);
	}
	opbx_mutex_unlock(&qlock);
	return res;
}

static int set_member_paused(char *queuename, char *interface, int paused)
{
	int found = 0;
	struct opbx_call_queue *q;
	struct member *mem;

	/* Special event for when all queues are paused - individual events still generated */

	if (opbx_strlen_zero(queuename))
		opbx_queue_log("NONE", "NONE", interface, (paused ? "PAUSEALL" : "UNPAUSEALL"), "%s", "");

	opbx_mutex_lock(&qlock);
	for (q = queues ; q ; q = q->next) {
		opbx_mutex_lock(&q->lock);
		if (opbx_strlen_zero(queuename) || !strcasecmp(q->name, queuename)) {
			if ((mem = interface_exists(q, interface))) {
				found++;
				if (mem->paused == paused)
					opbx_log(LOG_DEBUG, "%spausing already-%spaused queue member %s:%s\n", (paused ? "" : "un"), (paused ? "" : "un"), q->name, interface);
				mem->paused = paused;

				if (queue_persistent_members)
				    dump_queue_members(q);

				opbx_queue_log(q->name, "NONE", interface, (paused ? "PAUSE" : "UNPAUSE"), "%s", "");

				manager_event(EVENT_FLAG_AGENT, "QueueMemberPaused",
					"Queue: %s\r\n"
					"Location: %s\r\n"
					"Paused: %d\r\n",
						q->name, mem->interface, paused);
			}
		}
		opbx_mutex_unlock(&q->lock);
	}
	opbx_mutex_unlock(&qlock);

	if (found)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

/* Reload dynamic queue members persisted into the opbxdb */
static void reload_queue_members(void)
{
	char *cur_ptr;	
	char *queue_name;
	char *member;
	char *interface;
	char *penalty_tok;
	int penalty = 0;
	char *paused_tok;
	int paused = 0;
	struct opbx_db_entry *db_tree;
	struct opbx_db_entry *entry;
	struct opbx_call_queue *cur_queue;
	char queue_data[PM_MAX_LEN];

	opbx_mutex_lock(&qlock);

	/* Each key in 'pm_family' is the name of a queue */
	db_tree = opbx_db_gettree(pm_family, NULL);
	for (entry = db_tree; entry; entry = entry->next) {

		queue_name = entry->key + strlen(pm_family) + 2;

		cur_queue = queues;
		while (cur_queue) {
			opbx_mutex_lock(&cur_queue->lock);
			if (!strcmp(queue_name, cur_queue->name))
				break;
			opbx_mutex_unlock(&cur_queue->lock);
			cur_queue = cur_queue->next;
		}

		if (!cur_queue) {
			/* If the queue no longer exists, remove it from the
			 * database */
			opbx_db_del(pm_family, queue_name);
			continue;
		} else
			opbx_mutex_unlock(&cur_queue->lock);

		if (opbx_db_get(pm_family, queue_name, queue_data, PM_MAX_LEN))
			continue;

		cur_ptr = queue_data;
		while ((member = strsep(&cur_ptr, "|"))) {
			if (opbx_strlen_zero(member))
				continue;

			interface = strsep(&member, ";");
			penalty_tok = strsep(&member, ";");
			paused_tok = strsep(&member, ";");

			if (!penalty_tok) {
				opbx_log(LOG_WARNING, "Error parsing persisent member string for '%s' (penalty)\n", queue_name);
				break;
			}
			penalty = strtol(penalty_tok, NULL, 10);
			if (errno == ERANGE) {
				opbx_log(LOG_WARNING, "Error converting penalty: %s: Out of range.\n", penalty_tok);
				break;
			}
			
			if (!paused_tok) {
				opbx_log(LOG_WARNING, "Error parsing persistent member string for '%s' (paused)\n", queue_name);
				break;
			}
			paused = strtol(paused_tok, NULL, 10);
			if ((errno == ERANGE) || paused < 0 || paused > 1) {
				opbx_log(LOG_WARNING, "Error converting paused: %s: Expected 0 or 1.\n", paused_tok);
				break;
			}

			if (option_debug)
				opbx_log(LOG_DEBUG, "Reload Members: Queue: %s  Member: %s  Penalty: %d  Paused: %d\n", queue_name, interface, penalty, paused);
			
			if (add_to_queue(queue_name, interface, penalty, paused, 0) == RES_OUTOFMEMORY) {
				opbx_log(LOG_ERROR, "Out of Memory when reloading persistent queue member\n");
				break;
			}
		}
	}

	opbx_mutex_unlock(&qlock);
	if (db_tree) {
		opbx_log(LOG_NOTICE, "Queue members sucessfully reloaded from database.\n");
		opbx_db_freetree(db_tree);
	}
}

static int pqm_exec(struct opbx_channel *chan, void *data)
{
	struct localuser *u;
	char *queuename, *interface;

	if (opbx_strlen_zero(data)) {
		opbx_log(LOG_WARNING, "PauseQueueMember requires an argument ([queuename]|interface])\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	queuename = opbx_strdupa((char *)data);
	if (!queuename) {
		opbx_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	interface = strchr(queuename, '|');
	if (!interface) {
		opbx_log(LOG_WARNING, "Missing interface argument to PauseQueueMember ([queuename]|interface])\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	*interface = '\0';
	interface++;

	if (set_member_paused(queuename, interface, 1)) {
		opbx_log(LOG_WARNING, "Attempt to pause interface %s, not found\n", interface);
		if (opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) {
			LOCAL_USER_REMOVE(u);
			return 0;
		}
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	LOCAL_USER_REMOVE(u);

	return 0;
}

static int upqm_exec(struct opbx_channel *chan, void *data)
{
	struct localuser *u;
	char *queuename, *interface;

	if (opbx_strlen_zero(data)) {
		opbx_log(LOG_WARNING, "UnpauseQueueMember requires an argument ([queuename]|interface])\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	queuename = opbx_strdupa((char *)data);
	if (!queuename) {
		opbx_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	interface = strchr(queuename, '|');
	if (!interface) {
		opbx_log(LOG_WARNING, "Missing interface argument to PauseQueueMember ([queuename]|interface])\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	*interface = '\0';
	interface++;

	if (set_member_paused(queuename, interface, 0)) {
		opbx_log(LOG_WARNING, "Attempt to unpause interface %s, not found\n", interface);
		if (opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) {
			LOCAL_USER_REMOVE(u);
			return 0;
		}
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	LOCAL_USER_REMOVE(u);

	return 0;
}

static int rqm_exec(struct opbx_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	char *info, *queuename;
	char tmpchan[256]="";
	char *interface = NULL;

	if (opbx_strlen_zero(data)) {
		opbx_log(LOG_WARNING, "RemoveQueueMember requires an argument (queuename[|interface])\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	info = opbx_strdupa(data);
	if (!info) {
		opbx_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	queuename = info;
	if (queuename) {
		interface = strchr(queuename, '|');
		if (interface) {
			*interface = '\0';
			interface++;
		}
		else {
			opbx_copy_string(tmpchan, chan->name, sizeof(tmpchan));
			interface = strrchr(tmpchan, '-');
			if (interface)
				*interface = '\0';
			interface = tmpchan;
		}
	}

	switch (remove_from_queue(queuename, interface)) {
	case RES_OKAY:
		opbx_log(LOG_NOTICE, "Removed interface '%s' from queue '%s'\n", interface, queuename);
		res = 0;
		break;
	case RES_EXISTS:
		opbx_log(LOG_WARNING, "Unable to remove interface '%s' from queue '%s': Not there\n", interface, queuename);
		opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		res = 0;
		break;
	case RES_NOSUCHQUEUE:
		opbx_log(LOG_WARNING, "Unable to remove interface from queue '%s': No such queue\n", queuename);
		res = 0;
		break;
	case RES_OUTOFMEMORY:
		opbx_log(LOG_ERROR, "Out of memory\n");
		break;
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

static int aqm_exec(struct opbx_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	char *queuename;
	char *info;
	char tmpchan[512]="";
	char *interface=NULL;
	char *penaltys=NULL;
	int penalty = 0;

	if (opbx_strlen_zero(data)) {
		opbx_log(LOG_WARNING, "AddQueueMember requires an argument (queuename[|[interface][|penalty]])\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	info = opbx_strdupa(data);
	if (!info) {
		opbx_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	queuename = info;
	if (queuename) {
		interface = strchr(queuename, '|');
		if (interface) {
			*interface = '\0';
			interface++;
		}
		if (interface) {
			penaltys = strchr(interface, '|');
			if (penaltys) {
				*penaltys = '\0';
				penaltys++;
			}
		}
		if (opbx_strlen_zero(interface)) {
			opbx_copy_string(tmpchan, chan->name, sizeof(tmpchan));
			interface = strrchr(tmpchan, '-');
			if (interface)
				*interface = '\0';
			interface = tmpchan;
		}
		if (!opbx_strlen_zero(penaltys)) {
			if ((sscanf(penaltys, "%d", &penalty) != 1) || penalty < 0) {
				opbx_log(LOG_WARNING, "Penalty '%s' is invalid, must be an integer >= 0\n", penaltys);
				penalty = 0;
			}
		}
	}

	switch (add_to_queue(queuename, interface, penalty, 0, queue_persistent_members)) {
	case RES_OKAY:
		opbx_log(LOG_NOTICE, "Added interface '%s' to queue '%s'\n", interface, queuename);
		res = 0;
		break;
	case RES_EXISTS:
		opbx_log(LOG_WARNING, "Unable to add interface '%s' to queue '%s': Already there\n", interface, queuename);
		opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		res = 0;
		break;
	case RES_NOSUCHQUEUE:
		opbx_log(LOG_WARNING, "Unable to add interface to queue '%s': No such queue\n", queuename);
		res = 0;
		break;
	case RES_OUTOFMEMORY:
		opbx_log(LOG_ERROR, "Out of memory adding member %s to queue %s\n", interface, queuename);
		break;
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

static int queue_exec(struct opbx_channel *chan, void *data)
{
	int res=-1;
	int ringing=0;
	struct localuser *u;
	char *queuename;
	char info[512];
	char *info_ptr = info;
	char *options = NULL;
	char *url = NULL;
	char *announceoverride = NULL;
	char *user_priority;
	int prio;
	char *queuetimeoutstr = NULL;
	enum queue_result reason = QUEUE_UNKNOWN;

	/* whether to exit Queue application after the timeout hits */
	int go_on = 0;

	/* Our queue entry */
	struct queue_ent qe;
	
	if (opbx_strlen_zero(data)) {
		opbx_log(LOG_WARNING, "Queue requires an argument: queuename[|options[|URL][|announceoverride][|timeout]]\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	/* Setup our queue entry */
	memset(&qe, 0, sizeof(qe));
	qe.start = time(NULL);
	
	/* Parse our arguments XXX Check for failure XXX */
	opbx_copy_string(info, (char *) data, sizeof(info));
	queuename = strsep(&info_ptr, "|");
	options = strsep(&info_ptr, "|");
	url = strsep(&info_ptr, "|");
	announceoverride = strsep(&info_ptr, "|");
	queuetimeoutstr = info_ptr;

	/* set the expire time based on the supplied timeout; */
	if (queuetimeoutstr)
		qe.expire = qe.start + atoi(queuetimeoutstr);
	else
		qe.expire = 0;

	/* Get the priority from the variable ${QUEUE_PRIO} */
	user_priority = pbx_builtin_getvar_helper(chan, "QUEUE_PRIO");
	if (user_priority) {
		if (sscanf(user_priority, "%d", &prio) == 1) {
			if (option_debug)
				opbx_log(LOG_DEBUG, "%s: Got priority %d from ${QUEUE_PRIO}.\n",
					chan->name, prio);
		} else {
			opbx_log(LOG_WARNING, "${QUEUE_PRIO}: Invalid value (%s), channel %s.\n",
				user_priority, chan->name);
			prio = 0;
		}
	} else {
		if (option_debug > 2)
			opbx_log(LOG_DEBUG, "NO QUEUE_PRIO variable found. Using default.\n");
		prio = 0;
	}

	if (options && (strchr(options, 'r')))
		ringing = 1;

	if (option_debug)  
		opbx_log(LOG_DEBUG, "queue: %s, options: %s, url: %s, announce: %s, expires: %ld, priority: %d\n",
			queuename, options, url, announceoverride, (long)qe.expire, (int)prio);

	qe.chan = chan;
	qe.prio = (int)prio;
	qe.lopbx_pos_said = 0;
	qe.lopbx_pos = 0;
	qe.lopbx_periodic_announce_time = time(NULL);
	if (!join_queue(queuename, &qe, &reason)) {
		opbx_queue_log(queuename, chan->uniqueid, "NONE", "ENTERQUEUE", "%s|%s", url ? url : "",
			      chan->cid.cid_num ? chan->cid.cid_num : "");
check_turns:
		if (ringing) {
			opbx_indicate(chan, OPBX_CONTROL_RINGING);
		} else {              
			opbx_moh_start(chan, qe.moh);
		}
		for (;;) {
			/* This is the wait loop for callers 2 through maxlen */

			res = wait_our_turn(&qe, ringing, &reason);
			/* If they hungup, return immediately */
			if (res < 0) {
				/* Record this abandoned call */
				record_abandoned(&qe);
				opbx_queue_log(queuename, chan->uniqueid, "NONE", "ABANDON", "%d|%d|%ld", qe.pos, qe.opos, (long)time(NULL) - qe.start);
				if (option_verbose > 2) {
					opbx_verbose(VERBOSE_PREFIX_3 "User disconnected from queue %s while waiting their turn\n", queuename);
					res = -1;
				}
				break;
			}
			if (!res) 
				break;
			if (valid_exit(&qe, res)) {
				opbx_queue_log(queuename, chan->uniqueid, "NONE", "EXITWITHKEY", "%s|%d", qe.digits, qe.pos);
				break;
			}
		}
		if (!res) {
			int makeannouncement = 0;
			for (;;) {
				/* This is the wait loop for the head caller*/
				/* To exit, they may get their call answered; */
				/* they may dial a digit from the queue context; */
				/* or, they may timeout. */

				enum queue_member_status stat;

				/* Leave if we have exceeded our queuetimeout */
				if (qe.expire && (time(NULL) > qe.expire)) {
                                        record_abandoned(&qe);
					reason = QUEUE_TIMEOUT;
					res = 0;
					opbx_queue_log(queuename, chan->uniqueid,"NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
					break;
				}

				if (makeannouncement) {
					/* Make a position announcement, if enabled */
					if (qe.parent->announcefrequency && !ringing)
						res = say_position(&qe);
					if (res && valid_exit(&qe, res)) {
						opbx_queue_log(queuename, chan->uniqueid, "NONE", "EXITWITHKEY", "%s|%d", qe.digits, qe.pos);
						break;
					}

				}
				makeannouncement = 1;

				/* Make a periodic announcement, if enabled */
				if (qe.parent->periodicannouncefrequency && !ringing)
					res = say_periodic_announcement(&qe);

				if (res && valid_exit(&qe, res)) {
					opbx_queue_log(queuename, chan->uniqueid, "NONE", "EXITWITHKEY", "%c|%d", res, qe.pos);
					break;
				}

				/* Try calling all queue members for 'timeout' seconds */
				res = try_calling(&qe, options, announceoverride, url, &go_on);
				if (res) {
					if (res < 0) {
						if (!qe.handled) {
							record_abandoned(&qe);
							opbx_queue_log(queuename, chan->uniqueid, "NONE", "ABANDON", "%d|%d|%ld", qe.pos, qe.opos, (long)time(NULL) - qe.start);
						}
					} else if (res > 0)
						opbx_queue_log(queuename, chan->uniqueid, "NONE", "EXITWITHKEY", "%s|%d", qe.digits, qe.pos);
					break;
				}

				stat = get_member_status(qe.parent);

				/* leave the queue if no agents, if enabled */
				if (qe.parent->leavewhenempty && (stat == QUEUE_NO_MEMBERS)) {
					record_abandoned(&qe);
					reason = QUEUE_LEAVEEMPTY;
					res = 0;
					break;
				}

				/* leave the queue if no reachable agents, if enabled */
				if ((qe.parent->leavewhenempty == QUEUE_EMPTY_STRICT) && (stat == QUEUE_NO_REACHABLE_MEMBERS)) {
					record_abandoned(&qe);
					reason = QUEUE_LEAVEUNAVAIL;
					res = 0;
					break;
				}

				/* Leave if we have exceeded our queuetimeout */
				if (qe.expire && (time(NULL) > qe.expire)) {
					record_abandoned(&qe);
					reason = QUEUE_TIMEOUT;
					res = 0;
					opbx_queue_log(queuename, chan->uniqueid,"NONE", "EXITWITHTIMEOUT", "%d", qe.pos);	
					break;
				}

				/* OK, we didn't get anybody; wait for 'retry' seconds; may get a digit to exit with */
				res = wait_a_bit(&qe);
				if (res < 0) {
					record_abandoned(&qe);
					opbx_queue_log(queuename, chan->uniqueid, "NONE", "ABANDON", "%d|%d|%ld", qe.pos, qe.opos, (long)time(NULL) - qe.start);
					if (option_verbose > 2) {
						opbx_verbose(VERBOSE_PREFIX_3 "User disconnected from queue %s when they almost made it\n", queuename);
						res = -1;
					}
					break;
				}
				if (res && valid_exit(&qe, res)) {
					opbx_queue_log(queuename, chan->uniqueid, "NONE", "EXITWITHKEY", "%s|%d", qe.digits, qe.pos);
					break;
				}
				/* exit after 'timeout' cycle if 'n' option enabled */
				if (go_on) {
					if (option_verbose > 2) {
						opbx_verbose(VERBOSE_PREFIX_3 "Exiting on time-out cycle\n");
						res = -1;
					}
					opbx_queue_log(queuename, chan->uniqueid, "NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
                                        record_abandoned(&qe);
					reason = QUEUE_TIMEOUT;
					res = 0;
					break;
				}
				/* Since this is a priority queue and 
				 * it is not sure that we are still at the head
				 * of the queue, go and check for our turn again.
				 */
				if (!is_our_turn(&qe)) {
					if (option_debug)
						opbx_log(LOG_DEBUG, "Darn priorities, going back in queue (%s)!\n",
								qe.chan->name);
					goto check_turns;
				}
			}
		}
		/* Don't allow return code > 0 */
		if (res >= 0 && res != OPBX_PBX_KEEPALIVE) {
			res = 0;	
			if (ringing) {
				opbx_indicate(chan, -1);
			} else {
				opbx_moh_stop(chan);
			}			
			opbx_stopstream(chan);
		}
		leave_queue(&qe);
		if (reason != QUEUE_UNKNOWN)
			set_queue_result(chan, reason);
	} else {
		opbx_log(LOG_WARNING, "Unable to join queue '%s'\n", queuename);
		set_queue_result(chan, reason);
		res = 0;
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

static char *queue_function_qac(struct opbx_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	int count = 0;
	struct opbx_call_queue *q;
	struct localuser *u;
	struct member *m;

	LOCAL_USER_ACF_ADD(u);

	opbx_copy_string(buf, "0", len);
	
	if (opbx_strlen_zero(data)) {
		opbx_log(LOG_ERROR, "QUEUEAGENTCOUNT requires an argument: queuename\n");
		LOCAL_USER_REMOVE(u);
		return buf;
	}

	opbx_mutex_lock(&qlock);

	/* Find the right queue */
	for (q = queues; q; q = q->next) {
		if (!strcasecmp(q->name, data)) {
			opbx_mutex_lock(&q->lock);
			break;
		}
	}

	opbx_mutex_unlock(&qlock);

	if (q) {
		for (m = q->members; m; m = m->next) {
			/* Count the agents who are logged in and presently answering calls */
			if ((m->status != OPBX_DEVICE_UNAVAILABLE) && (m->status != OPBX_DEVICE_INVALID)) {
				count++;
			}
		}
		opbx_mutex_unlock(&q->lock);
	}

	snprintf(buf, len, "%d", count);
	LOCAL_USER_REMOVE(u);
	return buf;
}

static struct opbx_custom_function queueagentcount_function = {
	.name = "QUEUEAGENTCOUNT",
	.synopsis = "Count number of agents answering a queue",
	.syntax = "QUEUEAGENTCOUNT(<queuename>)",
	.read = queue_function_qac,
};

static void reload_queues(void)
{
	struct opbx_call_queue *q, *ql, *qn;
	struct opbx_config *cfg;
	char *cat, *tmp;
	struct opbx_variable *var;
	struct member *prev, *cur;
	int new;
	char *general_val = NULL;
	char interface[80];
	int penalty;
	
	cfg = opbx_config_load("queues.conf");
	if (!cfg) {
		opbx_log(LOG_NOTICE, "No call queueing config file (queues.conf), so no call queues\n");
		return;
	}
	memset(interface, 0, sizeof(interface));
	opbx_mutex_lock(&qlock);
	use_weight=0;
	/* Mark all queues as dead for the moment */
	q = queues;
	while(q) {
		q->dead = 1;
		q = q->next;
	}
	/* Chug through config file */
	cat = opbx_category_browse(cfg, NULL);
	while(cat) {
		if (!strcasecmp(cat, "general")) {	
			/* Initialize global settings */
			queue_persistent_members = 0;
			if ((general_val = opbx_variable_retrieve(cfg, "general", "persistentmembers")))
				queue_persistent_members = opbx_true(general_val);
		} else {	/* Define queue */
			/* Look for an existing one */
			q = queues;
			while(q) {
				if (!strcmp(q->name, cat))
					break;
				q = q->next;
			}
			if (!q) {
				/* Make one then */
				q = alloc_queue(cat);
				new = 1;
			} else
				new = 0;
			if (q) {
				if (!new)
					opbx_mutex_lock(&q->lock);
				/* Re-initialize the queue, and clear statistics */
				init_queue(q);
				clear_queue(q);
				free_members(q, 0);
				prev = q->members;
				if (prev) {
					/* find the end of any dynamic members */
					while(prev->next)
						prev = prev->next;
				}
				var = opbx_variable_browse(cfg, cat);
				while(var) {
					if (!strcasecmp(var->name, "member")) {
						/* Add a new member */
						opbx_copy_string(interface, var->value, sizeof(interface));
						if ((tmp = strchr(interface, ','))) {
							*tmp = '\0';
							tmp++;
							penalty = atoi(tmp);
							if (penalty < 0) {
								penalty = 0;
							}
						} else
							penalty = 0;
						cur = create_queue_member(interface, penalty, 0);
						if (cur) {
							if (prev)
								prev->next = cur;
							else
								q->members = cur;
							prev = cur;
						}
					} else {
						queue_set_param(q, var->name, var->value, var->lineno, 1);
					}
					var = var->next;
				}
				if (!new) 
					opbx_mutex_unlock(&q->lock);
				if (new) {
					q->next = queues;
					queues = q;
				}
			}
		}
		cat = opbx_category_browse(cfg, cat);
	}
	opbx_config_destroy(cfg);
	q = queues;
	ql = NULL;
	while(q) {
		qn = q->next;
		if (q->dead) {
			if (ql)
				ql->next = q->next;
			else
				queues = q->next;
			if (!q->count) {
				free(q);
			} else
				opbx_log(LOG_WARNING, "XXX Leaking a little memory :( XXX\n");
		} else {
			for (cur = q->members; cur; cur = cur->next)
				cur->status = opbx_device_state(cur->interface);
			ql = q;
		}
		q = qn;
	}
	opbx_mutex_unlock(&qlock);
}

static int __queues_show(int manager, int fd, int argc, char **argv, int queue_show)
{
	struct opbx_call_queue *q;
	struct queue_ent *qe;
	struct member *mem;
	int pos;
	time_t now;
	char max_buf[80];
	char *max;
	size_t max_left;
	float sl = 0;
	char *term = manager ? "\r\n" : "\n";

	time(&now);
	if ((!queue_show && argc != 2) || (queue_show && argc != 3))
		return RESULT_SHOWUSAGE;
	opbx_mutex_lock(&qlock);
	q = queues;
	if (!q) {	
		opbx_mutex_unlock(&qlock);
		if (queue_show)
			opbx_cli(fd, "No such queue: %s.%s",argv[2], term);
		else
			opbx_cli(fd, "No queues.%s", term);
		return RESULT_SUCCESS;
	}
	while (q) {
		opbx_mutex_lock(&q->lock);
		if (queue_show) {
			if (strcasecmp(q->name, argv[2]) != 0) {
				opbx_mutex_unlock(&q->lock);
				q = q->next;
				if (!q) {
					opbx_cli(fd, "No such queue: %s.%s",argv[2], term);
					break;
				}
				continue;
			}
		}
		max_buf[0] = '\0';
		max = max_buf;
		max_left = sizeof(max_buf);
		if (q->maxlen)
			opbx_build_string(&max, &max_left, "%d", q->maxlen);
		else
			opbx_build_string(&max, &max_left, "unlimited");
		sl = 0;
		if(q->callscompleted > 0)
			sl = 100*((float)q->callscompletedinsl/(float)q->callscompleted);
		opbx_cli(fd, "%-12.12s has %d calls (max %s) in '%s' strategy (%ds holdtime), W:%d, C:%d, A:%d, SL:%2.1f%% within %ds%s",
			q->name, q->count, max_buf, int2strat(q->strategy), q->holdtime, q->weight, q->callscompleted, q->callsabandoned,sl,q->servicelevel, term);
		if (q->members) {
			opbx_cli(fd, "   Members: %s", term);
			for (mem = q->members; mem; mem = mem->next) {
				max_buf[0] = '\0';
				max = max_buf;
				max_left = sizeof(max_buf);
				if (mem->penalty)
					opbx_build_string(&max, &max_left, " with penalty %d", mem->penalty);
				if (mem->dynamic)
					opbx_build_string(&max, &max_left, " (dynamic)");
				if (mem->paused)
					opbx_build_string(&max, &max_left, " (paused)");
				opbx_build_string(&max, &max_left, " (%s)", devstate2str(mem->status));
				if (mem->calls) {
					opbx_build_string(&max, &max_left, " has taken %d calls (last was %ld secs ago)",
							 mem->calls, (long)(time(NULL) - mem->lastcall));
				} else
					opbx_build_string(&max, &max_left, " has taken no calls yet");
				opbx_cli(fd, "      %s%s%s", mem->interface, max_buf, term);
			}
		} else
			opbx_cli(fd, "   No Members%s", term);
		if (q->head) {
			pos = 1;
			opbx_cli(fd, "   Callers: %s", term);
			for (qe = q->head; qe; qe = qe->next) 
				opbx_cli(fd, "      %d. %s (wait: %ld:%2.2ld, prio: %d)%s", pos++, qe->chan->name,
					(long)(now - qe->start) / 60, (long)(now - qe->start) % 60, qe->prio, term);
		} else
			opbx_cli(fd, "   No Callers%s", term);
		opbx_cli(fd, "%s", term);
		opbx_mutex_unlock(&q->lock);
		q = q->next;
		if (queue_show)
			break;
	}
	opbx_mutex_unlock(&qlock);
	return RESULT_SUCCESS;
}

static int queues_show(int fd, int argc, char **argv)
{
	return __queues_show(0, fd, argc, argv, 0);
}

static int queue_show(int fd, int argc, char **argv)
{
	return __queues_show(0, fd, argc, argv, 1);
}

static char *complete_queue(char *line, char *word, int pos, int state)
{
	struct opbx_call_queue *q;
	int which=0;
	
	opbx_mutex_lock(&qlock);
	for (q = queues; q; q = q->next) {
		if (!strncasecmp(word, q->name, strlen(word))) {
			if (++which > state)
				break;
		}
	}
	opbx_mutex_unlock(&qlock);
	return q ? strdup(q->name) : NULL;
}

/*! \brief callback to display queues status in manager
  \addtogroup Group_AMI
*/
static int manager_queues_show( struct mansession *s, struct message *m )
{
	char *a[] = { "show", "queues" };
	__queues_show(1, s->fd, 2, a, 0);
	opbx_cli(s->fd, "\r\n\r\n");	/* Properly terminate Manager output */

	return RESULT_SUCCESS;
} 

/* Dump queue status */
static int manager_queues_status( struct mansession *s, struct message *m )
{
	time_t now;
	int pos;
	char *id = astman_get_header(m,"ActionID");
	char *queuefilter = astman_get_header(m,"Queue");
	char *memberfilter = astman_get_header(m,"Member");
	char idText[256] = "";
	struct opbx_call_queue *q;
	struct queue_ent *qe;
	float sl = 0;
	struct member *mem;

	astman_send_ack(s, m, "Queue status will follow");
	time(&now);
	opbx_mutex_lock(&qlock);
	if (!opbx_strlen_zero(id)) {
		snprintf(idText,256,"ActionID: %s\r\n",id);
	}
	for (q = queues; q; q = q->next) {
		opbx_mutex_lock(&q->lock);

		/* List queue properties */
		if (opbx_strlen_zero(queuefilter) || !strcmp(q->name, queuefilter)) {
			if(q->callscompleted > 0)
				sl = 100*((float)q->callscompletedinsl/(float)q->callscompleted);
			opbx_cli(s->fd, "Event: QueueParams\r\n"
						"Queue: %s\r\n"
						"Max: %d\r\n"
						"Calls: %d\r\n"
						"Holdtime: %d\r\n"
						"Completed: %d\r\n"
						"Abandoned: %d\r\n"
						"ServiceLevel: %d\r\n"
						"ServicelevelPerf: %2.1f\r\n"
						"Weight: %d\r\n"
						"%s"
						"\r\n",
							q->name, q->maxlen, q->count, q->holdtime, q->callscompleted,
							q->callsabandoned, q->servicelevel, sl, q->weight, idText);
			/* List Queue Members */
			for (mem = q->members; mem; mem = mem->next) {
				if (opbx_strlen_zero(memberfilter) || !strcmp(mem->interface, memberfilter)) {
					opbx_cli(s->fd, "Event: QueueMember\r\n"
						"Queue: %s\r\n"
						"Location: %s\r\n"
						"Membership: %s\r\n"
						"Penalty: %d\r\n"
						"CallsTaken: %d\r\n"
						"LastCall: %ld\r\n"
						"Status: %d\r\n"
						"Paused: %d\r\n"
						"%s"
						"\r\n",
							q->name, mem->interface, mem->dynamic ? "dynamic" : "static",
							mem->penalty, mem->calls, mem->lastcall, mem->status, mem->paused, idText);
				}
			}
			/* List Queue Entries */
			pos = 1;
			for (qe = q->head; qe; qe = qe->next) {
				opbx_cli(s->fd, "Event: QueueEntry\r\n"
					"Queue: %s\r\n"
					"Position: %d\r\n"
					"Channel: %s\r\n"
					"CallerID: %s\r\n"
					"CallerIDName: %s\r\n"
					"Wait: %ld\r\n"
					"%s"
					"\r\n", 
						q->name, pos++, qe->chan->name, 
						qe->chan->cid.cid_num ? qe->chan->cid.cid_num : "unknown",
						qe->chan->cid.cid_name ? qe->chan->cid.cid_name : "unknown",
						(long)(now - qe->start), idText);
			}
		}
		opbx_mutex_unlock(&q->lock);
	}
	opbx_mutex_unlock(&qlock);

	opbx_cli(s->fd,
		"Event: QueueStatusComplete\r\n"
		"%s"
		"\r\n",idText);


	return RESULT_SUCCESS;
}

static int manager_add_queue_member(struct mansession *s, struct message *m)
{
	char *queuename, *interface, *penalty_s, *paused_s;
	int paused, penalty = 0;

	queuename = astman_get_header(m, "Queue");
	interface = astman_get_header(m, "Interface");
	penalty_s = astman_get_header(m, "Penalty");
	paused_s = astman_get_header(m, "Paused");

	if (opbx_strlen_zero(queuename)) {
		astman_send_error(s, m, "'Queue' not specified.");
		return 0;
	}

	if (opbx_strlen_zero(interface)) {
		astman_send_error(s, m, "'Interface' not specified.");
		return 0;
	}

	if (opbx_strlen_zero(penalty_s))
		penalty = 0;
	else if (sscanf(penalty_s, "%d", &penalty) != 1) {
		penalty = 0;
	}

	if (opbx_strlen_zero(paused_s))
		paused = 0;
	else
		paused = abs(opbx_true(paused_s));

	switch (add_to_queue(queuename, interface, penalty, paused, queue_persistent_members)) {
	case RES_OKAY:
		astman_send_ack(s, m, "Added interface to queue");
		break;
	case RES_EXISTS:
		astman_send_error(s, m, "Unable to add interface: Already there");
		break;
	case RES_NOSUCHQUEUE:
		astman_send_error(s, m, "Unable to add interface to queue: No such queue");
		break;
	case RES_OUTOFMEMORY:
		astman_send_error(s, m, "Out of memory");
		break;
	}
	return 0;
}

static int manager_remove_queue_member(struct mansession *s, struct message *m)
{
	char *queuename, *interface;

	queuename = astman_get_header(m, "Queue");
	interface = astman_get_header(m, "Interface");

	if (opbx_strlen_zero(queuename) || opbx_strlen_zero(interface)) {
		astman_send_error(s, m, "Need 'Queue' and 'Interface' parameters.");
		return 0;
	}

	switch (remove_from_queue(queuename, interface)) {
	case RES_OKAY:
		astman_send_ack(s, m, "Removed interface from queue");
		break;
	case RES_EXISTS:
		astman_send_error(s, m, "Unable to remove interface: Not there");
		break;
	case RES_NOSUCHQUEUE:
		astman_send_error(s, m, "Unable to remove interface from queue: No such queue");
		break;
	case RES_OUTOFMEMORY:
		astman_send_error(s, m, "Out of memory");
		break;
	}
	return 0;
}

static int manager_pause_queue_member(struct mansession *s, struct message *m)
{
	char *queuename, *interface, *paused_s;
	int paused;

	interface = astman_get_header(m, "Interface");
	paused_s = astman_get_header(m, "Paused");
	queuename = astman_get_header(m, "Queue");	/* Optional - if not supplied, pause the given Interface in all queues */

	if (opbx_strlen_zero(interface) || opbx_strlen_zero(paused_s)) {
		astman_send_error(s, m, "Need 'Interface' and 'Paused' parameters.");
		return 0;
	}

	paused = abs(opbx_true(paused_s));

	if (set_member_paused(queuename, interface, paused))
		astman_send_error(s, m, "Interface not found");
	else
		if (paused)
			astman_send_ack(s, m, "Interface paused successfully");
		else
			astman_send_ack(s, m, "Interface unpaused successfully");

	return 0;
}

static int handle_add_queue_member(int fd, int argc, char *argv[])
{
	char *queuename, *interface;
	int penalty;

	if ((argc != 6) && (argc != 8)) {
		return RESULT_SHOWUSAGE;
	} else if (strcmp(argv[4], "to")) {
		return RESULT_SHOWUSAGE;
	} else if ((argc == 8) && strcmp(argv[6], "penalty")) {
		return RESULT_SHOWUSAGE;
	}

	queuename = argv[5];
	interface = argv[3];
	if (argc == 8) {
		if (sscanf(argv[7], "%d", &penalty) == 1) {
			if (penalty < 0) {
				opbx_cli(fd, "Penalty must be >= 0\n");
				penalty = 0;
			}
		} else {
			opbx_cli(fd, "Penalty must be an integer >= 0\n");
			penalty = 0;
		}
	} else {
		penalty = 0;
	}

	switch (add_to_queue(queuename, interface, penalty, 0, queue_persistent_members)) {
	case RES_OKAY:
		opbx_cli(fd, "Added interface '%s' to queue '%s'\n", interface, queuename);
		return RESULT_SUCCESS;
	case RES_EXISTS:
		opbx_cli(fd, "Unable to add interface '%s' to queue '%s': Already there\n", interface, queuename);
		return RESULT_FAILURE;
	case RES_NOSUCHQUEUE:
		opbx_cli(fd, "Unable to add interface to queue '%s': No such queue\n", queuename);
		return RESULT_FAILURE;
	case RES_OUTOFMEMORY:
		opbx_cli(fd, "Out of memory\n");
		return RESULT_FAILURE;
	default:
		return RESULT_FAILURE;
	}
}

static char *complete_add_queue_member(char *line, char *word, int pos, int state)
{
	/* 0 - add; 1 - queue; 2 - member; 3 - <member>; 4 - to; 5 - <queue>; 6 - penalty; 7 - <penalty> */
	switch (pos) {
	case 3:
		/* Don't attempt to complete name of member (infinite possibilities) */
		return NULL;
	case 4:
		if (state == 0) {
			return strdup("to");
		} else {
			return NULL;
		}
	case 5:
		/* No need to duplicate code */
		return complete_queue(line, word, pos, state);
	case 6:
		if (state == 0) {
			return strdup("penalty");
		} else {
			return NULL;
		}
	case 7:
		if (state < 100) {	/* 0-99 */
			char *num = malloc(3);
			if (num) {
				sprintf(num, "%d", state);
			}
			return num;
		} else {
			return NULL;
		}
	default:
		return NULL;
	}
}

static int handle_remove_queue_member(int fd, int argc, char *argv[])
{
	char *queuename, *interface;

	if (argc != 6) {
		return RESULT_SHOWUSAGE;
	} else if (strcmp(argv[4], "from")) {
		return RESULT_SHOWUSAGE;
	}

	queuename = argv[5];
	interface = argv[3];

	switch (remove_from_queue(queuename, interface)) {
	case RES_OKAY:
		opbx_cli(fd, "Removed interface '%s' from queue '%s'\n", interface, queuename);
		return RESULT_SUCCESS;
	case RES_EXISTS:
		opbx_cli(fd, "Unable to remove interface '%s' from queue '%s': Not there\n", interface, queuename);
		return RESULT_FAILURE;
	case RES_NOSUCHQUEUE:
		opbx_cli(fd, "Unable to remove interface from queue '%s': No such queue\n", queuename);
		return RESULT_FAILURE;
	case RES_OUTOFMEMORY:
		opbx_cli(fd, "Out of memory\n");
		return RESULT_FAILURE;
	default:
		return RESULT_FAILURE;
	}
}

static char *complete_remove_queue_member(char *line, char *word, int pos, int state)
{
	int which = 0;
	struct opbx_call_queue *q;
	struct member *m;

	/* 0 - add; 1 - queue; 2 - member; 3 - <member>; 4 - to; 5 - <queue> */
	if ((pos > 5) || (pos < 3)) {
		return NULL;
	}
	if (pos == 4) {
		if (state == 0) {
			return strdup("from");
		} else {
			return NULL;
		}
	}

	if (pos == 5) {
		/* No need to duplicate code */
		return complete_queue(line, word, pos, state);
	}

	if (queues != NULL) {
		for (q = queues ; q ; q = q->next) {
			opbx_mutex_lock(&q->lock);
			for (m = q->members ; m ; m = m->next) {
				if (++which > state) {
					opbx_mutex_unlock(&q->lock);
					return strdup(m->interface);
				}
			}
			opbx_mutex_unlock(&q->lock);
		}
	}
	return NULL;
}

static char show_queues_usage[] = 
"Usage: show queues\n"
"       Provides summary information on call queues.\n";

static struct opbx_cli_entry cli_show_queues = {
	{ "show", "queues", NULL }, queues_show, 
	"Show status of queues", show_queues_usage, NULL };

static char show_queue_usage[] = 
"Usage: show queue\n"
"       Provides summary information on a specified queue.\n";

static struct opbx_cli_entry cli_show_queue = {
	{ "show", "queue", NULL }, queue_show, 
	"Show status of a specified queue", show_queue_usage, complete_queue };

static char aqm_cmd_usage[] =
"Usage: add queue member <channel> to <queue> [penalty <penalty>]\n";

static struct opbx_cli_entry cli_add_queue_member = {
	{ "add", "queue", "member", NULL }, handle_add_queue_member,
	"Add a channel to a specified queue", aqm_cmd_usage, complete_add_queue_member };

static char rqm_cmd_usage[] =
"Usage: remove queue member <channel> from <queue>\n";

static struct opbx_cli_entry cli_remove_queue_member = {
	{ "remove", "queue", "member", NULL }, handle_remove_queue_member,
	"Removes a channel from a specified queue", rqm_cmd_usage, complete_remove_queue_member };

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	opbx_cli_unregister(&cli_show_queue);
	opbx_cli_unregister(&cli_show_queues);
	opbx_cli_unregister(&cli_add_queue_member);
	opbx_cli_unregister(&cli_remove_queue_member);
	opbx_manager_unregister("Queues");
	opbx_manager_unregister("QueueStatus");
	opbx_manager_unregister("QueueAdd");
	opbx_manager_unregister("QueueRemove");
	opbx_manager_unregister("QueuePause");
	opbx_devstate_del(statechange_queue, NULL);
	opbx_unregister_application(app_aqm);
	opbx_unregister_application(app_rqm);
	opbx_unregister_application(app_pqm);
	opbx_unregister_application(app_upqm);
	opbx_custom_function_unregister(&queueagentcount_function);
	return opbx_unregister_application(app);
}

int load_module(void)
{
	int res;
	res = opbx_register_application(app, queue_exec, synopsis, descrip);
	if (!res) {
		opbx_cli_register(&cli_show_queue);
		opbx_cli_register(&cli_show_queues);
		opbx_cli_register(&cli_add_queue_member);
		opbx_cli_register(&cli_remove_queue_member);
		opbx_devstate_add(statechange_queue, NULL);
		opbx_manager_register( "Queues", 0, manager_queues_show, "Queues" );
		opbx_manager_register( "QueueStatus", 0, manager_queues_status, "Queue Status" );
		opbx_manager_register( "QueueAdd", EVENT_FLAG_AGENT, manager_add_queue_member, "Add interface to queue." );
		opbx_manager_register( "QueueRemove", EVENT_FLAG_AGENT, manager_remove_queue_member, "Remove interface from queue." );
		opbx_manager_register( "QueuePause", EVENT_FLAG_AGENT, manager_pause_queue_member, "Makes a queue member temporarily unavailable" );
		opbx_register_application(app_aqm, aqm_exec, app_aqm_synopsis, app_aqm_descrip) ;
		opbx_register_application(app_rqm, rqm_exec, app_rqm_synopsis, app_rqm_descrip) ;
		opbx_register_application(app_pqm, pqm_exec, app_pqm_synopsis, app_pqm_descrip) ;
		opbx_register_application(app_upqm, upqm_exec, app_upqm_synopsis, app_upqm_descrip) ;
		opbx_custom_function_register(&queueagentcount_function);
	}
	reload_queues();
	
	if (queue_persistent_members)
	    reload_queue_members();

	return res;
}


int reload(void)
{
	reload_queues();
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


