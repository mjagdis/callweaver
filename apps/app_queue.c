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
 * \brief True call queues with optional send URL on answer
 * 
 * \arg Config in \ref Config_qu queues.conf
 *
 * \par Development notes
 * \note 2004-11-25: Persistent Dynamic Members added by:
 *             NetNation Communications (www.netnation.com)
 *             Kevin Lindsay <kevinl@netnation.com>
 * 
 *             Each dynamic agent in each queue is now stored in the cwdb.
 *             When callweaver is restarted, each agent will be automatically
 *             read into their recorded queues. This feature can be
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
 * Added Priority jumping code for adding and removing queue members by Jonathan Stanton <callweaver@doilooklikeicare.com>
 *
 * Fixed to work with CVS as of 2004-02-25 and released as 1.07a
 * by Matthew Enger <m.enger@xi.com.au>
 *
 * \ingroup applications
 */
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/options.h"
#include "callweaver/app.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/say.h"
#include "callweaver/features.h"
#include "callweaver/musiconhold.h"
#include "callweaver/cli.h"
#include "callweaver/manager.h"
#include "callweaver/config.h"
#include "callweaver/monitor.h"
#include "callweaver/utils.h"
#include "callweaver/causes.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/devicestate.h"
#include "callweaver/keywords.h"


static void *queueagentcount_function;
static const char queueagentcount_func_name[] = "QUEUEAGENTCOUNT";
static const char queueagentcount_func_synopsis[] = "Count number of agents answering a queue";
static const char queueagentcount_func_syntax[] = "QUEUEAGENTCOUNT(queuename)";
static const char queueagentcount_func_desc[] = "";


#define QUEUE_STRATEGY_RINGALL		0
#define QUEUE_STRATEGY_ROUNDROBIN	1
#define QUEUE_STRATEGY_LEASTRECENT	2
#define QUEUE_STRATEGY_FEWESTCALLS	3
#define QUEUE_STRATEGY_RANDOM		4
#define QUEUE_STRATEGY_RRMEMORY		5

static struct strategy
{
    unsigned int strategy;
    const char *name;
} strategies[] =
{
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

static const char tdesc[] = "True Call Queueing";

static void *app;
static const char name[] = "Queue";
static const char synopsis[] = "Queue a call for a call queue";
static const char syntax[] = "Queue(queuename[, options[, URL[, announceoverride[, timeout]]]])";
static const char descrip[] =
    "Queues an incoming call in a particular call queue as defined in queues.conf.\n"
    "This application will return to the dialplan if the queue does not exist, or\n"
    "any of the join options cause the caller not to enter the queue.\n"
    "The option string may contain zero or more of the following characters:\n"
    "      'd' -- data-quality (modem) call (minimum delay)\n"
    "      'h' -- allow callee to hang up by hitting *\n"
    "      'H' -- allow caller to hang up by hitting *\n"
    "      'n' -- no retries on the timeout; will exit this application and \n"
    "	      go to the next step\n"
    "      'r' -- ring instead of playing MOH\n"
    "      't' -- allow the called user to transfer the calling user\n"
    "      'T' -- to allow the calling user to transfer the called user\n"
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

static void *app_aqm;
static const char name_aqm[] = "AddQueueMember" ;
static const char app_aqm_synopsis[] = "Dynamically adds queue members" ;
static const char app_aqm_syntax[] = "AddQueueMember(queuename[, interface[, penalty]])";
static const char app_aqm_descrip[] =
    "Dynamically adds interface to an existing queue.\n"
    "  This application sets the following channel variable upon completion:\n"
    "     AQMSTATUS    The status of the attempt to add a queue member as a \n"
    "                     text string, one of\n"
    "           ADDED | MEMBERALREADY | NOSUCHQUEUE \n"
    "Example: AddQueueMember(techsupport, SIP/3000)\n"
    "";

static void *app_rqm;
static const char name_rqm[] = "RemoveQueueMember" ;
static const char app_rqm_synopsis[] = "Dynamically removes queue members" ;
static const char app_rqm_syntax[] = "RemoveQueueMember(queuename[, interface])";
static const char app_rqm_descrip[] =
    "Dynamically removes interface to an existing queue\n"
    "  This application sets the following channel variable upon completion:\n"
    "     RQMSTATUS      The status of the attempt to remove a queue member as a\n"
    "                     text string, one of\n"
    "           REMOVED | NOTINQUEUE | NOSUCHQUEUE \n"
    "Example: RemoveQueueMember(techsupport, SIP/3000)\n"
    "";

static void *app_pqm;
static const char name_pqm[] = "PauseQueueMember" ;
static const char app_pqm_synopsis[] = "Pauses a queue member" ;
static const char app_pqm_syntax[] = "PauseQueueMember([queuename], interface)";
static const char app_pqm_descrip[] =
    "Pauses (blocks calls for) a queue member.\n"
    "The given interface will be paused in the given queue. This prevents\n"
    "any calls from being sent from the queue to the interface until it is\n"
    "unpaused with UnpauseQueueMember or the manager interface. If no\n"
    "queuename is given, the interface is paused in every queue it is a\n"
    "member of."
    "  This application sets the following channel variable upon completion:\n"
    "     PQMSTATUS      The status of the attempt to pause a queue member as a\n"
    "                     text string, one of\n"
    "           PAUSED | NOTFOUND\n"
    "Example: PauseQueueMember(, SIP/3000)\n";

static void *app_upqm;
static const char name_upqm[] = "UnpauseQueueMember" ;
static const char app_upqm_synopsis[] = "Unpauses a queue member" ;
static const char app_upqm_syntax[] = "UnpauseQueueMember([queuename], interface)";
static const char app_upqm_descrip[] =
    "Unpauses (resumes calls to) a queue member.\n"
    "This is the counterpart to PauseQueueMember and operates exactly the\n"
    "same way, except it unpauses instead of pausing the given interface.\n"
    "  This application sets the following channel variable upon completion:\n"
    "     UPQMSTATUS       The status of the attempt to unpause a queue \n"
    "                      member as a text string, one of\n"
    "            UNPAUSED | NOTFOUND\n"
    "Example: UnpauseQueueMember(, SIP/3000)\n";

/*! \brief Persistent Members cwdb family */
static const char pm_family[] = "/Queue/PersistentMembers";
/* The maximum lengh of each persistent member queue database entry */
#define PM_MAX_LEN 2048

/*! \brief queues.conf [general] option */
static int queue_persistent_members = 0;

/*! \brief queues.conf per-queue weight option */
static int use_weight = 0;

enum queue_result
{
    QUEUE_UNKNOWN = 0,
    QUEUE_TIMEOUT = 1,
    QUEUE_JOINEMPTY = 2,
    QUEUE_LEAVEEMPTY = 3,
    QUEUE_JOINUNAVAIL = 4,
    QUEUE_LEAVEUNAVAIL = 5,
    QUEUE_FULL = 6,
};

const struct
{
    enum queue_result id;
    const char *text;
}
queue_results[] =
{
    { QUEUE_UNKNOWN, "UNKNOWN" },
    { QUEUE_TIMEOUT, "TIMEOUT" },
    { QUEUE_JOINEMPTY,"JOINEMPTY" },
    { QUEUE_LEAVEEMPTY, "LEAVEEMPTY" },
    { QUEUE_JOINUNAVAIL, "JOINUNAVAIL" },
    { QUEUE_LEAVEUNAVAIL, "LEAVEUNAVAIL" },
    { QUEUE_FULL, "FULL" },
};


struct outchan
{
    struct cw_channel *chan;
    char interface[256];
    int stillgoing;
    int metric;
    int oldstatus;
    time_t lastcall;
    struct member *member;
    struct outchan *next;
};


struct queue_ent
{
    struct cw_call_queue *parent;	/*!< What queue is our parent */
    char moh[80];			/*!< Name of musiconhold to be used */
    char announce[80];		/*!< Announcement to play for member when call is answered */
    char context[CW_MAX_CONTEXT];	/*!< Context when user exits queue */
    char digits[CW_MAX_EXTENSION];	/*!< Digits entered while in queue */
    int pos;			/*!< Where we are in the queue */
    int trying_agent;		/*!< Are we trying to reach an agent */
    int prio;			/*!< Our priority */
    int last_pos_said;              /*!< Last position we told the user */
    time_t last_periodic_announce_time;	/*!< The last time we played a periodic anouncement */
    time_t last_pos;                /*!< Last time we told the user their position */
    int opos;			/*!< Where we started in the queue */
    int handled;			/*!< Whether our call was handled */
    time_t start;			/*!< When we started holding */
    time_t expire;			/*!< When this entry should expire (time out of queue) */
    struct cw_channel *chan;	/*!< Our channel */
    struct queue_ent *next;		/*!< The next queue entry */
};

struct member
{
    char interface[80];		/*!< Technology/Location */
    int penalty;		/*!< Are we a last resort? */
    int calls;			/*!< Number of calls serviced by this member */
    int dynamic;		/*!< Are we dynamically added? */
    cw_devicestate_t status;	/*!< Status of queue member */
    int paused;			/*!< Are we paused (not accepting calls)? */
    time_t lastcall;		/*!< When last successful call was hungup */
    int dead;			/*!< Used to detect members deleted in realtime */
    time_t added;		/* used to track when member was added */
    struct member *next;	/*!< Next member */
};

/* values used in multi-bit flags in cw_call_queue */
#define QUEUE_EMPTY_NORMAL 1
#define QUEUE_EMPTY_STRICT 2
#define ANNOUNCEHOLDTIME_ALWAYS 1
#define ANNOUNCEHOLDTIME_ONCE 2

struct cw_call_queue
{
    cw_mutex_t lock;
    char name[80];			/*!< Name */
    char moh[80];			/*!< Music On Hold class to be used */
    char announce[80];		/*!< Announcement to play when call is answered */
    char context[CW_MAX_CONTEXT];	/*!< Exit context */
    unsigned int monjoin: 1;
    unsigned int dead: 1;
    unsigned int joinempty: 2;
    unsigned int eventwhencalled: 1;
    unsigned int leavewhenempty: 2;
    unsigned int reportholdtime: 1;
    unsigned int wrapped: 1;
    unsigned int timeoutrestart: 1;
    unsigned int announceholdtime: 2;
    unsigned int strategy: 3;
    unsigned int maskmemberstatus: 1;
    unsigned int realtime: 1;
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
    char sound_reporthold[80];	    /*!< Sound file: "Hold time" (def. queue-reporthold) */
    char sound_periodicannounce[80];/*!< Sound file: Custom announce, no default */

    int report_maxpos;              /*!< How many position queue are we reporting (ex: if set to 5, 6 and more will not have pos report) */
    unsigned int reportpos_first: 1;/*!< Are we reporting the "you are the next" to the next caller */

    int count;			            /*!< How many entries */
    int maxlen;			            /*!< Max number of entries */
    int wrapuptime;			        /*!< Wrapup Time */

    int retry;			            /*!< Retry calling everyone after this amount of time */
    int timeout;			        /*!< How long to wait for an answer */
    int weight;                     /*!< Respective weight */

    /* Queue strategy things */
    int rrpos;			            /*!< Round Robin - position */
    int memberdelay;		        /*!< Seconds to delay connecting member to caller */

    struct member *members;		    /*!< Head of the list of members */
    struct queue_ent *head;		    /*!< Head of the list of callers */
    struct cw_call_queue *next;	/*!< Next call queue */
};

static struct cw_call_queue *queues = NULL;
CW_MUTEX_DEFINE_STATIC(qlock);

static void set_queue_result(struct cw_channel *chan, enum queue_result res)
{
    int i;

    for (i = 0;  i < sizeof(queue_results)/sizeof(queue_results[0]);  i++)
    {
        if (queue_results[i].id == res)
        {
            pbx_builtin_setvar_helper(chan, "QUEUESTATUS", queue_results[i].text);
            return;
        }
    }
}

static const char *int2strat(int strategy)
{
    int x;

    for (x = 0;  x < sizeof(strategies)/sizeof(strategies[0]);  x++)
    {
        if (strategy == strategies[x].strategy)
            return strategies[x].name;
    }
    return "<unknown>";
}

static int strat2int(const char *strategy)
{
    int x;

    for (x = 0;  x < sizeof(strategies)/sizeof(strategies[0]);  x++)
    {
        if (!strcasecmp(strategy, strategies[x].name))
            return strategies[x].strategy;
    }
    return -1;
}

/*! \brief Insert the 'new' entry after the 'prev' entry of queue 'q' */
static inline void insert_entry(struct cw_call_queue *q, struct queue_ent *prev, struct queue_ent *new, int *pos)
{
    struct queue_ent *cur;

    if (!q  ||  !new)
        return;
    if (prev)
    {
        cur = prev->next;
        prev->next = new;
    }
    else
    {
        cur = q->head;
        q->head = new;
    }
    new->next = cur;
    new->parent = q;
    new->pos = ++(*pos);
    new->opos = *pos;
}

enum queue_member_status
{
    QUEUE_NO_MEMBERS,
    QUEUE_NO_REACHABLE_MEMBERS,
    QUEUE_NORMAL
};

static enum queue_member_status get_member_status(struct cw_call_queue *q)
{
    struct member *member;
    enum queue_member_status result = QUEUE_NO_MEMBERS;

    cw_mutex_lock(&q->lock);
    for (member = q->members; member; member = member->next)
    {
        switch (member->status)
        {
        case CW_DEVICE_INVALID:
            /* nothing to do */
            break;
        case CW_DEVICE_UNAVAILABLE:
            result = QUEUE_NO_REACHABLE_MEMBERS;
            break;
        default:
            cw_mutex_unlock(&q->lock);
            return QUEUE_NORMAL;
        }
    }
    cw_mutex_unlock(&q->lock);
    return result;
}

struct statechange
{
    cw_devicestate_t state;
    char dev[0];
};

static void *changethread(void *data)
{
    struct cw_call_queue *q;
    struct statechange *sc = data;
    struct member *cur;
    char *loc;
    char *technology;

    technology = cw_strdupa(sc->dev);
    loc = strchr(technology, '/');
    if (loc)
    {
        *loc = '\0';
        loc++;
    }
    else
    {
        free(sc);
        return NULL;
    }
    if (option_debug)
        cw_log(CW_LOG_DEBUG, "Device '%s/%s' changed to state '%d' (%s)\n", technology, loc, sc->state, devstate2str(sc->state));
    cw_mutex_lock(&qlock);
    for (q = queues; q; q = q->next)
    {
        cw_mutex_lock(&q->lock);
        cur = q->members;
        while (cur)
        {
            if (!strcasecmp(sc->dev, cur->interface))
            {
                if (cur->status != sc->state)
                {
                    cur->status = sc->state;
                    if (!q->maskmemberstatus)
                    {
                        cw_manager_event(EVENT_FLAG_AGENT, "QueueMemberStatus",
                            8,
                            cw_msg_tuple("Queue",      "%s",  q->name),
                            cw_msg_tuple("Location",   "%s",  cur->interface),
                            cw_msg_tuple("Membership", "%s",  (cur->dynamic ? "dynamic" : "static")),
                            cw_msg_tuple("Penalty",    "%d",  cur->penalty),
                            cw_msg_tuple("CallsTaken", "%d",  cur->calls),
                            cw_msg_tuple("LastCall",   "%ld", cur->lastcall),
                            cw_msg_tuple("Status",     "%d",  cur->status),
                            cw_msg_tuple("Paused",     "%d",  cur->paused)
                        );
                    }
                }
            }
            cur = cur->next;
        }
        cw_mutex_unlock(&q->lock);
    }
    cw_mutex_unlock(&qlock);
    free(sc);
    return NULL;
}

static int statechange_queue(const char *dev, cw_devicestate_t state, void *ign)
{
    /* Avoid potential for deadlocks by spawning a new thread to handle the event */
    struct statechange *sc;
    pthread_t t;

    CW_UNUSED(ign);

    sc = malloc(sizeof(struct statechange) + strlen(dev) + 1);
    if (sc)
    {
        sc->state = state;
        strcpy(sc->dev, dev);
        if (cw_pthread_create(&t, &global_attr_detached, changethread, sc))
        {
            cw_log(CW_LOG_WARNING, "Failed to create update thread!\n");
            free(sc);
        }
    }
    return 0;
}

static struct member *create_queue_member(char *interface, int penalty, int paused)
{
    struct member *cur;

    /* Add a new member */

    if ((cur = malloc(sizeof(struct member))))
    {
        memset(cur, 0, sizeof(struct member));
        cur->penalty = penalty;
        cur->paused = paused;
        cw_copy_string(cur->interface, interface, sizeof(cur->interface));
        if (!strchr(cur->interface, '/'))
            cw_log(CW_LOG_WARNING, "No location at interface '%s'\n", interface);
        cur->status = cw_device_state(interface);
        cur->added = time(NULL);
    }

    return cur;
}

static struct cw_call_queue *alloc_queue(const char *queuename)
{
    struct cw_call_queue *q;

    if ((q = malloc(sizeof(*q))))
    {
        memset(q, 0, sizeof(*q));
        cw_mutex_init(&q->lock);
        cw_copy_string(q->name, queuename, sizeof(q->name));
    }
    return q;
}

static void init_queue(struct cw_call_queue *q)
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
    q->report_maxpos = -1;
    q->reportpos_first = cw_true("yes");

    cw_copy_string(q->sound_next, "queue-youarenext", sizeof(q->sound_next));
    cw_copy_string(q->sound_thereare, "queue-thereare", sizeof(q->sound_thereare));
    cw_copy_string(q->sound_calls, "queue-callswaiting", sizeof(q->sound_calls));
    cw_copy_string(q->sound_holdtime, "queue-holdtime", sizeof(q->sound_holdtime));
    cw_copy_string(q->sound_minutes, "queue-minutes", sizeof(q->sound_minutes));
    cw_copy_string(q->sound_seconds, "queue-seconds", sizeof(q->sound_seconds));
    cw_copy_string(q->sound_thanks, "queue-thankyou", sizeof(q->sound_thanks));
    cw_copy_string(q->sound_lessthan, "queue-less-than", sizeof(q->sound_lessthan));
    cw_copy_string(q->sound_reporthold, "queue-reporthold", sizeof(q->sound_reporthold));
    cw_copy_string(q->sound_periodicannounce, "queue-periodic-announce", sizeof(q->sound_periodicannounce));
}

