/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * UniCall support
 * 
 * Copyright (C) 2003, 2004, 2005 Steve Underwood <steveu@coppice.org>
 * based on work Copyright (C) 1999, Mark Spencer
 *
 * Steve Underwood <steveu@coppice.org>
 * Based on chan_zap.c by Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <string.h>
#ifdef __NetBSD__
#include <pthread.h>
#include <signal.h>
#else
#include <sys/signal.h>
#endif
#include <sys/select.h>
#include <errno.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <math.h>
#include <ctype.h>
#include <linux/zaptel.h>

#include <spandsp.h>
#include <libsupertone.h>
#include <unicall.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL$", "$Revision$")

#include "openpbx/lock.h"
#include "openpbx/channel.h"
#include "openpbx/config.h"
#include "openpbx/logger.h"
#include "openpbx/module.h"
#include "openpbx/pbx.h"
#include "openpbx/options.h"
#include "openpbx/file.h"
#include "openpbx/ulaw.h"
#include "openpbx/alaw.h"
#include "openpbx/phone_no_utils.h"
#include "openpbx/adsi.h"
#include "openpbx/cli.h"
#include "openpbx/cdr.h"
#include "openpbx/musiconhold.h"
#include "openpbx/say.h"
#include "openpbx/app.h"
#include "openpbx/dsp.h"
#include "openpbx/utils.h"
#include "openpbx/causes.h"

/* 
   XXX 
   XXX   We definitely need to lock the private structure in unicall_read and such 
   XXX  
 */

/*
    When a physical interface is not in the connected state, processing for that channel should be
    handled completely by the monitor code. Once a completed call connection exists, the call can
    be handled in other ways - usually by its owner. At this stage, proper monitoring for call
    disconnection is still required.
 */
 
/* Dialing plan - bundled packages of TON and NPI */
#define PRI_INTERNATIONAL_ISDN      ((UC_TON_INTERNATIONAL << 4) | UC_NPI_E163_E164)
#define PRI_NATIONAL_ISDN           ((UC_TON_NATIONAL << 4) | UC_NPI_E163_E164)
#define PRI_LOCAL_ISDN              ((UC_TON_SUBSCRIBER << 4) | UC_NPI_E163_E164)
#define PRI_PRIVATE                 ((UC_TON_SUBSCRIBER << 4) | UC_NPI_PRIVATE)
#define PRI_UNKNOWN                 ((UC_TON_UNKNOWN << 4) | UC_NPI_UNKNOWN)

#define CHANNEL_PSEUDO -12

static const char desc[] = "Unified call processing (UniCall)";
static const char tdesc[] = "Unified call processing (UniCall) driver";

static const char type[] = "UniCall";
static const char config[] = "unicall.conf";

static int logging_level = 0;
static char super_tones[10 + 1] = "hk";
static super_tone_set_t *set;

#define NUM_SPANS       32
#define RESET_INTERVAL  3600    /* How often (in seconds) to reset unused channels */

#define CHAN_PSEUDO    -2

static char context[OPBX_MAX_EXTENSION] = "default";

static char language[MAX_LANGUAGE] = "";
static char musicclass[MAX_LANGUAGE] = "";

static char *cur_protocol_class = NULL;
static char *cur_protocol_variant = NULL;
static char *cur_protocol_end = NULL;
static int cur_group = 0;
static int cur_callergroup = 0;
static int cur_pickupgroup = 0;

static int immediate = 0;
static int stripmsd = 0;
static int callreturn = 0;
static int threewaycalling = 0;
static int transfer = 0;
static int cancallforward = 0;
static float rxgain = 0.0;
static float txgain = 0.0;
static int t38_support = 0;
static int echocancel = 0;
static int echotraining = 0;
static int echocanbridged = 0;
static char accountcode[20] = "";

static char callerid[256] = "";
static int use_callerid = TRUE;

static char mailbox[OPBX_MAX_EXTENSION];

static int amaflags = 0;

static int adsi = 0;

static int minunused = 2;
static char idleext[OPBX_MAX_EXTENSION];
static char idledial[OPBX_MAX_EXTENSION];

static int usecnt = 0;
OPBX_MUTEX_DEFINE_STATIC(usecnt_lock);

/* Protect the interface list (of unicall_pvt's) */
OPBX_MUTEX_DEFINE_STATIC(iflock);

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
OPBX_MUTEX_DEFINE_STATIC(monlock);

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = OPBX_PTHREADT_NULL;

/* Chunk size to read -- we use 20ms chunks to make things happy.  */   
#define READ_SIZE 160

#define MASK_AVAIL              (1 << 0)        /* Channel available for PRI use */
#define MASK_INUSE              (1 << 1)        /* Channel currently in use */

static struct opbx_channel *unicall_request(const char *type, int format, void *data, int *cause);
static int unicall_call(struct opbx_channel *c, char *dest, int timeout);
static int unicall_hangup(struct opbx_channel *c);
static int unicall_answer(struct opbx_channel *c);
static struct opbx_frame *unicall_read(struct opbx_channel *c);
static int unicall_write(struct opbx_channel *c, struct opbx_frame *f);
static int unicall_indicate(struct opbx_channel *c, int condition);
static int unicall_fixup(struct opbx_channel *oldchan, struct opbx_channel *newchan);
static int unicall_digit(struct opbx_channel *c, char digit);
static int unicall_send_text(struct opbx_channel *c, const char *text);
static int unicall_bridge(struct opbx_channel *c0, struct opbx_channel *c1, int flags, struct opbx_frame **fo, struct opbx_channel **rc);
struct opbx_frame *unicall_exception(struct opbx_channel *ast);
static int unicall_setoption(struct opbx_channel *chan, int option, void *data, int datalen);

static const struct opbx_channel_tech unicall_tech = {
    .type = type,
    .description = tdesc,
    .capabilities = OPBX_FORMAT_ULAW,
    .requester = unicall_request,
    .call = unicall_call,
    .hangup = unicall_hangup,
    .answer = unicall_answer,
    .read = unicall_read,
    .write = unicall_write,
    .indicate = unicall_indicate,
    .fixup = unicall_fixup,
    .send_digit = unicall_digit,
    .send_text = unicall_send_text,
    .bridge = unicall_bridge,
    .exception = unicall_exception,
    .setoption = unicall_setoption,
};

struct unicall_pvt;

typedef struct unicall_uc_s
{
    pthread_t master;                           /* Thread of master */
    opbx_mutex_t lock;                          /* Mutex */
    char idleext[OPBX_MAX_EXTENSION];           /* Where to idle extra calls */
    char idlecontext[OPBX_MAX_EXTENSION];       /* What context to use for idle */
    char idledial[OPBX_MAX_EXTENSION];          /* What to dial before dumping */
    int minunused;                              /* Min # of channels to keep empty */
    int minidle;                                /* Min # of "idling" calls to keep active */
    int nodetype;                               /* Node type */
    int num_channels;                           /* Num of chans in group (say, 31 or 24) */
    uc_t *uc;
    int debug;
    int fd;
    int up;
    int offset;
    int group;
    int resetting;
    int resetchannel;
    time_t lastreset;
    int chanmask[32];                           /* Channel status */
    struct unicall_pvt *pvt[32];                /* Member channel pvt structs */
    struct unicall_channel *chan[32];           /* Channels on each line */
} unicall_uc_t;

#define SUB_REAL        0            /* Active call */
#define SUB_CALLWAIT    1            /* Call-waiting call on hold */
#define SUB_THREEWAY    2            /* Three-way call */

static char *subnames[] =
{
    "Real",
    "Callwait",
    "Threeway"
};

struct unicall_subchannel
{
    int fd;
    struct opbx_channel *owner;
    int chan;
    short buffer[OPBX_FRIENDLY_OFFSET/2 + READ_SIZE];
    int buffer_contents;
    struct opbx_frame f;                         /* One frame for each channel.  How did this ever work before? */
    int needringing;
    int needanswer;
    int needcongestion;
    int needbusy;
    int current_codec;
    int inthreeway;
    int curconfno;                              /* What conference we're currently in */
    dtmf_tx_state_t dtmf_tx_state;
    char dtmfq[OPBX_MAX_EXTENSION];
    int super_tone;
    super_tone_tx_state_t tx_state;
    ZT_CONFINFO curconf;
};

#define CONF_USER_REAL          (1 << 0)
#define CONF_USER_THIRDCALL     (1 << 1)

#define MAX_SLAVES              4

typedef struct unicall_pvt
{
    opbx_mutex_t lock;
    struct opbx_channel *owner;                 /* Our current active owner (if applicable) */
                                                /* Up to three channels can be associated with this call */
        
    struct unicall_subchannel sub_unused;       /* Just a safety precaution */
    struct unicall_subchannel subs[3];          /* Sub-channels */

    struct unicall_pvt *slaves[MAX_SLAVES];     /* Slave to us (follows our conferencing) */
    struct unicall_pvt *master;                 /* Master to us (we follow their conferencing) */
    int inconference;                           /* If our real should be in the conference */
    
    char *protocol_class;                       /* Signalling style */
    char *protocol_variant;                     /* Signalling style */
    int protocol_end;                           /* Signalling style */
    int radio;                                  /* radio type */
    int firstradio;                             /* first radio flag */
    float rxgain;
    float txgain;
    struct unicall_pvt *next;                   /* Next channel in the list */
    struct unicall_pvt *prev;                   /* Previous channel in the list */
    char context[OPBX_MAX_EXTENSION];
    char exten[OPBX_MAX_EXTENSION];
    char language[MAX_LANGUAGE];
    char musicclass[MAX_LANGUAGE];
    char cid_num[OPBX_MAX_EXTENSION];
    char cid_name[OPBX_MAX_EXTENSION];
    char lastcid_num[OPBX_MAX_EXTENSION];
    char lastcid_name[OPBX_MAX_EXTENSION];
    char *origcid_num;                  /* malloced original caller id */
    char *origcid_name;                 /* malloced original caller id */
    char callwait_num[OPBX_MAX_EXTENSION];
    char callwait_name[OPBX_MAX_EXTENSION];
    char rdnis[OPBX_MAX_EXTENSION];
    char dnid[OPBX_MAX_EXTENSION];
    unsigned int group;
    int law;
    int confno;                         /* Our conference */
    int confusers;                      /* Who is using our conference */
    int propconfno;                     /* Propagated conference number */
    int callgroup;
    int pickupgroup;
    int immediate;                      /* Answer before getting digits? */
    int channel;                        /* Channel Number */
    int span;                           /* Span number */
    int dialing;
    int use_callerid;                   /* Whether or not to use caller id on this channel */
    int callreturn;
    int stripmsd;
    int echocancel;
    int echotraining;
    int echocanbridged;
    int echocanon;
    int callingpres;
    int threewaycalling;
    int transfer;
    int digital;
    int outgoing;
    int dnd;
    char dialstr[256];
    int destroy;
    int ignoredtmf;                
    int current_alarms;
    int reserved;
    char accountcode[20];               /* Account code */
    int amaflags;                       /* AMA Flags */
    int adsi;
    int cancallforward;
    char call_forward[OPBX_MAX_EXTENSION];
    char mailbox[OPBX_MAX_EXTENSION];
    int onhooktime;
    int msgstate;
    struct opbx_dsp *dsp;
    int t38_support;
    int t38_active;
    
    int confirmanswer;                  /* Wait for '#' to confirm answer */
    int distinctivering;                /* Which distinctivering to use */
    
    int faxhandled;                     /* Has a fax tone already been handled? */
    
    char mate;                          /* flag to say its in MATE mode */
    int dtmfrelax;                      /* whether to run in relaxed DTMF mode */
    uc_t *uc;
    int blocked;
    int sigcheck;
    int alreadyhungup;
    uc_crn_t crn;
    //uc_call_t *call;
} unicall_pvt_t;

unicall_pvt_t *iflist = NULL;
unicall_pvt_t *ifend = NULL;
unicall_pvt_t *round_robin[32];

#define GET_CHANNEL(p) p->channel

static int restore_gains(struct unicall_pvt *p);
static int restart_monitor(void);
static int unicall_open(struct unicall_pvt *p, char *fn);
static int unicall_close(int fd);

/* Translate between Unicall causes and ast */
static int hangup_uc2cause(int cause)
{
    switch (cause)
    {
    case UC_CAUSE_USER_BUSY:
        return OPBX_CAUSE_BUSY;
    case UC_CAUSE_NORMAL_CLEARING:
        return OPBX_CAUSE_NORMAL;
    case UC_CAUSE_NETWORK_CONGESTION:
    case UC_CAUSE_REQ_CHANNEL_NOT_AVAILABLE:
        return OPBX_CAUSE_CONGESTION;
    case UC_CAUSE_UNASSIGNED_NUMBER:
    case UC_CAUSE_NUMBER_CHANGED:
        return OPBX_CAUSE_UNALLOCATED;
    default:
        return OPBX_CAUSE_FAILURE;
    }
    return 0;
}

/* translate between ast cause and Unicall */
static int hangup_cause2uc(int cause)
{
    switch (cause)
    {
    case OPBX_CAUSE_BUSY:
        return UC_CAUSE_USER_BUSY;
    case OPBX_CAUSE_NORMAL:
    default:
        return UC_CAUSE_NORMAL_CLEARING;
    }
    return 0;
}

int select_codec(struct unicall_pvt *p, int index, int new_codec)
{
    int res;

    res = 0;
    if (p->subs[index].current_codec != new_codec)
    {
        switch (new_codec)
        {
        case OPBX_FORMAT_SLINEAR:
            if (uc_channel_set_api_codec(p->uc, 0, UC_CODEC_LINEAR16))
                res = -1;
            /*endif*/
            break;
        case OPBX_FORMAT_ULAW:
            if (uc_channel_set_api_codec(p->uc, 0, UC_CODEC_ULAW))
                res = -1;
            /*endif*/
            break;
        case OPBX_FORMAT_ALAW:
            if (uc_channel_set_api_codec(p->uc, 0, UC_CODEC_ALAW))
                res = -1;
            /*endif*/
            break;
        default:
            return -1;
        }
        /*endswitch*/
        if (res)
            opbx_log(LOG_WARNING, "Unable to set channel %d to codec 0x%X.\n", p->channel, new_codec);
        else
            p->subs[index].current_codec = new_codec;
        /*endif*/
    }
    /*endif*/
    return res;
}

static void super_tone(struct unicall_subchannel *s, int type)
{
    s->super_tone = type;
    if (type != ST_TYPE_NONE)
        super_tone_tx_init(&s->tx_state, set->tone[type]);
    /*endif*/
}

static int unicall_get_index(struct opbx_channel *ast, struct unicall_pvt *p, int nullok)
{
    int res;

    if (p->subs[SUB_REAL].owner == ast)
        res = SUB_REAL;
    else if (p->subs[SUB_CALLWAIT].owner == ast)
        res = SUB_CALLWAIT;
    else if (p->subs[SUB_THREEWAY].owner == ast)
        res = SUB_THREEWAY;
    else
    {
        res = -1;
        if (!nullok)
            opbx_log(LOG_WARNING, "Unable to get index, and nullok is not asserted\n");
        /*endif*/
    }
    /*endif*/
    return res;
}

static void swap_subs(struct unicall_pvt *p, int a, int b)
{
    int tchan;
    int tinthreeway;
    struct opbx_channel *towner;

    opbx_log(LOG_DEBUG, "Swapping %d and %d\n", a, b);

    tchan = p->subs[a].chan;
    towner = p->subs[a].owner;
    tinthreeway = p->subs[a].inthreeway;

    p->subs[a].chan = p->subs[b].chan;
    p->subs[a].owner = p->subs[b].owner;
    p->subs[a].inthreeway = p->subs[b].inthreeway;

    p->subs[b].chan = tchan;
    p->subs[b].owner = towner;
    p->subs[b].inthreeway = tinthreeway;

    if (p->subs[a].owner)
        p->subs[a].owner->fds[0] = p->subs[a].fd;
    /*endif*/
    if (p->subs[b].owner)
        p->subs[b].owner->fds[0] = p->subs[b].fd;
    /*endif*/
}

static int alloc_sub(struct unicall_pvt *p, int x)
{
    if (p->subs[x].fd >= 0)
    {
        opbx_log(LOG_WARNING, "%s subchannel of %d already in use\n", subnames[x], p->channel);
        return -1;
    }
    /*endif*/
    if ((p->subs[x].fd = unicall_open(p, "/dev/zap/pseudo")) < 0)
    {
        opbx_log(LOG_WARNING, "Unable to open pseudo channel: %s\n", strerror(errno));
        return -1;
    }
    /*endif*/
    if (ioctl(p->subs[x].fd, ZT_CHANNO, &p->subs[x].chan))
    {
        opbx_log(LOG_WARNING, "Unable to get channel number for pseudo channel on FD %d\n", p->subs[x].fd);
        unicall_close(p->subs[x].fd);
        p->subs[x].fd = -1;
        return -1;
    }
    /*endif*/
    if (option_debug)
        opbx_log(LOG_DEBUG, "Allocated %s subchannel on FD %d channel %d\n", subnames[x], p->subs[x].fd, p->subs[x].chan);
    /*endif*/
    return 0;
}

static int unalloc_sub(struct unicall_pvt *p, int x)
{
    if (x == SUB_REAL)
    {
        opbx_log(LOG_WARNING, "Trying to unalloc the real channel %d?!?\n", p->channel);
        return -1;
    }
    /*endif*/
    opbx_log(LOG_DEBUG, "Released sub %d of channel %d\n", x, p->channel);
    if (p->subs[x].fd < 0)
        unicall_close(p->subs[x].fd);
    /*endif*/
    p->subs[x].fd = -1;
    p->subs[x].chan = 0;
    p->subs[x].owner = NULL;
    p->subs[x].inthreeway = FALSE;
    p->subs[x].current_codec = 0;
    p->subs[x].curconfno = -1;
    return 0;
}

static int unicall_open(struct unicall_pvt *p, char *fn)
{
    int fd;

    fd = uc_channel_open(p->protocol_class, p->protocol_variant, p->protocol_end, fn);

    return fd;
}

static int unicall_close(int fd)
{
    return close(fd);
}

static int unicall_digit(struct opbx_channel *ast, char digit)
{
    struct unicall_pvt *p;
    int index;
    char buf[2];

    p = ast->tech_pvt;
    if ((index = unicall_get_index(ast, p, 0)) == SUB_REAL)
    {
        buf[0] = digit;
        buf[1] = '\0';
opbx_log(LOG_WARNING, "Sending DTMF digit\n");
        dtmf_put(&p->subs[SUB_REAL].dtmf_tx_state, buf);
        p->dialing = TRUE;
    }
    /*endif*/    
    return 0;
}

#if 0
static char *zt_events[] =
{
    "No event",
    "On hook",
    "Ring/Answered",
    "Wink/Flash",
    "Alarm",
    "No more alarm",
    "HDLC Abort",
    "HDLC Overrun",
    "HDLC Bad FCS",
    "Dial Complete",
    "Ringer On",
    "Ringer Off",
    "Hook Transition Complete",
    "Bits Changed",
    "Pulse Start"
};
 