static void clear_queue(struct cw_call_queue *q)
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
static void queue_set_param(struct cw_call_queue *q, const char *param, const char *val, int linenum, int failunknown)
{
    if (!strcasecmp(param, "music") || !strcasecmp(param, "musiconhold"))
    {
        cw_copy_string(q->moh, val, sizeof(q->moh));
    }
    else if (!strcasecmp(param, "announce"))
    {
        cw_copy_string(q->announce, val, sizeof(q->announce));
    }
    else if (!strcasecmp(param, "context"))
    {
        cw_copy_string(q->context, val, sizeof(q->context));
    }
    else if (!strcasecmp(param, "timeout"))
    {
        q->timeout = atoi(val);
        if (q->timeout < 0)
            q->timeout = DEFAULT_TIMEOUT;
    }
    else if (!strcasecmp(param, "monitor-join"))
    {
        q->monjoin = cw_true(val);
    }
    else if (!strcasecmp(param, "monitor-format"))
    {
        cw_copy_string(q->monfmt, val, sizeof(q->monfmt));
    }
    else if (!strcasecmp(param, "queue-youarenext"))
    {
        cw_copy_string(q->sound_next, val, sizeof(q->sound_next));
    }
    else if (!strcasecmp(param, "queue-thereare"))
    {
        cw_copy_string(q->sound_thereare, val, sizeof(q->sound_thereare));
    }
    else if (!strcasecmp(param, "queue-callswaiting"))
    {
        cw_copy_string(q->sound_calls, val, sizeof(q->sound_calls));
    }
    else if (!strcasecmp(param, "queue-holdtime"))
    {
        cw_copy_string(q->sound_holdtime, val, sizeof(q->sound_holdtime));
    }
    else if (!strcasecmp(param, "queue-minutes"))
    {
        cw_copy_string(q->sound_minutes, val, sizeof(q->sound_minutes));
    }
    else if (!strcasecmp(param, "queue-seconds"))
    {
        cw_copy_string(q->sound_seconds, val, sizeof(q->sound_seconds));
    }
    else if (!strcasecmp(param, "queue-lessthan"))
    {
        cw_copy_string(q->sound_lessthan, val, sizeof(q->sound_lessthan));
    }
    else if (!strcasecmp(param, "queue-thankyou"))
    {
        cw_copy_string(q->sound_thanks, val, sizeof(q->sound_thanks));
    }
    else if (!strcasecmp(param, "queue-reporthold"))
    {
        cw_copy_string(q->sound_reporthold, val, sizeof(q->sound_reporthold));
    }
    else if (!strcasecmp(param, "reportmaxpos"))
    {
        q->report_maxpos = atoi(val);
    }
    else if (!strcasecmp(param, "reportpos-first"))
    {
        q->reportpos_first = cw_true(val);
    }
    else if (!strcasecmp(param, "announce-frequency"))
    {
        q->announcefrequency = atoi(val);
    }
    else if (!strcasecmp(param, "announce-round-seconds"))
    {
        q->roundingseconds = atoi(val);
        if (q->roundingseconds>60 || q->roundingseconds<0)
        {
            if (linenum >= 0)
            {
                cw_log(CW_LOG_WARNING, "'%s' isn't a valid value for %s "
                         "using 0 instead for queue '%s' at line %d of queues.conf\n",
                         val, param, q->name, linenum);
            }
            else
            {
                cw_log(CW_LOG_WARNING, "'%s' isn't a valid value for %s "
                         "using 0 instead for queue '%s'\n", val, param, q->name);
            }
            q->roundingseconds=0;
        }
    }
    else if (!strcasecmp(param, "announce-holdtime"))
    {
        if (!strcasecmp(val, "once"))
            q->announceholdtime = ANNOUNCEHOLDTIME_ONCE;
        else if (cw_true(val))
            q->announceholdtime = ANNOUNCEHOLDTIME_ALWAYS;
        else
            q->announceholdtime = 0;
    }
    else if (!strcasecmp(param, "periodic-announce"))
    {
        cw_copy_string(q->sound_periodicannounce, val, sizeof(q->sound_periodicannounce));
    }
    else if (!strcasecmp(param, "periodic-announce-frequency"))
    {
        q->periodicannouncefrequency = atoi(val);
    }
    else if (!strcasecmp(param, "retry"))
    {
        q->retry = atoi(val);
        if (q->retry <= 0)
            q->retry = DEFAULT_RETRY;
    }
    else if (!strcasecmp(param, "wrapuptime"))
    {
        q->wrapuptime = atoi(val);
    }
    else if (!strcasecmp(param, "maxlen"))
    {
        q->maxlen = atoi(val);
        if (q->maxlen < 0)
            q->maxlen = 0;
    }
    else if (!strcasecmp(param, "servicelevel"))
    {
        q->servicelevel= atoi(val);
    }
    else if (!strcasecmp(param, "strategy"))
    {
        q->strategy = strat2int(val);
    }
    else if (!strcasecmp(param, "joinempty"))
    {
        if (!strcasecmp(val, "strict"))
            q->joinempty = QUEUE_EMPTY_STRICT;
        else if (cw_true(val))
            q->joinempty = QUEUE_EMPTY_NORMAL;
        else
            q->joinempty = 0;
    }
    else if (!strcasecmp(param, "leavewhenempty"))
    {
        if (!strcasecmp(val, "strict"))
            q->leavewhenempty = QUEUE_EMPTY_STRICT;
        else if (cw_true(val))
            q->leavewhenempty = QUEUE_EMPTY_NORMAL;
        else
            q->leavewhenempty = 0;
    }
    else if (!strcasecmp(param, "eventmemberstatus"))
    {
        q->maskmemberstatus = !cw_true(val);
    }
    else if (!strcasecmp(param, "eventwhencalled"))
    {
        q->eventwhencalled = cw_true(val);
    }
    else if (!strcasecmp(param, "reportholdtime"))
    {
        q->reportholdtime = cw_true(val);
    }
    else if (!strcasecmp(param, "memberdelay"))
    {
        q->memberdelay = atoi(val);
    }
    else if (!strcasecmp(param, "weight"))
    {
        q->weight = atoi(val);
        if (q->weight)
            use_weight++;
        /* With Realtime queues, if the last queue using weights is deleted in realtime,
           we will not see any effect on use_weight until next reload. */
    }
    else if (!strcasecmp(param, "timeoutrestart"))
    {
        q->timeoutrestart = cw_true(val);
    }
    else if (failunknown)
    {
        if (linenum >= 0)
        {
            cw_log(CW_LOG_WARNING, "Unknown keyword in queue '%s': %s at line %d of queues.conf\n",
                     q->name, param, linenum);
        }
        else
        {
            cw_log(CW_LOG_WARNING, "Unknown keyword in queue '%s': %s\n", q->name, param);
        }
    }
}

static void rt_handle_member_record(struct cw_call_queue *q, char *interface, const char *penalty_str)
{
    struct member *m, *prev_m;
    int penalty = 0;

    if (penalty_str)
    {
        penalty = atoi(penalty_str);
        if (penalty < 0)
            penalty = 0;
    }

    /* Find the member, or the place to put a new one. */
    prev_m = NULL;
    m = q->members;
    while (m && strcmp(m->interface, interface))
    {
        prev_m = m;
        m = m->next;
    }

    /* Create a new one if not found, else update penalty */
    if (!m)
    {
        m = create_queue_member(interface, penalty, 0);
        if (m)
        {
            m->dead = 0;
            if (prev_m)
            {
                prev_m->next = m;
            }
            else
            {
                q->members = m;
            }
        }
    }
    else
    {
        m->dead = 0;   /* Do not delete this one. */
        m->penalty = penalty;
    }
}


/*! \brief Reload a single queue via realtime.
  \return Return the queue, or NULL if it doesn't exist.
  \note Should be called with the global qlock locked.
   When found, the queue is returned with q->lock locked. */
static struct cw_call_queue *reload_queue_rt(const char *queuename, struct cw_variable *queue_vars, struct cw_config *member_config)
{
    struct cw_variable *v;
    struct cw_call_queue *q, *prev_q;
    struct member *m, *prev_m, *next_m;
    char *interface;
    char *tmp, *tmp_name;
    char tmpbuf[64];    /* Must be longer than the longest queue param name. */

    /* Find the queue in the in-core list (we will create a new one if not found). */
    cw_mutex_lock(&qlock);
    q = queues;
    prev_q = NULL;
    while (q)
    {
        if (!strcasecmp(q->name, queuename))
        {
            break;
        }
        q = q->next;
        prev_q = q;
    }
    cw_mutex_unlock(&qlock);
    /* Static queues override realtime. */
    if (q)
    {
        cw_mutex_lock(&q->lock);
        if (!q->realtime)
        {
            if (q->dead)
            {
                cw_mutex_unlock(&q->lock);
                return NULL;
            }
            return q;
        }
    }
    else if (!member_config)
        /* Not found in the list, and it's not realtime ... */
        return NULL;

    /* Check if queue is defined in realtime. */
    if (!queue_vars)
    {
        /* Delete queue from in-core list if it has been deleted in realtime. */
        if (q)
        {
            /*! \note Hmm, can't seem to distinguish a DB failure from a not
               found condition... So we might delete an in-core queue
               in case of DB failure. */
            cw_log(CW_LOG_DEBUG, "Queue %s not found in realtime.\n", queuename);

            q->dead = 1;
            /* Delete if unused (else will be deleted when last caller leaves). */
            if (!q->count)
            {
                /* Delete. */
                if (!prev_q)
                {
                    queues = q->next;
                }
                else
                {
                    prev_q->next = q->next;
                }
                cw_mutex_unlock(&q->lock);
                free(q);
            }
            else
            {
                cw_mutex_unlock(&q->lock);
            }
        }
        return NULL;
    }

    /* Create a new queue if an in-core entry does not exist yet. */
    if (!q)
    {
        q = alloc_queue(queuename);
        if (!q)
            return NULL;
        cw_mutex_lock(&q->lock);
        clear_queue(q);
        q->realtime = 1;
        q->next = queues;
        queues = q;
    }
    init_queue(q);      /* Ensure defaults for all parameters not set explicitly. */

    v = queue_vars;
    memset(tmpbuf, 0, sizeof(tmpbuf));
    while (v)
    {
        /* Convert to dashes `-' from underscores `_' as the latter are more SQL friendly. */
        if ((tmp = strchr(v->name, '_')) != NULL)
        {
            cw_copy_string(tmpbuf, v->name, sizeof(tmpbuf));
            tmp_name = tmpbuf;
            tmp = tmp_name;
            while ((tmp = strchr(tmp, '_')) != NULL)
                *tmp++ = '-';
        }
        else
            tmp_name = v->name;
        queue_set_param(q, tmp_name, v->value, -1, 0);
        v = v->next;
    }

    /* Temporarily set members dead so we can detect deleted ones. */
    m = q->members;
    while (m)
    {
        m->dead = 1;
        m = m->next;
    }

    interface = cw_category_browse(member_config, NULL);
    while (interface)
    {
        rt_handle_member_record(q, interface, cw_variable_retrieve(member_config, interface, "penalty"));
        interface = cw_category_browse(member_config, interface);
    }

    /* Delete all realtime members that have been deleted in DB. */
    m = q->members;
    prev_m = NULL;
    while (m)
    {
        next_m = m->next;
        if (m->dead)
        {
            if (prev_m)
                prev_m->next = next_m;
            else
                q->members = next_m;
            free(m);
        }
        else
        {
            prev_m = m;
        }
        m = next_m;
    }
    return q;
}

static int join_queue(char *queuename, struct queue_ent *qe, enum queue_result *reason)
{
    struct cw_variable *queue_vars = NULL;
    struct cw_config *member_config = NULL;
    struct cw_call_queue *q;
    struct queue_ent *cur, *prev = NULL;
    int res = -1;
    int pos = 0;
    int inserted = 0;
    enum queue_member_status status;

    /*! \note Load from realtime before taking the global qlock, to avoid blocking all
       queue operations while waiting for the DB.

       This will be two separate database transactions, so we might
       see queue parameters as they were before another process
       changed the queue and member list as it was after the change.
       Thus we might see an empty member list when a queue is
       deleted. In practise, this is unlikely to cause a problem. */
    queue_vars = cw_load_realtime("queues", "name", queuename, NULL);
    if (queue_vars)
    {
        member_config = cw_load_realtime_multientry("queue_members", "interface LIKE", "%", "queue_name", queuename, NULL);
        if (!member_config)
        {
            cw_log(CW_LOG_ERROR, "no queue_members defined in your config (extconfig.conf).\n");
            return res;
        }
    }

    cw_mutex_lock(&qlock);
    q = reload_queue_rt(queuename, queue_vars, member_config);
    /* Note: If found, reload_queue_rt() returns with q->lock locked. */
    if (member_config)
        cw_config_destroy(member_config);
    if (queue_vars)
        cw_variables_destroy(queue_vars);

    if (!q)
    {
        cw_mutex_unlock(&qlock);
        return res;
    }

    /* This is our one */
    status = get_member_status(q);
    if (!q->joinempty  &&  (status == QUEUE_NO_MEMBERS))
        *reason = QUEUE_JOINEMPTY;
    else if ((q->joinempty == QUEUE_EMPTY_STRICT)  &&  (status == QUEUE_NO_REACHABLE_MEMBERS))
        *reason = QUEUE_JOINUNAVAIL;
    else if (q->maxlen  &&  (q->count >= q->maxlen))
        *reason = QUEUE_FULL;
    else
    {
        /* There's space for us, put us at the right position inside
         * the queue. 
         * Take into account the priority of the calling user */
        inserted = 0;
        prev = NULL;
        cur = q->head;
        while (cur)
        {
            /* We have higher priority than the current user, enter
             * before him, after all the other users with priority
             * higher or equal to our priority. */
            if ((!inserted) && (qe->prio > cur->prio))
            {
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
        cw_copy_string(qe->moh, q->moh, sizeof(qe->moh));
        cw_copy_string(qe->announce, q->announce, sizeof(qe->announce));
        cw_copy_string(qe->context, q->context, sizeof(qe->context));
        q->count++;
        res = 0;
        cw_manager_event(EVENT_FLAG_CALL, "Join",
            6,
            cw_msg_tuple("Channel",      "%s", qe->chan->name),
            cw_msg_tuple("CallerID",     "%s", (qe->chan->cid.cid_num ? qe->chan->cid.cid_num : "unknown")),
            cw_msg_tuple("CallerIDName", "%s", (qe->chan->cid.cid_name ? qe->chan->cid.cid_name : "unknown")),
            cw_msg_tuple("Queue",        "%s", q->name),
            cw_msg_tuple("Position",     "%d", qe->pos),
            cw_msg_tuple("Count",        "%d", q->count)
        );
#if 0
        cw_log(CW_LOG_NOTICE, "Queue '%s' Join, Channel '%s', Position '%d'\n", q->name, qe->chan->name, qe->pos );
#endif

    }
    cw_mutex_unlock(&q->lock);
    cw_mutex_unlock(&qlock);
    return res;
}

static void free_members(struct cw_call_queue *q, int all)
{
    /* Free non-dynamic members */
    struct member *curm, *next, *prev;

    curm = q->members;
    prev = NULL;
    while (curm)
    {
        next = curm->next;
        if (all  ||  !curm->dynamic)
        {
            if (prev)
                prev->next = next;
            else
                q->members = next;
            free(curm);
        }
        else
            prev = curm;
        curm = next;
    }
}

static void destroy_queue(struct cw_call_queue *q)
{
    struct cw_call_queue *cur, *prev = NULL;

    cw_mutex_lock(&qlock);
    for (cur = queues;  cur;  cur = cur->next)
    {
        if (cur == q)
        {
            if (prev)
                prev->next = cur->next;
            else
                queues = cur->next;
        }
        else
        {
            prev = cur;
        }
    }
    cw_mutex_unlock(&qlock);
    free_members(q, 1);
    cw_mutex_destroy(&q->lock);
    free(q);
}

static int play_file(struct cw_channel *chan, char *filename)
{
    int res=0;

    if (!cw_strlen_zero(filename))
    {
        cw_stopstream(chan);
        res = cw_streamfile(chan, filename, chan->language);

        if (!res)
            res = cw_waitstream(chan, CW_DIGIT_ANY);
        else
            res = 0;

        cw_stopstream(chan);
    }
    return res;
}

static int valid_exit(struct queue_ent *qe, char digit)
{
    int digitlen = strlen(qe->digits);

    /* Prevent possible buffer overflow */
    if (digitlen < sizeof(qe->digits) - 2)
    {
        qe->digits[digitlen] = digit;
        qe->digits[digitlen + 1] = '\0';
    }
    else
    {
        qe->digits[0] = '\0';
        return 0;
    }

    /* If there's no context to goto, short-circuit */
    if (cw_strlen_zero(qe->context))
        return 0;

    /* If the extension is bad, then reset the digits to blank */
    if (!cw_canmatch_extension(qe->chan, qe->context, qe->digits, 1, qe->chan->cid.cid_num))
    {
        qe->digits[0] = '\0';
        return 0;
    }

    /* We have an exact match */
    if (!cw_goto_if_exists_n(qe->chan, qe->context, qe->digits, 1))
    {
        /* Return 1 on a successful goto */
        return 1;
    }
    return 0;
}

static int say_position(struct queue_ent *qe)
{
    int res = 0;
    int avgholdmins;
    int avgholdsecs;
    time_t now;

    /* Check to see if this is ludicrous -- if we just announced position, don't do it again*/
    time(&now);
    if ((now - qe->last_pos) < 15)
        return 0;

    /* If either our position has changed, or we are over the freq timer, say position */
    if ( (qe->last_pos_said == qe->pos) && ((now - qe->last_pos) < qe->parent->announcefrequency) )
        return 0;

    cw_moh_stop(qe->chan);
    /* Say we're next, if we are */
    if (qe->pos == 1)
    {
        if (qe->parent->reportpos_first)
        {
            res = play_file(qe->chan, qe->parent->sound_next);
            if (res  &&  valid_exit(qe, res))
                goto playout;
            goto posout;
        }
        else
        {
            goto playout;
        }
    }
    else
    {
        if (qe->pos <= qe->parent->report_maxpos)
        {
            res = play_file(qe->chan, qe->parent->sound_thereare);
            if (res && valid_exit(qe, res))
                goto playout;
            res = cw_say_number(qe->chan, qe->pos, CW_DIGIT_ANY, qe->chan->language, (char *) NULL); /* Needs gender */
            if (res && valid_exit(qe, res))
                goto playout;
            res = play_file(qe->chan, qe->parent->sound_calls);
            if (res && valid_exit(qe, res))
                goto playout;
        }
        else
        {
            goto playout;
        }
    }
    /* Round hold time to nearest minute */
    avgholdmins = abs(((qe->parent->holdtime + 30) - (now - qe->start))/60);

    /* If they have specified a rounding then round the seconds as well */
    if (qe->parent->roundingseconds)
    {
        avgholdsecs = (abs(((qe->parent->holdtime + 30) - (now - qe->start))) - 60 * avgholdmins)/qe->parent->roundingseconds;
        avgholdsecs *= qe->parent->roundingseconds;
    }
    else
    {
        avgholdsecs = 0;
    }

    if (option_verbose > 2)
        cw_verbose(VERBOSE_PREFIX_3 "Hold time for %s is %d minutes %d seconds\n", qe->parent->name, avgholdmins, avgholdsecs);

    /* If the hold time is >1 min, if it's enabled, and if it's not
       supposed to be only once and we have already said it, say it */
    if ((avgholdmins+avgholdsecs) > 0 && (qe->parent->announceholdtime) &&
            (!(qe->parent->announceholdtime == ANNOUNCEHOLDTIME_ONCE) && qe->last_pos))
    {
        res = play_file(qe->chan, qe->parent->sound_holdtime);
        if (res && valid_exit(qe, res))
            goto playout;

        if (avgholdmins>0)
        {
            if (avgholdmins < 2)
            {
                res = play_file(qe->chan, qe->parent->sound_lessthan);
                if (res && valid_exit(qe, res))
                    goto playout;

                res = cw_say_number(qe->chan, 2, CW_DIGIT_ANY, qe->chan->language, (char *)NULL);
                if (res && valid_exit(qe, res))
                    goto playout;
            }
            else
            {
                res = cw_say_number(qe->chan, avgholdmins, CW_DIGIT_ANY, qe->chan->language, (char*) NULL);
                if (res && valid_exit(qe, res))
                    goto playout;
            }

            res = play_file(qe->chan, qe->parent->sound_minutes);
            if (res && valid_exit(qe, res))
                goto playout;
        }
        if (avgholdsecs>0)
        {
            res = cw_say_number(qe->chan, avgholdsecs, CW_DIGIT_ANY, qe->chan->language, (char*) NULL);
            if (res && valid_exit(qe, res))
                goto playout;

            res = play_file(qe->chan, qe->parent->sound_seconds);
            if (res && valid_exit(qe, res))
                goto playout;
        }

    }

posout:
    if (option_verbose > 2)
        cw_verbose(VERBOSE_PREFIX_3 "Told %s in %s their queue position (which was %d)\n",
                     qe->chan->name, qe->parent->name, qe->pos);
    if (!cw_strlen_zero(qe->parent->sound_thanks))
        res = play_file(qe->chan, qe->parent->sound_thanks);

playout:
    /* Set our last_pos indicators */
    qe->last_pos = now;
    qe->last_pos_said = qe->pos;
    cw_moh_start(qe->chan, qe->moh);

    return res;
}

static void recalc_holdtime(struct queue_ent *qe)
{
    int oldvalue, newvalue;

    /* Calculate holdtime using a recursive boxcar filter */
    /* Thanks to SRT for this contribution */
    /* 2^2 (4) is the filter coefficient; a higher exponent would give old entries more weight */

    newvalue = time(NULL) - qe->start;

    cw_mutex_lock(&qe->parent->lock);
    if (newvalue <= qe->parent->servicelevel)
        qe->parent->callscompletedinsl++;
    oldvalue = qe->parent->holdtime;
    qe->parent->holdtime = (((oldvalue << 2) - oldvalue) + newvalue) >> 2;
    cw_mutex_unlock(&qe->parent->lock);
}


static void leave_queue(struct queue_ent *qe)
{
    struct cw_call_queue *q;
    struct queue_ent *cur, *prev = NULL;
    int pos = 0;

    if ((q = qe->parent) == NULL)
        return;
    cw_mutex_lock(&q->lock);

    prev = NULL;
    cur = q->head;
    while (cur)
    {
        if (cur == qe)
        {
            q->count--;

            /* Take us out of the queue */
            cw_manager_event(EVENT_FLAG_CALL, "Leave",
                3,
                cw_msg_tuple("Channel", "%s", qe->chan->name),
                cw_msg_tuple("Queue",   "%s", q->name),
                cw_msg_tuple("Count",   "%d", q->count)
            );
#if 0
            cw_log(CW_LOG_NOTICE, "Queue '%s' Leave, Channel '%s'\n", q->name, qe->chan->name );
#endif
            /* Take us out of the queue */
            if (prev)
                prev->next = cur->next;
            else
                q->head = cur->next;
        }
        else
        {
            /* Renumber the people after us in the queue based on a new count */
            cur->pos = ++pos;
            prev = cur;
        }
        cur = cur->next;
    }
    cw_mutex_unlock(&q->lock);
    if (q->dead && !q->count)
    {
        /* It's dead and nobody is in it, so kill it */
        destroy_queue(q);
    }
}

/* Hang up a list of outgoing calls */
static void hangupcalls(struct outchan *outgoing, struct cw_channel *exception)
{
    struct outchan *oo;

    while (outgoing)
    {
        /* Hangup any existing lines we have open */
        if (outgoing->chan && (outgoing->chan != exception))
            cw_hangup(outgoing->chan);
        oo = outgoing;
        outgoing=outgoing->next;
        free(oo);
    }
}

static int update_status(struct cw_call_queue *q, struct member *member, cw_devicestate_t status)
{
    struct member *cur;

    /* Since a reload could have taken place, we have to traverse the list to
       be sure it's still valid */
    cw_mutex_lock(&q->lock);
    cur = q->members;
    while (cur)
    {
        if (member == cur)
        {
            cur->status = status;
            if (!q->maskmemberstatus)
            {
                cw_manager_event(EVENT_FLAG_AGENT, "QueueMemberStatus",
                    8,
                    cw_msg_tuple("Queue",      "%s",  q->name),
                    cw_msg_tuple("Location",   "%s",  cur->interface),
                    cw_msg_tuple("Membership", "%s",  (cur->dynamic ? "dynamic" : "static")),
                    cw_msg_tuple("Penalty",    "%d",  cur->penalty),
                    cw_msg_tuple("CallsTaken", "%d",  cur->calls),
                    cw_msg_tuple("LastCall",   "%ld", cur->lastcall),
                    cw_msg_tuple("Status",     "%d",  cur->status),
                    cw_msg_tuple("Paused",     "%d",  cur->paused)
		);
            }
            break;
        }
        cur = cur->next;
    }
    cw_mutex_unlock(&q->lock);
    return 0;
}

static int update_dial_status(struct cw_call_queue *q, struct member *member, int cause)
{
    cw_devicestate_t status = CW_DEVICE_UNKNOWN;

    if (cause == CW_CAUSE_BUSY)
        status = CW_DEVICE_BUSY;
    else if (cause == CW_CAUSE_UNREGISTERED)
        status = CW_DEVICE_UNAVAILABLE;
    else if (cause == CW_CAUSE_NOSUCHDRIVER)
        status = CW_DEVICE_INVALID;

    return update_status(q, member, status);
}

/* traverse all defined queues which have calls waiting and contain this member
   return 0 if no other queue has precedence (higher weight) or 1 if found  */
static int compare_weight(struct cw_call_queue *rq, struct member *member)
{
    struct cw_call_queue *q;
    struct member *mem;
    int found = 0;

    /* &qlock and &rq->lock already set by try_calling()
     * to solve deadlock */
    for (q = queues; q; q = q->next)
    {
        if (q == rq) /* don't check myself, could deadlock */
            continue;
        cw_mutex_lock(&q->lock);
        if (q->count  &&  q->members)
        {
            for (mem = q->members; mem; mem = mem->next)
            {
                if (!strcmp(mem->interface, member->interface))
                {
                    cw_log(CW_LOG_DEBUG, "Found matching member %s in queue '%s'\n", mem->interface, q->name);
                    if (q->weight > rq->weight)
                    {
                        cw_log(CW_LOG_DEBUG, "Queue '%s' (weight %d, calls %d) is preferred over '%s' (weight %d, calls %d)\n", q->name, q->weight, q->count, rq->name, rq->weight, rq->count);
                        found = 1;
                        break;
                    }
                }
            }
        }
        cw_mutex_unlock(&q->lock);
        if (found)
            break;
    }
    return found;
}

static int ring_entry(struct queue_ent *qe, struct outchan *tmp, int *busies)
{
    char tech[256];
    const char *location;
    char *p;
    int res;
    int cause;

    if (qe->parent->wrapuptime && (time(NULL) - tmp->lastcall < qe->parent->wrapuptime))
    {
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "Wrapuptime not yet expired for %s\n", tmp->interface);
        if (qe->chan->cdr)
            cw_cdr_busy(qe->chan->cdr);
        tmp->stillgoing = 0;
        (*busies)++;
        return 0;
    }

    if (tmp->member->paused)
    {
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "%s paused, can't receive call\n", tmp->interface);
        if (qe->chan->cdr)
            cw_cdr_busy(qe->chan->cdr);
        tmp->stillgoing = 0;
        return 0;
    }
    if (use_weight && compare_weight(qe->parent,tmp->member))
    {
        cw_log(CW_LOG_DEBUG, "Priority queue delaying call to %s:%s\n", qe->parent->name, tmp->interface);
        if (qe->chan->cdr)
            cw_cdr_busy(qe->chan->cdr);
        tmp->stillgoing = 0;
        (*busies)++;
        return 0;
    }

    cw_copy_string(tech, tmp->interface, sizeof(tech));
    if ((p = strchr(tech, '/'))) {
        *p = '\0';
	location = p + 1;
    } else
        location = "";

    /* Request the peer */
    tmp->chan = cw_request(tech, qe->chan->nativeformats, (void *)location, &cause);
    if (!tmp->chan)
    {           /* If we can't, just go on to the next call */
#if 0
        cw_log(CW_LOG_NOTICE, "Unable to create channel of type '%s' for Queue\n", cur->tech);
#endif

        if (qe->chan->cdr)
            cw_cdr_busy(qe->chan->cdr);
        tmp->stillgoing = 0;
        update_dial_status(qe->parent, tmp->member, cause);

        cw_mutex_lock(&qe->parent->lock);
        qe->parent->rrpos++;
        cw_mutex_unlock(&qe->parent->lock);

        (*busies)++;
        return 0;
    }
    else if (cause != tmp->oldstatus)
        update_dial_status(qe->parent, tmp->member, cause);

    tmp->chan->appl = "AppQueue (Outgoing Line)";
    tmp->chan->whentohangup = 0;
    free(tmp->chan->cid.cid_num);
    tmp->chan->cid.cid_num = NULL;
    free(tmp->chan->cid.cid_name);
    tmp->chan->cid.cid_name = NULL;
    free(tmp->chan->cid.cid_ani);
    tmp->chan->cid.cid_ani = NULL;
    if (qe->chan->cid.cid_num)
        tmp->chan->cid.cid_num = strdup(qe->chan->cid.cid_num);
    if (qe->chan->cid.cid_name)
        tmp->chan->cid.cid_name = strdup(qe->chan->cid.cid_name);
    if (qe->chan->cid.cid_ani)
        tmp->chan->cid.cid_ani = strdup(qe->chan->cid.cid_ani);

    /* Inherit specially named variables from parent channel */
    cw_var_inherit(&tmp->chan->vars, &qe->chan->vars);

    /* Presense of ADSI CPE on outgoing channel follows ours */
    tmp->chan->adsicpe = qe->chan->adsicpe;

    res = cw_call(tmp->chan, location);
    if (res)
    {
        /* Again, keep going even if there's an error */
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "CW call on peer returned %d\n", res);
        else if (option_verbose > 2)
            cw_verbose(VERBOSE_PREFIX_3 "Couldn't call %s\n", tmp->interface);
        cw_hangup(tmp->chan);
        tmp->chan = NULL;
        tmp->stillgoing = 0;
        (*busies)++;
        return 0;
    }
    else
    {
        if (qe->parent->eventwhencalled)
        {
            cw_manager_event(EVENT_FLAG_AGENT, "AgentCalled",
                7,
                cw_msg_tuple("AgentCalled",    "%s", tmp->interface),
                cw_msg_tuple("ChannelCalling", "%s", qe->chan->name),
                cw_msg_tuple("CallerID",       "%s", (tmp->chan->cid.cid_num ? tmp->chan->cid.cid_num : "unknown")),
                cw_msg_tuple("CallerIDName",   "%s", (tmp->chan->cid.cid_name ? tmp->chan->cid.cid_name : "unknown")),
                cw_msg_tuple("Context",        "%s", qe->chan->context),
                cw_msg_tuple("Extension",      "%s", qe->chan->exten),
                cw_msg_tuple("Priority",       "%d", qe->chan->priority)
            );
        }
        if (option_verbose > 2)
            cw_verbose(VERBOSE_PREFIX_3 "Called %s\n", tmp->interface);
    }
    return 1;
}

static int ring_one(struct queue_ent *qe, struct outchan *outgoing, int *busies)
{
    struct outchan *cur;
    struct outchan *best;
    int bestmetric=0;

    do
    {
        best = NULL;
        cur = outgoing;
        while (cur)
        {
            if (cur->stillgoing &&                  /* Not already done */
                    !cur->chan &&                   /* Isn't already going */
                    (!best || (cur->metric < bestmetric)))
            {   /* We haven't found one yet, or it's better */
                bestmetric = cur->metric;
                best = cur;
            }
            cur = cur->next;
        }
        if (best)
        {
            if (!qe->parent->strategy)
            {
                /* Ring everyone who shares this best metric (for ringall) */
                cur = outgoing;
                while (cur)
                {
                    if (cur->stillgoing && !cur->chan && (cur->metric <= bestmetric))
                    {
                        if (option_debug)
                            cw_log(CW_LOG_DEBUG, "(Parallel) Trying '%s' with metric %d\n", cur->interface, cur->metric);
                        ring_entry(qe, cur, busies);
                    }
                    cur = cur->next;
                }
            }
            else
            {
                /* Ring just the best channel */
                if (option_debug)
                    cw_log(CW_LOG_DEBUG, "Trying '%s' with metric %d\n", best->interface, best->metric);
                ring_entry(qe, best, busies);
            }
        }
    }
    while (best && !best->chan);
    if (!best)
    {
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "Nobody left to try ringing in queue\n");
        return 0;
    }
    return 1;
}