static char *zt_event2str(int event)
{
    static char buf[256];

    if (-1 < event  &&  event < 15)
        return zt_events[event];
    /*endif*/
    sprintf(buf, "Event %d", event);
    return buf;
}
#endif

static char *alarm2str(int alarm)
{
    static const struct
    {
        int alarm;
        char *name;
    } alarms[] =
    {
        {ZT_ALARM_RED, "Red Alarm"},
        {ZT_ALARM_YELLOW, "Yellow Alarm"},
        {ZT_ALARM_BLUE, "Blue Alarm"},
        {ZT_ALARM_RECOVER, "Recovering"},
        {ZT_ALARM_LOOPBACK, "Loopback"},
        {ZT_ALARM_NOTOPEN, "Not Open"},
        {ZT_ALARM_NONE, "None"},
    };
    int x;

    for (x = 0;  x < sizeof(alarms)/sizeof(alarms[0]);  x++)
    {
        if (alarms[x].alarm & alarm)
            return alarms[x].name;
        /*endif*/
    }
    /*endfor*/
    return  (alarm)  ?  "Unknown Alarm"  :  "No Alarm";
}

static void unicall_message(char *s)
{
    opbx_verbose(s);
}

static void unicall_report(char *s)
{
    opbx_log(LOG_WARNING, s);
}

static int conf_add(struct unicall_pvt *p, struct unicall_subchannel *c, int index, int slavechannel)
{
    ZT_CONFINFO zi;

    /* If the conference already exists, and we're already in it
       don't bother doing anything */
    memset(&zi, 0, sizeof(zi));
    zi.chan = 0;

    if (slavechannel > 0)
    {
        /* If we have only one slave, do a digital mon */
        zi.confmode = ZT_CONF_DIGITALMON;
        zi.confno = slavechannel;
    }
    else
    {
        if (index)
        {
            zi.confmode = ZT_CONF_CONF | ZT_CONF_TALKER | ZT_CONF_LISTENER;
        }
        else
        {
            /* Real-side and pseudo-side both participate in conference */
            zi.confmode = ZT_CONF_REALANDPSEUDO
                        | ZT_CONF_TALKER
                        | ZT_CONF_LISTENER
                        | ZT_CONF_PSEUDO_TALKER
                        | ZT_CONF_PSEUDO_LISTENER;
        }
        /*endif*/
        zi.confno = p->confno;
    }
    /*endif*/
    if (zi.confno == c->curconf.confno  &&  zi.confmode == c->curconf.confmode)
        return 0;
    /*endif*/
    if (c->fd < 0)
        return 0;
    /*endif*/
    if (ioctl(c->fd, ZT_SETCONF, &zi))
    {
        opbx_log(LOG_WARNING, "Failed to add %d to conference %d/%d\n", c->fd, zi.confmode, zi.confno);
        return -1;
    }
    /*endif*/
    if (slavechannel < 1)
        p->confno = zi.confno;
    /*endif*/
    memcpy(&c->curconf, &zi, sizeof(c->curconf));
    opbx_log(LOG_DEBUG, "Added %d to conference %d/%d\n", c->fd, c->curconf.confmode, c->curconf.confno);
    return 0;
}

static int isourconf(struct unicall_pvt *p, struct unicall_subchannel *c)
{
    /* If they're listening to our channel, they're ours */    
    if ((p->channel == c->curconf.confno)  &&  (c->curconf.confmode == ZT_CONF_DIGITALMON))
        return 1;
    /* If they're a talker on our (allocated) conference, they're ours */
    if ((p->confno > 0)  &&  (p->confno == c->curconf.confno)  &&  (c->curconf.confmode & ZT_CONF_TALKER))
        return 1;
    return 0;
}

static int conf_del(struct unicall_pvt *p, struct unicall_subchannel *c, int index)
{
    ZT_CONFINFO zi;

    /* Can't delete if there's no fd */
    /* Don't delete from the conference if it's not our conference */
    /* Don't delete if we don't think it's conferenced at all (implied) */
    if (c->fd < 0  ||  !isourconf(p, c))
        return 0;
    /*endif*/
    memset(&zi, 0, sizeof(zi));
    zi.chan = 0;
    zi.confno = 0;
    zi.confmode = 0;
    if (ioctl(c->fd, ZT_SETCONF, &zi))
    {
        opbx_log(LOG_WARNING, "Failed to drop %d from conference %d/%d\n", c->fd, c->curconf.confmode, c->curconf.confno);
        return -1;
    }
    /*endif*/
    opbx_log(LOG_DEBUG, "Removed %d from conference %d/%d\n", c->fd, c->curconf.confmode, c->curconf.confno);
    memcpy(&c->curconf, &zi, sizeof(c->curconf));
    return 0;
}

static inline int unicall_confmute(struct unicall_pvt *p, int muted)
{
    int x;

    x = (muted)  ?  UC_SWITCHING_MUTE  :  UC_SWITCHING_UNMUTE;
    if (uc_channel_switching(p->uc, 0, x, 0, 0) != UC_RET_OK)
    {
        opbx_log(LOG_WARNING, "unicall_confmute(%d) failed on channel %d: %s\n", muted, p->channel, strerror(errno));
        return -1;
    }
    /*endif*/
    return 0;
}

static int isslavenative(struct unicall_pvt *p, struct unicall_pvt **out)
{
    int x;
    int useslavenative;
    struct unicall_pvt *slave = NULL;

    /* Start out optimistic */
    useslavenative = TRUE;
    /* Update conference state in a stateless fashion */
    for (x = 0;  x < 3;  x++)
    {
        /* Any three-way calling makes slave native mode *definitely* out
           of the question */
        if ((p->subs[x].fd > -1)  &&  p->subs[x].inthreeway)
            useslavenative = FALSE;
        /*endif*/
    }
    /*endfor*/
    /* If we don't have any 3-way calls, check to see if we have
       precisely one slave */
    if (useslavenative)
    {
        for (x = 0;  x < MAX_SLAVES;  x++)
        {
            if (p->slaves[x])
            {
                if (slave)
                {
                    /* Whoops already have a slave!  No 
                       slave native and stop right away */
                    slave = NULL;
                    useslavenative = FALSE;
                    break;
                }
                /*endif*/
                /* We have one slave so far */
                slave = p->slaves[x];
            }
            /*endif*/
        }
        /*endfor*/
    }
    /*endif*/
    /* If no slave, slave native definitely out */
    if (slave == NULL)
    {
        useslavenative = FALSE;
    }
    else if (slave->law != p->law)
    {
        useslavenative = FALSE;
        slave = NULL;
    }
    /*endif*/
    if (out)
        *out = slave;
    /*endif*/
    return useslavenative;
}

static int reset_conf(struct unicall_pvt *p)
{
    p->confno = -1;
    if (p->subs[SUB_REAL].fd > -1)
    {
        if (uc_channel_switching(p->uc, 0, UC_SWITCHING_FREE, 0, 0) != UC_RET_OK)
            opbx_log(LOG_WARNING, "Failed to reset conferencing on channel %d!\n", p->channel);
        /*endif*/
    }
    /*endif*/
    return 0;
}

static int update_conf(struct unicall_pvt *p)
{
    int needconf;
    int x;
    int useslavenative;
    struct unicall_pvt *slave;

    needconf = 0;
    slave = NULL;
    useslavenative = isslavenative(p, &slave);
    /* Start with the obvious, general stuff */
    for (x = 0;  x < 3;  x++)
    {
        if (p->subs[x].fd >= 0  &&  p->subs[x].inthreeway)
        {
            conf_add(p, &p->subs[x], x, 0);
            needconf++;
        }
        else
        {
            conf_del(p, &p->subs[x], x);
        }
        /*endif*/
    }
    /*endfor*/
    /* If we have a slave, add him to our conference now, or DAX
       if this is slave native */
    for (x = 0;  x < MAX_SLAVES;  x++)
    {
        if (p->slaves[x])
        {
            if (useslavenative)
            {
                conf_add(p, &p->slaves[x]->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(p));
            }
            else
            {
                conf_add(p, &p->slaves[x]->subs[SUB_REAL], SUB_REAL, 0);
                needconf++;
            }
            /*endif*/
        }
        /*endif*/
    }
    /*endfor*/
    /* If we're supposed to be in there, do so now */
    if (p->inconference  &&  !p->subs[SUB_REAL].inthreeway)
    {
        if (useslavenative)
        {
            conf_add(p, &p->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(slave));
        }
        else
        {
            conf_add(p, &p->subs[SUB_REAL], SUB_REAL, 0);
            needconf++;
        }
        /*endif*/
    }
    /*endif*/
    /* If we have a master, add ourselves to this conference */
    if (p->master)
    {
        if (isslavenative(p->master, NULL))
            conf_add(p->master, &p->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(p->master));
        else
            conf_add(p->master, &p->subs[SUB_REAL], SUB_REAL, 0);
        /*endif*/
    }
    /*endif*/
    if (needconf == 0)
    {
        /* Nobody is left (or should be left) in our conference. Kill it. */
        p->confno = -1;
    }
    /*endif*/
    opbx_log(LOG_DEBUG, "Updated conferencing on %d, with %d conference users\n", p->channel, needconf);
    return 0;
}

static void unicall_enable_ec(struct unicall_pvt *p)
{
    if (p == NULL)
        return;
    /*endif*/
    if (p->echocanon)
    {
        opbx_log(LOG_DEBUG, "Echo cancellation already on\n");
        return;
    }
    /*endif*/
    if (p->digital)
    {
        /* TODO: This is a lie :-) */
        opbx_log(LOG_DEBUG, "Echo cancellation isn't required on digital connection\n");
        return;
    }
    /*endif*/
    if (p->echocancel)
    {
        if (uc_channel_echo_cancel(p->uc, 0, UC_ECHO_CANCEL_ON) != UC_RET_OK)
        {
            opbx_log(LOG_WARNING, "Unable to enable echo cancellation on channel %d\n", p->channel);
        }
        else
        {
            p->echocanon = TRUE;
            opbx_log(LOG_DEBUG, "Enabled echo cancellation on channel %d\n", p->channel);
        }
        /*endif*/
    }
    else
    {
        opbx_log(LOG_DEBUG, "No echo cancellation requested\n");
    }
    /*endif*/
}

static void unicall_train_ec(struct unicall_pvt *p)
{
    int x;

    if (p  &&  p->echocancel  &&  p->echotraining)
    {
        x = (p->echotraining)  ?  UC_ECHO_CANCEL_TRAINING  :  UC_ECHO_CANCEL_NOTRAINING;
        if (uc_channel_echo_cancel(p->uc, 0, x) != UC_RET_OK)
            opbx_log(LOG_WARNING, "Unable to request echo training on channel %d\n", p->channel);
        else
            opbx_log(LOG_DEBUG, "Engaged echo training on channel %d\n", p->channel);
        /*endif*/
    }
    else
    {
        opbx_log(LOG_DEBUG, "No echo training requested\n");
    }
    /*endif*/
}

static void unicall_disable_ec(struct unicall_pvt *p)
{
    if (p == NULL)
        return;
    /*endif*/
    if (p->echocancel)
    {
        if (uc_channel_echo_cancel(p->uc, 0, UC_ECHO_CANCEL_OFF) != UC_RET_OK)
            opbx_log(LOG_WARNING, "Unable to disable echo cancellation on channel %d\n", p->channel);
        else
            opbx_log(LOG_DEBUG, "disabled echo cancellation on channel %d\n", p->channel);
        /*endif*/
    }
    /*endif*/
    p->echocanon = FALSE;
}

static int unicall_call(struct opbx_channel *ast, char *rdest, int timeout)
{
    struct unicall_pvt *p = ast->tech_pvt;
    uc_callparms_t *callparms;
    char callerid[256];
    char dest[256];
    char *s;
    char *c;
    char *n;
    char *l;
    int ret;
    uc_makecall_t makecall;

    opbx_log(LOG_DEBUG, "unicall_call called - '%s'\n", rdest);
    if (ast->_state != OPBX_STATE_DOWN  &&  ast->_state != OPBX_STATE_RESERVED)
    {
        opbx_log(LOG_WARNING, "unicall_call called on %s, neither down nor reserved\n", ast->name);
        return -1;
    }
    /*endif*/

    if (p->radio)
    {
        /* If a radio channel, up immediately */
        /* Special pseudo -- automatically up */
        opbx_setstate(ast, OPBX_STATE_UP); 
        return 0;
    }
    /*endif*/
    if ((callparms = uc_new_callparms(NULL)) == NULL)
        return -1;
    /*endif*/

    strncpy(dest, rdest, sizeof(dest) - 1);
    if ((c = strchr(rdest, '/')))
        c++;
    else
        c = dest;
    /*endif*/
    if (ast->cid.cid_num)
    {
        strncpy(callerid, ast->cid.cid_num, sizeof(callerid) - 1);
        opbx_log(LOG_DEBUG, "unicall_call caller id - '%s'\n", callerid);
        opbx_callerid_parse(callerid, &n, &l);
        if (l)
        {
            opbx_shrink_phone_number(l);
            if (!opbx_isphonenumber(l))
                l = NULL;
            /*endif*/
        }
        /*endif*/
    }
    else
    {
        l = NULL;
    }
    /*endif*/
    if (strlen(c) < p->stripmsd)
    {
        opbx_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
        return -1;
    }
    /*endif*/
    if ((s = strchr(c + p->stripmsd, 'w')))
    {
        strncpy(p->dialstr, s, sizeof(p->dialstr));
        *s = '\0';
    }
    else
    {
        p->dialstr[0] = '\0';
    }
    /*endif*/
#if 0
    uc_callparm_bear_cap_transfer_cap(callparms, cap);
    uc_callparm_bear_cap_transfer_mode(callparms, mode);
    uc_callparm_bear_cap_transfer_rate(callparms, rate);
    uc_callparm_userinfo_layer1_protocol(callparms, prot);
    uc_callparm_user_rate(callparms, rate);
    
    uc_callparm_destination_npi(callparms, npi);
    uc_callparm_destination_ton(callparms, ton);
    uc_callparm_destination_sub_addr_npi(callparms, npi);
    uc_callparm_destination_sub_addr_ton(callparms, ton);
    uc_callparm_destination_sub_addr_number(callparms, num);

    uc_callparm_originating_presentation(callparms, pres);
    uc_callparm_originating_ton(callparms, ton);
    uc_callparm_originating_npi(callparms, npi);
    uc_callparm_originating_sub_addr_ton(callparms, ton);
    uc_callparm_originating_sub_addr_npi(callparms, npi);
    uc_callparm_originating_sub_addr_number(callparms, num);

    uc_callparm_redirecting_cause(callparms, cause);
    uc_callparm_redirecting_presentation(callparms, pres);
    uc_callparm_redirecting_ton(callparms, ton);
    uc_callparm_redirecting_npi(callparms, npi);
    uc_callparm_redirecting_number(callparms, num);
    uc_callparm_redirecting_subaddr_ton(callparms, ton);
    uc_callparm_redirecting_subaddr_npi(callparms, npi);
    uc_callparm_redirecting_subaddr(callparms, num);

    uc_callparm_original_called_number_ton(callparms, ton);
    uc_callparm_original_called_number_npi(callparms, npi);
    uc_callparm_original_called_number(callparms, num);
#endif
    uc_callparm_calling_party_category(callparms, UC_CALLER_CATEGORY_NATIONAL_SUBSCRIBER_CALL);
    uc_callparm_originating_number(callparms, l);
    uc_callparm_destination_number(callparms, c + p->stripmsd);
    
    makecall.callparms = callparms;
    makecall.crn = 0;
    if ((ret = uc_call_control(p->uc, UC_OP_MAKECALL, 0, &makecall)) != UC_RET_OK)
    {
        opbx_log(LOG_WARNING, "Make call failed - %s\n", uc_ret2str(ret));
        return -1;
    }
    /*endif*/
    p->crn = makecall.crn;
    opbx_setstate(ast, OPBX_STATE_DIALING);
    return 0;
}

static int destroy_channel(struct unicall_pvt *prev, struct unicall_pvt *cur, int now)
{
    int owned;
    int i;
    int res;

    owned = FALSE;
    i = 0;
    res = 0;
    if (!now)
    {
        if (cur->owner)
            owned = TRUE;
        /*endif*/
        for (i = 0;  i < 3;  i++)
        {
            if (cur->subs[i].owner)
                owned = TRUE;
            /*endif*/
        }
        /*endfor*/
        if (!owned)
        {
			if (prev)
            {
				prev->next = cur->next;
				if (prev->next)
					prev->next->prev = prev;
				else
					ifend = prev;
                /*endif*/
			}
            else
            {
				iflist = cur->next;
				if (iflist)
					iflist->prev = NULL;
				else
					ifend = NULL;
                /*endif*/
			}
            /*endif*/
            if ((res = unicall_close(cur->subs[SUB_REAL].fd)))
            {
                opbx_log(LOG_ERROR, "Unable to close device on channel %d\n", cur->channel);
                free(cur);
                return -1;
            }
            /*endif*/
            free(cur);
        }
        /*endif*/
    }
    else
    {
		if (prev)
        {
			prev->next = cur->next;
			if (prev->next)
				prev->next->prev = prev;
			else
				ifend = prev;
            /*endif*/
		}
        else
        {
			iflist = cur->next;
			if (iflist)
				iflist->prev = NULL;
			else
				ifend = NULL;
            /*endif*/
		}
        /*endif*/
        if ((res = unicall_close(cur->subs[SUB_REAL].fd)))
        {
            opbx_log(LOG_ERROR, "Unable to close device on channel %d\n", cur->channel);
            free(cur);
            return -1;
        }
        /*endif*/
        free(cur);
    }
    /*endif*/
    return 0;
}

static int restore_gains(struct unicall_pvt *p)
{
    if (uc_channel_gains(p->uc, 0, p->rxgain, p->txgain))
    {
        opbx_log(LOG_WARNING, "Unable to restore gains: %s\n", strerror(errno));
        return -1;
    }
    /*endif*/
    return 0;
}