static int store_next(struct queue_ent *qe, struct outchan *outgoing)
{
    struct outchan *cur;
    struct outchan *best;
    int bestmetric=0;

    best = NULL;
    cur = outgoing;
    while (cur)
    {
        if (cur->stillgoing &&                  /* Not already done */
                !cur->chan &&                   /* Isn't already going */
                (!best || (cur->metric < bestmetric)))
        {   /* We haven't found one yet, or it's better */
            bestmetric = cur->metric;
            best = cur;
        }
        cur = cur->next;
    }
    if (best)
    {
        /* Ring just the best channel */
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "Next is '%s' with metric %d\n", best->interface, best->metric);
        qe->parent->rrpos = best->metric % 1000;
    }
    else
    {
        /* Just increment rrpos */
        if (qe->parent->wrapped)
        {
            /* No more channels, start over */
            qe->parent->rrpos = 0;
        }
        else
        {
            /* Prioritize next entry */
            qe->parent->rrpos++;
        }
    }
    qe->parent->wrapped = 0;
    return 0;
}

static int background_file(struct queue_ent *qe, struct cw_channel *chan, char *filename)
{
    int res;

    cw_stopstream(chan);
    res = cw_streamfile(chan, filename, chan->language);

    if (!res)
    {
        /* Wait for a keypress */
        res = cw_waitstream(chan, CW_DIGIT_ANY);
        if (res <= 0 || !valid_exit(qe, res))
            res = 0;

        /* Stop playback */
        cw_stopstream(chan);
    }
    else
    {
        res = 0;
    }

    /*if (res) {
       cw_log(CW_LOG_WARNING, "cw_streamfile failed on %s \n", chan->name);
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
    if ((now - qe->last_periodic_announce_time) < qe->parent->periodicannouncefrequency)
        return 0;

    /* Stop the music on hold so we can play our own file */
    cw_moh_stop(qe->chan);

    if (option_verbose > 2)
        cw_verbose(VERBOSE_PREFIX_3 "Playing periodic announcement\n");

    /* play the announcement */
    res = background_file(qe, qe->chan, qe->parent->sound_periodicannounce);

    /* Resume Music on Hold */
    cw_moh_start(qe->chan, qe->moh);

    /* update last_periodic_announce_time */
    qe->last_periodic_announce_time = now;

    return res;
}

static void record_abandoned(struct queue_ent *qe)
{
    cw_mutex_lock(&qe->parent->lock);
    qe->parent->callsabandoned++;
    cw_mutex_unlock(&qe->parent->lock);
}


#define CW_MAX_WATCHERS 256

#define BUILD_WATCHERS do { \
		o = outgoing; \
		found = -1; \
		pos = 1; \
		numlines = 0; \
		watchers[0] = in; \
		while (o) { \
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
	} while (0)

static struct outchan *wait_for_answer(struct queue_ent *qe, struct outchan *outgoing, int *to, char *digit, int prebusies, int caller_disconnect)
{
    char *queue = qe->parent->name;
    struct outchan *o;
    int found;
    int numlines;
    int status;
    int sentringing = 0;
    int numbusies = prebusies;
    int numnochan = 0;
    int stillgoing = 0;
    int orig = *to;
    struct cw_frame *f;
    struct outchan *peer = NULL;
    struct cw_channel *watchers[CW_MAX_WATCHERS];
    int pos;
    struct cw_channel *winner;
    struct cw_channel *in = qe->chan;

    while (*to && !peer)
    {
        BUILD_WATCHERS;
        if ((found < 0) && stillgoing && !qe->parent->strategy)
        {
            /* On "ringall" strategy we only move to the next penalty level
               when *all* ringing phones are done in the current penalty level */
            ring_one(qe, outgoing, &numbusies);
            BUILD_WATCHERS;
        }
        if (found < 0)
        {
            if (numlines == (numbusies + numnochan))
            {
                cw_log(CW_LOG_DEBUG, "Everyone is busy at this time\n");
                /* Make sure it doesn't loop too fast */
                usleep(1000000);
            }
            else
            {
                cw_log(CW_LOG_NOTICE, "No one is answering queue '%s' (%d/%d/%d)\n", queue, numlines, numbusies, numnochan);
            }
            *to = 0;
            return NULL;
        }
        winner = cw_waitfor_n(watchers, pos, to);
        o = outgoing;
        while (o)
        {
            if (o->stillgoing && (o->chan) &&  (o->chan->_state == CW_STATE_UP))
            {
                if (!peer)
                {
                    if (option_verbose > 2)
                        cw_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
                    peer = o;
                }
            }
            else if (o->chan && (o->chan == winner))
            {
                if (!cw_strlen_zero(o->chan->call_forward))
                {
                    char tmpchan[256]="";
                    char *stuff;
                    const char *tech;

                    cw_copy_string(tmpchan, o->chan->call_forward, sizeof(tmpchan));
                    if ((stuff = strchr(tmpchan, '/')))
                    {
                        *stuff = '\0';
                        stuff++;
                        tech = tmpchan;
                    }
                    else
                    {
                        snprintf(tmpchan, sizeof(tmpchan), "%s@%s", o->chan->call_forward, o->chan->context);
                        stuff = tmpchan;
                        tech = "Local";
                    }
                    /* Before processing channel, go ahead and check for forwarding */
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_3 "Now forwarding %s to '%s/%s' (thanks to %s)\n", in->name, tech, stuff, o->chan->name);
                    /* Setup parameters */
                    o->chan = cw_request(tech, in->nativeformats, stuff, &status);
                    if (status != o->oldstatus)
                        update_dial_status(qe->parent, o->member, status);
                    if (!o->chan)
                    {
                        cw_log(CW_LOG_NOTICE, "Unable to create local channel for call forward to '%s/%s'\n", tech, stuff);
                        o->stillgoing = 0;
                        numnochan++;
                    }
                    else
                    {
                        cw_var_copy(&o->chan->vars, &in->vars);
                        free(o->chan->cid.cid_num);
                        o->chan->cid.cid_num = NULL;
                        free(o->chan->cid.cid_name);
                        o->chan->cid.cid_name = NULL;

                        if (in->cid.cid_num)
                        {
                            o->chan->cid.cid_num = strdup(in->cid.cid_num);
                            if (!o->chan->cid.cid_num)
                                cw_log(CW_LOG_WARNING, "Out of memory\n");
                        }
                        if (in->cid.cid_name)
                        {
                            o->chan->cid.cid_name = strdup(in->cid.cid_name);
                            if (!o->chan->cid.cid_name)
                                cw_log(CW_LOG_WARNING, "Out of memory\n");
                        }
                        cw_copy_string(o->chan->accountcode, in->accountcode, sizeof(o->chan->accountcode));
                        o->chan->cdrflags = in->cdrflags;

                        if (in->cid.cid_ani)
                        {
                            free(o->chan->cid.cid_ani);
                            o->chan->cid.cid_ani = malloc(strlen(in->cid.cid_ani) + 1);
                            if (o->chan->cid.cid_ani)
                                strncpy(o->chan->cid.cid_ani, in->cid.cid_ani, strlen(in->cid.cid_ani) + 1);
                            else
                                cw_log(CW_LOG_WARNING, "Out of memory\n");
                        }
                        free(o->chan->cid.cid_rdnis);
                        if (!cw_strlen_zero(in->proc_exten))
                            o->chan->cid.cid_rdnis = strdup(in->proc_exten);
                        else
                            o->chan->cid.cid_rdnis = strdup(in->exten);
                        if (cw_call(o->chan, tmpchan))
                        {
                            cw_log(CW_LOG_NOTICE, "Failed to dial on local channel for call forward to '%s'\n", tmpchan);
                            o->stillgoing = 0;
                            cw_hangup(o->chan);
                            o->chan = NULL;
                            numnochan++;
                        }
                    }
                    /* Hangup the original channel now, in case we needed it */
                    cw_hangup(winner);
                    continue;
                }
                f = cw_read(winner);
                if (f)
                {
                    if (f->frametype == CW_FRAME_CONTROL)
                    {
                        switch(f->subclass)
                        {
                        case CW_CONTROL_ANSWER:
                            /* This is our guy if someone answered. */
                            if (!peer)
                            {
                                if (option_verbose > 2)
                                    cw_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
                                peer = o;
                            }
                            break;
                        case CW_CONTROL_BUSY:
                            if (option_verbose > 2)
                                cw_verbose( VERBOSE_PREFIX_3 "%s is busy\n", o->chan->name);
                            o->stillgoing = 0;
                            if (in->cdr)
                                cw_cdr_busy(in->cdr);
                            cw_hangup(o->chan);
                            o->chan = NULL;
                            if (qe->parent->strategy)
                            {
                                if (qe->parent->timeoutrestart)
                                    *to = orig;
                                ring_one(qe, outgoing, &numbusies);
                            }
                            numbusies++;
                            break;
                        case CW_CONTROL_CONGESTION:
                            if (option_verbose > 2)
                                cw_verbose( VERBOSE_PREFIX_3 "%s is circuit-busy\n", o->chan->name);
                            o->stillgoing = 0;
                            if (in->cdr)
                                cw_cdr_busy(in->cdr);
                            cw_hangup(o->chan);
                            o->chan = NULL;
                            if (qe->parent->strategy)
                            {
                                if (qe->parent->timeoutrestart)
                                    *to = orig;
                                ring_one(qe, outgoing, &numbusies);
                            }
                            numbusies++;
                            break;
                        case CW_CONTROL_RINGING:
                            if (option_verbose > 2)
                                cw_verbose( VERBOSE_PREFIX_3 "%s is ringing\n", o->chan->name);
                            if (!sentringing)
                            {
#if 0
                                cw_indicate(in, CW_CONTROL_RINGING);
#endif

                                sentringing++;
                            }
                            break;
                        case CW_CONTROL_OFFHOOK:
                            /* Ignore going off hook */
                            break;
                        default:
                            cw_log(CW_LOG_DEBUG, "Dunno what to do with control type %d\n", f->subclass);
                        }
                    }
                    cw_fr_free(f);
                }
                else
                {
                    o->stillgoing = 0;
                    cw_hangup(o->chan);
                    o->chan = NULL;
                    if (qe->parent->strategy)
                    {
                        if (qe->parent->timeoutrestart)
                            *to = orig;
                        ring_one(qe, outgoing, &numbusies);
                    }
                }
            }
            o = o->next;
        }
        if (winner == in)
        {
            f = cw_read(in);
#if 0

            if (f && (f->frametype != CW_FRAME_VOICE))
                printf("Frame type: %d, %d\n", f->frametype, f->subclass);
            else if (!f || (f->frametype != CW_FRAME_VOICE))
                printf("Hangup received on %s\n", in->name);
#endif

            if (!f || ((f->frametype == CW_FRAME_CONTROL) && (f->subclass == CW_CONTROL_HANGUP)))
            {
                /* Got hung up */
                *to=-1;
                if (f)
                    cw_fr_free(f);
                return NULL;
            }
            if ((f->frametype == CW_FRAME_DTMF) && caller_disconnect && (f->subclass == '*'))
            {
                if (option_verbose > 3)
                    cw_verbose(VERBOSE_PREFIX_3 "User hit %c to disconnect call.\n", f->subclass);
                *to=0;
                cw_fr_free(f);
                return NULL;
            }
            if ((f->frametype == CW_FRAME_DTMF) && (f->subclass != '*') && valid_exit(qe, f->subclass))
            {
                if (option_verbose > 3)
                    cw_verbose(VERBOSE_PREFIX_3 "User pressed digit: %c\n", f->subclass);
                *to=0;
                *digit=f->subclass;
                cw_fr_free(f);
                return NULL;
            }
            cw_fr_free(f);
        }
        if (!*to && (option_verbose > 2))
            cw_verbose( VERBOSE_PREFIX_3 "Nobody picked up in %d ms\n", orig);
    }

    return peer;

}

static int is_our_turn(struct queue_ent *qe)
{
    struct queue_ent *ch;
    int res;
    struct queue_ent *cur_qe;

    /* Check if we have some agent available */
    /*
       found=0;
       cw_mutex_lock(&qe->parent->lock);
       cur = qe->parent->members;
       while (cur)
       {
           if (cur->status == 1 && cur->paused ==0)
           {
               found=1;
               break;
           }
           cur = cur->next;
       }
       cw_mutex_unlock(&qe->parent->lock);
    
       if (found !=1)
       {
           if (option_debug)
               cw_log(CW_LOG_DEBUG, "Not any available agent .\n");
           return 0;
       }
    */

    /* Atomically read the parent head */
    cw_mutex_lock(&qe->parent->lock);
    ch = qe->parent->head;
    /* If we are now at the top of the head, break out */
    if (ch == qe)
    {
        qe->trying_agent = 1;
        res = 1;
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "It's Head turn (%s).\n", qe->chan->name);
    }
    else
    {
        /* Are we next? */
        cur_qe = qe->parent->head;
        while (cur_qe)
        {
            if (cur_qe->trying_agent != 1)
                break;
            cur_qe = cur_qe->next;
        }

        if (ch->trying_agent == 1 && cur_qe && qe == cur_qe)
        {
            qe->trying_agent = 1;
            if (option_debug)
                cw_log(CW_LOG_DEBUG, "It's our turn (%s).\n", qe->chan->name);
            res = 1;
        }
        else
        {
            qe->trying_agent = 0;
            if (option_debug)
                cw_log(CW_LOG_DEBUG, "It's not our turn (%s).\n", qe->chan->name);
            res = 0;
        }
    }

    cw_mutex_unlock(&qe->parent->lock);
    return res;
}

static int wait_our_turn(struct queue_ent *qe, int ringing, enum queue_result *reason)
{
    int res = 0;

    /* This is the holding pen for callers 2 through maxlen */
    for (;;)
    {
        enum queue_member_status status;

        if (is_our_turn(qe))
            break;

        /* If we have timed out, break out */
        if (qe->expire && (time(NULL) > qe->expire))
        {
            *reason = QUEUE_TIMEOUT;
            cw_queue_log(qe->parent->name, qe->chan->uniqueid,"NONE", "EXITWITHTIMEOUT", "%d", qe->pos);
            break;
        }

        status = get_member_status(qe->parent);

        /* leave the queue if no agents, if enabled */
        if (qe->parent->leavewhenempty && (status == QUEUE_NO_MEMBERS))
        {
            *reason = QUEUE_LEAVEEMPTY;
            cw_queue_log(qe->parent->name, qe->chan->uniqueid, "NONE", "EXITWITHKEY", "empty|%d", qe->pos);
            leave_queue(qe);
            break;
        }

        /* leave the queue if no reachable agents, if enabled */
        if ((qe->parent->leavewhenempty == QUEUE_EMPTY_STRICT) && (status == QUEUE_NO_REACHABLE_MEMBERS))
        {
            *reason = QUEUE_LEAVEUNAVAIL;
            cw_queue_log(qe->parent->name, qe->chan->uniqueid, "NONE", "EXITWITHKEY", "empty|%d", qe->pos);
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
        if (!res)
            res = cw_waitfordigit(qe->chan, RECHECK * 1000);
        if (res)
            break;
    }
    return res;
}

static int update_queue(struct cw_call_queue *q, struct member *member)
{
    struct member *cur;

    /* Since a reload could have taken place, we have to traverse the list to
       be sure it's still valid */
    cw_mutex_lock(&q->lock);
    cur = q->members;
    while (cur)
    {
        if (member == cur)
        {
            time(&cur->lastcall);
            cur->calls++;
            break;
        }
        cur = cur->next;
    }
    q->callscompleted++;
    cw_mutex_unlock(&q->lock);
    return 0;
}

static int calc_metric(struct cw_call_queue *q, struct member *mem, int pos, struct queue_ent *qe, struct outchan *tmp)
{
    CW_UNUSED(qe);

    switch (q->strategy)
    {
    case QUEUE_STRATEGY_RINGALL:
        /* Everyone equal, except for penalty */
        tmp->metric = mem->penalty*1000000;
        break;
    case QUEUE_STRATEGY_ROUNDROBIN:
        if (!pos)
        {
            if (!q->wrapped)
            {
                /* No more channels, start over */
                q->rrpos = 0;
            }
            else
            {
                /* Prioritize next entry */
                q->rrpos++;
            }
            q->wrapped = 0;
        }
        /* Fall through */
    case QUEUE_STRATEGY_RRMEMORY:
        if (pos < q->rrpos)
        {
            tmp->metric = 1000 + pos;
        }
        else
        {
            if (pos > q->rrpos)
            {
                /* Indicate there is another priority */
                q->wrapped = 1;
            }
            tmp->metric = pos;
        }
        tmp->metric += mem->penalty * 1000000;
        break;
    case QUEUE_STRATEGY_RANDOM:
        tmp->metric = cw_random() % 1000;
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
        cw_log(CW_LOG_WARNING, "Can't calculate metric for unknown strategy %d\n", (int)q->strategy);
        break;
    }
    return 0;
}

static int try_calling(struct queue_ent *qe, const char *options, char *announceoverride, const char *url, int *go_on)
{
    struct member *cur;
    struct outchan *outgoing=NULL, *tmp = NULL;
    int to;
    char restofit[CW_MAX_EXTENSION];
    char oldexten[CW_MAX_EXTENSION]="";
    char oldcontext[CW_MAX_CONTEXT]="";
    char queuename[256]="";
    char *newnum;
    struct cw_channel *peer;
    struct cw_channel *which;
    struct outchan *lpeer;
    struct member *member;
    int res = 0;
    int bridge = 0;
    int numbusies = 0;
    int x = 0;
    char *announce = NULL;
    char digit = 0;
    time_t callstart;
    time_t now = time(NULL);
    struct cw_bridge_config bridge_config;
    char nondataquality = 1;

    memset(&bridge_config, 0, sizeof(bridge_config));
    time(&now);

    for (  ; options  &&  *options;  options++)
    {
        switch (*options)
        {
        case 't':
            cw_set_flag(&(bridge_config.features_callee), CW_FEATURE_REDIRECT);
            break;
        case 'T':
            cw_set_flag(&(bridge_config.features_caller), CW_FEATURE_REDIRECT);
            break;
        case 'w':
            cw_set_flag(&(bridge_config.features_callee), CW_FEATURE_AUTOMON);
            break;
        case 'W':
            cw_set_flag(&(bridge_config.features_caller), CW_FEATURE_AUTOMON);
            break;
        case 'd':
            nondataquality = 0;
            break;
        case 'h':
            cw_set_flag(&(bridge_config.features_callee), CW_FEATURE_DISCONNECT);
            break;
        case 'H':
            cw_set_flag(&(bridge_config.features_caller), CW_FEATURE_DISCONNECT);
            break;
        case 'n':
            if ((now - qe->start >= qe->parent->timeout))
                *go_on = 1;
            break;
        }
    }

    /* Hold the lock while we setup the outgoing calls */
    if (use_weight)
        cw_mutex_lock(&qlock);
    cw_mutex_lock(&qe->parent->lock);
    if (option_debug)
    {
        cw_log(CW_LOG_DEBUG, "%s is trying to call a queue member.\n",
                 qe->chan->name);
    }
    cw_copy_string(queuename, qe->parent->name, sizeof(queuename));
    cur = qe->parent->members;
    if (!cw_strlen_zero(qe->announce))
        announce = qe->announce;
    if (!cw_strlen_zero(announceoverride))
        announce = announceoverride;

    while (cur)
    {
        tmp = malloc(sizeof(*tmp));
        if (!tmp)
        {
            cw_mutex_unlock(&qe->parent->lock);
            if (use_weight)
                cw_mutex_unlock(&qlock);
            cw_log(CW_LOG_WARNING, "Out of memory\n");
            goto out;
        }
        memset(tmp, 0, sizeof(*tmp));
        tmp->stillgoing = -1;
        if (option_debug)
        {
            if (url)
                cw_log(CW_LOG_DEBUG, "Queue with URL=%s_\n", url);
            else
                cw_log(CW_LOG_DEBUG, "Simple queue (no URL)\n");
        }

        tmp->member = cur;      /* Never directly dereference!  Could change on reload */
        tmp->oldstatus = cur->status;
        tmp->lastcall = cur->lastcall;
        cw_copy_string(tmp->interface, cur->interface, sizeof(tmp->interface));
        /* If we're dialing by extension, look at the extension to know what to dial */
        if ((newnum = strstr(tmp->interface, "/BYEXTENSION")))
        {
            newnum++;
            strncpy(restofit, newnum + strlen("BYEXTENSION"), sizeof(restofit) - 1);
            snprintf(newnum, sizeof(tmp->interface) - (newnum - tmp->interface), "%s%s", qe->chan->exten, restofit);
            if (option_debug)
                cw_log(CW_LOG_DEBUG, "Dialing by extension %s\n", tmp->interface);
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
        if (outgoing->chan && (outgoing->chan->_state == CW_STATE_UP))
            break;

        cur = cur->next;
    }
    if (qe->parent->timeout)
        to = qe->parent->timeout * 1000;
    else
        to = -1;
    ring_one(qe, outgoing, &numbusies);
    cw_mutex_unlock(&qe->parent->lock);
    if (use_weight)
        cw_mutex_unlock(&qlock);
    lpeer = wait_for_answer(qe, outgoing, &to, &digit, numbusies, cw_test_flag(&(bridge_config.features_caller), CW_FEATURE_DISCONNECT));
    cw_mutex_lock(&qe->parent->lock);
    if (qe->parent->strategy == QUEUE_STRATEGY_RRMEMORY)
        store_next(qe, outgoing);
    cw_mutex_unlock(&qe->parent->lock);
    if (lpeer)
        peer = lpeer->chan;
    else
        peer = NULL;
    if (!peer)
    {
        if (to)
        {
            /* Musta gotten hung up */
            record_abandoned(qe);
            res = -1;
        }
        else
        {
            res = digit;
        }
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "%s: Nobody answered.\n", qe->chan->name);
        goto out;
    }
    if (peer)
    {
        /* Ah ha!  Someone answered within the desired timeframe.  Of course after this
           we will always return with -1 so that it is hung up properly after the 
           conversation.  */
        qe->handled++;
        if (!strcmp(qe->chan->type, "DAHDI"))
            cw_channel_setoption(qe->chan, CW_OPTION_TONE_VERIFY, &nondataquality, sizeof(nondataquality));
        if (!strcmp(peer->type, "DAHDI"))
            cw_channel_setoption(peer, CW_OPTION_TONE_VERIFY, &nondataquality, sizeof(nondataquality));
        /* Update parameters for the queue */
        recalc_holdtime(qe);
        member = lpeer->member;
        hangupcalls(outgoing, peer);
        outgoing = NULL;
        if (announce || qe->parent->reportholdtime || qe->parent->memberdelay)
        {
            int res2;

            res2 = cw_autoservice_start(qe->chan);
            if (!res2)
            {
                if (qe->parent->memberdelay)
                {
                    cw_log(CW_LOG_NOTICE, "Delaying member connect for %d seconds\n", qe->parent->memberdelay);
                    res2 |= cw_safe_sleep(peer, qe->parent->memberdelay * 1000);
                }
                if (!res2 && announce)
                {
                    if (play_file(peer, announce))
                        cw_log(CW_LOG_WARNING, "Announcement file '%s' is unavailable, continuing anyway...\n", announce);
                }
                if (!res2 && qe->parent->reportholdtime)
                {
                    if (!play_file(peer, qe->parent->sound_reporthold))
                    {
                        int holdtime;

                        time(&now);
                        holdtime = abs((now - qe->start)/60);
                        if (holdtime < 2)
                        {
                            play_file(peer, qe->parent->sound_lessthan);
                            cw_say_number(peer, 2, CW_DIGIT_ANY, peer->language, NULL);
                        }
                        else
                        {
                            cw_say_number(peer, holdtime, CW_DIGIT_ANY, peer->language, NULL);
                        }
                        play_file(peer, qe->parent->sound_minutes);
                    }
                }
            }
            res2 |= cw_autoservice_stop(qe->chan);
            if (peer->_softhangup)
            {
                /* Agent must have hung up */
                cw_log(CW_LOG_WARNING, "Agent on %s hungup on the customer.  They're going to be pissed.\n", peer->name);
                cw_queue_log(queuename, qe->chan->uniqueid, peer->name, "AGENTDUMP", "%s", "");
                record_abandoned(qe);
                if (qe->parent->eventwhencalled)
                {
                    cw_manager_event(EVENT_FLAG_AGENT, "AgentDump",
                        4,
                        cw_msg_tuple("Queue",    "%s", queuename),
                        cw_msg_tuple("Uniqueid", "%s", qe->chan->uniqueid),
                        cw_msg_tuple("Channel",  "%s", peer->name),
                        cw_msg_tuple("Member",   "%s", member->interface)
                    );
                }
                cw_hangup(peer);
                goto out;
            }
            else if (res2)
            {
                /* Caller must have hung up just before being connected*/
                cw_log(CW_LOG_NOTICE, "Caller was about to talk to agent on %s but the caller hungup.\n", peer->name);
                cw_queue_log(queuename, qe->chan->uniqueid, peer->name, "ABANDON", "%d|%d|%ld", qe->pos, qe->opos, (long)time(NULL) - qe->start);
                record_abandoned(qe);
                cw_hangup(peer);
                return -1;
            }
        }
        /* Stop music on hold */
        cw_moh_stop(qe->chan);
        /* If appropriate, log that we have a destination channel */
        if (qe->chan->cdr)
            cw_cdr_setdestchan(qe->chan->cdr, peer->name);
        /* Make sure channels are compatible */
        res = cw_channel_make_compatible(qe->chan, peer);
        if (res < 0)
        {
            cw_queue_log(queuename, qe->chan->uniqueid, peer->name, "SYSCOMPAT", "%s", "");
            cw_log(CW_LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", qe->chan->name, peer->name);
            record_abandoned(qe);
            cw_hangup(peer);
            return -1;
        }
        /* Begin Monitoring */
        if (qe->parent->monfmt && *qe->parent->monfmt)
        {
            struct cw_var_t *var;
            if ((var = pbx_builtin_getvar_helper(qe->chan, CW_KEYWORD_MONITOR_EXEC, "MONITOR_EXEC")))
            {
                which = qe->chan;
                cw_object_put(var);
            }
            if ((var = pbx_builtin_getvar_helper(qe->chan, CW_KEYWORD_MONITOR_EXEC_ARGS, "MONITOR_EXEC_ARGS")))
            {
                which = qe->chan;
                cw_object_put(var);
            }
            else
                which = peer;

	    if ((var = pbx_builtin_getvar_helper(qe->chan, CW_KEYWORD_MONITOR_FILENAME, "MONITOR_FILENAME")))
            {
                cw_monitor_start(which, qe->parent->monfmt, var->value, 1 );
                cw_object_put(var);
            }
            else if (qe->chan->cdr)
                cw_monitor_start(which, qe->parent->monfmt, qe->chan->cdr->uniqueid, 1 );
            else
            {
                /* Last ditch effort -- no CDR, make up something */
                char tmpid[256];
                snprintf(tmpid, sizeof(tmpid), "chan-%ld", cw_random());
                cw_monitor_start(which, qe->parent->monfmt, tmpid, 1 );
            }
            if (qe->parent->monjoin)
                cw_monitor_setjoinfiles(which, 1);
        }
        /* Drop out of the queue at this point, to prepare for next caller */
        leave_queue(qe);
        if (!cw_strlen_zero(url) && cw_channel_supports_html(peer))
        {
            if (option_debug)
                cw_log(CW_LOG_DEBUG, "app_queue: sendurl=%s.\n", url);
            cw_channel_sendurl(peer, url);
        }
        cw_queue_log(queuename, qe->chan->uniqueid, peer->name, "CONNECT", "%ld", (long)time(NULL) - qe->start);
        if (qe->parent->eventwhencalled)
            cw_manager_event(EVENT_FLAG_AGENT, "AgentConnect",
                5,
                cw_msg_tuple("Queue",    "%s", queuename),
                cw_msg_tuple("Uniqueid", "%s", qe->chan->uniqueid),
                cw_msg_tuple("Channel",  "%s", peer->name),
                cw_msg_tuple("Member",   "%s", member->interface),
                cw_msg_tuple("Holdtime", "%ld", (long)time(NULL) - qe->start)
            );
        cw_copy_string(oldcontext, qe->chan->context, sizeof(oldcontext));
        cw_copy_string(oldexten, qe->chan->exten, sizeof(oldexten));
        time(&callstart);

        bridge = cw_bridge_call(qe->chan,peer, &bridge_config);

        if (strcasecmp(oldcontext, qe->chan->context) || strcasecmp(oldexten, qe->chan->exten))
        {
            cw_queue_log(queuename, qe->chan->uniqueid, peer->name, "TRANSFER", "%s|%s", qe->chan->exten, qe->chan->context);
        }
        else if (qe->chan->_softhangup)
        {
            cw_queue_log(queuename, qe->chan->uniqueid, peer->name, "COMPLETECALLER", "%ld|%ld",
                           (long)(callstart - qe->start), (long)(time(NULL) - callstart));
            if (qe->parent->eventwhencalled)
            {
                cw_manager_event(EVENT_FLAG_AGENT, "AgentComplete",
                    7,
                    cw_msg_tuple("Queue",    "%s",  queuename),
                    cw_msg_tuple("Uniqueid", "%s",  qe->chan->uniqueid),
                    cw_msg_tuple("Channel",  "%s",  peer->name),
                    cw_msg_tuple("Member",   "%s",  member->interface),
                    cw_msg_tuple("HoldTime", "%ld", (long)(callstart - qe->start)),
                    cw_msg_tuple("TalkTime", "%ld", (long)(time(NULL) - callstart)),
                    cw_msg_tuple("Reason",   "%s",  "caller")
                );
            }
        }
        else
        {
            cw_queue_log(queuename, qe->chan->uniqueid, peer->name, "COMPLETEAGENT", "%ld|%ld", (long)(callstart - qe->start), (long)(time(NULL) - callstart));
            if (qe->parent->eventwhencalled)
            {
                cw_manager_event(EVENT_FLAG_AGENT, "AgentComplete",
                    6,
                    cw_msg_tuple("Queue",    "%s",  queuename),
                    cw_msg_tuple("Uniqueid", "%s",  qe->chan->uniqueid),
                    cw_msg_tuple("Channel",  "%s",  peer->name),
                    cw_msg_tuple("HoldTime", "%ld", (long)(callstart - qe->start)),
                    cw_msg_tuple("TalkTime", "%ld", (long)(time(NULL) - callstart)),
                    cw_msg_tuple("Reason",   "%s",  "agent")
                );
            }
        }

        if (bridge != CW_PBX_NO_HANGUP_PEER)
            cw_hangup(peer);
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

    return cw_waitfordigit(qe->chan, retrywait);
}

static struct member * interface_exists(struct cw_call_queue *q, char *interface)
{
    struct member *mem;

    if (q)
    {
        for (mem = q->members;  mem;  mem = mem->next)
        {
            if (!strcasecmp(interface, mem->interface))
                return mem;
        }
    }
    return NULL;
}


/* Dump all members in a specific queue to the databse
 *
 * <pm_family>/<queuename> = <interface>;<penalty>;<paused>[, ...]
 *
 */
static void dump_queue_members(struct cw_call_queue *pm_queue)
{
    struct member *cur_member;
    char value[PM_MAX_LEN];
    int value_len = 0;
    int res;

    memset(value, 0, sizeof(value));

    if (!pm_queue)
        return;

    for (cur_member = pm_queue->members; cur_member; cur_member = cur_member->next)
    {
        if (!cur_member->dynamic)
            continue;

        res = snprintf(value + value_len, sizeof(value) - value_len, "%s;%d;%d%s",
                       cur_member->interface, cur_member->penalty, cur_member->paused,
                       cur_member->next ? "," : "");
        if (res != strlen(value + value_len))
        {
            cw_log(CW_LOG_WARNING, "Could not create persistent member string, out of space\n");
            break;
        }
        value_len += res;
    }

    if (value_len && !cur_member)
    {
        if (cw_db_put(pm_family, pm_queue->name, value))
            cw_log(CW_LOG_WARNING, "failed to create persistent dynamic entry!\n");
    }
    else
    {
        /* Delete the entry if the queue is empty or there is an error */
        cw_db_del(pm_family, pm_queue->name);
    }
}

static int remove_from_queue(char *queuename, char *interface, time_t *added)
{
    struct cw_call_queue *q;
    struct member *last_member, *look;
    int res = RES_NOSUCHQUEUE;

    cw_mutex_lock(&qlock);
    for (q = queues;  q;  q = q->next)
    {
        cw_mutex_lock(&q->lock);
        if (!strcmp(q->name, queuename))
        {
            if ((last_member = interface_exists(q, interface)))
            {
                if ((look = q->members) == last_member)
                {
                    q->members = last_member->next;
                }
                else
                {
                    while (look != NULL)
                    {
                        if (look->next == last_member)
                        {
                            look->next = last_member->next;
                            break;
                        }
                        look = look->next;
                    }
                }
                cw_manager_event(EVENT_FLAG_AGENT, "QueueMemberRemoved",
                    2,
                    cw_msg_tuple("Queue",    "%s", q->name),
                    cw_msg_tuple("Location", "%s", last_member->interface)
                );
                if (added != NULL)
                    *added = last_member->added;
                free(last_member);

                if (queue_persistent_members)
                    dump_queue_members(q);

                res = RES_OKAY;
            }
            else
            {
                res = RES_EXISTS;
            }
            cw_mutex_unlock(&q->lock);
            break;
        }
        cw_mutex_unlock(&q->lock);
    }
    cw_mutex_unlock(&qlock);
    return res;
}

static int update_queue_member(char *queuename, char *interface, int penalty, int paused, int dump)
{
	struct cw_call_queue *q;
	struct member *last_member;
	int res = RES_NOSUCHQUEUE;

	cw_mutex_lock(&qlock);
	for (q = queues ; q ; q = q->next) {
		cw_mutex_lock(&q->lock);
		if (!strcmp(q->name, queuename)) {
			if ((last_member = interface_exists(q, interface)) != NULL) {
				last_member->penalty = penalty;
				last_member->paused = paused;
				cw_manager_event(EVENT_FLAG_AGENT, "QueueMemberUpdated",
					8,
					cw_msg_tuple("Queue",      "%s",  q->name),
					cw_msg_tuple("Location",   "%s",  last_member->interface),
					cw_msg_tuple("Membership", "%s",  (last_member->dynamic ? "dynamic" : "static")),
					cw_msg_tuple("Penalty",    "%d",  last_member->penalty),
					cw_msg_tuple("CallsTaken", "%d",  last_member->calls),
					cw_msg_tuple("LastCall",   "%ld", last_member->lastcall),
					cw_msg_tuple("Status",     "%d",  last_member->status),
					cw_msg_tuple("Paused",     "%d",  last_member->paused)
				);

				if (dump)
					dump_queue_members(q);

				res = RES_OKAY;
			} else {
				res = RES_EXISTS;
			}
			cw_mutex_unlock(&q->lock);
			break;
		}
		cw_mutex_unlock(&q->lock);
	}
	cw_mutex_unlock(&qlock);
	return res;
}

static int add_to_queue(char *queuename, char *interface, int penalty, int paused, int dump)
{
    struct cw_call_queue *q;
    struct member *new_member;
    int res = RES_NOSUCHQUEUE;

    cw_mutex_lock(&qlock);
    for (q = queues;  q;  q = q->next)
    {
        cw_mutex_lock(&q->lock);
        if (!strcmp(q->name, queuename))
        {
            if (interface_exists(q, interface) == NULL)
            {
                new_member = create_queue_member(interface, penalty, paused);

                if (new_member != NULL)
                {
                    new_member->dynamic = 1;
                    new_member->next = q->members;
                    q->members = new_member;
                    cw_manager_event(EVENT_FLAG_AGENT, "QueueMemberAdded",
                        8,
                        cw_msg_tuple("Queue",      "%s",  q->name),
                        cw_msg_tuple("Location",   "%s", new_member->interface),
                        cw_msg_tuple("Membership", "%s", (new_member->dynamic ? "dynamic" : "static")),
                        cw_msg_tuple("Penalty",    "%d", new_member->penalty),
                        cw_msg_tuple("CallsTaken", "%d", new_member->calls),
                        cw_msg_tuple("LastCall",   "%ld", new_member->lastcall),
                        cw_msg_tuple("Status",     "%d", new_member->status),
                        cw_msg_tuple("Paused",     "%d", new_member->paused)
                    );

                    if (dump)
                        dump_queue_members(q);

                    res = RES_OKAY;
                }
                else
                {
                    res = RES_OUTOFMEMORY;
                }
            }
            else
            {
                res = RES_EXISTS;
            }
            cw_mutex_unlock(&q->lock);
            break;
        }
        cw_mutex_unlock(&q->lock);
    }
    cw_mutex_unlock(&qlock);
    return res;
}

static int set_member_paused(char *queuename, char *interface, int paused)
{
    int found = 0;
    struct cw_call_queue *q;
    struct member *mem;

    /* Special event for when all queues are paused - individual events still generated */

    if (cw_strlen_zero(queuename))
        cw_queue_log("NONE", "NONE", interface, (paused ? "PAUSEALL" : "UNPAUSEALL"), "%s", "");

    cw_mutex_lock(&qlock);
    for (q = queues ; q ; q = q->next)
    {
        cw_mutex_lock(&q->lock);
        if (cw_strlen_zero(queuename)  ||  !strcasecmp(q->name, queuename))
        {
            if ((mem = interface_exists(q, interface)))
            {
                found++;
                if (mem->paused == paused)
                    cw_log(CW_LOG_DEBUG, "%spausing already-%spaused queue member %s:%s\n", (paused ? "" : "un"), (paused ? "" : "un"), q->name, interface);
                mem->paused = paused;

                if (queue_persistent_members)
                    dump_queue_members(q);

                cw_queue_log(q->name, "NONE", interface, (paused ? "PAUSE" : "UNPAUSE"), "%s", "");

                cw_manager_event(EVENT_FLAG_AGENT, "QueueMemberPaused",
                    3,
                    cw_msg_tuple("Queue",    "%s", q->name),
                    cw_msg_tuple("Location", "%s", mem->interface),
                    cw_msg_tuple("Paused",   "%d", paused)
                );
            }
        }
        cw_mutex_unlock(&q->lock);
    }
    cw_mutex_unlock(&qlock);

    if (found)
        return RESULT_SUCCESS;
    else
        return RESULT_FAILURE;
}

/* Reload dynamic queue members persisted into the cwdb */
static void reload_queue_members(void)
{
    struct cw_dynstr queue_data = CW_DYNSTR_INIT;
    char *cur_ptr;  
    char *queue_name;
    char *member;
    char *interface;
    char *penalty_tok;
    int penalty = 0;
    char *paused_tok;
    int paused = 0;
    struct cw_db_entry *db_tree;
    struct cw_db_entry *entry;
    struct cw_call_queue *cur_queue;

    cw_mutex_lock(&qlock);

    /* Each key in 'pm_family' is the name of a queue */
    db_tree = cw_db_gettree(pm_family, NULL);
    for (entry = db_tree; entry; entry = entry->next)
    {
        if (!strncmp(entry->key, pm_family, strlen(pm_family)))
            queue_name = entry->key + strlen(pm_family) + 2;
        else
            queue_name = entry->key;

        cur_queue = queues;
        while (cur_queue)
        {
            cw_mutex_lock(&cur_queue->lock);
            if (!strcmp(queue_name, cur_queue->name))
                break;
            cw_mutex_unlock(&cur_queue->lock);
            cur_queue = cur_queue->next;
        }

        if (!cur_queue)
        {
            /* If the queue no longer exists, remove it from the database */
            cw_db_del(pm_family, queue_name);
            continue;
        }
        cw_mutex_unlock(&cur_queue->lock);

        cw_dynstr_reset(&queue_data);

        if (cw_db_get(pm_family, queue_name, &queue_data) || queue_data.error)
            continue;

        cur_ptr = queue_data.data;
        while ((member = strsep(&cur_ptr, "|,")))
        {
            if (cw_strlen_zero(member))
                continue;

            interface = strsep(&member, ";");
            penalty_tok = strsep(&member, ";");
            paused_tok = strsep(&member, ";");

            if (!penalty_tok)
            {
                cw_log(CW_LOG_WARNING, "Error parsing persisent member string for '%s' (penalty)\n", queue_name);
                break;
            }
            penalty = strtol(penalty_tok, NULL, 10);
            if (errno == ERANGE)
            {
                cw_log(CW_LOG_WARNING, "Error converting penalty: %s: Out of range.\n", penalty_tok);
                break;
            }

            if (!paused_tok)
            {
                cw_log(CW_LOG_WARNING, "Error parsing persistent member string for '%s' (paused)\n", queue_name);
                break;
            }
            paused = strtol(paused_tok, NULL, 10);
            if ((errno == ERANGE) || paused < 0 || paused > 1)
            {
                cw_log(CW_LOG_WARNING, "Error converting paused: %s: Expected 0 or 1.\n", paused_tok);
                break;
            }

            if (option_debug)
                cw_log(CW_LOG_DEBUG, "Reload Members: Queue: %s  Member: %s  Penalty: %d  Paused: %d\n", queue_name, interface, penalty, paused);

            if (add_to_queue(queue_name, interface, penalty, paused, 0) == RES_OUTOFMEMORY)
            {
                cw_log(CW_LOG_ERROR, "Out of memory when reloading persistent queue member\n");
                break;
            }
        }
    }

    cw_dynstr_free(&queue_data);

    cw_mutex_unlock(&qlock);
    if (db_tree)
    {
        cw_log(CW_LOG_NOTICE, "Queue members sucessfully reloaded from database.\n");
        cw_db_freetree(db_tree);
    }
}

static int pqm_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    struct localuser *u;

    CW_UNUSED(result);

    if (argc != 2 || !argv[1][0])
        return cw_function_syntax(app_pqm_syntax);

    LOCAL_USER_ADD(u);

    if (set_member_paused(argv[0], argv[1], 1))
    {
        cw_log(CW_LOG_WARNING, "Attempt to pause interface %s, not found\n", argv[1]);
        LOCAL_USER_REMOVE(u);
        pbx_builtin_setvar_helper(chan, "PQMSTATUS", "NOTFOUND");
        return -1;
    }

    LOCAL_USER_REMOVE(u);
    pbx_builtin_setvar_helper(chan, "PQMSTATUS", "PAUSED");
    return 0;
}

static int upqm_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    struct localuser *u;

    CW_UNUSED(result);

    if (argc != 2 || !argv[1][0])
        return cw_function_syntax(app_upqm_syntax);

    LOCAL_USER_ADD(u);

    if (set_member_paused(argv[0], argv[1], 0))
    {
        cw_log(CW_LOG_WARNING, "Attempt to unpause interface %s, not found\n", argv[1]);
        LOCAL_USER_REMOVE(u);
        pbx_builtin_setvar_helper(chan, "UPQMSTATUS", "NOTFOUND");
        return -1;
    }

    LOCAL_USER_REMOVE(u);
    pbx_builtin_setvar_helper(chan, "UPQMSTATUS", "UNPAUSED");
    return 0;
}

static int rqm_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    struct localuser *u;
    time_t added = 0;
    int res = -1;

    CW_UNUSED(result);

    if (argc < 1 || argc > 3)
        return cw_function_syntax(app_rqm_syntax);

    LOCAL_USER_ADD(u);

    if (argc != 2 || !argv[1][0])
    {
        char *p;
        argv[1] = cw_strdupa(chan->name);
        if ((p = strrchr(argv[1], '-')))
            *p = '\0';
    }

    switch (remove_from_queue(argv[0], argv[1], &added))
    {
    case RES_OKAY:
        cw_log(CW_LOG_NOTICE, "Removed interface '%s' from queue '%s'\n", argv[1], argv[0]);
        cw_queue_log("NONE", chan->uniqueid, argv[1], "AGENTCALLBACKLOGOFF", "%s|%ld", argv[1], time(NULL) - added);
        pbx_builtin_setvar_helper(chan, "RQMSTATUS", "REMOVED");
        res = 0;
        break;
    case RES_EXISTS:
        cw_log(CW_LOG_WARNING, "Unable to remove interface '%s' from queue '%s': Not there\n", argv[1], argv[0]);
        pbx_builtin_setvar_helper(chan, "RQMSTATUS", "NOTINQUEUE");
        res = 0;
        break;
    case RES_NOSUCHQUEUE:
        cw_log(CW_LOG_WARNING, "Unable to remove interface from queue '%s': No such queue\n", argv[0]);
        pbx_builtin_setvar_helper(chan, "RQMSTATUS", "NOSUCHQUEUE");
        res = 0;
        break;
    case RES_OUTOFMEMORY:
        cw_log(CW_LOG_ERROR, "Out of memory\n");
        break;
    }

    LOCAL_USER_REMOVE(u);
    return res;
}