static int unicall_hangup(struct opbx_channel *ast)
{
    struct unicall_pvt *p = ast->tech_pvt;
    struct unicall_pvt *tmp;
    struct unicall_pvt *prev;
    int res;
    int ret;
    int index;
    int x;

    if (option_debug)
        opbx_log(LOG_DEBUG, "unicall_hangup(%s)\n", ast->name);
    /*endif*/
    if (p == NULL)
    {
        opbx_log(LOG_WARNING, "Asked to hangup channel not connected\n");
        return 0;
    }
    /*endif*/
    index = unicall_get_index(ast, p, 1);

    restore_gains(p);
    if (p->dsp)
        opbx_dsp_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);
    /*endif*/
    x = 0;
    unicall_confmute(p, 0);

    opbx_log(LOG_DEBUG,
            "Hangup: channel: %d index = %d, normal = %d, callwait = %d, thirdcall = %d\n",
            p->channel,
            index,
            p->subs[SUB_REAL].fd,
            p->subs[SUB_CALLWAIT].fd,
            p->subs[SUB_THREEWAY].fd);
    p->ignoredtmf = FALSE;
    
    if (index >= 0)
    {
        /* Real channel. Do some fixup */
        p->subs[index].owner = NULL;
        p->subs[index].needanswer = FALSE;
        p->subs[index].needbusy = FALSE;
        p->subs[index].needcongestion = FALSE;
        p->subs[index].needringing = FALSE;
        p->subs[index].current_codec = 0;
        if (uc_channel_set_api_codec(p->uc, 0, UC_CODEC_DEFAULT))
            opbx_log(LOG_WARNING, "Unable to set law on channel %d to default\n", p->channel);
        /*endif*/
        switch (index)
        {
        case SUB_REAL:
            if (p->subs[SUB_CALLWAIT].fd >= 0  &&  p->subs[SUB_THREEWAY].fd >= 0)
            {
                opbx_log(LOG_DEBUG, "Normal call hung up with both three way call and a call waiting call in place?\n");
                if (p->subs[SUB_CALLWAIT].inthreeway)
                {
                    /* We had flipped over to answer a callwait and now it's gone */
                    opbx_log(LOG_DEBUG, "We were flipped over to the callwait, moving back and unowning.\n");
                    /* Move to the call-wait, but un-own us until they flip back. */
                    swap_subs(p, SUB_CALLWAIT, SUB_REAL);
                    unalloc_sub(p, SUB_CALLWAIT);
                    p->owner = NULL;
                }
                else
                {
                    /* The three way hung up, but we still have a call wait */
                    opbx_log(LOG_DEBUG, "We were in the threeway and have a callwait still.  Ditching the threeway.\n");
                    swap_subs(p, SUB_THREEWAY, SUB_REAL);
                    unalloc_sub(p, SUB_THREEWAY);
                    if (p->subs[SUB_REAL].inthreeway)
                    {
                        /* This was part of a three way call.  Immediately make way for
                           another call */
                        opbx_log(LOG_DEBUG, "Call was complete, setting owner to former third call\n");
                        p->owner = p->subs[SUB_REAL].owner;
                    }
                    else
                    {
                        /* This call hasn't been completed yet...  Set owner to NULL */
                        opbx_log(LOG_DEBUG, "Call was incomplete, setting owner to NULL\n");
                        p->owner = NULL;
                    }
                    /*endif*/
                    p->subs[SUB_REAL].inthreeway = FALSE;
                }
                /*endif*/
            }
            else if (p->subs[SUB_CALLWAIT].fd >= 0)
            {
                /* Move to the call-wait and switch back to them. */
                swap_subs(p, SUB_CALLWAIT, SUB_REAL);
                unalloc_sub(p, SUB_CALLWAIT);
                p->owner = p->subs[SUB_REAL].owner;
                if (opbx_bridged_channel(p->subs[SUB_REAL].owner))
                    opbx_moh_stop(opbx_bridged_channel(p->subs[SUB_REAL].owner));
                /*endif*/
            }
            else if (p->subs[SUB_THREEWAY].fd >= 0)
            {
                swap_subs(p, SUB_THREEWAY, SUB_REAL);
                unalloc_sub(p, SUB_THREEWAY);
                if (p->subs[SUB_REAL].inthreeway)
                {
                    /* This was part of a three way call.  Immediately make way for
                       another call */
                    opbx_log(LOG_DEBUG, "Call was complete, setting owner to former third call\n");
                    p->owner = p->subs[SUB_REAL].owner;
                }
                else
                {
                    /* This call hasn't been completed yet...  Set owner to NULL */
                    opbx_log(LOG_DEBUG, "Call was incomplete, setting owner to NULL\n");
                    p->owner = NULL;
                }
                /*endif*/
                p->subs[SUB_REAL].inthreeway = FALSE;
            }
            /*endif*/
            break;
        case SUB_CALLWAIT:
            /* Ditch the holding callwait call, and immediately make it availabe */
            if (p->subs[SUB_CALLWAIT].inthreeway)
            {
                /* This is actually part of a three way, placed on hold.  Place the third part
                   on music on hold now */
                if (p->subs[SUB_THREEWAY].owner  &&  opbx_bridged_channel(p->subs[SUB_THREEWAY].owner))
                    opbx_moh_start(opbx_bridged_channel(p->subs[SUB_THREEWAY].owner), NULL);
                /*endif*/
                p->subs[SUB_THREEWAY].inthreeway = FALSE;
                /* Make it the call wait now */
                swap_subs(p, SUB_CALLWAIT, SUB_THREEWAY);
                unalloc_sub(p, SUB_THREEWAY);
            }
            else
            {
                unalloc_sub(p, SUB_CALLWAIT);
            }
            /*endif*/
            break;
        case SUB_THREEWAY:
            if (p->subs[SUB_CALLWAIT].inthreeway)
            {
                /* The other party of the three way call is currently in a call-wait state.
                   Start music on hold for them, and take the main guy out of the third call */
                if (p->subs[SUB_CALLWAIT].owner && opbx_bridged_channel(p->subs[SUB_CALLWAIT].owner))
                    opbx_moh_start(opbx_bridged_channel(p->subs[SUB_CALLWAIT].owner), NULL);
                /*endif*/
                p->subs[SUB_CALLWAIT].inthreeway = FALSE;
            }
            /*endif*/
            p->subs[SUB_REAL].inthreeway = FALSE;
            /* If this was part of a three way call index, let us make
               another three way call */
            unalloc_sub(p, SUB_THREEWAY);
            break;
        default:
            /* This wasn't any sort of call, but how are we an index? */
            opbx_log(LOG_WARNING, "Index found but not any type of call?\n");
            break;
        }
        /*endswitch*/
    }
    /*endif*/

    if (p->subs[SUB_REAL].owner == NULL  &&  p->subs[SUB_CALLWAIT].owner == NULL  &&  p->subs[SUB_THREEWAY].owner == NULL)
    {
        p->owner = NULL;
        p->distinctivering = 0;
        p->confirmanswer = 0;
        p->outgoing = 0;
        p->digital = 0;
        p->faxhandled = 0;
        p->onhooktime = time(NULL);
        if (p->dsp)
        {
            opbx_dsp_free(p->dsp);
            p->dsp = NULL;
        }
        /*endif*/
        if (uc_channel_set_api_codec(p->uc, 0, UC_CODEC_DEFAULT))
            opbx_log(LOG_WARNING, "Unable to set law on channel %d to default\n", p->channel);
        /*endif*/
        /* Perform low level drop call if no owner left */
        if (p->crn)
        {
            if ((ret = uc_call_control(p->uc, UC_OP_DROPCALL, p->crn, (void *) (intptr_t) hangup_cause2uc(ast->hangupcause))) < 0)
                opbx_log(LOG_WARNING, "Drop call failed - %s\n", uc_ret2str(ret));
            /*endif*/
            if (p->alreadyhungup)
                p->alreadyhungup = FALSE;
            /*endif*/
        }
        else
        {
            res = 0;
        }
        /*endif*/
        super_tone(&p->subs[SUB_REAL], ST_TYPE_NONE);
        x = 0;
        //opbx_channel_setoption(ast, OPBX_OPTION_TONE_VERIFY, &x, sizeof(char), 0);
        //opbx_channel_setoption(ast, OPBX_OPTION_TDD, &x, sizeof(char), 0);
        p->dialing = FALSE;
        p->rdnis[0] = '\0';
        update_conf(p);
        restart_monitor();
    }
    /*endif*/
    ast->tech_pvt = NULL;
    opbx_setstate(ast, OPBX_STATE_DOWN);
    opbx_mutex_lock(&usecnt_lock);
    if (--usecnt < 0) 
        opbx_log(LOG_WARNING, "Usecnt < 0???\n");
    /*endif*/
    opbx_mutex_unlock(&usecnt_lock);
    opbx_update_use_count();
    if (option_verbose > 2) 
        opbx_verbose( VERBOSE_PREFIX_3 "Hungup '%s'\n", ast->name);
    /*endif*/
    opbx_mutex_lock(&iflock);
    if (p->destroy)
    {
        for (prev = NULL, tmp = iflist;  iflist;  prev = tmp, tmp = tmp->next)
        {
            if (tmp == p)
            {
                destroy_channel(prev, tmp, 0);
                break;
            }
            /*endif*/
        }
        /*endfor*/
    }
    /*endif*/
    opbx_mutex_unlock(&iflock);
    return 0;
}

static int unicall_answer(struct opbx_channel *ast)
{
    struct unicall_pvt *p;
    int index;
    int ret;

    p = ast->tech_pvt;
    opbx_setstate(ast, OPBX_STATE_UP);
    if ((index = unicall_get_index(ast, p, 0)) < 0)
        index = SUB_REAL;
    /*endif*/
    /* Nothing to do if a radio channel */
    if (p->radio)
        return 0;
    /*endif*/
    if (p->crn)
    {
        opbx_log(LOG_WARNING, "Answer Call\n");
        if ((ret = uc_call_control(p->uc, UC_OP_ANSWERCALL, p->crn, NULL)))
            opbx_log(LOG_WARNING, "Answer call failed on %s - %s\n", ast->name, uc_ret2str(ret));
        /*endif*/
    }
    else
    {
        opbx_log(LOG_WARNING, "Trying to answer a non-existant call\n");
        ret = -1;
    }
    return ret;
}

static int unicall_setoption(struct opbx_channel *chan, int option, void *data, int datalen)
{
    struct unicall_pvt *p = chan->tech_pvt;
    char *cp;
    int x;

    opbx_log(LOG_WARNING, "unicall_setoption called - %d\n", option);
    if (option != OPBX_OPTION_TONE_VERIFY
        &&
        option != OPBX_OPTION_TDD
        &&
        option != OPBX_OPTION_RELAXDTMF)
    {
        errno = ENOSYS;
        return -1;
    }
    /*endif*/
    if (data == NULL  ||  datalen < 1)
    {
        errno = EINVAL;
        return -1;
    }
    /*endif*/
    cp = (char *) data;
    switch (option)
    {
    case OPBX_OPTION_TONE_VERIFY:
        switch (*cp)
        {
        case 1:
            opbx_log(LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF(1) on %s\n", chan->name);
            opbx_dsp_digitmode(p->dsp, DSP_DIGITMODE_MUTECONF | p->dtmfrelax);  /* set mute mode if desired */
            break;
        case 2:
            opbx_log(LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n", chan->name);
            opbx_dsp_digitmode(p->dsp, DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX | p->dtmfrelax);  /* set mute mode if desired */
            break;
        default:
            opbx_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n", chan->name);
            opbx_dsp_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);  /* set mute mode if desired */
            break;
        }
        /*endswitch*/
        break;
    case OPBX_OPTION_RELAXDTMF:
        /* Relax DTMF decoding (or not) */
        switch (*cp)
        {
        case 0:
            opbx_log(LOG_DEBUG, "Set option RELAX DTMF, value: OFF(0) on %s\n",chan->name);
            x = FALSE;
            break;
        default:
            opbx_log(LOG_DEBUG, "Set option RELAX DTMF, value: ON(1) on %s\n",chan->name);
            x = TRUE;
            break;
        }
        /*endswitch*/
        opbx_dsp_digitmode(p->dsp, (x)  ?  DSP_DIGITMODE_RELAXDTMF  :  (DSP_DIGITMODE_DTMF | p->dtmfrelax));
        break;
    }
    /*endswitch*/
    errno = 0;
    return 0;
}

static void unicall_unlink(struct unicall_pvt *slave, struct unicall_pvt *master)
{
    /* Unlink a specific slave or all slaves/masters from a given master */
    int x;
    int hasslaves;

    if (master == NULL)
        return;
    /*endif*/
    hasslaves = FALSE;
    for (x = 0;  x < MAX_SLAVES;  x++)
    {
        if (master->slaves[x])
        {
            if (slave == NULL  ||  master->slaves[x] == slave)
            {
                /* Take slave out of the conference */
                opbx_log(LOG_DEBUG, "Unlinking slave %d from %d\n", master->slaves[x]->channel, master->channel);
                conf_del(master, &master->slaves[x]->subs[SUB_REAL], SUB_REAL);
                conf_del(master->slaves[x], &master->subs[SUB_REAL], SUB_REAL);
                master->slaves[x]->master = NULL;
                master->slaves[x] = NULL;
            }
            else
            {
                hasslaves = TRUE;
            }
            /*endif*/
        }
        /*endif*/
        if (!hasslaves)
            master->inconference = FALSE;
        /*endif*/
    }
    /*endfor*/
    if (slave == NULL)
    {
        if (master->master)
        {
            /* Take master out of the conference */
            conf_del(master->master, &master->subs[SUB_REAL], SUB_REAL);
            conf_del(master, &master->master->subs[SUB_REAL], SUB_REAL);
            hasslaves = FALSE;
            for (x = 0;  x < MAX_SLAVES;  x++)
            {
                if (master->master->slaves[x] == master)
                    master->master->slaves[x] = NULL;
                else if (master->master->slaves[x])
                    hasslaves = TRUE;
                /*endif*/
            }
            /*endfor*/
            if (!hasslaves)
                master->master->inconference = FALSE;
            /*endif*/
        }
        /*endif*/
        master->master = NULL;
    }
    /*endif*/
    update_conf(master);
}

static void unicall_link(struct unicall_pvt *slave, struct unicall_pvt *master)
{
    int x;

    if (slave == NULL  ||  master == NULL)
    {
        opbx_log(LOG_WARNING, "Tried to link to/from NULL??\n");
        return;
    }
    /*endif*/
    for (x = 0;  x < MAX_SLAVES;  x++)
    {
        if (master->slaves[x] == NULL)
        {
            master->slaves[x] = slave;
            break;
        }
        /*endif*/
    }
    /*endfor*/
    if (x >= MAX_SLAVES)
    {
        opbx_log(LOG_WARNING, "Replacing slave %d with new slave, %d\n", master->slaves[MAX_SLAVES - 1]->channel, slave->channel);
        master->slaves[MAX_SLAVES - 1] = slave;
    }
    /*endif*/
    if (slave->master) 
        opbx_log(LOG_WARNING, "Replacing master %d with new master, %d\n", slave->master->channel, master->channel);
    /*endif*/
    slave->master = master;
    
    opbx_log(LOG_DEBUG, "Making %d slave to master %d\n", slave->channel, master->channel);
}

static int unicall_bridge(struct opbx_channel *c0, struct opbx_channel *c1, int flags, struct opbx_frame **fo, struct opbx_channel **rc)
{
    struct unicall_pvt *p0;
    struct unicall_pvt *p1;
    struct unicall_pvt *op0;
    struct unicall_pvt *op1;
    struct unicall_pvt *master = NULL;
    struct unicall_pvt *slave = NULL;
    struct opbx_channel *who = NULL;
    struct opbx_channel *cs[3];
    struct opbx_frame *f;
    int to;
    int inconf;
    int nothingok;
    int ofd1;
    int ofd2;
    int oi1;
    int oi2;
    int i1;
    int i2;
    int t1;
    int t2;
    int os1;
    int os2;
    struct opbx_channel *oc1;
    struct opbx_channel *oc2;

    i1 = -1;
    i2 = -1;
    os1 = -1;
    os2 = -1;
    inconf = FALSE;
    nothingok = FALSE;
    opbx_log(LOG_WARNING, "unicall_bridge called\n");
    /* If need DTMF, can't native bridge */
    if ((flags & (OPBX_BRIDGE_DTMF_CHANNEL_0 | OPBX_BRIDGE_DTMF_CHANNEL_1)))
        return -2;
    /*endif*/
    p0 = c0->tech_pvt;
    p1 = c1->tech_pvt;
    /* Can't do pseudo-channels here */
    if (p0->protocol_class == NULL  ||  p1->protocol_class == NULL)
        return -2;
    /*endif*/
    opbx_mutex_lock(&c0->lock);
    opbx_mutex_lock(&c1->lock);
    op0 =
    p0 = c0->tech_pvt;
    op1 =
    p1 = c1->tech_pvt;
    ofd1 = c0->fds[0];
    ofd2 = c1->fds[0];
    oi1 = unicall_get_index(c0, p0, 0);
    oi2 = unicall_get_index(c1, p1, 0);
    oc1 = p0->owner;
    oc2 = p1->owner;
    if (oi1 < 0  ||  oi2 < 0)
        return -1;
    /*endif*/
    opbx_mutex_lock(&p0->lock);
    if (opbx_mutex_trylock(&p1->lock))
    {
        /* Don't block, due to potential for deadlock */
        opbx_mutex_unlock(&p0->lock);
        opbx_mutex_unlock(&c0->lock);
        opbx_mutex_unlock(&c1->lock);
        opbx_log(LOG_NOTICE, "Avoiding deadlock...\n");
        return -3;
    }
    /*endif*/
    if (oi1 == SUB_REAL  &&  oi2 == SUB_REAL)
    {
        if (p0->owner == NULL  ||  p1->owner == NULL)
        {
            /* Currently unowned -- Do nothing. */
            nothingok = TRUE;
        }
        else
        {
            /* If we don't have a call-wait in a 3-way, and we aren't in a 3-way, we can be master */
            if (!p0->subs[SUB_CALLWAIT].inthreeway  &&  !p1->subs[SUB_REAL].inthreeway)
            {
                master = p0;
                slave = p1;
                inconf = TRUE;
            }
            else if (!p1->subs[SUB_CALLWAIT].inthreeway  &&  !p0->subs[SUB_REAL].inthreeway)
            {
                master = p1;
                slave = p0;
                inconf = TRUE;
            }
            else
            {
                opbx_log(LOG_WARNING, "Huh?  Both calls are callwaits or 3-ways?  That's clever...?\n");
                opbx_log(LOG_WARNING,
                        "p0: chan %d/%d/CW%d/3W%d, p1: chan %d/%d/CW%d/3W%d\n",
                        p0->channel,
                        oi1,
                        (p0->subs[SUB_CALLWAIT].fd >= 0)  ?  1  :  0,
                        p0->subs[SUB_REAL].inthreeway,
                        p0->channel,
                        oi1,
                        (p1->subs[SUB_CALLWAIT].fd >= 0)  ?  1  :  0,
                        p1->subs[SUB_REAL].inthreeway);
            }
            /*endif*/
        }
        /*endif*/
    }
    else if (oi1 == SUB_REAL  &&  oi2 == SUB_THREEWAY)
    {
        if (p1->subs[SUB_THREEWAY].inthreeway)
        {
            master = p1;
            slave = p0;
        }
        else
        {
            nothingok = TRUE;
        }
        /*endif*/
    }
    else if (oi1 == SUB_THREEWAY  &&  oi2 == SUB_REAL)
    {
        if (p0->subs[SUB_THREEWAY].inthreeway)
        {
            master = p0;
            slave = p1;
        }
        else
        {
            nothingok = TRUE;
        }
        /*endif*/
    }
    else if (oi1 == SUB_REAL  &&  oi2 == SUB_CALLWAIT)
    {
        /* We have a real and a call wait.  If we're in a three way call, put us in it, otherwise, 
           don't put us in anything */
        if (p1->subs[SUB_CALLWAIT].inthreeway)
        {
            master = p1;
            slave = p0;
        }
        else
        {
            nothingok = TRUE;
        }
        /*endif*/
    }
    else if (oi1 == SUB_CALLWAIT  &&  oi2 == SUB_REAL)
    {
        /* Same as previous */
        if (p0->subs[SUB_CALLWAIT].inthreeway)
        {
            master = p0;
            slave = p1;
        }
        else
        {
            nothingok = TRUE;
        }
        /*endif*/
    }
    /*endif*/
    if (master  &&  slave)
    {
        /* Stop any tones, or play ringtone as appropriate.  If they're bridged
           in an active three way call with a channel that is ringing, we should
           indicate ringing. */
        if (oi2 == SUB_THREEWAY
            && 
            p1->subs[SUB_THREEWAY].inthreeway
            && 
            p1->subs[SUB_REAL].owner
            && 
            p1->subs[SUB_REAL].inthreeway
            &&
            p1->subs[SUB_REAL].owner->_state == OPBX_STATE_RINGING)
        {
            opbx_log(LOG_DEBUG, "Playing ringback on %s since %s is in a ringing three-way\n", c0->name, c1->name);
            super_tone(&p0->subs[oi1], ST_TYPE_RINGBACK);
            os2 = p1->subs[SUB_REAL].owner->_state;
        }
        else
        {
            opbx_log(LOG_DEBUG, "Stoping tones on %d/%d talking to %d/%d\n", p0->channel, oi1, p1->channel, oi2);
            super_tone(&p0->subs[oi1], ST_TYPE_NONE);
        }
        /*endif*/
        if (oi1 == SUB_THREEWAY
            && 
            p0->subs[SUB_THREEWAY].inthreeway
            && 
            p0->subs[SUB_REAL].owner
            && 
            p0->subs[SUB_REAL].inthreeway
            && 
            p0->subs[SUB_REAL].owner->_state == OPBX_STATE_RINGING)
        {
            opbx_log(LOG_DEBUG, "Playing ringback on %s since %s is in a ringing three-way\n", c1->name, c0->name);
            super_tone(&p1->subs[oi2], ST_TYPE_RINGBACK);
            os1 = p0->subs[SUB_REAL].owner->_state;
        }
        else
        {
            opbx_log(LOG_DEBUG, "Stoping tones on %d/%d talking to %d/%d\n", p1->channel, oi2, p0->channel, oi1);
            super_tone(&p1->subs[oi1], ST_TYPE_NONE);
        }
        /*endif*/
        unicall_link(slave, master);
        master->inconference = inconf;
    }
    else if (!nothingok)
    {
        opbx_log(LOG_WARNING, "Can't link %d/%s with %d/%s\n", p0->channel, subnames[oi1], p1->channel, subnames[oi2]);
    }
    /*endif*/

    update_conf(p0);
    update_conf(p1);
    t1 = p0->subs[SUB_REAL].inthreeway;
    t2 = p1->subs[SUB_REAL].inthreeway;

    opbx_mutex_unlock(&p0->lock);
    opbx_mutex_unlock(&p1->lock);

    opbx_mutex_unlock(&c0->lock);
    opbx_mutex_unlock(&c1->lock);

    /* Native bridge failed */
    if ((master == NULL  ||  slave == NULL)  &&  !nothingok)
        return -1;
    /*endif*/
    cs[SUB_REAL] = c0;
    cs[SUB_CALLWAIT] = c1;
    cs[SUB_THREEWAY] = NULL;
    for (;;)
    {
        /* Here's our main loop...  Start by locking things, looking for private parts, 
           and then balking if anything is wrong */
        opbx_mutex_lock(&c0->lock);
        opbx_mutex_lock(&c1->lock);
        p0 = c0->tech_pvt;
        p1 = c1->tech_pvt;
        if (op0 == p0)
            i1 = unicall_get_index(c0, p0, 1);
        /*endif*/
        if (op1 == p1)
            i2 = unicall_get_index(c1, p1, 1);
        /*endif*/
        opbx_mutex_unlock(&c0->lock);
        opbx_mutex_unlock(&c1->lock);
        if (op0 != p0
            ||
            op1 != p1
            ||
            ofd1 != c0->fds[0]
            ||
            ofd2 != c1->fds[0]
            ||
            (p0->subs[SUB_REAL].owner  &&  (os1 > -1)  &&  (os1 != p0->subs[SUB_REAL].owner->_state))
            ||
            (p1->subs[SUB_REAL].owner  &&  (os2 > -1)  &&  (os2 != p1->subs[SUB_REAL].owner->_state))
            ||
            oc1 != p0->owner
            || 
            oc2 != p1->owner
            ||
            t1 != p0->subs[SUB_REAL].inthreeway
            ||
            t2 != p1->subs[SUB_REAL].inthreeway
            ||
            oi1 != i1
            ||
            oi2 != i2)
        {
            if (slave  &&  master)
                unicall_unlink(slave, master);
            /*endif*/
            opbx_log(LOG_DEBUG,
                    "Something changed out on %d/%d to %d/%d, returning -3 to restart\n",
                    op0->channel,
                    oi1,
                    op1->channel,
                    oi2);
            return -3;
        }
        /*endif*/
        to = -1;
        if ((who = opbx_waitfor_n(cs, 2, &to)) == NULL)
        {
            opbx_log(LOG_DEBUG, "Ooh, empty read...\n");
            continue;
        }
        /*endif*/
        if (who->tech_pvt == op0) 
            op0->ignoredtmf = TRUE;
        else if (who->tech_pvt == op1)
            op1->ignoredtmf = TRUE;
        /*endif*/
        f = opbx_read(who);
        if (who->tech_pvt == op0) 
            op0->ignoredtmf = FALSE;
        else if (who->tech_pvt == op1)
            op1->ignoredtmf = FALSE;
        /*endif*/
        if (f == NULL)
        {
            *fo = NULL;
            *rc = who;
            if (slave  &&  master)
                unicall_unlink(slave, master);
            /*endif*/
            return 0;
        }
        /*endif*/
        if (f->frametype == OPBX_FRAME_DTMF)
        {
            if ((who == c0  &&  (flags & OPBX_BRIDGE_DTMF_CHANNEL_0))
                || 
                (who == c1  &&  (flags & OPBX_BRIDGE_DTMF_CHANNEL_1)))
            {
                *fo = f;
                *rc = who;
                if (slave  &&  master)
                    unicall_unlink(slave, master);
                /*endif*/
                return 0;
            }
            /*endif*/
        }
        /*endif*/
        opbx_fr_free(f);

        /* Swap who gets priority */
        cs[2] = cs[0];
        cs[0] = cs[1];
        cs[1] = cs[2];
    }
    /*endif*/
}

static int unicall_indicate(struct opbx_channel *chan, int condition);

static int unicall_fixup(struct opbx_channel *oldchan, struct opbx_channel *newchan)
{
    struct unicall_pvt *p;
    int x;

    p = newchan->tech_pvt;
    opbx_mutex_lock(&p->lock);
    opbx_log(LOG_WARNING, "New owner for channel %d is %s\n", p->channel, newchan->name);
    //opbx_log(LOG_DEBUG, "New owner for channel %d is %s\n", p->channel, newchan->name);
    if (p->owner == oldchan)
        p->owner = newchan;
    /*endif*/
    for (x = 0;  x < 3;  x++)
    {
        if (p->subs[x].owner == oldchan)
        {
            if (x == 0)
                unicall_unlink(NULL, p);
            /*endif*/
            p->subs[x].owner = newchan;
        }
        /*endif*/
    }
    /*endfor*/
    if (newchan->_state == OPBX_STATE_RINGING) 
        unicall_indicate(newchan, OPBX_CONTROL_RINGING);
    /*endif*/
    update_conf(p);
    opbx_mutex_unlock(&p->lock);
    return 0;
}

static struct opbx_channel *unicall_new(struct unicall_pvt *, int, int, int, int);

struct opbx_frame *unicall_exception(struct opbx_channel *ast)
{
    struct unicall_pvt *p;
    int index;

    p = ast->tech_pvt;
    if ((index = unicall_get_index(ast, p, 0)) < 0)
        return  NULL;
    /*endif*/
    
    opbx_fr_init(&p->subs[index].f);
    p->subs[index].f.src = "unicall_exception";
    
    if (p->owner == NULL  &&  !p->radio)
    {
        /* If nobody owns us, absorb the event appropriately, otherwise
           we loop indefinitely.  This occurs when, during call waiting, the
           other end hangs up our channel so that it no longer exists, but we
           have neither FLASH'd nor ONHOOK'd to signify our desire to
           change to the other channel. */
        uc_check_event(p->uc);
        uc_schedule_run(p->uc);
        /* TODO: process event properly */
        return &p->subs[index].f;
    }
    /*endif*/
    if (!p->radio)
        opbx_log(LOG_DEBUG, "Exception on %d, channel %d\n", ast->fds[0],p->channel);
    /*endif*/
    /* If it's not us, return NULL immediately */
    if (ast != p->owner)
    {
        opbx_log(LOG_WARNING, "We're %s, not %s\n", ast->name, p->owner->name);
        return &p->subs[index].f;
    }
    /*endif*/
    uc_check_event(p->uc);
    uc_schedule_run(p->uc);
    return &p->subs[index].f;
}

void handle_uc_read(uc_t *uc, int ch, void *user_data, uint8_t *buf, int len)
{
    void *readbuf;
    struct unicall_pvt *p;
    int gen_len;
    int16_t amp[len];
    
    p = (struct unicall_pvt *) user_data;

    if (p->t38_active)
    {
        /* TODO */
    }
    else
    {
        readbuf = ((uint8_t *) p->subs[SUB_REAL].buffer) + OPBX_FRIENDLY_OFFSET;
        memcpy(readbuf, buf, len);
        p->subs[SUB_REAL].buffer_contents = len;
    }
    if (p->subs[SUB_REAL].super_tone != ST_TYPE_NONE)
    {
        gen_len = super_tone_tx(&p->subs[SUB_REAL].tx_state, amp, len);
        select_codec(p, SUB_REAL, OPBX_FORMAT_SLINEAR);
        if (uc_channel_write(uc, ch, (uint8_t *) amp, gen_len*sizeof(int16_t)) < 0)
            opbx_log(LOG_WARNING, "write failed: %s - %d\n", strerror(errno), errno);
        /*endif*/
    }
    else if (p->dialing)
    {
        gen_len = dtmf_tx(&p->subs[SUB_REAL].dtmf_tx_state, amp, len);
        select_codec(p, SUB_REAL, OPBX_FORMAT_SLINEAR);
        if (uc_channel_write(uc, ch, (uint8_t *) amp, gen_len*sizeof(int16_t)) < 0)
            opbx_log(LOG_WARNING, "write failed: %s - %d\n", strerror(errno), errno);
        /*endif*/
        if (gen_len < len)
            p->dialing = FALSE;
        /*endif*/
    }
    /*endif*/
}

struct opbx_frame *unicall_read(struct opbx_channel *ast)
{
    struct unicall_pvt *p;
    int index;
    struct opbx_frame *f;
    char tmpfax[256];

    p = ast->tech_pvt;
    //opbx_log(LOG_WARNING, "unicall_read\n");
    opbx_mutex_lock(&p->lock);
    
    index = unicall_get_index(ast, p, 0);
    
    /* Hang up if we don't really exist */
    if (index < 0)
    {
        opbx_log(LOG_WARNING, "We don't exist?\n");
        opbx_mutex_unlock(&p->lock);
        return NULL;
    }
    /*endif*/
    
    opbx_fr_init(&p->subs[index].f);
    p->subs[index].f.src = "unicall_read";
    
    /* Make sure it sends initial key state as first frame */
    if (p->radio  &&  !p->firstradio)
    {
        ZT_PARAMS ps;

        ps.channo = p->channel;
        if (ioctl(p->subs[SUB_REAL].fd, ZT_GET_PARAMS, &ps))
            return NULL;
        /*endif*/
        p->firstradio = TRUE;
        p->subs[index].f.frametype = OPBX_FRAME_CONTROL;
        if (ps.rxisoffhook)
            p->subs[index].f.subclass = OPBX_CONTROL_RADIO_KEY;
        else
            p->subs[index].f.subclass = OPBX_CONTROL_RADIO_UNKEY;
        /*endif*/
        opbx_mutex_unlock(&p->lock);
        return &p->subs[index].f;
    }
    /*endif*/

    select_codec(p, index, ast->rawreadformat);
    uc_check_event(p->uc);
    uc_schedule_run(p->uc);

    if (p->subs[index].needringing)
    {
        /* Send ringing frame if requested */
        opbx_log(LOG_DEBUG, "needringing\n");
        p->subs[index].needringing = 0;
        p->subs[index].f.frametype = OPBX_FRAME_CONTROL;
        p->subs[index].f.subclass = OPBX_CONTROL_RINGING;
        opbx_setstate(ast, OPBX_STATE_RINGING);
        opbx_mutex_unlock(&p->lock);
        return &p->subs[index].f;
    }
    /*endif*/
    if (p->subs[index].needbusy)
    {
        /* Send busy frame if requested */
        opbx_log(LOG_DEBUG, "needbusy\n");
        p->subs[index].needbusy = FALSE;
        p->subs[index].f.frametype = OPBX_FRAME_CONTROL;
        p->subs[index].f.subclass = OPBX_CONTROL_BUSY;
        opbx_mutex_unlock(&p->lock);
        return &p->subs[index].f;
    }
    if (p->subs[index].needcongestion)
    {
        /* Send congestion frame if requested */
        opbx_log(LOG_DEBUG, "needcongestion\n");
        p->subs[index].needcongestion = FALSE;
        p->subs[index].f.frametype = OPBX_FRAME_CONTROL;
        p->subs[index].f.subclass = OPBX_CONTROL_CONGESTION;
        opbx_mutex_unlock(&p->lock);
        return &p->subs[index].f;
    }
    if (p->subs[index].needanswer)
    {
        /* Send answer frame if requested */
        opbx_log(LOG_DEBUG, "needanswer\n");
        p->subs[index].needanswer = FALSE;
        p->subs[index].f.frametype = OPBX_FRAME_CONTROL;
        p->subs[index].f.subclass = OPBX_CONTROL_ANSWER;
        opbx_setstate(ast, OPBX_STATE_UP);
        opbx_mutex_unlock(&p->lock);
        return &p->subs[index].f;
    }
    /*endif*/
    
    /* Check first for any outstanding DTMF characters */
    if (p->subs[index].dtmfq[0])
    {
        opbx_log(LOG_WARNING, "got a DTMF %d\n", p->subs[index].dtmfq[0]);
        p->subs[index].f.subclass = p->subs[index].dtmfq[0];
        memmove(&p->subs[index].dtmfq[0], &p->subs[index].dtmfq[1], sizeof(p->subs[index].dtmfq) - 1);
        p->subs[index].f.frametype = OPBX_FRAME_DTMF;
        if (p->subs[index].f.subclass == 'f')
        {
            /* Fax tone -- Handle and return NULL */
            if (!p->faxhandled)
            {
                p->faxhandled++;
                if (strncasecmp(ast->exten, "fax", 3))
                {
                    snprintf(tmpfax, sizeof(tmpfax), "fax%s", ast->exten);
                    if (opbx_exists_extension(ast, ast->context, tmpfax, 1, ast->cid.cid_num))
                    {
                        if (option_verbose > 2)
                            opbx_verbose(VERBOSE_PREFIX_3 "Redirecting %s to specific fax extension %s\n", ast->name, tmpfax);
                        /*endif*/
                        if (opbx_async_goto(ast, ast->context, tmpfax, 1))
                            opbx_log(LOG_WARNING, "Failed to async goto '%s' into '%s' of '%s'\n", ast->name, tmpfax, ast->context);
                        /*endif*/
                    }
                    else if (opbx_exists_extension(ast, ast->context, "fax", 1, ast->cid.cid_num))
                    {
                        if (option_verbose > 2)
                            opbx_verbose(VERBOSE_PREFIX_3 "Redirecting %s to fax extension\n", ast->name);
                        /*endif*/
                        if (opbx_async_goto(ast, ast->context, "fax", 1))
                            opbx_log(LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", ast->name, ast->context);
                        /*endif*/
                    }
                    else
                    {
                        opbx_log(LOG_NOTICE, "Fax detected, but no fax extension\n");
                    }
                    /*endif*/
                }
                else
                {
                    opbx_log(LOG_DEBUG, "Already in a fax extension, not redirecting\n");
                }
                /*endif*/
            }
            else
            {
                opbx_log(LOG_DEBUG, "Fax already handled\n");
            }
            /*endif*/
            p->subs[index].f.frametype = OPBX_FRAME_NULL;
            p->subs[index].f.subclass = 0;
            return &p->subs[index].f;
        }
        /*endif*/
        if (p->confirmanswer)
        {
            opbx_log(LOG_WARNING, "Confirm answer!\n");
            /* Upon receiving a DTMF digit, consider this an answer confirmation instead
               of a DTMF digit */
            p->subs[index].f.frametype = OPBX_FRAME_CONTROL;
            p->subs[index].f.subclass = OPBX_CONTROL_ANSWER;
            opbx_setstate(ast, OPBX_STATE_UP);
        }
        /*endif*/
        opbx_mutex_unlock(&p->lock);
        return &p->subs[index].f;
    }
    /*endif*/

    if (p->subs[index].buffer_contents == 0)
    {
        /* Return a NULL frame */
        opbx_mutex_unlock(&p->lock);
        return &p->subs[index].f;
    }
    /*endif*/

    p->subs[index].buffer_contents = 0;
    if (p->dialing
        ||
        (index  &&  ast->_state != OPBX_STATE_UP)
        ||
        (index == SUB_CALLWAIT  &&  !p->subs[SUB_CALLWAIT].inthreeway))
    {
        /* Whoops, we're still dialing, or in a state where we shouldn't transmit....
           don't send anything */
        /* Return a NULL frame */
        opbx_mutex_unlock(&p->lock);
        return &p->subs[index].f;
    }
    /*endif*/

#if 0
    opbx_log(LOG_WARNING, "Read %d of voice on %s\n", p->subs[index].f.datalen, ast->name);
#endif
    if (ast->rawreadformat == OPBX_FORMAT_SLINEAR)
        p->subs[index].f.datalen = READ_SIZE*2;
    else 
        p->subs[index].f.datalen = READ_SIZE;
    /*endif*/
    p->subs[index].f.frametype = OPBX_FRAME_VOICE;
    p->subs[index].f.subclass = ast->rawreadformat;
    p->subs[index].f.samples = READ_SIZE;
    p->subs[index].f.mallocd = 0;
    p->subs[index].f.offset = OPBX_FRIENDLY_OFFSET;
    p->subs[index].f.data = p->subs[index].buffer + OPBX_FRIENDLY_OFFSET/2;
    if (p->dsp  &&  !p->ignoredtmf  &&  index == SUB_REAL)
    {
        if ((f = opbx_dsp_process(ast, p->dsp, &p->subs[index].f)))
        {
            if ((f->frametype == OPBX_FRAME_CONTROL)  &&  (f->subclass == OPBX_CONTROL_BUSY))
            {
                if ((ast->_state == OPBX_STATE_UP)  &&  !p->outgoing)
                {
                    /* Treat this as a "hangup" instead of a "busy" on the assumption that
                       its a busy  */
                    f = NULL;
                }
                /*endif*/
            }
            /*endif*/
        }
        /*endif*/
    }
    else 
    {
        f = &p->subs[index].f; 
    }
    /*endif*/
    if (f  &&  (f->frametype == OPBX_FRAME_DTMF))
    {
        opbx_log(LOG_DEBUG, "DTMF digit: %c on %s\n", f->subclass, ast->name);
        if (p->confirmanswer)
        {
            opbx_log(LOG_DEBUG, "Confirm answer on %s!\n", ast->name);
            /* Upon receiving a DTMF digit, consider this an answer confirmation instead
               of a DTMF digit */
            p->subs[index].f.frametype = OPBX_FRAME_CONTROL;
            p->subs[index].f.subclass = OPBX_CONTROL_ANSWER;
            opbx_setstate(ast, OPBX_STATE_UP);
            f = &p->subs[index].f;
        }
        else if (f->subclass == 'f')
        {
            /* Fax tone -- Handle and return NULL */
            if (!p->faxhandled)
            {
                p->faxhandled++;
                if (strcmp(ast->exten, "fax"))
                {
                    if (opbx_exists_extension(ast, ast->context, "fax", 1, ast->cid.cid_num))
                    {
                        if (option_verbose > 2)
                            opbx_verbose(VERBOSE_PREFIX_3 "Redirecting %s to fax extension\n", ast->name);
                        /*endif*/
                        if (opbx_async_goto(ast, ast->context, "fax", 1))
                            opbx_log(LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", ast->name, ast->context);
                        /*endif*/
                    }
                    else
                    {
                        opbx_log(LOG_NOTICE, "Fax detected, but no fax extension\n");
                    }
                    /*endif*/
                }
                else
                {
                    opbx_log(LOG_DEBUG, "Already in a fax extension, not redirecting\n");
                }
                /*endif*/
            }
            else
            {
                opbx_log(LOG_DEBUG, "Fax already handled\n");
            }
            /*endif*/
            unicall_confmute(p, 0);
            p->subs[index].f.frametype = OPBX_FRAME_NULL;
            p->subs[index].f.subclass = 0;
            f = &p->subs[index].f;
        }
        else if (f->subclass == 'm')
        {
            /* Confmute request */
            unicall_confmute(p, 1);
            p->subs[index].f.frametype = OPBX_FRAME_NULL;
            p->subs[index].f.subclass = 0;
            f = &p->subs[index].f;
        }
        else if (f->subclass == 'u')
        {
            /* Unmute */
            unicall_confmute(p, 0);
            p->subs[index].f.frametype = OPBX_FRAME_NULL;
            p->subs[index].f.subclass = 0;
            f = &p->subs[index].f;
        }
        else
        {
            unicall_confmute(p, 0);
        }
        /*endif*/
    }
    /*endif*/

    opbx_mutex_unlock(&p->lock);
    return f;
}

static int unicall_write(struct opbx_channel *ast, struct opbx_frame *frame)
{
    struct unicall_pvt *p;
    int index;
    int res;

    p = ast->tech_pvt;
    res = 0;
    //opbx_log(LOG_WARNING, "unicall_write\n");
    if ((index = unicall_get_index(ast, p, 0)) < 0)
    {
        opbx_log(LOG_WARNING, "%s doesn't really exist?\n", ast->name);
        return -1;
    }
    /*endif*/    
    /* Write a frame of (presumably voice) data */
    if (frame->frametype != OPBX_FRAME_VOICE)
    {
        if (frame->frametype != OPBX_FRAME_IMAGE)
            opbx_log(LOG_WARNING, "Don't know what to do with frame type '%d'\n", frame->frametype);
        /*endif*/
        return 0;
    }
    /*endif*/
    if ((frame->subclass != OPBX_FORMAT_SLINEAR)
        && 
        (frame->subclass != OPBX_FORMAT_ULAW)
        &&
        (frame->subclass != OPBX_FORMAT_ALAW))
    {
        opbx_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
        return 0;
    }
    /*endif*/
    if (p->dialing)
    {
        if (option_debug)
            opbx_log(LOG_DEBUG, "Dropping frame since I'm still dialing on %s...\n", ast->name);
        /*endif*/
        return 0;
    }
    /*endif*/
    if (p->subs[index].super_tone != ST_TYPE_NONE)
    {
        if (option_debug)
            opbx_log(LOG_DEBUG, "Dropping frame since I'm still sending supervisory tone %d on %s...\n", p->subs[index].super_tone, ast->name);
        /*endif*/
        return 0;
    }
    /*endif*/
    /* Return if it's not valid data */
    if (frame->data == NULL  ||  frame->datalen == 0)
        return 0;
    /*endif*/

    select_codec(p, index, frame->subclass);
    if ((res = uc_channel_write(p->uc, p->subs[index].chan, (unsigned char *) frame->data, frame->datalen)) < 0)
    {
        opbx_log(LOG_WARNING, "write failed: %s - %d\n", strerror(errno), errno);
        return -1;
    }
    /*endif*/
    return 0;
}

static int unicall_indicate(struct opbx_channel *chan, int condition)
{
    struct unicall_pvt *p;
    int res = -1;
    int index;
    int ret;

    p = chan->tech_pvt;
    opbx_log(LOG_WARNING, "unicall_indicate %d\n", condition);
    if ((index = unicall_get_index(chan, p, 0)) != SUB_REAL)
        return 0;
    /*endif*/
    switch (condition)
    {
    case OPBX_CONTROL_BUSY:
        super_tone(&p->subs[index], ST_TYPE_BUSY);
        break;
    case OPBX_CONTROL_RINGING:
        //super_tone(&p->subs[index], ST_TYPE_RINGBACK);
        //if ((ret = uc_call_control(p->uc, UC_OP_ACCEPTCALL, ev->offered.crn, NULL)) < 0)
        //    opbx_log(LOG_WARNING, "Accept call failed - %s\n", uc_ret2str(ret));
        if (chan->_state != OPBX_STATE_UP)
        {
            if (chan->_state != OPBX_STATE_RING)
                opbx_setstate(chan, OPBX_STATE_RINGING);
            /*endif*/
        }
        /*endif*/
        break;
    case OPBX_CONTROL_CONGESTION:
        super_tone(&p->subs[index], ST_TYPE_CONGESTED);
        break;
    case OPBX_CONTROL_RADIO_KEY:
        if (p->radio)
        {
            if ((ret = uc_call_control(p->uc, UC_OP_DROPCALL, p->crn, (void *) UC_CAUSE_NORMAL_CLEARING)) < 0)
                opbx_log(LOG_WARNING, "Drop call failed - %s\n", uc_ret2str(ret));
            /*endif*/
        }
        /*endif*/
        res = 0;
        break;
    case OPBX_CONTROL_RADIO_UNKEY:
        if (p->radio)
        {
            if ((ret = uc_call_control(p->uc, UC_OP_DROPCALL, p->crn, (void *) UC_CAUSE_NORMAL_CLEARING)) < 0)
                opbx_log(LOG_WARNING, "Drop call failed - %s\n", uc_ret2str(ret));
            /*endif*/
        }
        /*endif*/
        res = 0;
        break;
    case -1:
        super_tone(&p->subs[index], ST_TYPE_NONE);
        break;
    default:
        opbx_log(LOG_WARNING, "Don't know how to set condition %d on channel %s\n", condition, chan->name);
        break;
    }
    /*endswitch*/
    return res;
}

static struct opbx_channel *unicall_new(struct unicall_pvt *i, int state, int startpbx, int index, int law)
{
    struct opbx_channel *tmp;
    int deflaw;
    int x;
    int y;

    if ((tmp = opbx_channel_alloc(0)) == NULL)
    {
        opbx_log(LOG_WARNING, "Unable to allocate channel structure\n");
        return  NULL;
    }
    /*endif*/
    if (law == 0)
        law = uc_channel_get_api_codec(i->uc, 0);
    /*endif*/
    if (law == UC_CODEC_ALAW)
        deflaw = OPBX_FORMAT_ALAW;
    else
        deflaw = OPBX_FORMAT_ULAW;
    /*endif*/
    y = 1;
    do
    {
        snprintf(tmp->name, sizeof(tmp->name), "UniCall/%d-%d", i->channel, y);
        for (x = 0;  x < 3;  x++)
        {
            if (index != x  &&  i->subs[x].owner  &&  strcasecmp(tmp->name, i->subs[x].owner->name) == 0)
                break;
            /*endif*/
        }
        /*endfor*/
        y++;
    }
    while (x < 3);
    tmp->type = type;
    tmp->tech = &unicall_tech;
    tmp->fds[0] = i->subs[index].fd;
    tmp->nativeformats = OPBX_FORMAT_SLINEAR | deflaw;
    /* Start out assuming uLaw, since it's smaller :) */
    tmp->rawreadformat = deflaw;
    tmp->readformat = deflaw;
    tmp->rawwriteformat = deflaw;
    tmp->writeformat = deflaw;
    i->subs[index].current_codec = deflaw;
    if (uc_channel_set_api_codec(i->uc, 0, UC_CODEC_DEFAULT))
        opbx_log(LOG_WARNING, "Unable to set law on channel %d to default\n", i->channel);
    /*endif*/

    if (i->dsp)
    {
        opbx_log(LOG_DEBUG, "Already have a dsp on %s?\n", tmp->name);
    }
    else
    {
        if ((i->dsp = opbx_dsp_new()))
        {
            opbx_dsp_set_features(i->dsp, DSP_FEATURE_DTMF_DETECT);
            opbx_dsp_digitmode(i->dsp, DSP_DIGITMODE_DTMF | i->dtmfrelax);
        }
        /*endif*/
    }
    /*endif*/

    if (state == OPBX_STATE_RING)
        tmp->rings = 1;
    /*endif*/
    tmp->tech_pvt = i;
    if (strlen(i->language))
        strncpy(tmp->language, i->language, sizeof(tmp->language) - 1);
    /*endif*/
    if (strlen(i->musicclass))
        strncpy(tmp->musicclass, i->musicclass, sizeof(tmp->musicclass) - 1);
    /*endif*/
    if (i->owner == NULL)
        i->owner = tmp;
    /*endif*/
    if (strlen(i->accountcode))
        strncpy(tmp->accountcode, i->accountcode, sizeof(tmp->accountcode) - 1);
    /*endif*/
    if (i->amaflags)
        tmp->amaflags = i->amaflags;
    /*endif*/
    if (i->subs[index].owner)
        opbx_log(LOG_WARNING, "Channel %d already has a %s call\n", i->channel,subnames[index]);
    /*endif*/
    i->subs[index].owner = tmp;
    opbx_setstate(tmp, state);
    opbx_mutex_lock(&usecnt_lock);
    usecnt++;
    opbx_mutex_unlock(&usecnt_lock);
    opbx_update_use_count();
    strncpy(tmp->context, i->context, sizeof(tmp->context) - 1);
    /* Copy call forward info */
    strncpy(tmp->call_forward, i->call_forward, sizeof(tmp->call_forward));
    /* If we've been told "no ADSI" then enforce it */
    if (!i->adsi)
        tmp->adsicpe = OPBX_ADSI_UNAVAILABLE;
    /*endif*/
    if (!opbx_strlen_zero(i->exten))
        strncpy(tmp->exten, i->exten, sizeof(tmp->exten) - 1);
    /*endif*/
    if (!opbx_strlen_zero(i->rdnis))
        tmp->cid.cid_rdnis = strdup(i->rdnis);
    /*endif*/
    if (!opbx_strlen_zero(i->dnid))
        tmp->cid.cid_dnid = strdup(i->dnid);
    /*endif*/
    if (!opbx_strlen_zero(i->cid_num))
    {
        tmp->cid.cid_num = strdup(i->cid_num);
        tmp->cid.cid_ani = strdup(i->cid_num);
    }
    /*endif*/
    if (!opbx_strlen_zero(i->cid_name))
        tmp->cid.cid_name = strdup(i->cid_name);
    /*endif*/
    tmp->cid.cid_pres = i->callingpres;
    if (startpbx  &&  opbx_pbx_start(tmp))
    {
        opbx_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
        opbx_hangup(tmp);
        tmp = NULL;
    }
    /*endif*/
    return tmp;
}

int channel_error(uc_t *uc, int user_data, int cause)
{
    opbx_log(LOG_ERROR, "Channel error - %d\n", cause);
    return  0;
}

void handle_uc_event(uc_t *uc, void *user_data, uc_event_t *ev)
{
    struct opbx_channel *c;
    struct unicall_pvt *i;
    int ch;
    int ret;
    
    ch = 0;
    i = (struct unicall_pvt *) user_data;
    if (i->radio)
        return;
    /*endif*/
    /* Handle an event on a given channel for the monitor thread. */

    opbx_log(LOG_WARNING, "Unicall/%d event %s\n", i->channel, uc_event2str(ev->e));
    switch (ev->e)
    {
    case UC_EVENT_PROTOCOLFAIL:
        if (option_verbose > 2)
            opbx_verbose(VERBOSE_PREFIX_3 "Unicall/%d protocol error. Cause %d\n", i->channel, ev->gen.data);
        /*endif*/
        if (!i->blocked)
        {
            /* Behave like call clearing, to try to settle things down. */
            if (i->owner)
            {
                i->alreadyhungup = TRUE;
                i->owner->_softhangup |= OPBX_SOFTHANGUP_DEV;
            }
            /*endif*/
            i->crn = 0;
            unicall_disable_ec(i);
        }
        /*endif*/
        break;
    case UC_EVENT_LOCALUNBLOCKED:
        if (option_verbose > 2)
            opbx_verbose(VERBOSE_PREFIX_3 "Unicall/%d local unblocked\n", i->channel);
        /*endif*/
        break;
    case UC_EVENT_LOCALBLOCKED:
        if (option_verbose > 2)
            opbx_verbose(VERBOSE_PREFIX_3 "Unicall/%d local blocked\n", i->channel);
        /*endif*/
        break;
    case UC_EVENT_FARUNBLOCKED:
        i->blocked = FALSE;
        if (option_verbose > 2)
            opbx_verbose(VERBOSE_PREFIX_3 "Unicall/%d far unblocked\n", i->channel);
        /*endif*/
        break;
    case UC_EVENT_FARBLOCKED:
        i->blocked = TRUE;
        if (option_verbose > 2)
            opbx_verbose(VERBOSE_PREFIX_3 "Unicall/%d far blocked\n", i->channel);
        /*endif*/
        break;
    case UC_EVENT_DETECTED:
        /* A call is beginning to come in */
        i->reserved = TRUE;
        break;
    case UC_EVENT_DIALTONE:
        break;
    case UC_EVENT_DIALING:
        break;
    case UC_EVENT_PROCEEDING:
        break;
    case UC_EVENT_OFFERED:
        /* Check for callerid, digits, etc */
        opbx_log(LOG_WARNING,
                "CRN %d - Offered on channel %d (ANI: %s, DNIS: %s, Cat: %d)\n",
                ev->offered.crn,
                ev->offered.channel,
                ev->offered.parms.originating_number,
                ev->offered.parms.destination_number,
                ev->offered.parms.calling_party_category);
        if ((ch = ev->offered.channel) >= 0)
        {
            /* Get caller ID */
            if (i->use_callerid)
            {
                opbx_shrink_phone_number(ev->offered.parms.originating_number);
                strncpy(i->cid_num, ev->offered.parms.originating_number, sizeof(i->cid_num) - 1);
                strncpy(i->cid_name, ev->offered.parms.originating_name, sizeof(i->cid_name) - 1);
            }
            else
            {
                i->cid_num[0] = '\0';
                i->cid_name[0] = '\0';
            }
            /*endif*/
            if (i->owner)
            {
                i->owner->cid.cid_num = strdup(i->cid_num);
            }
            /*endif*/
            strncpy(i->rdnis, ev->offered.parms.redirecting_number, sizeof(i->rdnis));
            i->rdnis[sizeof(i->rdnis) - 1] = '\0';
            /* Get called number */
            if (strlen(ev->offered.parms.destination_number))
                strncpy(i->exten, ev->offered.parms.destination_number, sizeof(i->exten) - 1);
            else
                strcpy(i->exten, "s");
            /*endif*/
            /* Make sure the extension exists */
            if (opbx_exists_extension(NULL, i->context, i->exten, 1, i->cid_num))
            {
                //i->call = ev->offered.call;
                i->crn = ev->offered.crn;
                /* Setup law */
                if (ev->offered.parms.userinfo_layer1_protocol == UC_LAYER_1_ALAW)
                    i->law = UC_CODEC_ALAW;
                else
                    i->law = UC_CODEC_ULAW;
                /*endif*/
                if ((ret = uc_call_control(i->uc, UC_OP_ACCEPTCALL, ev->offered.crn, NULL)) < 0)
                    opbx_log(LOG_WARNING, "Accept call failed - %s\n", uc_ret2str(ret));
                /*endif*/
            }
            else
            {
                ret = UC_RET_OK;
                if (opbx_matchmore_extension(NULL, i->context, i->exten, 1, i->cid_num)
                    &&
                    uc_call_control(i->uc, UC_OP_CALLACK, ev->offered.crn, NULL) == UC_RET_OK)
                {
                    /* Wait for more digits */
                }
                else
                {
                    if (option_verbose > 2)
                    {
                        opbx_verbose(VERBOSE_PREFIX_3 "Unicall/%d extension '%s' in context '%s' from '%s' does not exist.  Rejecting call\n",
                                    i->channel,
                                    i->exten,
                                    i->context,
                                    i->cid_num);
                    }
                    /*endif*/
                    if ((ret = uc_call_control(i->uc, UC_OP_DROPCALL, ev->offered.crn, (void *) UC_CAUSE_UNASSIGNED_NUMBER)) < 0)
                        opbx_log(LOG_WARNING, "Drop call failed - %s\n", uc_ret2str(ret));
                    /*endif*/
                }
                /*endif*/
            }
            /*endif*/
        }
        else
        { 
            if ((ret = uc_call_control(i->uc, UC_OP_DROPCALL, ev->offered.crn, (void *) UC_CAUSE_CHANNEL_UNACCEPTABLE)) < 0)
                opbx_log(LOG_WARNING, "Drop call failed - %s\n", uc_ret2str(ret));
            /*endif*/
        }
        /*endif*/
        break;
    case UC_EVENT_MOREDIGITS:
        opbx_log(LOG_WARNING, "Dest '%s'\n", ev->offered.parms.destination_number);
        if ((ch = ev->offered.channel) >= 0)
        {
            /* Get caller ID */
            if (i->use_callerid)
            {
                opbx_shrink_phone_number(ev->offered.parms.originating_number);
                strncpy(i->cid_num, ev->offered.parms.originating_number, sizeof(i->cid_num) - 1);
                strncpy(i->cid_name, ev->offered.parms.originating_name, sizeof(i->cid_name) - 1);
            }
            else
            {
                i->cid_num[0] = '\0';
                i->cid_name[0] = '\0';
            }
            /*endif*/
            strncpy(i->rdnis, ev->offered.parms.redirecting_number, sizeof(i->rdnis));
            i->rdnis[sizeof(i->rdnis) - 1] = '\0';
            /* Get called number */
            if (strlen(ev->offered.parms.destination_number))
                strncpy(i->exten, ev->offered.parms.destination_number, sizeof(i->exten) - 1);
            else
                strcpy(i->exten, "s");
            /*endif*/
            i->exten[sizeof(i->exten) - 1] = '\0';
            /* Make sure extension exists */
            if (opbx_exists_extension(NULL, i->context, i->exten, 1, i->cid_num))
            {
                //i->call = ev->offered.call;
                i->crn = ev->offered.crn;
                /* Setup law */
                if (ev->offered.parms.userinfo_layer1_protocol == UC_LAYER_1_ALAW)
                    i->law = UC_CODEC_ALAW;
                else
                    i->law = UC_CODEC_ULAW;
                /*endif*/
                if ((ret = uc_call_control(i->uc, UC_OP_ACCEPTCALL, ev->offered.crn, NULL)) < 0)
                    opbx_log(LOG_WARNING, "Accept call failed - %s\n", uc_ret2str(ret));
                /*endif*/
            }
            else
            {
                if (!opbx_matchmore_extension(NULL, i->context, i->exten, 1, i->cid_num))
                {
                    if (option_verbose > 2)
                    {
                        opbx_verbose(VERBOSE_PREFIX_3 "Unicall/%d extension '%s' in context '%s' from '%s' does not exist.  Rejecting call\n",
                                    i->channel,
                                    i->exten,
                                    i->context,
                                    i->cid_num);
                    }
                    /*endif*/
                    if ((ret = uc_call_control(i->uc, UC_OP_DROPCALL, ev->offered.crn, (void *) UC_CAUSE_UNASSIGNED_NUMBER)) < 0)
                        opbx_log(LOG_WARNING, "Drop call failed - %s\n", uc_ret2str(ret));
                    /*endif*/
                }
                /*endif*/
            }
            /*endif*/
        }
        else
        { 
            if ((ret = uc_call_control(i->uc, UC_OP_DROPCALL, ev->offered.crn, (void *) UC_CAUSE_CHANNEL_UNACCEPTABLE)) < 0)
                opbx_log(LOG_WARNING, "Drop call failed - %s\n", uc_ret2str(ret));
            /*endif*/
        }
        /*endif*/
        break;
    case UC_EVENT_ACCEPTED:        
        if (uc_channel_set_api_codec(i->uc, 0, UC_CODEC_DEFAULT))
            opbx_log(LOG_WARNING, "Unicall/%d unable to set law\n", i->channel);
        /*endif*/
        if (uc_channel_gains(i->uc, 0, i->rxgain, i->txgain))
            opbx_log(LOG_WARNING, "Unicall/%d unable to set gains\n", i->channel);
        /*endif*/
        /* Start PBX */
        if ((c = unicall_new(i, OPBX_STATE_RING, 1, SUB_REAL, i->law)) == NULL)
        {
            opbx_log(LOG_WARNING, "Unicall/%d unable to start PBX\n", i->channel);
            if ((ret = uc_call_control(i->uc, UC_OP_DROPCALL, ev->offered.crn, (void *) UC_CAUSE_NETWORK_CONGESTION)) < 0)
                opbx_log(LOG_WARNING, "Drop call failed - %s\n", uc_ret2str(ret));
            /*endif*/
        }
        /*endif*/
        i->subs[SUB_REAL].needanswer = FALSE;
        break;
    case UC_EVENT_ALERTING:
        ch = ev->gen.channel;
        if (ch)
        {
            if (opbx_strlen_zero(i->dialstr))
            {
                unicall_enable_ec(i);
                i->subs[SUB_REAL].needringing = 1;
            }
            else
            {
                opbx_log(LOG_DEBUG, "Deferring ringing notification because of extra digits to dial...\n");
            }
            /*endif*/
        }
        /*endif*/
        break;
    case UC_EVENT_CONNECTED:
        if ((ch = ev->gen.channel) >= 0)
        {
            if (opbx_strlen_zero(i->dialstr))
            {
                i->subs[SUB_REAL].needanswer = TRUE;
            }
            else
            {
                i->dialing = TRUE;
                dtmf_put(&i->subs[SUB_REAL].dtmf_tx_state, i->dialstr);
                opbx_log(LOG_DEBUG, "Sent deferred digit string: %s\n", i->dialstr);
                i->dialstr[0] = '\0';
            }
            /*endif*/
            /* Enable echo cancellation if it's not on already */
            unicall_enable_ec(i);
            i->sigcheck = 0;
        }
        else
        {
            opbx_log(LOG_WARNING, "Connected/answered on bad channel %d\n", ev->gen.channel);
        }
        /*endif*/
        break;
    case UC_EVENT_ANSWERED:
        if ((ch = ev->gen.channel) >= 0)
        {
            /* Enable echo cancellation if it's not on already */
            unicall_enable_ec(i);
        }
        else
        {
            opbx_log(LOG_WARNING, "Connected/answered on bad channel %d\n", ev->gen.channel);
        }
        /*endif*/
        break;
    case UC_EVENT_FARDISCONNECTED:
        if ((ch = ev->fardisconnected.channel) >= 0)
        {
            opbx_log(LOG_WARNING, "CRN %d - far disconnected cause=%s [%d]\n", ev->fardisconnected.crn, uc_cause2str(ev->fardisconnected.cause), ev->fardisconnected.cause);
            if (i->owner)
            {
                i->alreadyhungup = TRUE;
                i->owner->_softhangup |= OPBX_SOFTHANGUP_DEV;
                if (option_verbose > 2) 
                    opbx_verbose(VERBOSE_PREFIX_3 "Channel %d got hangup\n", ch);
                /*endif*/
            }
            else
            {
                if ((ret = uc_call_control(i->uc, UC_OP_DROPCALL, ev->fardisconnected.crn, (void *) UC_CAUSE_NORMAL_CLEARING)) < 0)
                    opbx_log(LOG_WARNING, "Drop call failed - %s\n", uc_ret2str(ret));
                /*endif*/
            }
            /*endif*/
            switch (ev->fardisconnected.cause)
            {
            case UC_CAUSE_REQ_CHANNEL_NOT_AVAILABLE:
                if (option_verbose > 2)
                    opbx_verbose(VERBOSE_PREFIX_3 "Forcing restart of channel %d since channel reported in use\n", ch);
                //pri_reset(i, ch);
                //i->resetting = TRUE;
                break;
            case UC_CAUSE_USER_BUSY:
                i->subs[SUB_REAL].needbusy = TRUE;
                break;
            case UC_CAUSE_TEMPORARY_FAILURE:
            case UC_CAUSE_NO_ANSWER_FROM_USER:
            case UC_CAUSE_NETWORK_CONGESTION:
            case UC_CAUSE_DEST_OUT_OF_ORDER:
            case UC_CAUSE_UNASSIGNED_NUMBER:
            case UC_CAUSE_UNSPECIFIED_CAUSE:
                i->subs[SUB_REAL].needcongestion = TRUE;
                break;
            }
            /*endswitch*/

        }
        else
        {
            opbx_log(LOG_WARNING, "Hangup on bad channel %d\n", ev->fardisconnected.channel);
        }
        /*endif*/
        break;
    case UC_EVENT_DROPCALL:
        if ((ch = ev->gen.channel) >= 0)
        {
            opbx_log(LOG_DEBUG, "CRN %d - Doing a release call\n", ev->gen.crn);
            if ((ret = uc_call_control(i->uc, UC_OP_RELEASECALL, ev->gen.crn, NULL)) < 0)
                opbx_log(LOG_WARNING, "Release call failed - %s\n", uc_ret2str(ret));
            /*endif*/
        }
        else
        {
            opbx_log(LOG_WARNING, "Hangup on bad channel %d\n", ev->fardisconnected.channel);
        }
        /*endif*/
        break;
    case UC_EVENT_RELEASECALL:
        if ((ch = ev->gen.channel) >= 0)
        {
            opbx_log(LOG_DEBUG, "CRN %d - Call released\n", ev->gen.crn);
            if (option_verbose > 2)
                opbx_verbose(VERBOSE_PREFIX_3 "Unicall/%d released\n", i->channel);
            /*endif*/
            i->crn = 0;
            unicall_disable_ec(i);
        }
        else
        {
            opbx_log(LOG_WARNING, "Hangup on bad channel %d\n", ev->fardisconnected.channel);
        }
        /*endif*/
        i->reserved = FALSE;
        break;
    case UC_EVENT_ALARM:
        /* TODO: No alarm handling is bad! */
        opbx_log(LOG_WARNING,
                "Unicall/%d Alarm masks 0x%04X 0x%04X\n",
                i->channel,
                ev->alarm.raised,
                ev->alarm.cleared);
        opbx_log(LOG_WARNING,
                "Unicall/%d Alarm %s raised, %s cleared\n",
                i->channel,
                alarm2str(ev->alarm.raised),
                alarm2str(ev->alarm.cleared));
        i->current_alarms |= ev->alarm.raised;
        i->current_alarms &= ~ev->alarm.cleared;
        break;
    default:
        opbx_log(LOG_WARNING,
                "Unicall/%d Don't know how to handle signalling event %s\n",
                i->channel,
                uc_event2str(ev->e));
        break;
    }
    /*endswitch*/
}

static void *do_monitor(void *data)
{
    struct unicall_pvt *i;
    struct unicall_pvt *last = NULL;
    fd_set efds;
    fd_set rfds;
    int n;
    int res;
    struct timeval tv;
    time_t thispass = 0;
    time_t lastpass = 0;
    int found;

    /* This thread monitors all the channels which are idle. Basically, it is waiting
       for calls to arrive. Once they do, a call thread will be created, and that will do
       all the I/O for the channel until the call ends. Then things return to this thread
       to await another call. If the channel is grabbed to make an outgoing call, this will
       also transfer I/O work to the call's thread, until the channel returns to the idle
       state. */

    /* From here on out, we die whenever asked */
#if 0
    if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL))
    {
        opbx_log(LOG_WARNING, "Unable to set cancel type to asynchronous\n");
        return NULL;
    }
    /*endif*/
    opbx_log(LOG_DEBUG, "Monitor starting...\n");
#endif
    for (;;)
    {
        /* Lock the interface list */
        if (opbx_mutex_lock(&iflock))
        {
            opbx_log(LOG_ERROR, "Unable to grab interface lock\n");
            return NULL;
        }
        /*endif*/
        /* Build the stuff we're going to select on. This is the socket of every
           unicall_pvt that does not have an associated owner channel. */
        n = -1;
        FD_ZERO(&efds);
        FD_ZERO(&rfds);
        for (i = iflist;  i;  i = i->next)
        {
            if (i->subs[SUB_REAL].fd >= 0  &&  i->protocol_class  &&  !i->radio)
            {
                if (FD_ISSET(i->subs[SUB_REAL].fd, &efds))
                    opbx_log(LOG_WARNING, "Descriptor %d appears twice?\n", i->subs[SUB_REAL].fd);
                /*endif*/
                if (i->sigcheck  ||  (i->owner == NULL  &&  i->subs[SUB_REAL].owner == NULL))
                {
                    /* This needs to be watched, as it lacks an owner */
                    FD_SET(i->subs[SUB_REAL].fd, &rfds);
                    FD_SET(i->subs[SUB_REAL].fd, &efds);
                    if (i->subs[SUB_REAL].fd > n)
                        n = i->subs[SUB_REAL].fd;
                    /*endif*/
                }
                /*endif*/
            }
            /*endif*/
        }
        /*endfor*/
        /* Okay, now that we know what to do, release the interface lock */
        opbx_mutex_unlock(&iflock);
        
        pthread_testcancel();
        /* Wait at least a second for something to happen */
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        res = select(n + 1, &rfds, NULL, &efds, &tv);
        pthread_testcancel();
        /* Okay, select has finished.  Let's see what happened.  */
        if (res < 0)
        {
            if (errno != EAGAIN  &&  errno != EINTR)
                opbx_log(LOG_WARNING, "select return %d: %s\n", res, strerror(errno));
            /*endif*/
            continue;
        }
        /*endif*/
        /* Alright, lock the interface list again, and let's look and see what has
           happened */
        if (opbx_mutex_lock(&iflock))
        {
            opbx_log(LOG_WARNING, "Unable to lock the interface list\n");
            continue;
        }
        /*endif*/
        found = 0;
        lastpass = thispass;
        thispass = time(NULL);
        for (i = iflist;  i;  i = i->next)
        {
            if (thispass != lastpass)
            {
                if (!found  &&  ((i == last)  ||  ((i == iflist)  &&  !last)))
                {
                    last = i;
                    if (last)
                    {
                        if (last->owner == NULL
                            &&
                            strlen(last->mailbox)
                            &&
                            (thispass - last->onhooktime > 3)
                            &&
                            0) //(last->protocol_class & __ZT_SIG_FXO))
                        {
                            opbx_log(LOG_DEBUG, "Channel %d has mailbox %s\n", last->channel, last->mailbox);
                            if ((res = opbx_app_has_voicemail(last->mailbox, NULL)) != last->msgstate)
                            {
                                opbx_log(LOG_DEBUG, "Message status for %s changed from %d to %d on %d\n", last->mailbox, last->msgstate, res, last->channel);
                                found++;
                            }
                            /*endif*/
                        }
                        /*endif*/
                        last = last->next;
                    }
                    /*endif*/
                }
                /*endif*/
            }
            /*endif*/
            if (i->subs[SUB_REAL].fd >= 0  &&  i->protocol_class  &&  !i->radio)
            {
                if (FD_ISSET(i->subs[SUB_REAL].fd, &rfds))
                {
                    if (!i->sigcheck  &&  (i->owner  ||  i->subs[SUB_REAL].owner))
                    {
                        opbx_log(LOG_WARNING, "Unicall/%d Whoa....  I'm owned but found (%d) in read [%p, %p]...\n", i->channel, i->subs[SUB_REAL].fd, i->owner, i->subs[SUB_REAL].owner);
                        continue;
                    }
                    /*endif*/
                    uc_check_event(i->uc);
                    uc_schedule_run(i->uc);
                    continue;
                }
                /*endif*/
                if (FD_ISSET(i->subs[SUB_REAL].fd, &efds))
                {
                    if (!i->sigcheck  &&  (i->owner  ||  i->subs[SUB_REAL].owner))
                    {
                        opbx_log(LOG_WARNING, "Whoa....  I'm owned but found (%d)...\n", i->subs[SUB_REAL].fd);
                        continue;
                    }
                    /*endif*/
                    uc_check_event(i->uc);
                    uc_schedule_run(i->uc);
                }
                /*endif*/
            }
            /*endif*/
        }
        /*endfor*/
        opbx_mutex_unlock(&iflock);
    }
    /*endfor*/
    /* Never reached */
    return NULL;
}

static int restart_monitor(void)
{
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    /* If we're supposed to be stopped -- stay stopped */
    if (monitor_thread == OPBX_PTHREADT_STOP)
        return 0;
    /*endif*/
    if (opbx_mutex_lock(&monlock))
    {
        opbx_log(LOG_WARNING, "Unable to lock monitor\n");
        return -1;
    }
    /*endif*/
    if (monitor_thread == pthread_self())
    {
        opbx_mutex_unlock(&monlock);
        opbx_log(LOG_WARNING, "Cannot kill myself\n");
        return -1;
    }
    /*endif*/
    if (monitor_thread != OPBX_PTHREADT_NULL)
    {
        /* Just signal it to be sure it wakes up */
#if 0
        pthread_cancel(monitor_thread);
#endif
        pthread_kill(monitor_thread, SIGURG);
#if 0
        pthread_join(monitor_thread, NULL);
#endif
    }
    else
    {
        /* Start a new monitor */
        if (opbx_pthread_create(&monitor_thread, &attr, do_monitor, NULL) < 0)
        {
            opbx_mutex_unlock(&monlock);
            opbx_log(LOG_ERROR, "Unable to start monitor thread.\n");
            return -1;
        }
        /*endif*/
    }
    /*endif*/
#if 0
    opbx_log(LOG_DEBUG, "Created thread %ld detached in restart monitor\n", monitor_thread);
#endif
    opbx_mutex_unlock(&monlock);
    return 0;
}

static int reset_channel(struct unicall_pvt *p)
{
    int drop = TRUE;
    int ret;
    int i;

    opbx_log(LOG_DEBUG, "reset_channel()\n");
    if (p->owner)
    {
        drop = FALSE;
        p->owner->_softhangup |= OPBX_SOFTHANGUP_DEV;
    }
    /*endif*/
    for (i = 0;  i < 3;  i++)
    {
        if (p->subs[i].owner)
        {
            drop = FALSE;
            p->subs[i].owner->_softhangup |= OPBX_SOFTHANGUP_DEV;
        }
        /*endif*/
    }
    /*endfor*/
    if (drop)
    {
        if ((ret = uc_call_control(p->uc, UC_OP_DROPCALL, p->crn, (void *) UC_CAUSE_NORMAL_CLEARING)))
        {
            opbx_log(LOG_WARNING, "Drop call failed - %s\n", uc_ret2str(ret));
            opbx_log(LOG_ERROR, "Unable to hangup chan_unicall channel %d\n", p->channel);
            return -1;
        }
        /*endif*/
    }
    /*endif*/

    return 0;
}

static struct unicall_pvt *mkintf(int channel, char *protocol_class, char *protocol_variant, char *protocol_end, int radio)
{
    /* Make a unicall_pvt structure for this interface */
    struct unicall_pvt *tmp = NULL;
    struct unicall_pvt *tmp2;
    struct unicall_pvt *prev = NULL;
	struct unicall_pvt **wlist;
	struct unicall_pvt **wend;
    char fn[80];
    int here;
    int x;
    int ret;
    ZT_PARAMS p;

	wlist = &iflist;
	wend = &ifend;

    here = 0;
    for (prev = NULL, tmp2 = *wlist;  tmp2;  prev = tmp2, tmp2 = tmp2->next)
    {
        if (tmp2->channel == channel)
        {
            tmp = tmp2;
            here = 1;
            break;
        }
        /*endif*/
        if (tmp2->channel > channel)
            break;
        /*endif*/
    }
    /*endfor*/

    if (!here)
    {
        if ((tmp = (struct unicall_pvt *) malloc(sizeof(struct unicall_pvt))) == NULL)
        {
            opbx_log(LOG_ERROR, "MALLOC FAILED\n");
            return NULL;
        }
        /*endif*/
        memset(tmp, 0, sizeof(struct unicall_pvt));
        for (x = 0;  x < 3;  x++)
            tmp->subs[x].fd = -1;
        /*endfor*/
        tmp->next = tmp2;
        if (prev)
            prev->next = tmp;
        else
            iflist = tmp;
        /*endif*/
    }
    /*endif*/

    if (tmp)
    {
        if (channel != CHAN_PSEUDO)
        {
            memset(&p, 0, sizeof(p));
            if (here  &&  strcmp(tmp->protocol_class, protocol_class) != 0)
            {
                if (reset_channel(tmp))
                {
                    opbx_log(LOG_ERROR, "Failed to reset chan_unicall channel %d\n", tmp->channel);
                    return NULL;
                }
                /*endif*/
            }
            /*endif*/

            snprintf(fn, sizeof(fn), "%d", channel);
            tmp->protocol_class = protocol_class;
            tmp->protocol_variant = protocol_variant;
            tmp->protocol_end = (protocol_end == NULL  ||  strcasecmp(protocol_end, "co") == 0)  ?  UC_MODE_CO  :  UC_MODE_CPE;
            if (!here)
                tmp->subs[SUB_REAL].fd = unicall_open(tmp, fn);
            /*endif*/
            if (tmp->subs[SUB_REAL].fd < 0)
            {
                opbx_log(LOG_ERROR, "Unable to open channel %d: %s\nhere = %d, tmp->channel = %d, channel = %d\n", channel, strerror(errno), here, tmp->channel, channel);
                free(tmp);
                return NULL;
            }
            /*endif*/
        }
        else
        {
            protocol_class = NULL;
        }
        /*endif*/
        tmp->protocol_class = protocol_class;
        tmp->protocol_variant = protocol_variant;
        tmp->protocol_end = (protocol_end == NULL  ||  strcasecmp(protocol_end, "co") == 0)  ?  UC_MODE_CO  :  UC_MODE_CPE;
        if ((tmp->uc = uc_new(tmp->subs[SUB_REAL].fd,
                              tmp->subs[SUB_REAL].fd,
                              tmp->protocol_class,
                              tmp->protocol_variant,
                              tmp->protocol_end,
                              1)) == NULL)
        {
            opbx_log(LOG_WARNING, "Unable to create UC context :(\n");
            unicall_close(tmp->subs[SUB_REAL].fd);
            free(tmp);
            return NULL;
        }
        /*endif*/
        snprintf(fn, sizeof(fn), "UniCall/%d", channel);
        uc_set_logging(tmp->uc, logging_level, 0, fn);
        uc_set_signaling_callback(tmp->uc, handle_uc_event, (void *) tmp);
        uc_set_channel_read_callback(tmp->uc, 0, handle_uc_read, (void *) tmp);
        //uc_set_channel_error_callback(tmp->uc, channel_error, (void *) tmp);
        dtmf_tx_init(&tmp->subs[SUB_REAL].dtmf_tx_state);
        if ((ret = uc_call_control(tmp->uc, UC_OP_UNBLOCK, 0, (void *) -1)) < 0)
            opbx_log(LOG_WARNING, "Unblock failed - %s\n", uc_ret2str(ret));
        /*endif*/
        tmp->immediate = immediate;
        tmp->protocol_class = protocol_class;
        tmp->t38_support = t38_support;
        tmp->radio = radio;
        tmp->firstradio = FALSE;
        /* Flag to destroy the channel must be cleared on new mkif.  Part of changes for reload to work */
        tmp->destroy = FALSE;
        tmp->threewaycalling = threewaycalling;
        tmp->adsi = adsi;
        tmp->callreturn = callreturn;
        tmp->echocancel = echocancel;
        tmp->echotraining = echotraining;
        tmp->echocanbridged = echocanbridged;
        tmp->cancallforward = cancallforward;
        tmp->channel = channel;
        tmp->stripmsd = stripmsd;
        tmp->use_callerid = use_callerid;
        strncpy(tmp->accountcode, accountcode, sizeof(tmp->accountcode) - 1);
        tmp->accountcode[sizeof(tmp->accountcode) - 1] = '\0';
        tmp->amaflags = amaflags;
        if (!here)
        {
            tmp->confno = -1;
            tmp->propconfno = -1;
        }
        /*endif*/
        tmp->transfer = transfer;
        opbx_mutex_init(&tmp->lock);
        strncpy(tmp->language, language, sizeof(tmp->language) - 1);
        strncpy(tmp->musicclass, musicclass, sizeof(tmp->musicclass) - 1);
        strncpy(tmp->context, context, sizeof(tmp->context) - 1);
        strncpy(tmp->mailbox, mailbox, sizeof(tmp->mailbox) - 1);
        tmp->msgstate = -1;
        tmp->group = cur_group;
        tmp->callgroup = cur_callergroup;
        tmp->pickupgroup = cur_pickupgroup;
        tmp->rxgain = rxgain;
        tmp->txgain = txgain;
        tmp->onhooktime = time(NULL);
        if (tmp->subs[SUB_REAL].fd >= 0)
        {
            uc_channel_gains(tmp->uc, 0, tmp->rxgain, tmp->txgain);
            if (tmp->dsp)
                opbx_dsp_digitmode(tmp->dsp, DSP_DIGITMODE_DTMF | tmp->dtmfrelax);
            /*endif*/
            update_conf(tmp);
            tmp->current_alarms = 0;
        }
        /*endif*/
    }
    /*endif*/
	if (tmp  &&  !here)
    {
		if (*wlist == NULL)
        {
    		/* Nothing on the iflist */
			*wlist = tmp;
			tmp->prev = NULL;
			tmp->next = NULL;
			*wend = tmp;
		}
        else
        {
			/* At least one member on the iflist */
			struct unicall_pvt *working = *wlist;

			/* Check if we maybe have to put it on the beginning */
			if (working->channel > tmp->channel)
            {
				tmp->next = *wlist;
				tmp->prev = NULL;
				(*wlist)->prev = tmp;
				*wlist = tmp;
			}
            else
            {
			    /* Go through all the members and put the member in the right place */
				while (working)
                {
					/* In the middle */
					if (working->next)
                    {
						if (working->channel < tmp->channel && working->next->channel > tmp->channel)
                        {
							tmp->next = working->next;
							tmp->prev = working;
							working->next->prev = tmp;
							working->next = tmp;
							break;
						}
					}
                    else
                    {
    					/* The last */
						if (working->channel < tmp->channel)
                        {
							working->next = tmp;
							tmp->next = NULL;
							tmp->prev = working;
							*wend = tmp;
							break;
						}
					}
					working = working->next;
				}
			}
		}
	}
    return tmp;
}

static inline int available(struct unicall_pvt *p, int channelmatch, int groupmatch, int *busy)
{
    /* First, check group matching */
    if ((p->group & groupmatch) != groupmatch)
        return FALSE;
    /*endif*/
    /* Check to see if we have a channel match */
    if (channelmatch > 0  &&  p->channel != channelmatch)
        return FALSE;
    /*endif*/
	/* We're at least busy at this point */
	if (busy)
        *busy = TRUE;
    /* If do not disturb, definitely not */
    if (p->dnd)
        return FALSE;
    /*endif*/
    /* If no owner definitely available */
    if (p->owner)
        return FALSE;
    /*endif*/
    if (p->uc)
    {
        if (p->crn  ||  p->blocked)
            return FALSE;
        /*endif*/
        return TRUE;
    }
    /*endif*/
    if (!p->radio)
    {
        /* Check hook state */
        /* TODO: check hook state */
    }
    /*endif*/
    return TRUE;
}

static struct unicall_pvt *chandup(struct unicall_pvt *src)
{
    struct unicall_pvt *p;

    if ((p = malloc(sizeof(struct unicall_pvt))) == NULL)
        return NULL;
    /*endif*/
    memcpy(p, src, sizeof(struct unicall_pvt));
    if ((p->subs[SUB_REAL].fd = unicall_open(p, "/dev/zap/pseudo")) < 0)
    {
        opbx_log(LOG_ERROR, "Unable to dup channel: %s\n",  strerror(errno));
        free(p);
        return NULL;
    }
    /*endif*/
    p->destroy = TRUE;
    p->next = iflist;
    iflist = p;
    return p;
}

static struct opbx_channel *unicall_request(const char *type, int format, void *data, int *cause)
{
    struct unicall_pvt *p;
	struct unicall_pvt *exit;
    struct unicall_pvt *start;
    struct unicall_pvt *end;
    int oldformat;
    int groupmatch = 0;
    int channelmatch = -1;
    int callwait = 0;
    struct opbx_channel *tmp = NULL;
    char *dest = NULL;
    int x;
    char *s;
    char opt = 0;
    int res = 0;
    int y = 0;
    char *stringp;
  	int roundrobin = FALSE;
	int backwards = FALSE;
	int busy = FALSE;
 	opbx_mutex_t *lock;
 
	/* Assume we're locking the iflock */
	lock = &iflock;
	start = iflist;
	end = ifend;

    /* We do signed linear */
    oldformat = format;
    format &= (OPBX_FORMAT_SLINEAR | OPBX_FORMAT_ALAW | OPBX_FORMAT_ULAW);
    if (!format)
    {
        opbx_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
        return NULL;
    }
    /*endif*/
    if (data == NULL)
    {
        opbx_log(LOG_WARNING, "Channel requested with no data\n");
        return NULL;
    }
    /*endif*/
    if ((dest = strdup((char *) data)) == NULL)
    {
        opbx_log(LOG_ERROR, "Out of memory\n");
        return NULL;
    }
    /*endif*/
	if (toupper(dest[0]) == 'G'  ||  toupper(dest[0])=='R')
    {
        /* Retrieve the group number */
        stringp = dest + 1;
        s = strsep(&stringp, "/");
        if ((res = sscanf(s, "%d%c%d", &x, &opt, &y)) < 1)
        {
            opbx_log(LOG_WARNING, "Unable to determine group for data %s\n", (char *) data);
            free(dest);
            return NULL;
        }
        /*endif*/
        groupmatch = 1 << x;
		if (toupper(dest[0]) == 'G')
        {
			if (dest[0] == 'G')
            {
				backwards = 1;
				p = ifend;
			}
            else
            {
				p = iflist;
            }
            /*endif*/
		}
        else
        {
			if (dest[0] == 'R')
            {
				backwards = 1;
				if ((p = (round_robin[x])  ?  round_robin[x]->prev  :  ifend) == NULL)
					p = ifend;
                /*endif*/
			}
            else
            {
				if ((p = (round_robin[x])  ?  round_robin[x]->next  :  iflist) == NULL)
					p = iflist;
                /*endif*/
			}
            /*endif*/
			roundrobin = 1;
		}
        /*endif*/
    }
    else
    {
        stringp = dest;
        s = strsep(&stringp, "/");
		p = iflist;
        if (!strcasecmp(s, "pseudo"))
        {
            /* Special case for pseudo */
            x = CHAN_PSEUDO;
        }
        else if ((res = sscanf(s, "%d%c%d", &x, &opt, &y)) < 1)
        {
            opbx_log(LOG_WARNING, "Unable to determine channel for data %s\n", (char *) data);
            free(dest);
            return NULL;
        }
        /*endif*/
        channelmatch = x;
    }
    /*endif*/
    /* Search for an unowned channel */
    if (opbx_mutex_lock(lock))
    {
        opbx_log(LOG_ERROR, "Unable to lock interface list???\n");
        return NULL;
    }
    /*endif*/
    exit = p;
    while (p  &&  tmp == NULL)
    {
		if (roundrobin)
			round_robin[x] = p;
        /*endif*/
        if (p  &&  available(p, channelmatch, groupmatch, &busy))
        {
            if (option_debug)
                opbx_log(LOG_DEBUG, "Using channel %d\n", p->channel);
            /*endif*/
            if (p->reserved  ||  p->current_alarms)
                continue;
            /*endif*/
            if (p->uc)
            {
#if 0
                if ((p->call = uc_new_call(p->uc, NULL)) == NULL)
                {
                    opbx_log(LOG_WARNING, "Unable to create call on channel %d\n", p->channel);
                    break;
                }
                /*endif*/
#endif
            }
            /*endif*/
            callwait = (p->owner != NULL);
            if (p->channel == CHAN_PSEUDO)
            {
                if ((p = chandup(p)) == NULL)
                    break;
                /*endif*/
            }
            /*endif*/
            if (p->owner)
            {
                if (alloc_sub(p, SUB_CALLWAIT))
                {
                    p = NULL;
                    break;
                }
                /*endif*/
            }
            /*endif*/
            tmp = unicall_new(p, OPBX_STATE_RESERVED, 0, p->owner  ?  SUB_CALLWAIT  :  SUB_REAL, 0);
            /* Make special notes */
            if (res > 1)
            {
                if (opt == 'c')
                {
                    /* Confirm answer */
                    p->confirmanswer = 1;
                }
                else if (opt == 'r')
                {
                    /* Distinctive ring */
                    if (res < 3)
                        opbx_log(LOG_WARNING, "Distinctive ring missing identifier in '%s'\n", (char *) data);
                    else
                        p->distinctivering = y;
                    /*endif*/
                }
                else if (opt == 'd')
                {
                    /* If this is an ISDN call, make it digital */
                    p->digital = 1;
                }
                else
                {
                    opbx_log(LOG_WARNING, "Unknown option '%c' in '%s'\n", opt, (char *) data);
                }
                /*endif*/
            }
            /*endif*/
            /* Note if the call is a call waiting call */
            if (callwait)
                tmp->cdrflags |= OPBX_CDR_CALLWAIT;
            /*endif*/
            break;
        }
        /*endif*/
		if (backwards)
        {
			if ((p = p->prev) == NULL)
				p = end;
            /*endif*/
		}
        else
        {
			if ((p = p->next) == NULL)
				p = start;
            /*endif*/
    	}
        /*endif*/
		/* Stop when we get back where we started */
		if (p == exit)
			break;
        /*endif*/
    }
    /*endwhile*/
    opbx_mutex_unlock(lock);
    restart_monitor();
	if (callwait  ||  (tmp == NULL  &&  busy))
		*cause = OPBX_CAUSE_BUSY;
    return tmp;
}

static int get_group(char *s)
{
    char *copy;
    char *piece;
    char *c = NULL;
    int start = 0;
    int finish = 0;
    int x;
    int group = 0;

    if ((copy = strdup(s)) == NULL)
    {
        opbx_log(LOG_ERROR, "Out of memory\n");
        return 0;
    }
    /*endif*/
    c = copy;
    piece = strsep(&c, ",");
    while (piece)
    {
        if (sscanf(piece, "%d-%d", &start, &finish) == 2)
        {
            /* Range */
        }
        else if (sscanf(piece, "%d", &start))
        {
            /* Just one */
            finish = start;
        }
        else
        {
            opbx_log(LOG_ERROR, "Syntax error parsing '%s' at '%s'.  Using '0'\n", s,piece);
            return 0;
        }
        piece = strsep(&c, ",");
        for (x = start;  x <= finish;  x++)
        {
            if (x > 31  ||  x < 0)
                opbx_log(LOG_WARNING, "Ignoring invalid group %d\n", x);
            else
                group |= (1 << x);
            /*endif*/
        }
        /*endfor*/
    }
    /*endwhile*/
    free(copy);
    return group;
}

static char *complete_span(char *line, char *word, int pos, int state)
{
    int span;
    char tmp[50];

    for (span = 1;  span <= NUM_SPANS;  span++)
    {
        if (span > state)
        {
            snprintf(tmp, sizeof(tmp), "%d", span);
            return strdup(tmp);
        }
        /*endif*/
        span++;
    }
    /*endfor*/
    return NULL;
}

static int handle_uc_debug(int fd, int argc, char *argv[])
{
    int chan;
    struct unicall_pvt *tmp = NULL;

    if (argc < 4)
        return RESULT_SHOWUSAGE;
    /*endif*/
    chan = atoi(argv[3]);
    if (chan < 1  ||  chan > NUM_SPANS)
    {
        opbx_cli(fd, "Invalid span %s.  Should be a number %d to %d\n", argv[3], 1, NUM_SPANS);
        return RESULT_SUCCESS;
    }
    /*endif*/
    for (tmp = iflist;  tmp;  tmp = tmp->next)
    {
        if (tmp->channel == chan)
        {
            if (tmp->uc)
            {
                uc_set_logging(tmp->uc, logging_level, 0, NULL);
                opbx_cli(fd, "Enabled debugging on channel %d\n", chan);
                return RESULT_SUCCESS;
            }
            /*endif*/
            break;
        }
        /*endif*/
    }
    /*endfor*/
    if (tmp) 
        opbx_cli(fd, "UniCall not running on channel %d\n", chan);
    else
        opbx_cli(fd, "No such zap channel %d\n", chan);
    /*endif*/
    return RESULT_SUCCESS;
}

static int handle_uc_no_debug(int fd, int argc, char *argv[])
{
    int chan;
    struct unicall_pvt *tmp;

    if (argc < 5)
        return RESULT_SHOWUSAGE;
    /*endif*/
    chan = atoi(argv[4]);
    if (chan < 1  ||  chan > NUM_SPANS)
    {
        opbx_cli(fd, "Invalid channel %d.  Should be a number greater than 0\n", chan);
        return RESULT_SUCCESS;
    }
    /*endif*/
    for (tmp = iflist;  tmp;  tmp = tmp->next)
    {
        if (tmp->channel == chan)
        {
            if (tmp->uc)
            {
                uc_set_logging(tmp->uc, 0, 0, NULL);
                opbx_cli(fd, "Disabled debugging on channel %d\n", chan);
                return RESULT_SUCCESS;
            }
            /*endif*/
            break;
        }
        /*endif*/
    }
    /*endwhile*/
    if (tmp) 
        opbx_cli(fd, "UniCall not running on channel %d\n", chan);
    else
        opbx_cli(fd, "No such zap channel %d\n", chan);
    /*endif*/
    return RESULT_SUCCESS;
}

static char uc_debug_help[] = 
    "Usage: UC debug span <span>\n"
    "       Enables debugging on a given PRI span\n";
    
static char uc_no_debug_help[] = 
    "Usage: UC no debug span <span>\n"
    "       Disables debugging on a given PRI span\n";

static struct opbx_cli_entry uc_debug =
{
    { "UC", "debug", "span", NULL }, handle_uc_debug, "Enables UC debugging on a span", uc_debug_help, complete_span 
};

static struct opbx_cli_entry uc_no_debug =
{
    { "UC", "no", "debug", "span", NULL }, handle_uc_no_debug, "Disables PRI debugging on a span", uc_no_debug_help, complete_span
};

static int unicall_destroy_channel(int fd, int argc, char **argv)
{
    struct unicall_pvt *tmp;
    struct unicall_pvt *prev;
    int channel;
    
    if (argc != 4)
        return RESULT_SHOWUSAGE;
    /*endif*/
    channel = atoi(argv[3]);

    for (prev = NULL, tmp = iflist;  tmp;  prev = tmp, tmp = tmp->next)
    {
        if (tmp->channel == channel)
        {
            destroy_channel(prev, tmp, 1);
            return RESULT_SUCCESS;
        }
        /*endif*/
    }
    /*endfor*/
    return RESULT_FAILURE;
}

static int unicall_show_channels(int fd, int argc, char **argv)
{
#define FORMAT1 "%7s %-10.10s %-15.15s %-10.10s %-10.10s %-20.20s\n"
#define FORMAT2 "%7s %-10.10s %-15.15s %-10.10s %-10.10s %-20.20s\n"
    struct unicall_pvt *tmp;
    char tmp1x[20];
    char *tmp1;
    char tmp2x[20];
    char *tmp2;

    if (argc != 3)
        return RESULT_SHOWUSAGE;
    /*endif*/
    opbx_mutex_lock(&iflock);
    opbx_cli(fd, FORMAT2, "Channel", "Extension", "Context", "Status", "Language", "MusicOnHold");
    
    for (tmp = iflist;  tmp;  tmp = tmp->next)
    {
        if (tmp->channel > 0)
            snprintf(tmp1 = tmp1x, sizeof(tmp1x), "%d", tmp->channel);
        else
            tmp1 = "Pseudo";
        if (tmp->blocked)
            tmp2 = "Blocked";
        else if (tmp->crn == 0)
            tmp2 = "Idle";
        else
            snprintf(tmp2 = tmp2x, sizeof(tmp2x), "%d", tmp->crn);
        opbx_cli(fd, FORMAT1, tmp1, tmp->exten, tmp->context, tmp2, tmp->language, tmp->musicclass);
    }
    /*endfor*/
    opbx_mutex_unlock(&iflock);
    return RESULT_SUCCESS;
#undef FORMAT1
#undef FORMAT2
}

static int unicall_show_channel(int fd, int argc, char **argv)
{
    struct unicall_pvt *tmp = NULL;
    int channel;
    int x;

    if (argc != 4)
        return RESULT_SHOWUSAGE;
    /*endif*/
    channel = atoi(argv[3]);

    opbx_mutex_lock(&iflock);
    for (tmp = iflist;  tmp;  tmp = tmp->next)
    {
        if (tmp->channel == channel)
        {
            opbx_cli(fd, "Channel: %d\n", tmp->channel);
            opbx_cli(fd, "File descriptor: %d\n", tmp->subs[SUB_REAL].fd);
            opbx_cli(fd, "Group: %d\n", tmp->group);
            opbx_cli(fd, "Extension: %s\n", tmp->exten);
            opbx_cli(fd, "Context: %s\n", tmp->context);
            opbx_cli(fd, "Destroy: %d\n", tmp->destroy);
            opbx_cli(fd, "Signalling type: %s\n", tmp->protocol_class);
            opbx_cli(fd, "Signalling variant: %s\n", tmp->protocol_variant);
            opbx_cli(fd, "Signalling end: %s\n", (tmp->protocol_end == UC_MODE_CO)  ?  "CO"  :  "CPE");
            opbx_cli(fd, "Owner: %s\n", (tmp->owner)  ?  tmp->owner->name  :  "<None>");
            opbx_cli(fd, "Real: %s%s\n", (tmp->subs[SUB_REAL].owner)  ?  tmp->subs[SUB_REAL].owner->name  :  "<None>", (tmp->subs[SUB_REAL].inthreeway)  ?  " (Confed)"  :  "");
            opbx_cli(fd, "Callwait: %s%s\n", (tmp->subs[SUB_CALLWAIT].owner)  ?  tmp->subs[SUB_CALLWAIT].owner->name  :  "<None>", (tmp->subs[SUB_CALLWAIT].inthreeway)  ?  " (Confed)"  :  "");
            opbx_cli(fd, "Threeway: %s%s\n", (tmp->subs[SUB_THREEWAY].owner)  ?  tmp->subs[SUB_THREEWAY].owner->name  :  "<None>", (tmp->subs[SUB_THREEWAY].inthreeway)  ?  " (Confed)"  :  "");
            opbx_cli(fd, "Confno: %d\n", tmp->confno);
            opbx_cli(fd, "Propagated conference: %d\n", tmp->propconfno);
            opbx_cli(fd, "Real in conference: %d\n", tmp->inconference);
            opbx_cli(fd, "Dialing: %d\n", tmp->dialing);
            opbx_cli(fd, "Default law: %s\n", (tmp->law == UC_CODEC_ULAW)  ?  "ulaw"  :  (tmp->law == UC_CODEC_ALAW)  ?  "alaw"  :  "unknown");
            opbx_cli(fd, "Fax handled: %s\n", (tmp->faxhandled)  ?  "yes"  :  "no");
            if (tmp->master)
                opbx_cli(fd, "Master Channel: %d\n", tmp->master->channel);
            /*endif*/
            for (x = 0;  x < MAX_SLAVES;  x++)
            {
                if (tmp->slaves[x])
                    opbx_cli(fd, "Slave Channel: %d\n", tmp->slaves[x]->channel);
                /*endif*/
            }
            /*endfor*/
            if (tmp->uc)
            {
                opbx_cli(fd, "UniCall flags: ");
                if (tmp->blocked)
                    opbx_cli(fd, "Blocked ");
                /*endif*/
                if (tmp->crn)
                    opbx_cli(fd, "Call ");
                /*endif*/
                opbx_cli(fd, "\n");
            }
            /*endif*/
            opbx_mutex_unlock(&iflock);
            return RESULT_SUCCESS;
        }
        /*endif*/
    }
    /*endfor*/
    
    opbx_cli(fd, "Unable to find given channel %d\n", channel);
    opbx_mutex_unlock(&iflock);
    return RESULT_FAILURE;
}

static char show_channels_usage[] =
    "Usage: UC show channels\n"
    "    Shows a list of available channels\n";

static char show_channel_usage[] =
    "Usage: UC show channel <chan num>\n"
    "    Detailed information about a given channel\n";
static char destroy_channel_usage[] =
    "Usage: UC destroy channel <chan num>\n"
    "    DON'T USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.  Immediately removes a given channel, whether it is in use or not\n";

static struct opbx_cli_entry cli_show_channels =
{ 
    {"UC", "show", "channels", NULL}, unicall_show_channels, "Show active UniCall channels", show_channels_usage, NULL
};

static struct opbx_cli_entry cli_show_channel =
{ 
    {"UC", "show", "channel", NULL}, unicall_show_channel, "Show information on a channel", show_channel_usage, NULL
};

static struct opbx_cli_entry cli_destroy_channel =
{ 
    {"UC", "destroy", "channel", NULL}, unicall_destroy_channel, "Destroy a channel", destroy_channel_usage, NULL
};

static int setup_unicall(int reload)
{
    struct unicall_pvt *tmp;
    struct opbx_config *cfg;
    struct opbx_variable *v;
    char *chan;
    char *c;
    int start;
    int finish;
    int x;
    int y;
    int cur_radio;

    /* We *must* have a config file, otherwise stop immediately */
    if ((cfg = opbx_config_load((char *) config)) == NULL)
    {
        opbx_log(LOG_ERROR, "Unable to load config %s\n", config);
        return -1;
    }
    /*endif*/

    if (opbx_mutex_lock(&iflock))
    {
        /* It's a little silly to lock it, but we might as well just to be sure */
        opbx_log(LOG_ERROR, "Unable to lock interface list???\n");
        return -1;
    }
    /*endif*/
    cur_protocol_class = NULL;
    cur_protocol_variant = NULL;
    cur_protocol_end = NULL;
    t38_support = FALSE;
    cur_radio = FALSE;

    /* Set some defaults for things that might not be specified in the config file. */
    use_callerid = TRUE;

    v = opbx_variable_browse(cfg, "channels");
    while (v)
    {
        /* Create the interface list */
        if (strcasecmp(v->name, "channel") == 0)
        {
            if (cur_protocol_class == NULL)
            {
                opbx_log(LOG_ERROR, "Signalling must be specified before any channels are.\n");
                opbx_config_destroy(cfg);
                opbx_mutex_unlock(&iflock);
                unload_module();
                return -1;
            }
            /*endif*/
            c = v->value;
            chan = strsep(&c, ",");
            while (chan)
            {
                if (sscanf(chan, "%d-%d", &start, &finish) == 2)
                {
                    /* Range */
                }
                else if (sscanf(chan, "%d", &start))
                {
                    /* Just one */
                    finish = start;
                }
                else if (strcasecmp(chan, "pseudo") == 0)
                {
                    finish =
                    start = CHAN_PSEUDO;
                }
                else
                {
                    opbx_log(LOG_ERROR, "Syntax error parsing '%s' at '%s'\n", v->value, chan);
                    opbx_config_destroy(cfg);
                    opbx_mutex_unlock(&iflock);
                    unload_module();
                    return -1;
                }
                /*endif*/
                if (finish < start)
                {
                    opbx_log(LOG_WARNING, "Sillyness: %d < %d\n", start, finish);
                    /* Just swap them */
                    x = finish;
                    finish = start;
                    start = x;
                }
                /*endif*/
                for (x = start;  x <= finish;  x++)
                {
                    if ((tmp = mkintf(x, cur_protocol_class, cur_protocol_variant, cur_protocol_end, cur_radio)) == NULL)
                    {
                        opbx_log(LOG_ERROR, "Unable to register channel '%s'\n", v->value);
                        opbx_config_destroy(cfg);
                        opbx_mutex_unlock(&iflock);
                        unload_module();
                        return -1;
                    }
                    /*endif*/
                    if (option_verbose > 2)
                        opbx_verbose(VERBOSE_PREFIX_3 "Registered channel %d, %s signalling\n", x, tmp->protocol_class);
                    /*endif*/
                }
                /*endfor*/
                chan = strsep(&c, ",");
            }
            /*endwhile*/
        }
        else if (strcasecmp(v->name, "protocolclass") == 0)
        {
            cur_protocol_class = strdup(v->value);
            cur_radio = FALSE;
        }
        else if (strcasecmp(v->name, "protocolvariant") == 0)
        {
            cur_protocol_variant = strdup(v->value);
            cur_radio = FALSE;
        }
        else if (strcasecmp(v->name, "protocolend") == 0)
        {
            cur_protocol_end = strdup(v->value);
            cur_radio = FALSE;
        }
        else if (strcasecmp(v->name, "t38support") == 0)
        {
            t38_support = opbx_true(v->value);
        }
        else if (strcasecmp(v->name, "threewaycalling") == 0)
        {
            threewaycalling = opbx_true(v->value);
        }
        else if (strcasecmp(v->name, "cancallforward") == 0)
        {
            cancallforward = opbx_true(v->value);
        }
        else if (strcasecmp(v->name, "mailbox") == 0)
        {
            strncpy(mailbox, v->value, sizeof(mailbox) -1);
        }
        else if (strcasecmp(v->name, "adsi") == 0)
        {
            adsi = opbx_true(v->value);
        }
        else if (strcasecmp(v->name, "transfer") == 0)
        {
            transfer = opbx_true(v->value);
        }
        else if (strcasecmp(v->name, "echocancelwhenbridged") == 0)
        {
            echocanbridged = opbx_true(v->value);
        }
        else if (strcasecmp(v->name, "echocancel") == 0)
        {
            if (v->value  &&  strlen(v->value))
                y = atoi(v->value);
            else
                y = 0;
            /*endif*/
            if ((y == 32)  ||  (y == 64)  ||  (y == 128)  ||  (y == 256))
                echocancel = y;
            else
                echocancel = opbx_true(v->value);
            /*endif*/
        }
        else if (strcasecmp(v->name, "callreturn") == 0)
        {
            callreturn = opbx_true(v->value);
        }
        else if (strcasecmp(v->name, "context") == 0)
        {
            strncpy(context, v->value, sizeof(context) - 1);
        }
        else if (strcasecmp(v->name, "language") == 0)
        {
            strncpy(language, v->value, sizeof(language) - 1);
        }
        else if (strcasecmp(v->name, "musiconhold") == 0)
        {
            strncpy(musicclass, v->value, sizeof(musicclass) - 1);
        } 
        else if (strcasecmp(v->name, "stripmsd") == 0)
        {
            stripmsd = atoi(v->value);
        } 
        else if (strcasecmp(v->name, "group") == 0)
        {
            cur_group = get_group(v->value);
        } 
        else if (strcasecmp(v->name, "callgroup") == 0)
        {
            cur_callergroup = get_group(v->value);
        } 
        else if (strcasecmp(v->name, "pickupgroup") == 0)
        {
            cur_pickupgroup = get_group(v->value);
        } 
        else if (strcasecmp(v->name, "immediate") == 0)
        {
            immediate = opbx_true(v->value);
        } 
        else if (strcasecmp(v->name, "rxgain") == 0)
        {
            if (sscanf(v->value, "%f", &rxgain) != 1)
                opbx_log(LOG_WARNING, "Invalid rxgain: %s\n", v->value);
            /*endif*/
        } 
        else if (strcasecmp(v->name, "txgain") == 0)
        {
            if (sscanf(v->value, "%f", &txgain) != 1)
                opbx_log(LOG_WARNING, "Invalid txgain: %s\n", v->value);
            /*endif*/
        } 
        else if (strcasecmp(v->name, "accountcode") == 0)
        {
            strncpy(accountcode, v->value, sizeof(accountcode) - 1);
        } 
        else if (strcasecmp(v->name, "amaflags") == 0)
        {
            y = opbx_cdr_amaflags2int(v->value);
            if (y < 0) 
                opbx_log(LOG_WARNING, "Invalid AMA flags: %s at line %d\n", v->value, v->lineno);
            else
                amaflags = y;
            /*endif*/
        } 
        else if (strcasecmp(v->name, "minunused") == 0)
        {
            minunused = atoi(v->value);
        }
        else if (strcasecmp(v->name, "idleext") == 0)
        {
            strncpy(idleext, v->value, sizeof(idleext) - 1);
        }
        else if (strcasecmp(v->name, "idledial") == 0)
        {
            strncpy(idledial, v->value, sizeof(idledial) - 1);
        }
        else if (strcasecmp(v->name, "usecallerid") == 0)
        {
            use_callerid = opbx_true(v->value);
        }
        else if (strcasecmp(v->name, "callerid") == 0)
        {
            if (strcasecmp(v->value, "asreceived") == 0)
                callerid[0] = '\0';
            else
                strncpy(callerid, v->value, sizeof(callerid) - 1);
            /*endif*/
            callerid[sizeof(callerid) - 1] = '\0';
        }
        else if (strcasecmp(v->name, "supertones") == 0)
        {
            strncpy(super_tones, v->value, sizeof(super_tones) - 1);
        }
        else if (strcasecmp(v->name, "loglevel") == 0)
        {
            logging_level = atoi(v->value) & UC_LOG_SEVERITY_MASK;
            logging_level |= (UC_LOG_SHOW_TAG | UC_LOG_SHOW_PROTOCOL);
        }
        else
        {
            opbx_log(LOG_DEBUG, "Ignoring %s\n", v->name);
        }
        /*endif*/
        v = v->next;
    }
    /*endwhile*/
    if ((set = get_supervisory_tone_set(super_tones)) == NULL)
    {
        opbx_log(LOG_ERROR, "Unable to read supervisory tone set %s\n", super_tones);
        opbx_config_destroy(cfg);
        unload_module();
        return -1;
    }
    /*endif*/
    opbx_mutex_unlock(&iflock);

    /* Make sure we can register our UC channel type */
    if (opbx_channel_register(&unicall_tech))
    {
        opbx_log(LOG_ERROR, "Unable to register channel class %s\n", type);
        opbx_config_destroy(cfg);
        unload_module();
        return -1;
    }
    /*endif*/
    return 0;
}

int load_module(void)
{
    uc_start();
    uc_set_error_handler(unicall_report);
    uc_set_message_handler(unicall_message);

    if (setup_unicall(0))
        return -1;
    /*endif*/

    opbx_cli_register(&uc_debug);
    opbx_cli_register(&uc_no_debug);

    opbx_cli_register(&cli_show_channels);
    opbx_cli_register(&cli_show_channel);
    opbx_cli_register(&cli_destroy_channel);

    /* And start the monitor for the first time */
    restart_monitor();
    return 0;
}

int unload_module(void)
{
    struct unicall_pvt *p;
    struct unicall_pvt *pl;

    /* First, take us out of the channel loop */
    opbx_channel_unregister(&unicall_tech);
    opbx_cli_unregister(&cli_show_channels);
    opbx_cli_unregister(&cli_show_channel);
    opbx_cli_unregister(&cli_destroy_channel);
    if (opbx_mutex_lock(&iflock))
    {
        opbx_log(LOG_WARNING, "Unable to lock the monitor\n");
        return -1;
    }
    /*endif*/
    /* Hangup all interfaces if they have an owner */
    for (p = iflist;  p;  p = p->next)
    {
        if (p->owner)
            opbx_softhangup(p->owner, OPBX_SOFTHANGUP_APPUNLOAD);
        /*endif*/
    }
    /*endfor*/
    iflist = NULL;
    opbx_mutex_unlock(&iflock);
    if (opbx_mutex_lock(&monlock))
    {
        opbx_log(LOG_WARNING, "Unable to lock the monitor\n");
        return -1;
    }
    /*endif*/
    if (monitor_thread  &&  (monitor_thread != OPBX_PTHREADT_STOP)  &&  (monitor_thread != OPBX_PTHREADT_NULL))
    {
        pthread_cancel(monitor_thread);
        pthread_kill(monitor_thread, SIGURG);
        pthread_join(monitor_thread, NULL);
    }
    /*endif*/
    monitor_thread = OPBX_PTHREADT_STOP;
    opbx_mutex_unlock(&monlock);

    if (opbx_mutex_lock(&iflock))
    {
        opbx_log(LOG_WARNING, "Unable to lock the monitor\n");
        return -1;
    }
    /*endif*/
    /* Destroy all the interfaces and free their memory */
    for (p = iflist;  p;  )
    {
        /* Close the zapata thingy */
        if (p->subs[SUB_REAL].fd >= 0)
            unicall_close(p->subs[SUB_REAL].fd);
        /*endif*/
        pl = p;
        p = p->next;
        /* Free associated memory */
        free(pl);
    }
    /*endfor*/
    iflist = NULL;
    opbx_mutex_unlock(&iflock);
    return 0;
}

static int unicall_send_text(struct opbx_channel *c, const char *text)
{
    struct unicall_pvt *p = c->tech_pvt;
    int index;
    int ret;
    uc_usertouser_t msg;

    if ((index = unicall_get_index(c, p, 0)) < 0)
    {
        opbx_log(LOG_WARNING, "Huh?  I don't exist?\n");
        return -1;
    }
    /*endif*/
    if (text[0] == '\0')
        return 0;
    /*endif*/
    msg.len = strlen(text);
    msg.message = (unsigned char *) text;
    if ((ret = uc_call_control(p->uc, UC_OP_USERTOUSER, 0, &msg)) < 0)
    {
        opbx_log(LOG_WARNING, "User to user failed - %s\n", uc_ret2str(ret));
        return -1;
    }
    /*endif*/
    return  0;
}

int reload(void)
{
    int res;

    if ((res = setup_unicall(1)))
    {
        opbx_log(LOG_WARNING, "Reload of chan_unicall.so is unsuccessful!\n");
        return -1;
    }
    return 0;
}

int usecount(void)
{
    int res;

    opbx_mutex_lock(&usecnt_lock);
    res = usecnt;
    opbx_mutex_unlock(&usecnt_lock);
    return res;
}

char *description(void)
{
    return (char *) desc;
}