static int aqm_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    struct localuser *u;
    int penalty = 0;
    int res = -1;

    CW_UNUSED(result);

    if (argc < 1 || argc > 3)
        return cw_function_syntax(app_aqm_syntax);

    LOCAL_USER_ADD(u);

    if (argc < 2 || !argv[1][0])
    {
        char *p;
        argv[1] = cw_strdupa(chan->name);
        if ((p = strrchr(argv[1], '-')))
            *p = '\0';
    }

    penalty = (argc > 2 ? atoi(argv[2]) : 0);
    if (penalty < 0) penalty = 0;

    switch (add_to_queue(argv[0], argv[1], penalty, 0, queue_persistent_members))
    {
    case RES_OKAY:
        cw_log(CW_LOG_NOTICE, "Added interface '%s' to queue '%s'\n", argv[1], argv[0]);
        cw_queue_log("NONE", chan->uniqueid, argv[1], "AGENTCALLBACKLOGIN", "%s", argv[1]);
        pbx_builtin_setvar_helper(chan, "AQMSTATUS", "ADDED");
        res = 0;
        break;
    case RES_EXISTS:
        cw_log(CW_LOG_WARNING, "Unable to add interface '%s' to queue '%s': Already there\n", argv[1], argv[0]);
        pbx_builtin_setvar_helper(chan, "AQMSTATUS", "MEMBERALREADY");
        res = 0;
        break;
    case RES_NOSUCHQUEUE:
        cw_log(CW_LOG_WARNING, "Unable to add interface to queue '%s': No such queue\n", argv[0]);
        pbx_builtin_setvar_helper(chan, "AQMSTATUS", "NOSUCHQUEUE");
        res = 0;
        break;
    case RES_OUTOFMEMORY:
        cw_log(CW_LOG_ERROR, "Out of memory adding member %s to queue %s\n", argv[1], argv[0]);
        break;
    }

    LOCAL_USER_REMOVE(u);
    return res;
}

static int queue_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    struct localuser *u;
    struct cw_var_t *var;
    int prio;
    int ringing = 0;
    int res = -1;
    enum queue_result reason = QUEUE_UNKNOWN;

    /* whether to exit Queue application after the timeout hits */
    int go_on = 0;

    /* Our queue entry */
    struct queue_ent qe;
    
    CW_UNUSED(result);

    if (argc < 1 || argc > 5 || !argv[0][0])
        return cw_function_syntax(syntax);

    LOCAL_USER_ADD(u);

    /* Setup our queue entry */
    memset(&qe, 0, sizeof(qe));
    qe.start = time(NULL);
    
    /* set the expire time based on the supplied timeout; */
    qe.expire = (argc > 4 ? qe.start + atoi(argv[4]) : 0);

    /* Get the priority from the variable ${QUEUE_PRIO} */
    if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_QUEUE_PRIO, "QUEUE_PRIO")))
    {
        if (sscanf(var->value, "%d", &prio) == 1)
        {
            if (option_debug)
                cw_log(CW_LOG_DEBUG, "%s: Got priority %d from ${QUEUE_PRIO}.\n",
                    chan->name, prio);
        }
        else
        {
            cw_log(CW_LOG_WARNING, "${QUEUE_PRIO}: Invalid value (%s), channel %s.\n",
                var->value, chan->name);
            prio = 0;
        }
        cw_object_put(var);
    }
    else
    {
        if (option_debug > 2)
            cw_log(CW_LOG_DEBUG, "NO QUEUE_PRIO variable found. Using default.\n");
        prio = 0;
    }

    if (argc > 1 && (strchr(argv[1], 'r')))
        ringing = 1;

    if (option_debug)  
        cw_log(CW_LOG_DEBUG, "queue: %s, options: %s, url: %s, announce: %s, expires: %ld, priority: %d\n",
            argv[0], argv[1], argv[2], argv[3], (long)qe.expire, (int)prio);

    qe.chan = chan;
    qe.prio = (int)prio;
    qe.last_pos_said = 0;
    qe.last_pos = 0;
    qe.trying_agent=0;
    qe.last_periodic_announce_time = time(NULL);
    if (!join_queue(argv[0], &qe, &reason))
    {
        cw_queue_log(argv[0], chan->uniqueid, "NONE", "ENTERQUEUE", "%s|%s", (argc > 2 ? argv[2] : ""),
                  chan->cid.cid_num ? chan->cid.cid_num : "");
check_turns:
        if (ringing)
        {
            cw_indicate(chan, CW_CONTROL_RINGING);
        }
        else
        {              
            cw_moh_start(chan, qe.moh);
        }
        for (;;)
        {
            /* This is the wait loop for callers 2 through maxlen */

            res = wait_our_turn(&qe, ringing, &reason);
            /* If they hungup, return immediately */
            if (res < 0)
            {
                /* Record this abandoned call */
                record_abandoned(&qe);
                cw_queue_log(argv[0], chan->uniqueid, "NONE", "ABANDON", "%d|%d|%ld", qe.pos, qe.opos, (long)time(NULL) - qe.start);
                if (option_verbose > 2)
                {
                    cw_verbose(VERBOSE_PREFIX_3 "User disconnected from queue %s while waiting their turn\n", argv[0]);
                    res = -1;
                }
                break;
            }
            if (!res) 
                break;
            if (valid_exit(&qe, res))
            {
                cw_queue_log(argv[0], chan->uniqueid, "NONE", "EXITWITHKEY", "%s|%d", qe.digits, qe.pos);
                break;
            }
        }
        if (!res)
        {
            int makeannouncement = 0;
            for (;;)
            {
                /* This is the wait loop for the head caller*/
                /* To exit, they may get their call answered; */
                /* they may dial a digit from the queue context; */
                /* or, they may timeout. */

                enum queue_member_status status;

                /* Leave if we have exceeded our queuetimeout */
                if (qe.expire && (time(NULL) > qe.expire))
                {
                    record_abandoned(&qe);
                    reason = QUEUE_TIMEOUT;
                    res = 0;
                    cw_queue_log(argv[0], chan->uniqueid,"NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
                    break;
                }

                if (makeannouncement)
                {
                    /* Make a position announcement, if enabled */
                    if (qe.parent->announcefrequency && !ringing)
                        res = say_position(&qe);
                    if (res && valid_exit(&qe, res))
                    {
                        cw_queue_log(argv[0], chan->uniqueid, "NONE", "EXITWITHKEY", "%s|%d", qe.digits, qe.pos);
                        break;
                    }

                }
                makeannouncement = 1;

                /* Make a periodic announcement, if enabled */
                if (qe.parent->periodicannouncefrequency && !ringing)
                    res = say_periodic_announcement(&qe);

                if (res && valid_exit(&qe, res))
                {
                    cw_queue_log(argv[0], chan->uniqueid, "NONE", "EXITWITHKEY", "%c|%d", res, qe.pos);
                    break;
                }

                /* Try calling all queue members for 'timeout' seconds */
                res = try_calling(&qe, argv[1], argv[3], argv[2], &go_on);
                if (res)
                {
                    if (res < 0)
                    {
                        if (!qe.handled)
                        {
                            record_abandoned(&qe);
                            cw_queue_log(argv[0], chan->uniqueid, "NONE", "ABANDON", "%d|%d|%ld", qe.pos, qe.opos, (long)time(NULL) - qe.start);
                        }
                    }
                    else if (res > 0)
                        cw_queue_log(argv[0], chan->uniqueid, "NONE", "EXITWITHKEY", "%s|%d", qe.digits, qe.pos);
                    break;
                }

                status = get_member_status(qe.parent);

                /* leave the queue if no agents, if enabled */
                if (qe.parent->leavewhenempty && (status == QUEUE_NO_MEMBERS))
                {
                    record_abandoned(&qe);
                    reason = QUEUE_LEAVEEMPTY;
                    res = 0;
                    break;
                }

                /* leave the queue if no reachable agents, if enabled */
                if ((qe.parent->leavewhenempty == QUEUE_EMPTY_STRICT) && (status == QUEUE_NO_REACHABLE_MEMBERS))
                {
                    record_abandoned(&qe);
                    reason = QUEUE_LEAVEUNAVAIL;
                    res = 0;
                    break;
                }

                /* Leave if we have exceeded our queuetimeout */
                if (qe.expire && (time(NULL) > qe.expire))
                {
                    record_abandoned(&qe);
                    reason = QUEUE_TIMEOUT;
                    res = 0;
                    cw_queue_log(argv[0], chan->uniqueid,"NONE", "EXITWITHTIMEOUT", "%d", qe.pos);    
                    break;
                }

                /* OK, we didn't get anybody; wait for 'retry' seconds; may get a digit to exit with */
                res = wait_a_bit(&qe);

                if (res < 0)
                {
                    record_abandoned(&qe);
                    cw_queue_log(argv[0], chan->uniqueid, "NONE", "ABANDON", "%d|%d|%ld", qe.pos, qe.opos, (long)time(NULL) - qe.start);
                    if (option_verbose > 2)
                    {
                        cw_verbose(VERBOSE_PREFIX_3 "User disconnected from queue %s when they almost made it\n", argv[0]);
                        res = -1;
                    }
                    break;
                }
                if (res && valid_exit(&qe, res))
                {
                    cw_queue_log(argv[0], chan->uniqueid, "NONE", "EXITWITHKEY", "%s|%d", qe.digits, qe.pos);
                    break;
                }
                /* exit after 'timeout' cycle if 'n' option enabled */
                if (go_on)
                {
                    if (option_verbose > 2)
                    {
                        cw_verbose(VERBOSE_PREFIX_3 "Exiting on time-out cycle\n");
                        res = -1;
                    }
                    cw_queue_log(argv[0], chan->uniqueid, "NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
                    record_abandoned(&qe);
                    reason = QUEUE_TIMEOUT;
                    res = 0;
                    break;
                }
                /* Since this is a priority queue and 
                 * it is not sure that we are still at the head
                 * of the queue, go and check for our turn again.
                 */
                qe.trying_agent =0;
                if (!is_our_turn(&qe))
                {
                    if (option_debug)
                        cw_log(CW_LOG_DEBUG, "Darn priorities, going back in queue (%s)!\n",
                                qe.chan->name);
                    goto check_turns;
                }
            }
        }
        /* Don't allow return code > 0 */
        if (res >= 0 && res != CW_PBX_KEEPALIVE)
        {
            res = 0;    
            if (ringing)
            {
                cw_indicate(chan, -1);
            }
            else
            {
                cw_moh_stop(chan);
            }           
            cw_stopstream(chan);
        }
        leave_queue(&qe);
        cw_queue_log(argv[0], chan->uniqueid, "NONE", "EXITWITHKEY", "empty|%d", qe.pos);
        if (reason != QUEUE_UNKNOWN)
            set_queue_result(chan, reason);
    }
    else
    {
        cw_log(CW_LOG_WARNING, "Unable to join queue '%s' reason %d\n", argv[0], (int)reason);
        set_queue_result(chan, reason);
        res = 0;
    }
    LOCAL_USER_REMOVE(u);
    return res;
}

static int queue_function_qac(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    int count = 0;
    struct cw_call_queue *q;
    struct localuser *u;
    struct member *m;

    if (argc != 1)
        return cw_function_syntax(queueagentcount_func_syntax);

    if (!result)
        return 0;

    LOCAL_USER_ADD(u);

    /* Find the right queue */
    cw_mutex_lock(&qlock);
    for (q = queues; q; q = q->next)
    {
        if (!strcasecmp(q->name, argv[0]))
        {
            cw_mutex_lock(&q->lock);
            break;
        }
    }
    cw_mutex_unlock(&qlock);

    if (q)
    {
        for (m = q->members; m; m = m->next)
        {
            /* Count the agents who are logged in and presently answering calls */
            if ((m->status != CW_DEVICE_UNAVAILABLE) && (m->status != CW_DEVICE_INVALID))
            {
                count++;
            }
        }
        cw_mutex_unlock(&q->lock);
    }

    cw_dynstr_printf(result, "%d", count);

    LOCAL_USER_REMOVE(u);
    return 0;
}


static void reload_queues(void)
{
    struct cw_call_queue *q, *ql, *qn;
    struct cw_config *cfg;
    char *cat, *tmp;
    struct cw_variable *var;
    struct member *prev, *cur;
    int new;
    char *general_val = NULL;
    char interface[80];
    int penalty;
    
    cfg = cw_config_load("queues.conf");
    if (!cfg)
    {
        cw_log(CW_LOG_NOTICE, "No call queueing config file (queues.conf), so no call queues\n");
        return;
    }
    memset(interface, 0, sizeof(interface));
    cw_mutex_lock(&qlock);
    use_weight=0;
    /* Mark all queues as dead for the moment */
    q = queues;
    while(q)
    {
        q->dead = 1;
        q = q->next;
    }
    /* Chug through config file */
    cat = cw_category_browse(cfg, NULL);
    while(cat)
    {
        if (!strcasecmp(cat, "general"))
        {  
            /* Initialize global settings */
            queue_persistent_members = 0;
            if ((general_val = cw_variable_retrieve(cfg, "general", "persistentmembers")))
                queue_persistent_members = cw_true(general_val);
        }
        else
        {    /* Define queue */
            /* Look for an existing one */
            q = queues;
            while(q)
            {
                if (!strcmp(q->name, cat))
                    break;
                q = q->next;
            }
            if (!q)
            {
                /* Make one then */
                q = alloc_queue(cat);
                new = 1;
            }
            else
                new = 0;
            if (q)
            {
                if (!new)
                    cw_mutex_lock(&q->lock);
                /* Re-initialize the queue, and clear statistics */
                init_queue(q);
                clear_queue(q);
                free_members(q, 0);
                prev = q->members;
                if (prev)
                {
                    /* find the end of any dynamic members */
                    while(prev->next)
                        prev = prev->next;
                }
                var = cw_variable_browse(cfg, cat);
                while(var)
                {
                    if (!strcasecmp(var->name, "member"))
                    {
                        /* Add a new member */
                        cw_copy_string(interface, var->value, sizeof(interface));
                        if ((tmp = strchr(interface, ',')))
                        {
                            *tmp = '\0';
                            tmp++;
                            penalty = atoi(tmp);
                            if (penalty < 0)
                            {
                                penalty = 0;
                            }
                        }
                        else
                            penalty = 0;
                        cur = create_queue_member(interface, penalty, 0);
                        if (cur)
                        {
                            if (prev)
                                prev->next = cur;
                            else
                                q->members = cur;
                            prev = cur;
                        }
                    }
                    else
                    {
                        queue_set_param(q, var->name, var->value, var->lineno, 1);
                    }
                    var = var->next;
                }
                if (!new) 
                    cw_mutex_unlock(&q->lock);
                if (new)
                {
                    q->next = queues;
                    queues = q;
                }
            }
        }
        cat = cw_category_browse(cfg, cat);
    }
    cw_config_destroy(cfg);
    q = queues;
    ql = NULL;
    while(q)
    {
        qn = q->next;
        if (q->dead)
        {
            if (ql)
                ql->next = q->next;
            else
                queues = q->next;
            if (!q->count)
            {
                free(q);
            }
            else
                cw_log(CW_LOG_WARNING, "XXX Leaking a little memory :( XXX\n");
        }
        else
        {
            for (cur = q->members; cur; cur = cur->next)
                cur->status = cw_device_state(cur->interface);
            ql = q;
        }
        q = qn;
    }
    cw_mutex_unlock(&qlock);
}

static int __queues_show(int manager, struct cw_dynstr *ds_p, int argc, char **argv, int queue_show)
{
    struct cw_call_queue *q;
    struct queue_ent *qe;
    struct member *mem;
    int pos;
    time_t now;
    char max_buf[80];
    char *max;
    size_t max_left;
    float sl = 0;
    const char *term = manager ? "\r\n" : "\n";

    time(&now);
    if ((!queue_show && argc != 2) || (queue_show && argc != 3))
        return RESULT_SHOWUSAGE;
    cw_mutex_lock(&qlock);
    q = queues;
    if (!q)
    {   
        cw_mutex_unlock(&qlock);
        if (queue_show)
            cw_dynstr_printf(ds_p, "No such queue: %s.%s",argv[2], term);
        else
            cw_dynstr_printf(ds_p, "No queues.%s", term);
        return RESULT_SUCCESS;
    }
    while (q)
    {
        cw_mutex_lock(&q->lock);
        if (queue_show)
        {
            if (strcasecmp(q->name, argv[2]) != 0)
            {
                cw_mutex_unlock(&q->lock);
                q = q->next;
                if (!q)
                {
                    cw_dynstr_printf(ds_p, "No such queue: %s.%s",argv[2], term);
                    break;
                }
                continue;
            }
        }
        max_buf[0] = '\0';
        max = max_buf;
        max_left = sizeof(max_buf);
        if (q->maxlen)
            cw_build_string(&max, &max_left, "%d", q->maxlen);
        else
            cw_build_string(&max, &max_left, "unlimited");
        sl = 0;
        if(q->callscompleted > 0)
            sl = 100*((float)q->callscompletedinsl/(float)q->callscompleted);
        cw_dynstr_printf(ds_p, "%-12.12s has %d calls (max %s) in '%s' strategy (%ds holdtime), W:%d, C:%d, A:%d, SL:%2.1f%% within %ds%s",
            q->name, q->count, max_buf, int2strat(q->strategy), q->holdtime, q->weight, q->callscompleted, q->callsabandoned,sl,q->servicelevel, term);
        if (q->members)
        {
            cw_dynstr_printf(ds_p, "   Members: %s", term);
            for (mem = q->members; mem; mem = mem->next)
            {
                max_buf[0] = '\0';
                max = max_buf;
                max_left = sizeof(max_buf);
                if (mem->penalty)
                    cw_build_string(&max, &max_left, " with penalty %d", mem->penalty);
                if (mem->dynamic)
                    cw_build_string(&max, &max_left, " (dynamic)");
                if (mem->paused)
                    cw_build_string(&max, &max_left, " (paused)");
                cw_build_string(&max, &max_left, " (%s)", devstate2str(mem->status));
                if (mem->calls)
                {
                    cw_build_string(&max, &max_left, " has taken %d calls (last was %ld secs ago)",
                             mem->calls, (long)(time(NULL) - mem->lastcall));
                }
                else
                    cw_build_string(&max, &max_left, " has taken no calls yet");
                cw_dynstr_printf(ds_p, "      %s%s%s", mem->interface, max_buf, term);
            }
        }
        else
            cw_dynstr_printf(ds_p, "   No Members%s", term);
        if (q->head)
        {
            pos = 1;
            cw_dynstr_printf(ds_p, "   Callers: %s", term);
            for (qe = q->head; qe; qe = qe->next) 
                cw_dynstr_printf(ds_p, "      %d. %s (wait: %ld:%2.2ld, prio: %d)%s", pos++, qe->chan->name,
                    (long)(now - qe->start) / 60, (long)(now - qe->start) % 60, qe->prio, term);
        }
        else
            cw_dynstr_printf(ds_p, "   No Callers%s", term);
        cw_dynstr_printf(ds_p, "%s", term);
        cw_mutex_unlock(&q->lock);
        q = q->next;
        if (queue_show)
            break;
    }
    cw_mutex_unlock(&qlock);
    return RESULT_SUCCESS;
}

static int queues_show(struct cw_dynstr *ds_p, int argc, char **argv)
{
    return __queues_show(0, ds_p, argc, argv, 0);
}

static int queue_show(struct cw_dynstr *ds_p, int argc, char **argv)
{
    return __queues_show(0, ds_p, argc, argv, 1);
}

static void complete_queue(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
    struct cw_call_queue *q;

    cw_mutex_lock(&qlock);

    for (q = queues; q; q = q->next)
    {
        if (!strncasecmp(argv[lastarg], q->name, lastarg_len))
            cw_dynstr_printf(ds_p, "%s\n", q->name);
    }

    cw_mutex_unlock(&qlock);
}

/*! \brief callback to display queues status in manager
  \addtogroup Group_AMI
*/
static struct cw_manager_message *manager_queues_show(struct mansession *sess, const struct message *req)
{
	static const char *a[] = { "show", "queues" };
	struct cw_manager_message *msg;

	CW_UNUSED(sess);
	CW_UNUSED(req);

	if ((msg = cw_manager_response("Follows", NULL))) {
		__queues_show(1, &msg->ds, 2, (char **)a, 0);
		cw_dynstr_printf(&msg->ds, "--END COMMAND--\r\n\r\n");
	}

	return msg;
} 

/* Dump queue status */
static struct cw_manager_message *manager_queues_status(struct mansession *sess, const struct message *req)
{
    time_t now;
    struct cw_manager_message *msg;
    char *queuefilter = cw_manager_msg_header(req," Queue");
    char *memberfilter = cw_manager_msg_header(req, "Member");
    struct cw_call_queue *q;
    struct queue_ent *qe;
    struct member *mem;
    float sl = 0;
    int pos;
    int err;

    if ((msg = cw_manager_response("Success", "Queue status will follow")) && !cw_manager_send(sess, req, &msg)) {
	err = 0;

        cw_mutex_lock(&qlock);

        time(&now);

        for (q = queues; !err && q; q = q->next)
        {
            cw_mutex_lock(&q->lock);

            /* List queue properties */
            if (cw_strlen_zero(queuefilter) || !strcmp(q->name, queuefilter))
            {
                if(q->callscompleted > 0)
                    sl = 100*((float)q->callscompletedinsl/(float)q->callscompleted);

		cw_manager_msg(&msg, 10,
			cw_msg_tuple("Event",            "%s",    "QueueParams"),
			cw_msg_tuple("Queue",            "%s",    q->name),
			cw_msg_tuple("Max",              "%d",    q->maxlen),
			cw_msg_tuple("Calls",            "%d",    q->count),
			cw_msg_tuple("Holdtime",         "%d",    q->holdtime),
			cw_msg_tuple("Completed",        "%d",    q->callscompleted),
			cw_msg_tuple("Abandoned",        "%d",    q->callsabandoned),
			cw_msg_tuple("ServiceLevel",     "%d",    q->servicelevel),
			cw_msg_tuple("ServiceLevelPerf", "%2.1f", sl),
			cw_msg_tuple("Weight",           "%d",    q->weight)
		);

		err = cw_manager_send(sess, req, &msg);

                /* List Queue Members */
                for (mem = q->members; !err && mem; mem = mem->next)
                {
                    if (cw_strlen_zero(memberfilter) || !strcmp(mem->interface, memberfilter))
                    {
			cw_manager_msg(&msg, 9,
					cw_msg_tuple("Event",      "%s",  "QueueMember"),
					cw_msg_tuple("Queue",      "%s",  q->name),
					cw_msg_tuple("Location",   "%s",  mem->interface),
					cw_msg_tuple("Membership", "%s",  (mem->dynamic ? "dynamic" : "static")),
					cw_msg_tuple("Penalty",    "%d",  mem->penalty),
					cw_msg_tuple("CallsTaken", "%d",  mem->calls),
					cw_msg_tuple("LastCall",   "%ld", mem->lastcall),
					cw_msg_tuple("Status",     "%d",  mem->status),
					cw_msg_tuple("Paused",     "%d",  mem->paused)
			);

                        err = cw_manager_send(sess, req, &msg);
                    }
                }

                /* List Queue Entries */
                pos = 1;
                for (qe = q->head; !err && qe; qe = qe->next)
                {
		    cw_manager_msg(&msg, 7,
				    cw_msg_tuple("Event",        "%s",  "QueueEntry"),
				    cw_msg_tuple("Queue",        "%s",  q->name),
				    cw_msg_tuple("Position",     "%d",  pos++),
				    cw_msg_tuple("Channel",      "%s",  qe->chan->name),
				    cw_msg_tuple("CallerID",     "%s",  (qe->chan->cid.cid_num ? qe->chan->cid.cid_num : "unknown")),
				    cw_msg_tuple("CallerIDName", "%s",  (qe->chan->cid.cid_name ? qe->chan->cid.cid_name : "unknown")),
				    cw_msg_tuple("Wait",         "%ld", (long)(now - qe->start))
		    );

                    err = cw_manager_send(sess, req, &msg);
                }
            }
            cw_mutex_unlock(&q->lock);
        }
        cw_mutex_unlock(&qlock);

	if (!err)
		cw_manager_msg(&msg, 1, cw_msg_tuple("Event", "%s", "QueueStatusComplete"));
    }

    return msg;
}

static struct cw_manager_message *manager_add_queue_member(struct mansession *sess, const struct message *req)
{
    struct cw_manager_message *msg;
    char *queuename, *interface, *penalty_s, *paused_s;
    int paused, penalty = 0;

    CW_UNUSED(sess);

    queuename = cw_manager_msg_header(req, "Queue");
    interface = cw_manager_msg_header(req, "Interface");
    penalty_s = cw_manager_msg_header(req, "Penalty");
    paused_s = cw_manager_msg_header(req, "Paused");

    if (cw_strlen_zero(queuename))
        return cw_manager_response("Error", "'Queue' not specified.");

    if (cw_strlen_zero(interface))
        return cw_manager_response("Error", "'Interface' not specified.");

    if (cw_strlen_zero(penalty_s))
        penalty = 0;
    else if (sscanf(penalty_s, "%d", &penalty) != 1)
    {
        penalty = 0;
    }

    if (cw_strlen_zero(paused_s))
        paused = 0;
    else
        paused = abs(cw_true(paused_s));

    msg = NULL;
    switch (add_to_queue(queuename, interface, penalty, paused, queue_persistent_members))
    {
    case RES_OKAY:
        msg = cw_manager_response("Success", "Added interface to queue");
        break;
    case RES_EXISTS:
        msg = cw_manager_response("Error", "Unable to add interface: Already there");
        break;
    case RES_NOSUCHQUEUE:
        msg = cw_manager_response("Error", "Unable to add interface to queue: No such queue");
        break;
    case RES_OUTOFMEMORY:
        msg = cw_manager_response("Error", "Out of memory");
        break;
    }
    return msg;
}

static struct cw_manager_message *manager_update_queue_member(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg;
	char *queuename, *interface, *penalty_s, *paused_s;
	int paused, penalty = 0;

	CW_UNUSED(sess);

	queuename = cw_manager_msg_header(req, "Queue");
	interface = cw_manager_msg_header(req, "Interface");
	penalty_s = cw_manager_msg_header(req, "Penalty");
	paused_s = cw_manager_msg_header(req, "Paused");

	if (cw_strlen_zero(queuename))
		return cw_manager_response("Error", "'Queue' not specified.");

	if (cw_strlen_zero(interface))
		return cw_manager_response("Error", "'Interface' not specified.");

	if (cw_strlen_zero(penalty_s))
		penalty = 0;
	else if (sscanf(penalty_s, "%d", &penalty) != 1) {
		penalty = 0;
	}

	if (cw_strlen_zero(paused_s))
		paused = 0;
	else
		paused = abs(cw_true(paused_s));

	msg = NULL;
	switch (update_queue_member(queuename, interface, penalty, paused, queue_persistent_members)) {
	case RES_OKAY:
		msg = cw_manager_response("Success", "Updated member to queue");
		break;
	case RES_EXISTS:
		msg = cw_manager_response("Error", "Unable to update member: Not there");
		break;
	case RES_NOSUCHQUEUE:
		msg = cw_manager_response("Error", "Unable to update member on queue: No such queue");
		break;
	case RES_OUTOFMEMORY:
		msg = cw_manager_response("Error", "Out of memory");
		break;
	}
	return msg;
}


static struct cw_manager_message *manager_remove_queue_member(struct mansession *sess, const struct message *req)
{
    char *queuename, *interface;
    struct cw_manager_message *msg;

    CW_UNUSED(sess);

    queuename = cw_manager_msg_header(req, "Queue");
    interface = cw_manager_msg_header(req, "Interface");

    if (cw_strlen_zero(queuename) || cw_strlen_zero(interface))
        return cw_manager_response("Error", "Need 'Queue' and 'Interface' parameters.");

    msg = NULL;
    switch (remove_from_queue(queuename, interface, NULL))
    {
    case RES_OKAY:
        msg = cw_manager_response("Success", "Removed interface from queue");
        break;
    case RES_EXISTS:
        msg = cw_manager_response("Error", "Unable to remove interface: Not there");
        break;
    case RES_NOSUCHQUEUE:
        msg = cw_manager_response("Error", "Unable to remove interface from queue: No such queue");
        break;
    case RES_OUTOFMEMORY:
        msg = cw_manager_response("Error", "Out of memory");
        break;
    }
    return msg;
}

static struct cw_manager_message *manager_pause_queue_member(struct mansession *sess, const struct message *req)
{
    struct cw_manager_message *msg = NULL;
    char *queuename, *interface, *paused_s;
    int paused;

    CW_UNUSED(sess);

    interface = cw_manager_msg_header(req, "Interface");
    paused_s = cw_manager_msg_header(req, "Paused");
    queuename = cw_manager_msg_header(req, "Queue");  /* Optional - if not supplied, pause the given Interface in all queues */

    if (!cw_strlen_zero(interface) && !cw_strlen_zero(paused_s)) {
        paused = abs(cw_true(paused_s));

        if (set_member_paused(queuename, interface, paused))
            msg = cw_manager_response("Error", "Interface not found");
        else {
            if (paused)
                msg = cw_manager_response("Success", "Interface paused successfully");
            else
                msg = cw_manager_response("Success", "Interface unpaused successfully");
        }
    } else
        msg = cw_manager_response("Error", "Need 'Interface' and 'Paused' parameters.");

    return msg;
}

static int handle_add_queue_member(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    char *queuename, *interface;
    int penalty;
    int ret;

    if ((argc != 6 && argc != 8) || strcmp(argv[4], "to") || (argc == 8 && strcmp(argv[6], "penalty")))
        return RESULT_SHOWUSAGE;

    queuename = argv[5];
    interface = argv[3];
    if (argc == 8)
    {
        if (sscanf(argv[7], "%d", &penalty) == 1)
        {
            if (penalty < 0)
            {
                cw_dynstr_printf(ds_p, "Penalty must be >= 0\n");
                penalty = 0;
            }
        }
        else
        {
            cw_dynstr_printf(ds_p, "Penalty must be an integer >= 0\n");
            penalty = 0;
        }
    }
    else
    {
        penalty = 0;
    }

    switch (add_to_queue(queuename, interface, penalty, 0, queue_persistent_members))
    {
    case RES_OKAY:
        cw_dynstr_printf(ds_p, "Added interface '%s' to queue '%s'\n", interface, queuename);
        ret = RESULT_SUCCESS;
        break;
    case RES_EXISTS:
        cw_dynstr_printf(ds_p, "Unable to add interface '%s' to queue '%s': Already there\n", interface, queuename);
        ret = RESULT_FAILURE;
        break;
    case RES_NOSUCHQUEUE:
        cw_dynstr_printf(ds_p, "Unable to add interface to queue '%s': No such queue\n", queuename);
        ret = RESULT_FAILURE;
        break;
    case RES_OUTOFMEMORY:
        cw_dynstr_printf(ds_p, "Out of memory\n");
        ret = RESULT_FAILURE;
        break;
    default:
        ret = RESULT_FAILURE;
        break;
    }

    return ret;
}

static void complete_add_queue_member(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
    int i;

    /* 0 - add; 1 - queue; 2 - member; 3 - <member>; 4 - to; 5 - <queue>; 6 - penalty; 7 - <penalty> */
    switch (lastarg)
    {
    case 3:
        /* Don't attempt to complete name of member (infinite possibilities) */
        break;
    case 4:
        if (!strncmp(argv[4], "to", lastarg_len))
            cw_dynstr_printf(ds_p, "to\n");
        break;
    case 5:
        /* No need to duplicate code */
        complete_queue(ds_p, argv, lastarg, lastarg_len);
        break;
    case 6:
        if (!strncmp(argv[6], "penalty", lastarg_len))
            cw_dynstr_printf(ds_p, "penalty\n");
        break;
    case 7:
        for (i = 0; i < 100; i++)
            cw_dynstr_printf(ds_p, "%d\n", i);
        break;
    }
}

static int handle_remove_queue_member(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    char *queuename, *interface;

    if (argc != 6)
    {
        return RESULT_SHOWUSAGE;
    }
    else if (strcmp(argv[4], "from"))
    {
        return RESULT_SHOWUSAGE;
    }

    queuename = argv[5];
    interface = argv[3];

    switch (remove_from_queue(queuename, interface, NULL))
    {
    case RES_OKAY:
        cw_dynstr_printf(ds_p, "Removed interface '%s' from queue '%s'\n", interface, queuename);
        return RESULT_SUCCESS;
    case RES_EXISTS:
        cw_dynstr_printf(ds_p, "Unable to remove interface '%s' from queue '%s': Not there\n", interface, queuename);
        return RESULT_FAILURE;
    case RES_NOSUCHQUEUE:
        cw_dynstr_printf(ds_p, "Unable to remove interface from queue '%s': No such queue\n", queuename);
        return RESULT_FAILURE;
    case RES_OUTOFMEMORY:
        cw_dynstr_printf(ds_p, "Out of memory\n");
        return RESULT_FAILURE;
    default:
        return RESULT_FAILURE;
    }
}

static void complete_remove_queue_member(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
    struct cw_call_queue *q;
    struct member *m;

    /* 0 - add; 1 - queue; 2 - member; 3 - <member>; 4 - to; 5 - <queue> */
    if ((lastarg > 5) || (lastarg < 3))
    {
        return;
    }

    if (lastarg == 4)
    {
        if (!strncmp(argv[4], "from", lastarg_len))
            cw_dynstr_printf(ds_p, "from\n");
        return;
    }

    if (lastarg == 5)
    {
        /* No need to duplicate code */
        complete_queue(ds_p, argv, lastarg, lastarg_len);
        return;
    }

    for (q = queues; q; q = q->next)
    {
        cw_mutex_lock(&q->lock);
        for (m = q->members; m; m = m->next)
            if (!strncmp(argv[lastarg], m->interface, lastarg_len))
                cw_dynstr_printf(ds_p, "%s\n", m->interface);
        cw_mutex_unlock(&q->lock);
    }
}

static const char show_queues_usage[] =
    "Usage: show queues\n"
    "       Provides summary information on call queues.\n";

static struct cw_clicmd cli_show_queues = {
    .cmda = { "show", "queues", NULL },
    .handler = queues_show, 
    .summary = "Show status of queues",
    .usage = show_queues_usage,
};

static const char show_queue_usage[] =
    "Usage: show queue\n"
    "       Provides summary information on a specified queue.\n";

static struct cw_clicmd cli_show_queue = {
    .cmda = { "show", "queue", NULL },
    .handler = queue_show, 
    .generator = complete_queue,
    .summary = "Show status of a specified queue",
    .usage = show_queue_usage,
};

static const char aqm_cmd_usage[] =
    "Usage: add queue member <channel> to <queue> [penalty <penalty>]\n";

static struct cw_clicmd cli_add_queue_member = {
    .cmda = { "add", "queue", "member", NULL },
    .handler = handle_add_queue_member,
    .generator = complete_add_queue_member,
    .summary = "Add a channel to a specified queue",
    .usage = aqm_cmd_usage,
};

static const char rqm_cmd_usage[] =
    "Usage: remove queue member <channel> from <queue>\n";

static struct cw_clicmd cli_remove_queue_member = {
    .cmda = { "remove", "queue", "member", NULL },
    .handler = handle_remove_queue_member,
    .generator = complete_remove_queue_member,
    .summary = "Removes a channel from a specified queue",
    .usage = rqm_cmd_usage,
};


static struct manager_action manager_actions[] = {
    {
	    .action = "Queues",
	    .authority = 0,
	    .func = manager_queues_show,
	    .synopsis = "Queues",
    },
    {
	    .action = "QueueStatus",
	    .authority = 0,
	    .func = manager_queues_status,
	    .synopsis = "Queue Status",
    },
    {
	    .action = "QueueAdd",
	    .authority = EVENT_FLAG_AGENT,
	    .func = manager_add_queue_member,
	    .synopsis = "Add interface to queue.",
    },
    {
	    .action = "QueueRemove",
	    .authority = EVENT_FLAG_AGENT,
	    .func = manager_remove_queue_member,
	    .synopsis = "Remove interface from queue.",
    },
    {
	    .action = "QueuePause",
	    .authority = EVENT_FLAG_AGENT,
	    .func = manager_pause_queue_member,
	    .synopsis = "Makes a queue member temporarily unavailable",
    },
    {
	    .action = "QueueMemberUpdate",
	    .authority = EVENT_FLAG_AGENT,
	    .func = manager_update_queue_member,
	    .synopsis = "Update Member on queue.",
    },
};

static int unload_module(void)
{
    int res = 0;

    cw_cli_unregister(&cli_show_queue);
    cw_cli_unregister(&cli_show_queues);
    cw_cli_unregister(&cli_add_queue_member);
    cw_cli_unregister(&cli_remove_queue_member);
    cw_manager_action_unregister_multiple(manager_actions, arraysize(manager_actions));
    cw_devstate_del(statechange_queue, NULL);
    res |= cw_unregister_function(app_aqm);
    res |= cw_unregister_function(app_rqm);
    res |= cw_unregister_function(app_pqm);
    res |= cw_unregister_function(app_upqm);
    res |= cw_unregister_function(queueagentcount_function);
    res |= cw_unregister_function(app);
    return res;
}

static int load_module(void)
{
    app = cw_register_function(name, queue_exec, synopsis, syntax, descrip);
    cw_cli_register(&cli_show_queue);
    cw_cli_register(&cli_show_queues);
    cw_cli_register(&cli_add_queue_member);
    cw_cli_register(&cli_remove_queue_member);
    cw_devstate_add(statechange_queue, NULL);
    cw_manager_action_register_multiple(manager_actions, arraysize(manager_actions));
    app_aqm = cw_register_function(name_aqm, aqm_exec, app_aqm_synopsis, app_aqm_syntax, app_aqm_descrip) ;
    app_rqm = cw_register_function(name_rqm, rqm_exec, app_rqm_synopsis, app_rqm_syntax, app_rqm_descrip) ;
    app_pqm = cw_register_function(name_pqm, pqm_exec, app_pqm_synopsis, app_pqm_syntax, app_pqm_descrip) ;
    app_upqm = cw_register_function(name_upqm, upqm_exec, app_upqm_synopsis, app_upqm_syntax, app_upqm_descrip) ;
    queueagentcount_function = cw_register_function(queueagentcount_func_name, queue_function_qac, queueagentcount_func_synopsis, queueagentcount_func_syntax, queueagentcount_func_desc);

    reload_queues();
    
    if (queue_persistent_members)
        reload_queue_members();

    return 0;
}


static int reload_module(void)
{
    reload_queues();
    return 0;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, tdesc)
