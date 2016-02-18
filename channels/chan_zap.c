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
 * DAHDI Pseudo TDM interface 
 * 
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#ifdef __NetBSD__
#include <pthread.h>
#include <signal.h>
#else
#include <sys/signal.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/ioctl.h>
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>

#include <math.h>
#include DAHDI_H
#include TONEZONE_H
#include <ctype.h>
#ifdef ZAPATA_PRI
#include <libpri.h>
#ifndef PRI_USER_USER_TX
#error "You need newer libpri"
#endif
#endif

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/channel.h"
#include "callweaver/config.h"
#include "callweaver/logger.h"
#include "callweaver/module.h"
#include "callweaver/pbx.h"
#include "callweaver/options.h"
#include "callweaver/file.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/callerid.h"
#include "callweaver/cli.h"
#include "callweaver/cdr.h"
#include "callweaver/features.h"
#include "callweaver/musiconhold.h"
#include "callweaver/say.h"
#include "callweaver/app.h"
#include "callweaver/dsp.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/manager.h"
#include "callweaver/causes.h"
#include "callweaver/utils.h"
#include "callweaver/transcap.h"
#include "callweaver/keywords.h"

#include "callweaver/callweaver_pcm.h"

#include "callweaver/generic_jb.h"

#include "callweaver_addon/adsi.h"


static struct cw_jb_conf global_jbconf;

#if !defined(FALSE)
#define FALSE 0
#endif
#if !defined(TRUE)
#define TRUE (!FALSE)
#endif

#ifndef DAHDI_TONEDETECT
/* Work around older code with no tone detect */
#define DAHDI_EVENT_DTMFDOWN 0
#define DAHDI_EVENT_DTMFUP 0
#endif

/* Copied from dahdi/kernel.h */
#define DAHDI_DEFAULT_RINGTIME 2000
#define DAHDI_RINGOFFTIME 4000

/*
 * Define ZHONE_HACK to cause us to go off hook and then back on hook when
 * the user hangs up to reset the state machine so ring works properly.
 * This is used to be able to support kewlstart by putting the zhone in
 * groundstart mode since their forward disconnect supervision is entirely
 * broken even though their documentation says it isn't and their support
 * is entirely unwilling to provide any assistance with their channel banks
 * even though their web site says they support their products for life.
 */

/* #define ZHONE_HACK */

/*
 * Define if you want to check the hook state for an FXO (FXS signalled) interface
 * before dialing on it.  Certain FXO interfaces always think they're out of
 * service with this method however.
 */
/* #define DAHDI_CHECK_HOOKSTATE */


#define CHANNEL_PSEUDO -12

#define CW_LAW(p) (((p)->law == DAHDI_LAW_MULAW) ? CW_FORMAT_ULAW : CW_FORMAT_ALAW)

/* Signaling types that need to use MF detection should be placed in this macro */
#define NEED_MFDETECT(p) (((p)->sig == SIG_FEATDMF) || ((p)->sig == SIG_FEATDMF_TA) || ((p)->sig == SIG_E911) || ((p)->sig == SIG_FEATB)) 

static const char desc[] = "DAHDI Telephony"
#ifdef ZAPATA_PRI
               " w/PRI"
#endif
;

static const char tdesc[] = "DAHDI Telephony Driver"
#ifdef ZAPATA_PRI
               " w/PRI"
#endif
;

#define SIG_EM		DAHDI_SIG_EM
#define SIG_EMWINK 	(0x0100000 | DAHDI_SIG_EM)
#define SIG_FEATD	(0x0200000 | DAHDI_SIG_EM)
#define	SIG_FEATDMF	(0x0400000 | DAHDI_SIG_EM)
#define	SIG_FEATB	(0x0800000 | DAHDI_SIG_EM)
#define	SIG_E911	(0x1000000 | DAHDI_SIG_EM)
#define	SIG_FEATDMF_TA	(0x2000000 | DAHDI_SIG_EM)
#define SIG_FXSLS	DAHDI_SIG_FXSLS
#define SIG_FXSGS	DAHDI_SIG_FXSGS
#define SIG_FXSKS	DAHDI_SIG_FXSKS
#define SIG_FXOLS	DAHDI_SIG_FXOLS
#define SIG_FXOGS	DAHDI_SIG_FXOGS
#define SIG_FXOKS	DAHDI_SIG_FXOKS
#define SIG_PRI		DAHDI_SIG_CLEAR
#define	SIG_SF		DAHDI_SIG_SF
#define SIG_SFWINK 	(0x0100000 | DAHDI_SIG_SF)
#define SIG_SF_FEATD	(0x0200000 | DAHDI_SIG_SF)
#define	SIG_SF_FEATDMF	(0x0400000 | DAHDI_SIG_SF)
#define	SIG_SF_FEATB	(0x0800000 | DAHDI_SIG_SF)
#define SIG_EM_E1	DAHDI_SIG_EM_E1
#define SIG_GR303FXOKS	(0x0100000 | DAHDI_SIG_FXOKS)
#define SIG_GR303FXSKS	(0x0100000 | DAHDI_SIG_FXSKS)

#define CID_START_RING		1
#define CID_START_POLARITY	2
#define CID_START_IDLE		3

#define NUM_SPANS 		32
#define NUM_DCHANS		4		/* No more than 4 d-channels */
#define MAX_CHANNELS	672		/* No more than a DS3 per trunk group */

#define CHAN_PSEUDO	-2

#define DCHAN_PROVISIONED (1 << 0)
#define DCHAN_NOTINALARM  (1 << 1)
#define DCHAN_UP          (1 << 2)

#define DCHAN_AVAILABLE	(DCHAN_PROVISIONED | DCHAN_NOTINALARM | DCHAN_UP)

static char defaultcic[64];
static char defaultozz[64];

static char progzone[10];

static int cur_signalling = -1;

static cw_group_t cur_group = 0;
static cw_group_t cur_callergroup = 0;
static cw_group_t cur_pickupgroup = 0;

static int numbufs = 4;

static int cur_prewink = -1;
static int cur_preflash = -1;
static int cur_wink = -1;
static int cur_flash = -1;
static int cur_start = -1;
static int cur_rxwink = -1;
static int cur_rxflash = -1;
static int cur_debounce = -1;

#ifdef ZAPATA_PRI
static int minunused = 2;
static int minidle = 0;
static char idleext[CW_MAX_EXTENSION];
static char idledial[CW_MAX_EXTENSION];
static int overlapdial = 0;
static int facilityenable = 0;
static char internationalprefix[10] = "";
static char nationalprefix[10] = "";
static char localprefix[20] = "";
static char privateprefix[20] = "";
static char unknownprefix[20] = "";
static long resetinterval = 3600;			/* How often (in seconds) to reset unused channels. Default 1 hour. */
static struct cw_channel inuse = { .name = "GR-303InUse" };
#ifdef PRI_GETSET_TIMERS
static int pritimers[PRI_MAX_TIMERS];
#endif
static int pridebugfd = -1;
static char pridebugfilename[1024]="";
#endif

/* Wait up to 16 seconds for first digit (FXO logic) */
static int firstdigittimeout = 16000;

/* How long to wait for following digits (FXO logic) */
static int gendigittimeout = 8000;

/* How long to wait for an extra digit, if there is an ambiguous match */
static int matchdigittimeout = 3000;

/* Protect the interface list (of dahdi_pvt's) */
CW_MUTEX_DEFINE_STATIC(iflock);


static int ifcount = 0;

#ifdef ZAPATA_PRI
CW_MUTEX_DEFINE_STATIC(pridebugfdlock);
#endif


/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
CW_MUTEX_DEFINE_STATIC(monlock);

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = CW_PTHREADT_NULL;

static int restart_monitor(void);

static enum cw_bridge_result dahdi_bridge(struct cw_channel *c0, struct cw_channel *c1, int flags, struct cw_frame **fo, struct cw_channel **rc, int timeoutms);

static int dahdi_sendtext(struct cw_channel *c, const char *text);

static inline int dahdi_get_event(int fd)
{
	/* Avoid the silly dahdi_getevent which ignores a bunch of events */
	int j;
	if (ioctl(fd, DAHDI_GETEVENT, &j) == -1) return -1;
	return j;
}

static inline int dahdi_wait_event(int fd)
{
	/* Avoid the silly dahdi_waitevent which ignores a bunch of events */
	int i,j=0;
	i = DAHDI_IOMUX_SIGEVENT;
	if (ioctl(fd, DAHDI_IOMUX, &i) == -1) return -1;
	if (ioctl(fd, DAHDI_GETEVENT, &j) == -1) return -1;
	return j;
}

/* Chunk size to read -- we use 20ms chunks to make things happy.  */   
#define READ_SIZE 160

#define MASK_AVAIL		(1 << 0)		/* Channel available for PRI use */
#define MASK_INUSE		(1 << 1)		/* Channel currently in use */

/* The DAHDI driver currently makes this a fixed value but
 * has comments suggesting it may one day be variable.
 */
#ifndef DAHDI_CHUNKSIZE
#  define DAHDI_CHUNKSIZE	8
#endif

/* Convert milliseconds to samples, assuming the driver is
 * sampling at a standard 64kbit/s.
 */
#define MS_TO_SAMPLES(X)	((X) * (64 / DAHDI_CHUNKSIZE))
#define SAMPLES_TO_MS(X)	((X) / (64 / DAHDI_CHUNKSIZE))

#define CALLWAITING_REPEAT_SAMPLES	MS_TO_SAMPLES(10000)	/* 10 s */
#define CIDCW_EXPIRE_SAMPLES		MS_TO_SAMPLES(500)	/* 500 ms */
#define DEFAULT_RINGT 			MS_TO_SAMPLES(8000)	/* 8 s */
#define MIN_MS_SINCE_FLASH		2000			/* 2 s */

struct dahdi_pvt;


static int ringt_base = DEFAULT_RINGT;

#ifdef ZAPATA_PRI

#define PVT_TO_CHANNEL(p) (((p)->prioffset) | ((p)->logicalspan << 8) | (p->pri->mastertrunkgroup ? 0x10000 : 0))
#define PRI_CHANNEL(p) ((p) & 0xff)
#define PRI_SPAN(p) (((p) >> 8) & 0xff)
#define PRI_EXPLICIT(p) (((p) >> 16) & 0x01)

struct dahdi_pri {
	pthread_t master;						/* Thread of master */
	cw_mutex_t lock;						/* Mutex */
	char idleext[CW_MAX_EXTENSION];				/* Where to idle extra calls */
	char idlecontext[CW_MAX_CONTEXT];				/* What context to use for idle */
	char idledial[CW_MAX_EXTENSION];				/* What to dial before dumping */
	int minunused;							/* Min # of channels to keep empty */
	int minidle;							/* Min # of "idling" calls to keep active */
	int nodetype;							/* Node type */
	int switchtype;							/* Type of switch to emulate */
	int nsf;							/* Network-Specific Facilities */
	int dialplan;							/* Dialing plan */
	int localdialplan;						/* Local dialing plan */
	char internationalprefix[10];					/* country access code ('00' for european dialplans) */
	char nationalprefix[10];					/* area access code ('0' for european dialplans) */
	char localprefix[20];						/* area access code + area code ('0'+area code for european dialplans) */
	char privateprefix[20];						/* for private dialplans */
	char unknownprefix[20];						/* for unknown dialplans */
	int dchannels[NUM_DCHANS];					/* What channel are the dchannels on */
	int trunkgroup;							/* What our trunkgroup is */
	int mastertrunkgroup;						/* What trunk group is our master */
	int prilogicalspan;						/* Logical span number within trunk group */
	int numchans;							/* Num of channels we represent */
	int overlapdial;						/* In overlap dialing mode */
	int facilityenable;						/* Enable facility IEs */
	struct pri *dchans[NUM_DCHANS];					/* Actual d-channels */
	int dchanavail[NUM_DCHANS];					/* Whether each channel is available */
	struct pri *pri;						/* Currently active D-channel */
	int debug;
	int fds[NUM_DCHANS];						/* FD's for d-channels */
	int offset;
	int span;
	int resetting;
	int resetpos;
	time_t lastreset;						/* time when unused channels were last reset */
	long resetinterval;						/* Interval (in seconds) for resetting unused channels */
	struct dahdi_pvt *pvts[MAX_CHANNELS];				/* Member channel pvt structs */
	struct dahdi_pvt *crvs;						/* Member CRV structs */
	struct dahdi_pvt *crvend;						/* Pointer to end of CRV structs */
};


static struct dahdi_pri pris[NUM_SPANS];

static int pritype = PRI_CPE;

#if 0
#define DEFAULT_PRI_DEBUG (PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q921_DUMP | PRI_DEBUG_Q921_RAW | PRI_DEBUG_Q921_STATE)
#else
#define DEFAULT_PRI_DEBUG 0
#endif

static inline void pri_rel(struct dahdi_pri *pri)
{
	cw_mutex_unlock(&pri->lock);
}

static int switchtype = PRI_SWITCH_NI2;
static int nsf = PRI_NSF_NONE;
static int dialplan = PRI_NATIONAL_ISDN + 1;
static int localdialplan = PRI_NATIONAL_ISDN + 1;

#else
/* Shut up the compiler */
struct dahdi_pri;
#endif

#define SUB_REAL		0			/* Active call */
#define SUB_CALLWAIT	1			/* Call-Waiting call on hold */
#define SUB_THREEWAY	2			/* Three-way call */

/* Polarity states */
#define POLARITY_IDLE   0
#define POLARITY_REV    1


static struct dahdi_distRings drings;

struct distRingData {
	int ring[3];
};
struct ringContextData {
	char contextData[CW_MAX_CONTEXT];
	char extenData[CW_MAX_EXTENSION];
};
struct dahdi_distRings {
	struct distRingData ringnum[3];
	struct ringContextData ringContext[3];
};

static const char *subnames[] = {
	"Real",
	"Callwait",
	"Threeway"
};

struct dahdi_subchannel {
	int dfd;
	struct cw_channel *owner;
	int chan;
	short buffer[CW_FRIENDLY_OFFSET/2 + READ_SIZE];
	struct cw_frame f;		/* One frame for each channel.  How did this ever work before? */
	unsigned int needringing:1;
	unsigned int needbusy:1;
	unsigned int needcongestion:1;
	unsigned int needcallerid:1;
	unsigned int needanswer:1;
	unsigned int needflash:1;
	unsigned int linear:1;
	unsigned int inthreeway:1;
	struct dahdi_confinfo curconf;
};

#define CONF_USER_REAL		(1 << 0)
#define CONF_USER_THIRDCALL	(1 << 1)

#define MAX_SLAVES	4

static struct dahdi_pvt {
	cw_mutex_t lock;
	struct cw_channel *owner;			/* Our current active owner (if applicable) */
							/* Up to three channels can be associated with this call */
		
	struct dahdi_subchannel sub_unused;		/* Just a safety precaution */
	struct dahdi_subchannel subs[3];			/* Sub-channels */
	struct dahdi_confinfo saveconf;			/* Saved conference info */

	struct dahdi_pvt *slaves[MAX_SLAVES];		/* Slave to us (follows our conferencing) */
	struct dahdi_pvt *master;				/* Master to us (we follow their conferencing) */
	int inconference;				/* If our real should be in the conference */
	
	int sig;					/* Signalling style */
	int radio;					/* radio type */
	float cid_rxgain;				/* Gain to apply during CID detection */
	float rxgain;
	float txgain;
	int tonezone;					/* tone zone for this chan, or -1 for default */
	struct dahdi_pvt *next;				/* Next channel in list */
	struct dahdi_pvt *prev;				/* Prev channel in list */

	/* flags */
	unsigned int adsi:1;
	unsigned int answeronpolarityswitch:1;
	unsigned int busydetect:1;
	unsigned int callreturn:1;
	unsigned int callwaiting:1;
	unsigned int callwaitingcallerid:1;
	unsigned int cancallforward:1;
	unsigned int canpark:1;
	unsigned int confirmanswer:1;			/* Wait for '#' to confirm answer */
	unsigned int destroy:1;
	unsigned int didtdd:1;				/* flag to say its done it once */
	unsigned int dialednone:1;
	unsigned int dialing:1;
	unsigned int digital:1;
	unsigned int dnd:1;
	unsigned int echobreak:1;
	unsigned int echocanbridged:1;
	unsigned int echocanon:1;
	unsigned int faxhandled:1;			/* Has a fax tone already been handled? */
	unsigned int firstradio:1;
	unsigned int hanguponpolarityswitch:1;
	unsigned int hardwaredtmf:1;
	unsigned int hidecallerid;
	unsigned int ignoredtmf:1;
	unsigned int immediate:1;			/* Answer before getting digits? */
	unsigned int inalarm:1;
	unsigned int mate:1;				/* flag to say its in MATE mode */
	unsigned int outgoing:1;
	unsigned int overlapdial:1;
	unsigned int permcallwaiting:1;
	unsigned int permhidecallerid:1;		/* Whether to hide our outgoing caller ID or not */
	unsigned int priindication_oob:1;
	unsigned int priexclusive:1;
	unsigned int pulse:1;
	unsigned int pulsedial:1;			/* whether a pulse dial phone is detected */
	unsigned int restrictcid:1;			/* Whether restrict the callerid -> only send ANI */
	unsigned int threewaycalling:1;
	unsigned int transfer:1;
	unsigned int use_callerid:1;			/* Whether or not to use caller id on this channel */
	unsigned int use_callingpres:1;			/* Whether to use the callingpres the calling switch sends */
	unsigned int usedistinctiveringdetection:1;
	unsigned int distinctiveringaftercid:1;
	unsigned int dahditrcallerid:1;			/* should we use the callerid from incoming call on DAHDI transfer or not */
	unsigned int transfertobusy:1;			/* allow flash-transfers to busy channels */
#if defined(ZAPATA_PRI)
	unsigned int alerting:1;
	unsigned int alreadyhungup:1;
	unsigned int isidlecall:1;
	unsigned int proceeding:1;
	unsigned int progress:1;
	unsigned int resetting:1;
	unsigned int setup_ack:1;
#endif
	unsigned int polarity_events:1;

	struct dahdi_distRings drings;

	char context[CW_MAX_CONTEXT];
	char defcontext[CW_MAX_CONTEXT];
	char exten[CW_MAX_EXTENSION];
	char language[MAX_LANGUAGE];
	char musicclass[MAX_MUSICCLASS];
#ifdef PRI_ANI
	char cid_ani[CW_MAX_EXTENSION];
#endif
	char cid_num[CW_MAX_EXTENSION];
	int cid_ton;					/* Type Of Number (TON) */
	char cid_name[CW_MAX_EXTENSION];
	char lastcid_num[CW_MAX_EXTENSION];
	char lastcid_name[CW_MAX_EXTENSION];
	char *origcid_num;				/* malloced original callerid */
	char *origcid_name;				/* malloced original callerid */
	char callwait_num[CW_MAX_EXTENSION];
	char callwait_name[CW_MAX_EXTENSION];
	char rdnis[CW_MAX_EXTENSION];
	char dnid[CW_MAX_EXTENSION];
	unsigned int group;
	int law;
	int confno;					/* Our conference */
	int confusers;					/* Who is using our conference */
	int propconfno;					/* Propagated conference number */
	cw_group_t callgroup;
	cw_group_t pickupgroup;
	int channel;					/* Channel Number or CRV */
	int span;					/* Span number */
	time_t guardtime;				/* Must wait this much time before using for new call */
	adsi_rx_state_t *adsi_rx1, *adsi_rx2;		/* Internal CID decoding states */
	int cid_signalling;				/* CID signalling type bell202 or v23 */
	int cid_start;					/* CID start indicator, polarity or ring */
	int cid_send_on;				/* Internal send CID on polarity or ring indicator */
	int callingpres;				/* The value of callling presentation that we're going to use when placing a PRI call */
	int callwaitingrepeat;				/* How many samples to wait before repeating call waiting */
	int cidcwexpire;				/* When to expire our muting for CID/CW */
	uint8_t *cidspill;
	int cidpos;
	int cidlen;
	uint8_t *cidspill2;
	int cidlen2;
	int ringt;
	int ringt_base;
	int stripmsd;
	int callwaitcas;
	int callwaitrings;
	int echocancel;
	int echotraining;
	char echorest[20];
	int busycount;
	int busy_tonelength;
	int busy_quietlength;
	int callprogress;
	struct timeval flashtime;			/* Last flash-hook time */
	struct cw_dsp *dsp;
	int cref;					/* Call reference number */
	struct dahdi_dialoperation dop;
	int whichwink;					/* SIG_FEATDMF_TA Which wink are we on? */
	char finaldial[64];
	char accountcode[CW_MAX_ACCOUNT_CODE];		/* Account code */
	int amaflags;					/* AMA Flags */
	struct tdd_state *tdd;				/* TDD flag */
	char call_forward[CW_MAX_EXTENSION];
	char mailbox[CW_MAX_EXTENSION];
	char dialdest[256];
	int onhooktime;
	int msgstate;
	int distinctivering;				/* Which distinctivering to use */
	int cidrings;					/* Which ring to deliver CID on */
	int dtmfrelax;					/* whether to run in relaxed DTMF mode */
	int fake_event;
	int polarityonanswerdelay;
	struct timeval polaritydelaytv;
	int sendcalleridafter;
#ifdef ZAPATA_PRI
	struct dahdi_pri *pri;
	struct dahdi_pvt *bearer;
	struct dahdi_pvt *realcall;
	q931_call *call;
	int prioffset;
	int logicalspan;
	int dsp_features;
#endif	
	int polarity;

	struct cw_jb_conf jbconf;

} *iflist = NULL, *ifend = NULL;

static struct dahdi_pvt global = {
	.accountcode = "",
	.amaflags = 0,
	.mailbox = "",

	.tonezone = -1,

	.context = "default",

	.rxgain = 0.0,
	.txgain = 0.0,

	.priindication_oob = 0,
	.priexclusive = 0,

	.adsi = 0,
	.pulse = 0,
	.polarity_events = 1,
	.answeronpolarityswitch = 0,
	.polarityonanswerdelay = 600,
	.hanguponpolarityswitch = 0,

	.busydetect = 0,
	.busycount =3,
	.busy_tonelength = 0,
	.busy_quietlength = 0,

	.callprogress = 0,

	.echocancel = 0,
	.echocanbridged = 0,
	.echotraining = 0,

	.use_callerid = 1,
	.cid_rxgain = 5.0,
	.cid_signalling = ADSI_STANDARD_CLIP,
	.cid_start = CID_START_POLARITY,
	.sendcalleridafter = -1,
	.permhidecallerid = 0,
	.hidecallerid = 0,
	.restrictcid = 0,
	.use_callingpres = 0,

	.usedistinctiveringdetection = 1,
	.distinctiveringaftercid = 1,

	.callwaiting = 0,
	.callwaitingcallerid = 0,

	.callreturn = 0,
	.threewaycalling = 0,

	.cancallforward = 0,
	.canpark = 0,
	.dahditrcallerid = 0,
	.dtmfrelax = 0,
	.immediate = 0,
	.stripmsd = 0,
	.transfer = 0,
	.transfertobusy = 1,
};

static struct cw_channel *dahdi_request(const char *type, int format, void *data, int *cause);
static int dahdi_digit(struct cw_channel *cw, char digit);
static int dahdi_sendtext(struct cw_channel *c, const char *text);
static int dahdi_call(struct cw_channel *cw, const char *rdest);
static int dahdi_hangup(struct cw_channel *cw);
static int dahdi_answer(struct cw_channel *cw);
struct cw_frame *dahdi_read(struct cw_channel *cw);
static int dahdi_write(struct cw_channel *cw, struct cw_frame *frame);
struct cw_frame *dahdi_exception(struct cw_channel *cw);
static int dahdi_indicate(struct cw_channel *chan, int condition);
static int dahdi_fixup(struct cw_channel *oldchan, struct cw_channel *newchan);
static int dahdi_setoption(struct cw_channel *chan, int option, void *data, int datalen);


static const struct cw_channel_tech dahdi_tech = {
	.type = "DAHDI",
	.description = tdesc,
	.capabilities = CW_FORMAT_SLINEAR,
	.requester = dahdi_request,
	.send_digit = dahdi_digit,
	.send_text = dahdi_sendtext,
	.call = dahdi_call,
	.hangup = dahdi_hangup,
	.answer = dahdi_answer,
	.read = dahdi_read,
	.write = dahdi_write,
	.bridge = dahdi_bridge,
	.exception = dahdi_exception,
	.indicate = dahdi_indicate,
	.fixup = dahdi_fixup,
	.setoption = dahdi_setoption,
};

#ifdef ZAPATA_PRI
#define GET_CHANNEL(p) ((p)->bearer ? (p)->bearer->channel : p->channel)
#else
#define GET_CHANNEL(p) ((p)->channel)
#endif

struct dahdi_pvt *round_robin[32];

#ifdef ZAPATA_PRI
static inline int pri_grab(struct dahdi_pvt *pvt, struct dahdi_pri *pri)
{
	int res;
	/* Grab the lock first */
	do {
	    res = cw_mutex_trylock(&pri->lock);
		if (res) {
			cw_mutex_unlock(&pvt->lock);
			/* Release the lock and try again */
			usleep(1);
			cw_mutex_lock(&pvt->lock);
		}
	} while(res);
	/* Then break the poll */
	pthread_kill(pri->master, SIGURG);
	return 0;
}
#endif

#define NUM_CADENCE_MAX 25
static int num_cadence = 4;
static int user_has_defined_cadences = 0;

static struct dahdi_ring_cadence cadences[NUM_CADENCE_MAX] = {
	{ { 125, 125, 2000, 4000 } },			/* Quick chirp followed by normal ring */
	{ { 250, 250, 500, 1000, 250, 250, 500, 4000 } }, /* British style ring */
	{ { 125, 125, 125, 125, 125, 4000 } },	/* Three short bursts */
	{ { 1000, 500, 2500, 5000 } },	/* Long ring */
};

/* cidrings says in which pause to transmit the cid information, where the first pause
 * is 1, the second pause is 2 and so on.
 */

static int cidrings[NUM_CADENCE_MAX] = {
	2,										/* Right after first long ring */
	4,										/* Right after long part */
	3,										/* After third chirp */
	2,										/* Second spell */
};

#define ISTRUNK(p) ((p->sig == SIG_FXSLS) || (p->sig == SIG_FXSKS) || \
			(p->sig == SIG_FXSGS) || (p->sig == SIG_PRI))

#define CANBUSYDETECT(p) (ISTRUNK(p) || (p->sig & (SIG_EM | SIG_EM_E1 | SIG_SF)) /* || (p->sig & __DAHDI_SIG_FXO) */)
#define CANPROGRESSDETECT(p) (ISTRUNK(p) || (p->sig & (SIG_EM | SIG_EM_E1 | SIG_SF)) /* || (p->sig & __DAHDI_SIG_FXO) */)

static int dahdi_get_index(struct cw_channel *cw, struct dahdi_pvt *p, int nullok)
{
	int res;
	if (p->subs[0].owner == cw)
		res = 0;
	else if (p->subs[1].owner == cw)
		res = 1;
	else if (p->subs[2].owner == cw)
		res = 2;
	else {
		res = -1;
		if (!nullok)
			cw_log(CW_LOG_WARNING, "Unable to get index, and nullok is not asserted\n");
	}
	return res;
}

#ifdef ZAPATA_PRI
static void wakeup_sub(struct dahdi_pvt *p, int a, struct dahdi_pri *pri)
{
	if (pri)
		cw_mutex_unlock(&pri->lock);
#else
static void wakeup_sub(struct dahdi_pvt *p, int a, void *pri)
{
	CW_UNUSED(pri);
#endif
	for (;;) {
		if (p->subs[a].owner) {
			if (cw_channel_trylock(p->subs[a].owner)) {
				cw_mutex_unlock(&p->lock);
				usleep(1);
				cw_mutex_lock(&p->lock);
			} else {
				cw_queue_frame(p->subs[a].owner, &cw_null_frame);
				cw_channel_unlock(p->subs[a].owner);
				break;
			}
		} else
			break;
	}
#ifdef ZAPATA_PRI
	if (pri)
		cw_mutex_lock(&pri->lock);
#endif			
}

#ifdef ZAPATA_PRI
static void dahdi_queue_frame(struct dahdi_pvt *p, struct cw_frame *f, struct dahdi_pri *pri)
{
	/* We must unlock the PRI to avoid the possibility of a deadlock */
	if (pri)
		cw_mutex_unlock(&pri->lock);
#else
static void dahdi_queue_frame(struct dahdi_pvt *p, struct cw_frame *f, void *pri)
{
	CW_UNUSED(pri);
#endif
	for (;;) {
		if (p->owner) {
			if (cw_channel_trylock(p->owner)) {
				cw_mutex_unlock(&p->lock);
				usleep(1);
				cw_mutex_lock(&p->lock);
			} else {
				cw_queue_frame(p->owner, f);
				cw_channel_unlock(p->owner);
				break;
			}
		} else
			break;
	}
#ifdef ZAPATA_PRI
	if (pri)
		cw_mutex_lock(&pri->lock);
#endif		
}

#ifdef ZAPATA_PRI
#define MAX_MAND_IES 10

struct msgtype {
        int msgnum;
        char *name;
        int mandies[MAX_MAND_IES];
};

static char *code2str(int code, struct msgtype *codes, int max)
{
	int x;
	for (x=0;x<max; x++)
		if (codes[x].msgnum == code)
			return codes[x].name;
	return "Unknown";
}
static char *ton2str(int plan)
{
	static struct msgtype plans[] = {
		{ PRI_UNKNOWN, "Unknown Number Type" },
		{ PRI_INTERNATIONAL_ISDN, "International Number" },
		{ PRI_NATIONAL_ISDN, "National Number" },
		{ PRI_LOCAL_ISDN, "Local Number" },
		{ PRI_PRIVATE, "Private Number" }
	};
	return code2str(plan, plans, sizeof(plans) / sizeof(plans[0]));
}
#endif

static int restore_gains(struct dahdi_pvt *p);

static void swap_subs(struct dahdi_pvt *p, int a, int b)
{
	int tchan;
	int tinthreeway;
	struct cw_channel *towner;

	cw_log(CW_LOG_DEBUG, "Swapping %d and %d\n", a, b);

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
		p->subs[a].owner->fds[0] = p->subs[a].dfd;
	if (p->subs[b].owner) 
		p->subs[b].owner->fds[0] = p->subs[b].dfd;
	wakeup_sub(p, a, NULL);
	wakeup_sub(p, b, NULL);
}

static int dahdi_open(const char *fn)
{
	int fd;
	int isnum;
	int chan = 0;
	int bs;
	int x;
	isnum = 1;
	for (x=0;x<strlen(fn);x++) {
		if (!isdigit(fn[x])) {
			isnum = 0;
			break;
		}
	}
	if (isnum) {
		chan = atoi(fn);
		if (chan < 1) {
			cw_log(CW_LOG_WARNING, "Invalid channel number '%s'\n", fn);
			return -1;
		}
		fn = "/dev/dahdi/channel";
	}
	fd = open(fn, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		cw_log(CW_LOG_WARNING, "Unable to open '%s': %s\n", fn, strerror(errno));
		return -1;
	}
	if (chan) {
		if (ioctl(fd, DAHDI_SPECIFY, &chan)) {
			x = errno;
			close(fd);
			errno = x;
			cw_log(CW_LOG_WARNING, "Unable to specify channel %d: %s\n", chan, strerror(errno));
			return -1;
		}
	}
	bs = READ_SIZE;
	if (ioctl(fd, DAHDI_SET_BLOCKSIZE, &bs) == -1) return -1;
	return fd;
}

static void dahdi_close(int fd)
{
	if(fd > 0)
		close(fd);
}

static int dahdi_setlinear(int dfd, int linear)
{
	int res;
	res = ioctl(dfd, DAHDI_SETLINEAR, &linear);
	if (res)
		return res;
	return 0;
}

#if 0
/* Currently unused */

static int dahdi_setlaw(int dfd, int law)
{
	int res;
	res = ioctl(dfd, DAHDI_SETLAW, &law);
	if (res)
		return res;
	return 0;
}
#endif

static int alloc_sub(struct dahdi_pvt *p, int x)
{
	struct dahdi_bufferinfo bi;
	int res;
	if (p->subs[x].dfd < 0) {
		p->subs[x].dfd = dahdi_open("/dev/dahdi/pseudo");
		if (p->subs[x].dfd > -1) {
			res = ioctl(p->subs[x].dfd, DAHDI_GET_BUFINFO, &bi);
			if (!res) {
				bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
				bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
				bi.numbufs = numbufs;
				res = ioctl(p->subs[x].dfd, DAHDI_SET_BUFINFO, &bi);
				if (res < 0) {
					cw_log(CW_LOG_WARNING, "Unable to set buffer policy on channel %d\n", x);
				}
			} else 
				cw_log(CW_LOG_WARNING, "Unable to check buffer policy on channel %d\n", x);
			if (ioctl(p->subs[x].dfd, DAHDI_CHANNO, &p->subs[x].chan) == 1) {
				cw_log(CW_LOG_WARNING,"Unable to get channel number for pseudo channel on FD %d\n",p->subs[x].dfd);
				dahdi_close(p->subs[x].dfd);
				p->subs[x].dfd = -1;
				return -1;
			}
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "Allocated %s subchannel on FD %d channel %d\n", subnames[x], p->subs[x].dfd, p->subs[x].chan);
			return 0;
		} else
			cw_log(CW_LOG_WARNING, "Unable to open pseudo channel: %s\n", strerror(errno));
		return -1;
	}
	cw_log(CW_LOG_WARNING, "%s subchannel of %d already in use\n", subnames[x], p->channel);
	return -1;
}

static int unalloc_sub(struct dahdi_pvt *p, int x)
{
	if (!x) {
		cw_log(CW_LOG_WARNING, "Trying to unalloc the real channel %d?!?\n", p->channel);
		return -1;
	}
	cw_log(CW_LOG_DEBUG, "Released sub %d of channel %d\n", x, p->channel);
	if (p->subs[x].dfd > -1) {
		dahdi_close(p->subs[x].dfd);
	}
	p->subs[x].dfd = -1;
	p->subs[x].linear = 0;
	p->subs[x].chan = 0;
	p->subs[x].owner = NULL;
	p->subs[x].inthreeway = 0;
	p->polarity = POLARITY_IDLE;
	memset(&p->subs[x].curconf, 0, sizeof(p->subs[x].curconf));
	return 0;
}

static int dahdi_digit(struct cw_channel *cw, char digit)
{
	struct dahdi_dialoperation zo;
	struct dahdi_pvt *p;
	int res = 0;
	int curindex;
	p = cw->tech_pvt;
	cw_mutex_lock(&p->lock);
	curindex = dahdi_get_index(cw, p, 0);
	if ((curindex == SUB_REAL) && p->owner) {
#ifdef ZAPATA_PRI
		if ((p->sig == SIG_PRI) && (cw->_state == CW_STATE_DIALING) && !p->proceeding) {
			if (p->setup_ack) {
				if (!pri_grab(p, p->pri)) {
					pri_information(p->pri->pri,p->call,digit);
					pri_rel(p->pri);
				} else
					cw_log(CW_LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
			} else if (strlen(p->dialdest) < sizeof(p->dialdest) - 1) {
				cw_log(CW_LOG_DEBUG, "Queueing digit '%c' since setup_ack not yet received\n", digit);
				res = strlen(p->dialdest);
				p->dialdest[res++] = digit;
				p->dialdest[res] = '\0';
			}
		} else {
#else
		{
#endif
			zo.op = DAHDI_DIAL_OP_APPEND;
			zo.dialstr[0] = 'T';
			zo.dialstr[1] = digit;
			zo.dialstr[2] = 0;
			if ((res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &zo)))
				cw_log(CW_LOG_WARNING, "Couldn't dial digit %c\n", digit);
			else
				p->dialing = 1;
		}
	}
	cw_mutex_unlock(&p->lock);
	return res;
}

static const char *events[] = {
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
		"Pulse Start",
		"Timer Expired",
		"Timer Ping",
		"Polarity Reversal",
		"Ring Begin",
};

static struct {
	int alarm;
	const char *name;
} alarms[] = {
	{ DAHDI_ALARM_RED, "Red Alarm" },
	{ DAHDI_ALARM_YELLOW, "Yellow Alarm" },
	{ DAHDI_ALARM_BLUE, "Blue Alarm" },
	{ DAHDI_ALARM_RECOVER, "Recovering" },
	{ DAHDI_ALARM_LOOPBACK, "Loopback" },
	{ DAHDI_ALARM_NOTOPEN, "Not Open" },
	{ DAHDI_ALARM_NONE, "None" },
};

static const char *alarm2str(int alarm_num)
{
	int x;
	for (x=0;x<sizeof(alarms) / sizeof(alarms[0]); x++) {
		if (alarms[x].alarm & alarm_num)
			return alarms[x].name;
	}
	return alarm_num ? "Unknown Alarm" : "No Alarm";
}

static const char *event2str(int event)
{
        static char buf[256];
        if ((event < (sizeof(events) / sizeof(events[0]))) && (event > -1))
                return events[event];
        sprintf(buf, "Event %d", event); /* safe */
        return buf;
}

#ifdef ZAPATA_PRI
static const char *dialplan2str(int dialplan)
{
	if (dialplan == -1) {
		return("Dynamically set dialplan in ISDN");
	}
	return(pri_plan2str(dialplan));
}
#endif

static const char *dahdi_sig2str(int sig)
{
	static char buf[256];
	switch(sig) {
	case SIG_EM:
		return "E & M Immediate";
	case SIG_EMWINK:
		return "E & M Wink";
	case SIG_EM_E1:
		return "E & M E1";
	case SIG_FEATD:
		return "Feature Group D (DTMF)";
	case SIG_FEATDMF:
		return "Feature Group D (MF)";
	case SIG_FEATDMF_TA:
		return "Feature Groud D (MF) Tandem Access";
	case SIG_FEATB:
		return "Feature Group B (MF)";
	case SIG_E911:
		return "E911 (MF)";
	case SIG_FXSLS:
		return "FXS Loopstart";
	case SIG_FXSGS:
		return "FXS Groundstart";
	case SIG_FXSKS:
		return "FXS Kewlstart";
	case SIG_FXOLS:
		return "FXO Loopstart";
	case SIG_FXOGS:
		return "FXO Groundstart";
	case SIG_FXOKS:
		return "FXO Kewlstart";
	case SIG_PRI:
		return "PRI Signalling";
	case SIG_SF:
		return "SF (Tone) Signalling Immediate";
	case SIG_SFWINK:
		return "SF (Tone) Signalling Wink";
	case SIG_SF_FEATD:
		return "SF (Tone) Signalling with Feature Group D (DTMF)";
	case SIG_SF_FEATDMF:
		return "SF (Tone) Signalling with Feature Group D (MF)";
	case SIG_SF_FEATB:
		return "SF (Tone) Signalling with Feature Group B (MF)";
	case SIG_GR303FXOKS:
		return "GR-303 Signalling with FXOKS";
	case SIG_GR303FXSKS:
		return "GR-303 Signalling with FXSKS";
	case 0:
		return "Pseudo Signalling";
	default:
		snprintf(buf, sizeof(buf), "Unknown signalling %d", sig);
		return buf;
	}
}

#define sig2str dahdi_sig2str

static int conf_add(struct dahdi_pvt *p, struct dahdi_subchannel *c, int i, int slavechannel)
{
	/* If the conference already exists, and we're already in it
	   don't bother doing anything */
	struct dahdi_confinfo zi;
	
	memset(&zi, 0, sizeof(zi));
	zi.chan = 0;

	if (slavechannel > 0) {
		/* If we have only one slave, do a digital mon */
		zi.confmode = DAHDI_CONF_DIGITALMON;
		zi.confno = slavechannel;
	} else {
		if (!i) {
			/* Real-side and pseudo-side both participate in conference */
			zi.confmode = DAHDI_CONF_REALANDPSEUDO | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER |
								DAHDI_CONF_PSEUDO_TALKER | DAHDI_CONF_PSEUDO_LISTENER;
		} else
			zi.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
		zi.confno = p->confno;
	}
	if ((zi.confno == c->curconf.confno) && (zi.confmode == c->curconf.confmode))
		return 0;
	if (c->dfd < 0)
		return 0;
	if (ioctl(c->dfd, DAHDI_SETCONF, &zi)) {
		cw_log(CW_LOG_WARNING, "Failed to add %d to conference %d/%d\n", c->dfd, zi.confmode, zi.confno);
		return -1;
	}
	if (slavechannel < 1) {
		p->confno = zi.confno;
	}
	memcpy(&c->curconf, &zi, sizeof(c->curconf));
	cw_log(CW_LOG_DEBUG, "Added %d to conference %d/%d\n", c->dfd, c->curconf.confmode, c->curconf.confno);
	return 0;
}

static int isourconf(struct dahdi_pvt *p, struct dahdi_subchannel *c)
{
	/* If they're listening to our channel, they're ours */	
	if ((p->channel == c->curconf.confno) && (c->curconf.confmode == DAHDI_CONF_DIGITALMON))
		return 1;
	/* If they're a talker on our (allocated) conference, they're ours */
	if ((p->confno > 0) && (p->confno == c->curconf.confno) && (c->curconf.confmode & DAHDI_CONF_TALKER))
		return 1;
	return 0;
}

static int conf_del(struct dahdi_pvt *p, struct dahdi_subchannel *c, int i)
{
	struct dahdi_confinfo zi;

	CW_UNUSED(i);

	if (/* Can't delete if there's no dfd */
		(c->dfd < 0) ||
		/* Don't delete from the conference if it's not our conference */
		!isourconf(p, c)
		/* Don't delete if we don't think it's conferenced at all (implied) */
		) return 0;
	memset(&zi, 0, sizeof(zi));
	if (ioctl(c->dfd, DAHDI_SETCONF, &zi)) {
		cw_log(CW_LOG_WARNING, "Failed to drop %d from conference %d/%d\n", c->dfd, c->curconf.confmode, c->curconf.confno);
		return -1;
	}
	cw_log(CW_LOG_DEBUG, "Removed %d from conference %d/%d\n", c->dfd, c->curconf.confmode, c->curconf.confno);
	memcpy(&c->curconf, &zi, sizeof(c->curconf));
	return 0;
}

static int isslavenative(struct dahdi_pvt *p, struct dahdi_pvt **out)
{
	int x;
	int useslavenative;
	struct dahdi_pvt *slave = NULL;
	/* Start out optimistic */
	useslavenative = 1;
	/* Update conference state in a stateless fashion */
	for (x=0;x<3;x++) {
		/* Any three-way calling makes slave native mode *definitely* out
		   of the question */
		if ((p->subs[x].dfd > -1) && p->subs[x].inthreeway)
			useslavenative = 0;
	}
	/* If we don't have any 3-way calls, check to see if we have
	   precisely one slave */
	if (useslavenative) {
		for (x=0;x<MAX_SLAVES;x++) {
			if (p->slaves[x]) {
				if (slave) {
					/* Whoops already have a slave!  No 
					   slave native and stop right away */
					slave = NULL;
					useslavenative = 0;
					break;
				} else {
					/* We have one slave so far */
					slave = p->slaves[x];
				}
			}
		}
	}
	/* If no slave, slave native definitely out */
	if (!slave)
		useslavenative = 0;
	else if (slave->law != p->law) {
		useslavenative = 0;
		slave = NULL;
	}
	if (out)
		*out = slave;
	return useslavenative;
}

static int reset_conf(struct dahdi_pvt *p)
{
	p->confno = -1;
	memset(&p->subs[SUB_REAL].curconf, 0, sizeof(p->subs[SUB_REAL].curconf));
	if (p->subs[SUB_REAL].dfd > -1) {
		struct dahdi_confinfo zi;

		memset(&zi, 0, sizeof(zi));
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCONF, &zi))
			cw_log(CW_LOG_WARNING, "Failed to reset conferencing on channel %d!\n", p->channel);
	}
	return 0;
}

static int update_conf(struct dahdi_pvt *p)
{
	int needconf = 0;
	int x;
	int useslavenative;
	struct dahdi_pvt *slave = NULL;

	useslavenative = isslavenative(p, &slave);
	/* Start with the obvious, general stuff */
	for (x=0;x<3;x++) {
		/* Look for three way calls */
		if ((p->subs[x].dfd > -1) && p->subs[x].inthreeway) {
			conf_add(p, &p->subs[x], x, 0);
			needconf++;
		} else {
			conf_del(p, &p->subs[x], x);
		}
	}
	/* If we have a slave, add him to our conference now. or DAX
	   if this is slave native */
	for (x=0;x<MAX_SLAVES;x++) {
		if (p->slaves[x]) {
			if (useslavenative)
				conf_add(p, &p->slaves[x]->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(p));
			else {
				conf_add(p, &p->slaves[x]->subs[SUB_REAL], SUB_REAL, 0);
				needconf++;
			}
		}
	}
	/* If we're supposed to be in there, do so now */
	if (p->inconference && !p->subs[SUB_REAL].inthreeway) {
		if (useslavenative)
			conf_add(p, &p->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(slave));
		else {
			conf_add(p, &p->subs[SUB_REAL], SUB_REAL, 0);
			needconf++;
		}
	}
	/* If we have a master, add ourselves to his conference */
	if (p->master) {
		if (isslavenative(p->master, NULL)) {
			conf_add(p->master, &p->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(p->master));
		} else {
			conf_add(p->master, &p->subs[SUB_REAL], SUB_REAL, 0);
		}
	}
	if (!needconf) {
		/* Nobody is left (or should be left) in our conference.  
		   Kill it.  */
		p->confno = -1;
	}
	cw_log(CW_LOG_DEBUG, "Updated conferencing on %d, with %d conference users\n", p->channel, needconf);
	return 0;
}

static void dahdi_enable_ec(struct dahdi_pvt *p)
{
	int x;
	int res;
	if (!p)
		return;
	if (p->echocanon) {
		cw_log(CW_LOG_DEBUG, "Echo cancellation already on\n");
		return;
	}
	if (p->digital) {
		cw_log(CW_LOG_DEBUG, "Echo cancellation isn't required on digital connection\n");
		return;
	}
	if (p->echocancel) {
		if (p->sig == SIG_PRI) {
			x = 1;
			res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &x);
			if (res)
				cw_log(CW_LOG_WARNING, "Unable to enable echo cancellation on channel %d\n", p->channel);
		}
		x = p->echocancel;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOCANCEL, &x);
		if (res) 
			cw_log(CW_LOG_WARNING, "Unable to enable echo cancellation on channel %d\n", p->channel);
		else {
			p->echocanon = 1;
			cw_log(CW_LOG_DEBUG, "Enabled echo cancellation on channel %d\n", p->channel);
		}
	} else
		cw_log(CW_LOG_DEBUG, "No echo cancellation requested\n");
}

static void dahdi_train_ec(struct dahdi_pvt *p)
{
	int x;
	int res;
	if (p && p->echocancel && p->echotraining) {
		x = p->echotraining;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOTRAIN, &x);
		if (res) 
			cw_log(CW_LOG_WARNING, "Unable to request echo training on channel %d\n", p->channel);
		else {
			cw_log(CW_LOG_DEBUG, "Engaged echo training on channel %d\n", p->channel);
		}
	} else
		cw_log(CW_LOG_DEBUG, "No echo training requested\n");
}

static void dahdi_disable_ec(struct dahdi_pvt *p)
{
	int x;
	int res;
	if (p->echocancel) {
		x = 0;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOCANCEL, &x);
		if (res) 
			cw_log(CW_LOG_WARNING, "Unable to disable echo cancellation on channel %d\n", p->channel);
		else
			cw_log(CW_LOG_DEBUG, "disabled echo cancellation on channel %d\n", p->channel);
	}
	p->echocanon = 0;
}

static void fill_txgain(struct dahdi_gains *g, float gain, int law)
{
	int j;
	int k;
	float linear_gain = pow(10.0, gain / 20.0);

	switch (law) {
	case DAHDI_LAW_ALAW:
		for (j = 0; j < (sizeof(g->txgain) / sizeof(g->txgain[0])); j++) {
			if (gain) {
				k = (int) (((float) CW_ALAW(j)) * linear_gain);
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->txgain[j] = CW_LIN2A(k);
			} else {
				g->txgain[j] = j;
			}
		}
		break;
	case DAHDI_LAW_MULAW:
		for (j = 0; j < (sizeof(g->txgain) / sizeof(g->txgain[0])); j++) {
			if (gain) {
				k = (int) (((float) CW_MULAW(j)) * linear_gain);
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->txgain[j] = CW_LIN2MU(k);
			} else {
				g->txgain[j] = j;
			}
		}
		break;
	}
}

static void fill_rxgain(struct dahdi_gains *g, float gain, int law)
{
	int j;
	int k;
	float linear_gain = pow(10.0, gain / 20.0);

	switch (law) {
	case DAHDI_LAW_ALAW:
		for (j = 0; j < (sizeof(g->rxgain) / sizeof(g->rxgain[0])); j++) {
			if (gain) {
				k = (int) (((float) CW_ALAW(j)) * linear_gain);
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->rxgain[j] = CW_LIN2A(k);
			} else {
				g->rxgain[j] = j;
			}
		}
		break;
	case DAHDI_LAW_MULAW:
		for (j = 0; j < (sizeof(g->rxgain) / sizeof(g->rxgain[0])); j++) {
			if (gain) {
				k = (int) (((float) CW_MULAW(j)) * linear_gain);
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->rxgain[j] = CW_LIN2MU(k);
			} else {
				g->rxgain[j] = j;
			}
		}
		break;
	}
}

static int set_actual_txgain(int fd, int chan, float gain, int law)
{
	struct dahdi_gains g;
	int res;

	memset(&g, 0, sizeof(g));
	g.chan = chan;
	res = ioctl(fd, DAHDI_GETGAINS, &g);
	if (res) {
		cw_log(CW_LOG_DEBUG, "Failed to read gains: %s\n", strerror(errno));
		return res;
	}

	fill_txgain(&g, gain, law);

	return ioctl(fd, DAHDI_SETGAINS, &g);
}

static int set_actual_rxgain(int fd, int chan, float gain, int law)
{
	struct dahdi_gains g;
	int res;

	memset(&g, 0, sizeof(g));
	g.chan = chan;
	res = ioctl(fd, DAHDI_GETGAINS, &g);
	if (res) {
		cw_log(CW_LOG_DEBUG, "Failed to read gains: %s\n", strerror(errno));
		return res;
	}

	fill_rxgain(&g, gain, law);

	return ioctl(fd, DAHDI_SETGAINS, &g);
}

static int set_actual_gain(int fd, int chan, float rxgain, float txgain, int law)
{
	return set_actual_txgain(fd, chan, txgain, law) | set_actual_rxgain(fd, chan, rxgain, law);
}

static int bump_gains(struct dahdi_pvt *p)
{
	int res;

	res = set_actual_gain(p->subs[SUB_REAL].dfd, 0, p->rxgain + p->cid_rxgain, p->txgain, p->law);
	if (res) {
		cw_log(CW_LOG_WARNING, "Unable to bump gain: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static int restore_gains(struct dahdi_pvt *p)
{
	int res;

	res = set_actual_gain(p->subs[SUB_REAL].dfd, 0, p->rxgain, p->txgain, p->law);
	if (res) {
		cw_log(CW_LOG_WARNING, "Unable to restore gains: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static inline int dahdi_set_hook(int fd, int hs)
{
	int x, res;
	x = hs;
	res = ioctl(fd, DAHDI_HOOK, &x);
	if (res < 0) 
	{
		if (errno == EINPROGRESS) return 0;
		cw_log(CW_LOG_WARNING, "zt hook failed: %s\n", strerror(errno));
	}
	return res;
}

static inline int dahdi_confmute(struct dahdi_pvt *p, int muted)
{
	int x, y, res;
	x = muted;
	if (p->sig == SIG_PRI) {
		y = 1;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &y);
		if (res)
			cw_log(CW_LOG_WARNING, "Unable to set audio mode on '%d'\n", p->channel);
	}
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_CONFMUTE, &x);
	if (res < 0) 
		cw_log(CW_LOG_WARNING, "zt confmute(%d) failed on channel %d: %s\n", muted, p->channel, strerror(errno));
	return res;
}

static int save_conference(struct dahdi_pvt *p)
{
	struct dahdi_confinfo c;
	int res;
	if (p->saveconf.confmode) {
		cw_log(CW_LOG_WARNING, "Can't save conference -- already in use\n");
		return -1;
	}
	p->saveconf.chan = 0;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GETCONF, &p->saveconf);
	if (res) {
		cw_log(CW_LOG_WARNING, "Unable to get conference info: %s\n", strerror(errno));
		p->saveconf.confmode = 0;
		return -1;
	}
	memset(&c, 0, sizeof(c));
	c.confmode = DAHDI_CONF_NORMAL;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCONF, &c);
	if (res) {
		cw_log(CW_LOG_WARNING, "Unable to set conference info: %s\n", strerror(errno));
		return -1;
	}
	if (option_debug)
		cw_log(CW_LOG_DEBUG, "Disabled conferencing\n");
	return 0;
}

static int restore_conference(struct dahdi_pvt *p)
{
	int res;
	if (p->saveconf.confmode) {
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCONF, &p->saveconf);
		p->saveconf.confmode = 0;
		if (res) {
			cw_log(CW_LOG_WARNING, "Unable to restore conference info: %s\n", strerror(errno));
			return -1;
		}
	}
	if (option_debug)
		cw_log(CW_LOG_DEBUG, "Restored conferencing\n");
	return 0;
}

static int send_callerid(struct dahdi_pvt *p);

static int send_cwcidspill(struct dahdi_pvt *p)
{
	p->callwaitcas = 0;
	p->cidcwexpire = 0;
	p->cidspill = malloc(MAX_CALLERID_SIZE);
	if (p->cidspill) {
		p->cidlen = cw_callerid_generate(p->cid_signalling, p->cidspill, MAX_CALLERID_SIZE, CW_PRES_ALLOWED, p->callwait_num, p->callwait_name, 1, CW_LAW(p));
		p->cidpos = 0;
		send_callerid(p);
		if (option_verbose > 2)
			cw_verbose(VERBOSE_PREFIX_3 "CPE supports Call Waiting Caller*ID.  Sending '%s/%s'\n", p->callwait_name, p->callwait_num);
	} else return -1;
	return 0;
}

static int send_callerid(struct dahdi_pvt *p)
{
	/* Assumes spill in p->cidspill, p->cidlen in length and we're p->cidpos into it */
	int res;
	/* Take out of linear mode if necessary */
	if (p->subs[SUB_REAL].linear) {
		p->subs[SUB_REAL].linear = 0;
		dahdi_setlinear(p->subs[SUB_REAL].dfd, 0);
	}
	while(p->cidpos < p->cidlen) {
		res = write(p->subs[SUB_REAL].dfd, p->cidspill + p->cidpos, p->cidlen - p->cidpos);
		if (res < 0) {
			if (errno == EAGAIN)
				return 0;
			else {
				cw_log(CW_LOG_WARNING, "write failed: %s\n", strerror(errno));
				return -1;
			}
		}
		if (!res)
			return 0;
		p->cidpos += res;
	}
	if (p->callwaitcas) {
		/* Wait for CID/CW to expire */
		p->cidcwexpire = CIDCW_EXPIRE_SAMPLES;
	} else
		restore_conference(p);
	return 1;
}

static int dahdi_callwait(struct cw_channel *cw)
{
	struct dahdi_pvt *p = cw->tech_pvt;
	p->callwaitingrepeat = CALLWAITING_REPEAT_SAMPLES;
	if (p->cidspill) {
		cw_log(CW_LOG_WARNING, "%s: Discarded existing spill!\n", cw->name);
		free(p->cidspill);
	}
	p->cidspill = malloc(MAX_CALLERID_SIZE);
	if (p->cidspill) {
		save_conference(p);
		if (!p->callwaitrings && p->callwaitingcallerid) {
			p->cidlen = cw_gen_cas(p->cidspill, MAX_CALLERID_SIZE, 1, CW_LAW(p));
			p->callwaitcas = 1;
		} else {
			p->cidlen = cw_gen_cas(p->cidspill, MAX_CALLERID_SIZE, 1, CW_LAW(p));
			p->callwaitcas = 0;
		}
		p->cidpos = 0;
		send_callerid(p);
	} else {
		cw_log(CW_LOG_WARNING, "%s: No memory for SAS/CAS spill\n", cw->name);
		return -1;
	}
	return 0;
}

static int dahdi_call(struct cw_channel *cw, const char *rdest)
{
	struct dahdi_ring_cadence rcad, *cadence;
	struct tone_zone *tzone;
	struct dahdi_pvt *p = cw->tech_pvt;
	int x, res, curindex;
	char *c, *n, *l;
#ifdef ZAPATA_PRI
	char *s=NULL;
#endif
	char dest[256]; /* must be same length as p->dialdest */
	cw_mutex_lock(&p->lock);
	cw_copy_string(dest, rdest, sizeof(dest));
	cw_copy_string(p->dialdest, rdest, sizeof(p->dialdest));
	if ((cw->_state == CW_STATE_BUSY)) {
		p->subs[SUB_REAL].needbusy = 1;
		cw_mutex_unlock(&p->lock);
		return 0;
	}
	if ((cw->_state != CW_STATE_DOWN) && (cw->_state != CW_STATE_RESERVED)) {
		cw_log(CW_LOG_WARNING, "dahdi_call called on %s, neither down nor reserved\n", cw->name);
		cw_mutex_unlock(&p->lock);
		return -1;
	}
	p->dialednone = 0;
	if (p->radio)  /* if a radio channel, up immediately */
	{
		/* Special pseudo -- automatically up */
		cw_setstate(cw, CW_STATE_UP); 
		cw_mutex_unlock(&p->lock);
		return 0;
	}
	x = DAHDI_FLUSH_READ | DAHDI_FLUSH_WRITE;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_FLUSH, &x);
	if (res)
		cw_log(CW_LOG_WARNING, "Unable to flush input on channel %d\n", p->channel);
	p->outgoing = 1;

	set_actual_gain(p->subs[SUB_REAL].dfd, 0, p->rxgain, p->txgain, p->law);

	switch(p->sig) {
	case SIG_FXOLS:
	case SIG_FXOGS:
	case SIG_FXOKS:
		if (p->owner == cw) {
			/* Normal ring, on hook */
			
			/* nick@dccinc.com 4/3/03 mods to allow for deferred dialing */
			c = strchr(dest, '/');
			if (c)
				c++;
			if (c && (strlen(c) < p->stripmsd)) {
				cw_log(CW_LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
				c = NULL;
			}
			if (c) {
				p->dop.op = DAHDI_DIAL_OP_REPLACE;
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "Tw%s", c);
				cw_log(CW_LOG_DEBUG, "FXO: setup deferred dialstring: %s\n", c);
			} else {
				p->dop.dialstr[0] = '\0';
			}

			if (p->cidspill) {
				cw_log(CW_LOG_WARNING, "cidspill already exists??\n");
				free(p->cidspill);
				p->cidspill = NULL;
			}
			if (p->use_callerid) {
				/* Caller ID before ring uses defined signalling format. Caller ID between
				 * rings is (nearly) always US style. This is sufficiently compatible with all the
				 * possibilities we might want that it should "just work".
				 * Note: India (Bharti) uses DTMF after the first ring apparently. If you want
				 * that you have to use explicit cidsignalling=dtmf, cidstart=ring and forego
				 * any pre-ring CID.
				 */
				p->cidspill = malloc(MAX_CALLERID_SIZE);
				p->callwaitcas = 0;
				if (p->cidspill) {
					p->cidlen = cw_callerid_generate(p->cid_signalling, p->cidspill, MAX_CALLERID_SIZE, cw->cid.cid_pres, cw->cid.cid_num, cw->cid.cid_name, 0, CW_LAW(p));
					p->cidpos = 0;

					if (p->cid_start != CID_START_RING || !p->cidrings) {
						p->cidspill2 = malloc(MAX_CALLERID_SIZE);
						if (p->cidspill2)
							p->cidlen2 = cw_callerid_generate(ADSI_STANDARD_CLASS, p->cidspill2, MAX_CALLERID_SIZE, cw->cid.cid_pres, cw->cid.cid_num, cw->cid.cid_name, 0, CW_LAW(p));
					}
				} else {
					/* No memory for caller ID but maybe we can still make the call */
					cw_log(CW_LOG_WARNING, "Unable to generate CallerID spill\n");
				}
			}

			/* Choose main cadence */
			if (p->distinctivering > 0 && p->distinctivering <= num_cadence) {
				cadence = &cadences[p->distinctivering-1];
				p->cidrings = cidrings[p->distinctivering-1];
			} else if (!ioctl(p->subs[SUB_REAL].dfd, DAHDI_GETTONEZONE, &res)
			&& (tzone = tone_zone_find_by_num(res))) {
				memcpy(rcad.ringcadence, tzone->ringcadence, sizeof(rcad.ringcadence));
				cadence = &rcad;
				p->cidrings = -1;
			} else {
				rcad.ringcadence[0] = -DAHDI_DEFAULT_RINGTIME;
				rcad.ringcadence[1] = DAHDI_RINGOFFTIME;
				cadence = &rcad;
				p->cidrings = 1;
			}

			if (p->cidspill && p->distinctiveringaftercid) {
				/* Insert the initial cadence making the silence long enough for
				 * the CID. Note that we don't adjust silence like this anywhere
				 * else otherwise it breaks the remote's distinctive ring
				 * detection. Even this might break some but... tough!
				 */
				memmove(&rcad.ringcadence[2], &cadence->ringcadence[0],
					sizeof(rcad.ringcadence) - 2 * sizeof(rcad.ringcadence[0]));
				rcad.ringcadence[0] = 250;
				rcad.ringcadence[1] = SAMPLES_TO_MS(p->cidlen);
				if (rcad.ringcadence[1] < 1000)
					rcad.ringcadence[1] = 1000;
				cadence = &rcad;
				p->cidrings = 1;

				/* If the main cadence doesn't explicitly mark the loop start
				 * we need to mark it ourselves to be the start of the main
				 * cadence, after the initial cadence
				 */
				for (x = 2; x < arraysize(rcad.ringcadence) && rcad.ringcadence[x] >= 0; x += 2);
				if (x >= arraysize(rcad.ringcadence))
					rcad.ringcadence[2] = -rcad.ringcadence[2];
			} else if (p->cidrings < 0 && (p->cidrings = p->sendcalleridafter) < 0) {
				/* Look for the longest silence in the cadence and use
				 * that for the CID position
				 */
				int i = 0;
				for (x = 1; x < arraysize(rcad.ringcadence); x += 2) {
					if (rcad.ringcadence[x] > i) {
						i = rcad.ringcadence[x];
						p->cidrings = (x >> 1) + 1;
					}
				}
			}

			if (option_debug) {
				for (x = 0; x < arraysize(rcad.ringcadence) && cadence->ringcadence[x]; x += 2)
					cw_log(CW_LOG_DEBUG, "%s: cadence[%d] = %d/%d\n", cw->name, (x >> 1) + 1, cadence->ringcadence[x], cadence->ringcadence[x+1]);
				cw_log(CW_LOG_DEBUG, "%s: Post-ring CID follows ring %d\n", cw->name, p->cidrings);
			}

			if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCADENCE, cadence))
				cw_log(CW_LOG_WARNING, "%s: Unable to set ring cadence\n", cw->name);

			if (!p->cidspill || (p->cid_start == CID_START_RING && p->cidrings)) {
				/* Caller ID goes in the silence between two rings. Just start ringing
				 * the line. Caller ID will be despatched in due course.
				 */
				cw_log(CW_LOG_DEBUG, "%s: Start ringing\n", cw->name);
				p->cid_send_on = CID_START_RING;
				x = DAHDI_RING;
				if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x) && (errno != EINPROGRESS)) {
					cw_log(CW_LOG_WARNING, "%s: Unable to ring phone: %s\n", cw->name, strerror(errno));
					cw_mutex_unlock(&p->lock);
					return -1;
				}
			} else {
				/* Caller ID goes before the first ring. We assume that means it's flagged
				 * by a UK style polarity reversal, followed by data, followed by ringing.
				 * If we are sending using DTMF then in some implementations there should be
				 * no polarity reversal. It probably doesn't hurt, though?
				 * If one or both of these fail caller ID might fail but the call may still
				 * be possible. We'll find out when we try and start ringing after the
				 * caller ID has been sent (possibly into the ether).
				 */
				cw_log(CW_LOG_DEBUG, "%s: Send pre-ring caller ID\n", cw->name);
				p->cid_send_on = CID_START_POLARITY;
				x = p->cidlen;
				if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_ONHOOKTRANSFER, &x))
					cw_log(CW_LOG_DEBUG, "%s: Unable to start on hook transfer: %s\n", cw->name, strerror(errno));
				x = POLARITY_REV;
				ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETPOLARITY, &x);
				send_callerid(p);
			}

			/* Don't send audio while on hook, until the call is answered */
			p->dialing = 1;
		} else {
			/* Call waiting call */
			p->callwaitrings = 0;
			if (cw->cid.cid_num)
				cw_copy_string(p->callwait_num, cw->cid.cid_num, sizeof(p->callwait_num));
			else
				p->callwait_num[0] = '\0';
			if (cw->cid.cid_name)
				cw_copy_string(p->callwait_name, cw->cid.cid_name, sizeof(p->callwait_name));
			else
				p->callwait_name[0] = '\0';
			/* Call waiting tone instead */
			if (dahdi_callwait(cw)) {
				cw_mutex_unlock(&p->lock);
				return -1;
			}
			/* Make ring-back */
			if (tone_zone_play_tone(p->subs[SUB_CALLWAIT].dfd, DAHDI_TONE_RINGTONE))
				cw_log(CW_LOG_WARNING, "Unable to generate call-wait ring-back on channel %s\n", cw->name);
				
		}
		n = cw->cid.cid_name;
		l = cw->cid.cid_num;
		if (l)
			cw_copy_string(p->lastcid_num, l, sizeof(p->lastcid_num));
		else
			p->lastcid_num[0] = '\0';
		if (n)
			cw_copy_string(p->lastcid_name, n, sizeof(p->lastcid_name));
		else
			p->lastcid_name[0] = '\0';
		cw_setstate(cw, CW_STATE_RINGING);
		curindex = dahdi_get_index(cw, p, 0);
		if (curindex > -1) {
			p->subs[curindex].needringing = 1;
		}
		break;
	case SIG_FXSLS:
	case SIG_FXSGS:
	case SIG_FXSKS:
	case SIG_EMWINK:
	case SIG_EM:
	case SIG_EM_E1:
	case SIG_FEATD:
	case SIG_FEATDMF:
	case SIG_E911:
	case SIG_FEATB:
	case SIG_SFWINK:
	case SIG_SF:
	case SIG_SF_FEATD:
	case SIG_SF_FEATDMF:
	case SIG_FEATDMF_TA:
	case SIG_SF_FEATB:
		c = strchr(dest, '/');
		if (c)
			c++;
		else
			c = (char *)"";
		if (strlen(c) < p->stripmsd) {
			cw_log(CW_LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
			cw_mutex_unlock(&p->lock);
			return -1;
		}
#ifdef ZAPATA_PRI
		/* Start the trunk, if not GR-303 */
		if (!p->pri) {
#endif
			x = DAHDI_START;
			res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
			if (res < 0) {
				if (errno != EINPROGRESS) {
					cw_log(CW_LOG_WARNING, "Unable to start channel: %s\n", strerror(errno));
					cw_mutex_unlock(&p->lock);
					return -1;
				}
			}
#ifdef ZAPATA_PRI
		}
#endif
		cw_log(CW_LOG_DEBUG, "Dialing '%s'\n", c);
		p->dop.op = DAHDI_DIAL_OP_REPLACE;

		c += p->stripmsd;

		switch (p->sig) {
		case SIG_FEATD:
			l = cw->cid.cid_num;
			if (l) 
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T*%s*%s*", l, c);
			else
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T**%s*", c);
			break;
		case SIG_FEATDMF:
			l = cw->cid.cid_num;
			if (l) 
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*00%s#*%s#", l, c);
			else
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*02#*%s#", c);
			break;
		case SIG_FEATDMF_TA:
		{
			struct cw_var_t *cic, *ozz;

			/* If you have to go through a Tandem Access point you need to use this */
			ozz = pbx_builtin_getvar_helper(p->owner, CW_KEYWORD_FEATDMF_OZZ, "FEATDMF_OZZ");
			cic = pbx_builtin_getvar_helper(p->owner, CW_KEYWORD_FEATDMF_CIC, "FEATDMF_CIC");

			if ((!ozz && !defaultozz[0]) || (!cic && !defaultcic[0])) {
				cw_log(CW_LOG_WARNING, "Unable to dial channel of type feature group D MF tandem access without CIC or OZZ set\n");
				cw_mutex_unlock(&p->lock);
				return -1;
			}

			snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*%s%s#", (ozz ? ozz->value : defaultozz), (cic ? cic->value : defaultcic));
			snprintf(p->finaldial, sizeof(p->finaldial), "M*%s#", c);
			p->whichwink = 0;
		}
			break;
		case SIG_E911:
			cw_copy_string(p->dop.dialstr, "M*911#", sizeof(p->dop.dialstr));
			break;
		case SIG_FEATB:
			snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*%s#", c);
			break;
		default:
			if (p->pulse)
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "P%sw", c);
			else
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T%sw", c);
			break;
		}

		if (p->echotraining && (strlen(p->dop.dialstr) > 4)) {
			memset(p->echorest, 'w', sizeof(p->echorest) - 1);
			strcpy(p->echorest + (p->echotraining / 400) + 1, p->dop.dialstr + strlen(p->dop.dialstr) - 2);
			p->echorest[sizeof(p->echorest) - 1] = '\0';
			p->echobreak = 1;
			p->dop.dialstr[strlen(p->dop.dialstr)-2] = '\0';
		} else
			p->echobreak = 0;
		if (!res) {
			if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop)) {
				x = DAHDI_ONHOOK;
				ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
				cw_log(CW_LOG_WARNING, "Dialing failed on channel %d: %s\n", p->channel, strerror(errno));
				cw_mutex_unlock(&p->lock);
				return -1;
			}
		} else
			cw_log(CW_LOG_DEBUG, "Deferring dialing...\n");
		p->dialing = 1;
		if (cw_strlen_zero(c))
			p->dialednone = 1;
		cw_setstate(cw, CW_STATE_DIALING);
		break;
	case 0:
		/* Special pseudo -- automatically up*/
		cw_setstate(cw, CW_STATE_UP);
		break;		
	case SIG_PRI:
		/* We'll get it in a moment -- but use dialdest to store pre-setup_ack digits */
		p->dialdest[0] = '\0';
		break;
	default:
		cw_log(CW_LOG_DEBUG, "not yet implemented\n");
		cw_mutex_unlock(&p->lock);
		return -1;
	}
#ifdef ZAPATA_PRI
	if (p->pri) {
		struct cw_var_t *var;
		struct pri_sr *sr;
		int pridialplan;
		int dp_strip;
		int prilocaldialplan;
		int ldp_strip;
		int exclusive;

		c = strchr(dest, '/');
		if (c)
			c++;
		else
			c = dest;
		if (!p->hidecallerid) {
			l = cw->cid.cid_num;
			n = cw->cid.cid_name;
		} else {
			l = NULL;
			n = NULL;
		}
		if (strlen(c) < p->stripmsd) {
			cw_log(CW_LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
			cw_mutex_unlock(&p->lock);
			return -1;
		}
		if (p->sig != SIG_FXSKS) {
			p->dop.op = DAHDI_DIAL_OP_REPLACE;
			s = strchr(c + p->stripmsd, 'w');
			if (s) {
				if (strlen(s) > 1)
					snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T%s", s);
				else
					p->dop.dialstr[0] = '\0';
				*s = '\0';
			} else {
				p->dop.dialstr[0] = '\0';
			}
		}
		if (pri_grab(p, p->pri)) {
			cw_log(CW_LOG_WARNING, "Failed to grab PRI!\n");
			cw_mutex_unlock(&p->lock);
			return -1;
		}
		if (!(p->call = pri_new_call(p->pri->pri))) {
			cw_log(CW_LOG_WARNING, "Unable to create call on channel %d\n", p->channel);
			pri_rel(p->pri);
			cw_mutex_unlock(&p->lock);
			return -1;
		}
		if (!(sr = pri_sr_new())) {
			cw_log(CW_LOG_WARNING, "Failed to allocate setup request channel %d\n", p->channel);
			pri_rel(p->pri);
			cw_mutex_unlock(&p->lock);
		}
		if (p->bearer || (p->sig == SIG_FXSKS)) {
			if (p->bearer) {
				cw_log(CW_LOG_DEBUG, "Oooh, I have a bearer on %d (%d:%d)\n", PVT_TO_CHANNEL(p->bearer), p->bearer->logicalspan, p->bearer->channel);
				p->bearer->call = p->call;
			} else
				cw_log(CW_LOG_DEBUG, "I'm being setup with no bearer right now...\n");
			pri_set_crv(p->pri->pri, p->call, p->channel, 0);
		}
		p->digital = IS_DIGITAL(cw->transfercapability);
		/* Add support for exclusive override */
		if (p->priexclusive)
			exclusive = 1;
		else {
		/* otherwise, traditional behavior */
			if (p->pri->nodetype == PRI_NETWORK)
				exclusive = 0;
			else
				exclusive = 1;
		}
		
		pri_sr_set_channel(sr, p->bearer ? PVT_TO_CHANNEL(p->bearer) : PVT_TO_CHANNEL(p), exclusive, 1);
		pri_sr_set_bearer(sr, p->digital ? PRI_TRANS_CAP_DIGITAL : cw->transfercapability, 
					(p->digital ? -1 : 
						((p->law == DAHDI_LAW_ALAW) ? PRI_LAYER_1_ALAW : PRI_LAYER_1_ULAW)));
		if (p->pri->facilityenable)
			pri_facility_enable(p->pri->pri);

		if (option_verbose > 2)
			cw_verbose(VERBOSE_PREFIX_3 "Requested transfer capability: 0x%.2x - %s\n", cw->transfercapability, cw_transfercapability2str(cw->transfercapability));
		dp_strip = 0;
 		pridialplan = p->pri->dialplan - 1;
 		if (pridialplan == -2) { /* compute dynamically */
			if (strlen(p->pri->internationalprefix) > 0 && strncmp(c + p->stripmsd, p->pri->internationalprefix, strlen(p->pri->internationalprefix)) == 0) {
				dp_strip = strlen(p->pri->internationalprefix);
				pridialplan = PRI_INTERNATIONAL_ISDN;
			} else if (strlen(p->pri->nationalprefix) > 0 && strncmp(c + p->stripmsd, p->pri->nationalprefix, strlen(p->pri->nationalprefix)) == 0) {
				dp_strip = strlen(p->pri->nationalprefix);
				pridialplan = PRI_NATIONAL_ISDN;
			} else if (strlen(p->pri->localprefix) > 0 && strncmp(c + p->stripmsd, p->pri->localprefix, strlen(p->pri->localprefix)) == 0) {
				dp_strip = strlen(p->pri->localprefix);
				pridialplan = PRI_LOCAL_ISDN;
			} else if (strlen(p->pri->privateprefix) > 0 && strncmp(c + p->stripmsd, p->pri->privateprefix, strlen(p->pri->privateprefix)) == 0) {
				dp_strip = strlen(p->pri->privateprefix);
				pridialplan = PRI_PRIVATE;
			} else if (strlen(p->pri->unknownprefix) > 0 && strncmp(c + p->stripmsd, p->pri->unknownprefix, strlen(p->pri->unknownprefix)) == 0) {
				dp_strip = strlen(p->pri->unknownprefix);
				pridialplan = PRI_UNKNOWN;
			} else {
				pridialplan = PRI_LOCAL_ISDN;
			}
		}

		if ((var = pbx_builtin_getvar_helper(cw, CW_KEYWORD_PRITON, "PRITON"))) {
			if (!strcasecmp(var->value, "INTERNATIONAL")) {
				pridialplan = PRI_INTERNATIONAL_ISDN;
			} else if (!strcasecmp(var->value, "NATIONAL")) {
				pridialplan = PRI_NATIONAL_ISDN;
			} else if (!strcasecmp(var->value, "LOCAL")) {
				pridialplan = PRI_LOCAL_ISDN;
			} else if (!strcasecmp(var->value, "PRIVATE")) {
				pridialplan = PRI_PRIVATE;
			} else if (!strcasecmp(var->value, "UNKNOWN")) {
				pridialplan = PRI_UNKNOWN;
			} else {
				cw_log(CW_LOG_WARNING, "Unable to determine PRI type of number '%s', using instead '%s'\n", var->value, ton2str(pridialplan));
			}
			cw_object_put(var);
		}
		

 		pri_sr_set_called(sr, c + p->stripmsd + dp_strip, pridialplan,  s ? 1 : 0);

		ldp_strip = 0;
		prilocaldialplan = p->pri->localdialplan - 1;
		if ((l != NULL) && (prilocaldialplan == -2)) { /* compute dynamically */
			if (strlen(p->pri->internationalprefix) > 0 && strncmp(l, p->pri->internationalprefix, strlen(p->pri->internationalprefix)) == 0) {
				ldp_strip = strlen(p->pri->internationalprefix);
				prilocaldialplan = PRI_INTERNATIONAL_ISDN;
			} else if (strlen(p->pri->nationalprefix) > 0 && strncmp(l, p->pri->nationalprefix, strlen(p->pri->nationalprefix)) == 0) {
				ldp_strip = strlen(p->pri->nationalprefix);
				prilocaldialplan = PRI_NATIONAL_ISDN;
			} else if (strlen(p->pri->localprefix) > 0 && strncmp(l, p->pri->localprefix, strlen(p->pri->localprefix)) == 0) {
				ldp_strip = strlen(p->pri->localprefix);
				prilocaldialplan = PRI_LOCAL_ISDN;
			} else if (strlen(p->pri->privateprefix) > 0 && strncmp(l, p->pri->privateprefix, strlen(p->pri->privateprefix)) == 0) {
				ldp_strip = strlen(p->pri->privateprefix);
				prilocaldialplan = PRI_PRIVATE;
			} else if (strlen(p->pri->unknownprefix) > 0 && strncmp(l, p->pri->unknownprefix, strlen(p->pri->unknownprefix)) == 0) {
				ldp_strip = strlen(p->pri->unknownprefix);
				prilocaldialplan = PRI_UNKNOWN;
			} else {
				prilocaldialplan = PRI_LOCAL_ISDN;
			}
		}
		if ((var = pbx_builtin_getvar_helper(cw, CW_KEYWORD_PRILOCALTON, "PRILOCALTON"))) {
			if (!strcasecmp(var->value, "INTERNATIONAL")) {
				prilocaldialplan = PRI_INTERNATIONAL_ISDN;
			} else if (!strcasecmp(var->value, "NATIONAL")) {
				prilocaldialplan = PRI_NATIONAL_ISDN;
			} else if (!strcasecmp(var->value, "LOCAL")) {
				prilocaldialplan = PRI_LOCAL_ISDN;
			} else if (!strcasecmp(var->value, "PRIVATE")) {
				prilocaldialplan = PRI_PRIVATE;
			} else if (!strcasecmp(var->value, "UNKNOWN")) {
				prilocaldialplan = PRI_UNKNOWN;
			} else {
				cw_log(CW_LOG_WARNING, "Unable to determine local PRI type of number '%s', using instead '%s'\n", var->value, ton2str(prilocaldialplan));
			}
			cw_object_put(var);
		}

		pri_sr_set_caller(sr, l ? (l + ldp_strip) : NULL, n, prilocaldialplan, 
					l ? (p->use_callingpres ? cw->cid.cid_pres : PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN) : 
						 PRES_NUMBER_NOT_AVAILABLE);
		pri_sr_set_redirecting(sr, cw->cid.cid_rdnis, p->pri->localdialplan - 1, PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN, PRI_REDIR_UNCONDITIONAL);
		/* User-user info */
		if ((var = pbx_builtin_getvar_helper(p->owner, CW_KEYWORD_USERUSERINFO, "USERUSERINFO"))) {
			pri_sr_set_useruser(sr, var->value);
			cw_object_put(var);
		}

		if (pri_setup(p->pri->pri, p->call,  sr)) {
 			cw_log(CW_LOG_WARNING, "Unable to setup call to %s (using %s)\n", 
 						c + p->stripmsd + dp_strip, dialplan2str(p->pri->dialplan));
			pri_rel(p->pri);
			cw_mutex_unlock(&p->lock);
			pri_sr_free(sr);
			return -1;
		}
		pri_sr_free(sr);
		cw_setstate(cw, CW_STATE_DIALING);
		pri_rel(p->pri);
	}
#endif		
	cw_mutex_unlock(&p->lock);
	return 0;
}

static void destroy_dahdi_pvt(struct dahdi_pvt **pvt)
{
	struct dahdi_pvt *p = *pvt;
	/* Remove channel from the list */
	if(p->prev)
		p->prev->next = p->next;
	if(p->next)
		p->next->prev = p->prev;
	cw_mutex_destroy(&p->lock);
	free(p);
	*pvt = NULL;
}

static int destroy_channel(struct dahdi_pvt *prev, struct dahdi_pvt *cur, int now)
{
	int owned = 0;
	int i = 0;

	if (!now) {
		if (cur->owner) {
			owned = 1;
		}

		for (i = 0; i < 3; i++) {
			if (cur->subs[i].owner) {
				owned = 1;
			}
		}
		if (!owned) {
			if (prev) {
				prev->next = cur->next;
				if (prev->next)
					prev->next->prev = prev;
				else
					ifend = prev;
			} else {
				iflist = cur->next;
				if (iflist)
					iflist->prev = NULL;
				else
					ifend = NULL;
			}
			if (cur->subs[SUB_REAL].dfd > -1) {
				dahdi_close(cur->subs[SUB_REAL].dfd);
			}
			destroy_dahdi_pvt(&cur);
		}
	} else {
		if (prev) {
			prev->next = cur->next;
			if (prev->next)
				prev->next->prev = prev;
			else
				ifend = prev;
		} else {
			iflist = cur->next;
			if (iflist)
				iflist->prev = NULL;
			else
				ifend = NULL;
		}
		if (cur->subs[SUB_REAL].dfd > -1) {
			dahdi_close(cur->subs[SUB_REAL].dfd);
		}
		destroy_dahdi_pvt(&cur);
	}
	return 0;
}

#ifdef ZAPATA_PRI
int pri_is_up(struct dahdi_pri *pri)
{
	int x;
	for (x=0;x<NUM_DCHANS;x++) {
		if (pri->dchanavail[x] == DCHAN_AVAILABLE)
			return 1;
	}
	return 0;
}

int pri_assign_bearer(struct dahdi_pvt *crv, struct dahdi_pri *pri, struct dahdi_pvt *bearer)
{
	bearer->owner = &inuse;
	bearer->realcall = crv;
	crv->subs[SUB_REAL].dfd = bearer->subs[SUB_REAL].dfd;
	if (crv->subs[SUB_REAL].owner)
		crv->subs[SUB_REAL].owner->fds[0] = crv->subs[SUB_REAL].dfd;
	crv->bearer = bearer;
	crv->call = bearer->call;
	crv->pri = pri;
	return 0;
}

static char *pri_order(int level)
{
	switch(level) {
	case 0:
		return "Primary";
	case 1:
		return "Secondary";
	case 2:
		return "Tertiary";
	case 3:
		return "Quaternary";
	default:
		return "<Unknown>";
	}		
}

/* Returns fd of the active dchan */
int pri_active_dchan_fd(struct dahdi_pri *pri)
{
	int x = -1;

	for (x = 0; x < NUM_DCHANS; x++) {
		if ((pri->dchans[x] == pri->pri))
			break;
	}

	return pri->fds[x];
}

int pri_find_dchan(struct dahdi_pri *pri)
{
	int oldslot = -1;
	struct pri *old;
	int newslot = -1;
	int x;
	old = pri->pri;
	for(x=0;x<NUM_DCHANS;x++) {
		if ((pri->dchanavail[x] == DCHAN_AVAILABLE) && (newslot < 0))
			newslot = x;
		if (pri->dchans[x] == old) {
			oldslot = x;
		}
	}
	if (newslot < 0) {
		newslot = 0;
		cw_log(CW_LOG_WARNING, "No D-channels available!  Using Primary channel %d as D-channel anyway!\n",
			pri->dchannels[newslot]);
	}
	if (old && (oldslot != newslot))
		cw_log(CW_LOG_NOTICE, "Switching from from d-channel %d to channel %d!\n",
			pri->dchannels[oldslot], pri->dchannels[newslot]);
	pri->pri = pri->dchans[newslot];
	return 0;
}
#endif

static int dahdi_hangup(struct cw_channel *cw)
{
	/*static int restore_gains(struct zt_pvt *p);*/
	struct dahdi_params par;
	struct dahdi_pvt *p = cw->tech_pvt;
	struct dahdi_pvt *tmp = NULL;
	struct dahdi_pvt *prev = NULL;
	struct cw_channel *bchan;
	int res;
	int curindex, x, law;

	if (option_debug)
		cw_log(CW_LOG_DEBUG, "dahdi_hangup(%s)\n", cw->name);
	if (!cw->tech_pvt) {
		cw_log(CW_LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	
	cw_mutex_lock(&p->lock);
	
	curindex = dahdi_get_index(cw, p, 1);

	if (p->sig == SIG_PRI) {
		x = 1;
		cw_channel_setoption(cw, CW_OPTION_AUDIO_MODE, &x, sizeof(char));
	}

	x = 0;
	dahdi_confmute(p, 0);
	restore_gains(p);
	if (p->origcid_num) {
		cw_copy_string(p->cid_num, p->origcid_num, sizeof(p->cid_num));
		free(p->origcid_num);
		p->origcid_num = NULL;
	}	
	if (p->origcid_name) {
		cw_copy_string(p->cid_name, p->origcid_name, sizeof(p->cid_name));
		free(p->origcid_name);
		p->origcid_name = NULL;
	}	
	if (p->dsp)
		cw_dsp_digitmode(p->dsp,DSP_DIGITMODE_DTMF | p->dtmfrelax);
#if 0
	if (p->exten)
		p->exten[0] = '\0';
#endif

	cw_log(CW_LOG_DEBUG, "Hangup: channel: %d index = %d, normal = %d, callwait = %d, thirdcall = %d\n",
		p->channel, curindex, p->subs[SUB_REAL].dfd, p->subs[SUB_CALLWAIT].dfd, p->subs[SUB_THREEWAY].dfd);
	p->ignoredtmf = 0;
	
	if (curindex > -1) {
		/* Real channel, do some fixup */
		p->subs[curindex].owner = NULL;
		p->subs[curindex].needanswer = 0;
		p->subs[curindex].needflash = 0;
		p->subs[curindex].needringing = 0;
		p->subs[curindex].needbusy = 0;
		p->subs[curindex].needcongestion = 0;
		p->subs[curindex].linear = 0;
		p->subs[curindex].needcallerid = 0;
		p->polarity = POLARITY_IDLE;
		dahdi_setlinear(p->subs[curindex].dfd, 0);
		if (curindex == SUB_REAL) {
			if ((p->subs[SUB_CALLWAIT].dfd > -1) && (p->subs[SUB_THREEWAY].dfd > -1)) {
				cw_log(CW_LOG_DEBUG, "Normal call hung up with both three way call and a call waiting call in place?\n");
				if (p->subs[SUB_CALLWAIT].inthreeway) {
					/* We had flipped over to answer a callwait and now it's gone */
					cw_log(CW_LOG_DEBUG, "We were flipped over to the callwait, moving back and unowning.\n");
					/* Move to the call-wait, but un-own us until they flip back. */
					swap_subs(p, SUB_CALLWAIT, SUB_REAL);
					unalloc_sub(p, SUB_CALLWAIT);
					p->owner = NULL;
				} else {
					/* The three way hung up, but we still have a call wait */
					cw_log(CW_LOG_DEBUG, "We were in the threeway and have a callwait still.  Ditching the threeway.\n");
					swap_subs(p, SUB_THREEWAY, SUB_REAL);
					unalloc_sub(p, SUB_THREEWAY);
					if (p->subs[SUB_REAL].inthreeway) {
						/* This was part of a three way call.  Immediately make way for
						   another call */
						cw_log(CW_LOG_DEBUG, "Call was complete, setting owner to former third call\n");
						p->owner = p->subs[SUB_REAL].owner;
					} else {
						/* This call hasn't been completed yet...  Set owner to NULL */
						cw_log(CW_LOG_DEBUG, "Call was incomplete, setting owner to NULL\n");
						p->owner = NULL;
					}
					p->subs[SUB_REAL].inthreeway = 0;
				}
			} else if (p->subs[SUB_CALLWAIT].dfd > -1) {
				/* Move to the call-wait and switch back to them. */
				swap_subs(p, SUB_CALLWAIT, SUB_REAL);
				unalloc_sub(p, SUB_CALLWAIT);
				p->owner = p->subs[SUB_REAL].owner;
				if (p->owner->_state != CW_STATE_UP)
					p->subs[SUB_REAL].needanswer = 1;
				if ((bchan = cw_bridged_channel(p->subs[SUB_REAL].owner))) {
					cw_moh_stop(bchan);
					cw_object_put(bchan);
				}
			} else if (p->subs[SUB_THREEWAY].dfd > -1) {
				swap_subs(p, SUB_THREEWAY, SUB_REAL);
				unalloc_sub(p, SUB_THREEWAY);
				if (p->subs[SUB_REAL].inthreeway) {
					/* This was part of a three way call.  Immediately make way for
					   another call */
					cw_log(CW_LOG_DEBUG, "Call was complete, setting owner to former third call\n");
					p->owner = p->subs[SUB_REAL].owner;
				} else {
					/* This call hasn't been completed yet...  Set owner to NULL */
					cw_log(CW_LOG_DEBUG, "Call was incomplete, setting owner to NULL\n");
					p->owner = NULL;
				}
				p->subs[SUB_REAL].inthreeway = 0;
			}
		} else if (curindex == SUB_CALLWAIT) {
			/* Ditch the holding callwait call, and immediately make it availabe */
			if (p->subs[SUB_CALLWAIT].inthreeway) {
				/* This is actually part of a three way, placed on hold.  Place the third part
				   on music on hold now */
				if (p->subs[SUB_THREEWAY].owner && (bchan = cw_bridged_channel(p->subs[SUB_THREEWAY].owner))) {
					cw_moh_start(bchan, NULL);
					cw_object_put(bchan);
				}
				p->subs[SUB_THREEWAY].inthreeway = 0;
				/* Make it the call wait now */
				swap_subs(p, SUB_CALLWAIT, SUB_THREEWAY);
				unalloc_sub(p, SUB_THREEWAY);
			} else
				unalloc_sub(p, SUB_CALLWAIT);
		} else if (curindex == SUB_THREEWAY) {
			if (p->subs[SUB_CALLWAIT].inthreeway) {
				/* The other party of the three way call is currently in a call-wait state.
				   Start music on hold for them, and take the main guy out of the third call */
				if (p->subs[SUB_CALLWAIT].owner && (bchan = cw_bridged_channel(p->subs[SUB_CALLWAIT].owner))) {
					cw_moh_start(bchan, NULL);
					cw_object_put(bchan);
				}
				p->subs[SUB_CALLWAIT].inthreeway = 0;
			}
			p->subs[SUB_REAL].inthreeway = 0;
			/* If this was part of a three way call index, let us make
			   another three way call */
			unalloc_sub(p, SUB_THREEWAY);
		} else {
			/* This wasn't any sort of call, but how are we an index? */
			cw_log(CW_LOG_WARNING, "Index found but not any type of call?\n");
		}
	}


	if (!p->subs[SUB_REAL].owner && !p->subs[SUB_CALLWAIT].owner && !p->subs[SUB_THREEWAY].owner) {
		p->owner = NULL;
		p->ringt = 0;
		p->distinctivering = 0;
		p->confirmanswer = 0;
		p->cidrings = 1;
		p->outgoing = 0;
		p->digital = 0;
		p->faxhandled = 0;
		p->pulsedial = 0;
		p->onhooktime = time(NULL);
#ifdef ZAPATA_PRI
		p->proceeding = 0;
		p->progress = 0;
		p->alerting = 0;
		p->setup_ack = 0;
#endif		
		cw_dsp_free(p->dsp);
		p->dsp = NULL;

		law = DAHDI_LAW_DEFAULT;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETLAW, &law);
		if (res < 0) 
			cw_log(CW_LOG_WARNING, "Unable to set law on channel %d to default\n", p->channel);
		/* Perform low level hangup if no owner left */
#ifdef ZAPATA_PRI
		if (p->pri) {
			/* Make sure we have a call (or REALLY have a call in the case of a PRI) */
			if (p->call && (!p->bearer || (p->bearer->call == p->call))) {
				if (!pri_grab(p, p->pri)) {
					struct cw_var_t *useruser = pbx_builtin_getvar_helper(cw, CW_KEYWORD_USERUSERINFO, "USERUSERINFO");
					if (p->alreadyhungup) {
						cw_log(CW_LOG_DEBUG, "Already hungup...  Calling hangup once, and clearing call\n");
						pri_call_set_useruser(p->call, (useruser ? useruser->value : NULL));
						pri_hangup(p->pri->pri, p->call, -1);
						p->call = NULL;
						if (p->bearer) 
							p->bearer->call = NULL;
					} else {
						struct cw_var_t *cause;
						int icause = cw->hangupcause ? cw->hangupcause : -1;
						cw_log(CW_LOG_DEBUG, "Not yet hungup...  Calling hangup once with icause, and clearing call\n");
						pri_call_set_useruser(p->call, (useruser ? useruser->value : NULL));
						p->alreadyhungup = 1;
						if (p->bearer)
							p->bearer->alreadyhungup = 1;
						if ((cause = pbx_builtin_getvar_helper(cw, CW_KEYWORD_PRI_CAUSE, "PRI_CAUSE"))) {
							int n;
							if ((n = atoi(cause->value)))
								icause = n;
							cw_object_put(cause);
						}
						pri_hangup(p->pri->pri, p->call, icause);
					}
					if (useruser)
						cw_object_put(useruser);
					if (res < 0) 
						cw_log(CW_LOG_WARNING, "pri_disconnect failed\n");
					pri_rel(p->pri);			
				} else {
					cw_log(CW_LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
					res = -1;
				}
			} else {
				if (p->bearer)
					cw_log(CW_LOG_DEBUG, "Bearer call is %p, while ours is still %p\n", p->bearer->call, p->call);
				p->call = NULL;
				res = 0;
			}
		}
#endif
		if (p->sig && (p->sig != SIG_PRI))
			res = dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_ONHOOK);
		if (res < 0) {
			cw_log(CW_LOG_WARNING, "Unable to hangup line %s\n", cw->name);
		}
		switch(p->sig) {
		case SIG_FXOGS:
		case SIG_FXOLS:
		case SIG_FXOKS:
			memset(&par, 0, sizeof(par));
			res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &par);
			if (!res) {
#if 0
				cw_log(CW_LOG_DEBUG, "Hanging up channel %d, offhook = %d\n", p->channel, par.rxisoffhook);
#endif
				/* If they're off hook, try playing congestion */
				if ((par.rxisoffhook) && (!p->radio))
					tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
				else
					tone_zone_play_tone(p->subs[SUB_REAL].dfd, -1);
			}
			break;
		case SIG_FXSGS:
		case SIG_FXSLS:
		case SIG_FXSKS:
			/* Make sure we're not made available for at least two seconds assuming
			   we were actually used for an inbound or outbound call. */
			if (cw->_state != CW_STATE_RESERVED) {
				time(&p->guardtime);
				p->guardtime += 2;
			}
			break;
		default:
			tone_zone_play_tone(p->subs[SUB_REAL].dfd, -1);
		}
		free(p->cidspill);
		free(p->cidspill2);
		if (p->sig)
			dahdi_disable_ec(p);
		x = 0;
		cw_channel_setoption(cw, CW_OPTION_TONE_VERIFY, &x, sizeof(char));
		cw_channel_setoption(cw, CW_OPTION_TDD, &x, sizeof(char));
		p->didtdd = FALSE;
		p->cidspill = p->cidspill2 = NULL;
		p->callwaitcas = 0;
		p->callwaiting = p->permcallwaiting;
		p->hidecallerid = p->permhidecallerid;
		p->dialing = 0;
		p->rdnis[0] = '\0';
		update_conf(p);
		reset_conf(p);
		/* Restore data mode */
		if (p->sig == SIG_PRI) {
			x = 0;
			cw_channel_setoption(cw, CW_OPTION_AUDIO_MODE, &x, sizeof(char));
		}
#ifdef ZAPATA_PRI
		if (p->bearer) {
			cw_log(CW_LOG_DEBUG, "Freeing up bearer channel %d\n", p->bearer->channel);
			/* Free up the bearer channel as well, and
			   don't use its file descriptor anymore */
			update_conf(p->bearer);
			reset_conf(p->bearer);
			p->bearer->owner = NULL;
			p->bearer->realcall = NULL;
			p->bearer = NULL;
			p->subs[SUB_REAL].dfd = -1;
			p->pri = NULL;
		}
#endif
		restart_monitor();
	}


	p->callwaitingrepeat = 0;
	p->cidcwexpire = 0;
	cw->tech_pvt = NULL;
	cw_mutex_unlock(&p->lock);
	if (option_verbose > 2) 
		cw_verbose( VERBOSE_PREFIX_3 "Hungup '%s'\n", cw->name);

	cw_mutex_lock(&iflock);
	tmp = iflist;
	prev = NULL;
	if (p->destroy) {
		while (tmp) {
			if (tmp == p) {
				destroy_channel(prev, tmp, 0);
				break;
			} else {
				prev = tmp;
				tmp = tmp->next;
			}
		}
	}
	cw_mutex_unlock(&iflock);
	return 0;
}

static int dahdi_answer(struct cw_channel *cw)
{
	struct dahdi_pvt *p = cw->tech_pvt;
	int res=0;
	int curindex;
	int oldstate = cw->_state;
	cw_setstate(cw, CW_STATE_UP);
	cw_mutex_lock(&p->lock);
	curindex = dahdi_get_index(cw, p, 0);
	if (curindex < 0)
		curindex = SUB_REAL;
	/* nothing to do if a radio channel */
	if (p->radio) {
		cw_mutex_unlock(&p->lock);
		return 0;
	}
	switch(p->sig) {
	case SIG_FXSLS:
	case SIG_FXSGS:
	case SIG_FXSKS:
		p->ringt = 0;
		/* Fall through */
	case SIG_EM:
	case SIG_EM_E1:
	case SIG_EMWINK:
	case SIG_FEATD:
	case SIG_FEATDMF:
	case SIG_E911:
	case SIG_FEATB:
	case SIG_SF:
	case SIG_SFWINK:
	case SIG_SF_FEATD:
	case SIG_SF_FEATDMF:
	case SIG_SF_FEATB:
	case SIG_FXOLS:
	case SIG_FXOGS:
	case SIG_FXOKS:
		/* Pick up the line */
		cw_log(CW_LOG_DEBUG, "Took %s off hook\n", cw->name);
		if(p->hanguponpolarityswitch) {
			gettimeofday(&p->polaritydelaytv, NULL);
		}
		res =  dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
		tone_zone_play_tone(p->subs[curindex].dfd, -1);
		p->dialing = 0;
		if ((curindex == SUB_REAL) && p->subs[SUB_THREEWAY].inthreeway) {
			if (oldstate == CW_STATE_RINGING) {
				cw_log(CW_LOG_DEBUG, "Finally swapping real and threeway\n");
				tone_zone_play_tone(p->subs[SUB_THREEWAY].dfd, -1);
				swap_subs(p, SUB_THREEWAY, SUB_REAL);
				p->owner = p->subs[SUB_REAL].owner;
			}
		}
		if (p->sig & __DAHDI_SIG_FXS) {
			dahdi_enable_ec(p);
			dahdi_train_ec(p);
		}
		break;
#ifdef ZAPATA_PRI
	case SIG_PRI:
		/* Send a pri acknowledge */
		if (!pri_grab(p, p->pri)) {
			p->proceeding = 1;
			res = pri_answer(p->pri->pri, p->call, 0, !p->digital);
			pri_rel(p->pri);
		} else {
			cw_log(CW_LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
			res= -1;
		}
		break;
#endif
	case 0:
		cw_mutex_unlock(&p->lock);
		return 0;
	default:
		cw_log(CW_LOG_WARNING, "Don't know how to answer signalling %d (channel %d)\n", p->sig, p->channel);
		res = -1;
	}
	cw_mutex_unlock(&p->lock);
	return res;
}

static int dahdi_setoption(struct cw_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	signed char *scp;
	int x;
	int curindex;
	struct dahdi_pvt *p = chan->tech_pvt;

	/* all supported options require data */
	if (!data || (datalen < 1)) {
		errno = EINVAL;
		return -1;
	}

	switch(option) {
	case CW_OPTION_TXGAIN:
		scp = (signed char *) data;
		curindex = dahdi_get_index(chan, p, 0);
		if (curindex < 0) {
			cw_log(CW_LOG_WARNING, "No index in TXGAIN?\n");
			return -1;
		}
		cw_log(CW_LOG_DEBUG, "Setting actual tx gain on %s to %f\n", chan->name, p->txgain + (float) *scp);
		return set_actual_txgain(p->subs[curindex].dfd, 0, p->txgain + (float) *scp, p->law);
	case CW_OPTION_RXGAIN:
		scp = (signed char *) data;
		curindex = dahdi_get_index(chan, p, 0);
		if (curindex < 0) {
			cw_log(CW_LOG_WARNING, "No index in RXGAIN?\n");
			return -1;
		}
		cw_log(CW_LOG_DEBUG, "Setting actual rx gain on %s to %f\n", chan->name, p->rxgain + (float) *scp);
		return set_actual_rxgain(p->subs[curindex].dfd, 0, p->rxgain + (float) *scp, p->law);
	case CW_OPTION_ECHOCANCEL:
		if (*(char *)data)
			dahdi_enable_ec(p);
		else
			dahdi_disable_ec(p);
		return 0;
	case CW_OPTION_TONE_VERIFY:
		if (!p->dsp)
			break;
		cp = (char *) data;
		switch (*cp) {
		case 1:
			cw_log(CW_LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF(1) on %s\n",chan->name);
			cw_dsp_digitmode(p->dsp,DSP_DIGITMODE_MUTECONF | p->dtmfrelax);  /* set mute mode if desired */
			break;
		case 2:
			cw_log(CW_LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n",chan->name);
			cw_dsp_digitmode(p->dsp,DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX | p->dtmfrelax);  /* set mute mode if desired */
			break;
		default:
			cw_log(CW_LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n",chan->name);
			cw_dsp_digitmode(p->dsp,DSP_DIGITMODE_DTMF | p->dtmfrelax);  /* set mute mode if desired */
			break;
		}
		break;
	case CW_OPTION_MUTECONF:
		dahdi_confmute(p, *(char *)data);
		break;
	case CW_OPTION_TDD:
		/* turn on or off TDD */
		cp = (char *) data;
		p->mate = 0;
		if (!*cp) { /* turn it off */
			cw_log(CW_LOG_DEBUG, "Set option TDD MODE, value: OFF(0) on %s\n",chan->name);
			if (p->tdd)
				tdd_free(p->tdd);
			p->tdd = FALSE;
			break;
		}
		cw_log(CW_LOG_DEBUG, "Set option TDD MODE, value: %s(%d) on %s\n",
			(*cp == 2) ? "MATE" : "ON", (int) *cp, chan->name);
		dahdi_disable_ec(p);
		/* otherwise, turn it on */
		if (!p->didtdd) {
			/* if haven't done it yet */
			uint8_t mybuf[41000];
			uint8_t *buf;
			int size,res,fd,len;
			struct pollfd fds[1];

			buf = mybuf;
			memset(buf, 0x7f, sizeof(mybuf)); /* set to silence */
			cw_gen_ecdisa(buf + 16000, 16000, CW_LAW(p));  /* put in tone */
			len = 40000;
			curindex = dahdi_get_index(chan, p, 0);
			if (curindex < 0) {
				cw_log(CW_LOG_WARNING, "No index in TDD?\n");
				return -1;
			}
			fd = p->subs[curindex].dfd;
			while(len) {
				if (cw_check_hangup(chan)) return -1;
				size = len;
				if (size > READ_SIZE)
					size = READ_SIZE;
				fds[0].fd = fd;
				fds[0].events = POLLPRI | POLLOUT;
				fds[0].revents = 0;
				res = poll(fds, 1, -1);
				if (!res) {
					cw_log(CW_LOG_DEBUG, "poll (for write) ret. 0 on channel %d\n", p->channel);
					continue;
				}
				/* if got exception */
				if (fds[0].revents & POLLPRI) return -1;
				if (!(fds[0].revents & POLLOUT)) {
					cw_log(CW_LOG_DEBUG, "write fd not ready on channel %d\n", p->channel);
					continue;
				}
				res = write(fd, buf, size);
				if (res != size) {
					if (res == -1) return -1;
					cw_log(CW_LOG_DEBUG, "Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
					break;
				}
				len -= size;
				buf += size;
			}
			p->didtdd = TRUE; /* set to have done it now */		
		}
		if (*cp == 2) { /* Mate mode */
			if (p->tdd)
				tdd_free(p->tdd);
			p->tdd = FALSE;
			p->mate = 1;
			break;
		}		
		if (!p->tdd) {
			/* if we dont have one yet */
			p->tdd = tdd_new(); /* allocate one */
		}		
		break;
	case CW_OPTION_RELAXDTMF:  /* Relax DTMF decoding (or not) */
		if (!p->dsp)
			break;
		cp = (char *) data;
		cw_log(CW_LOG_DEBUG, "Set option RELAX DTMF, value: %s(%d) on %s\n",
			*cp ? "ON" : "OFF", (int) *cp, chan->name);
		cw_dsp_digitmode(p->dsp, ((*cp) ? DSP_DIGITMODE_RELAXDTMF : DSP_DIGITMODE_DTMF) | p->dtmfrelax);
		break;
	case CW_OPTION_AUDIO_MODE:  /* Set AUDIO mode (or not) */
		cp = (char *) data;
		if (!*cp) {		
			cw_log(CW_LOG_DEBUG, "Set option AUDIO MODE, value: OFF(0) on %s\n", chan->name);
			x = 0;
			dahdi_disable_ec(p);
		} else {		
			cw_log(CW_LOG_DEBUG, "Set option AUDIO MODE, value: ON(1) on %s\n", chan->name);
			x = 1;
		}
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &x) == -1)
			cw_log(CW_LOG_WARNING, "Unable to set audio mode on channel %d to %d\n", p->channel, x);
		break;
	}
	errno = 0;

	return 0;
}

static void dahdi_unlink(struct dahdi_pvt *slave, struct dahdi_pvt *master, int needlock)
{
	/* Unlink a specific slave or all slaves/masters from a given master */
	int x;
	int hasslaves;
	if (!master)
		return;
	if (needlock) {
		cw_mutex_lock(&master->lock);
		if (slave) {
			while(cw_mutex_trylock(&slave->lock)) {
				cw_mutex_unlock(&master->lock);
				usleep(1);
				cw_mutex_lock(&master->lock);
			}
		}
	}
	hasslaves = 0;
	for (x=0;x<MAX_SLAVES;x++) {
		if (master->slaves[x]) {
			if (!slave || (master->slaves[x] == slave)) {
				/* Take slave out of the conference */
				cw_log(CW_LOG_DEBUG, "Unlinking slave %d from %d\n", master->slaves[x]->channel, master->channel);
				conf_del(master, &master->slaves[x]->subs[SUB_REAL], SUB_REAL);
				conf_del(master->slaves[x], &master->subs[SUB_REAL], SUB_REAL);
				master->slaves[x]->master = NULL;
				master->slaves[x] = NULL;
			} else
				hasslaves = 1;
		}
		if (!hasslaves)
			master->inconference = 0;
	}
	if (!slave) {
		if (master->master) {
			/* Take master out of the conference */
			conf_del(master->master, &master->subs[SUB_REAL], SUB_REAL);
			conf_del(master, &master->master->subs[SUB_REAL], SUB_REAL);
			hasslaves = 0;
			for (x=0;x<MAX_SLAVES;x++) {
				if (master->master->slaves[x] == master)
					master->master->slaves[x] = NULL;
				else if (master->master->slaves[x])
					hasslaves = 1;
			}
			if (!hasslaves)
				master->master->inconference = 0;
		}
		master->master = NULL;
	}
	update_conf(master);
	if (needlock) {
		if (slave)
			cw_mutex_unlock(&slave->lock);
		cw_mutex_unlock(&master->lock);
	}
}

static void dahdi_link(struct dahdi_pvt *slave, struct dahdi_pvt *master) {
	int x;
	if (!slave || !master) {
		cw_log(CW_LOG_WARNING, "Tried to link to/from NULL??\n");
		return;
	}
	for (x=0;x<MAX_SLAVES;x++) {
		if (!master->slaves[x]) {
			master->slaves[x] = slave;
			break;
		}
	}
	if (x >= MAX_SLAVES) {
		cw_log(CW_LOG_WARNING, "Replacing slave %d with new slave, %d\n", master->slaves[MAX_SLAVES - 1]->channel, slave->channel);
		master->slaves[MAX_SLAVES - 1] = slave;
	}
	if (slave->master) 
		cw_log(CW_LOG_WARNING, "Replacing master %d with new master, %d\n", slave->master->channel, master->channel);
	slave->master = master;
	
	cw_log(CW_LOG_DEBUG, "Making %d slave to master %d at %d\n", slave->channel, master->channel, x);
}

static void disable_dtmf_detect(struct dahdi_pvt *p)
{
#ifdef DAHDI_TONEDETECT
	int val;
#endif

	p->ignoredtmf = 1;

#ifdef DAHDI_TONEDETECT
	val = 0;
	ioctl(p->subs[SUB_REAL].dfd, DAHDI_TONEDETECT, &val);
#endif		
	
}

static void enable_dtmf_detect(struct dahdi_pvt *p)
{
#ifdef DAHDI_TONEDETECT
	int val;
#endif

	p->ignoredtmf = 0;

#ifdef DAHDI_TONEDETECT
	val = DAHDI_TONEDETECT_ON | DAHDI_TONEDETECT_MUTE;
	ioctl(p->subs[SUB_REAL].dfd, DAHDI_TONEDETECT, &val);
#endif		
}

static enum cw_bridge_result dahdi_bridge(struct cw_channel *c0, struct cw_channel *c1, int flags, struct cw_frame **fo, struct cw_channel **rc, int timeoutms)
{
	struct cw_channel *who;
	struct dahdi_pvt *p0, *p1, *op0, *op1;
	struct dahdi_pvt *master = NULL, *slave = NULL;
	struct cw_frame *f;
	int inconf = 0;
	int nothingok = 1;
	int ofd0, ofd1;
	int oi0, oi1, i0 = -1, i1 = -1, t0, t1;
	int os0 = -1, os1 = -1;
	int priority = 0;
	struct cw_channel *oc0, *oc1;
	enum cw_bridge_result res;

#ifdef PRI_2BCT
	int triedtopribridge = 0;
	q931_call *q931c0 = NULL, *q931c1 = NULL;
#endif

	/* For now, don't attempt to native bridge if either channel needs DTMF detection.
	   There is code below to handle it properly until DTMF is actually seen,
	   but due to currently unresolved issues it's ignored...
	*/

	if (flags & (CW_BRIDGE_DTMF_CHANNEL_0 | CW_BRIDGE_DTMF_CHANNEL_1))
		return CW_BRIDGE_FAILED_NOWARN;

	cw_channel_lock(c0);
	cw_channel_lock(c1);

	p0 = c0->tech_pvt;
	p1 = c1->tech_pvt;
	/* cant do pseudo-channels here */
	if (!p0 || (!p0->sig) || !p1 || (!p1->sig)) {
		cw_channel_unlock(c0);
		cw_channel_unlock(c1);
		return CW_BRIDGE_FAILED_NOWARN;
	}

	oi0 = dahdi_get_index(c0, p0, 0);
	oi1 = dahdi_get_index(c1, p1, 0);
	if ((oi0 < 0) || (oi1 < 0)) {
		cw_channel_unlock(c0);
		cw_channel_unlock(c1);
		return CW_BRIDGE_FAILED;
	}

	op0 = p0 = c0->tech_pvt;
	op1 = p1 = c1->tech_pvt;
	ofd0 = c0->fds[0];
	ofd1 = c1->fds[0];
	oc0 = p0->owner;
	oc1 = p1->owner;

	cw_mutex_lock(&p0->lock);
	if (cw_mutex_trylock(&p1->lock)) {
		/* Don't block, due to potential for deadlock */
		cw_mutex_unlock(&p0->lock);
		cw_channel_unlock(c0);
		cw_channel_unlock(c1);
		cw_log(CW_LOG_NOTICE, "Avoiding deadlock...\n");
		return CW_BRIDGE_RETRY;
	}

	if ((oi0 == SUB_REAL) && (oi1 == SUB_REAL)) {
		if (p0->owner && p1->owner) {
			/* If we don't have a call-wait in a 3-way, and we aren't in a 3-way, we can be master */
			if (!p0->subs[SUB_CALLWAIT].inthreeway && !p1->subs[SUB_REAL].inthreeway) {
				master = p0;
				slave = p1;
				inconf = 1;
			} else if (!p1->subs[SUB_CALLWAIT].inthreeway && !p0->subs[SUB_REAL].inthreeway) {
				master = p1;
				slave = p0;
				inconf = 1;
			} else {
				cw_log(CW_LOG_WARNING, "Huh?  Both calls are callwaits or 3-ways?  That's clever...?\n");
				cw_log(CW_LOG_WARNING, "p0: chan %d/%d/CW%d/3W%x, p1: chan %d/%d/CW%d/3W%x\n",
					p0->channel,
					oi0, (p0->subs[SUB_CALLWAIT].dfd > -1 ? 1 : 0),
					p0->subs[SUB_REAL].inthreeway, p0->channel,
					oi0, (p1->subs[SUB_CALLWAIT].dfd > -1 ? 1 : 0),
					p1->subs[SUB_REAL].inthreeway);
			}
			nothingok = 0;
		}
	} else if ((oi0 == SUB_REAL) && (oi1 == SUB_THREEWAY)) {
		if (p1->subs[SUB_THREEWAY].inthreeway) {
			master = p1;
			slave = p0;
			nothingok = 0;
		}
	} else if ((oi0 == SUB_THREEWAY) && (oi1 == SUB_REAL)) {
		if (p0->subs[SUB_THREEWAY].inthreeway) {
			master = p0;
			slave = p1;
			nothingok = 0;
		}
	} else if ((oi0 == SUB_REAL) && (oi1 == SUB_CALLWAIT)) {
		/* We have a real and a call wait.  If we're in a three way call, put us in it, otherwise, 
		   don't put us in anything */
		if (p1->subs[SUB_CALLWAIT].inthreeway) {
			master = p1;
			slave = p0;
			nothingok = 0;
		}
	} else if ((oi0 == SUB_CALLWAIT) && (oi1 == SUB_REAL)) {
		/* Same as previous */
		if (p0->subs[SUB_CALLWAIT].inthreeway) {
			master = p0;
			slave = p1;
			nothingok = 0;
		}
	}
	cw_log(CW_LOG_DEBUG, "master: %d, slave: %d, nothingok: %d\n",
		master ? master->channel : 0, slave ? slave->channel : 0, nothingok);
	if (master && slave) {
		/* Stop any tones, or play ringtone as appropriate.  If they're bridged
		   in an active threeway call with a channel that is ringing, we should
		   indicate ringing. */
		if ((oi1 == SUB_THREEWAY) && 
		    p1->subs[SUB_THREEWAY].inthreeway && 
		    p1->subs[SUB_REAL].owner && 
		    p1->subs[SUB_REAL].inthreeway && 
		    (p1->subs[SUB_REAL].owner->_state == CW_STATE_RINGING)) {
			cw_log(CW_LOG_DEBUG, "Playing ringback on %s since %s is in a ringing three-way\n", c0->name, c1->name);
			tone_zone_play_tone(p0->subs[oi0].dfd, DAHDI_TONE_RINGTONE);
			os1 = p1->subs[SUB_REAL].owner->_state;
		} else {
			cw_log(CW_LOG_DEBUG, "Stopping tones on %d/%d talking to %d/%d\n", p0->channel, oi0, p1->channel, oi1);
			tone_zone_play_tone(p0->subs[oi0].dfd, -1);
		}
		if ((oi0 == SUB_THREEWAY) && 
		    p0->subs[SUB_THREEWAY].inthreeway && 
		    p0->subs[SUB_REAL].owner && 
		    p0->subs[SUB_REAL].inthreeway && 
		    (p0->subs[SUB_REAL].owner->_state == CW_STATE_RINGING)) {
			cw_log(CW_LOG_DEBUG, "Playing ringback on %s since %s is in a ringing three-way\n", c1->name, c0->name);
			tone_zone_play_tone(p1->subs[oi1].dfd, DAHDI_TONE_RINGTONE);
			os0 = p0->subs[SUB_REAL].owner->_state;
		} else {
			cw_log(CW_LOG_DEBUG, "Stopping tones on %d/%d talking to %d/%d\n", p1->channel, oi1, p0->channel, oi0);
			tone_zone_play_tone(p1->subs[oi0].dfd, -1);
		}
		if ((oi0 == SUB_REAL) && (oi1 == SUB_REAL)) {
			if (!p0->echocanbridged || !p1->echocanbridged) {
				/* Disable echo cancellation if appropriate */
				dahdi_disable_ec(p0);
				dahdi_disable_ec(p1);
			}
		}
		dahdi_link(slave, master);
		master->inconference = inconf;
	} else if (!nothingok)
		cw_log(CW_LOG_WARNING, "Can't link %d/%s with %d/%s\n", p0->channel, subnames[oi0], p1->channel, subnames[oi1]);

	update_conf(p0);
	update_conf(p1);
	t0 = p0->subs[SUB_REAL].inthreeway;
	t1 = p1->subs[SUB_REAL].inthreeway;

	cw_mutex_unlock(&p0->lock);
	cw_mutex_unlock(&p1->lock);

	cw_channel_unlock(c0);
	cw_channel_unlock(c1);

	/* Native bridge failed */
	if ((!master || !slave) && !nothingok) {
		dahdi_enable_ec(p0);
		dahdi_enable_ec(p1);
		return CW_BRIDGE_FAILED;
	}
	
	if (!(flags & CW_BRIDGE_DTMF_CHANNEL_0))
		disable_dtmf_detect(op0);

	if (!(flags & CW_BRIDGE_DTMF_CHANNEL_1))
		disable_dtmf_detect(op1);

	for (;;) {
		struct cw_channel *c0_priority[2] = {c0, c1};
		struct cw_channel *c1_priority[2] = {c1, c0};

		/* Here's our main loop...  Start by locking things, looking for private parts, 
		   and then balking if anything is wrong */
		cw_channel_lock(c0);
		cw_channel_lock(c1);
		p0 = c0->tech_pvt;
		p1 = c1->tech_pvt;

		if (op0 == p0)
			i0 = dahdi_get_index(c0, p0, 1);
		if (op1 == p1)
			i1 = dahdi_get_index(c1, p1, 1);
		cw_channel_unlock(c0);
		cw_channel_unlock(c1);

		if (!timeoutms
            || 
		    (op0 != p0)
            ||
		    (op1 != p1)
            ||
		    (ofd0 != c0->fds[0])
            ||
		    (ofd1 != c1->fds[0])
            ||
		    (p0->subs[SUB_REAL].owner && (os0 > -1) && (os0 != p0->subs[SUB_REAL].owner->_state))
            ||
		    (p1->subs[SUB_REAL].owner && (os1 > -1) && (os1 != p1->subs[SUB_REAL].owner->_state))
            ||
		    (oc0 != p0->owner)
            ||
		    (oc1 != p1->owner)
            ||
		    (t0 != p0->subs[SUB_REAL].inthreeway)
            ||
		    (t1 != p1->subs[SUB_REAL].inthreeway)
            ||
		    (oi0 != i0)
            ||
		    (oi1 != i1))
        {
			cw_log(CW_LOG_DEBUG, "Something changed out on %d/%d to %d/%d, returning -3 to restart\n",
				op0->channel, oi0, op1->channel, oi1);
			res = CW_BRIDGE_RETRY;
			goto return_from_bridge;
		}

#ifdef PRI_2BCT
		q931c0 = p0->call;
		q931c1 = p1->call;
		if (p0->transfer && p1->transfer 
		    && q931c0 && q931c1 
		    && !triedtopribridge) {
			pri_channel_bridge(q931c0, q931c1);
			triedtopribridge = 1;
		}
#endif

		who = cw_waitfor_n(priority ? c0_priority : c1_priority, 2, &timeoutms);
		if (!who) {
			cw_log(CW_LOG_DEBUG, "Ooh, empty read...\n");
			continue;
		}
		f = cw_read(who);
		if (!f || (f->frametype == CW_FRAME_CONTROL)) {
			*fo = f;
			*rc = who;
			res = CW_BRIDGE_COMPLETE;
			goto return_from_bridge;
		}
		if (f->frametype == CW_FRAME_DTMF) {
			if ((who == c0) && p0->pulsedial) {
				cw_write(c1, &f);
			} else if (p1->pulsedial) {
				cw_write(c0, &f);
			} else {
				*fo = f;
				*rc = who;
				res = CW_BRIDGE_COMPLETE;
				goto return_from_bridge;
			}
		}
		cw_fr_free(f);
		
		/* Swap who gets priority */
		priority = !priority;
	}

return_from_bridge:
	if (op0 == p0)
		dahdi_enable_ec(p0);

	if (op1 == p1)
		dahdi_enable_ec(p1);

	if (!(flags & CW_BRIDGE_DTMF_CHANNEL_0))
		enable_dtmf_detect(op0);

	if (!(flags & CW_BRIDGE_DTMF_CHANNEL_1))
		enable_dtmf_detect(op1);

	dahdi_unlink(slave, master, 1);

	return res;
}

static int dahdi_fixup(struct cw_channel *oldchan, struct cw_channel *newchan)
{
	struct dahdi_pvt *p = newchan->tech_pvt;
	int x;
	cw_mutex_lock(&p->lock);
	cw_log(CW_LOG_DEBUG, "New owner for channel %d is %s\n", p->channel, newchan->name);
	if (p->owner == oldchan) {
		p->owner = newchan;
	}
	for (x=0;x<3;x++)
		if (p->subs[x].owner == oldchan) {
			if (!x)
				dahdi_unlink(NULL, p, 0);
			p->subs[x].owner = newchan;
		}
	if (newchan->_state == CW_STATE_RINGING) 
		dahdi_indicate(newchan, CW_CONTROL_RINGING);
	update_conf(p);
	cw_mutex_unlock(&p->lock);
	return 0;
}

static int dahdi_ring_phone(struct dahdi_pvt *p)
{
	int x;
	int res;
	/* Make sure our transmit state is on hook */
	x = 0;
	x = DAHDI_ONHOOK;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
	do {
		x = DAHDI_RING;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
		if (res) {
			switch(errno) {
			case EBUSY:
			case EINTR:
				/* Wait just in case */
				usleep(10000);
				continue;
			case EINPROGRESS:
				res = 0;
				break;
			default:
				cw_log(CW_LOG_WARNING, "Couldn't ring the phone: %s\n", strerror(errno));
				res = 0;
			}
		}
	} while (res);
	return res;
}

static void *ss_thread(void *data);

static struct cw_channel *dahdi_new(struct dahdi_pvt *, int, int, int, int, int);

static int attempt_transfer(struct dahdi_pvt *p)
{
	struct cw_channel *bchan, *bchan2;

	/* In order to transfer, we need at least one of the channels to
	   actually be in a call bridge.  We can't conference two applications
	   together (but then, why would we want to?) */
	if ((bchan = cw_bridged_channel(p->subs[SUB_REAL].owner))) {
		/* The three-way person we're about to transfer to could still be in MOH, so
		   stop if now if appropriate */
		if ((bchan2 = cw_bridged_channel(p->subs[SUB_THREEWAY].owner))) {
			cw_moh_stop(bchan2);
			cw_object_put(bchan2);
		}
		if (p->subs[SUB_THREEWAY].owner->_state == CW_STATE_RINGING) {
			cw_indicate(bchan, CW_CONTROL_RINGING);
		}
		if (p->subs[SUB_REAL].owner->cdr) {
			/* Move CDR from second channel to current one */
			p->subs[SUB_THREEWAY].owner->cdr =
				cw_cdr_append(p->subs[SUB_THREEWAY].owner->cdr, p->subs[SUB_REAL].owner->cdr);
			p->subs[SUB_REAL].owner->cdr = NULL;
		}
		if (bchan->cdr) {
			/* Move CDR from second channel's bridge to current one */
			p->subs[SUB_THREEWAY].owner->cdr =
				cw_cdr_append(p->subs[SUB_THREEWAY].owner->cdr, bchan->cdr);
			bchan->cdr = NULL;
		}
		 if (cw_channel_masquerade(p->subs[SUB_THREEWAY].owner, bchan)) {
			cw_log(CW_LOG_WARNING, "Unable to masquerade %s as %s\n",
					bchan->name, p->subs[SUB_THREEWAY].owner->name);
			return -1;
		}
		/* Orphan the channel after releasing the lock */
		cw_channel_unlock(p->subs[SUB_THREEWAY].owner);
		cw_object_put(bchan);
		unalloc_sub(p, SUB_THREEWAY);
	} else if ((bchan = cw_bridged_channel(p->subs[SUB_THREEWAY].owner))) {
		if (p->subs[SUB_REAL].owner->_state == CW_STATE_RINGING) {
			cw_indicate(bchan, CW_CONTROL_RINGING);
		}
		cw_moh_stop(bchan);
		if (p->subs[SUB_THREEWAY].owner->cdr) {
			/* Move CDR from second channel to current one */
			p->subs[SUB_REAL].owner->cdr =
				cw_cdr_append(p->subs[SUB_REAL].owner->cdr, p->subs[SUB_THREEWAY].owner->cdr);
			p->subs[SUB_THREEWAY].owner->cdr = NULL;
		}
		if (bchan->cdr) {
			/* Move CDR from second channel's bridge to current one */
			p->subs[SUB_REAL].owner->cdr =
				cw_cdr_append(p->subs[SUB_REAL].owner->cdr, bchan->cdr);
			bchan->cdr = NULL;
		}
		if (cw_channel_masquerade(p->subs[SUB_REAL].owner, bchan)) {
			cw_log(CW_LOG_WARNING, "Unable to masquerade %s as %s\n",
					bchan->name, p->subs[SUB_REAL].owner->name);
			return -1;
		}
		/* Three-way is now the REAL */
		swap_subs(p, SUB_THREEWAY, SUB_REAL);
		cw_channel_unlock(p->subs[SUB_REAL].owner);
		cw_object_put(bchan);
		unalloc_sub(p, SUB_THREEWAY);
		/* Tell the caller not to hangup */
		return 1;
	} else {
		cw_log(CW_LOG_DEBUG, "Neither %s nor %s are in a bridge, nothing to transfer\n",
					p->subs[SUB_REAL].owner->name, p->subs[SUB_THREEWAY].owner->name);
		p->subs[SUB_THREEWAY].owner->_softhangup |= CW_SOFTHANGUP_DEV;
		return -1;
	}
	return 0;
}

static int check_for_conference(struct dahdi_pvt *p)
{
	struct dahdi_confinfo ci;
	/* Fine if we already have a master, etc */
	if (p->master || (p->confno > -1))
		return 0;
	memset(&ci, 0, sizeof(ci));
	if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_GETCONF, &ci)) {
		cw_log(CW_LOG_WARNING, "Failed to get conference info on channel %d\n", p->channel);
		return 0;
	}
	/* If we have no master and don't have a confno, then 
	   if we're in a conference, it's probably a MeetMe room or
	   some such, so don't let us 3-way out! */
	if ((p->subs[SUB_REAL].curconf.confno != ci.confno) || (p->subs[SUB_REAL].curconf.confmode != ci.confmode)) {
		if (option_verbose > 2)	
			cw_verbose(VERBOSE_PREFIX_3 "Avoiding 3-way call when in an external conference\n");
		return 1;
	}
	return 0;
}

static int get_alarms(struct dahdi_pvt *p)
{
	int res;
	struct dahdi_spaninfo zi;
	memset(&zi, 0, sizeof(zi));
	zi.spanno = p->span;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SPANSTAT, &zi);
	if (res < 0) {
		cw_log(CW_LOG_WARNING, "Unable to determine alarm on channel %d\n", p->channel);
		return 0;
	}
	return zi.alarms;
}
			
static struct cw_frame *dahdi_handle_event(struct cw_channel *cw)
{
	int res,x;
	int curindex;
	char *c;
	struct dahdi_pvt *p = cw->tech_pvt;
	pthread_t threadid;
	struct cw_channel *chan;

	if ((curindex = dahdi_get_index(cw, p, 0)) < 0)
		return NULL;

	if (p->fake_event) {
		res = p->fake_event;
		p->fake_event = 0;
	} else
		res = dahdi_get_event(p->subs[curindex].dfd);

	cw_log(CW_LOG_DEBUG, "%s: Got event %s(%d) on channel %d (index %d)\n", cw->name, event2str(res), res, p->channel, curindex);

	if (res & (DAHDI_EVENT_PULSEDIGIT | DAHDI_EVENT_DTMFUP)) {
		if (res & DAHDI_EVENT_PULSEDIGIT)
			p->pulsedial = 1;
		else
			p->pulsedial = 0;
		cw_log(CW_LOG_DEBUG, "Detected %sdigit '%c'\n", p->pulsedial ? "pulse ": "", res & 0xff);
#ifdef ZAPATA_PRI
		if (!p->proceeding && p->sig == SIG_PRI && p->pri && p->pri->overlapdial) {
			p->subs[curindex].f.frametype = CW_FRAME_NULL;
			p->subs[curindex].f.subclass = 0;
		} else {
#endif
			p->subs[curindex].f.frametype = CW_FRAME_DTMF;
			p->subs[curindex].f.subclass = res & 0xff;
#ifdef ZAPATA_PRI
		}
#endif
		/* Unmute conference, return the captured digit */
		dahdi_confmute(p, 0);
		return &p->subs[curindex].f;
	}

	if (res & DAHDI_EVENT_DTMFDOWN) {
		cw_log(CW_LOG_DEBUG, "DTMF Down '%c'\n", res & 0xff);
		p->subs[curindex].f.frametype = CW_FRAME_NULL;
		p->subs[curindex].f.subclass = 0;
		dahdi_confmute(p, 1);
		/* Mute conference, return null frame */
		return &p->subs[curindex].f;
	}

	switch(res) {
		case DAHDI_EVENT_BITSCHANGED:
			cw_log(CW_LOG_WARNING, "Recieved bits changed on %s signalling?\n", sig2str(p->sig));
			break;
		case DAHDI_EVENT_PULSE_START:
			/* Stop tone if there's a pulse start and the PBX isn't started */
			if (!cw->pbx)
				tone_zone_play_tone(p->subs[curindex].dfd, -1);
			break;
		case DAHDI_EVENT_DIALCOMPLETE:
			if (p->inalarm) break;
			if (p->radio) break;
			if (ioctl(p->subs[curindex].dfd,DAHDI_DIALING,&x) == -1) {
				cw_log(CW_LOG_DEBUG, "DAHDI_DIALING ioctl failed on %s\n",cw->name);
				return NULL;
			}
			if (!x) { /* if not still dialing in driver */
				dahdi_enable_ec(p);
				if (p->echobreak) {
					dahdi_train_ec(p);
					cw_copy_string(p->dop.dialstr, p->echorest, sizeof(p->dop.dialstr));
					p->dop.op = DAHDI_DIAL_OP_REPLACE;
					res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop);
					p->echobreak = 0;
				} else {
					p->dialing = 0;
					if (p->sig == SIG_E911) {
						/* if thru with dialing after offhook */
						if (cw->_state == CW_STATE_DIALING_OFFHOOK) {
							cw_setstate(cw, CW_STATE_UP);
							p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
							p->subs[curindex].f.subclass = CW_CONTROL_ANSWER;
							break;
						} else { /* if to state wait for offhook to dial rest */
							/* we now wait for off hook */
							cw_setstate(cw,CW_STATE_DIALING_OFFHOOK);
						}
					}
					if (cw->_state == CW_STATE_DIALING) {
						if ((p->callprogress & 1) && CANPROGRESSDETECT(p) && p->dsp && p->outgoing) {
							cw_log(CW_LOG_DEBUG, "Done dialing, but waiting for progress detection before doing more...\n");
						} else if (p->confirmanswer || (!p->dialednone && ((p->sig == SIG_EM) || (p->sig == SIG_EM_E1) ||  (p->sig == SIG_EMWINK) || (p->sig == SIG_FEATD) || (p->sig == SIG_FEATDMF) || (p->sig == SIG_E911) || (p->sig == SIG_FEATB) || (p->sig == SIG_SF) || (p->sig == SIG_SFWINK) || (p->sig == SIG_SF_FEATD) || (p->sig == SIG_SF_FEATDMF) || (p->sig == SIG_SF_FEATB)))) {
							cw_setstate(cw, CW_STATE_RINGING);
						} else if (!p->answeronpolarityswitch) {
							cw_setstate(cw, CW_STATE_UP);
							p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
							p->subs[curindex].f.subclass = CW_CONTROL_ANSWER;
						}
					}
				}
			}
			break;
		case DAHDI_EVENT_ALARM:
#ifdef ZAPATA_PRI
			if (p->call) {
				if (p->pri && p->pri->pri) {
					if (!pri_grab(p, p->pri)) {
						pri_hangup(p->pri->pri, p->call, -1);
						pri_destroycall(p->pri->pri, p->call);
						p->call = NULL;
						pri_rel(p->pri);
					} else
						cw_log(CW_LOG_WARNING, "Failed to grab PRI!\n");
				} else
					cw_log(CW_LOG_WARNING, "The PRI Call have not been destroyed\n");
			}
			if (p->owner)
				p->owner->_softhangup |= CW_SOFTHANGUP_DEV;
			if (p->bearer)
				p->bearer->inalarm = 1;
			else
#endif
			p->inalarm = 1;
			res = get_alarms(p);
			cw_log(CW_LOG_WARNING, "Detected alarm on channel %d: %s\n", p->channel, alarm2str(res));
			cw_manager_event(EVENT_FLAG_SYSTEM, "Alarm",
				2,
				cw_msg_tuple("Alarm",   "%s", alarm2str(res)),
				cw_msg_tuple("Channel", "%d", p->channel)
			);
			/* fall through intentionally */
		case DAHDI_EVENT_ONHOOK:
			if (p->radio)
			{
				p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
				p->subs[curindex].f.subclass = CW_CONTROL_RADIO_UNKEY;
				break;
			}
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				p->onhooktime = time(NULL);
				p->msgstate = -1;
				/* Check for some special conditions regarding call waiting */
				if (curindex == SUB_REAL) {
					/* The normal line was hung up */
					if (p->subs[SUB_CALLWAIT].owner) {
						/* There's a call waiting call, so ring the phone, but make it unowned in the mean time */
						swap_subs(p, SUB_CALLWAIT, SUB_REAL);
						if (option_verbose > 2) 
							cw_verbose(VERBOSE_PREFIX_3 "Channel %d still has (callwait) call, ringing phone\n", p->channel);
						unalloc_sub(p, SUB_CALLWAIT);	
#if 0
						p->subs[curindex].needanswer = 0;
						p->subs[curindex].needringing = 0;
#endif						
						p->callwaitingrepeat = 0;
						p->cidcwexpire = 0;
						p->owner = NULL;
						/* Don't start streaming audio yet if the incoming call isn't up yet */
						if (p->subs[SUB_REAL].owner->_state != CW_STATE_UP)
							p->dialing = 1;
						dahdi_ring_phone(p);
					} else if (p->subs[SUB_THREEWAY].owner) {
						unsigned int mssinceflash;
						/* Here we have to retain the lock on both the main channel, the 3-way channel, and
						   the private structure -- not especially easy or clean */
						while(p->subs[SUB_THREEWAY].owner && cw_channel_trylock(p->subs[SUB_THREEWAY].owner)) {
							/* Yuck, didn't get the lock on the 3-way, gotta release everything and re-grab! */
							cw_mutex_unlock(&p->lock);
							cw_channel_unlock(cw);
							usleep(1);
							/* We can grab cw and p in that order, without worry.  We should make sure
							   nothing seriously bad has happened though like some sort of bizarre double
							   masquerade! */
							cw_channel_lock(cw);
							cw_mutex_lock(&p->lock);
							if (p->owner != cw) {
								cw_log(CW_LOG_WARNING, "This isn't good...\n");
								return NULL;
							}
						}
						if (!p->subs[SUB_THREEWAY].owner) {
							cw_log(CW_LOG_NOTICE, "Whoa, threeway disappeared kinda randomly.\n");
							return NULL;
						}
						mssinceflash = cw_tvdiff_ms(cw_tvnow(), p->flashtime);
						cw_log(CW_LOG_DEBUG, "Last flash was %u ms ago\n", mssinceflash);
						if (mssinceflash < MIN_MS_SINCE_FLASH) {
							/* It hasn't been long enough since the last flashook.  This is probably a bounce on 
							   hanging up.  Hangup both channels now */
							if (p->subs[SUB_THREEWAY].owner)
								cw_queue_hangup(p->subs[SUB_THREEWAY].owner);
							p->subs[SUB_THREEWAY].owner->_softhangup |= CW_SOFTHANGUP_DEV;
							cw_log(CW_LOG_DEBUG, "Looks like a bounced flash, hanging up both calls on %d\n", p->channel);
							cw_channel_unlock(p->subs[SUB_THREEWAY].owner);
						} else if ((cw->pbx) || (cw->_state == CW_STATE_UP)) {
							if (p->transfer) {
								/* In any case this isn't a threeway call anymore */
								p->subs[SUB_REAL].inthreeway = 0;
								p->subs[SUB_THREEWAY].inthreeway = 0;
								/* Only attempt transfer if the phone is ringing; why transfer to busy tone eh? */
								if (!p->transfertobusy && cw->_state == CW_STATE_BUSY) {
									cw_channel_unlock(p->subs[SUB_THREEWAY].owner);
									/* Swap subs and dis-own channel */
									swap_subs(p, SUB_THREEWAY, SUB_REAL);
									p->owner = NULL;
									/* Ring the phone */
									dahdi_ring_phone(p);
								} else {
									if ((res = attempt_transfer(p)) < 0) {
										p->subs[SUB_THREEWAY].owner->_softhangup |= CW_SOFTHANGUP_DEV;
										if (p->subs[SUB_THREEWAY].owner)
											cw_channel_unlock(p->subs[SUB_THREEWAY].owner);
									} else if (res) {
										/* Don't actually hang up at this point */
										if (p->subs[SUB_THREEWAY].owner)
											cw_channel_unlock(p->subs[SUB_THREEWAY].owner);
										break;
									}
								}
							} else {
								p->subs[SUB_THREEWAY].owner->_softhangup |= CW_SOFTHANGUP_DEV;
								if (p->subs[SUB_THREEWAY].owner)
									cw_channel_unlock(p->subs[SUB_THREEWAY].owner);
							}
						} else {
							cw_channel_unlock(p->subs[SUB_THREEWAY].owner);
							/* Swap subs and dis-own channel */
							swap_subs(p, SUB_THREEWAY, SUB_REAL);
							p->owner = NULL;
							/* Ring the phone */
							dahdi_ring_phone(p);
						}
					}
				} else {
					cw_log(CW_LOG_WARNING, "Got a hangup and my index is %d?\n", curindex);
				}
				/* Fall through */
			default:
				dahdi_disable_ec(p);
				return NULL;
			}
			break;
		case DAHDI_EVENT_RINGOFFHOOK:
			if (p->inalarm) break;
			if (p->radio)
			{
				p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
				p->subs[curindex].f.subclass = CW_CONTROL_RADIO_KEY;
				break;
			}
			/* for E911, its supposed to wait for offhook then dial
			   the second half of the dial string */
			if ((p->sig == SIG_E911) && (cw->_state == CW_STATE_DIALING_OFFHOOK)) {
				c = strchr(p->dialdest, '/');
				if (c)
					c++;
				else
					c = p->dialdest;
				if (*c) snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*0%s#", c);
				else cw_copy_string(p->dop.dialstr,"M*2#", sizeof(p->dop.dialstr));
				if (strlen(p->dop.dialstr) > 4) {
					memset(p->echorest, 'w', sizeof(p->echorest) - 1);
					strcpy(p->echorest + (p->echotraining / 401) + 1, p->dop.dialstr + strlen(p->dop.dialstr) - 2);
					p->echorest[sizeof(p->echorest) - 1] = '\0';
					p->echobreak = 1;
					p->dop.dialstr[strlen(p->dop.dialstr)-2] = '\0';
				} else
					p->echobreak = 0;
				if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop)) {
					x = DAHDI_ONHOOK;
					ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
					cw_log(CW_LOG_WARNING, "Dialing failed on channel %d: %s\n", p->channel, strerror(errno));
					return NULL;
					}
				p->dialing = 1;
				return &p->subs[curindex].f;
			}
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				switch(cw->_state) {
				case CW_STATE_RINGING:
					dahdi_enable_ec(p);
					dahdi_train_ec(p);
					p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
					p->subs[curindex].f.subclass = CW_CONTROL_ANSWER;
					/* Make sure it stops ringing */
					dahdi_set_hook(p->subs[curindex].dfd, DAHDI_OFFHOOK);
					cw_log(CW_LOG_DEBUG, "channel %d answered\n", p->channel);
					/* Cancel any running CallerID spill */
					free(p->cidspill);
					p->cidspill = NULL;
					free(p->cidspill2);
					p->cidspill2 = NULL;
					p->dialing = 0;
					p->callwaitcas = 0;
					if (p->confirmanswer) {
						/* Ignore answer if "confirm answer" is enabled */
						p->subs[curindex].f.frametype = CW_FRAME_NULL;
						p->subs[curindex].f.subclass = 0;
					} else if (!cw_strlen_zero(p->dop.dialstr)) {
						/* nick@dccinc.com 4/3/03 - fxo should be able to do deferred dialing */
						res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop);
						if (res < 0) {
							cw_log(CW_LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", p->channel);
							p->dop.dialstr[0] = '\0';
							return NULL;
						} else {
							cw_log(CW_LOG_DEBUG, "Sent FXO deferred digit string: %s\n", p->dop.dialstr);
							p->subs[curindex].f.frametype = CW_FRAME_NULL;
							p->subs[curindex].f.subclass = 0;
							p->dialing = 1;
						}
						p->dop.dialstr[0] = '\0';
						cw_setstate(cw, CW_STATE_DIALING);
					} else
						cw_setstate(cw, CW_STATE_UP);
					return &p->subs[curindex].f;
				case CW_STATE_DOWN:
					cw_setstate(cw, CW_STATE_RING);
					cw->rings = 1;
					p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
					p->subs[curindex].f.subclass = CW_CONTROL_OFFHOOK;
					cw_log(CW_LOG_DEBUG, "channel %d picked up\n", p->channel);
					return &p->subs[curindex].f;
				case CW_STATE_UP:
					/* Make sure it stops ringing */
					dahdi_set_hook(p->subs[curindex].dfd, DAHDI_OFFHOOK);
					/* Okay -- probably call waiting*/
					if ((chan = cw_bridged_channel(p->owner))) {
						cw_moh_stop(chan);
						cw_object_put(chan);
					}
					break;
				case CW_STATE_RESERVED:
					/* Start up dialtone */
					if (cw_app_has_voicemail(p->mailbox, NULL))
						res = tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_STUTTER);
					else
						res = tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_DIALTONE);
					break;
				default:
					cw_log(CW_LOG_WARNING, "FXO phone off hook in weird state %d??\n", cw->_state);
				}
				break;
			case SIG_FXSLS:
			case SIG_FXSGS:
			case SIG_FXSKS:
				if (cw->_state == CW_STATE_RING)
					p->ringt = p->ringt_base;

				/* If we get a ring then we cannot be in 
				 * reversed polarity. So we reset to idle */
				cw_log(CW_LOG_DEBUG, "Setting IDLE polarity due "
					"to ring. Old polarity was %d\n", 
					p->polarity);
				p->polarity = POLARITY_IDLE;

				/* Fall through */
			case SIG_EM:
			case SIG_EM_E1:
			case SIG_EMWINK:
			case SIG_FEATD:
			case SIG_FEATDMF:
			case SIG_FEATDMF_TA:
			case SIG_E911:
			case SIG_FEATB:
			case SIG_SF:
			case SIG_SFWINK:
			case SIG_SF_FEATD:
			case SIG_SF_FEATDMF:
			case SIG_SF_FEATB:
				if (cw->_state == CW_STATE_PRERING)
					cw_setstate(cw, CW_STATE_RING);
				if ((cw->_state == CW_STATE_DOWN) || (cw->_state == CW_STATE_RING)) {
					if (option_debug)
						cw_log(CW_LOG_DEBUG, "Ring detected\n");
					p->subs[curindex].f.frametype = CW_FRAME_NULL;
					p->subs[curindex].f.subclass = 0;
				} else if (p->outgoing && ((cw->_state == CW_STATE_RINGING) || (cw->_state == CW_STATE_DIALING))) {
					if (option_debug)
						cw_log(CW_LOG_DEBUG, "Line answered\n");
					if (p->confirmanswer) {
						p->subs[curindex].f.frametype = CW_FRAME_NULL;
						p->subs[curindex].f.subclass = 0;
					} else {
						p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
						p->subs[curindex].f.subclass = CW_CONTROL_ANSWER;
						cw_setstate(cw, CW_STATE_UP);
					}
				} else if (cw->_state != CW_STATE_RING)
					cw_log(CW_LOG_WARNING, "Ring/Off-hook in strange state %d on channel %d\n", cw->_state, p->channel);
				break;
			default:
				cw_log(CW_LOG_WARNING, "Don't know how to handle ring/off hook for signalling %d\n", p->sig);
			}
			break;
#ifdef DAHDI_EVENT_RINGBEGIN
		case DAHDI_EVENT_RINGBEGIN:
			switch(p->sig) {
			case SIG_FXSLS:
			case SIG_FXSGS:
			case SIG_FXSKS:
				if (cw->_state == CW_STATE_RING)
					p->ringt = p->ringt_base;
				break;
			}
			break;
#endif			
		case DAHDI_EVENT_RINGEROFF:
			if (p->inalarm) break;
			if (p->radio) break;
			cw->rings++;
			if (p->cidspill && cw->rings == p->cidrings)
				cw_log(CW_LOG_DEBUG, "%s: Send post-ring caller ID\n", cw->name);
			p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
			p->subs[curindex].f.subclass = CW_CONTROL_RINGING;
			break;
		case DAHDI_EVENT_RINGERON:
			if (p->cidspill && cw->rings > p->cidrings) {
				cw_log(CW_LOG_WARNING, "%s: Didn't finish sending caller ID before start of ring!\n", cw->name);
				free(p->cidspill);
				p->cidspill = NULL;
				p->callwaitcas = 0;
			}
			break;
		case DAHDI_EVENT_NOALARM:
			p->inalarm = 0;
#ifdef ZAPATA_PRI
			/* Extremely unlikely but just in case */
			if (p->bearer)
				p->bearer->inalarm = 0;
#endif				
			cw_log(CW_LOG_NOTICE, "Alarm cleared on channel %d\n", p->channel);
			cw_manager_event(EVENT_FLAG_SYSTEM, "AlarmClear",
				1,
				cw_msg_tuple("Channel", "%d", p->channel)
			);
			break;
		case DAHDI_EVENT_WINKFLASH:
			if (p->inalarm) break;
			if (p->radio) break;
			/* Remember last time we got a flash-hook */
			gettimeofday(&p->flashtime, NULL);
			switch(p->sig) {
			case SIG_FXOLS:
			case SIG_FXOGS:
			case SIG_FXOKS:
				cw_log(CW_LOG_DEBUG, "Winkflash, index: %d, normal: %d, callwait: %d, thirdcall: %d\n",
					curindex, p->subs[SUB_REAL].dfd, p->subs[SUB_CALLWAIT].dfd, p->subs[SUB_THREEWAY].dfd);
				p->callwaitcas = 0;

				if (curindex != SUB_REAL) {
					cw_log(CW_LOG_WARNING, "Got flash hook with index %d on channel %d?!?\n", curindex, p->channel);
					goto winkflashdone;
				}
				
				if (p->subs[SUB_CALLWAIT].owner) {
					/* Swap to call-wait */
					swap_subs(p, SUB_REAL, SUB_CALLWAIT);
					tone_zone_play_tone(p->subs[SUB_REAL].dfd, -1);
					p->owner = p->subs[SUB_REAL].owner;
					cw_log(CW_LOG_DEBUG, "Making %s the new owner\n", p->owner->name);
					if (p->owner->_state == CW_STATE_RINGING) {
						cw_setstate(p->owner, CW_STATE_UP);
						p->subs[SUB_REAL].needanswer = 1;
					}
					p->callwaitingrepeat = 0;
					p->cidcwexpire = 0;
					/* Start music on hold if appropriate */
					if (!p->subs[SUB_CALLWAIT].inthreeway && (chan = cw_bridged_channel(p->subs[SUB_CALLWAIT].owner))) {
						cw_moh_start(chan, NULL);
						cw_object_put(chan);
					}
					if ((chan = cw_bridged_channel(p->subs[SUB_REAL].owner))) {
						cw_moh_stop(chan);
						cw_object_put(chan);
					}
				} else if (!p->subs[SUB_THREEWAY].owner) {
					char cid_num[256];
					char cid_name[256];

					if (!p->threewaycalling) {
						/* Just send a flash if no 3-way calling */
						p->subs[SUB_REAL].needflash = 1;
						goto winkflashdone;
					} else if (!check_for_conference(p)) {
						if (p->dahditrcallerid && p->owner) {
							if (p->owner->cid.cid_num)
								cw_copy_string(cid_num, p->owner->cid.cid_num, sizeof(cid_num));
							if (p->owner->cid.cid_name)
								cw_copy_string(cid_name, p->owner->cid.cid_name, sizeof(cid_name));
						}
						/* XXX This section needs much more error checking!!! XXX */
						/* Start a 3-way call if feasible */
						if (!((cw->pbx) ||
						      (cw->_state == CW_STATE_UP) ||
						      (cw->_state == CW_STATE_RING))) {
							cw_log(CW_LOG_DEBUG, "Flash when call not up or ringing\n");
								goto winkflashdone;
						}
						if (alloc_sub(p, SUB_THREEWAY)) {
							cw_log(CW_LOG_WARNING, "Unable to allocate three-way subchannel\n");
							goto winkflashdone;
						}
						/* Make new channel */
						chan = dahdi_new(p, CW_STATE_RESERVED, 0, SUB_THREEWAY, 0, 0);
						if (p->dahditrcallerid) {
							if (!p->origcid_num)
								p->origcid_num = strdup(p->cid_num);
							if (!p->origcid_name)
								p->origcid_name = strdup(p->cid_name);
							cw_copy_string(p->cid_num, cid_num, sizeof(p->cid_num));
							cw_copy_string(p->cid_name, cid_name, sizeof(p->cid_name));
						}
						/* Swap things around between the three-way and real call */
						swap_subs(p, SUB_THREEWAY, SUB_REAL);
						/* Disable echo canceller for better dialing */
						dahdi_disable_ec(p);
						res = tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_DIALRECALL);
						if (res)
							cw_log(CW_LOG_WARNING, "Unable to start dial recall tone on channel %d\n", p->channel);
						p->owner = chan;
						if (!chan) {
							cw_log(CW_LOG_WARNING, "Cannot allocate new structure on channel %d\n", p->channel);
						} else if (cw_pthread_create(&threadid, &global_attr_detached, ss_thread, chan)) {
							cw_log(CW_LOG_WARNING, "Unable to start simple switch on channel %d\n", p->channel);
							res = tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
							dahdi_enable_ec(p);
							cw_hangup(chan);
						} else {
							if (option_verbose > 2)	
								cw_verbose(VERBOSE_PREFIX_3 "Started three way call on channel %d\n", p->channel);
							/* Start music on hold if appropriate */
							if ((chan = cw_bridged_channel(p->subs[SUB_THREEWAY].owner))) {
								cw_moh_start(chan, NULL);
								cw_object_put(chan);
							}
						}		
					}
				} else {
					/* Already have a 3 way call */
					if (p->subs[SUB_THREEWAY].inthreeway) {
						/* Call is already up, drop the last person */
						if (option_debug)
							cw_log(CW_LOG_DEBUG, "Got flash with three way call up, dropping last call on %d\n", p->channel);
						/* If the primary call isn't answered yet, use it */
						if ((p->subs[SUB_REAL].owner->_state != CW_STATE_UP) && (p->subs[SUB_THREEWAY].owner->_state == CW_STATE_UP)) {
							/* Swap back -- we're dropping the real 3-way that isn't finished yet*/
							swap_subs(p, SUB_THREEWAY, SUB_REAL);
							p->owner = p->subs[SUB_REAL].owner;
						}
						/* Drop the last call and stop the conference */
						if (option_verbose > 2)
							cw_verbose(VERBOSE_PREFIX_3 "Dropping three-way call on %s\n", p->subs[SUB_THREEWAY].owner->name);
						p->subs[SUB_THREEWAY].owner->_softhangup |= CW_SOFTHANGUP_DEV;
						p->subs[SUB_REAL].inthreeway = 0;
						p->subs[SUB_THREEWAY].inthreeway = 0;
					} else {
						/* Lets see what we're up to */
						if (((cw->pbx) || (cw->_state == CW_STATE_UP)) && 
						    (p->transfertobusy || (cw->_state != CW_STATE_BUSY))) {
							int otherindex = SUB_THREEWAY;

							if (option_verbose > 2)
								cw_verbose(VERBOSE_PREFIX_3 "Building conference on call on %s and %s\n", p->subs[SUB_THREEWAY].owner->name, p->subs[SUB_REAL].owner->name);
							/* Put them in the threeway, and flip */
							p->subs[SUB_THREEWAY].inthreeway = 1;
							p->subs[SUB_REAL].inthreeway = 1;
							if (cw->_state == CW_STATE_UP) {
								swap_subs(p, SUB_THREEWAY, SUB_REAL);
								otherindex = SUB_REAL;
							}
							if (p->subs[otherindex].owner && (chan = cw_bridged_channel(p->subs[otherindex].owner))) {
								cw_moh_stop(chan);
								cw_object_put(chan);
							}
							p->owner = p->subs[SUB_REAL].owner;
							if (cw->_state == CW_STATE_RINGING) {
								cw_log(CW_LOG_DEBUG, "Enabling ringtone on real and threeway\n");
								res = tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_RINGTONE);
								res = tone_zone_play_tone(p->subs[SUB_THREEWAY].dfd, DAHDI_TONE_RINGTONE);
							}
						} else {
							if (option_verbose > 2)
								cw_verbose(VERBOSE_PREFIX_3 "Dumping incomplete call on on %s\n", p->subs[SUB_THREEWAY].owner->name);
							swap_subs(p, SUB_THREEWAY, SUB_REAL);
							p->subs[SUB_THREEWAY].owner->_softhangup |= CW_SOFTHANGUP_DEV;
							p->owner = p->subs[SUB_REAL].owner;
							if (p->subs[SUB_REAL].owner && (chan = cw_bridged_channel(p->subs[SUB_REAL].owner))) {
								cw_moh_stop(chan);
								cw_object_put(chan);
							}
							dahdi_enable_ec(p);
						}
							
					}
				}
			winkflashdone:			       
				update_conf(p);
				break;
			case SIG_EM:
			case SIG_EM_E1:
			case SIG_EMWINK:
			case SIG_FEATD:
			case SIG_SF:
			case SIG_SFWINK:
			case SIG_SF_FEATD:
			case SIG_FXSLS:
			case SIG_FXSGS:
				if (p->dialing)
					cw_log(CW_LOG_DEBUG, "Ignoring wink on channel %d\n", p->channel);
				else
					cw_log(CW_LOG_DEBUG, "Got wink in weird state %d on channel %d\n", cw->_state, p->channel);
				break;
			case SIG_FEATDMF_TA:
				switch (p->whichwink) {
				case 0:
					cw_log(CW_LOG_DEBUG, "ANI2 set to '%d' and ANI is '%s'\n", p->owner->cid.cid_ani2, p->owner->cid.cid_ani);
					snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "M*%d%s#", p->owner->cid.cid_ani2, p->owner->cid.cid_ani);
					break;
				case 1:
					cw_copy_string(p->dop.dialstr, p->finaldial, sizeof(p->dop.dialstr));
					break;
				case 2:
					cw_log(CW_LOG_WARNING, "Received unexpected wink on channel of type SIG_FEATDMF_TA\n");
					return NULL;
				}
				p->whichwink++;
				/* Fall through */
			case SIG_FEATDMF:
			case SIG_E911:
			case SIG_FEATB:
			case SIG_SF_FEATDMF:
			case SIG_SF_FEATB:
				/* FGD MF *Must* wait for wink */
				if (!cw_strlen_zero(p->dop.dialstr))
					res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop);
				else if (res < 0) {
					cw_log(CW_LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", p->channel);
					p->dop.dialstr[0] = '\0';
					return NULL;
				} else 
					cw_log(CW_LOG_DEBUG, "Sent deferred digit string: %s\n", p->dop.dialstr);
				p->dop.dialstr[0] = '\0';
				break;
			default:
				cw_log(CW_LOG_WARNING, "Don't know how to handle ring/off hoook for signalling %d\n", p->sig);
			}
			break;
		case DAHDI_EVENT_HOOKCOMPLETE:
			if (p->inalarm) break;
			if (p->radio) break;
			switch(p->sig) {
			case SIG_FXSLS:  /* only interesting for FXS */
			case SIG_FXSGS:
			case SIG_FXSKS:
			case SIG_EM:
			case SIG_EM_E1:
			case SIG_EMWINK:
			case SIG_FEATD:
			case SIG_SF:
			case SIG_SFWINK:
			case SIG_SF_FEATD:
				if (!cw_strlen_zero(p->dop.dialstr)) 
					res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop);
				else if (res < 0) {
					cw_log(CW_LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", p->channel);
					p->dop.dialstr[0] = '\0';
					return NULL;
				} else 
					cw_log(CW_LOG_DEBUG, "Sent deferred digit string: %s\n", p->dop.dialstr);
				p->dop.dialstr[0] = '\0';
				p->dop.op = DAHDI_DIAL_OP_REPLACE;
				break;
			case SIG_FEATDMF:
			case SIG_E911:
			case SIG_FEATB:
			case SIG_SF_FEATDMF:
			case SIG_SF_FEATB:
				cw_log(CW_LOG_DEBUG, "Got hook complete in MF FGD, waiting for wink now on channel %d\n",p->channel);
				break;
			default:
				break;
			}
			break;
		case DAHDI_EVENT_POLARITY:
                        /*
                         * If we get a Polarity Switch event, check to see
                         * if we should change the polarity state and
                         * mark the channel as UP or if this is an indication
                         * of remote end disconnect.
                         */
                        if (p->polarity == POLARITY_IDLE) {
                                p->polarity = POLARITY_REV;
                                if (p->answeronpolarityswitch &&
                                    ((cw->_state == CW_STATE_DIALING) ||
                                     (cw->_state == CW_STATE_RINGING))) {
                                        cw_log(CW_LOG_DEBUG, "Answering on polarity switch!\n");
                                        cw_setstate(p->owner, CW_STATE_UP);
                                } else
                                        cw_log(CW_LOG_DEBUG, "Ignore switch to REVERSED Polarity on channel %d, state %d\n", p->channel, cw->_state);
			} 
			/* Removed else statement from here as it was preventing hangups from ever happening*/
			/* Added CW_STATE_RING in if statement below to deal with calling party hangups that take place when ringing */
			if(p->hanguponpolarityswitch &&
				(p->polarityonanswerdelay > 0) &&
			       (p->polarity == POLARITY_REV) &&
				((cw->_state == CW_STATE_UP) || (cw->_state == CW_STATE_RING)) ) {
                                /* Added log_debug information below to provide a better indication of what is going on */
				cw_log(CW_LOG_DEBUG, "Polarity Reversal event occured - DEBUG 1: channel %d, state %d, pol= %d, aonp= %x, honp= %x, pdelay= %d, tv= %d\n", p->channel, cw->_state, p->polarity, p->answeronpolarityswitch, p->hanguponpolarityswitch, p->polarityonanswerdelay, cw_tvdiff_ms(cw_tvnow(), p->polaritydelaytv) );
			
				if(cw_tvdiff_ms(cw_tvnow(), p->polaritydelaytv) > p->polarityonanswerdelay) {
					cw_log(CW_LOG_DEBUG, "Polarity Reversal detected and now Hanging up on channel %d\n", p->channel);
					cw_softhangup(p->owner, CW_SOFTHANGUP_EXPLICIT);
					p->polarity = POLARITY_IDLE;
				} else {
					cw_log(CW_LOG_DEBUG, "Polarity Reversal detected but NOT hanging up (too close to answer event) on channel %d, state %d\n", p->channel, cw->_state);
				}
			} else {
				p->polarity = POLARITY_IDLE;
				cw_log(CW_LOG_DEBUG, "Ignoring Polarity switch to IDLE on channel %d, state %d\n", p->channel, cw->_state);
			}
                     	/* Added more log_debug information below to provide a better indication of what is going on */
			cw_log(CW_LOG_DEBUG, "Polarity Reversal event occured - DEBUG 2: channel %d, state %d, pol= %d, aonp= %x, honp= %x, pdelay= %d, tv= %d\n", p->channel, cw->_state, p->polarity, p->answeronpolarityswitch, p->hanguponpolarityswitch, p->polarityonanswerdelay, cw_tvdiff_ms(cw_tvnow(), p->polaritydelaytv) );
			break;
		default:
			cw_log(CW_LOG_DEBUG, "Dunno what to do with event %d on channel %d\n", res, p->channel);
	}
	return &p->subs[curindex].f;
}

static struct cw_frame *__dahdi_exception(struct cw_channel *cw)
{
	struct dahdi_pvt *p = cw->tech_pvt;
	struct cw_frame *f;
	struct cw_channel *bchan;
	int res;
	int usedindex=-1;
	int curindex;

	curindex = dahdi_get_index(cw, p, 1);

	if ((!p->owner) && (!p->radio)) {
		/* If nobody owns us, absorb the event appropriately, otherwise
		   we loop indefinitely.  This occurs when, during call waiting, the
		   other end hangs up our channel so that it no longer exists, but we
		   have neither FLASH'd nor ONHOOK'd to signify our desire to
		   change to the other channel. */
		if (p->fake_event) {
			res = p->fake_event;
			p->fake_event = 0;
		} else
			res = dahdi_get_event(p->subs[SUB_REAL].dfd);
		/* Switch to real if there is one and this isn't something really silly... */
		if ((res != DAHDI_EVENT_RINGEROFF) && (res != DAHDI_EVENT_RINGERON) &&
			(res != DAHDI_EVENT_HOOKCOMPLETE)) {
			cw_log(CW_LOG_DEBUG, "Restoring owner of channel %d on event %d\n", p->channel, res);
			p->owner = p->subs[SUB_REAL].owner;
			if (p->owner && (bchan = cw_bridged_channel(p->owner))) {
				cw_moh_stop(bchan);
				cw_object_put(bchan);
			}
		}
		switch(res) {
		case DAHDI_EVENT_ONHOOK:
			dahdi_disable_ec(p);
			if (p->owner) {
				if (option_verbose > 2) 
					cw_verbose(VERBOSE_PREFIX_3 "Channel %s still has call, ringing phone\n", p->owner->name);
				dahdi_ring_phone(p);
				p->callwaitingrepeat = 0;
				p->cidcwexpire = 0;
			} else
				cw_log(CW_LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			update_conf(p);
			break;
		case DAHDI_EVENT_RINGOFFHOOK:
			dahdi_enable_ec(p);
			dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
			if (p->owner && (p->owner->_state == CW_STATE_RINGING)) {
				p->subs[SUB_REAL].needanswer = 1;
				p->dialing = 0;
			}
			break;
		case DAHDI_EVENT_HOOKCOMPLETE:
		case DAHDI_EVENT_RINGERON:
		case DAHDI_EVENT_RINGEROFF:
			/* Do nothing */
			break;
		case DAHDI_EVENT_WINKFLASH:
			gettimeofday(&p->flashtime, NULL);
			if (p->owner) {
				if (option_verbose > 2) 
					cw_verbose(VERBOSE_PREFIX_3 "Channel %d flashed to other channel %s\n", p->channel, p->owner->name);
				if (p->owner->_state != CW_STATE_UP) {
					/* Answer if necessary */
					usedindex = dahdi_get_index(p->owner, p, 0);
					if (usedindex > -1) {
						p->subs[usedindex].needanswer = 1;
					}
					cw_setstate(p->owner, CW_STATE_UP);
				}
				p->callwaitingrepeat = 0;
				p->cidcwexpire = 0;
				if ((bchan = cw_bridged_channel(p->owner))) {
					cw_moh_stop(bchan);
					cw_object_put(bchan);
				}
			} else
				cw_log(CW_LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			update_conf(p);
			break;
		default:
			cw_log(CW_LOG_WARNING, "Don't know how to absorb event %s\n", event2str(res));
		}
		p->subs[curindex].f.frametype = CW_FRAME_NULL;
		f = &p->subs[curindex].f;
		return f;
	}
	if (!p->radio) cw_log(CW_LOG_DEBUG, "%s: Exception on %d, channel %d\n", cw->name, cw->fds[0],p->channel);
	/* If it's not us, return NULL immediately */
	if (cw != p->owner) {
		cw_log(CW_LOG_WARNING, "We're %s, not %s\n", cw->name, p->owner->name);
		p->subs[curindex].f.frametype = CW_FRAME_NULL;
		f = &p->subs[curindex].f;
		return f;
	}
	f = dahdi_handle_event(cw);
	return f;
}

struct cw_frame *dahdi_exception(struct cw_channel *cw)
{
	struct dahdi_pvt *p = cw->tech_pvt;
	struct cw_frame *f;
	cw_mutex_lock(&p->lock);
	f = __dahdi_exception(cw);
	cw_mutex_unlock(&p->lock);
	return f;
}

struct cw_frame  *dahdi_read(struct cw_channel *cw)
{
	struct dahdi_pvt *p = cw->tech_pvt;
	int res;
	int curindex;
	struct cw_frame *f;
	

	cw_mutex_lock(&p->lock);
	
	curindex = dahdi_get_index(cw, p, 0);
	
	if (curindex < 0 || (p->radio && p->inalarm)) {
		if (curindex < 0)
			cw_log(CW_LOG_WARNING, "We dont exist?\n");
		cw_mutex_unlock(&p->lock);
		return NULL;
	}

	p->subs[curindex].f.ts += p->subs[curindex].f.duration;
	p->subs[curindex].f.seq_no++;
	p->subs[curindex].f.datalen = 0;
	p->subs[curindex].f.samples = 0;
	p->subs[curindex].f.duration = 0;

	/* make sure it sends initial key state as first frame */
	if (p->radio && (!p->firstradio))
	{
		struct dahdi_params ps;

		memset(&ps, 0, sizeof(ps));
		ps.channo = p->channel;
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &ps) < 0) {
			cw_mutex_unlock(&p->lock);
			return NULL;
		}
		p->firstradio = 1;
		p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
		p->subs[curindex].f.subclass = (ps.rxisoffhook ? CW_CONTROL_RADIO_KEY : CW_CONTROL_RADIO_UNKEY);
		cw_mutex_unlock(&p->lock);
		return &p->subs[curindex].f;
	}

	if (p->subs[curindex].needringing) {
		/* Send ringing frame if requested */
		p->subs[curindex].needringing = 0;
		p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
		p->subs[curindex].f.subclass = CW_CONTROL_RINGING;
		cw_setstate(cw, CW_STATE_RINGING);
		cw_mutex_unlock(&p->lock);
		return &p->subs[curindex].f;
	}

	if (p->subs[curindex].needbusy) {
		/* Send busy frame if requested */
		p->subs[curindex].needbusy = 0;
		p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
		p->subs[curindex].f.subclass = CW_CONTROL_BUSY;
		cw_mutex_unlock(&p->lock);
		return &p->subs[curindex].f;
	}

	if (p->subs[curindex].needcongestion) {
		/* Send congestion frame if requested */
		p->subs[curindex].needcongestion = 0;
		p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
		p->subs[curindex].f.subclass = CW_CONTROL_CONGESTION;
		cw_mutex_unlock(&p->lock);
		return &p->subs[curindex].f;
	}

	if (p->subs[curindex].needcallerid) {
		cw_set_callerid(cw, !cw_strlen_zero(p->lastcid_num) ? p->lastcid_num : NULL, 
							!cw_strlen_zero(p->lastcid_name) ? p->lastcid_name : NULL,
							!cw_strlen_zero(p->lastcid_num) ? p->lastcid_num : NULL
							);
		p->subs[curindex].needcallerid = 0;
	}
	
	if (p->subs[curindex].needanswer) {
		/* Send answer frame if requested */
		p->subs[curindex].needanswer = 0;
		p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
		p->subs[curindex].f.subclass = CW_CONTROL_ANSWER;
		cw_mutex_unlock(&p->lock);
		return &p->subs[curindex].f;
	}	
	
	if (p->subs[curindex].needflash) {
		/* Send flash frame if requested */
		p->subs[curindex].needflash = 0;
		p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
		p->subs[curindex].f.subclass = CW_CONTROL_FLASH;
		cw_mutex_unlock(&p->lock);
		return &p->subs[curindex].f;
	}	
	
	if (cw->rawreadformat == CW_FORMAT_SLINEAR) {
		if (!p->subs[curindex].linear) {
			p->subs[curindex].linear = 1;
			res = dahdi_setlinear(p->subs[curindex].dfd, p->subs[curindex].linear);
			if (res) 
				cw_log(CW_LOG_WARNING, "Unable to set channel %d (index %d) to linear mode.\n", p->channel, curindex);
		}
	} else if ((cw->rawreadformat == CW_FORMAT_ULAW) ||
		   (cw->rawreadformat == CW_FORMAT_ALAW)) {
		if (p->subs[curindex].linear) {
			p->subs[curindex].linear = 0;
			res = dahdi_setlinear(p->subs[curindex].dfd, p->subs[curindex].linear);
			if (res) 
				cw_log(CW_LOG_WARNING, "Unable to set channel %d (index %d) to companded mode.\n", p->channel, curindex);
		}
	} else {
		cw_log(CW_LOG_WARNING, "Don't know how to read frames in format %s\n", cw_getformatname(cw->rawreadformat));
		cw_mutex_unlock(&p->lock);
		return NULL;
	}

	p->subs[curindex].f.offset = CW_FRIENDLY_OFFSET;
	p->subs[curindex].f.data = (char *)p->subs[curindex].buffer + CW_FRIENDLY_OFFSET;

	CHECK_BLOCKING(cw);
	res = read(p->subs[curindex].dfd, p->subs[curindex].f.data, p->subs[curindex].linear ? READ_SIZE * 2 : READ_SIZE);
	cw_clear_flag(cw, CW_FLAG_BLOCKING);

	/* Check for hangup */
	if (res < 0) {
		f = NULL;
		if (res == -1)  {
			if (errno == EAGAIN) {
				/* Return "NULL" frame if there is nobody there */
				p->subs[curindex].f.frametype = CW_FRAME_NULL;
				cw_mutex_unlock(&p->lock);
				return &p->subs[curindex].f;
			} else if (errno == ELAST) {
				f = __dahdi_exception(cw);
			} else
				cw_log(CW_LOG_WARNING, "dahdi_rec: %s\n", strerror(errno));
		}
		cw_mutex_unlock(&p->lock);
		return f;
	}

	/* We read something so fill in datalen, samples and duration */
	p->subs[curindex].f.datalen = res;
	p->subs[curindex].f.samples = (p->subs[curindex].linear ? res / 2 : res);
	p->subs[curindex].f.duration = SAMPLES_TO_MS(p->subs[curindex].f.samples);

	if (p->tdd) {
		int c;

		/* if in TDD mode, see if we receive that */
		c = tdd_feed(p->tdd, p->subs[curindex].f.data, res, CW_LAW(p));
		if (c < 0) {
			cw_log(CW_LOG_DEBUG,"tdd_feed failed\n");
			cw_mutex_unlock(&p->lock);
			return NULL;
		}
		if (c) { /* if a char to return */
			p->subs[curindex].f.frametype = CW_FRAME_TEXT;
			p->subs[curindex].f.subclass = 0;
			p->subs[curindex].f.mallocd = 0;
			p->subs[curindex].f.offset = CW_FRIENDLY_OFFSET;
			p->subs[curindex].f.data = p->subs[curindex].buffer + CW_FRIENDLY_OFFSET;
			p->subs[curindex].f.datalen = 1;
			*((char *) p->subs[curindex].f.data) = c;
			cw_mutex_unlock(&p->lock);
			return &p->subs[curindex].f;
		}
	}

	/* Ok, so it's audio data */
	p->subs[curindex].f.frametype = CW_FRAME_VOICE;
	p->subs[curindex].f.subclass = cw->rawreadformat;

	if (p->ringt > 0) {
		p->ringt -= p->subs[curindex].f.samples;
		if (p->ringt <= 0) {
			cw_mutex_unlock(&p->lock);
			return NULL;
		}
	}


	if (p->callwaitingrepeat > 0) {
		p->callwaitingrepeat -= p->subs[curindex].f.samples;
		if (p->callwaitingrepeat <= 0) {
			/* Repeat callwaiting */
			p->callwaitrings++;
			dahdi_callwait(cw);
		}
	}

	if (p->cidcwexpire > 0) {
		p->cidcwexpire -= p->subs[curindex].f.samples;
		if (p->cidcwexpire <= 0) {
			/* Expire CID/CW */
			if (option_verbose > 2)
				cw_verbose(VERBOSE_PREFIX_3 "CPE does not support Call Waiting Caller*ID.\n");
			restore_conference(p);
		}
	}

	/* Handle CallerID Transmission */
	if (p->owner == cw && p->cidspill && (cw->_state == CW_STATE_UP || p->cid_send_on != CID_START_RING || cw->rings == p->cidrings)) {
		if (send_callerid(p)) {
			/* Once we're done sending caller ID we need to start ringing the line
			 * if it isn't up and we were sending caller ID before the first ring.
			 */
			if (cw->_state != CW_STATE_UP && p->cid_send_on != CID_START_RING && !cw->rings) {
				ioctl(p->subs[SUB_REAL].dfd, DAHDI_SYNC, &res);
				res = POLARITY_IDLE;
				ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETPOLARITY, &res);
				cw_log(CW_LOG_DEBUG, "%s: Start ringing\n", cw->name);
				res = DAHDI_RING;
				if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &res) && errno != EINPROGRESS)
					cw_log(CW_LOG_WARNING, "%s: Unable to ring phone: %s\n", cw->name, strerror(errno));
				/* Set up for post-ring caller ID */
				free(p->cidspill);
				p->cidspill = p->cidspill2;
				p->cidspill2 = NULL;
				p->cidlen = p->cidlen2;
				p->cidpos = 0;
				p->cid_send_on = CID_START_RING;
			} else {
				free(p->cidspill);
				p->cidspill = NULL;
			}
		}
	}

#if 0
	cw_log(CW_LOG_DEBUG, "Read %d of voice on %s\n", p->subs[curindex].f.datalen, cw->name);
#endif	
	if (p->dialing || /* Transmitting something */
	   (curindex && (cw->_state != CW_STATE_UP)) || /* Three-way or callwait that isn't up */
	   ((curindex == SUB_CALLWAIT) && !p->subs[SUB_CALLWAIT].inthreeway) /* Inactive and non-confed call-wait */
	   ) {
		/* Whoops, we're still dialing, or in a state where we shouldn't transmit....
		   don't send anything */
		p->subs[curindex].f.frametype = CW_FRAME_NULL;
		p->subs[curindex].f.subclass = 0;
		p->subs[curindex].f.datalen= 0;
		p->subs[curindex].f.samples = 0;
		p->subs[curindex].f.duration = 0;
	}
	if (p->dsp && (!p->ignoredtmf || p->callwaitcas || p->busydetect  || p->callprogress) && !curindex) {
		/* Perform busy detection. etc on the DAHDI line */
		f = cw_dsp_process(cw, p->dsp, &p->subs[curindex].f);
		if (f) {
			if ((f->frametype == CW_FRAME_CONTROL) && (f->subclass == CW_CONTROL_BUSY)) {
				if ((cw->_state == CW_STATE_UP) && !p->outgoing) {
					/* Treat this as a "hangup" instead of a "busy" on the assumption that
					   a busy  */
					f = NULL;
				}
			} else if (f->frametype == CW_FRAME_DTMF) {
#ifdef ZAPATA_PRI
				if (!p->proceeding && p->sig==SIG_PRI && p->pri && p->pri->overlapdial) {
					/* Don't accept in-band DTMF when in overlap dial mode */
					f->frametype = CW_FRAME_NULL;
					f->subclass = 0;
				}
#endif				
				/* DSP clears us of being pulse */
				p->pulsedial = 0;
			}
		}
	} else
		f = &p->subs[curindex].f;
	if (f && (f->frametype == CW_FRAME_DTMF)) {
		cw_log(CW_LOG_DEBUG, "DTMF digit: %c on %s\n", f->subclass, cw->name);
		if (p->confirmanswer) {
			cw_log(CW_LOG_DEBUG, "Confirm answer on %s!\n", cw->name);
			/* Upon receiving a DTMF digit, consider this an answer confirmation instead
			   of a DTMF digit */
			p->subs[curindex].f.frametype = CW_FRAME_CONTROL;
			p->subs[curindex].f.subclass = CW_CONTROL_ANSWER;
			f = &p->subs[curindex].f;
			/* Reset confirmanswer so DTMF's will behave properly for the duration of the call */
			p->confirmanswer = 0;
		} else if (p->callwaitcas) {
			if ((f->subclass == 'A') || (f->subclass == 'D')) {
				cw_log(CW_LOG_DEBUG, "Got some DTMF, but it's for the CAS\n");
				free(p->cidspill);
				send_cwcidspill(p);
			}
			p->callwaitcas = 0;
			p->subs[curindex].f.frametype = CW_FRAME_NULL;
			p->subs[curindex].f.subclass = 0;
			f = &p->subs[curindex].f;
		} else if (f->subclass == 'f') {
			/* Fax tone -- Handle and return NULL */
			if (!p->faxhandled) {
				p->faxhandled++;
				if (strcmp(cw->exten, "fax")) {
					const char *target_context = cw_strlen_zero(cw->proc_context)  ?  cw->context  :  cw->proc_context;

					if (cw_exists_extension(cw, target_context, "fax", 1, cw->cid.cid_num)) {
						if (option_verbose > 2)
							cw_verbose(VERBOSE_PREFIX_3 "Redirecting %s to fax extension\n", cw->name);
						/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
						pbx_builtin_setvar_helper(cw, "FAXEXTEN", cw->exten);
						if (cw_async_goto_n(cw, target_context, "fax", 1))
							cw_log(CW_LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", cw->name, target_context);
					} else
						cw_log(CW_LOG_NOTICE, "Fax detected, but no fax extension\n");
				} else
					cw_log(CW_LOG_DEBUG, "Already in a fax extension, not redirecting\n");
			} else
				cw_log(CW_LOG_DEBUG, "Fax already handled\n");
			dahdi_confmute(p, 0);
			p->subs[curindex].f.frametype = CW_FRAME_NULL;
			p->subs[curindex].f.subclass = 0;
			f = &p->subs[curindex].f;
		} else
			dahdi_confmute(p, 0);
	}

	/* If we have a fake_event, trigger exception to handle it */
	if (p->fake_event)
		cw_set_flag(cw, CW_FLAG_EXCEPTION);

	cw_mutex_unlock(&p->lock);
	return f;
}

static int my_dahdi_write(struct dahdi_pvt *p, unsigned char *buf, int len, int curindex, int linear)
{
	int sent=0;
	int size;
	int res;
	int fd;
	fd = p->subs[curindex].dfd;
	while(len) {
		size = len;
		if (size > (linear ? READ_SIZE * 2 : READ_SIZE))
			size = (linear ? READ_SIZE * 2 : READ_SIZE);
		res = write(fd, buf, size);
		if (res != size) {
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
			return sent;
		}
		len -= size;
		buf += size;
	}
	return sent;
}

static int dahdi_write(struct cw_channel *cw, struct cw_frame *frame)
{
	struct dahdi_pvt *p = cw->tech_pvt;
	int res;
	unsigned char outbuf[4096];
	int curindex;
	curindex = dahdi_get_index(cw, p, 0);
	if (curindex < 0) {
		cw_log(CW_LOG_WARNING, "%s doesn't really exist?\n", cw->name);
		return -1;
	}

#if 0
#ifdef ZAPATA_PRI
	cw_mutex_lock(&p->lock);
	if (!p->proceeding && p->sig==SIG_PRI && p->pri && !p->outgoing) {
		if (p->pri->pri) {		
			if (!pri_grab(p, p->pri)) {
					pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), !p->digital);
					pri_rel(p->pri);
			} else
					cw_log(CW_LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
		}
		p->proceeding=1;
	}
	cw_mutex_unlock(&p->lock);
#endif
#endif
	/* Write a frame of (presumably voice) data */
	if (frame->frametype != CW_FRAME_VOICE) {
		if (frame->frametype != CW_FRAME_IMAGE)
			cw_log(CW_LOG_WARNING, "Don't know what to do with frame type '%d'\n", frame->frametype);
		return 0;
	}
	if ((frame->subclass != CW_FORMAT_SLINEAR) && 
	    (frame->subclass != CW_FORMAT_ULAW) &&
	    (frame->subclass != CW_FORMAT_ALAW)) {
		cw_log(CW_LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		return -1;
	}
	if (p->dialing) {
		if (option_debug)
			cw_log(CW_LOG_DEBUG, "Dropping frame since I'm still dialing on %s...\n",cw->name);
		return 0;
	}
	if (!p->owner) {
		if (option_debug)
			cw_log(CW_LOG_DEBUG, "Dropping frame since there is no active owner on %s...\n",cw->name);
		return 0;
	}
	if (p->cidspill) {
		if (option_debug)
			cw_log(CW_LOG_DEBUG, "Dropping frame since I've still got a callerid spill\n");
		return 0;
	}
	/* Return if it's not valid data */
	if (!frame->data || !frame->datalen)
		return 0;
	if (frame->datalen > sizeof(outbuf) * 2) {
		cw_log(CW_LOG_WARNING, "Frame too large\n");
		return 0;
	}

	if (frame->subclass == CW_FORMAT_SLINEAR) {
		if (!p->subs[curindex].linear) {
			p->subs[curindex].linear = 1;
			res = dahdi_setlinear(p->subs[curindex].dfd, p->subs[curindex].linear);
			if (res)
				cw_log(CW_LOG_WARNING, "Unable to set linear mode on channel %d\n", p->channel);
		}
		res = my_dahdi_write(p, (unsigned char *)frame->data, frame->datalen, curindex, 1);
	} else {
		/* x-law already */
		if (p->subs[curindex].linear) {
			p->subs[curindex].linear = 0;
			res = dahdi_setlinear(p->subs[curindex].dfd, p->subs[curindex].linear);
			if (res)
				cw_log(CW_LOG_WARNING, "Unable to set companded mode on channel %d\n", p->channel);
		}
		res = my_dahdi_write(p, (unsigned char *)frame->data, frame->datalen, curindex, 0);
	}
	if (res < 0) {
		cw_log(CW_LOG_WARNING, "write failed: %s\n", strerror(errno));
		return -1;
	} 
	return 0;
}

static int dahdi_indicate(struct cw_channel *chan, int condition)
{
	struct dahdi_pvt *p = chan->tech_pvt;
	int res=-1;
	int curindex;
	int func = DAHDI_FLASH;
	cw_mutex_lock(&p->lock);
	curindex = dahdi_get_index(chan, p, 0);
	cw_log(CW_LOG_DEBUG, "Requested indication %d on channel %s\n", condition, chan->name);
	if (curindex == SUB_REAL) {
		switch(condition) {
		case CW_CONTROL_BUSY:
#ifdef ZAPATA_PRI
			if (p->priindication_oob && p->sig == SIG_PRI) {
				chan->hangupcause = CW_CAUSE_USER_BUSY;
				chan->_softhangup |= CW_SOFTHANGUP_DEV;
				res = 0;
			} else if (!p->progress && p->sig==SIG_PRI && p->pri && !p->outgoing) {
				if (p->pri->pri) {		
					if (!pri_grab(p, p->pri)) {
						pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
						pri_rel(p->pri);
					}
					else
						cw_log(CW_LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
				}
				p->progress = 1;
				res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_BUSY);
			} else
#endif
				res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_BUSY);
			break;
		case CW_CONTROL_RINGING:
#ifdef ZAPATA_PRI
			if ((!p->alerting) && p->sig==SIG_PRI && p->pri && !p->outgoing && (chan->_state != CW_STATE_UP)) {
				if (p->pri->pri) {		
					if (!pri_grab(p, p->pri)) {
						pri_acknowledge(p->pri->pri,p->call, PVT_TO_CHANNEL(p), !p->digital);
						pri_rel(p->pri);
					}
					else
						cw_log(CW_LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
				}
				p->alerting = 1;
			}
#endif
			res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_RINGTONE);
			if (chan->_state != CW_STATE_UP) {
				if ((chan->_state != CW_STATE_RING) ||
					((p->sig != SIG_FXSKS) &&
					 (p->sig != SIG_FXSLS) &&
					 (p->sig != SIG_FXSGS)))
					cw_setstate(chan, CW_STATE_RINGING);
			}
			break;
		case CW_CONTROL_PROCEEDING:
			cw_log(CW_LOG_DEBUG,"Received CW_CONTROL_PROCEEDING on %s\n",chan->name);
#ifdef ZAPATA_PRI
			if (!p->proceeding && p->sig==SIG_PRI && p->pri && !p->outgoing) {
				if (p->pri->pri) {		
					if (!pri_grab(p, p->pri)) {
						pri_proceeding(p->pri->pri,p->call, PVT_TO_CHANNEL(p), !p->digital);
						pri_rel(p->pri);
					}
					else
						cw_log(CW_LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
				}
				p->proceeding = 1;
			}
#endif
			/* don't continue in cw_indicate */
			res = 0;
			break;
		case CW_CONTROL_PROGRESS:
			cw_log(CW_LOG_DEBUG,"Received CW_CONTROL_PROGRESS on %s\n",chan->name);
#ifdef ZAPATA_PRI
			p->digital = 0;	/* Digital-only calls isn't allows any inband progress messages */
			if (!p->progress && p->sig==SIG_PRI && p->pri && !p->outgoing) {
				if (p->pri->pri) {		
					if (!pri_grab(p, p->pri)) {
						pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
						pri_rel(p->pri);
					}
					else
						cw_log(CW_LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
				}
				p->progress = 1;
			}
#endif
			/* don't continue in cw_indicate */
			res = 0;
			break;
		case CW_CONTROL_CONGESTION:
			chan->hangupcause = CW_CAUSE_CONGESTION;
#ifdef ZAPATA_PRI
			if (p->priindication_oob && p->sig == SIG_PRI) {
				chan->hangupcause = CW_CAUSE_SWITCH_CONGESTION;
				chan->_softhangup |= CW_SOFTHANGUP_DEV;
				res = 0;
			} else if (!p->progress && p->sig==SIG_PRI && p->pri && !p->outgoing) {
				if (p->pri) {		
					if (!pri_grab(p, p->pri)) {
						pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
						pri_rel(p->pri);
					} else
						cw_log(CW_LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);
				}
				p->progress = 1;
				res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_CONGESTION);
			} else
#endif
				res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_CONGESTION);
			break;
#ifdef ZAPATA_PRI
		case CW_CONTROL_HOLD:
			if (p->pri) {
				if (!pri_grab(p, p->pri)) {
					res = pri_notify(p->pri->pri, p->call, p->prioffset, PRI_NOTIFY_REMOTE_HOLD);
					pri_rel(p->pri);
				} else
						cw_log(CW_LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);			
			}
			break;
		case CW_CONTROL_UNHOLD:
			if (p->pri) {
				if (!pri_grab(p, p->pri)) {
					res = pri_notify(p->pri->pri, p->call, p->prioffset, PRI_NOTIFY_REMOTE_RETRIEVAL);
					pri_rel(p->pri);
				} else
						cw_log(CW_LOG_WARNING, "Unable to grab PRI on span %d\n", p->span);			
			}
			break;
#endif
		case CW_CONTROL_RADIO_KEY:
			if (p->radio) 
			    res =  dahdi_set_hook(p->subs[curindex].dfd, DAHDI_OFFHOOK);
			res = 0;
			break;
		case CW_CONTROL_RADIO_UNKEY:
			if (p->radio)
			    res =  dahdi_set_hook(p->subs[curindex].dfd, DAHDI_RINGOFF);
			res = 0;
			break;
		case CW_CONTROL_FLASH:
			/* flash hookswitch */
			if (ISTRUNK(p) && (p->sig != SIG_PRI)) {
				/* Clear out the dial buffer */
				p->dop.dialstr[0] = '\0';
				if ((ioctl(p->subs[SUB_REAL].dfd,DAHDI_HOOK,&func) == -1) && (errno != EINPROGRESS)) {
					cw_log(CW_LOG_WARNING, "Unable to flash external trunk on channel %s: %s\n", 
						chan->name, strerror(errno));
				} else
					res = 0;
			} else
				res = 0;
			break;
		case -1:
			res = tone_zone_play_tone(p->subs[curindex].dfd, -1);
			break;
		}
	} else
		res = 0;
	cw_mutex_unlock(&p->lock);
	return res;
}

static struct cw_channel *dahdi_new(struct dahdi_pvt *i, int state, int startpbx, int curindex, int law, int transfercapability)
{
	char buf[sizeof(((struct cw_channel *)0)->name)];
	struct cw_channel *tmp;
	int deflaw;
	int res;
	int x,y;
	int features;
	struct dahdi_params ps;

	CW_UNUSED(transfercapability);

	if (i->subs[curindex].owner) {
		cw_log(CW_LOG_WARNING, "Channel %d already has a %s call\n", i->channel,subnames[curindex]);
		return NULL;
	}

	y = 1;
	do {
#ifdef ZAPATA_PRI
		if (i->bearer || (i->pri && (i->sig == SIG_FXSKS)))
			snprintf(buf, sizeof(buf), "DAHDI/%d:%d-%d", i->pri->trunkgroup, i->channel, y);
		else
#endif
		if (i->channel == CHAN_PSEUDO)
			snprintf(buf, sizeof(buf), "DAHDI/pseudo-%ld", cw_random());
		else
			snprintf(buf, sizeof(buf), "DAHDI/%d-%d", i->channel, y);
		for (x = 0; x < 3; x++) {
			if ((curindex != x) && i->subs[x].owner && !strcasecmp(buf, i->subs[x].owner->name))
				break;
		}
		y++;
	} while (x < 3);
	tmp = cw_channel_alloc(0, "%s", buf);
	if (tmp) {
		tmp->tech = &dahdi_tech;
		memset(&ps, 0, sizeof(ps));
		ps.channo = i->channel;
		res = ioctl(i->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &ps);
		if (res) {
			cw_log(CW_LOG_WARNING, "Unable to get parameters, assuming MULAW\n");
			ps.curlaw = DAHDI_LAW_MULAW;
		}
		if (ps.curlaw == DAHDI_LAW_ALAW)
			deflaw = CW_FORMAT_ALAW;
		else
			deflaw = CW_FORMAT_ULAW;
		if (law) {
			if (law == DAHDI_LAW_ALAW)
				deflaw = CW_FORMAT_ALAW;
			else
				deflaw = CW_FORMAT_ULAW;
		}
		deflaw = CW_FORMAT_SLINEAR;
		tmp->type = tmp->tech->type;
		tmp->fds[0] = i->subs[curindex].dfd;
		tmp->nativeformats = CW_FORMAT_SLINEAR | deflaw;
		/* Start out assuming ulaw since it's smaller :) */
		tmp->rawreadformat = deflaw;
		tmp->readformat = deflaw;
		tmp->rawwriteformat = deflaw;
		tmp->writeformat = deflaw;
		i->subs[curindex].linear = 0;
		dahdi_setlinear(i->subs[curindex].dfd, i->subs[curindex].linear);
		features = 0;
		if (i->busydetect && CANBUSYDETECT(i)) {
			features |= DSP_FEATURE_BUSY_DETECT;
		}
		if ((i->callprogress & 1) && CANPROGRESSDETECT(i)) {
			features |= DSP_FEATURE_CALL_PROGRESS;
		}
		if ((!i->outgoing && (i->callprogress & 4)) || 
		    (i->outgoing && (i->callprogress & 2))) {
			features |= DSP_FEATURE_FAX_CNG_DETECT;
		}
#ifdef DAHDI_TONEDETECT
		x = DAHDI_TONEDETECT_ON | DAHDI_TONEDETECT_MUTE;
		if (ioctl(i->subs[curindex].dfd, DAHDI_TONEDETECT, &x)) {
#endif		
			i->hardwaredtmf = 0;
			features |= DSP_FEATURE_DTMF_DETECT;
#ifdef DAHDI_TONEDETECT
		} else if (NEED_MFDETECT(i)) {
			i->hardwaredtmf = 1;
			features |= DSP_FEATURE_DTMF_DETECT;
		}
#endif
		if (features) {
			if (i->dsp) {
				cw_log(CW_LOG_DEBUG, "Already have a dsp on %s?\n", tmp->name);
			} else {
				i->dsp = cw_dsp_new();
				if (i->dsp) {
#ifdef ZAPATA_PRI
					/* We cannot do progress detection until receives PROGRESS message */
					if (i->outgoing && (i->sig == SIG_PRI)) {
						/* Remember requested DSP features, don't treat
						   talking as ANSWER */
						i->dsp_features = features & ~DSP_PROGRESS_TALK;
						features = 0;
					}
#endif
					cw_dsp_set_features(i->dsp, features);
					cw_dsp_digitmode(i->dsp, DSP_DIGITMODE_DTMF | i->dtmfrelax);
					if (!cw_strlen_zero(progzone))
						cw_dsp_set_call_progress_zone(i->dsp, progzone);
					if (i->busydetect && CANBUSYDETECT(i)) {
						cw_dsp_set_busy_count(i->dsp, i->busycount);
						cw_dsp_set_busy_pattern(i->dsp, i->busy_tonelength, i->busy_quietlength);
					}
				}
			}
		}
		
		if (state == CW_STATE_RING)
			tmp->rings = 1;
		tmp->tech_pvt = i;
		if ((i->sig == SIG_FXOKS) || (i->sig == SIG_FXOGS) || (i->sig == SIG_FXOLS)) {
			/* Only FXO signalled stuff can be picked up */
			tmp->callgroup = i->callgroup;
			tmp->pickupgroup = i->pickupgroup;
		}
		if (!cw_strlen_zero(i->language))
			cw_copy_string(tmp->language, i->language, sizeof(tmp->language));
		if (!cw_strlen_zero(i->musicclass))
			cw_copy_string(tmp->musicclass, i->musicclass, sizeof(tmp->musicclass));
		if (!i->owner)
			i->owner = tmp;
		if (!cw_strlen_zero(i->accountcode))
			cw_copy_string(tmp->accountcode, i->accountcode, sizeof(tmp->accountcode));
		if (i->amaflags)
			tmp->amaflags = i->amaflags;
		i->subs[curindex].owner = tmp;
		cw_copy_string(tmp->context, i->context, sizeof(tmp->context));
		/* Copy call forward info */
		cw_copy_string(tmp->call_forward, i->call_forward, sizeof(tmp->call_forward));
		/* If we've been told "no ADSI" then enforce it */
		if (!i->adsi)
			tmp->adsicpe = CW_ADSI_UNAVAILABLE;
		if (!cw_strlen_zero(i->exten))
			cw_copy_string(tmp->exten, i->exten, sizeof(tmp->exten));
		if (!cw_strlen_zero(i->rdnis))
			tmp->cid.cid_rdnis = strdup(i->rdnis);
		if (!cw_strlen_zero(i->dnid))
			tmp->cid.cid_dnid = strdup(i->dnid);

#ifdef PRI_ANI
		cw_set_callerid(tmp, i->cid_num, i->cid_name, cw_strlen_zero(i->cid_ani) ? i->cid_num : i->cid_ani);
#else
		cw_set_callerid(tmp, i->cid_num, i->cid_name, i->cid_num);
#endif
		tmp->cid.cid_pres = i->callingpres;
		tmp->cid.cid_ton = i->cid_ton;
#ifdef ZAPATA_PRI
		tmp->transfercapability = transfercapability;
		pbx_builtin_setvar_helper(tmp, "TRANSFERCAPABILITY", cw_transfercapability2str(transfercapability));
		if (transfercapability & PRI_TRANS_CAP_DIGITAL) {
			i->digital = 1;
		}
		/* Assume calls are not idle calls unless we're told differently */
		i->isidlecall = 0;
		i->alreadyhungup = 0;
#endif
		cw_jb_configure(tmp, &i->jbconf);

		/* clear the fake event in case we posted one before we had cw_channel */
		i->fake_event = 0;
		/* Assure there is no confmute on this channel */
		dahdi_confmute(i, 0);
		cw_setstate(tmp, state);
		if (startpbx) {
			if (cw_pbx_start(tmp)) {
				cw_log(CW_LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				cw_hangup(tmp);
				tmp = NULL;
			}
		}
	} else
		cw_log(CW_LOG_WARNING, "Unable to allocate channel structure\n");

	return tmp;
}


static int my_getsigstr(struct cw_channel *chan, char *str, const char *term, int ms)
{
	char c;

	*str = 0; /* start with empty output buffer */
	for (;;)
	{
		/* Wait for the first digit (up to specified ms). */
		c = cw_waitfordigit(chan, ms);
		/* if timeout, hangup or error, return as such */
		if (c < 1)
			return c;
		*str++ = c;
		*str = 0;
		if (strchr(term, c))
			return 1;
	}
}

static int dahdi_wink(struct dahdi_pvt *p, int curindex)
{
	int j;
	dahdi_set_hook(p->subs[curindex].dfd, DAHDI_WINK);
	for(;;)
	{
		   /* set bits of interest */
		j = DAHDI_IOMUX_SIGEVENT;
		    /* wait for some happening */
		if (ioctl(p->subs[curindex].dfd,DAHDI_IOMUX,&j) == -1) return(-1);
		   /* exit loop if we have it */
		if (j & DAHDI_IOMUX_SIGEVENT) break;
	}
	  /* get the event info */
	if (ioctl(p->subs[curindex].dfd,DAHDI_GETEVENT,&j) == -1) return(-1);
	return 0;
}

static void ss_thread_adsi_get(struct dahdi_pvt *p, adsi_rx_state_t *state, const uint8_t *msg, int len) {
	if (!callerid_get(state, p->owner, msg, len)) {
		free(p->adsi_rx1);
		p->adsi_rx1 = NULL;
		free(p->adsi_rx2);
		p->adsi_rx2 = NULL;
	}
}
static void ss_thread_adsi_get1(void *data, const uint8_t *msg, int len) {
	struct dahdi_pvt *p = data;
	ss_thread_adsi_get(p, p->adsi_rx1, msg, len);
}
static void ss_thread_adsi_get2(void *data, const uint8_t *msg, int len) {
	struct dahdi_pvt *p = data;
	ss_thread_adsi_get(p, p->adsi_rx2, msg, len);
}

static void *ss_thread(void *data)
{
	char dtmfbuf[300];
	char exten[CW_MAX_EXTENSION]="";
	char exten2[CW_MAX_EXTENSION]="";
	struct cw_channel *chan = data;
	struct cw_channel *bchan;
	struct dahdi_pvt *p = chan->tech_pvt;
	int curRingData[2 + arraysize(drings.ringnum[0].ring)];
	int receivedRingT;
	int counter;
	int samples = 0;

	int i;
	int timeout;
	int getforward=0;
	char *s1, *s2;
	int len = 0;
	int res;
	int curindex;

	if (option_verbose > 2) 
		cw_verbose( VERBOSE_PREFIX_3 "Starting simple switch on '%s'\n", chan->name);
	curindex = dahdi_get_index(chan, p, 1);
	if (curindex < 0) {
		cw_log(CW_LOG_WARNING, "Huh?\n");
		cw_hangup(chan);
		return NULL;
	}
	if (p->dsp)
		cw_dsp_digitreset(p->dsp);
	switch(p->sig) {
#ifdef ZAPATA_PRI
	case SIG_PRI:
		/* Now loop looking for an extension */
		cw_copy_string(exten, p->exten, sizeof(exten));
		len = strlen(exten);
		res = 0;
		while((len < CW_MAX_EXTENSION-1) && cw_matchmore_extension(chan, chan->context, exten, 1, p->cid_num)) {
			if (len && !cw_ignore_pattern(chan->context, exten))
				tone_zone_play_tone(p->subs[curindex].dfd, -1);
			else
				tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_DIALTONE);
			if (cw_exists_extension(chan, chan->context, exten, 1, p->cid_num))
				timeout = matchdigittimeout;
			else
				timeout = gendigittimeout;
			res = cw_waitfordigit(chan, timeout);
			if (res < 0) {
				cw_log(CW_LOG_DEBUG, "waitfordigit returned < 0...\n");
				cw_hangup(chan);
				return NULL;
			} else if (res) {
				exten[len++] = res;
			} else
				break;
		}
		/* if no extension was received ('unspecified') on overlap call, use the 's' extension */
		if (cw_strlen_zero(exten)) {
			if (option_verbose > 2)
				cw_verbose(VERBOSE_PREFIX_3 "Going to extension s|1 because of empty extension received on overlap call\n");
			exten[0] = 's';
			exten[1] = '\0';
		}
		tone_zone_play_tone(p->subs[curindex].dfd, -1);
		if (cw_exists_extension(chan, chan->context, exten, 1, p->cid_num)) {
			/* Start the real PBX */
			cw_copy_string(chan->exten, exten, sizeof(chan->exten));
			if (p->dsp) cw_dsp_digitreset(p->dsp);
			dahdi_enable_ec(p);
			cw_setstate(chan, CW_STATE_RING);
			res = cw_pbx_run(chan);
			if (res) {
				cw_log(CW_LOG_WARNING, "PBX exited non-zero!\n");
			}
		} else {
			cw_log(CW_LOG_DEBUG, "No such possible extension '%s' in context '%s'\n", exten, chan->context);
			chan->hangupcause = CW_CAUSE_UNALLOCATED;
			cw_hangup(chan);
			p->exten[0] = '\0'; /* FIXME: is this right? We didn't use it except as an initial base */
			/* Since we send release complete here, we won't get one */
			p->call = NULL;
		}
		return NULL;
		break;
#endif
	case SIG_FEATD:
	case SIG_FEATDMF:
	case SIG_E911:
	case SIG_FEATB:
	case SIG_EMWINK:
	case SIG_SF_FEATD:
	case SIG_SF_FEATDMF:
	case SIG_SF_FEATB:
	case SIG_SFWINK:
		if (dahdi_wink(p, curindex))
			return NULL;
		/* Fall through */
	case SIG_EM:
	case SIG_EM_E1:
	case SIG_SF:
		res = tone_zone_play_tone(p->subs[curindex].dfd, -1);
		if (p->dsp)
			cw_dsp_digitreset(p->dsp);
		/* set digit mode appropriately */
		if (p->dsp) {
			if (NEED_MFDETECT(p))
				cw_dsp_digitmode(p->dsp,DSP_DIGITMODE_MF | p->dtmfrelax); 
			else 
				cw_dsp_digitmode(p->dsp,DSP_DIGITMODE_DTMF | p->dtmfrelax);
		}
		dtmfbuf[0] = 0;
		/* Wait for the first digit only if immediate=no */
		if (!p->immediate)
			/* Wait for the first digit (up to 5 seconds). */
			res = cw_waitfordigit(chan, 5000);
		else res = 0;
		if (res > 0) {
			/* save first char */
			dtmfbuf[0] = res;
			switch(p->sig) {
			case SIG_FEATD:
			case SIG_SF_FEATD:
				res = my_getsigstr(chan, dtmfbuf + 1, "*", 3000);
				if (res > 0)
					res = my_getsigstr(chan, dtmfbuf + strlen(dtmfbuf), "*", 3000);
				if ((res < 1) && (p->dsp)) cw_dsp_digitreset(p->dsp);
				break;
			case SIG_FEATDMF:
			case SIG_E911:
			case SIG_SF_FEATDMF:
				res = my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				if (res > 0) {
					/* if E911, take off hook */
					if (p->sig == SIG_E911)
						dahdi_set_hook(p->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
					res = my_getsigstr(chan, dtmfbuf + strlen(dtmfbuf), "#", 3000);
				}
				if ((res < 1) && (p->dsp)) cw_dsp_digitreset(p->dsp);
				break;
			case SIG_FEATB:
			case SIG_SF_FEATB:
				res = my_getsigstr(chan, dtmfbuf + 1, "#", 3000);
				if ((res < 1) && (p->dsp)) cw_dsp_digitreset(p->dsp);
				break;
			case SIG_EMWINK:
				/* if we received a '*', we are actually receiving Feature Group D
				   dial syntax, so use that mode; otherwise, fall through to normal
				   mode
				*/
				if (res == '*') {
					res = my_getsigstr(chan, dtmfbuf + 1, "*", 3000);
					if (res > 0)
						res = my_getsigstr(chan, dtmfbuf + strlen(dtmfbuf), "*", 3000);
					if ((res < 1) && (p->dsp)) cw_dsp_digitreset(p->dsp);
					break;
				}
			default:
				/* If we got the first digit, get the rest */
				len = 1;
				while((len < CW_MAX_EXTENSION-1) && cw_matchmore_extension(chan, chan->context, dtmfbuf, 1, p->cid_num)) {
					if (cw_exists_extension(chan, chan->context, dtmfbuf, 1, p->cid_num)) {
						timeout = matchdigittimeout;
					} else {
						timeout = gendigittimeout;
					}
					res = cw_waitfordigit(chan, timeout);
					if (res < 0) {
						cw_log(CW_LOG_DEBUG, "waitfordigit returned < 0...\n");
						cw_hangup(chan);
						return NULL;
					} else if (res) {
						dtmfbuf[len++] = res;
					} else {
						break;
					}
				}
				break;
			}
		}
		if (res == -1) {
			cw_log(CW_LOG_WARNING, "getdtmf on channel %d: %s\n", p->channel, strerror(errno));
			cw_hangup(chan);
			return NULL;
		} else if (res < 0) {
			cw_log(CW_LOG_DEBUG, "Got hung up before digits finished\n");
			cw_hangup(chan);
			return NULL;
		}
		cw_copy_string(exten, dtmfbuf, sizeof(exten));
		if (cw_strlen_zero(exten))
			cw_copy_string(exten, "s", sizeof(exten));
		if (p->sig == SIG_FEATD || p->sig == SIG_EMWINK) {
			/* Look for Feature Group D on all E&M Wink and Feature Group D trunks */
			if (exten[0] == '*') {
				char *stringp=NULL;
				cw_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "*");
				s2 = strsep(&stringp, "*");
				if (s2) {
					if (!cw_strlen_zero(p->cid_num))
						cw_set_callerid(chan, p->cid_num, NULL, p->cid_num);
					else
						cw_set_callerid(chan, s1, NULL, s1);
					cw_copy_string(exten, s2, sizeof(exten));
				} else
					cw_copy_string(exten, s1, sizeof(exten));
			} else if (p->sig == SIG_FEATD)
				cw_log(CW_LOG_WARNING, "Got a non-Feature Group D input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if (p->sig == SIG_FEATDMF) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				cw_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				s2 = strsep(&stringp, "#");
				if (s2) {
					if (!cw_strlen_zero(p->cid_num))
						cw_set_callerid(chan, p->cid_num, NULL, p->cid_num);
					else
						if(*(s1 + 2))
							cw_set_callerid(chan, s1 + 2, NULL, s1 + 2);
					cw_copy_string(exten, s2 + 1, sizeof(exten));
				} else
					cw_copy_string(exten, s1 + 2, sizeof(exten));
			} else
				cw_log(CW_LOG_WARNING, "Got a non-Feature Group D input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if (p->sig == SIG_E911) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				cw_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				s2 = strsep(&stringp, "#");
				if (s2 && (*(s2 + 1) == '0')) {
					if(*(s2 + 2))
						cw_set_callerid(chan, s2 + 2, NULL, s2 + 2);
				}
				if (s1)	cw_copy_string(exten, s1, sizeof(exten));
				else cw_copy_string(exten, "911", sizeof(exten));
			} else
				cw_log(CW_LOG_WARNING, "Got a non-E911 input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if (p->sig == SIG_FEATB) {
			if (exten[0] == '*') {
				char *stringp=NULL;
				cw_copy_string(exten2, exten, sizeof(exten2));
				/* Parse out extension and callerid */
				stringp=exten2 +1;
				s1 = strsep(&stringp, "#");
				cw_copy_string(exten, exten2 + 1, sizeof(exten));
			} else
				cw_log(CW_LOG_WARNING, "Got a non-Feature Group B input on channel %d.  Assuming E&M Wink instead\n", p->channel);
		}
		if (p->sig == SIG_FEATDMF) {
			dahdi_wink(p, curindex);
		}
		dahdi_enable_ec(p);
		if (NEED_MFDETECT(p)) {
			if (p->dsp) {
				if (!p->hardwaredtmf)
					cw_dsp_digitmode(p->dsp,DSP_DIGITMODE_DTMF | p->dtmfrelax); 
				else {
					cw_dsp_free(p->dsp);
					p->dsp = NULL;
				}
			}
		}

		if (cw_exists_extension(chan, chan->context, exten, 1, chan->cid.cid_num)) {
			cw_copy_string(chan->exten, exten, sizeof(chan->exten));
			if (p->dsp) cw_dsp_digitreset(p->dsp);
			res = cw_pbx_run(chan);
			if (res) {
				cw_log(CW_LOG_WARNING, "PBX exited non-zero\n");
				res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_CONGESTION);
			}
			return NULL;
		} else {
			if (option_verbose > 2)
				cw_verbose(VERBOSE_PREFIX_2 "Unknown extension '%s' in context '%s' requested\n", exten, chan->context);
			sleep(2);
			res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_INFO);
			if (res < 0)
				cw_log(CW_LOG_WARNING, "Unable to start special tone on %d\n", p->channel);
			else
				sleep(1);
			res = cw_streamfile(chan, "ss-noservice", chan->language);
			if (res >= 0)
				cw_waitstream(chan, "");
			res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_CONGESTION);
			cw_hangup(chan);
			return NULL;
		}
		break;
	case SIG_FXOLS:
	case SIG_FXOGS:
	case SIG_FXOKS:
		/* Read the first digit */
		timeout = firstdigittimeout;
		/* If starting a threeway call, never timeout on the first digit so someone
		   can use flash-hook as a "hold" feature */
		if (p->subs[SUB_THREEWAY].owner) 
			timeout = 999999;
		while(len < CW_MAX_EXTENSION-1) {
			/* Read digit unless it's supposed to be immediate, in which case the
			   only answer is 's' */
			if (p->immediate) 
				res = 's';
			else
				res = cw_waitfordigit(chan, timeout);
			timeout = 0;
			if (res < 0) {
				cw_log(CW_LOG_DEBUG, "waitfordigit returned < 0...\n");
				res = tone_zone_play_tone(p->subs[curindex].dfd, -1);
				cw_hangup(chan);
				return NULL;
			} else if (res)  {
				exten[len++]=res;
				exten[len] = '\0';
			}
			if (!cw_ignore_pattern(chan->context, exten))
				tone_zone_play_tone(p->subs[curindex].dfd, -1);
			else
				tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_DIALTONE);
			if (cw_exists_extension(chan, chan->context, exten, 1, p->cid_num) && strcmp(exten, cw_parking_ext())) {
				if (!res || !cw_matchmore_extension(chan, chan->context, exten, 1, p->cid_num)) {
					if (getforward) {
						/* Record this as the forwarding extension */
						cw_copy_string(p->call_forward, exten, sizeof(p->call_forward)); 
						if (option_verbose > 2)
							cw_verbose(VERBOSE_PREFIX_3 "Setting call forward to '%s' on channel %d\n", p->call_forward, p->channel);
						res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_DIALRECALL);
						if (res)
							break;
						usleep(500000);
						res = tone_zone_play_tone(p->subs[curindex].dfd, -1);
						sleep(1);
						memset(exten, 0, sizeof(exten));
						res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_DIALTONE);
						len = 0;
						getforward = 0;
					} else  {
						res = tone_zone_play_tone(p->subs[curindex].dfd, -1);
						cw_copy_string(chan->exten, exten, sizeof(chan->exten));
						if (!cw_strlen_zero(p->cid_num)) {
							if (!p->hidecallerid)
								cw_set_callerid(chan, p->cid_num, NULL, p->cid_num); 
							else
								cw_set_callerid(chan, NULL, NULL, p->cid_num); 
						}
						if (!cw_strlen_zero(p->cid_name)) {
							if (!p->hidecallerid)
								cw_set_callerid(chan, NULL, p->cid_name, NULL);
						}
						cw_setstate(chan, CW_STATE_RING);
						dahdi_enable_ec(p);
						res = cw_pbx_run(chan);
						if (res) {
							cw_log(CW_LOG_WARNING, "PBX exited non-zero\n");
							res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_CONGESTION);
						}
						return NULL;
					}
				} else {
					/* It's a match, but they just typed a digit, and there is an ambiguous match,
					   so just set the timeout to matchdigittimeout and wait some more */
					timeout = matchdigittimeout;
				}
			} else if (res == 0) {
				cw_log(CW_LOG_DEBUG, "not enough digits (and no ambiguous match)...\n");
				res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_CONGESTION);
				dahdi_wait_event(p->subs[curindex].dfd);
				cw_hangup(chan);
				return NULL;
			} else if (p->callwaiting && !strcmp(exten, "*70")) {
				if (option_verbose > 2) 
					cw_verbose(VERBOSE_PREFIX_3 "Disabling call waiting on %s\n", chan->name);
				/* Disable call waiting if enabled */
				p->callwaiting = 0;
				res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_DIALRECALL);
				if (res) {
					cw_log(CW_LOG_WARNING, "Unable to do dial recall on channel %s: %s\n", 
						chan->name, strerror(errno));
				}
				len = 0;
				ioctl(p->subs[curindex].dfd,DAHDI_CONFDIAG,&len);
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;
					
			} else if (!strcmp(exten,cw_pickup_ext())) {
				/* Scan all channels and see if any there
				 * ringing channqels with that have call groups
				 * that equal this channels pickup group  
				 */
				if (curindex == SUB_REAL) {
					/* Switch us from Third call to Call Wait */
				  	if (p->subs[SUB_THREEWAY].owner) {
						/* If you make a threeway call and the *8# a call, it should actually 
						   look like a callwait */
						alloc_sub(p, SUB_CALLWAIT);	
					  	swap_subs(p, SUB_CALLWAIT, SUB_THREEWAY);
						unalloc_sub(p, SUB_THREEWAY);
					}
					dahdi_enable_ec(p);
					if (cw_pickup_call(chan)) {
						cw_log(CW_LOG_DEBUG, "No call pickup possible...\n");
						res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_CONGESTION);
						dahdi_wait_event(p->subs[curindex].dfd);
					}
					cw_hangup(chan);
					return NULL;
				} else {
					cw_log(CW_LOG_WARNING, "Huh?  Got *8# on call not on real\n");
					cw_hangup(chan);
					return NULL;
				}
				
			} else if (!p->hidecallerid && !strcmp(exten, "*67")) {
				if (option_verbose > 2) 
					cw_verbose(VERBOSE_PREFIX_3 "Disabling Caller*ID on %s\n", chan->name);
				/* Disable Caller*ID if enabled */
				p->hidecallerid = 1;
				free(chan->cid.cid_num);
				chan->cid.cid_num = NULL;
				free(chan->cid.cid_name);
				chan->cid.cid_name = NULL;
				res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_DIALRECALL);
				if (res) {
					cw_log(CW_LOG_WARNING, "Unable to do dial recall on channel %s: %s\n", 
						chan->name, strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;
			} else if (p->callreturn && !strcmp(exten, "*69")) {
				res = 0;
				if (!cw_strlen_zero(p->lastcid_num)) {
					res = cw_say_digit_str(chan, p->lastcid_num, "", chan->language);
				}
				if (!res)
					res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_DIALRECALL);
				break;
			} else if (!strcmp(exten, "*78")) {
				/* Do not disturb */
				if (option_verbose > 2) {
					cw_verbose(VERBOSE_PREFIX_3 "Enabled DND on channel %d\n", p->channel);
					cw_manager_event(EVENT_FLAG_SYSTEM, "DNDState",
						2,
						cw_msg_tuple("Channel", "DAHDI/%d", p->channel),
						cw_msg_tuple("Status",  "%s",       "enabled")
					);
				}
				res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_DIALRECALL);
				p->dnd = 1;
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (!strcmp(exten, "*79")) {
				/* Do not disturb */
				if (option_verbose > 2) {
					cw_verbose(VERBOSE_PREFIX_3 "Disabled DND on channel %d\n", p->channel);
					cw_manager_event(EVENT_FLAG_SYSTEM, "DNDState",
						2,
						cw_msg_tuple("Channel", "DAHDI/%d", p->channel),
						cw_msg_tuple("Status",  "%s",       "disabled")
					);
				}
				res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_DIALRECALL);
				p->dnd = 0;
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (p->cancallforward && !strcmp(exten, "*72")) {
				res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_DIALRECALL);
				getforward = 1;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if (p->cancallforward && !strcmp(exten, "*73")) {
				if (option_verbose > 2)
					cw_verbose(VERBOSE_PREFIX_3 "Cancelling call forwarding on channel %d\n", p->channel);
				res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_DIALRECALL);
				memset(p->call_forward, 0, sizeof(p->call_forward));
				getforward = 0;
				memset(exten, 0, sizeof(exten));
				len = 0;
			} else if ((p->transfer || p->canpark) && !strcmp(exten, cw_parking_ext()) && p->subs[SUB_THREEWAY].owner && (bchan = cw_bridged_channel(p->subs[SUB_THREEWAY].owner))) {
				/* This is a three way call, the main call being a real channel, 
					and we're parking the first call. */
				cw_masq_park_call(bchan, chan, 0, NULL);
				if (option_verbose > 2)
					cw_verbose(VERBOSE_PREFIX_3 "Parking call to '%s'\n", chan->name);
				cw_object_put(bchan);
				break;
			} else if (!cw_strlen_zero(p->lastcid_num) && !strcmp(exten, "*60")) {
				if (option_verbose > 2)
					cw_verbose(VERBOSE_PREFIX_3 "Blacklisting number %s\n", p->lastcid_num);
				res = cw_db_put("blacklist", p->lastcid_num, "1");
				if (!res) {
					res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_DIALRECALL);
					memset(exten, 0, sizeof(exten));
					len = 0;
				}
			} else if (p->hidecallerid && !strcmp(exten, "*82")) {
				if (option_verbose > 2) 
					cw_verbose(VERBOSE_PREFIX_3 "Enabling Caller*ID on %s\n", chan->name);
				/* Enable Caller*ID if enabled */
				p->hidecallerid = 0;
				free(chan->cid.cid_num);
				chan->cid.cid_num = NULL;
				free(chan->cid.cid_name);
				chan->cid.cid_name = NULL;
				cw_set_callerid(chan, p->cid_num, p->cid_name, NULL);
				res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_DIALRECALL);
				if (res) {
					cw_log(CW_LOG_WARNING, "Unable to do dial recall on channel %s: %s\n", 
						chan->name, strerror(errno));
				}
				len = 0;
				memset(exten, 0, sizeof(exten));
				timeout = firstdigittimeout;
			} else if (!strcmp(exten, "*0")) {
				struct cw_channel *nbridge =
					p->subs[SUB_THREEWAY].owner;
				struct dahdi_pvt *pbridge = NULL;
				/* set up the private struct of the bridged one, if any */
				bchan = NULL;
				if (nbridge && (bchan = cw_bridged_channel(nbridge)))
					pbridge = bchan->tech_pvt;
				if (nbridge && pbridge && 
				    (!strcmp(nbridge->type, dahdi_tech.type)) && 
					(!strcmp(bchan->type,  dahdi_tech.type)) &&
				    ISTRUNK(pbridge)) {
					int func = DAHDI_FLASH;
					/* Clear out the dial buffer */
					p->dop.dialstr[0] = '\0';
					/* flash hookswitch */
					if ((ioctl(pbridge->subs[SUB_REAL].dfd,DAHDI_HOOK,&func) == -1) && (errno != EINPROGRESS)) {
						cw_log(CW_LOG_WARNING, "Unable to flash external trunk on channel %s: %s\n", 
							nbridge->name, strerror(errno));
					}
					swap_subs(p, SUB_REAL, SUB_THREEWAY);
					unalloc_sub(p, SUB_THREEWAY);
					p->owner = p->subs[SUB_REAL].owner;
					if (bchan)
						cw_object_put(bchan);
					if ((bchan = cw_bridged_channel(p->subs[SUB_REAL].owner))) {
						cw_moh_stop(bchan);
						cw_object_put(bchan);
					}
					cw_hangup(chan);
					return NULL;
				} else {
					if (bchan)
						cw_object_put(bchan);
					tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_CONGESTION);
					dahdi_wait_event(p->subs[curindex].dfd);
					tone_zone_play_tone(p->subs[curindex].dfd, -1);
					swap_subs(p, SUB_REAL, SUB_THREEWAY);
					unalloc_sub(p, SUB_THREEWAY);
					p->owner = p->subs[SUB_REAL].owner;
					cw_hangup(chan);
					return NULL;
				}
			} else if (!cw_canmatch_extension(chan, chan->context, exten, 1, chan->cid.cid_num) &&
							((exten[0] != '*') || (strlen(exten) > 2))) {
				if (option_debug)
					cw_log(CW_LOG_DEBUG, "Can't match %s from '%s' in context %s\n", exten, chan->cid.cid_num ? chan->cid.cid_num : "<Unknown Caller>", chan->context);
				break;
			}
			if (!timeout)
				timeout = gendigittimeout;
			if (len && !cw_ignore_pattern(chan->context, exten))
				tone_zone_play_tone(p->subs[curindex].dfd, -1);
		}
		break;
	case SIG_FXSLS:
	case SIG_FXSGS:
	case SIG_FXSKS:
#ifdef ZAPATA_PRI
		if (p->pri) {
			/* This is a GR-303 trunk actually.  Wait for the first ring... */
			struct cw_frame *f;
			int res;
			time_t start;

			time(&start);
			cw_setstate(chan, CW_STATE_RING);
			while(time(NULL) < start + 3) {
				res = cw_waitfor(chan, 1000);
				if (res) {
					f = cw_read(chan);
					if (!f) {
						cw_log(CW_LOG_WARNING, "Whoa, hangup while waiting for first ring!\n");
						cw_hangup(chan);
						return NULL;
					} else if ((f->frametype == CW_FRAME_CONTROL) && (f->subclass == CW_CONTROL_RING)) {
						res = 1;
					} else
						res = 0;
					cw_fr_free(f);
					if (res) {
						cw_log(CW_LOG_DEBUG, "Got ring!\n");
						res = 0;
						break;
					}
				}
			}
		}
#endif
		cw_log(CW_LOG_DEBUG, "CID/distinctive ring detection...\n");
		cw_copy_string(chan->exten, p->exten, sizeof(chan->exten));
		if (p->use_callerid) {
			/* DTMF detection in CID will be handled by spandsp */
			disable_dtmf_detect(p);

			bump_gains(p);

			/* Take out of linear mode if necessary */
			dahdi_setlinear(p->subs[curindex].dfd, 0);

			/* Pre-ring could be V23 or DTMF but not Bell202 (AFAIK).
			 * Post-ring could be US style Bell202, Japanese V23 or even UK V23 (sent
			 * after a short first ring by some exchanges used by the cable industry)
			 * or DTMF (used by Bharti in India?).
			 * Fortunately Bell202/V23 are very nearly the same and interchangeable
			 * for most decoders and the message formats are near enough identical
			 * for our purposes. So we just need to deal with Bell202 and DTMF...
			 */
			if (!p->adsi_rx1 && (p->adsi_rx1 = malloc(sizeof(adsi_rx_state_t))))
				adsi_rx_init(p->adsi_rx1, ADSI_STANDARD_CLASS, ss_thread_adsi_get1, p);
			if (!p->adsi_rx2 && (p->adsi_rx2 = malloc(sizeof(adsi_rx_state_t))))
				adsi_rx_init(p->adsi_rx2, ADSI_STANDARD_CLIP_DTMF, ss_thread_adsi_get2, p);
		}

		/* Clear the current ring data array so we dont have old data in it. */
		for (i=0; i < arraysize(curRingData); i++)
			curRingData[i] = 0;

		/* Check to see if context is what it should be, if not set to be. */
		if (strcmp(p->context,p->defcontext) != 0) {
			cw_copy_string(p->context, p->defcontext, sizeof(p->context));
			cw_copy_string(chan->context,p->defcontext,sizeof(chan->context));
		}

		/* Note we only look for caller ID during the distinctive ring detection phase.
		 * With the 3 step distinctive ring config and a possible initial ring/silence
		 * before the first step for chirp/CID that means we listen in the first
		 * two silences (and before the first ring if that wasn't the call trigger)
		 */
		p->ringt = p->ringt_base;
		receivedRingT = !p->usedistinctiveringdetection;
		samples = 0;
		chan->rings = chan->_state == CW_STATE_PRERING ? -1 : 0;
		while (p->ringt > 0
		&& (!receivedRingT || (p->adsi_rx1 && chan->rings < (int)arraysize(curRingData)-1))) {
			struct pollfd pfd;
			pfd.fd = p->subs[curindex].dfd;
			pfd.events = POLLIN;
			pfd.revents = 0;
			/* N.B. The only reason we do poll before read is for the timeout... */
			res = poll(&pfd, 1, SAMPLES_TO_MS(p->ringt));
			if (res > 0) {
				int16_t lin[256];
				uint8_t buf[256];
				res = read(p->subs[curindex].dfd, buf, sizeof(buf));
				if (res > 0) {
					samples += res;
					p->ringt -= res;

					if (p->law == DAHDI_LAW_MULAW)
						for (i = 0; i < res; i++)
							lin[i] = CW_MULAW(buf[i]);
					else
						for (i = 0; i < res; i++)
							lin[i] = CW_ALAW(buf[i]);
					if (p->adsi_rx1) adsi_rx(p->adsi_rx1, lin, res);
					if (p->adsi_rx2) adsi_rx(p->adsi_rx2, lin, res);
				} else if (res == 0) {
					cw_log(CW_LOG_WARNING, "%s: unexpected zero return from read\n", chan->name);
				} else if (errno == ELAST) {
					res = dahdi_get_event(p->subs[curindex].dfd);
					if (option_debug)
						cw_log(CW_LOG_DEBUG, "%s: Got event %d (%s)...\n", chan->name, res, event2str(res));
					switch (res) {
					case DAHDI_EVENT_RINGOFFHOOK:
						if (p->adsi_rx1) adsi_rx_init(p->adsi_rx1, ADSI_STANDARD_CLASS, ss_thread_adsi_get1, p);
						if (p->adsi_rx2) adsi_rx_init(p->adsi_rx2, ADSI_STANDARD_CLIP_DTMF, ss_thread_adsi_get2, p);
						/* Fall through */
					case DAHDI_EVENT_RINGBEGIN:
						p->ringt = p->ringt_base;
						if (chan->rings >= 0 && !receivedRingT) {
							int offset;
							curRingData[chan->rings] = SAMPLES_TO_MS(samples);
							if (option_debug)
								cw_log(CW_LOG_DEBUG, "%s: cadence=%d,%d,%d,%d,%d\n", chan->name, curRingData[0], curRingData[1], curRingData[2], curRingData[3], curRingData[4]);
							len = 0;
							counter = 0;
							/* Perhaps distinctive ring follows an initial chirp+CID
							 * (as for some UK cable co's that use US style CID with
							 * UK cadence)
							 */
							for (offset = 0; offset <= chan->rings && !counter && offset <= 2; offset += 2) {
								for (i = 0; i < arraysize(p->drings.ringnum); i++) {
									int j;
									if (!p->drings.ringnum[i].ring[0])
										continue;
									samples = 0;
									for (j = 0; j <= chan->rings - offset && j < arraysize(p->drings.ringnum[0].ring); j++) {
										samples += curRingData[offset + j] - p->drings.ringnum[i].ring[j];
										if (abs(samples) > 180)
											break;
									}
									if (option_debug)
										cw_log(CW_LOG_DEBUG, "dring%d cmp (offset=%d, j=%d, samples=%d, dring=%d,%d,%d)\n", i+1, offset, j, samples, p->drings.ringnum[i].ring[0], p->drings.ringnum[i].ring[1], p->drings.ringnum[i].ring[2]);
									if (offset + j > chan->rings && j >= len) {
									if (option_debug)
										cw_log(CW_LOG_DEBUG, "dring%d match (j=%d, samples=%d, dring=%d,%d,%d)\n", i+1, j, samples, p->drings.ringnum[i].ring[0], p->drings.ringnum[i].ring[1], p->drings.ringnum[i].ring[2]);
										if (j == len) {
											counter++;
										} else {
											len = j;
											res = i;
											counter++;
										}
									}
								}
							}
							if (counter == 1) {
								/* Exactly one match - use it */
								if (!cw_strlen_zero(p->drings.ringContext[res].contextData)) {
									cw_copy_string(chan->context, p->drings.ringContext[res].contextData, sizeof(chan->context));
									/* FIXME: why do we have a private context? */
									cw_copy_string(p->context, chan->context, sizeof(p->context));
								}
								if (!cw_strlen_zero(p->drings.ringContext[res].extenData))
									cw_copy_string(chan->exten, p->drings.ringContext[res].extenData, sizeof(chan->exten));
								if (option_verbose > 2)
									cw_verbose(VERBOSE_PREFIX_3 "%s: Matched Distinctive Ring %d context %s exten %s\n", chan->name, res + 1, chan->context, chan->exten);
								receivedRingT = 1;
							} else if (counter == 0 && chan->rings >= 2) {
								/* No matches and we're beyond any initial chirp/CID. Give in */
								receivedRingT = 1;
							} else if (chan->rings == (int)arraysize(curRingData)-1) {
								/* More than one match but we've reached the max steps */
								cw_log(CW_LOG_WARNING, "%d matches for received ring cadence", counter);
								receivedRingT = 1;
							}
						}
						chan->rings++;
						samples = 0;
						break;
					}
				} else {
					cw_log(CW_LOG_ERROR, "%s: read failed: %s\n", chan->name, strerror(errno));
					cw_hangup(chan);
					return NULL;
 				}
			} else if (res == 0) {
				cw_log(CW_LOG_ERROR, "%s: ring timeout reached with no data or events?!?\n", chan->name);
				cw_hangup(chan);
				return NULL;
			} else if (res == -1) {
				cw_log(CW_LOG_ERROR, "%s: poll failed: %s\n", chan->name, strerror(errno));
				cw_hangup(chan);
				return NULL;
			}
		}
		if (option_verbose > 2)
			cw_verbose(VERBOSE_PREFIX_3 "%s: Done CID/distinctive ring detection: cadence=%d,%d,%d,%d,%d\n",
				chan->name, curRingData[0], curRingData[1], curRingData[2], curRingData[3], curRingData[4]);

		if (p->use_callerid) {
			/* Restore linear mode if necessary */
			if (p->subs[curindex].linear)
				dahdi_setlinear(p->subs[curindex].dfd, p->subs[curindex].linear);

			restore_gains(p);

			if (!p->ignoredtmf)
				enable_dtmf_detect(p);

			free(p->adsi_rx1);
			p->adsi_rx1 = NULL;
			free(p->adsi_rx2);
			p->adsi_rx2 = NULL;
		}

		cw_setstate(chan, CW_STATE_RING);
		chan->rings = 1;
		p->ringt = p->ringt_base;
		res = cw_pbx_run(chan);
		if (res) {
			cw_hangup(chan);
			cw_log(CW_LOG_WARNING, "PBX exited non-zero\n");
		}
		return NULL;
	default:
		cw_log(CW_LOG_WARNING, "Don't know how to handle simple switch with signalling %s on channel %d\n", sig2str(p->sig), p->channel);
		res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_CONGESTION);
		if (res < 0)
				cw_log(CW_LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	}
	res = tone_zone_play_tone(p->subs[curindex].dfd, DAHDI_TONE_CONGESTION);
	if (res < 0)
			cw_log(CW_LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	cw_hangup(chan);
	return NULL;
}

static int handle_init_event(struct dahdi_pvt *i, int event)
{
	int res;
	pthread_t threadid;
	struct cw_channel *chan;

	/* Handle an event on a given channel for the monitor thread. */
	switch(event) {
	case DAHDI_EVENT_NONE:
	case DAHDI_EVENT_BITSCHANGED:
		if (i->radio) break;
		break;
	case DAHDI_EVENT_WINKFLASH:
	case DAHDI_EVENT_RINGBEGIN:
	case DAHDI_EVENT_RINGOFFHOOK:
		if (i->inalarm) break;
		if (i->radio) break;
		/* Got a ring/answer.  What kind of channel are we? */
		switch(i->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FXOKS:
		        dahdi_set_hook(i->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);

			/* Cancel VMWI spill */
			free(i->cidspill);
			i->cidspill = NULL;

			if (i->immediate) {
				dahdi_enable_ec(i);
				/* The channel is immediately up.  Start right away */
				res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_RINGTONE);
				chan = dahdi_new(i, CW_STATE_RING, 1, SUB_REAL, 0, 0);
				if (!chan) {
					cw_log(CW_LOG_WARNING, "Unable to start PBX on channel %d\n", i->channel);
					res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
					if (res < 0)
						cw_log(CW_LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
				}
			} else {
				/* Check for callerid, digits, etc */
				chan = dahdi_new(i, CW_STATE_RESERVED, 0, SUB_REAL, 0, 0);
				if (chan) {
					if (cw_app_has_voicemail(i->mailbox, NULL))
						res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_STUTTER);
					else
						res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_DIALTONE);
					if (res < 0) 
						cw_log(CW_LOG_WARNING, "Unable to play dialtone on channel %d\n", i->channel);
					if (cw_pthread_create(&threadid, &global_attr_detached, ss_thread, chan)) {
						cw_log(CW_LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
						res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
						if (res < 0)
							cw_log(CW_LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
						cw_hangup(chan);
					}
				} else
					cw_log(CW_LOG_WARNING, "Unable to create channel\n");
			}
			break;
		case SIG_FXSLS:
		case SIG_FXSGS:
		case SIG_FXSKS:
				i->ringt = i->ringt_base;
				/* Fall through */
		case SIG_EMWINK:
		case SIG_FEATD:
		case SIG_FEATDMF:
		case SIG_E911:
		case SIG_FEATB:
		case SIG_EM:
		case SIG_EM_E1:
		case SIG_SFWINK:
		case SIG_SF_FEATD:
		case SIG_SF_FEATDMF:
		case SIG_SF_FEATB:
		case SIG_SF:
				/* Check for callerid, digits, etc */
				chan = dahdi_new(i, CW_STATE_RING, 0, SUB_REAL, 0, 0);
				if (chan && cw_pthread_create(&threadid, &global_attr_detached, ss_thread, chan)) {
					cw_log(CW_LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
					res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
					if (res < 0)
						cw_log(CW_LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
					cw_hangup(chan);
				} else if (!chan) {
					cw_log(CW_LOG_WARNING, "Cannot allocate new structure on channel %d\n", i->channel);
				}
				break;
		default:
			cw_log(CW_LOG_WARNING, "Don't know how to handle ring/answer with signalling %s on channel %d\n", sig2str(i->sig), i->channel);
			res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, DAHDI_TONE_CONGESTION);
			if (res < 0)
					cw_log(CW_LOG_WARNING, "Unable to play congestion tone on channel %d\n", i->channel);
			return -1;
		}
		break;
	case DAHDI_EVENT_NOALARM:
		i->inalarm = 0;
		cw_log(CW_LOG_NOTICE, "Alarm cleared on channel %d\n", i->channel);
		break;
	case DAHDI_EVENT_ALARM:
		i->inalarm = 1;
		res = get_alarms(i);
		cw_log(CW_LOG_WARNING, "Detected alarm on channel %d: %s\n", i->channel, alarm2str(res));
		cw_manager_event(EVENT_FLAG_SYSTEM, "Alarm",
			2,
			cw_msg_tuple("Alarm",   "%s", alarm2str(res)),
			cw_msg_tuple("Channel", "%d", i->channel)
		);
		/* fall thru intentionally */
	case DAHDI_EVENT_ONHOOK:
		if (i->radio) break;
		/* Back on hook.  Hang up. */
		switch(i->sig) {
		case SIG_FXOLS:
		case SIG_FXOGS:
		case SIG_FEATD:
		case SIG_FEATDMF:
		case SIG_E911:
		case SIG_FEATB:
		case SIG_EM:
		case SIG_EM_E1:
		case SIG_EMWINK:
		case SIG_SF_FEATD:
		case SIG_SF_FEATDMF:
		case SIG_SF_FEATB:
		case SIG_SF:
		case SIG_SFWINK:
		case SIG_FXSLS:
		case SIG_FXSGS:
		case SIG_FXSKS:
		case SIG_GR303FXSKS:
			dahdi_disable_ec(i);
			res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, -1);
			dahdi_set_hook(i->subs[SUB_REAL].dfd, DAHDI_ONHOOK);
			break;
		case SIG_GR303FXOKS:
		case SIG_FXOKS:
			dahdi_disable_ec(i);
			/* Diddle the battery for the zhone */
#ifdef ZHONE_HACK
			dahdi_set_hook(i->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
			usleep(1);
#endif			
			res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, -1);
			dahdi_set_hook(i->subs[SUB_REAL].dfd, DAHDI_ONHOOK);
			break;
		case SIG_PRI:
			dahdi_disable_ec(i);
			res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, -1);
			break;
		default:
			cw_log(CW_LOG_WARNING, "Don't know how to handle on hook with signalling %s on channel %d\n", sig2str(i->sig), i->channel);
			res = tone_zone_play_tone(i->subs[SUB_REAL].dfd, -1);
			return -1;
		}
		break;
	default:
		if (event == DAHDI_EVENT_POLARITY || (event & DAHDI_EVENT_DTMFUP))
        {
			switch (i->sig)
            {
			case SIG_FXSLS:
			case SIG_FXSKS:
			case SIG_FXSGS:
				if (event == DAHDI_EVENT_POLARITY)
					i->polarity = POLARITY_REV;
				if (option_verbose > 1)
					cw_verbose(VERBOSE_PREFIX_2 "Starting post polarity/DTMF CID detection on channel %d\n", i->channel);
				chan = dahdi_new(i, CW_STATE_PRERING, 0, SUB_REAL, 0, 0);
				if (chan && cw_pthread_create(&threadid, &global_attr_detached, ss_thread, chan)) {
					cw_log(CW_LOG_WARNING, "Unable to start simple switch thread on channel %d\n", i->channel);
				}
				break;
			default:
				cw_log(CW_LOG_WARNING, "handle_init_event detected "
					"polarity reversal or on-hook DTMF on non-FXO (SIG_FXS) "
					"interface %d\n", i->channel);
                break;
			}
		}
		break;
	}
	return 0;
}

static void *do_monitor(void *data)
{
	uint8_t buf[256];
	int16_t lin[arraysize(buf)];
	int count, res, res2, spoint, pollres=0;
	struct dahdi_pvt *i;
	struct dahdi_pvt *last = NULL;
	time_t thispass = 0, lastpass = 0;
	int found;
	struct pollfd *pfds=NULL;
	int lastalloc = -1;

	CW_UNUSED(data);

	/* This thread monitors all the frame relay interfaces which are not yet in use
	   (and thus do not have a separate thread) indefinitely */
	/* From here on out, we die whenever asked */
#if 0
	cw_log(CW_LOG_DEBUG, "Monitor starting...\n");
#endif
	for(;;) {
		/* Lock the interface list */
		if (cw_mutex_lock(&iflock)) {
			cw_log(CW_LOG_ERROR, "Unable to grab interface lock\n");
			return NULL;
		}
		if (!pfds || (lastalloc != ifcount)) {
			free(pfds);
			if (ifcount) {
				pfds = malloc(ifcount * sizeof(struct pollfd));
				if (!pfds) {
					cw_log(CW_LOG_WARNING, "Critical memory error.  DAHDI dies.\n");
					cw_mutex_unlock(&iflock);
					return NULL;
				}
			}
			lastalloc = ifcount;
		}
		/* Build the stuff we're going to poll on, that is the socket of every
		   dahdi_pvt that does not have an associated owner channel */
		count = 0;
		i = iflist;
		while(i) {
			if ((i->subs[SUB_REAL].dfd > -1) && i->sig && (!i->radio)) {
				if (!i->owner && !i->subs[SUB_REAL].owner) {
					/* This needs to be watched, as it lacks an owner */
					pfds[count].fd = i->subs[SUB_REAL].dfd;
					pfds[count].events = POLLPRI;
					pfds[count].revents = 0;
					/* Message waiting also get watched for reading */
					if (i->cidspill)
						pfds[count].events |= POLLIN;
					/* If caller ID is required but we don't get polarity events
					 * on this FXO we have to actively monitor the line for a
					 * bell/v23 carrier or a DTMF digit.
					 */
					if (i->use_callerid && !i->polarity_events) {
						if (i->adsi_rx1 || (i->adsi_rx1 = malloc(sizeof(adsi_rx_state_t)))) {
							adsi_rx_init(i->adsi_rx1, ADSI_STANDARD_CLIP, ss_thread_adsi_get1, i);
							i->adsi_rx1->bit_pos = -1;
						}
						if (i->adsi_rx2 || (i->adsi_rx2 = malloc(sizeof(adsi_rx_state_t))))
							adsi_rx_init(i->adsi_rx2, ADSI_STANDARD_CLIP_DTMF, ss_thread_adsi_get2, i);
						pfds[count].events |= POLLIN;
					}
					count++;
				}
			}
			i = i->next;
		}
		/* Okay, now that we know what to do, release the interface lock */
		cw_mutex_unlock(&iflock);
		
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		pthread_testcancel();

		/* Wait at least a second for something to happen */
		res = poll(pfds, count, 1000);
		res2 = errno;

		pthread_testcancel();
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		/* Okay, poll has finished.  Let's see what happened.  */
		if (res < 0) {
			if (res2 != EAGAIN && res2 != EINTR)
				cw_log(CW_LOG_WARNING, "poll return %d: %s\n", res, strerror(res2));
			continue;
		}
		/* Alright, lock the interface list again, and let's look and see what has
		   happened */
		if (cw_mutex_lock(&iflock)) {
			cw_log(CW_LOG_WARNING, "Unable to lock the interface list\n");
			continue;
		}
		found = 0;
		spoint = 0;
		lastpass = thispass;
		thispass = time(NULL);
		i = iflist;
		while(i) {
			if (thispass != lastpass) {
				if (!found && ((i == last) || ((i == iflist) && !last))) {
					last = i;
					if (last) {
						if (!last->cidspill && !last->owner && !cw_strlen_zero(last->mailbox) && (thispass - last->onhooktime > 3) &&
							(last->sig & __DAHDI_SIG_FXO)) {
							res = cw_app_has_voicemail(last->mailbox, NULL);
							if (last->msgstate != res) {
								int x;
								cw_log(CW_LOG_DEBUG, "Message status for %s changed from %d to %d on %d\n", last->mailbox, last->msgstate, res, last->channel);
#ifdef DAHDI_VMWI
								ioctl(last->subs[SUB_REAL].dfd, DAHDI_VMWI, res);
#endif
								x = DAHDI_FLUSH_BOTH;
								res2 = ioctl(last->subs[SUB_REAL].dfd, DAHDI_FLUSH, &x);
								if (res2)
									cw_log(CW_LOG_WARNING, "Unable to flush input on channel %d\n", last->channel);
								last->cidspill = malloc(MAX_CALLERID_SIZE);
								if (last->cidspill) {
									last->cidlen = vmwi_generate(last->cidspill, MAX_CALLERID_SIZE, res, 1, CW_LAW(last));
									ioctl(last->subs[SUB_REAL].dfd, DAHDI_ONHOOKTRANSFER, &last->cidlen);
									last->cidpos = 0;
									last->msgstate = res;
									last->onhooktime = thispass;
								}
								found ++;
							}
						}
						last = last->next;
					}
				}
			}
			if ((i->subs[SUB_REAL].dfd > -1) && i->sig) {
				if (i->radio && !i->owner)
				{
					res = dahdi_get_event(i->subs[SUB_REAL].dfd);
					if (res)
					{
						if (option_debug)
							cw_log(CW_LOG_DEBUG, "Monitor doohicky got event %s on radio channel %d\n", event2str(res), i->channel);
						/* Don't hold iflock while handling init events */
						cw_mutex_unlock(&iflock);
						handle_init_event(i, res);
						cw_mutex_lock(&iflock);	
					}
					i = i->next;
					continue;
				}					

				/* Careful: POLLPRI just means there is an event queued. If the DAHDI
				 * driver has support for in-order events we need to read data until
				 * read gives ELAST to make sure that the sample counts for subsequent
				 * distinctive ring detection are right. If the DAHDI driver doesn't
				 * have in-order events read will return ELAST immediately there is
				 * an event in the queue and distinctive ring detection will be on
				 * a wing and a prayer.
				 */
				pollres = cw_fdisset(pfds, i->subs[SUB_REAL].dfd, count, &spoint);
				if (pollres & (POLLPRI | POLLIN)) {
					if (i->owner || i->subs[SUB_REAL].owner) {
#ifdef ZAPATA_PRI
						if (!i->pri)
#endif						
							cw_log(CW_LOG_WARNING, "Whoa....  I'm owned but found (%d) in read...\n", i->subs[SUB_REAL].dfd);
						i = i->next;
						continue;
					}
					res = read(i->subs[SUB_REAL].dfd, buf, sizeof(buf));
					if (res > 0) {
						if (i->cidspill) {
							/* We read some number of bytes.  Write an equal amount of data */
							res2 = write(i->subs[SUB_REAL].dfd, i->cidspill + i->cidpos,
									(res > i->cidlen - i->cidpos
									 	? i->cidlen - i->cidpos
										: res)
								    );
							if (res2 > 0) {
								i->cidpos += res2;
								if (i->cidpos >= i->cidlen) {
									free(i->cidspill);
									i->cidspill = 0;
									i->cidpos = 0;
									i->cidlen = 0;
								}
							} else {
								cw_log(CW_LOG_WARNING, "Write failed: %s\n", strerror(errno));
								i->msgstate = -1;
							}
						}
						if (i->use_callerid && !i->polarity_events) {
							if (i->law == DAHDI_LAW_MULAW)
								for (res2 = 0; res2 < res; res2++)
									lin[res2] = CW_MULAW(buf[res2]);
							else
								for (res2 = 0; res2 < res; res2++)
									lin[res2] = CW_ALAW(buf[res2]);
							if (i->adsi_rx1) adsi_rx(i->adsi_rx1, lin, res);
							if (i->adsi_rx2) adsi_rx(i->adsi_rx2, lin, res);
							if ((i->adsi_rx1 && i->adsi_rx1->bit_pos == 0)
							|| (i->adsi_rx2 && i->adsi_rx2->msg_len)) {
								cw_mutex_unlock(&iflock);
								handle_init_event(i, DAHDI_EVENT_POLARITY);
								cw_mutex_lock(&iflock);	
							}
						}
						continue;
					} else if (res == 0 || errno != ELAST) {
						cw_log(CW_LOG_WARNING, "Read failed with %d: %s\n", res, strerror(errno));
					}
				}
				if (pollres & POLLPRI) {
					if (i->owner || i->subs[SUB_REAL].owner) {
#ifdef ZAPATA_PRI
						if (!i->pri)
#endif						
							cw_log(CW_LOG_WARNING, "Whoa....  I'm owned but found (%d)...\n", i->subs[SUB_REAL].dfd);
						i = i->next;
						continue;
					}
					res = dahdi_get_event(i->subs[SUB_REAL].dfd);
					if (option_debug)
						cw_log(CW_LOG_DEBUG, "Monitor doohicky got event %s on channel %d\n", event2str(res), i->channel);
					/* Don't hold iflock while handling init events */
					cw_mutex_unlock(&iflock);
					handle_init_event(i, res);
					cw_mutex_lock(&iflock);	
				}
			}
			i=i->next;
		}
		cw_mutex_unlock(&iflock);
	}
	/* Never reached */
	return NULL;
	
}

static int restart_monitor(void)
{
	/* If we're supposed to be stopped -- stay stopped */
	if (pthread_equal(monitor_thread, CW_PTHREADT_STOP))
		return 0;
	if (cw_mutex_lock(&monlock)) {
		cw_log(CW_LOG_WARNING, "Unable to lock monitor\n");
		return -1;
	}
	if (pthread_equal(monitor_thread, pthread_self())) {
		cw_mutex_unlock(&monlock);
		cw_log(CW_LOG_WARNING, "Cannot kill myself\n");
		return -1;
	}
	if (!pthread_equal(monitor_thread, CW_PTHREADT_NULL)) {
		/* Just signal it to be sure it wakes up */
#if 0
		pthread_cancel(monitor_thread);
#endif
		pthread_kill(monitor_thread, SIGURG);
#if 0
		pthread_join(monitor_thread, NULL);
#endif
	} else {
		/* Start a new monitor */
		if (cw_pthread_create(&monitor_thread, &global_attr_detached, do_monitor, NULL) < 0) {
			cw_mutex_unlock(&monlock);
			cw_log(CW_LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	cw_mutex_unlock(&monlock);
	return 0;
}

#ifdef ZAPATA_PRI
static int pri_resolve_span(int *span, int channel, int offset, struct dahdi_spaninfo *si)
{
	int x;
	int trunkgroup;
	/* Get appropriate trunk group if there is one */
	trunkgroup = pris[*span].mastertrunkgroup;
	if (trunkgroup) {
		/* Select a specific trunk group */
		for (x=0;x<NUM_SPANS;x++) {
			if (pris[x].trunkgroup == trunkgroup) {
				*span = x;
				return 0;
			}
		}
		cw_log(CW_LOG_WARNING, "Channel %d on span %d configured to use nonexistent trunk group %d\n", channel, *span, trunkgroup);
		*span = -1;
	} else {
		if (pris[*span].trunkgroup) {
			cw_log(CW_LOG_WARNING, "Unable to use span %d implicitly since it is trunk group %d (please use spanmap)\n", *span, pris[*span].trunkgroup);
			*span = -1;
		} else if (pris[*span].mastertrunkgroup) {
			cw_log(CW_LOG_WARNING, "Unable to use span %d implicitly since it is already part of trunk group %d\n", *span, pris[*span].mastertrunkgroup);
			*span = -1;
		} else {
			if (si->totalchans == 31) { /* if it's an E1 */
				pris[*span].dchannels[0] = 16 + offset;
			} else {
				pris[*span].dchannels[0] = 24 + offset;
			}
			pris[*span].dchanavail[0] |= DCHAN_PROVISIONED;
			pris[*span].offset = offset;
			pris[*span].span = *span + 1;
		}
	}
	return 0;
}

static int pri_create_trunkgroup(int trunkgroup, int *channels)
{
	struct dahdi_spaninfo si;
	struct dahdi_params p;
	int fd;
	int span;
	int ospan=0;
	int x,y;
	for (x=0;x<NUM_SPANS;x++) {
		if (pris[x].trunkgroup == trunkgroup) {
			cw_log(CW_LOG_WARNING, "Trunk group %d already exists on span %d, Primary d-channel %d\n", trunkgroup, x + 1, pris[x].dchannels[0]);
			return -1;
		}
	}
	for (y=0;y<NUM_DCHANS;y++) {
		if (!channels[y])	
			break;
		memset(&si, 0, sizeof(si));
		memset(&p, 0, sizeof(p));
		fd = open("/dev/dahdi/channel", O_RDWR);
		if (fd < 0) {
			cw_log(CW_LOG_WARNING, "Failed to open channel: %s\n", strerror(errno));
			return -1;
		}
		x = channels[y];
		if (ioctl(fd, DAHDI_SPECIFY, &x)) {
			cw_log(CW_LOG_WARNING, "Failed to specify channel %d: %s\n", channels[y], strerror(errno));
			dahdi_close(fd);
			return -1;
		}
		if (ioctl(fd, DAHDI_GET_PARAMS, &p)) {
			cw_log(CW_LOG_WARNING, "Failed to get channel parameters for channel %d: %s\n", channels[y], strerror(errno));
			return -1;
		}
		if (ioctl(fd, DAHDI_SPANSTAT, &si)) {
			cw_log(CW_LOG_WARNING, "Failed go get span information on channel %d (span %d)\n", channels[y], p.spanno);
			dahdi_close(fd);
			return -1;
		}
		span = p.spanno - 1;
		if (pris[span].trunkgroup) {
			cw_log(CW_LOG_WARNING, "Span %d is already provisioned for trunk group %d\n", span + 1, pris[span].trunkgroup);
			dahdi_close(fd);
			return -1;
		}
		if (pris[span].pvts[0]) {
			cw_log(CW_LOG_WARNING, "Span %d is already provisioned with channels (implicit PRI maybe?)\n", span + 1);
			dahdi_close(fd);
			return -1;
		}
		if (!y) {
			pris[span].trunkgroup = trunkgroup;
			pris[span].offset = channels[y] - p.chanpos;
			ospan = span;
		}
		pris[ospan].dchannels[y] = channels[y];
		pris[ospan].dchanavail[y] |= DCHAN_PROVISIONED;
		pris[span].span = span + 1;
		dahdi_close(fd);
	}
	return 0;	
}

static int pri_create_spanmap(int span, int trunkgroup, int logicalspan)
{
	if (pris[span].mastertrunkgroup) {
		cw_log(CW_LOG_WARNING, "Span %d is already part of trunk group %d, cannot add to trunk group %d\n", span + 1, pris[span].mastertrunkgroup, trunkgroup);
		return -1;
	}
	pris[span].mastertrunkgroup = trunkgroup;
	pris[span].prilogicalspan = logicalspan;
	return 0;
}

#endif

static struct dahdi_pvt *mkintf(int channel, int signalling, int radio, struct dahdi_pri *pri, int reloading)
{
	/* Make a dahdi_pvt structure for this interface (or CRV if "pri" is specified) */
	struct dahdi_pvt *tmp = NULL, *tmp2;
	char fn[80];
#if 1
	struct dahdi_bufferinfo bi;
#endif
	struct dahdi_spaninfo si;
	int res;
#ifdef ZAPATA_PRI
	int span=0;
#endif
	int here = 0;
	int x;
	struct dahdi_pvt **wlist;
	struct dahdi_pvt **wend;
	struct dahdi_params p;

	wlist = &iflist;
	wend = &ifend;

#ifdef ZAPATA_PRI
	if (pri) {
		wlist = &pri->crvs;
		wend = &pri->crvend;
	}
#endif

	tmp2 = *wlist;

	while (tmp2) {
		if (!tmp2->destroy) {
			if (tmp2->channel == channel) {
				tmp = tmp2;
				here = 1;
				break;
			}
			if (tmp2->channel > channel) {
				break;
			}
		}
		tmp2 = tmp2->next;
	}

	if (!here && !reloading) {
		tmp = (struct dahdi_pvt*)malloc(sizeof(struct dahdi_pvt));
		if (!tmp) {
			cw_log(CW_LOG_ERROR, "MALLOC FAILED\n");
			return NULL;
		}
		memset(tmp, 0, sizeof(struct dahdi_pvt));
		cw_mutex_init(&tmp->lock);
		ifcount++;
		for (x = 0; x < 3; x++) {
			tmp->subs[x].dfd = -1;
			cw_fr_init(&tmp->subs[x].f);
			tmp->subs[x].f.has_timing_info = 1;
		}
		tmp->channel = channel;

		/* Assign default jb conf to the new dahdi_pvt */
		memcpy(&tmp->jbconf, &global_jbconf, 
		       sizeof(struct cw_jb_conf));
	}

	if (tmp) {
		if (!here) {
			if ((channel != CHAN_PSEUDO) && !pri) {
				snprintf(fn, sizeof(fn), "%d", channel);
				/* Open non-blocking */
				if (!here)
					tmp->subs[SUB_REAL].dfd = dahdi_open(fn);
				/* Allocate a zapata structure */
				if (tmp->subs[SUB_REAL].dfd < 0) {
					cw_log(CW_LOG_ERROR, "Unable to open channel %d: %s\nhere = %d, tmp->channel = %d, channel = %d\n", channel, strerror(errno), here, tmp->channel, channel);
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}
				memset(&p, 0, sizeof(p));
				res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &p);
				if (res < 0) {
					cw_log(CW_LOG_ERROR, "Unable to get parameters\n");
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}
				if (p.sigtype != (signalling & 0x3ffff)) {
					cw_log(CW_LOG_ERROR, "Signalling requested on channel %d is %s but line is in %s signalling\n", channel, sig2str(signalling), sig2str(p.sigtype));
					destroy_dahdi_pvt(&tmp);
					return tmp;
				}
				tmp->law = p.curlaw;
				tmp->span = p.spanno;
#ifdef ZAPATA_PRI
				span = p.spanno - 1;
#endif
			} else {
				if (channel == CHAN_PSEUDO)
					signalling = 0;
				else if ((signalling != SIG_FXOKS) && (signalling != SIG_FXSKS)) {
					cw_log(CW_LOG_ERROR, "CRV's must use FXO/FXS Kewl Start (fxo_ks/fxs_ks) signalling only.\n");
					return NULL;
				}
			}
#ifdef ZAPATA_PRI
			if ((signalling == SIG_PRI) || (signalling == SIG_GR303FXOKS) || (signalling == SIG_GR303FXSKS)) {
				int offset;
				int myswitchtype;
				int matchesdchan;
				int x,y;
				offset = 0;
				if ((signalling == SIG_PRI) && ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &offset)) {
					cw_log(CW_LOG_ERROR, "Unable to set clear mode on clear channel %d of span %d: %s\n", channel, p.spanno, strerror(errno));
					destroy_dahdi_pvt(&tmp);
					return NULL;
				}
				if (span >= NUM_SPANS) {
					cw_log(CW_LOG_ERROR, "Channel %d does not lie on a span I know of (%d)\n", channel, span);
					destroy_dahdi_pvt(&tmp);
					return NULL;
				} else {
					si.spanno = 0;
					if (ioctl(tmp->subs[SUB_REAL].dfd,DAHDI_SPANSTAT,&si) == -1) {
						cw_log(CW_LOG_ERROR, "Unable to get span status: %s\n", strerror(errno));
						destroy_dahdi_pvt(&tmp);
						return NULL;
					}
					/* Store the logical span first based upon the real span */
					tmp->logicalspan = pris[span].prilogicalspan;
					pri_resolve_span(&span, channel, (channel - p.chanpos), &si);
					if (span < 0) {
						cw_log(CW_LOG_WARNING, "Channel %d: Unable to find locate channel/trunk group!\n", channel);
						destroy_dahdi_pvt(&tmp);
						return NULL;
					}
					if (signalling == SIG_PRI)
						myswitchtype = switchtype;
					else
						myswitchtype = PRI_SWITCH_GR303_TMC;
					/* Make sure this isn't a d-channel */
					matchesdchan=0;
					for (x=0;x<NUM_SPANS;x++) {
						for (y=0;y<NUM_DCHANS;y++) {
							if (pris[x].dchannels[y] == tmp->channel) {
								matchesdchan = 1;
								break;
							}
						}
					}
					offset = p.chanpos;
					if (!matchesdchan) {
						if (pris[span].nodetype && (pris[span].nodetype != pritype)) {
							cw_log(CW_LOG_ERROR, "Span %d is already a %s node\n", span + 1, pri_node2str(pris[span].nodetype));
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if (pris[span].switchtype && (pris[span].switchtype != myswitchtype)) {
							cw_log(CW_LOG_ERROR, "Span %d is already a %s switch\n", span + 1, pri_switch2str(pris[span].switchtype));
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if ((pris[span].dialplan) && (pris[span].dialplan != dialplan)) {
							cw_log(CW_LOG_ERROR, "Span %d is already a %s dialing plan\n", span + 1, dialplan2str(pris[span].dialplan));
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if (!cw_strlen_zero(pris[span].idledial) && strcmp(pris[span].idledial, idledial)) {
							cw_log(CW_LOG_ERROR, "Span %d already has idledial '%s'.\n", span + 1, idledial);
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if (!cw_strlen_zero(pris[span].idleext) && strcmp(pris[span].idleext, idleext)) {
							cw_log(CW_LOG_ERROR, "Span %d already has idleext '%s'.\n", span + 1, idleext);
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if (pris[span].minunused && (pris[span].minunused != minunused)) {
							cw_log(CW_LOG_ERROR, "Span %d already has minunused of %d.\n", span + 1, minunused);
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if (pris[span].minidle && (pris[span].minidle != minidle)) {
							cw_log(CW_LOG_ERROR, "Span %d already has minidle of %d.\n", span + 1, minidle);
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						if (pris[span].numchans >= MAX_CHANNELS) {
							cw_log(CW_LOG_ERROR, "Unable to add channel %d: Too many channels in trunk group %d!\n", channel,
								pris[span].trunkgroup);
							destroy_dahdi_pvt(&tmp);
							return NULL;
						}
						pris[span].nodetype = pritype;
						pris[span].switchtype = myswitchtype;
						pris[span].nsf = nsf;
						pris[span].dialplan = dialplan;
						pris[span].localdialplan = localdialplan;
						pris[span].pvts[pris[span].numchans++] = tmp;
						pris[span].minunused = minunused;
						pris[span].minidle = minidle;
						pris[span].overlapdial = overlapdial;
						pris[span].facilityenable = facilityenable;
						cw_copy_string(pris[span].idledial, idledial, sizeof(pris[span].idledial));
						cw_copy_string(pris[span].idleext, idleext, sizeof(pris[span].idleext));
						cw_copy_string(pris[span].internationalprefix, internationalprefix, sizeof(pris[span].internationalprefix));
						cw_copy_string(pris[span].nationalprefix, nationalprefix, sizeof(pris[span].nationalprefix));
						cw_copy_string(pris[span].localprefix, localprefix, sizeof(pris[span].localprefix));
						cw_copy_string(pris[span].privateprefix, privateprefix, sizeof(pris[span].privateprefix));
						cw_copy_string(pris[span].unknownprefix, unknownprefix, sizeof(pris[span].unknownprefix));
						
						pris[span].resetinterval = resetinterval;
						
						tmp->pri = &pris[span];
						tmp->prioffset = offset;
						tmp->call = NULL;
					} else {
						cw_log(CW_LOG_ERROR, "Channel %d is reserved for D-channel.\n", offset);
						destroy_dahdi_pvt(&tmp);
						return NULL;
					}
				}
			} else {
				tmp->prioffset = 0;
			}
#endif
		} else {
			signalling = tmp->sig;
			radio = tmp->radio;
			memset(&p, 0, sizeof(p));
			if (tmp->subs[SUB_REAL].dfd > -1)
				res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &p);
		}
		/* Adjust starttime on loopstart and kewlstart trunks to reasonable values */
		if ((signalling == SIG_FXSKS) || (signalling == SIG_FXSLS) ||
		    (signalling == SIG_EM) || (signalling == SIG_EM_E1) ||  (signalling == SIG_EMWINK) ||
			(signalling == SIG_FEATD) || (signalling == SIG_FEATDMF) || (signalling == SIG_FEATDMF_TA) ||
			  (signalling == SIG_FEATB) || (signalling == SIG_E911) ||
		    (signalling == SIG_SF) || (signalling == SIG_SFWINK) ||
			(signalling == SIG_SF_FEATD) || (signalling == SIG_SF_FEATDMF) ||
			  (signalling == SIG_SF_FEATB)) {
			p.starttime = 250;
		}
		if (radio) {
			/* XXX Waiting to hear back from Jim if these should be adjustable XXX */
			p.channo = channel;
			p.rxwinktime = 1;
			p.rxflashtime = 1;
			p.starttime = 1;
			p.debouncetime = 5;
		}
		if (!radio) {
			p.channo = channel;
			/* Override timing settings based on config file */
			if (cur_prewink >= 0)
				p.prewinktime = cur_prewink;
			if (cur_preflash >= 0)
				p.preflashtime = cur_preflash;
			if (cur_wink >= 0)
				p.winktime = cur_wink;
			if (cur_flash >= 0)
				p.flashtime = cur_flash;
			if (cur_start >= 0)
				p.starttime = cur_start;
			if (cur_rxwink >= 0)
				p.rxwinktime = cur_rxwink;
			if (cur_rxflash >= 0)
				p.rxflashtime = cur_rxflash;
			if (cur_debounce >= 0)
				p.debouncetime = cur_debounce;
		}
		
		/* dont set parms on a pseudo-channel (or CRV) */
		if (tmp->subs[SUB_REAL].dfd >= 0)
		{
			res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_SET_PARAMS, &p);
			if (res < 0) {
				cw_log(CW_LOG_ERROR, "Unable to set parameters\n");
				destroy_dahdi_pvt(&tmp);
				return NULL;
			}
		}
#if 1
		if (!here && (tmp->subs[SUB_REAL].dfd > -1)) {
			memset(&bi, 0, sizeof(bi));
			res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_BUFINFO, &bi);
			if (!res) {
				bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
				bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
				bi.numbufs = numbufs;
				res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi);
				if (res < 0) {
					cw_log(CW_LOG_WARNING, "Unable to set buffer policy on channel %d\n", channel);
				}
			} else
				cw_log(CW_LOG_WARNING, "Unable to check buffer policy on channel %d\n", channel);
		}
#endif
		tmp->immediate = global.immediate;
		tmp->transfertobusy = global.transfertobusy;
		tmp->sig = signalling;
		tmp->radio = radio;
		tmp->ringt_base = ringt_base;
		tmp->firstradio = 0;
		if ((signalling == SIG_FXOKS) || (signalling == SIG_FXOLS) || (signalling == SIG_FXOGS))
			tmp->permcallwaiting = global.callwaiting;
		else
			tmp->permcallwaiting = 0;
		/* Flag to destroy the channel must be cleared on new mkif.  Part of changes for reload to work */
		tmp->destroy = 0;
		tmp->channel = channel;
		tmp->drings = drings;

		tmp->usedistinctiveringdetection = global.usedistinctiveringdetection;
		tmp->distinctiveringaftercid = global.distinctiveringaftercid;
		tmp->callwaitingcallerid = global.callwaitingcallerid;
		tmp->threewaycalling = global.threewaycalling;
		tmp->adsi = global.adsi;
		tmp->permhidecallerid = global.permhidecallerid;
		tmp->hidecallerid = global.hidecallerid;
		tmp->callreturn = global.callreturn;
		tmp->echocancel = global.echocancel;
		tmp->echotraining = global.echotraining;
		tmp->pulse = global.pulse;
		tmp->echocanbridged = global.echocanbridged;
		tmp->busydetect = global.busydetect;
		tmp->busycount = global.busycount;
		tmp->busy_tonelength = global.busy_tonelength;
		tmp->busy_quietlength = global.busy_quietlength;
		tmp->callprogress = global.callprogress;
		tmp->cancallforward = global.cancallforward;
		tmp->dtmfrelax = global.dtmfrelax;
		tmp->callwaiting = tmp->permcallwaiting;
		tmp->stripmsd = global.stripmsd;
		tmp->use_callerid = global.use_callerid;
		tmp->cid_signalling = global.cid_signalling;
		tmp->cid_start = global.cid_start;
		tmp->cid_send_on = global.cid_send_on;
		tmp->polarity_events = (signalling & __DAHDI_SIG_FXS) ? global.polarity_events : 1;
		tmp->dahditrcallerid = global.dahditrcallerid;
		tmp->restrictcid = global.restrictcid;
		tmp->use_callingpres = global.use_callingpres;
		tmp->priindication_oob = global.priindication_oob;
		tmp->priexclusive = global.priexclusive;

		cw_copy_string(tmp->accountcode, global.accountcode, sizeof(tmp->accountcode));
		tmp->amaflags = global.amaflags;
		tmp->canpark = global.canpark;
		tmp->transfer = global.transfer;
		cw_copy_string(tmp->defcontext, global.context, sizeof(tmp->defcontext));
		cw_copy_string(tmp->language, global.language, sizeof(tmp->language));
		cw_copy_string(tmp->musicclass, global.musicclass, sizeof(tmp->musicclass));
		cw_copy_string(tmp->context, global.context, sizeof(tmp->context));
		cw_copy_string(tmp->exten, global.exten, sizeof(tmp->exten));
		cw_copy_string(tmp->cid_num, global.cid_num, sizeof(tmp->cid_num));

		if (!here) {
			tmp->confno = -1;
			tmp->propconfno = -1;
		}
		tmp->cid_ton = 0;
		cw_copy_string(tmp->cid_name, global.cid_name, sizeof(tmp->cid_name));
		cw_copy_string(tmp->mailbox, global.mailbox, sizeof(tmp->mailbox));
		tmp->msgstate = -1;
		tmp->group = cur_group;
		tmp->callgroup=cur_callergroup;
		tmp->pickupgroup=cur_pickupgroup;
		tmp->cid_rxgain = global.cid_rxgain;
		tmp->rxgain = global.rxgain;
		tmp->txgain = global.txgain;
		tmp->tonezone = global.tonezone;
		tmp->onhooktime = time(NULL);
		if (tmp->subs[SUB_REAL].dfd > -1) {
			set_actual_gain(tmp->subs[SUB_REAL].dfd, 0, tmp->rxgain, tmp->txgain, tmp->law);
			if (tmp->dsp)
				cw_dsp_digitmode(tmp->dsp, DSP_DIGITMODE_DTMF | tmp->dtmfrelax);
			update_conf(tmp);
			if (!here) {
				if (signalling != SIG_PRI)
					/* Hang it up to be sure it's good */
					dahdi_set_hook(tmp->subs[SUB_REAL].dfd, DAHDI_ONHOOK);
			}
			ioctl(tmp->subs[SUB_REAL].dfd,DAHDI_SETTONEZONE,&tmp->tonezone);
#ifdef ZAPATA_PRI
			/* the dchannel is down so put the channel in alarm */
			if (tmp->pri && !pri_is_up(tmp->pri))
				tmp->inalarm = 1;
			else
				tmp->inalarm = 0;
#endif				
			memset(&si, 0, sizeof(si));
			if (ioctl(tmp->subs[SUB_REAL].dfd,DAHDI_SPANSTAT,&si) == -1) {
				cw_log(CW_LOG_ERROR, "Unable to get span status: %s\n", strerror(errno));
				destroy_dahdi_pvt(&tmp);
				return NULL;
			}
			if (si.alarms) tmp->inalarm = 1;
		}

		tmp->polarityonanswerdelay = global.polarityonanswerdelay;
		tmp->answeronpolarityswitch = global.answeronpolarityswitch;
		tmp->hanguponpolarityswitch = global.hanguponpolarityswitch;
		tmp->sendcalleridafter = global.sendcalleridafter;

	}
	if (tmp && !here) {
		/* nothing on the iflist */
		if (!*wlist) {
			*wlist = tmp;
			tmp->prev = NULL;
			tmp->next = NULL;
			*wend = tmp;
		} else {
			/* at least one member on the iflist */
			struct dahdi_pvt *working = *wlist;

			/* check if we maybe have to put it on the begining */
			if (working->channel > tmp->channel) {
				tmp->next = *wlist;
				tmp->prev = NULL;
				(*wlist)->prev = tmp;
				*wlist = tmp;
			} else {
			/* go through all the members and put the member in the right place */
				while (working) {
					/* in the middle */
					if (working->next) {
						if (working->channel < tmp->channel && working->next->channel > tmp->channel) {
							tmp->next = working->next;
							tmp->prev = working;
							working->next->prev = tmp;
							working->next = tmp;
							break;
						}
					} else {
					/* the last */
						if (working->channel < tmp->channel) {
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

static inline int available(struct dahdi_pvt *p, int channelmatch, int groupmatch, int *busy)
{
	int res;
	struct dahdi_params par;
	/* First, check group matching */
	if ((p->group & groupmatch) != groupmatch)
		return 0;
	/* Check to see if we have a channel match */
	if ((channelmatch > 0) && (p->channel != channelmatch))
		return 0;
	/* We're at least busy at this point */
	if (busy) {
		if ((p->sig == SIG_FXOKS) || (p->sig == SIG_FXOLS) || (p->sig == SIG_FXOGS))
			*busy = 1;
	}
	/* If do not disturb, definitely not */
	if (p->dnd)
		return 0;
	/* If guard time, definitely not */
	if (p->guardtime && (time(NULL) < p->guardtime)) 
		return 0;
		
	/* If no owner definitely available */
	if (!p->owner) {
#ifdef ZAPATA_PRI
		/* Trust PRI */
		if (p->pri) {
			if (p->resetting || p->call)
				return 0;
			else
				return 1;
		}
#endif
		if (!p->radio)
		{
			if (!p->sig || (p->sig == SIG_FXSLS))
				return 1;
			/* Check hook state */
			if (p->subs[SUB_REAL].dfd > -1) {
				memset(&par, 0, sizeof(par));
				res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &par);
			} else {
				/* Assume not off hook on CVRS */
				res = 0;
				par.rxisoffhook = 0;
			}
			if (res) {
				cw_log(CW_LOG_WARNING, "Unable to check hook state on channel %d\n", p->channel);
			} else if ((p->sig == SIG_FXSKS) || (p->sig == SIG_FXSGS)) {
				/* When "onhook" that means no battery on the line, and thus
				  it is out of service..., if it's on a TDM card... If it's a channel
				  bank, there is no telling... */
				if (par.rxbits > -1)
					return 1;
				if (par.rxisoffhook)
					return 1;
				else
#ifdef DAHDI_CHECK_HOOKSTATE
					return 0;
#else
					return 1;
#endif
			} else if (par.rxisoffhook) {
				cw_log(CW_LOG_DEBUG, "Channel %d off hook, can't use\n", p->channel);
				/* Not available when the other end is off hook */
				return 0;
			}
		}
		return 1;
	}

	/* If it's not an FXO, forget about call wait */
	if ((p->sig != SIG_FXOKS) && (p->sig != SIG_FXOLS) && (p->sig != SIG_FXOGS)) 
		return 0;

	if (!p->callwaiting) {
		/* If they don't have call waiting enabled, then for sure they're unavailable at this point */
		return 0;
	}

	if (p->subs[SUB_CALLWAIT].dfd > -1) {
		/* If there is already a call waiting call, then we can't take a second one */
		return 0;
	}
	
	if ((p->owner->_state != CW_STATE_UP) &&
		((p->owner->_state != CW_STATE_RINGING) || p->outgoing)) {
		/* If the current call is not up, then don't allow the call */
		return 0;
	}
	if ((p->subs[SUB_THREEWAY].owner) && (!p->subs[SUB_THREEWAY].inthreeway)) {
		/* Can't take a call wait when the three way calling hasn't been merged yet. */
		return 0;
	}
	/* We're cool */
	return 1;
}

static struct dahdi_pvt *chandup(struct dahdi_pvt *src)
{
	struct dahdi_pvt *p;
	struct dahdi_bufferinfo bi;
	int res;
	p = malloc(sizeof(struct dahdi_pvt));
	if (p) {
		memcpy(p, src, sizeof(struct dahdi_pvt));
		cw_mutex_init(&p->lock);
		p->subs[SUB_REAL].dfd = dahdi_open("/dev/dahdi/pseudo");
		/* Allocate a zapata structure */
		if (p->subs[SUB_REAL].dfd < 0) {
			cw_log(CW_LOG_ERROR, "Unable to dup channel: %s\n",  strerror(errno));
			destroy_dahdi_pvt(&p);
			return NULL;
		}
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_BUFINFO, &bi);
		if (!res) {
			bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
			bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
			bi.numbufs = numbufs;
			res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi);
			if (res < 0) {
				cw_log(CW_LOG_WARNING, "Unable to set buffer policy on dup channel\n");
			}
		} else
			cw_log(CW_LOG_WARNING, "Unable to check buffer policy on dup channel\n");
	}
	p->destroy = 1;
	p->next = iflist;
	iflist = p;
	return p;
}
	

#ifdef ZAPATA_PRI
static int pri_find_empty_chan(struct dahdi_pri *pri, int backwards)
{
	int x;
	if (backwards)
		x = pri->numchans;
	else
		x = 0;
	for (;;) {
		if (backwards && (x < 0))
			break;
		if (!backwards && (x >= pri->numchans))
			break;
		if (pri->pvts[x] && !pri->pvts[x]->inalarm && !pri->pvts[x]->owner) {
			cw_log(CW_LOG_DEBUG, "Found empty available channel %d/%d\n", 
				pri->pvts[x]->logicalspan, pri->pvts[x]->prioffset);
			return x;
		}
		if (backwards)
			x--;
		else
			x++;
	}
	return -1;
}
#endif

static struct cw_channel *dahdi_request(const char *type, int format, void *data, int *cause)
{
	int oldformat;
	int groupmatch = 0;
	int channelmatch = -1;
	int roundrobin = 0;
	int callwait = 0;
	int busy = 0;
	struct dahdi_pvt *p;
	struct cw_channel *tmp = NULL;
	char *dest=NULL;
	int x;
	char *s;
	char opt=0;
	int res=0, y=0;
	int backwards = 0;
#ifdef ZAPATA_PRI
	int crv;
	int bearer = -1;
	int trunkgroup;
	struct dahdi_pri *pri=NULL;
#endif	
	struct dahdi_pvt *last, *start, *end;
	cw_mutex_t *lock;

	CW_UNUSED(type);

	/* Assume we're locking the iflock */
	lock = &iflock;
	start = iflist;
	end = ifend;
	/* We do signed linear */
	oldformat = format;
	format &= (CW_FORMAT_SLINEAR | CW_FORMAT_ULAW);
	if (!format) {
		cw_log(CW_LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}
	if (data) {
		dest = cw_strdupa((char *)data);
	} else {
		cw_log(CW_LOG_WARNING, "Channel requested with no data\n");
		return NULL;
	}
	if (toupper(dest[0]) == 'G'  ||  toupper(dest[0]) == 'R')
    {
		/* Retrieve the group number */
		char *stringp;

		stringp = dest + 1;
		s = strsep(&stringp, "/");
		if ((res = sscanf(s, "%d%c%d", &x, &opt, &y)) < 1)
        {
			cw_log(CW_LOG_WARNING, "Unable to determine group for data %s\n", (char *) data);
			return NULL;
		}
		groupmatch = 1 << x;
		if (toupper(dest[0]) == 'G')
        {
			if (dest[0] == 'G')
            {
				backwards = 1;
				p = ifend;
			}
            else
				p = iflist;
		}
        else
        {
			if (dest[0] == 'R')
            {
				backwards = 1;
				if ((p = round_robin[x]?round_robin[x]->prev:ifend) == NULL)
					p = ifend;
			}
            else
            {
				if ((p = round_robin[x]?round_robin[x]->next:iflist) == NULL)
					p = iflist;
			}
			roundrobin = 1;
		}
	}
    else
    {
		char *stringp;

		stringp = dest;
		s = strsep(&stringp, "/");
		p = iflist;
		if (!strcasecmp(s, "pseudo"))
        {
			/* Special case for pseudo */
			x = CHAN_PSEUDO;
			channelmatch = x;
		} 
#ifdef ZAPATA_PRI
		else if ((res = sscanf(s, "%d:%d%c%d", &trunkgroup, &crv, &opt, &y)) > 1)
        {
			if ((trunkgroup < 1) || (crv < 1))
            {
				cw_log(CW_LOG_WARNING, "Unable to determine trunk group and CRV for data %s\n", (char *)data);
				return NULL;
			}
			res--;
			for (x = 0;  x < NUM_SPANS;  x++)
            {
				if (pris[x].trunkgroup == trunkgroup) {
					pri = pris + x;
					lock = &pri->lock;
					start = pri->crvs;
					end = pri->crvend;
					break;
				}
			}
			if (!pri) {
				cw_log(CW_LOG_WARNING, "Unable to find trunk group %d\n", trunkgroup);
				return NULL;
			}
			channelmatch = crv;
			p = pris[x].crvs;
		}
#endif	
		else if ((res = sscanf(s, "%d%c%d", &x, &opt, &y)) < 1) {
			cw_log(CW_LOG_WARNING, "Unable to determine channel for data %s\n", (char *)data);
			return NULL;
		}
        else
        {
			channelmatch = x;
		}
	}
	/* Search for an unowned channel */
	if (cw_mutex_lock(lock))
    {
		cw_log(CW_LOG_ERROR, "Unable to lock interface list???\n");
		return NULL;
	}
	last = p;
	while (p  &&  !tmp)
    {
		if (roundrobin)
			round_robin[x] = p;
#if 0
		cw_verbose("name = %s, %d, %d, %d\n",p->owner ? p->owner->name : "<none>", p->channel, channelmatch, groupmatch);
#endif
		if (p  &&  available(p, channelmatch, groupmatch, &busy))
        {
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "Using channel %d\n", p->channel);
				if (p->inalarm) 
					goto next;

			callwait = (p->owner != NULL);
#ifdef ZAPATA_PRI
			if (pri && (p->subs[SUB_REAL].dfd < 0)) {
				if (p->sig != SIG_FXSKS) {
					/* Gotta find an actual channel to use for this
					   CRV if this isn't a callwait */
					bearer = pri_find_empty_chan(pri, 0);
					if (bearer < 0) {
						cw_log(CW_LOG_NOTICE, "Out of bearer channels on span %d for call to CRV %d:%d\n", pri->span, trunkgroup, crv);
						p = NULL;
						break;
					}
					pri_assign_bearer(p, pri, pri->pvts[bearer]);
				} else {
					if (alloc_sub(p, 0)) {
						cw_log(CW_LOG_NOTICE, "Failed to allocate place holder pseudo channel!\n");
						p = NULL;
						break;
					} else
						cw_log(CW_LOG_DEBUG, "Allocated placeholder pseudo channel\n");
					p->pri = pri;
				}
			}
#endif			
			if (p->channel == CHAN_PSEUDO)
            {
				if ((p = chandup(p)) == NULL)
					break;
			}
			if (p->owner)
            {
				if (alloc_sub(p, SUB_CALLWAIT))
                {
					p = NULL;
					break;
				}
			}
			p->outgoing = 1;
			tmp = dahdi_new(p, CW_STATE_RESERVED, 0, p->owner ? SUB_CALLWAIT : SUB_REAL, 0, 0);
#ifdef ZAPATA_PRI
			if (p->bearer)
            {
				/* Log owner to bearer channel, too */
				p->bearer->owner = tmp;
			}
#endif			
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
						cw_log(CW_LOG_WARNING, "Distinctive ring missing identifier in '%s'\n", (char *)data);
					else
						p->distinctivering = y;
				}
                else if (opt == 'd')
                {
					/* If this is an ISDN call, make it digital */
					p->digital = 1;
					if (tmp)
						tmp->transfercapability = CW_TRANS_CAP_DIGITAL;
				}
                else
                {
					cw_log(CW_LOG_WARNING, "Unknown option '%c' in '%s'\n", opt, (char *)data);
				}
			}
			/* Note if the call is a call waiting call */
			if (tmp && callwait)
				tmp->cdrflags |= CW_CDR_CALLWAIT;
			break;
		}
next:
		if (backwards)
        {
			if ((p = p->prev) == NULL)
				p = end;
		}
        else
        {
			if ((p = p->next) == NULL)
				p = start;
		}
		/* Stop when rolling to the one that we started from */
		if (p == last)
			break;
	}
	cw_mutex_unlock(lock);
	restart_monitor();
	if (callwait  ||  (tmp == NULL  &&  busy))
		*cause = CW_CAUSE_BUSY;
	return tmp;
}


#ifdef ZAPATA_PRI
static struct dahdi_pvt *pri_find_crv(struct dahdi_pri *pri, int crv)
{
	struct dahdi_pvt *p;
	p = pri->crvs;
	while(p) {
		if (p->channel == crv)
			return p;
		p = p->next;
	}
	return NULL;
}


static int pri_find_principle(struct dahdi_pri *pri, int channel)
{
	int x;
	int span = PRI_SPAN(channel);
	int spanfd;
	struct dahdi_params param;
	int principle = -1;
	int explicit = PRI_EXPLICIT(channel);
	span = PRI_SPAN(channel);
	channel = PRI_CHANNEL(channel);

	if (!explicit) {
		spanfd = pri_active_dchan_fd(pri);
		memset(&param, 0, sizeof(param));
		if (ioctl(spanfd, DAHDI_GET_PARAMS, &param))
			return -1;
		span = pris[param.spanno - 1].prilogicalspan;
	}

	for (x=0;x<pri->numchans;x++) {
		if (pri->pvts[x] && (pri->pvts[x]->prioffset == channel) && (pri->pvts[x]->logicalspan == span)) {
			principle = x;
			break;
		}
	}
	
	return principle;
}

static int pri_fixup_principle(struct dahdi_pri *pri, int principle, q931_call *c)
{
	int x;
	struct dahdi_pvt *crv;
	if (!c) {
		if (principle < 0)
			return -1;
		return principle;
	}
	if ((principle > -1) && 
		(principle < pri->numchans) && 
		(pri->pvts[principle]) && 
		(pri->pvts[principle]->call == c))
		return principle;
	/* First, check for other bearers */
	for (x=0;x<pri->numchans;x++) {
		if (!pri->pvts[x]) continue;
		if (pri->pvts[x]->call == c) {
			/* Found our call */
			if (principle != x) {
				if (option_verbose > 2)
					cw_verbose(VERBOSE_PREFIX_3 "Moving call from channel %d to channel %d\n",
						pri->pvts[x]->channel, pri->pvts[principle]->channel);
				if (pri->pvts[principle]->owner) {
					cw_log(CW_LOG_WARNING, "Can't fix up channel from %d to %d because %d is already in use\n",
						pri->pvts[x]->channel, pri->pvts[principle]->channel, pri->pvts[principle]->channel);
					return -1;
				}
				/* Fix it all up now */
				pri->pvts[principle]->owner = pri->pvts[x]->owner;
				if (pri->pvts[principle]->owner) {
					cw_change_name(pri->pvts[principle]->owner, "DAHDI/%d:%d-%d", pri->trunkgroup, pri->pvts[principle]->channel, 1);
					pri->pvts[principle]->owner->tech_pvt = pri->pvts[principle];
					pri->pvts[principle]->owner->fds[0] = pri->pvts[principle]->subs[SUB_REAL].dfd;
					pri->pvts[principle]->subs[SUB_REAL].owner = pri->pvts[x]->subs[SUB_REAL].owner;
				} else
					cw_log(CW_LOG_WARNING, "Whoa, there's no  owner, and we're having to fix up channel %d to channel %d\n", pri->pvts[x]->channel, pri->pvts[principle]->channel);
				pri->pvts[principle]->call = pri->pvts[x]->call;
				/* Free up the old channel, now not in use */
				pri->pvts[x]->subs[SUB_REAL].owner = NULL;
				pri->pvts[x]->owner = NULL;
				pri->pvts[x]->call = NULL;
			}
			return principle;
		}
	}
	/* Now check for a CRV with no bearer */
	crv = pri->crvs;
	while(crv) {
		if (crv->call == c) {
			/* This is our match...  Perform some basic checks */
			if (crv->bearer)
				cw_log(CW_LOG_WARNING, "Trying to fix up call which already has a bearer which isn't the one we think it is\n");
			else if (pri->pvts[principle]->owner) 
				cw_log(CW_LOG_WARNING, "Tring to fix up a call to a bearer which already has an owner!\n");
			else {
				/* Looks good.  Drop the pseudo channel now, clear up the assignment, and
				   wakeup the potential sleeper */
				dahdi_close(crv->subs[SUB_REAL].dfd);
				pri->pvts[principle]->call = crv->call;
				pri_assign_bearer(crv, pri, pri->pvts[principle]);
				cw_log(CW_LOG_DEBUG, "Assigning bearer %d/%d to CRV %d:%d\n",
									pri->pvts[principle]->logicalspan, pri->pvts[principle]->prioffset,
									pri->trunkgroup, crv->channel);
				wakeup_sub(crv, SUB_REAL, pri);
			}
			return principle;
		}
		crv = crv->next;
	}
	cw_log(CW_LOG_WARNING, "Call specified, but not found?\n");
	return -1;
}

static void *do_idle_thread(void *vchan)
{
	struct cw_channel *chan = vchan;
	struct dahdi_pvt *pvt = chan->tech_pvt;
	struct cw_frame *f;
	char ex[80];
	/* Wait up to 30 seconds for an answer */
	int newms, ms = 30000;
	if (option_verbose > 2) 
		cw_verbose(VERBOSE_PREFIX_3 "Initiating idle call on channel %s\n", chan->name);
	snprintf(ex, sizeof(ex), "%d/%s", pvt->channel, pvt->pri->idledial);
	if (cw_call(chan, ex)) {
		cw_log(CW_LOG_WARNING, "Idle dial failed on '%s' to '%s'\n", chan->name, ex);
		cw_hangup(chan);
		return NULL;
	}
	while((newms = cw_waitfor(chan, ms)) > 0) {
		f = cw_read(chan);
		if (!f) {
			/* Got hangup */
			break;
		}
		if (f->frametype == CW_FRAME_CONTROL) {
			switch(f->subclass) {
			case CW_CONTROL_ANSWER:
				/* Launch the PBX */
				cw_copy_string(chan->exten, pvt->pri->idleext, sizeof(chan->exten));
				cw_copy_string(chan->context, pvt->pri->idlecontext, sizeof(chan->context));
				chan->priority = 1;
				if (option_verbose > 3) 
					cw_verbose(VERBOSE_PREFIX_3 "Idle channel '%s' answered, sending to %s@%s\n", chan->name, chan->exten, chan->context);
				cw_pbx_run(chan);
				/* It's already hungup, return immediately */
				return NULL;
			case CW_CONTROL_BUSY:
				if (option_verbose > 3) 
					cw_verbose(VERBOSE_PREFIX_3 "Idle channel '%s' busy, waiting...\n", chan->name);
				break;
			case CW_CONTROL_CONGESTION:
				if (option_verbose > 3) 
					cw_verbose(VERBOSE_PREFIX_3 "Idle channel '%s' congested, waiting...\n", chan->name);
				break;
			};
		}
		cw_fr_free(f);
		ms = newms;
	}
	/* Hangup the channel since nothing happend */
	cw_hangup(chan);
	return NULL;
}

#ifndef PRI_RESTART
#error "Upgrade your libpri"
#endif
static void dahdi_pri_message(struct pri *pri, char *s)
{
	int x, y;
	int dchan = -1, span = -1;
	int dchancount = 0;

	if (pri) {
		for (x = 0; x < NUM_SPANS; x++) {
			for (y = 0; y < NUM_DCHANS; y++) {
				if (pris[x].dchans[y])
					dchancount++;

				if (pris[x].dchans[y] == pri)
					dchan = y;
			}
			if (dchan >= 0) {
				span = x;
				break;
			}
			dchancount = 0;
		}
		if ((dchan >= 0) && (span >= 0)) {
			if (dchancount > 1)
				cw_verbose("[Span %d D-Channel %d]%s", span, dchan, s);
			else
				cw_verbose("%s", s);
		} else
			cw_verbose("PRI debug error: could not find pri associated it with debug message output\n");
	} else
		cw_verbose("%s", s);

	cw_mutex_lock(&pridebugfdlock);

	if (pridebugfd >= 0)
		write(pridebugfd, s, strlen(s));

	cw_mutex_unlock(&pridebugfdlock);
}

static void dahdi_pri_error(struct pri *pri, char *s)
{
	int x, y;
	int dchan = -1, span = -1;
	int dchancount = 0;

	if (pri) {
		for (x = 0; x < NUM_SPANS; x++) {
			for (y = 0; y < NUM_DCHANS; y++) {
				if (pris[x].dchans[y])
					dchancount++;

				if (pris[x].dchans[y] == pri)
					dchan = y;
			}
			if (dchan >= 0) {
				span = x;
				break;
			}
			dchancount = 0;
		}
		if ((dchan >= 0) && (span >= 0)) {
			if (dchancount > 1)
				cw_log(CW_LOG_WARNING, "[Span %d D-Channel %d] PRI: %s", span, dchan, s);
			else
				cw_verbose("%s", s);
		} else
			cw_verbose("PRI debug error: could not find pri associated it with debug message output\n");
	} else
		cw_log(CW_LOG_WARNING, "%s", s);

	cw_mutex_lock(&pridebugfdlock);

	if (pridebugfd >= 0)
		write(pridebugfd, s, strlen(s));

	cw_mutex_unlock(&pridebugfdlock);
}

static int pri_check_restart(struct dahdi_pri *pri)
{
	do {
		pri->resetpos++;
	} while((pri->resetpos < pri->numchans) &&
		 (!pri->pvts[pri->resetpos] ||
		  pri->pvts[pri->resetpos]->call ||
		  pri->pvts[pri->resetpos]->resetting));
	if (pri->resetpos < pri->numchans) {
		/* Mark the channel as resetting and restart it */
		pri->pvts[pri->resetpos]->resetting = 1;
		pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[pri->resetpos]));
	} else {
		pri->resetting = 0;
		time(&pri->lastreset);
	}
	return 0;
}

static int pri_hangup_all(struct dahdi_pvt *p, struct dahdi_pri *pri)
{
	int x;
	int redo;
	cw_mutex_unlock(&pri->lock);
	cw_mutex_lock(&p->lock);
	do {
		redo = 0;
		for (x=0;x<3;x++) {
			while(p->subs[x].owner && cw_channel_trylock(p->subs[x].owner)) {
				redo++;
				cw_mutex_unlock(&p->lock);
				usleep(1);
				cw_mutex_lock(&p->lock);
			}
			if (p->subs[x].owner) {
				cw_queue_hangup(p->subs[x].owner);
				cw_channel_unlock(p->subs[x].owner);
			}
		}
	} while (redo);
	cw_mutex_unlock(&p->lock);
	cw_mutex_lock(&pri->lock);
	return 0;
}
char * redirectingreason2str(int redirectingreason)
{
	switch (redirectingreason) {
	case 0:
		return "UNKNOWN";
	case 1:
		return "BUSY";
	case 2:
		return "NO_REPLY";
	case 0xF:
		return "UNCONDITIONAL";
	default:
		return "NOREDIRECT";
	}
}

static void apply_plan_to_number(char *buf, size_t size, const struct dahdi_pri *pri, const char *number, const int plan)
{
	switch (plan) {
	case PRI_INTERNATIONAL_ISDN:		/* Q.931 dialplan == 0x11 international dialplan => prepend international prefix digits */
		snprintf(buf, size, "%s%s", pri->internationalprefix, number);
		break;
	case PRI_NATIONAL_ISDN:			/* Q.931 dialplan == 0x21 national dialplan => prepend national prefix digits */
		snprintf(buf, size, "%s%s", pri->nationalprefix, number);
		break;
	case PRI_LOCAL_ISDN:			/* Q.931 dialplan == 0x41 local dialplan => prepend local prefix digits */
		snprintf(buf, size, "%s%s", pri->localprefix, number);
		break;
	case PRI_PRIVATE:			/* Q.931 dialplan == 0x49 private dialplan => prepend private prefix digits */
		snprintf(buf, size, "%s%s", pri->privateprefix, number);
		break;
	case PRI_UNKNOWN:			/* Q.931 dialplan == 0x00 unknown dialplan => prepend unknown prefix digits */
		snprintf(buf, size, "%s%s", pri->unknownprefix, number);
		break;
	default:				/* other Q.931 dialplan => don't twiddle with callingnum */
		snprintf(buf, size, "%s", number);
		break;
	}
}

static void *pri_dchannel(void *vpri)
{
	struct dahdi_pri *pri = vpri;
	pri_event *e;
	struct pollfd fds[NUM_DCHANS];
	int res;
	int chanpos = 0;
	int x;
	int haveidles;
	int activeidles;
	int nextidle = -1;
	struct cw_channel *c;
	struct timeval tv, lowest, *next;
	struct timeval lastidle = { 0, 0 };
	int doidling=0;
	char *cc;
	char idlen[80];
	struct cw_channel *idle;
	pthread_t p;
	time_t t;
	int i, which=-1;
	int numdchans;
	int cause=0;
	struct dahdi_pvt *crv;
	pthread_t threadid;
	char ani2str[6];
	char plancallingnum[256];
	char plancallingani[256];
	char calledtonstr[10];

	gettimeofday(&lastidle, NULL);
	if (!cw_strlen_zero(pri->idledial) && !cw_strlen_zero(pri->idleext)) {
		/* Need to do idle dialing, check to be sure though */
		cc = strchr(pri->idleext, '@');
		if (cc) {
			*cc = '\0';
			cc++;
			cw_copy_string(pri->idlecontext, cc, sizeof(pri->idlecontext));
#if 0
			/* Extensions may not be loaded yet */
			if (!cw_exists_extension(NULL, pri->idlecontext, pri->idleext, 1, NULL))
				cw_log(CW_LOG_WARNING, "Extension '%s @ %s' does not exist\n", pri->idleext, pri->idlecontext);
			else
#endif
				doidling = 1;
		} else
			cw_log(CW_LOG_WARNING, "Idle dial string '%s' lacks '@context'\n", pri->idleext);
	}
	for(;;) {
		for (i=0;i<NUM_DCHANS;i++) {
			if (!pri->dchannels[i])
				break;
			fds[i].fd = pri->fds[i];
			fds[i].events = POLLIN | POLLPRI;
			fds[i].revents = 0;
		}
		numdchans = i;
		time(&t);
		cw_mutex_lock(&pri->lock);
		if (pri->switchtype != PRI_SWITCH_GR303_TMC && (pri->resetinterval > 0)) {
			if (pri->resetting && pri_is_up(pri)) {
				if (pri->resetpos < 0)
					pri_check_restart(pri);
			} else {
				if (!pri->resetting	&& (t - pri->lastreset) >= pri->resetinterval) {
					pri->resetting = 1;
					pri->resetpos = -1;
				}
			}
		}
		/* Look for any idle channels if appropriate */
		if (doidling && pri_is_up(pri)) {
			nextidle = -1;
			haveidles = 0;
			activeidles = 0;
			for (x=pri->numchans;x>=0;x--) {
				if (pri->pvts[x] && !pri->pvts[x]->owner && 
				    !pri->pvts[x]->call) {
					if (haveidles < pri->minunused) {
						haveidles++;
					} else if (!pri->pvts[x]->resetting) {
						nextidle = x;
						break;
					}
				} else if (pri->pvts[x] && pri->pvts[x]->owner && pri->pvts[x]->isidlecall)
					activeidles++;
			}
			if (nextidle > -1) {
				if (cw_tvdiff_ms(cw_tvnow(), lastidle) > 1000) {
					/* Don't create a new idle call more than once per second */
					snprintf(idlen, sizeof(idlen), "%d/%s", pri->pvts[nextidle]->channel, pri->idledial);
					idle = dahdi_request("DAHDI", CW_FORMAT_ULAW, idlen, &cause);
					if (idle) {
						pri->pvts[nextidle]->isidlecall = 1;
						if (cw_pthread_create(&p, &global_attr_default, do_idle_thread, idle)) {
							cw_log(CW_LOG_WARNING, "Unable to start new thread for idle channel '%s'\n", idle->name);
							dahdi_hangup(idle);
						}
					} else
						cw_log(CW_LOG_WARNING, "Unable to request channel 'DAHDI/%s' for idle call\n", idlen);
					gettimeofday(&lastidle, NULL);
				}
			} else if ((haveidles < pri->minunused) &&
				   (activeidles > pri->minidle)) {
				/* Mark something for hangup if there is something 
				   that can be hungup */
				for (x=pri->numchans;x>=0;x--) {
					/* find a candidate channel */
					if (pri->pvts[x] && pri->pvts[x]->owner && pri->pvts[x]->isidlecall) {
						pri->pvts[x]->owner->_softhangup |= CW_SOFTHANGUP_DEV;
						haveidles++;
						/* Stop if we have enough idle channels or
						  can't spare any more active idle ones */
						if ((haveidles >= pri->minunused) ||
						    (activeidles <= pri->minidle))
							break;
					} 
				}
			}
		}
		/* Start with reasonable max */
		lowest = cw_tv(60, 0);
		for (i=0; i<NUM_DCHANS; i++) {
			/* Find lowest available d-channel */
			if (!pri->dchannels[i])
				break;
			if ((next = pri_schedule_next(pri->dchans[i]))) {
				/* We need relative time here */
				tv = cw_tvsub(*next, cw_tvnow());
				if (tv.tv_sec < 0) {
					tv = cw_tv(0,0);
				}
				if (doidling || pri->resetting) {
					if (tv.tv_sec > 1) {
						tv = cw_tv(1, 0);
					}
				} else {
					if (tv.tv_sec > 60) {
						tv = cw_tv(60, 0);
					}
				}
			} else if (doidling || pri->resetting) {
				/* Make sure we stop at least once per second if we're
				   monitoring idle channels */
				tv = cw_tv(1,0);
			} else {
				/* Don't poll for more than 60 seconds */
				tv = cw_tv(60, 0);
			}
			if (!i || cw_tvcmp(tv, lowest) < 0) {
				lowest = tv;
			}
		}
		cw_mutex_unlock(&pri->lock);

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		pthread_testcancel();

		e = NULL;
		res = poll(fds, numdchans, lowest.tv_sec * 1000 + lowest.tv_usec / 1000);

		pthread_testcancel();
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		cw_mutex_lock(&pri->lock);
		if (!res) {
			for (which=0;which<NUM_DCHANS;which++) {
				if (!pri->dchans[which])
					break;
				/* Just a timeout, run the scheduler */
				e = pri_schedule_run(pri->dchans[which]);
				if (e)
					break;
			}
		} else if (res > -1) {
			for (which=0;which<NUM_DCHANS;which++) {
				if (!pri->dchans[which])
					break;
				if (fds[which].revents & POLLPRI) {
					/* Check for an event */
					x = 0;
					res = ioctl(pri->fds[which], DAHDI_GETEVENT, &x);
					if (x) 
						cw_log(CW_LOG_NOTICE, "PRI got event: %s (%d) on %s D-channel of span %d\n", event2str(x), x, pri_order(which), pri->span);
					/* Keep track of alarm state */	
					if (x == DAHDI_EVENT_ALARM) {
						pri->dchanavail[which] &= ~(DCHAN_NOTINALARM | DCHAN_UP);
						pri_find_dchan(pri);
					} else if (x == DAHDI_EVENT_NOALARM) {
						pri->dchanavail[which] |= DCHAN_NOTINALARM;
						pri_restart(pri->dchans[which]);
					}
				
					if (option_debug)
						cw_log(CW_LOG_DEBUG, "Got event %s (%d) on D-channel for span %d\n", event2str(x), x, pri->span);
				} else if (fds[which].revents & POLLIN) {
					e = pri_check_event(pri->dchans[which]);
				}
				if (e)
					break;
			}
		} else if (errno != EINTR)
			cw_log(CW_LOG_WARNING, "pri_event returned error %d (%s)\n", errno, strerror(errno));

		if (e) {
			if (pri->debug)
				pri_dump_event(pri->dchans[which], e);
			if (e->e != PRI_EVENT_DCHAN_DOWN)
				pri->dchanavail[which] |= DCHAN_UP;

			if ((e->e != PRI_EVENT_DCHAN_UP) && (e->e != PRI_EVENT_DCHAN_DOWN) && (pri->pri != pri->dchans[which]))
				/* Must be an NFAS group that has the secondary dchan active */
				pri->pri = pri->dchans[which];

			switch(e->e) {
			case PRI_EVENT_DCHAN_UP:
				if (option_verbose > 1) 
					cw_verbose(VERBOSE_PREFIX_2 "%s D-Channel on span %d up\n", pri_order(which), pri->span);
				pri->dchanavail[which] |= DCHAN_UP;
				if (!pri->pri) pri_find_dchan(pri);

				/* Note presense of D-channel */
				time(&pri->lastreset);

				/* Restart in 5 seconds */
				if (pri->resetinterval > -1) {
					pri->lastreset -= pri->resetinterval;
					pri->lastreset += 5;
				}
				pri->resetting = 0;
				/* Take the channels from inalarm condition */
				for (i=0; i<pri->numchans; i++)
					if (pri->pvts[i]) {
						pri->pvts[i]->inalarm = 0;
					}
				break;
			case PRI_EVENT_DCHAN_DOWN:
				if (option_verbose > 1) 
					cw_verbose(VERBOSE_PREFIX_2 "%s D-Channel on span %d down\n", pri_order(which), pri->span);
				pri->dchanavail[which] &= ~DCHAN_UP;
				pri_find_dchan(pri);
				if (!pri_is_up(pri)) {
					pri->resetting = 0;
					/* Hangup active channels and put them in alarm mode */
					for (i=0; i<pri->numchans; i++) {
						struct dahdi_pvt *p = pri->pvts[i];
						if (p) {
							if (p->call) {
								if (p->pri && p->pri->pri) {
									pri_hangup(p->pri->pri, p->call, -1);
									pri_destroycall(p->pri->pri, p->call);
									p->call = NULL;
								} else
									cw_log(CW_LOG_WARNING, "The PRI Call have not been destroyed\n");
							}
							if (p->realcall) {
								pri_hangup_all(p->realcall, pri);
							} else if (p->owner)
								p->owner->_softhangup |= CW_SOFTHANGUP_DEV;
							p->inalarm = 1;
						}
					}
				}
				break;
			case PRI_EVENT_RESTART:
				if (e->restart.channel > -1) {
					chanpos = pri_find_principle(pri, e->restart.channel);
					if (chanpos < 0)
						cw_log(CW_LOG_WARNING, "Restart requested on odd/unavailable channel number %d/%d on span %d\n", 
							PRI_SPAN(e->restart.channel), PRI_CHANNEL(e->restart.channel), pri->span);
					else {
						if (option_verbose > 2)
							cw_verbose(VERBOSE_PREFIX_3 "B-channel %d/%d restarted on span %d\n", 
								PRI_SPAN(e->restart.channel), PRI_CHANNEL(e->restart.channel), pri->span);
						cw_mutex_lock(&pri->pvts[chanpos]->lock);
						if (pri->pvts[chanpos]->call) {
							pri_destroycall(pri->pri, pri->pvts[chanpos]->call);
							pri->pvts[chanpos]->call = NULL;
						}
						/* Force soft hangup if appropriate */
						if (pri->pvts[chanpos]->realcall) 
							pri_hangup_all(pri->pvts[chanpos]->realcall, pri);
						else if (pri->pvts[chanpos]->owner)
							pri->pvts[chanpos]->owner->_softhangup |= CW_SOFTHANGUP_DEV;
						cw_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				} else {
					if (option_verbose > 2)
						cw_verbose(VERBOSE_PREFIX_2 "Restart on requested on entire span %d\n", pri->span);
					for (x=0;x < pri->numchans;x++)
						if (pri->pvts[x]) {
							cw_mutex_lock(&pri->pvts[x]->lock);
							if (pri->pvts[x]->call) {
								pri_destroycall(pri->pri, pri->pvts[x]->call);
								pri->pvts[x]->call = NULL;
							}
							if (pri->pvts[chanpos]->realcall) 
								pri_hangup_all(pri->pvts[chanpos]->realcall, pri);
 							else if (pri->pvts[x]->owner)
								pri->pvts[x]->owner->_softhangup |= CW_SOFTHANGUP_DEV;
							cw_mutex_unlock(&pri->pvts[x]->lock);
						}
				}
				break;
			case PRI_EVENT_KEYPAD_DIGIT:
				chanpos = pri_find_principle(pri, e->digit.channel);
				if (chanpos < 0) {
					cw_log(CW_LOG_WARNING, "KEYPAD_DIGITs received on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->digit.channel), PRI_CHANNEL(e->digit.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->digit.call);
					if (chanpos > -1) {
						cw_mutex_lock(&pri->pvts[chanpos]->lock);
						/* queue DTMF frame if the PBX for this call was already started (we're forwarding KEYPAD_DIGITs further on */
						if (pri->overlapdial && pri->pvts[chanpos]->call==e->digit.call && pri->pvts[chanpos]->owner) {
							/* how to do that */
							int digitlen = strlen(e->digit.digits);
							char digit;
							int i;					
							for (i=0; i<digitlen; i++) {	
								digit = e->digit.digits[i];
								{
									struct cw_frame f = { CW_FRAME_DTMF, digit, };
									dahdi_queue_frame(pri->pvts[chanpos], &f, pri);
								}
							}
						}
						cw_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;
				
			case PRI_EVENT_INFO_RECEIVED:
				chanpos = pri_find_principle(pri, e->ring.channel);
				if (chanpos < 0) {
					cw_log(CW_LOG_WARNING, "INFO received on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->ring.call);
					if (chanpos > -1) {
						cw_mutex_lock(&pri->pvts[chanpos]->lock);
						/* queue DTMF frame if the PBX for this call was already started (we're forwarding INFORMATION further on */
						if (pri->overlapdial && pri->pvts[chanpos]->call==e->ring.call && pri->pvts[chanpos]->owner) {
							/* how to do that */
							int digitlen = strlen(e->ring.callednum);
							char digit;
							int i;					
							for (i=0; i<digitlen; i++) {	
								digit = e->ring.callednum[i];
								{
									struct cw_frame f = { CW_FRAME_DTMF, digit, };
									dahdi_queue_frame(pri->pvts[chanpos], &f, pri);
								}
							}
						}
						cw_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;
			case PRI_EVENT_RING:
				crv = NULL;
				if (e->ring.channel == -1)
					chanpos = pri_find_empty_chan(pri, 1);
				else
					chanpos = pri_find_principle(pri, e->ring.channel);
				/* if no channel specified find one empty */
				if (chanpos < 0) {
					cw_log(CW_LOG_WARNING, "Ring requested on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
				} else {
					cw_mutex_lock(&pri->pvts[chanpos]->lock);
					if (pri->pvts[chanpos]->owner) {
						if (pri->pvts[chanpos]->call == e->ring.call) {
							cw_log(CW_LOG_WARNING, "Duplicate setup requested on channel %d/%d already in use on span %d\n", 
								PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
							cw_mutex_unlock(&pri->pvts[chanpos]->lock);
							break;
						} else {
							/* This is where we handle initial glare */
							cw_log(CW_LOG_DEBUG, "Ring requested on channel %d/%d already in use or previously requested on span %d.  Attempting to renegotiating channel.\n", 
								PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
							cw_mutex_unlock(&pri->pvts[chanpos]->lock);
							chanpos = -1;
						}
					}
					if (chanpos > -1)
						cw_mutex_unlock(&pri->pvts[chanpos]->lock);
				}
				if ((chanpos < 0) && (e->ring.flexible))
					chanpos = pri_find_empty_chan(pri, 1);
				if (chanpos > -1) {
					cw_mutex_lock(&pri->pvts[chanpos]->lock);
					if (pri->switchtype == PRI_SWITCH_GR303_TMC) {
						/* Should be safe to lock CRV AFAIK while bearer is still locked */
						crv = pri_find_crv(pri, pri_get_crv(pri->pri, e->ring.call, NULL));
						if (crv)
							cw_mutex_lock(&crv->lock);
						if (!crv || crv->owner) {
							pri->pvts[chanpos]->call = NULL;
							if (crv) {
								if (crv->owner)
									crv->owner->_softhangup |= CW_SOFTHANGUP_DEV;
								cw_log(CW_LOG_WARNING, "Call received for busy CRV %d on span %d\n", pri_get_crv(pri->pri, e->ring.call, NULL), pri->span);
							} else
								cw_log(CW_LOG_NOTICE, "Call received for unconfigured CRV %d on span %d\n", pri_get_crv(pri->pri, e->ring.call, NULL), pri->span);
							pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_INVALID_CALL_REFERENCE);
							if (crv)
								cw_mutex_unlock(&crv->lock);
							cw_mutex_unlock(&pri->pvts[chanpos]->lock);
							break;
						}
					}
					pri->pvts[chanpos]->call = e->ring.call;
					apply_plan_to_number(plancallingnum, sizeof(plancallingnum), pri, e->ring.callingnum, e->ring.callingplan);
					if (pri->pvts[chanpos]->global.use_callerid) {
						cw_shrink_phone_number(plancallingnum);
						cw_copy_string(pri->pvts[chanpos]->cid_num, plancallingnum, sizeof(pri->pvts[chanpos]->cid_num));
#ifdef PRI_ANI
						if (!cw_strlen_zero(e->ring.callingani)) {
							apply_plan_to_number(plancallingani, sizeof(plancallingani), pri, e->ring.callingani, e->ring.callingplanani);
							cw_shrink_phone_number(plancallingani);
							cw_copy_string(pri->pvts[chanpos]->cid_ani, plancallingani, sizeof(pri->pvts[chanpos]->cid_ani));
						} else {
							pri->pvts[chanpos]->cid_ani[0] = '\0';
						}
#endif
						cw_copy_string(pri->pvts[chanpos]->cid_name, e->ring.callingname, sizeof(pri->pvts[chanpos]->cid_name));
						pri->pvts[chanpos]->cid_ton = e->ring.callingplan; /* this is the callingplan (TON/NPI), e->ring.callingplan>>4 would be the TON */
					} else {
						pri->pvts[chanpos]->cid_num[0] = '\0';
						pri->pvts[chanpos]->cid_ani[0] = '\0';
						pri->pvts[chanpos]->cid_name[0] = '\0';
						pri->pvts[chanpos]->cid_ton = 0;
					}
					apply_plan_to_number(pri->pvts[chanpos]->rdnis, sizeof(pri->pvts[chanpos]->rdnis), pri,
							     e->ring.redirectingnum, e->ring.callingplanrdnis);
					/* If immediate=yes go to s|1 */
					if (pri->pvts[chanpos]->immediate) {
						if (option_verbose > 2)
							cw_verbose(VERBOSE_PREFIX_3 "Going to extension s|1 because of immediate=yes\n");
						pri->pvts[chanpos]->exten[0] = 's';
						pri->pvts[chanpos]->exten[1] = '\0';
					}
					/* Get called number */
					else if (!cw_strlen_zero(e->ring.callednum)) {
						cw_copy_string(pri->pvts[chanpos]->exten, e->ring.callednum, sizeof(pri->pvts[chanpos]->exten));
						cw_copy_string(pri->pvts[chanpos]->dnid, e->ring.callednum, sizeof(pri->pvts[chanpos]->dnid));
					} else
						pri->pvts[chanpos]->exten[0] = '\0';
					/* Set DNID on all incoming calls -- even immediate */
					if (!cw_strlen_zero(e->ring.callednum))
						cw_copy_string(pri->pvts[chanpos]->dnid, e->ring.callednum, sizeof(pri->pvts[chanpos]->dnid));
					/* No number yet, but received "sending complete"? */
					if (e->ring.complete && (cw_strlen_zero(e->ring.callednum))) {
						if (option_verbose > 2)
							cw_verbose(VERBOSE_PREFIX_3 "Going to extension s|1 because of Complete received\n");
						pri->pvts[chanpos]->exten[0] = 's';
						pri->pvts[chanpos]->exten[1] = '\0';
					}
					/* Make sure extension exists (or in overlap dial mode, can exist) */
					if ((pri->overlapdial && cw_canmatch_extension(NULL, pri->pvts[chanpos]->context, pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num)) ||
						cw_exists_extension(NULL, pri->pvts[chanpos]->context, pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num)) {
						/* Setup law */
						int law;
						if (pri->switchtype != PRI_SWITCH_GR303_TMC) {
							/* Set to audio mode at this point */
							law = 1;
							if (ioctl(pri->pvts[chanpos]->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &law) == -1)
								cw_log(CW_LOG_WARNING, "Unable to set audio mode on channel %d to %d\n", pri->pvts[chanpos]->channel, law);
						}
						if (e->ring.layer1 == PRI_LAYER_1_ALAW)
							law = DAHDI_LAW_ALAW;
						else
							law = DAHDI_LAW_MULAW;
						res = dahdi_setlaw(pri->pvts[chanpos]->subs[SUB_REAL].dfd, law);
						if (res < 0) 
							cw_log(CW_LOG_WARNING, "Unable to set law on channel %d\n", pri->pvts[chanpos]->channel);
						res = set_actual_gain(pri->pvts[chanpos]->subs[SUB_REAL].dfd, 0, pri->pvts[chanpos]->rxgain, pri->pvts[chanpos]->txgain, law);
						if (res < 0)
							cw_log(CW_LOG_WARNING, "Unable to set gains on channel %d\n", pri->pvts[chanpos]->channel);
						if (e->ring.complete || !pri->overlapdial) {
							/* Just announce proceeding */
							pri->pvts[chanpos]->proceeding = 1;
							pri_proceeding(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 0);
						} else  {
							if (pri->switchtype != PRI_SWITCH_GR303_TMC) 
								pri_need_more_info(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 1);
							else
								pri_answer(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 1);
						}
						/* Get the use_callingpres state */
						pri->pvts[chanpos]->callingpres = e->ring.callingpres;
					
						/* Start PBX */
						if (pri->overlapdial && cw_matchmore_extension(NULL, pri->pvts[chanpos]->context, pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num)) {
							/* Release the PRI lock while we create the channel */
							cw_mutex_unlock(&pri->lock);
							if (crv) {
								/* Set bearer and such */
								pri_assign_bearer(crv, pri, pri->pvts[chanpos]);
								c = dahdi_new(crv, CW_STATE_RESERVED, 0, SUB_REAL, law, e->ring.ctype);
								pri->pvts[chanpos]->owner = &inuse;
								cw_log(CW_LOG_DEBUG, "Started up crv %d:%d on bearer channel %d\n", pri->trunkgroup, crv->channel, crv->bearer->channel);
							} else {
								c = dahdi_new(pri->pvts[chanpos], CW_STATE_RESERVED, 0, SUB_REAL, law, e->ring.ctype);
							}
							if (!cw_strlen_zero(e->ring.callingsubaddr)) {
								pbx_builtin_setvar_helper(c, "CALLINGSUBADDR", e->ring.callingsubaddr);
							}
							if(e->ring.ani2 >= 0) {
								snprintf(ani2str, 5, "%.2d", e->ring.ani2);
								pbx_builtin_setvar_helper(c, "ANI2", ani2str);
							}
							if (!cw_strlen_zero(e->ring.useruserinfo)) {
								pbx_builtin_setvar_helper(c, "USERUSERINFO", e->ring.useruserinfo);
							}
							snprintf(calledtonstr, sizeof(calledtonstr)-1, "%d", e->ring.calledplan);
							pbx_builtin_setvar_helper(c, "CALLEDTON", calledtonstr);
							if (e->ring.redirectingreason >= 0)
								pbx_builtin_setvar_helper(c, "PRIREDIRECTREASON", redirectingreason2str(e->ring.redirectingreason));
							
							cw_mutex_lock(&pri->lock);
							if (c && !cw_pthread_create(&threadid, &global_attr_detached, ss_thread, c)) {
								if (option_verbose > 2)
									cw_verbose(VERBOSE_PREFIX_3 "Accepting overlap call from '%s' to '%s' on channel %d/%d, span %d\n",
										plancallingnum, !cw_strlen_zero(pri->pvts[chanpos]->exten) ? pri->pvts[chanpos]->exten : "<unspecified>", 
										pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
							} else {
								cw_log(CW_LOG_WARNING, "Unable to start PBX on channel %d/%d, span %d\n", 
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
								if (c)
									cw_hangup(c);
								else {
									pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_SWITCH_CONGESTION);
									pri->pvts[chanpos]->call = NULL;
								}
							}
						} else  {
							cw_mutex_unlock(&pri->lock);
							/* Release PRI lock while we create the channel */
							c = dahdi_new(pri->pvts[chanpos], CW_STATE_RING, 1, SUB_REAL, law, e->ring.ctype);
							cw_mutex_lock(&pri->lock);
							if (c) {
								char calledtonstr[10];
								if(e->ring.ani2 >= 0) {
									snprintf(ani2str, 5, "%d", e->ring.ani2);
									pbx_builtin_setvar_helper(c, "ANI2", ani2str);
								}
								if (!cw_strlen_zero(e->ring.useruserinfo)) {
									pbx_builtin_setvar_helper(c, "USERUSERINFO", e->ring.useruserinfo);
								}
								if (e->ring.redirectingreason >= 0)
									pbx_builtin_setvar_helper(c, "PRIREDIRECTREASON", redirectingreason2str(e->ring.redirectingreason));
							
								snprintf(calledtonstr, sizeof(calledtonstr)-1, "%d", e->ring.calledplan);
								pbx_builtin_setvar_helper(c, "CALLEDTON", calledtonstr);
								if (option_verbose > 2)
									cw_verbose(VERBOSE_PREFIX_3 "Accepting call from '%s' to '%s' on channel %d/%d, span %d\n",
										plancallingnum, pri->pvts[chanpos]->exten, 
											pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
								dahdi_enable_ec(pri->pvts[chanpos]);
							} else {
								cw_log(CW_LOG_WARNING, "Unable to start PBX on channel %d/%d, span %d\n", 
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
								pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_SWITCH_CONGESTION);
								pri->pvts[chanpos]->call = NULL;
							}
						}
					} else {
						if (option_verbose > 2)
							cw_verbose(VERBOSE_PREFIX_3 "Extension '%s' in context '%s' from '%s' does not exist.  Rejecting call on channel %d/%d, span %d\n",
								pri->pvts[chanpos]->exten, pri->pvts[chanpos]->context, pri->pvts[chanpos]->cid_num, pri->pvts[chanpos]->logicalspan, 
									pri->pvts[chanpos]->prioffset, pri->span);
						pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_UNALLOCATED);
						pri->pvts[chanpos]->call = NULL;
						pri->pvts[chanpos]->exten[0] = '\0';
					}
					if (crv)
						cw_mutex_unlock(&crv->lock);
					cw_mutex_unlock(&pri->pvts[chanpos]->lock);
				} else {
					if (e->ring.flexible)
						pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION);
					else
						pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_REQUESTED_CHAN_UNAVAIL);
				}
				break;
			case PRI_EVENT_RINGING:
				chanpos = pri_find_principle(pri, e->ringing.channel);
				if (chanpos < 0) {
					cw_log(CW_LOG_WARNING, "Ringing requested on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->ringing.channel), PRI_CHANNEL(e->ringing.channel), pri->span);
					chanpos = -1;
				}
				if (chanpos > -1) {
					chanpos = pri_fixup_principle(pri, chanpos, e->ringing.call);
					if (chanpos < 0) {
						cw_log(CW_LOG_WARNING, "Ringing requested on channel %d/%d not in use on span %d\n", 
							PRI_SPAN(e->ringing.channel), PRI_CHANNEL(e->ringing.channel), pri->span);
						chanpos = -1;
					} else {
						cw_mutex_lock(&pri->pvts[chanpos]->lock);
						if (cw_strlen_zero(pri->pvts[chanpos]->dop.dialstr)) {
							dahdi_enable_ec(pri->pvts[chanpos]);
							pri->pvts[chanpos]->subs[SUB_REAL].needringing = 1;
							pri->pvts[chanpos]->alerting = 1;
						} else
							cw_log(CW_LOG_DEBUG, "Deferring ringing notification because of extra digits to dial...\n");
#ifdef PRI_PROGRESS_MASK
						if (e->ringing.progressmask & PRI_PROG_INBAND_AVAILABLE) {
#else
						if (e->ringing.progress == 8) {
#endif
							/* Now we can do call progress detection */
							if(pri->pvts[chanpos]->dsp && pri->pvts[chanpos]->dsp_features) {
								/* RINGING detection isn't required because we got ALERTING signal */
								cw_dsp_set_features(pri->pvts[chanpos]->dsp, pri->pvts[chanpos]->dsp_features & ~DSP_PROGRESS_RINGING);
								pri->pvts[chanpos]->dsp_features = 0;
							}
						}
						if (!cw_strlen_zero(e->ringing.useruserinfo)) {
							pbx_builtin_setvar_helper(pri->pvts[chanpos]->owner, "USERUSERINFO", e->ringing.useruserinfo);
						}
						cw_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;
			case PRI_EVENT_PROGRESS:
				/* Get chan value if e->e is not PRI_EVNT_RINGING */
				chanpos = pri_find_principle(pri, e->proceeding.channel);
				if (chanpos > -1) {
#ifdef PRI_PROGRESS_MASK
					if ((!pri->pvts[chanpos]->progress) || (e->proceeding.progressmask & PRI_PROG_INBAND_AVAILABLE)) {
#else
					if ((!pri->pvts[chanpos]->progress) || (e->proceeding.progress == 8)) {
#endif
						struct cw_frame f = { CW_FRAME_CONTROL, CW_CONTROL_PROGRESS, };

						if (e->proceeding.cause > -1) {
							if (option_verbose > 2)
								cw_verbose(VERBOSE_PREFIX_3 "PROGRESS with cause code %d received\n", e->proceeding.cause);

							/* Work around broken, out of spec USER_BUSY cause in a progress message */
							if (e->proceeding.cause == CW_CAUSE_USER_BUSY) {
								if (pri->pvts[chanpos]->owner) {
									if (option_verbose > 2)
										cw_verbose(VERBOSE_PREFIX_3 "PROGRESS with 'user busy' received, signaling CW_CONTROL_BUSY instead of CW_CONTROL_PROGRESS\n");

									pri->pvts[chanpos]->owner->hangupcause = e->proceeding.cause;
									f.subclass = CW_CONTROL_BUSY;
								}
							}
						}
						
						cw_mutex_lock(&pri->pvts[chanpos]->lock);
						cw_log(CW_LOG_DEBUG, "Queuing frame from PRI_EVENT_PROGRESS on channel %d/%d span %d\n",
								pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset,pri->span);
						dahdi_queue_frame(pri->pvts[chanpos], &f, pri);
#ifdef PRI_PROGRESS_MASK
						if (e->proceeding.progressmask & PRI_PROG_INBAND_AVAILABLE) {
#else
						if (e->proceeding.progress == 8) {
#endif
							/* Now we can do call progress detection */
							if(pri->pvts[chanpos]->dsp && pri->pvts[chanpos]->dsp_features) {
								cw_dsp_set_features(pri->pvts[chanpos]->dsp, pri->pvts[chanpos]->dsp_features);
								pri->pvts[chanpos]->dsp_features = 0;
							}
						}
						pri->pvts[chanpos]->progress = 1;
						cw_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;
			case PRI_EVENT_PROCEEDING:
				chanpos = pri_find_principle(pri, e->proceeding.channel);
				if (chanpos > -1) {
					if (!pri->pvts[chanpos]->proceeding) {
						struct cw_frame f = { CW_FRAME_CONTROL, CW_CONTROL_PROCEEDING, };
						
						cw_mutex_lock(&pri->pvts[chanpos]->lock);
						cw_log(CW_LOG_DEBUG, "Queuing frame from PRI_EVENT_PROCEEDING on channel %d/%d span %d\n",
								pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset,pri->span);
						dahdi_queue_frame(pri->pvts[chanpos], &f, pri);
#ifdef PRI_PROGRESS_MASK
						if (e->proceeding.progressmask & PRI_PROG_INBAND_AVAILABLE) {
#else
						if (e->proceeding.progress == 8) {
#endif
							/* Now we can do call progress detection */
							if(pri->pvts[chanpos]->dsp && pri->pvts[chanpos]->dsp_features) {
								cw_dsp_set_features(pri->pvts[chanpos]->dsp, pri->pvts[chanpos]->dsp_features);
								pri->pvts[chanpos]->dsp_features = 0;
							}
							/* Bring voice path up */
							f.subclass = CW_CONTROL_PROGRESS;
							dahdi_queue_frame(pri->pvts[chanpos], &f, pri);
						}
						pri->pvts[chanpos]->proceeding = 1;
						cw_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;
			case PRI_EVENT_FACNAME:
				chanpos = pri_find_principle(pri, e->facname.channel);
				if (chanpos < 0) {
					cw_log(CW_LOG_WARNING, "Facility Name requested on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->facname.channel), PRI_CHANNEL(e->facname.channel), pri->span);
					chanpos = -1;
				}
				if (chanpos > -1) {
					chanpos = pri_fixup_principle(pri, chanpos, e->facname.call);
					if (chanpos < 0) {
						cw_log(CW_LOG_WARNING, "Facility Name requested on channel %d/%d not in use on span %d\n", 
							PRI_SPAN(e->facname.channel), PRI_CHANNEL(e->facname.channel), pri->span);
						chanpos = -1;
					} else {
						/* Re-use *69 field for PRI */
						cw_mutex_lock(&pri->pvts[chanpos]->lock);
						cw_copy_string(pri->pvts[chanpos]->lastcid_num, e->facname.callingnum, sizeof(pri->pvts[chanpos]->lastcid_num));
						cw_copy_string(pri->pvts[chanpos]->lastcid_name, e->facname.callingname, sizeof(pri->pvts[chanpos]->lastcid_name));
						pri->pvts[chanpos]->subs[SUB_REAL].needcallerid =1;
						dahdi_enable_ec(pri->pvts[chanpos]);
						cw_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;				
			case PRI_EVENT_ANSWER:
				chanpos = pri_find_principle(pri, e->answer.channel);
				if (chanpos < 0) {
					cw_log(CW_LOG_WARNING, "Answer on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->answer.channel), PRI_CHANNEL(e->answer.channel), pri->span);
					chanpos = -1;
				}
				if (chanpos > -1) {
					chanpos = pri_fixup_principle(pri, chanpos, e->answer.call);
					if (chanpos < 0) {
						cw_log(CW_LOG_WARNING, "Answer requested on channel %d/%d not in use on span %d\n", 
							PRI_SPAN(e->answer.channel), PRI_CHANNEL(e->answer.channel), pri->span);
						chanpos = -1;
					} else {
						cw_mutex_lock(&pri->pvts[chanpos]->lock);
						/* Now we can do call progress detection */

						/* We changed this so it turns on the DSP no matter what... progress or no progress.
						 * By this time, we need DTMF detection and other features that were previously disabled
						 * -- Matt F */
						if(pri->pvts[chanpos]->dsp && pri->pvts[chanpos]->dsp_features) {
							cw_dsp_set_features(pri->pvts[chanpos]->dsp, pri->pvts[chanpos]->dsp_features);
							pri->pvts[chanpos]->dsp_features = 0;
						}
						if (pri->pvts[chanpos]->realcall && (pri->pvts[chanpos]->realcall->sig == SIG_FXSKS)) {
							cw_log(CW_LOG_DEBUG, "Starting up GR-303 trunk now that we got CONNECT...\n");
							x = DAHDI_START;
							res = ioctl(pri->pvts[chanpos]->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
							if (res < 0) {
								if (errno != EINPROGRESS) {
									cw_log(CW_LOG_WARNING, "Unable to start channel: %s\n", strerror(errno));
								}
							}
						} else if (!cw_strlen_zero(pri->pvts[chanpos]->dop.dialstr)) {
							pri->pvts[chanpos]->dialing = 1;
							/* Send any "w" waited stuff */
							res = ioctl(pri->pvts[chanpos]->subs[SUB_REAL].dfd, DAHDI_DIAL, &pri->pvts[chanpos]->dop);
							if (res < 0) {
								cw_log(CW_LOG_WARNING, "Unable to initiate dialing on trunk channel %d\n", pri->pvts[chanpos]->channel);
								pri->pvts[chanpos]->dop.dialstr[0] = '\0';
							} else 
								cw_log(CW_LOG_DEBUG, "Sent deferred digit string: %s\n", pri->pvts[chanpos]->dop.dialstr);
							pri->pvts[chanpos]->dop.dialstr[0] = '\0';
						} else if (pri->pvts[chanpos]->confirmanswer) {
							cw_log(CW_LOG_DEBUG, "Waiting on answer confirmation on channel %d!\n", pri->pvts[chanpos]->channel);
						} else {
							pri->pvts[chanpos]->subs[SUB_REAL].needanswer =1;
							/* Enable echo cancellation if it's not on already */
							dahdi_enable_ec(pri->pvts[chanpos]);
						}
						if (!cw_strlen_zero(e->answer.useruserinfo)) {
							pbx_builtin_setvar_helper(pri->pvts[chanpos]->owner, "USERUSERINFO", e->answer.useruserinfo);
						}
						cw_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;				
			case PRI_EVENT_HANGUP:
				chanpos = pri_find_principle(pri, e->hangup.channel);
				if (chanpos < 0) {
					cw_log(CW_LOG_WARNING, "Hangup requested on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					chanpos = -1;
				}
				if (chanpos > -1) {
					chanpos = pri_fixup_principle(pri, chanpos, e->hangup.call);
					if (chanpos > -1) {
						cw_mutex_lock(&pri->pvts[chanpos]->lock);
						if (!pri->pvts[chanpos]->alreadyhungup) {
							/* we're calling here dahdi_hangup so once we get there we need to clear p->call after calling pri_hangup */
							pri->pvts[chanpos]->alreadyhungup = 1;
							if (pri->pvts[chanpos]->realcall) 
								pri_hangup_all(pri->pvts[chanpos]->realcall, pri);
							else if (pri->pvts[chanpos]->owner) {
								/* Queue a BUSY instead of a hangup if our cause is appropriate */
								pri->pvts[chanpos]->owner->hangupcause = e->hangup.cause;
								switch(e->hangup.cause) {
								case PRI_CAUSE_USER_BUSY:
									pri->pvts[chanpos]->subs[SUB_REAL].needbusy =1;
									break;
								case PRI_CAUSE_CALL_REJECTED:
								case PRI_CAUSE_NETWORK_OUT_OF_ORDER:
								case PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION:
								case PRI_CAUSE_SWITCH_CONGESTION:
								case PRI_CAUSE_DESTINATION_OUT_OF_ORDER:
								case PRI_CAUSE_NORMAL_TEMPORARY_FAILURE:
									pri->pvts[chanpos]->subs[SUB_REAL].needcongestion =1;
									break;
								default:
									pri->pvts[chanpos]->owner->_softhangup |= CW_SOFTHANGUP_DEV;
								}
							}
							if (option_verbose > 2) 
								cw_verbose(VERBOSE_PREFIX_3 "Channel %d/%d, span %d got hangup\n", 
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
						} else {
							pri_hangup(pri->pri, pri->pvts[chanpos]->call, e->hangup.cause);
							pri->pvts[chanpos]->call = NULL;
						}
						if (e->hangup.cause == PRI_CAUSE_REQUESTED_CHAN_UNAVAIL) {
							if (option_verbose > 2)
								cw_verbose(VERBOSE_PREFIX_3 "Forcing restart of channel %d/%d on span %d since channel reported in use\n", 
									PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
							pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[chanpos]));
							pri->pvts[chanpos]->resetting = 1;
						}
						if (e->hangup.aoc_units > -1)
							if (option_verbose > 2)
								cw_verbose(VERBOSE_PREFIX_3 "Channel %d/%d, span %d received AOC-E charging %d unit%s\n",
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span, (int)e->hangup.aoc_units, (e->hangup.aoc_units == 1) ? "" : "s");
						if (!cw_strlen_zero(e->hangup.useruserinfo)) {
							pbx_builtin_setvar_helper(pri->pvts[chanpos]->owner, "USERUSERINFO", e->hangup.useruserinfo);
						}
						cw_mutex_unlock(&pri->pvts[chanpos]->lock);
					} else {
						cw_log(CW_LOG_WARNING, "Hangup on bad channel %d/%d on span %d\n", 
							PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					}
				} 
				break;
#ifndef PRI_EVENT_HANGUP_REQ
#error please update libpri
#endif
			case PRI_EVENT_HANGUP_REQ:
				chanpos = pri_find_principle(pri, e->hangup.channel);
				if (chanpos < 0) {
					cw_log(CW_LOG_WARNING, "Hangup REQ requested on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					chanpos = -1;
				}
				if (chanpos > -1) {
					chanpos = pri_fixup_principle(pri, chanpos, e->hangup.call);
					if (chanpos > -1) {
						cw_mutex_lock(&pri->pvts[chanpos]->lock);
						if (pri->pvts[chanpos]->realcall) 
							pri_hangup_all(pri->pvts[chanpos]->realcall, pri);
						else if (pri->pvts[chanpos]->owner) {
							pri->pvts[chanpos]->owner->hangupcause = e->hangup.cause;
							switch(e->hangup.cause) {
							case PRI_CAUSE_USER_BUSY:
								pri->pvts[chanpos]->subs[SUB_REAL].needbusy =1;
								break;
							case PRI_CAUSE_CALL_REJECTED:
							case PRI_CAUSE_NETWORK_OUT_OF_ORDER:
							case PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION:
							case PRI_CAUSE_SWITCH_CONGESTION:
							case PRI_CAUSE_DESTINATION_OUT_OF_ORDER:
							case PRI_CAUSE_NORMAL_TEMPORARY_FAILURE:
								pri->pvts[chanpos]->subs[SUB_REAL].needcongestion =1;
								break;
							default:
								pri->pvts[chanpos]->owner->_softhangup |= CW_SOFTHANGUP_DEV;
							}
							if (option_verbose > 2) 
								cw_verbose(VERBOSE_PREFIX_3 "Channel %d/%d, span %d got hangup request\n", PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
							if (e->hangup.aoc_units > -1)
								if (option_verbose > 2)
									cw_verbose(VERBOSE_PREFIX_3 "Channel %d/%d, span %d received AOC-E charging %d unit%s\n",
										pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span, (int)e->hangup.aoc_units, (e->hangup.aoc_units == 1) ? "" : "s");
						} else {
							pri_hangup(pri->pri, pri->pvts[chanpos]->call, e->hangup.cause);
							pri->pvts[chanpos]->call = NULL;
						}
						if (e->hangup.cause == PRI_CAUSE_REQUESTED_CHAN_UNAVAIL) {
							if (option_verbose > 2)
								cw_verbose(VERBOSE_PREFIX_3 "Forcing restart of channel %d/%d span %d since channel reported in use\n", 
									PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
							pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[chanpos]));
							pri->pvts[chanpos]->resetting = 1;
						}
						if (!cw_strlen_zero(e->hangup.useruserinfo)) {
							pbx_builtin_setvar_helper(pri->pvts[chanpos]->owner, "USERUSERINFO", e->hangup.useruserinfo);
						}
						cw_mutex_unlock(&pri->pvts[chanpos]->lock);
					} else {
						cw_log(CW_LOG_WARNING, "Hangup REQ on bad channel %d/%d on span %d\n", PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					}
				} 
				break;
			case PRI_EVENT_HANGUP_ACK:
				chanpos = pri_find_principle(pri, e->hangup.channel);
				if (chanpos < 0) {
					cw_log(CW_LOG_WARNING, "Hangup ACK requested on unconfigured channel number %d/%d span %d\n", 
						PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					chanpos = -1;
				}
				if (chanpos > -1) {
					chanpos = pri_fixup_principle(pri, chanpos, e->hangup.call);
					if (chanpos > -1) {
						cw_mutex_lock(&pri->pvts[chanpos]->lock);
						pri->pvts[chanpos]->call = NULL;
						pri->pvts[chanpos]->resetting = 0;
						if (pri->pvts[chanpos]->owner) {
							if (option_verbose > 2) 
								cw_verbose(VERBOSE_PREFIX_3 "Channel %d/%d, span %d got hangup ACK\n", PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
						}
						if (!cw_strlen_zero(e->hangup.useruserinfo)) {
							pbx_builtin_setvar_helper(pri->pvts[chanpos]->owner, "USERUSERINFO", e->hangup.useruserinfo);
						}
						cw_mutex_unlock(&pri->pvts[chanpos]->lock);
					}
				}
				break;
			case PRI_EVENT_CONFIG_ERR:
				cw_log(CW_LOG_WARNING, "PRI Error: %s\n", e->err.err);
				break;
			case PRI_EVENT_RESTART_ACK:
				chanpos = pri_find_principle(pri, e->restartack.channel);
				if (chanpos < 0) {
					/* Sometime switches (e.g. I421 / British Telecom) don't give us the
					   channel number, so we have to figure it out...  This must be why
					   everybody resets exactly a channel at a time. */
					for (x=0;x<pri->numchans;x++) {
						if (pri->pvts[x] && pri->pvts[x]->resetting) {
							chanpos = x;
							cw_mutex_lock(&pri->pvts[chanpos]->lock);
							cw_log(CW_LOG_DEBUG, "Assuming restart ack is really for channel %d/%d span %d\n", pri->pvts[chanpos]->logicalspan, 
									pri->pvts[chanpos]->prioffset, pri->span);
							if (pri->pvts[chanpos]->realcall) 
								pri_hangup_all(pri->pvts[chanpos]->realcall, pri);
							else if (pri->pvts[chanpos]->owner) {
								cw_log(CW_LOG_WARNING, "Got restart ack on channel %d/%d with owner on span %d\n", pri->pvts[chanpos]->logicalspan, 
									pri->pvts[chanpos]->prioffset, pri->span);
								pri->pvts[chanpos]->owner->_softhangup |= CW_SOFTHANGUP_DEV;
							}
							pri->pvts[chanpos]->resetting = 0;
							if (option_verbose > 2)
								cw_verbose(VERBOSE_PREFIX_3 "B-channel %d/%d successfully restarted on span %d\n", pri->pvts[chanpos]->logicalspan, 
									pri->pvts[chanpos]->prioffset, pri->span);
							cw_mutex_unlock(&pri->pvts[chanpos]->lock);
							if (pri->resetting)
								pri_check_restart(pri);
							break;
						}
					}
					if (chanpos < 0) {
						cw_log(CW_LOG_WARNING, "Restart ACK requested on strange channel %d/%d span %d\n", 
							PRI_SPAN(e->restartack.channel), PRI_CHANNEL(e->restartack.channel), pri->span);
					}
					chanpos = -1;
				}
				if (chanpos > -1) {
					if (pri->pvts[chanpos]) {
						cw_mutex_lock(&pri->pvts[chanpos]->lock);
						if (pri->pvts[chanpos]->realcall) 
							pri_hangup_all(pri->pvts[chanpos]->realcall, pri);
						else if (pri->pvts[chanpos]->owner) {
							cw_log(CW_LOG_WARNING, "Got restart ack on channel %d/%d span %d with owner\n",
								PRI_SPAN(e->restartack.channel), PRI_CHANNEL(e->restartack.channel), pri->span);
							pri->pvts[chanpos]->owner->_softhangup |= CW_SOFTHANGUP_DEV;
						}
						pri->pvts[chanpos]->resetting = 0;
						if (option_verbose > 2)
							cw_verbose(VERBOSE_PREFIX_3 "B-channel %d/%d successfully restarted on span %d\n", pri->pvts[chanpos]->logicalspan, 
									pri->pvts[chanpos]->prioffset, pri->span);
						cw_mutex_unlock(&pri->pvts[chanpos]->lock);
						if (pri->resetting)
							pri_check_restart(pri);
					}
				}
				break;
			case PRI_EVENT_SETUP_ACK:
				chanpos = pri_find_principle(pri, e->setup_ack.channel);
				if (chanpos < 0) {
					cw_log(CW_LOG_WARNING, "Received SETUP_ACKNOWLEDGE on unconfigured channel %d/%d span %d\n", 
						PRI_SPAN(e->setup_ack.channel), PRI_CHANNEL(e->setup_ack.channel), pri->span);
				} else {
					cw_mutex_lock(&pri->pvts[chanpos]->lock);
					pri->pvts[chanpos]->setup_ack = 1;
					/* Send any queued digits */
					for (x=0;x<strlen(pri->pvts[chanpos]->dialdest);x++) {
						cw_log(CW_LOG_DEBUG, "Sending pending digit '%c'\n", pri->pvts[chanpos]->dialdest[x]);
						pri_information(pri->pri, pri->pvts[chanpos]->call, 
							pri->pvts[chanpos]->dialdest[x]);
					}
					cw_mutex_unlock(&pri->pvts[chanpos]->lock);
				}
				break;
			case PRI_EVENT_NOTIFY:
				chanpos = pri_find_principle(pri, e->notify.channel);
				if (chanpos < 0) {
					cw_log(CW_LOG_WARNING, "Received NOTIFY on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->notify.channel), PRI_CHANNEL(e->notify.channel), pri->span);
				} else {
					struct cw_frame f = { CW_FRAME_CONTROL, };
					cw_mutex_lock(&pri->pvts[chanpos]->lock);
					switch(e->notify.info) {
					case PRI_NOTIFY_REMOTE_HOLD:
						f.subclass = CW_CONTROL_HOLD;
						dahdi_queue_frame(pri->pvts[chanpos], &f, pri);
						break;
					case PRI_NOTIFY_REMOTE_RETRIEVAL:
						f.subclass = CW_CONTROL_UNHOLD;
						dahdi_queue_frame(pri->pvts[chanpos], &f, pri);
						break;
					}
					cw_mutex_unlock(&pri->pvts[chanpos]->lock);
				}
				break;
			default:
				cw_log(CW_LOG_DEBUG, "Event: %d\n", e->e);
			}
		}	
		cw_mutex_unlock(&pri->lock);
	}
	/* Never reached */
	return NULL;
}

static int start_pri(struct dahdi_pri *pri)
{
	int res, x;
	struct dahdi_params p;
	struct dahdi_bufferinfo bi;
	struct dahdi_spaninfo si;
	int i;
	
	for (i=0;i<NUM_DCHANS;i++) {
		if (!pri->dchannels[i])
			break;
		pri->fds[i] = open("/dev/dahdi/channel", O_RDWR);
		x = pri->dchannels[i];
		if ((pri->fds[i] < 0) || (ioctl(pri->fds[i],DAHDI_SPECIFY,&x) == -1)) {
			cw_log(CW_LOG_ERROR, "Unable to open D-channel %d (%s)\n", x, strerror(errno));
			return -1;
		}
		memset(&p, 0, sizeof(p));
		res = ioctl(pri->fds[i], DAHDI_GET_PARAMS, &p);
		if (res) {
			dahdi_close(pri->fds[i]);
			pri->fds[i] = -1;
			cw_log(CW_LOG_ERROR, "Unable to get parameters for D-channel %d (%s)\n", x, strerror(errno));
			return -1;
		}
		if ((p.sigtype != DAHDI_SIG_HDLCFCS) && (p.sigtype != DAHDI_SIG_HARDHDLC)) {
			dahdi_close(pri->fds[i]);
			pri->fds[i] = -1;
			cw_log(CW_LOG_ERROR, "D-channel %d is not in HDLC/FCS mode.  See /etc/dahdi/system.conf\n", x);
			return -1;
		}
		memset(&si, 0, sizeof(si));
		res = ioctl(pri->fds[i], DAHDI_SPANSTAT, &si);
		if (res) {
			dahdi_close(pri->fds[i]);
			pri->fds[i] = -1;
			cw_log(CW_LOG_ERROR, "Unable to get span state for D-channel %d (%s)\n", x, strerror(errno));
		}
		if (!si.alarms)
			pri->dchanavail[i] |= DCHAN_NOTINALARM;
		else
			pri->dchanavail[i] &= ~DCHAN_NOTINALARM;
		bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.numbufs = 32;
		bi.bufsize = 1024;
		if (ioctl(pri->fds[i], DAHDI_SET_BUFINFO, &bi)) {
			cw_log(CW_LOG_ERROR, "Unable to set appropriate buffering on channel %d\n", x);
			dahdi_close(pri->fds[i]);
			pri->fds[i] = -1;
			return -1;
		}
		pri->dchans[i] = pri_new(pri->fds[i], pri->nodetype, pri->switchtype);
		/* Force overlap dial if we're doing GR-303! */
		if (pri->switchtype == PRI_SWITCH_GR303_TMC)
			pri->overlapdial = 1;
		pri_set_overlapdial(pri->dchans[i],pri->overlapdial);
		/* Enslave to master if appropriate */
		if (i)
			pri_enslave(pri->dchans[0], pri->dchans[i]);
		if (!pri->dchans[i]) {
			dahdi_close(pri->fds[i]);
			pri->fds[i] = -1;
			cw_log(CW_LOG_ERROR, "Unable to create PRI structure\n");
			return -1;
		}
		pri_set_debug(pri->dchans[i], DEFAULT_PRI_DEBUG);
		pri_set_nsf(pri->dchans[i], pri->nsf);
#ifdef PRI_GETSET_TIMERS
		for (x = 0; x < PRI_MAX_TIMERS; x++) {
			if (pritimers[x] != 0)
				pri_set_timer(pri->dchans[i], x, pritimers[x]);
		}
#endif
	}
	/* Assume primary is the one we use */
	pri->pri = pri->dchans[0];
	pri->resetpos = -1;
	if (cw_pthread_create(&pri->master, &global_attr_default, pri_dchannel, pri)) {
		for (i=0;i<NUM_DCHANS;i++) {
			if (!pri->dchannels[i])
				break;
			dahdi_close(pri->fds[i]);
			pri->fds[i] = -1;
		}
		cw_log(CW_LOG_ERROR, "Unable to spawn D-channel: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static void complete_span_helper(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len, int rpos)
{
	int span = 1;

	if (lastarg == rpos) {
		for (span = 1; span <= NUM_SPANS; span++) {
			if (pris[span-1].pri)
				cw_dynstr_printf(ds_p, "%d\n", span);
		}
	}
}

static void complete_span_4(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	complete_span_helper(ds_p, argv, lastarg, lastarg_len, 3);
}

static void complete_span_5(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	complete_span_helper(ds_p, argv, lastarg, lastarg_len, 4);
}

static int handle_pri_set_debug_file(struct cw_dynstr *ds_p, int argc, char **argv)
{
	int myfd;

	if (!strncasecmp(argv[1], "set", 3)) {
		if (argc < 5) 
			return RESULT_SHOWUSAGE;

		if (cw_strlen_zero(argv[4]))
			return RESULT_SHOWUSAGE;

		myfd = open(argv[4], O_CREAT | O_WRONLY, 0660);
		if (myfd < 0) {
			cw_dynstr_printf(ds_p, "Unable to open '%s' for writing\n", argv[4]);
			return RESULT_SUCCESS;
		}

		cw_mutex_lock(&pridebugfdlock);

		if (pridebugfd >= 0)
			close(pridebugfd);

		pridebugfd = myfd;
		cw_copy_string(pridebugfilename,argv[4],sizeof(pridebugfilename));
		
		cw_mutex_unlock(&pridebugfdlock);

		cw_dynstr_printf(ds_p, "PRI debug output will be sent to '%s'\n", argv[4]);
	} else {
		/* Assume it is unset */
		cw_mutex_lock(&pridebugfdlock);
		close(pridebugfd);
		pridebugfd = -1;
		cw_dynstr_printf(ds_p, "PRI debug output to file disabled\n");
		cw_mutex_unlock(&pridebugfdlock);
	}

	return RESULT_SUCCESS;
}

static int handle_pri_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int span;
	int x;
	if (argc < 4) {
		return RESULT_SHOWUSAGE;
	}
	span = atoi(argv[3]);
	if ((span < 1) || (span > NUM_SPANS)) {
		cw_dynstr_printf(ds_p, "Invalid span %s.  Should be a number %d to %d\n", argv[3], 1, NUM_SPANS);
		return RESULT_SUCCESS;
	}
	if (!pris[span-1].pri) {
		cw_dynstr_printf(ds_p, "No PRI running on span %d\n", span);
		return RESULT_SUCCESS;
	}
	for (x=0;x<NUM_DCHANS;x++) {
		if (pris[span-1].dchans[x])
			pri_set_debug(pris[span-1].dchans[x], PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q931_STATE);
	}
	cw_dynstr_printf(ds_p, "Enabled debugging on span %d\n", span);
	return RESULT_SUCCESS;
}



static int handle_pri_no_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int span;
	int x;
	if (argc < 5)
		return RESULT_SHOWUSAGE;
	span = atoi(argv[4]);
	if ((span < 1) || (span > NUM_SPANS)) {
		cw_dynstr_printf(ds_p, "Invalid span %s.  Should be a number %d to %d\n", argv[4], 1, NUM_SPANS);
		return RESULT_SUCCESS;
	}
	if (!pris[span-1].pri) {
		cw_dynstr_printf(ds_p, "No PRI running on span %d\n", span);
		return RESULT_SUCCESS;
	}
	for (x=0;x<NUM_DCHANS;x++) {
		if (pris[span-1].dchans[x])
			pri_set_debug(pris[span-1].dchans[x], 0);
	}
	cw_dynstr_printf(ds_p, "Disabled debugging on span %d\n", span);
	return RESULT_SUCCESS;
}

static int handle_pri_really_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int span;
	int x;
	if (argc < 5)
		return RESULT_SHOWUSAGE;
	span = atoi(argv[4]);
	if ((span < 1) || (span > NUM_SPANS)) {
		cw_dynstr_printf(ds_p, "Invalid span %s.  Should be a number %d to %d\n", argv[4], 1, NUM_SPANS);
		return RESULT_SUCCESS;
	}
	if (!pris[span-1].pri) {
		cw_dynstr_printf(ds_p, "No PRI running on span %d\n", span);
		return RESULT_SUCCESS;
	}
	for (x=0;x<NUM_DCHANS;x++) {
		if (pris[span-1].dchans[x])
			pri_set_debug(pris[span-1].dchans[x], (PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q921_DUMP | PRI_DEBUG_Q921_RAW | PRI_DEBUG_Q921_STATE));
	}
	cw_dynstr_printf(ds_p, "Enabled EXTENSIVE debugging on span %d\n", span);
	return RESULT_SUCCESS;
}

static void build_status(char *s, size_t len, int status, int active)
{
	if (!s || len < 1) {
		return;
	}
	s[0] = '\0';
	if (status & DCHAN_PROVISIONED)
		strncat(s, "Provisioned, ", len - strlen(s) - 1);
	if (!(status & DCHAN_NOTINALARM))
		strncat(s, "In Alarm, ", len - strlen(s) - 1);
	if (status & DCHAN_UP)
		strncat(s, "Up", len - strlen(s) - 1);
	else
		strncat(s, "Down", len - strlen(s) - 1);
	if (active)
		strncat(s, ", Active", len - strlen(s) - 1);
	else
		strncat(s, ", Standby", len - strlen(s) - 1);
	s[len - 1] = '\0';
}

static int handle_pri_show_span(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int span;
	int x;
	char status[256];
	if (argc < 4)
		return RESULT_SHOWUSAGE;
	span = atoi(argv[3]);
	if ((span < 1) || (span > NUM_SPANS)) {
		cw_dynstr_printf(ds_p, "Invalid span %s.  Should be a number %d to %d\n", argv[4], 1, NUM_SPANS);
		return RESULT_SUCCESS;
	}
	if (!pris[span-1].pri) {
		cw_dynstr_printf(ds_p, "No PRI running on span %d\n", span);
		return RESULT_SUCCESS;
	}
	for(x=0;x<NUM_DCHANS;x++) {
		if (pris[span-1].dchannels[x]) {
#ifdef PRI_DUMP_INFO_STR
			char *info_str = NULL;
#endif
			cw_dynstr_printf(ds_p, "%s D-channel: %d\n", pri_order(x), pris[span-1].dchannels[x]);
			build_status(status, sizeof(status), pris[span-1].dchanavail[x], pris[span-1].dchans[x] == pris[span-1].pri);
			cw_dynstr_printf(ds_p, "Status: %s\n", status);
#ifdef PRI_DUMP_INFO_STR
			info_str = pri_dump_info_str(pris[span-1].pri);
			if (info_str) {
				cw_dynstr_printf(ds_p, "%s", info_str);
				free(info_str);
			}
#else
			pri_dump_info(pris[span-1].pri);
#endif
			cw_dynstr_printf(ds_p, "\n");
		}
	}
	return RESULT_SUCCESS;
}

static int handle_pri_show_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int x;
	int span;
	int count=0;
	int debug=0;

	for(span=0;span<NUM_SPANS;span++) {
	        if (pris[span].pri) {
			for(x=0;x<NUM_DCHANS;x++) {
				debug=0;
	        		if (pris[span].dchans[x]) {
	        			debug = pri_get_debug(pris[span].dchans[x]);
					cw_dynstr_printf(ds_p, "Span %d: Debug: %s\tIntense: %s\n", span+1, (debug&PRI_DEBUG_Q931_STATE)? "Yes" : "No" ,(debug&PRI_DEBUG_Q921_RAW)? "Yes" : "No" );
					count++;
				}
			}
		}

	}
	cw_mutex_lock(&pridebugfdlock);
	if (pridebugfd >= 0) 
		cw_dynstr_printf(ds_p, "Logging PRI debug to file %s\n", pridebugfilename);
	cw_mutex_unlock(&pridebugfdlock);
	    
	if (!count) 
		cw_dynstr_printf(ds_p, "No debug set or no PRI running\n");
	return RESULT_SUCCESS;
}

static const char pri_debug_help[] =
	"Usage: pri debug span <span>\n"
	"       Enables debugging on a given PRI span\n";
	
static const char pri_no_debug_help[] =
	"Usage: pri no debug span <span>\n"
	"       Disables debugging on a given PRI span\n";

static const char pri_really_debug_help[] =
	"Usage: pri intensive debug span <span>\n"
	"       Enables debugging down to the Q.921 level\n";

static const char pri_show_span_help[] =
	"Usage: pri show span <span>\n"
	"       Displays PRI Information\n";

static struct cw_clicmd dahdi_pri_cli[] = {
	{
		.cmda = { "pri", "debug", "span", NULL },
		.handler = handle_pri_debug,
		.summary = "Enables PRI debugging on a span",
		.usage = pri_debug_help,
		.generator = complete_span_4,
	},
	{
		.cmda = { "pri", "no", "debug", "span", NULL },
		.handler = handle_pri_no_debug,
		.summary = "Disables PRI debugging on a span",
		.usage = pri_no_debug_help,
		.generator = complete_span_5,
	},
	{
		.cmda = { "pri", "intense", "debug", "span", NULL },
		.handler = handle_pri_really_debug,
		.summary = "Enables REALLY INTENSE PRI debugging",
		.usage = pri_really_debug_help,
		.generator = complete_span_5,
	},
	{
		.cmda = { "pri", "show", "span", NULL },
		.handler = handle_pri_show_span,
		.summary = "Displays PRI Information",
		.usage = pri_show_span_help,
		.generator = complete_span_4,
	},
	{
		.cmda = { "pri", "show", "debug", NULL },
		.handler = handle_pri_show_debug,
		.summary = "Displays current PRI debug settings" },
	{
		.cmda = { "pri", "set", "debug", "file", NULL },
		.handler = handle_pri_set_debug_file,
		.summary = "Sends PRI debug output to the specified file",
	},
	{
		.cmda = { "pri", "unset", "debug", "file", NULL },
		.handler = handle_pri_set_debug_file,
		.summary = "Ends PRI debug output to file",
	},
};

#endif /* ZAPATA_PRI */

static int dahdi_destroy_channel(struct cw_dynstr *ds_p, int argc, char **argv)
{
	int channel = 0;
	struct dahdi_pvt *tmp = NULL;
	struct dahdi_pvt *prev = NULL;

	CW_UNUSED(ds_p);

	if (argc != 4) {
		return RESULT_SHOWUSAGE;
	}
	channel = atoi(argv[3]);

	tmp = iflist;
	while (tmp) {
		if (tmp->channel == channel) {
			destroy_channel(prev, tmp, 1);
			return RESULT_SUCCESS;
		}
		prev = tmp;
		tmp = tmp->next;
	}
	return RESULT_FAILURE;
}

static int dahdi_show_channels(struct cw_dynstr *ds_p, int argc, char **argv)
{
#define FORMAT "%7s %-16.16s %-15.15s %-10.10s %-20.20s\n"
#define FORMAT2 "%7s %-16.16s %-15.15s %-10.10s %-20.20s\n"
	char tmps[20] = "";
	struct dahdi_pvt *tmp = NULL;
	cw_mutex_t *lock;
	struct dahdi_pvt *start;
#ifdef ZAPATA_PRI
	struct dahdi_pri *pri=NULL;
	int trunkgroup;
	int x;
#else
	CW_UNUSED(argv);
#endif

	lock = &iflock;
	start = iflist;

#ifdef ZAPATA_PRI
	if (argc == 4) {
		if ((trunkgroup = atoi(argv[3])) < 1)
			return RESULT_SHOWUSAGE;
		for (x=0;x<NUM_SPANS;x++) {
			if (pris[x].trunkgroup == trunkgroup) {
				pri = pris + x;
				break;
			}
		}
		if (pri) {
			start = pri->crvs;
			lock = &pri->lock;
		} else {
			cw_dynstr_printf(ds_p, "No such trunk group %d\n", trunkgroup);
			return RESULT_FAILURE;
		}
	} else
#endif
	if (argc != 3)
		return RESULT_SHOWUSAGE;

	cw_mutex_lock(lock);
#ifdef ZAPATA_PRI
	cw_dynstr_printf(ds_p, FORMAT2, pri ? "CRV" : "Chan", "Extension", "Context", "Language", "MusicOnHold");
#else
	cw_dynstr_printf(ds_p, FORMAT2, "Chan", "Extension", "Context", "Language", "MusicOnHold");
#endif	
	
	tmp = start;
	while (tmp) {
		if (tmp->channel > 0) {
			snprintf(tmps, sizeof(tmps), "%d", tmp->channel);
		} else
			cw_copy_string(tmps, "pseudo", sizeof(tmps));
		cw_dynstr_printf(ds_p, FORMAT, tmps, tmp->exten, tmp->context, tmp->language, tmp->musicclass);
		tmp = tmp->next;
	}
	cw_mutex_unlock(lock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int dahdi_show_channel(struct cw_dynstr *ds_p, int argc, char **argv)
{
	int channel;
	struct dahdi_pvt *tmp = NULL;
	struct dahdi_confinfo ci;
	struct dahdi_params ps;
	int x;
	cw_mutex_t *lock;
	struct dahdi_pvt *start;
#ifdef ZAPATA_PRI
	char *c;
	int trunkgroup;
	struct dahdi_pri *pri=NULL;
#endif

	lock = &iflock;
	start = iflist;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
#ifdef ZAPATA_PRI
	if ((c = strchr(argv[3], ':'))) {
		if (sscanf(argv[3], "%d:%d", &trunkgroup, &channel) != 2)
			return RESULT_SHOWUSAGE;
		if ((trunkgroup < 1) || (channel < 1))
			return RESULT_SHOWUSAGE;
		for (x=0;x<NUM_SPANS;x++) {
			if (pris[x].trunkgroup == trunkgroup) {
				pri = pris + x;
				break;
			}
		}
		if (pri) {
			start = pri->crvs;
			lock = &pri->lock;
		} else {
			cw_dynstr_printf(ds_p, "No such trunk group %d\n", trunkgroup);
			return RESULT_FAILURE;
		}
	} else
#endif
		channel = atoi(argv[3]);

	cw_mutex_lock(lock);
	tmp = start;
	while (tmp) {
		if (tmp->channel == channel) {
#ifdef ZAPATA_PRI
			if (pri) 
				cw_dynstr_printf(ds_p, "Trunk/CRV: %d/%d\n", trunkgroup, tmp->channel);
			else
#endif			
			cw_dynstr_printf(ds_p, "Channel: %d\n", tmp->channel);

			cw_dynstr_tprintf(ds_p, 26,
				cw_fmtval("File Descriptor: %d\n", tmp->subs[SUB_REAL].dfd),
				cw_fmtval("Span: %d\n", tmp->span),
				cw_fmtval("Extension: %s\n", tmp->exten),
				cw_fmtval("Dialing: %s\n", (tmp->dialing ? "yes" : "no")),
				cw_fmtval("Context: %s\n", tmp->context),
				cw_fmtval("Caller ID: %s\n", tmp->cid_num),
				cw_fmtval("Calling TON: %d\n", tmp->cid_ton),
				cw_fmtval("Caller ID name: %s\n", tmp->cid_name),
				cw_fmtval("Destroy: %x\n", tmp->destroy),
				cw_fmtval("InAlarm: %x\n", tmp->inalarm),
				cw_fmtval("Signalling Type: %s\n", sig2str(tmp->sig)),
				cw_fmtval("Radio: %d\n", tmp->radio),
				cw_fmtval("Owner: %s\n", (tmp->owner ? tmp->owner->name : "<None>")),
				cw_fmtval("Real: %s%s%s\n",
					(tmp->subs[SUB_REAL].owner ? tmp->subs[SUB_REAL].owner->name : "<None>"),
					(tmp->subs[SUB_REAL].inthreeway ? " (Confed)" : ""),
					(tmp->subs[SUB_REAL].linear ? " (Linear)" : "")),
				cw_fmtval("Callwait: %s%s%s\n",
					(tmp->subs[SUB_CALLWAIT].owner ? tmp->subs[SUB_CALLWAIT].owner->name : "<None>"),
					(tmp->subs[SUB_CALLWAIT].inthreeway ? " (Confed)" : ""),
					(tmp->subs[SUB_CALLWAIT].linear ? " (Linear)" : "")),
				cw_fmtval("Threeway: %s%s%s\n",
					(tmp->subs[SUB_THREEWAY].owner ? tmp->subs[SUB_THREEWAY].owner->name : "<None>"),
					(tmp->subs[SUB_THREEWAY].inthreeway ? " (Confed)" : ""),
					(tmp->subs[SUB_THREEWAY].linear ? " (Linear)" : "")),
				cw_fmtval("Confno: %d\n", tmp->confno),
				cw_fmtval("Propagated Conference: %d\n", tmp->propconfno),
				cw_fmtval("Real in conference: %d\n", tmp->inconference),
				cw_fmtval("DSP: %s\n", (tmp->dsp ? "yes" : "no")),
				cw_fmtval("Relax DTMF: %s\n", (tmp->dtmfrelax ? "yes" : "no")),
				cw_fmtval("Dialing/CallwaitCAS: %x/%d\n", tmp->dialing, tmp->callwaitcas),
				cw_fmtval("Default law: %s\n", (tmp->law == DAHDI_LAW_MULAW ? "ulaw" : (tmp->law == DAHDI_LAW_ALAW ? "alaw" : "unknown"))),
				cw_fmtval("Fax Handled: %s\n", (tmp->faxhandled ? "yes" : "no")),
				cw_fmtval("Pulse phone: %s\n", (tmp->pulsedial ? "yes" : "no")),
				cw_fmtval("Echo Cancellation: %d taps%s, currently %s\n", tmp->echocancel, (tmp->echocanbridged ? "" : " unless TDM bridged"), (tmp->echocanon ? "ON" : "OFF"))
			);

			if (tmp->master)
				cw_dynstr_printf(ds_p, "Master Channel: %d\n", tmp->master->channel);
			for (x=0;x<MAX_SLAVES;x++) {
				if (tmp->slaves[x])
					cw_dynstr_printf(ds_p, "Slave Channel: %d\n", tmp->slaves[x]->channel);
			}
#ifdef ZAPATA_PRI
			if (tmp->pri) {
				cw_dynstr_printf(ds_p, "PRI Flags: ");
				if (tmp->resetting)
					cw_dynstr_printf(ds_p, "Resetting ");
				if (tmp->call)
					cw_dynstr_printf(ds_p, "Call ");
				if (tmp->bearer)
					cw_dynstr_printf(ds_p, "Bearer ");
				cw_dynstr_printf(ds_p, "\n");
				if (tmp->logicalspan) 
					cw_dynstr_printf(ds_p, "PRI Logical Span: %d\n", tmp->logicalspan);
				else
					cw_dynstr_printf(ds_p, "PRI Logical Span: Implicit\n");
			}
				
#endif
			if (tmp->subs[SUB_REAL].dfd > -1) {
				memset(&ci, 0, sizeof(ci));
				if (!ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GETCONF, &ci)) {
					cw_dynstr_printf(ds_p, "Actual Confinfo: Num/%d, Mode/0x%04x\n", ci.confno, ci.confmode);
				}
#ifdef DAHDI_GETCONFMUTE
				if (!ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GETCONFMUTE, &x)) {
					cw_dynstr_printf(ds_p, "Actual Confmute: %s\n", x ? "Yes" : "No");
				}
#endif
				memset(&ps, 0, sizeof(ps));
				ps.channo = tmp->channel;
				if (ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &ps) < 0) {
					cw_log(CW_LOG_WARNING, "Failed to get parameters on channel %d\n", tmp->channel);
				} else {
					cw_dynstr_printf(ds_p, "Hookstate (FXS only): %s\n", ps.rxisoffhook ? "Offhook" : "Onhook");
				}
			}
			cw_mutex_unlock(lock);
			return RESULT_SUCCESS;
		}
		tmp = tmp->next;
	}
	
	cw_dynstr_printf(ds_p, "Unable to find given channel %d\n", channel);
	cw_mutex_unlock(lock);
	return RESULT_FAILURE;
}

static char dahdi_show_cadences_help[] =
"Usage: dahdi show cadences\n"
"       Shows all cadences currently defined\n";

static int handle_dahdi_show_cadences(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int i, j;

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	for (i = 0; i < num_cadence; i++) {
		char output[1024];
		char tmp[16];

		snprintf(output, sizeof(output), "r%d: ", i + 1);

		for (j=0;j<16;j++) {

			if (cadences[i].ringcadence[j] == 0)
				break;

			snprintf(tmp, sizeof(tmp), "%d", cadences[i].ringcadence[j]);

			if (j != 0)
				strncat(output, ",", sizeof(output) - strlen(output) - 1);

			strncat(output, tmp, sizeof(output) - strlen(output) - 1);
		}
		cw_dynstr_printf(ds_p,"%s\n",output);
	}
	return 0;
}

/* Based on irqmiss.c */
static int dahdi_show_status(struct cw_dynstr *ds_p, int argc, char *argv[]) {
	#define FORMAT "%-40.40s %-10.10s %-10d %-10d %-10d\n"
	#define FORMAT2 "%-40.40s %-10.10s %-10.10s %-10.10s %-10.10s\n"

	char alarmstr[50];
	struct dahdi_spaninfo s;
	int span;
	int res;
	int ctl;

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	ctl = open("/dev/dahdi/ctl", O_RDWR);
	if (ctl < 0) {
		cw_log(CW_LOG_WARNING, "Unable to open /dev/dahdi/ctl: %s\n", strerror(errno));
		cw_dynstr_printf(ds_p, "No DAHDI interface found.\n");
		return RESULT_FAILURE;
	}
	cw_dynstr_printf(ds_p,FORMAT2, "Description", "Alarms","IRQ","bpviol","CRC4");

	for (span=1;span < DAHDI_MAX_SPANS;++span) {
		s.spanno = span;
		res = ioctl(ctl, DAHDI_SPANSTAT, &s);
		if (res) {
			continue;
		}
		alarmstr[0] = '\0';
		if (s.alarms > 0) {
			if (s.alarms & DAHDI_ALARM_BLUE)
				strcat(alarmstr,"BLU/");
			if (s.alarms & DAHDI_ALARM_YELLOW)
				strcat(alarmstr, "YEL/");
			if (s.alarms & DAHDI_ALARM_RED)
				strcat(alarmstr, "RED/");
			if (s.alarms & DAHDI_ALARM_LOOPBACK)
				strcat(alarmstr,"LB/");
			if (s.alarms & DAHDI_ALARM_RECOVER)
				strcat(alarmstr,"REC/");
			if (s.alarms & DAHDI_ALARM_NOTOPEN)
				strcat(alarmstr, "NOP/");
			if (!strlen(alarmstr))
				strcat(alarmstr, "UUU/");
			if (strlen(alarmstr)) {
				/* Strip trailing / */
				alarmstr[strlen(alarmstr)-1]='\0';
			}
		} else {
			if (s.numchans)
				strcpy(alarmstr, "OK");
			else
				strcpy(alarmstr, "UNCONFIGURED");
		}

		cw_dynstr_printf(ds_p, FORMAT, s.desc, alarmstr, s.irqmisses, s.bpvcount, s.crc4count);
	}
	close(ctl);

	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static const char show_channels_usage[] =
	"Usage: dahdi show channels\n"
	"	Shows a list of available channels\n";

static const char show_channel_usage[] =
	"Usage: dahdi show channel <chan num>\n"
	"	Detailed information about a given channel\n";

static const char dahdi_show_status_usage[] =
	"Usage: dahdi show status\n"
	"       Shows a list of DAHDI cards with status\n";

static const char destroy_channel_usage[] =
	"Usage: dahdi destroy channel <chan num>\n"
	"	DON'T USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.  Immediately removes a given channel, whether it is in use or not\n";

static struct cw_clicmd dahdi_cli[] = {
	{
		.cmda = { "dahdi", "show", "cadences", NULL },
		.handler = handle_dahdi_show_cadences,
		.summary = "List cadences",
		.usage = dahdi_show_cadences_help,
	},
	{
		.cmda = {"dahdi", "show", "channels", NULL},
		.handler = dahdi_show_channels,
		.summary = "Show active DAHDI channels",
		.usage = show_channels_usage,
	},
	{
		.cmda = {"dahdi", "show", "channel", NULL},
		.handler = dahdi_show_channel,
		.summary = "Show information on a channel",
		.usage = show_channel_usage,
	},
	{
		.cmda = {"dahdi", "destroy", "channel", NULL},
		.handler = dahdi_destroy_channel,
		.summary = "Destroy a channel",
		.usage = destroy_channel_usage,
	},
	{
		.cmda = {"dahdi", "show", "status", NULL},
		.handler = dahdi_show_status,
		.summary = "Show all DAHDI cards status",
		.usage = dahdi_show_status_usage,
	},

};

#define TRANSFER	0
#define HANGUP		1

static int dahdi_fake_event(struct dahdi_pvt *p, int mode)
{
	if (p) {
		switch(mode) {
			case TRANSFER:
				p->fake_event = DAHDI_EVENT_WINKFLASH;
				break;
			case HANGUP:
				p->fake_event = DAHDI_EVENT_ONHOOK;
				break;
			default:
				cw_log(CW_LOG_WARNING, "I don't know how to handle transfer event with this: %d on channel %s\n",mode, p->owner->name);	
		}
	}
	return 0;
}
static struct dahdi_pvt *find_channel(int channel)
{
	struct dahdi_pvt *p = iflist;
	while(p) {
		if (p->channel == channel) {
			break;
		}
		p = p->next;
	}
	return p;
}

static struct cw_manager_message *action_dahdidndon(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg;
	struct dahdi_pvt *p;
	char *hdr = cw_manager_msg_header(req, "DAHDIChannel");

	CW_UNUSED(sess);

	if (!cw_strlen_zero(hdr)) {
		if ((p = find_channel(atoi(hdr)))) {
			p->dnd = 1;
			msg = cw_manager_response("Success", "DND Enabled");
		} else
			msg = cw_manager_response("Error", "No such channel");
	} else
		msg = cw_manager_response("Error", "No channel specified");

	return msg;
}

static struct cw_manager_message *action_dahdidndoff(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg;
	struct dahdi_pvt *p;
	char *hdr = cw_manager_msg_header(req, "DAHDIChannel");

	CW_UNUSED(sess);

	if (!cw_strlen_zero(hdr)) {
		if ((p = find_channel(atoi(hdr)))) {
			p->dnd = 0;
			msg = cw_manager_response("Success", "DND Disabled");
		} else
			msg = cw_manager_response("Error", "No such channel");
	} else
		msg = cw_manager_response("Error", "No channel specified");

	return msg;
}

static struct cw_manager_message *action_transfer(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg;
	struct dahdi_pvt *p;
	char *hdr = cw_manager_msg_header(req, "DAHDIChannel");

	CW_UNUSED(sess);

	if (!cw_strlen_zero(hdr)) {
		if ((p = find_channel(atoi(hdr)))) {
			dahdi_fake_event(p,TRANSFER);
			msg = cw_manager_response("Success", "Transferred");
		} else
			msg = cw_manager_response("Error", "No such channel");
	} else
		msg = cw_manager_response("Error", "No channel specified");

	return msg;
}

static struct cw_manager_message *action_transferhangup(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg;
	struct dahdi_pvt *p;
	char *hdr = cw_manager_msg_header(req, "DAHDIChannel");

	CW_UNUSED(sess);

	if (!cw_strlen_zero(hdr)) {
		if ((p = find_channel(atoi(hdr)))) {
			dahdi_fake_event(p,HANGUP);
			msg = cw_manager_response("Success", "Hungup");
		} else
			msg = cw_manager_response("Error", "No such channel");
	} else
		msg = cw_manager_response("Error", "No channel specified");

	return msg;
}

static struct cw_manager_message *action_dahdidialoffhook(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg;
	struct dahdi_pvt *p;
	char *hdr = cw_manager_msg_header(req, "DAHDIChannel");
	int i;

	CW_UNUSED(sess);

	if (!cw_strlen_zero(hdr)) {
		if ((p = find_channel(atoi(hdr)))) {
			if (p->owner) {
				hdr = cw_manager_msg_header(req, "Number");
				if (!cw_strlen_zero(hdr)) {
					for (i = 0; i < strlen(hdr); i++) {
						struct cw_frame f = { CW_FRAME_DTMF, hdr[i] };
						dahdi_queue_frame(p, &f, NULL); 
					}
					msg = cw_manager_response("Success", "Dialled");
				} else
					msg = cw_manager_response("Error", "No number specified");
			} else
				msg = cw_manager_response("Error", "Channel does not have it's owner");
		} else
			msg = cw_manager_response("Error", "No such channel");
	} else
		msg = cw_manager_response("Error", "No channel specified");

	return msg;
}

static struct cw_manager_message *action_dahdishowchannels(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg;
	struct dahdi_pvt *tmp;
	int err;

	if ((msg = cw_manager_response("Success", "DAHDI channel status will follow")) && !cw_manager_send(sess, req, &msg)) {
		err = 0;

		cw_mutex_lock(&iflock);
	
		for (tmp = iflist; !err && tmp; tmp = tmp->next) {
			if (tmp->channel > 0) {
				cw_manager_msg(&msg, 6,
					cw_msg_tuple("Event",      "%s", "DAHDIShowChannels"),
					cw_msg_tuple("Channel",    "%d", tmp->channel),
					cw_msg_tuple("Signalling", "%s", sig2str(tmp->sig)),
					cw_msg_tuple("Context",    "%s", tmp->context),
					cw_msg_tuple("DND",        "%s", (tmp->dnd ? "Enabled" : "Disabled")),
					cw_msg_tuple("Alarm",      "%s", alarm2str(get_alarms(tmp)))
				);

				err = cw_manager_send(sess, req, &msg);
			}
		}

		cw_mutex_unlock(&iflock);

		if (!err)
			cw_manager_msg(&msg, 1, cw_msg_tuple("Event", "%s", "Complete"));
	}

	return msg;
}

static void *dahdidisableec_app;
static const char dahdidisableec_name[] = "DAHDIDisableEC";
static const char dahdidisableec_syntax[] = "DAHDIDisableEC()";
static const char dahdidisableec_synopsis[] = "Disable Echo Canceller onto the current channel";
static const char dahdidisableec_description[] = "Disable Echo Canceller onto the current channel\n";

static int action_dahdidisableec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    CW_UNUSED(argc);
    CW_UNUSED(argv);
    CW_UNUSED(result);

    if (chan==NULL) {
	cw_log(CW_LOG_WARNING, "channel is NULL\n");
        return 0;
    }
    if ( (chan->type==NULL) || (strcmp(chan->type, dahdi_tech.type)) ) {
	cw_log(CW_LOG_WARNING, "channel is not DAHDI\n");
        return 0;
    }
    struct dahdi_pvt *p = chan->tech_pvt;
    if (p==NULL) {
	cw_log(CW_LOG_WARNING, "channel has no PVT structure\n");
        return 0;
    }
    dahdi_disable_ec(p);
    cw_log(CW_LOG_NOTICE, "echo cancelling OFF\n");
    return 0;
}

static struct manager_action manager_actions[] = {
	{
		.action = "DAHDITransfer",
		.authority = 0,
		.func = action_transfer,
		.synopsis = "Transfer DAHDI Channel",
	},
	{
		.action = "DAHDIHangup",
		.authority = 0,
		.func = action_transferhangup,
		.synopsis = "Hangup DAHDI Channel",
	},
	{
		.action = "DAHDIDialOffhook",
		.authority = 0,
		.func = action_dahdidialoffhook,
		.synopsis = "Dial over DAHDI channel while offhook",
	},
	{
		.action = "DAHDIDNDon",
		.authority = 0,
		.func = action_dahdidndon,
		.synopsis = "Toggle DAHDI channel Do Not Disturb status ON",
	},
	{
		.action = "DAHDIDNDoff",
		.authority = 0,
		.func = action_dahdidndoff,
		.synopsis = "Toggle DAHDI channel Do Not Disturb status OFF",
	},
	{
		.action = "DAHDIShowChannels",
		.authority = 0,
		.func = action_dahdishowchannels,
		.synopsis = "Show status of DAHDI channels",
	},
};


static int __unload_module(void)
{
	int x = 0;
	struct dahdi_pvt *p, *pl;
#ifdef ZAPATA_PRI
	int i;
	for(i=0;i<NUM_SPANS;i++) {
		if (!pthread_equal(pris[i].master, CW_PTHREADT_NULL))
			pthread_cancel(pris[i].master);
	}
	cw_cli_unregister_multiple(dahdi_pri_cli, sizeof(dahdi_pri_cli) / sizeof(dahdi_pri_cli[0]));
#endif
	cw_cli_unregister_multiple(dahdi_cli, sizeof(dahdi_cli) / sizeof(dahdi_cli[0]));

	cw_manager_action_unregister_multiple(manager_actions, arraysize(manager_actions));

	cw_unregister_function(dahdidisableec_app);
	cw_channel_unregister(&dahdi_tech);

	if (!cw_mutex_lock(&iflock)) {
		/* Hangup all interfaces if they have an owner */
		p = iflist;
		while(p) {
			if (p->owner)
				cw_softhangup(p->owner, CW_SOFTHANGUP_APPUNLOAD);
			p = p->next;
		}
		cw_mutex_unlock(&iflock);
	} else {
		cw_log(CW_LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
	if (!cw_mutex_lock(&monlock)) {
		if (monitor_thread && !pthread_equal(monitor_thread, CW_PTHREADT_STOP) && !pthread_equal(monitor_thread, CW_PTHREADT_NULL)) {
			pthread_cancel(monitor_thread);
			pthread_kill(monitor_thread, SIGURG);
			pthread_join(monitor_thread, NULL);
		}
		monitor_thread = CW_PTHREADT_STOP;
		cw_mutex_unlock(&monlock);
	} else {
		cw_log(CW_LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}

	if (!cw_mutex_lock(&iflock)) {
		/* Destroy all the interfaces and free their memory */
		p = iflist;
		while(p) {
			/* Free any callerid */
			free(p->cidspill);
			free(p->cidspill2);
			/* Close the zapata thingy */
			if (p->subs[SUB_REAL].dfd > -1)
				dahdi_close(p->subs[SUB_REAL].dfd);
			pl = p;
			p = p->next;
			x++;
			/* Free associated memory */
			if(pl)
				destroy_dahdi_pvt(&pl);
			cw_verbose(VERBOSE_PREFIX_3 "Unregistered channel %d\n", x);
		}
		iflist = NULL;
		ifcount = 0;
		cw_mutex_unlock(&iflock);
	} else {
		cw_log(CW_LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
#ifdef ZAPATA_PRI		
	for(i=0;i<NUM_SPANS;i++) {
		if (pris[i].master && !pthread_equal(pris[i].master, CW_PTHREADT_NULL))
			pthread_join(pris[i].master, NULL);
		dahdi_close(pris[i].fds[i]);
	}
#endif
	return 0;
}

static int unload_module()
{
#ifdef ZAPATA_PRI		
	int y;
	for (y=0;y<NUM_SPANS;y++)
		cw_mutex_destroy(&pris[y].lock);
#endif
	return __unload_module();
}
		
static int setup_dahdi(int reload)
{
	struct cw_config *cfg;
	struct cw_variable *v;
	struct dahdi_pvt *tmp;
	char *chan;
	char *c;
	char *ringc;
	int start, finish,x;
	int y;
	int found_pseudo = 0;
	int cur_radio = 0;
    int i;
#ifdef ZAPATA_PRI
	int spanno;
	int logicalspan;
	int trunkgroup;
	int dchannels[NUM_DCHANS];
	struct dahdi_pri *pri;
#endif

	/* We *must* have a config file otherwise stop immediately */
	if (!(cfg = cw_config_load("chan_dahdi.conf"))) {
		cw_log(CW_LOG_ERROR, "Unable to load config chan_dahdi.conf\n");
		return -1;
	}
	

	if (cw_mutex_lock(&iflock)) {
		/* It's a little silly to lock it, but we mind as well just to be sure */
		cw_log(CW_LOG_ERROR, "Unable to lock interface list???\n");
		return -1;
	}
#ifdef ZAPATA_PRI
	if (!reload) {
		/* Process trunkgroups first */
		v = cw_variable_browse(cfg, "trunkgroups");
		while(v) {
			if (!strcasecmp(v->name, "trunkgroup")) {
				trunkgroup = atoi(v->value);
				if (trunkgroup > 0) {
					if ((c = strchr(v->value, ','))) {
						i = 0;
						memset(dchannels, 0, sizeof(dchannels));
						while(c && (i < NUM_DCHANS)) {
							dchannels[i] = atoi(c + 1);
							if (dchannels[i] < 0) {
								cw_log(CW_LOG_WARNING, "D-channel for trunk group %d must be a postiive number at line %d of chan_dahdi.conf\n", trunkgroup, v->lineno);
							} else
								i++;
							c = strchr(c + 1, ',');
						}
						if (i) {
							if (pri_create_trunkgroup(trunkgroup, dchannels)) {
								cw_log(CW_LOG_WARNING, "Unable to create trunk group %d with Primary D-channel %d at line %d of chan_dahdi.conf\n", trunkgroup, dchannels[0], v->lineno);
							} else if (option_verbose > 1)
								cw_verbose(VERBOSE_PREFIX_2 "Created trunk group %d with Primary D-channel %d and %d backup%s\n", trunkgroup, dchannels[0], i - 1, (i == 1) ? "" : "s");
						} else
							cw_log(CW_LOG_WARNING, "Trunk group %d lacks any valid D-channels at line %d of chan_dahdi.conf\n", trunkgroup, v->lineno);
					} else
						cw_log(CW_LOG_WARNING, "Trunk group %d lacks a primary D-channel at line %d of chan_dahdi.conf\n", trunkgroup, v->lineno);
				} else
					cw_log(CW_LOG_WARNING, "Trunk group identifier must be a positive integer at line %d of chan_dahdi.conf\n", v->lineno);
			} else if (!strcasecmp(v->name, "spanmap")) {
				spanno = atoi(v->value);
				if (spanno > 0) {
					if ((c = strchr(v->value, ','))) {
						trunkgroup = atoi(c + 1);
						if (trunkgroup > 0) {
							if ((c = strchr(c + 1, ','))) 
								logicalspan = atoi(c + 1);
							else
								logicalspan = 0;
							if (logicalspan >= 0) {
								if (pri_create_spanmap(spanno - 1, trunkgroup, logicalspan)) {
									cw_log(CW_LOG_WARNING, "Failed to map span %d to trunk group %d (logical span %d)\n", spanno, trunkgroup, logicalspan);
								} else if (option_verbose > 1) 
									cw_verbose(VERBOSE_PREFIX_2 "Mapped span %d to trunk group %d (logical span %d)\n", spanno, trunkgroup, logicalspan);
							} else
								cw_log(CW_LOG_WARNING, "Logical span must be a postive number, or '0' (for unspecified) at line %d of chan_dahdi.conf\n", v->lineno);
						} else
							cw_log(CW_LOG_WARNING, "Trunk group must be a postive number at line %d of chan_dahdi.conf\n", v->lineno);
					} else
						cw_log(CW_LOG_WARNING, "Missing trunk group for span map at line %d of chan_dahdi.conf\n", v->lineno);
				} else
					cw_log(CW_LOG_WARNING, "Span number must be a postive integer at line %d of chan_dahdi.conf\n", v->lineno);
			} else {
				cw_log(CW_LOG_NOTICE, "Ignoring unknown keyword '%s' in trunkgroups\n", v->name);
			}
			v = v->next;
		}
	}
#endif

	/* Copy the default jb config over global_jbconf */
	cw_jb_default_config(&global_jbconf);

	v = cw_variable_browse(cfg, "channels");
	while(v) {
		/* handle jb conf */
		if(cw_jb_read_conf(&global_jbconf, v->name, v->value) == 0)
		{
			v = v->next;
			continue;
		}

		/* Create the interface list */
		if (!strcasecmp(v->name, "channel")
#ifdef ZAPATA_PRI
			|| !strcasecmp(v->name, "crv")
#endif			
					) {
			if (reload == 0) {
				if (cur_signalling < 0) {
					cw_log(CW_LOG_ERROR, "Signalling must be specified before any channels are.\n");
					cw_config_destroy(cfg);
					cw_mutex_unlock(&iflock);
					return -1;
				}
			}
			c = v->value;

#ifdef ZAPATA_PRI
			pri = NULL;
			if (!strcasecmp(v->name, "crv")) {
				if (sscanf(c, "%d:%n", &trunkgroup, &y) != 1) {
					cw_log(CW_LOG_WARNING, "CRV must begin with trunkgroup followed by a colon at line %d\n", v->lineno);
					cw_config_destroy(cfg);
					cw_mutex_unlock(&iflock);
					return -1;
				}
				if (trunkgroup < 1) {
					cw_log(CW_LOG_WARNING, "CRV trunk group must be a postive number at line %d\n", v->lineno);
					cw_config_destroy(cfg);
					cw_mutex_unlock(&iflock);
					return -1;
				}
				c+=y;
				for (y=0;y<NUM_SPANS;y++) {
					if (pris[y].trunkgroup == trunkgroup) {
						pri = pris + y;
						break;
					}
				}
				if (!pri) {
					cw_log(CW_LOG_WARNING, "No such trunk group %d at CRV declaration at line %d\n", trunkgroup, v->lineno);
					cw_config_destroy(cfg);
					cw_mutex_unlock(&iflock);
					return -1;
				}
			}
#endif			
			chan = strsep(&c, ",");
			while(chan) {
				if (sscanf(chan, "%d-%d", &start, &finish) == 2) {
					/* Range */
				} else if (sscanf(chan, "%d", &start)) {
					/* Just one */
					finish = start;
				} else if (!strcasecmp(chan, "pseudo")) {
					finish = start = CHAN_PSEUDO;
					found_pseudo = 1;
				} else {
					cw_log(CW_LOG_ERROR, "Syntax error parsing '%s' at '%s'\n", v->value, chan);
					cw_config_destroy(cfg);
					cw_mutex_unlock(&iflock);
					return -1;
				}
				if (finish < start) {
					cw_log(CW_LOG_WARNING, "Sillyness: %d < %d\n", start, finish);
					x = finish;
					finish = start;
					start = x;
				}
				for (x=start;x<=finish;x++) {
#ifdef ZAPATA_PRI
					tmp = mkintf(x, cur_signalling, cur_radio, pri, reload);
#else					
					tmp = mkintf(x, cur_signalling, cur_radio, NULL, reload);
#endif					

					if (tmp) {
						if (option_verbose > 2) {
#ifdef ZAPATA_PRI
							if (pri)
								cw_verbose(VERBOSE_PREFIX_3 "%s CRV %d:%d, %s signalling\n", reload ? "Reconfigured" : "Registered", trunkgroup,x, sig2str(tmp->sig));
							else
#endif
								cw_verbose(VERBOSE_PREFIX_3 "%s channel %d, %s signalling\n", reload ? "Reconfigured" : "Registered", x, sig2str(tmp->sig));
						}
					} else {
						if (reload == 1)
							cw_log(CW_LOG_ERROR, "Unable to reconfigure channel '%s'\n", v->value);
						else
							cw_log(CW_LOG_ERROR, "Unable to register channel '%s'\n", v->value);
						cw_config_destroy(cfg);
						cw_mutex_unlock(&iflock);
						return -1;
					}
				}
				chan = strsep(&c, ",");
			}
		} else if (!strcasecmp(v->name, "usedistinctiveringdetection")) {
			global.usedistinctiveringdetection = cw_true(v->value);
		} else if (!strcasecmp(v->name, "distinctiveringaftercid")) {
			global.distinctiveringaftercid = cw_true(v->value);
		} else if (!strcasecmp(v->name, "dring1cadence")) {
			ringc = v->value;
			sscanf(ringc, "%d,%d,%d", &drings.ringnum[0].ring[0], &drings.ringnum[0].ring[1], &drings.ringnum[0].ring[2]);
		} else if (!strcasecmp(v->name, "dring2cadence")) {
			ringc = v->value;
			sscanf(ringc, "%d,%d,%d", &drings.ringnum[1].ring[0], &drings.ringnum[1].ring[1], &drings.ringnum[1].ring[2]);
		} else if (!strcasecmp(v->name, "dring3cadence")) {
			ringc = v->value;
			sscanf(ringc, "%d,%d,%d", &drings.ringnum[2].ring[0], &drings.ringnum[2].ring[1], &drings.ringnum[2].ring[2]);
		} else if (!strcasecmp(v->name, "dring1context")) {
			cw_copy_string(drings.ringContext[0].contextData,v->value,sizeof(drings.ringContext[0].contextData));
		} else if (!strcasecmp(v->name, "dring2context")) {
			cw_copy_string(drings.ringContext[1].contextData,v->value,sizeof(drings.ringContext[1].contextData));
		} else if (!strcasecmp(v->name, "dring3context")) {
			cw_copy_string(drings.ringContext[2].contextData,v->value,sizeof(drings.ringContext[2].contextData));
		} else if (!strcasecmp(v->name, "dring1exten")) {
			cw_copy_string(drings.ringContext[0].extenData,v->value,sizeof(drings.ringContext[0].extenData));
		} else if (!strcasecmp(v->name, "dring2exten")) {
			cw_copy_string(drings.ringContext[1].extenData,v->value,sizeof(drings.ringContext[1].extenData));
		} else if (!strcasecmp(v->name, "dring3exten")) {
			cw_copy_string(drings.ringContext[2].extenData,v->value,sizeof(drings.ringContext[2].extenData));
		} else if (!strcasecmp(v->name, "dring1")) { /* OBSOLETE */
			ringc = v->value;
			sscanf(ringc, "%d,%d,%d", &drings.ringnum[0].ring[0], &drings.ringnum[0].ring[1], &drings.ringnum[0].ring[2]);
			for (i = 0; i < sizeof(drings.ringnum[0].ring)/sizeof(drings.ringnum[0].ring[0]); i++)
				drings.ringnum[0].ring[i] = SAMPLES_TO_MS(drings.ringnum[0].ring[i] * READ_SIZE);
			cw_log(CW_LOG_NOTICE, "\"%s=%s\" is obsolete. Please replace with \"%scadence=%d,%d,%d\"\n", v->name, ringc, v->name, drings.ringnum[0].ring[0], drings.ringnum[0].ring[1], drings.ringnum[0].ring[2]);
		} else if (!strcasecmp(v->name, "dring2")) { /* OBSOLETE */
			ringc = v->value;
			sscanf(ringc, "%d,%d,%d", &drings.ringnum[1].ring[0], &drings.ringnum[1].ring[1], &drings.ringnum[1].ring[2]);
			for (i = 0; i < sizeof(drings.ringnum[0].ring)/sizeof(drings.ringnum[0].ring[0]); i++)
				drings.ringnum[0].ring[i] = SAMPLES_TO_MS(drings.ringnum[0].ring[i] * READ_SIZE);
			cw_log(CW_LOG_NOTICE, "\"%s=%s\" is obsolete. Please replace with \"%scadence=%d,%d,%d\"\n", v->name, ringc, v->name, drings.ringnum[1].ring[0], drings.ringnum[1].ring[1], drings.ringnum[1].ring[2]);
		} else if (!strcasecmp(v->name, "dring3")) { /* OBSOLETE */
			ringc = v->value;
			sscanf(ringc, "%d,%d,%d", &drings.ringnum[2].ring[0], &drings.ringnum[2].ring[1], &drings.ringnum[2].ring[2]);
			for (i = 0; i < sizeof(drings.ringnum[0].ring)/sizeof(drings.ringnum[0].ring[0]); i++)
				drings.ringnum[0].ring[i] = SAMPLES_TO_MS(drings.ringnum[0].ring[i] * READ_SIZE);
			cw_log(CW_LOG_NOTICE, "\"%s=%s\" is obsolete. Please replace with \"%scadence=%d,%d,%d\"\n", v->name, ringc, v->name, drings.ringnum[2].ring[0], drings.ringnum[2].ring[1], drings.ringnum[2].ring[2]);
		} else if (!strcasecmp(v->name, "usecallerid")) {
			global.use_callerid = cw_true(v->value);
		} else if (!strcasecmp(v->name, "cidsignalling")) {
			if (!strcasecmp(v->value, "v23"))
				global.cid_signalling = ADSI_STANDARD_CLIP;
			else if (!strcasecmp(v->value, "dtmf"))
				global.cid_signalling = ADSI_STANDARD_CLIP_DTMF;
			if (!strcasecmp(v->value, "bell") || cw_true(v->value))
				global.cid_signalling = ADSI_STANDARD_CLASS;
		} else if (!strcasecmp(v->name, "cidstart")) {
			if (!strcasecmp(v->value, "polarity"))
				global.cid_start = global.cid_send_on = CID_START_POLARITY;
			else if (!strcasecmp(v->value, "ring") || cw_true(v->value))
				global.cid_start = global.cid_send_on = CID_START_RING;
		} else if (!strcasecmp(v->name, "polarityevents")) {
			global.polarity_events = cw_true(v->value);
		} else if (!strcasecmp(v->name, "threewaycalling")) {
			global.threewaycalling = cw_true(v->value);
		} else if (!strcasecmp(v->name, "cancallforward")) {
			global.cancallforward = cw_true(v->value);
		} else if (!strcasecmp(v->name, "relaxdtmf")) {
			if (cw_true(v->value)) 
				global.dtmfrelax = DSP_DIGITMODE_RELAXDTMF;
			else
				global.dtmfrelax = 0;
		} else if (!strcasecmp(v->name, "mailbox")) {
			cw_copy_string(global.mailbox, v->value, sizeof(global.mailbox));
		} else if (!strcasecmp(v->name, "adsi")) {
			global.adsi = cw_true(v->value);
		} else if (!strcasecmp(v->name, "transfer")) {
			global.transfer = cw_true(v->value);
		} else if (!strcasecmp(v->name, "canpark")) {
			global.canpark = cw_true(v->value);
		} else if (!strcasecmp(v->name, "echocancelwhenbridged")) {
			global.echocanbridged = cw_true(v->value);
		} else if (!strcasecmp(v->name, "busydetect")) {
			global.busydetect = cw_true(v->value);
		} else if (!strcasecmp(v->name, "busycount")) {
			global.busycount = atoi(v->value);
		} else if (!strcasecmp(v->name, "busypattern")) {
			if (sscanf(v->value, "%d,%d", &global.busy_tonelength, &global.busy_quietlength) != 2) {
				cw_log(CW_LOG_ERROR, "busypattern= expects busypattern=tonelength,quietlength\n");
			}
		} else if (!strcasecmp(v->name, "callprogress")) {
			if (cw_true(v->value))
				global.callprogress |= 1;
			else
				global.callprogress &= ~1;
		} else if (!strcasecmp(v->name, "faxdetect")) {
			if (!strcasecmp(v->value, "incoming")) {
				global.callprogress |= 4;
				global.callprogress &= ~2;
			} else if (!strcasecmp(v->value, "outgoing")) {
				global.callprogress &= ~4;
				global.callprogress |= 2;
			} else if (!strcasecmp(v->value, "both") || cw_true(v->value))
				global.callprogress |= 6;
			else
				global.callprogress &= ~6;
		} else if (!strcasecmp(v->name, "echocancel")) {
			if (!cw_strlen_zero(v->value)) {
				y = atoi(v->value);
			} else
				y = 0;
			if ((y == 32) || (y == 64) || (y == 128) || (y == 256) || (y == 512) || (y == 1024))
				global.echocancel = y;
			else {
				global.echocancel = cw_true(v->value);
				if (global.echocancel)
					global.echocancel=128;
			}
		} else if (!strcasecmp(v->name, "echotraining")) {
			if (sscanf(v->value, "%d", &y) == 1) {
				if ((y < 10) || (y > 4000)) {
					cw_log(CW_LOG_WARNING, "Echo training time must be within the range of 10 to 2000 ms at line %d\n", v->lineno);					
				} else {
					global.echotraining = y;
				}
			} else if (cw_true(v->value)) {
				global.echotraining = 400;
			} else
				global.echotraining = 0;
		} else if (!strcasecmp(v->name, "hidecallerid")) {
			global.hidecallerid = cw_true(v->value);
 		} else if (!strcasecmp(v->name, "pulsedial")) {
			global.pulse = cw_true(v->value);
		} else if (!strcasecmp(v->name, "callreturn")) {
			global.callreturn = cw_true(v->value);
		} else if (!strcasecmp(v->name, "callwaiting")) {
			global.callwaiting = cw_true(v->value);
		} else if (!strcasecmp(v->name, "callwaitingcallerid")) {
			global.callwaitingcallerid = cw_true(v->value);
		} else if (!strcasecmp(v->name, "context")) {
			cw_copy_string(global.context, v->value, sizeof(global.context));
		} else if (!strcasecmp(v->name, "exten")) {
			cw_copy_string(global.exten, v->value, sizeof(global.exten));
		} else if (!strcasecmp(v->name, "language")) {
			cw_copy_string(global.language, v->value, sizeof(global.language));
		} else if (!strcasecmp(v->name, "progzone")) {
			cw_copy_string(progzone, v->value, sizeof(progzone));
		} else if (!strcasecmp(v->name, "musiconhold")) {
			cw_copy_string(global.musicclass, v->value, sizeof(global.musicclass));
		} else if (!strcasecmp(v->name, "stripmsd")) {
			global.stripmsd = atoi(v->value);
		} else if (!strcasecmp(v->name, "jitterbuffers")) {
			numbufs = atoi(v->value);
		} else if (!strcasecmp(v->name, "group")) {
			cur_group = cw_get_group(v->value);
		} else if (!strcasecmp(v->name, "callgroup")) {
			cur_callergroup = cw_get_group(v->value);
		} else if (!strcasecmp(v->name, "pickupgroup")) {
			cur_pickupgroup = cw_get_group(v->value);
		} else if (!strcasecmp(v->name, "immediate")) {
			global.immediate = cw_true(v->value);
		} else if (!strcasecmp(v->name, "transfertobusy")) {
			global.transfertobusy = cw_true(v->value);
		} else if (!strcasecmp(v->name, "cid_rxgain")) {
			if (sscanf(v->value, "%f", &global.cid_rxgain) != 1) {
				cw_log(CW_LOG_WARNING, "Invalid cid_rxgain: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "rxgain")) {
			if (sscanf(v->value, "%f", &global.rxgain) != 1) {
				cw_log(CW_LOG_WARNING, "Invalid rxgain: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "txgain")) {
			if (sscanf(v->value, "%f", &global.txgain) != 1) {
				cw_log(CW_LOG_WARNING, "Invalid txgain: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "tonezone")) {
			if (sscanf(v->value, "%d", &global.tonezone) != 1) {
				cw_log(CW_LOG_WARNING, "Invalid tonezone: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "callerid")) {
			if (!strcasecmp(v->value, "asreceived")) {
				global.cid_num[0] = '\0';
				global.cid_name[0] = '\0';
			} else {
				cw_callerid_split(v->value, global.cid_name, sizeof(global.cid_name), global.cid_num, sizeof(global.cid_num));
			}
		} else if (!strcasecmp(v->name, "useincomingcalleridondahditransfer")) {
			global.dahditrcallerid = cw_true(v->value);
		} else if (!strcasecmp(v->name, "restrictcid")) {
			global.restrictcid = cw_true(v->value);
		} else if (!strcasecmp(v->name, "usecallingpres")) {
			global.use_callingpres = cw_true(v->value);
		} else if (!strcasecmp(v->name, "accountcode")) {
			cw_copy_string(global.accountcode, v->value, sizeof(global.accountcode));
		} else if (!strcasecmp(v->name, "amaflags")) {
			y = cw_cdr_amaflags2int(v->value);
			if (y < 0) 
				cw_log(CW_LOG_WARNING, "Invalid AMA flags: %s at line %d\n", v->value, v->lineno);
			else
				global.amaflags = y;
		} else if(!reload){ 
			 if (!strcasecmp(v->name, "signalling")) {
				if (!strcasecmp(v->value, "em")) {
					cur_signalling = SIG_EM;
				} else if (!strcasecmp(v->value, "em_e1")) {
					cur_signalling = SIG_EM_E1;
				} else if (!strcasecmp(v->value, "em_w")) {
					cur_signalling = SIG_EMWINK;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxs_ls")) {
					cur_signalling = SIG_FXSLS;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxs_gs")) {
					cur_signalling = SIG_FXSGS;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxs_ks")) {
					cur_signalling = SIG_FXSKS;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxo_ls")) {
					cur_signalling = SIG_FXOLS;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxo_gs")) {
					cur_signalling = SIG_FXOGS;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxo_ks")) {
					cur_signalling = SIG_FXOKS;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "fxs_rx")) {
					cur_signalling = SIG_FXSKS;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "fxo_rx")) {
					cur_signalling = SIG_FXOLS;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "fxs_tx")) {
					cur_signalling = SIG_FXSLS;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "fxo_tx")) {
					cur_signalling = SIG_FXOGS;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "em_rx")) {
					cur_signalling = SIG_EM;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "em_tx")) {
					cur_signalling = SIG_EM;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "em_rxtx")) {
					cur_signalling = SIG_EM;
					cur_radio = 2;
				} else if (!strcasecmp(v->value, "em_txrx")) {
					cur_signalling = SIG_EM;
					cur_radio = 2;
				} else if (!strcasecmp(v->value, "sf")) {
					cur_signalling = SIG_SF;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "sf_w")) {
					cur_signalling = SIG_SFWINK;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "sf_featd")) {
					cur_signalling = SIG_FEATD;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "sf_featdmf")) {
					cur_signalling = SIG_FEATDMF;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "sf_featb")) {
					cur_signalling = SIG_SF_FEATB;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "sf")) {
					cur_signalling = SIG_SF;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "sf_rx")) {
					cur_signalling = SIG_SF;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "sf_tx")) {
					cur_signalling = SIG_SF;
					cur_radio = 1;
				} else if (!strcasecmp(v->value, "sf_rxtx")) {
					cur_signalling = SIG_SF;
					cur_radio = 2;
				} else if (!strcasecmp(v->value, "sf_txrx")) {
					cur_signalling = SIG_SF;
					cur_radio = 2;
				} else if (!strcasecmp(v->value, "featd")) {
					cur_signalling = SIG_FEATD;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "featdmf")) {
					cur_signalling = SIG_FEATDMF;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "featdmf_ta")) {
					cur_signalling = SIG_FEATDMF_TA;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "e911")) {
					cur_signalling = SIG_E911;
					cur_radio = 0;
				} else if (!strcasecmp(v->value, "featb")) {
					cur_signalling = SIG_FEATB;
					cur_radio = 0;
#ifdef ZAPATA_PRI
				} else if (!strcasecmp(v->value, "pri_net")) {
					cur_radio = 0;
					cur_signalling = SIG_PRI;
					pritype = PRI_NETWORK;
				} else if (!strcasecmp(v->value, "pri_cpe")) {
					cur_signalling = SIG_PRI;
					cur_radio = 0;
					pritype = PRI_CPE;
				} else if (!strcasecmp(v->value, "gr303fxoks_net")) {
					cur_signalling = SIG_GR303FXOKS;
					cur_radio = 0;
					pritype = PRI_NETWORK;
				} else if (!strcasecmp(v->value, "gr303fxsks_cpe")) {
					cur_signalling = SIG_GR303FXSKS;
					cur_radio = 0;
					pritype = PRI_CPE;
#endif
				} else {
					cw_log(CW_LOG_ERROR, "Unknown signalling method '%s'\n", v->value);
				}
#ifdef ZAPATA_PRI
			} else if (!strcasecmp(v->name, "pridialplan")) {
				if (!strcasecmp(v->value, "national")) {
					dialplan = PRI_NATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "unknown")) {
					dialplan = PRI_UNKNOWN + 1;
				} else if (!strcasecmp(v->value, "private")) {
					dialplan = PRI_PRIVATE + 1;
				} else if (!strcasecmp(v->value, "international")) {
					dialplan = PRI_INTERNATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "local")) {
					dialplan = PRI_LOCAL_ISDN + 1;
	 			} else if (!strcasecmp(v->value, "dynamic")) {
 					dialplan = -1;
				} else {
					cw_log(CW_LOG_WARNING, "Unknown PRI dialplan '%s' at line %d.\n", v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "prilocaldialplan")) {
				if (!strcasecmp(v->value, "national")) {
					localdialplan = PRI_NATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "unknown")) {
					localdialplan = PRI_UNKNOWN + 1;
				} else if (!strcasecmp(v->value, "private")) {
					localdialplan = PRI_PRIVATE + 1;
				} else if (!strcasecmp(v->value, "international")) {
					localdialplan = PRI_INTERNATIONAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "local")) {
					localdialplan = PRI_LOCAL_ISDN + 1;
				} else if (!strcasecmp(v->value, "dynamic")) {
					localdialplan = -1;
				} else {
					cw_log(CW_LOG_WARNING, "Unknown PRI dialplan '%s' at line %d.\n", v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "switchtype")) {
				if (!strcasecmp(v->value, "national")) 
					switchtype = PRI_SWITCH_NI2;
				else if (!strcasecmp(v->value, "ni1"))
					switchtype = PRI_SWITCH_NI1;
				else if (!strcasecmp(v->value, "dms100"))
					switchtype = PRI_SWITCH_DMS100;
				else if (!strcasecmp(v->value, "4ess"))
					switchtype = PRI_SWITCH_ATT4ESS;
				else if (!strcasecmp(v->value, "5ess"))
					switchtype = PRI_SWITCH_LUCENT5E;
				else if (!strcasecmp(v->value, "euroisdn"))
					switchtype = PRI_SWITCH_EUROISDN_E1;
				else if (!strcasecmp(v->value, "qsig"))
					switchtype = PRI_SWITCH_QSIG;
				else {
					cw_log(CW_LOG_ERROR, "Unknown switchtype '%s'\n", v->value);
					cw_config_destroy(cfg);
					cw_mutex_unlock(&iflock);
					return -1;
				}
			} else if (!strcasecmp(v->name, "nsf")) {
				if (!strcasecmp(v->value, "sdn"))
					nsf = PRI_NSF_SDN;
				else if (!strcasecmp(v->value, "megacom"))
					nsf = PRI_NSF_MEGACOM;
				else if (!strcasecmp(v->value, "accunet"))
					nsf = PRI_NSF_ACCUNET;
				else if (!strcasecmp(v->value, "none"))
					nsf = PRI_NSF_NONE;
				else {
					cw_log(CW_LOG_WARNING, "Unknown network-specific facility '%s'\n", v->value);
					nsf = PRI_NSF_NONE;
				}
			} else if (!strcasecmp(v->name, "priindication")) {
				if (!strcasecmp(v->value, "outofband"))
					global.priindication_oob = 1;
				else if (!strcasecmp(v->value, "inband"))
					global.priindication_oob = 0;
				else
					cw_log(CW_LOG_WARNING, "'%s' is not a valid pri indication value, should be 'inband' or 'outofband' at line %d\n",
						v->value, v->lineno);
			} else if (!strcasecmp(v->name, "priexclusive")) {
				global.priexclusive = cw_true(v->value);
			} else if (!strcasecmp(v->name, "internationalprefix")) {
				cw_copy_string(internationalprefix, v->value, sizeof(internationalprefix));
			} else if (!strcasecmp(v->name, "nationalprefix")) {
				cw_copy_string(nationalprefix, v->value, sizeof(nationalprefix));
			} else if (!strcasecmp(v->name, "localprefix")) {
				cw_copy_string(localprefix, v->value, sizeof(localprefix));
			} else if (!strcasecmp(v->name, "privateprefix")) {
				cw_copy_string(privateprefix, v->value, sizeof(privateprefix));
			} else if (!strcasecmp(v->name, "unknownprefix")) {
				cw_copy_string(unknownprefix, v->value, sizeof(unknownprefix));
			} else if (!strcasecmp(v->name, "resetinterval")) {
				if (!strcasecmp(v->value, "never"))
					resetinterval = -1;
				else if( atoi(v->value) >= 60 )
					resetinterval = atoi(v->value);
				else
					cw_log(CW_LOG_WARNING, "'%s' is not a valid reset interval, should be >= 60 seconds or 'never' at line %d\n",
						v->value, v->lineno);
			} else if (!strcasecmp(v->name, "minunused")) {
				minunused = atoi(v->value);
			} else if (!strcasecmp(v->name, "idleext")) {
				cw_copy_string(idleext, v->value, sizeof(idleext));
			} else if (!strcasecmp(v->name, "idledial")) {
				cw_copy_string(idledial, v->value, sizeof(idledial));
			} else if (!strcasecmp(v->name, "overlapdial")) {
				overlapdial = cw_true(v->value);
			} else if (!strcasecmp(v->name, "pritimer")) {
#ifdef PRI_GETSET_TIMERS
				char *timerc;
				int timer, timeridx;
				c = v->value;
				timerc = strsep(&c, ",");
				if (timerc) {
					timer = atoi(c);
					if (!timer)
						cw_log(CW_LOG_WARNING, "'%s' is not a valid value for an ISDN timer\n", timerc);
					else {
						if ((timeridx = pri_timer2idx(timerc)) >= 0)
							pritimers[timeridx] = timer;
						else
							cw_log(CW_LOG_WARNING, "'%s' is not a valid ISDN timer\n", timerc);
					}
				} else
					cw_log(CW_LOG_WARNING, "'%s' is not a valid ISDN timer configuration string\n", v->value);

			} else if (!strcasecmp(v->name, "facilityenable")) {
				facilityenable = cw_true(v->value);
#endif /* PRI_GETSET_TIMERS */
#endif /* ZAPATA_PRI */
			} else if (!strcasecmp(v->name, "cadence")) {
				/* setup to scan our argument */
				int element_count, ms[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
				struct dahdi_ring_cadence new_cadence;
				int cid_location = -1;
                		int firstcadencepos = 0;
				char original_args[80];
				int cadence_is_ok = 1;

				cw_copy_string(original_args, v->value, sizeof(original_args));
				/* 16 cadences allowed (8 pairs) */
				element_count = sscanf(v->value, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &ms[0], &ms[1], &ms[2], &ms[3], &ms[4], &ms[5], &ms[6], &ms[7], &ms[8], &ms[9], &ms[10], &ms[11], &ms[12], &ms[13], &ms[14], &ms[15]);
	
				/* Cadence must be even (on/off) */
				if (element_count % 2 == 1) {
					cw_log(CW_LOG_ERROR, "Must be a silence duration for each ring duration: %s\n", original_args);
					cadence_is_ok = 0;
				}
	
				/* Ring cadences cannot be negative */
				for (i=0; i < element_count; i++) {
				        if (ms[i] == 0) {
					        cw_log(CW_LOG_ERROR, "Ring or silence duration cannot be zero: %s\n", original_args);
						cadence_is_ok = 0;
						break;
					} else if (ms[i] < 0) {
						if (i % 2 == 1) {
						        /* Silence duration, negative possibly okay */
							if (cid_location == -1) {
							        cid_location = i;
								ms[i] *= -1;
							} else {
							        cw_log(CW_LOG_ERROR, "CID location specified twice: %s\n", original_args);
								cadence_is_ok = 0;
								break;
							}
						} else {
							if (firstcadencepos == 0) {
							        firstcadencepos = i; /* only recorded to avoid duplicate specification */
								                     /* duration will be passed negative to the DAHDI driver */
							} else {
							        cw_log(CW_LOG_ERROR, "First cadence position specified twice: %s\n", original_args);
								cadence_is_ok = 0;
								break;
							}
						}
					}
				}
	
				/* Substitute our scanned cadence */
				for (i=0; i < 16; i++) {
					new_cadence.ringcadence[i] = ms[i];
				}
	
				if (cadence_is_ok) {
					/* ---we scanned it without getting annoyed; now some sanity checks--- */
					if (element_count < 2) {
						cw_log(CW_LOG_ERROR, "Minimum cadence is ring,pause: %s\n", original_args);
					} else {
						if (cid_location == -1) {
							/* user didn't say; default to first pause */
							cid_location = 1;
						} else {
							/* convert element_index to cidrings value */
							cid_location = (cid_location + 1) / 2;
						}
						/* ---we like their cadence; try to install it--- */
						if (!user_has_defined_cadences++)
							/* this is the first user-defined cadence; clear the default user cadences */
							num_cadence = 0;
						if ((num_cadence+1) >= NUM_CADENCE_MAX)
							cw_log(CW_LOG_ERROR, "Already %d cadences; can't add another: %s\n", NUM_CADENCE_MAX, original_args);
						else {
							cadences[num_cadence] = new_cadence;
							cidrings[num_cadence++] = cid_location;
							if (option_verbose > 2)
								cw_verbose(VERBOSE_PREFIX_3 "cadence 'r%d' added: %s\n", num_cadence, original_args);
						}
					}
				}
			} else if (!strcasecmp(v->name, "ringtimeout")) {
				ringt_base = MS_TO_SAMPLES(atoi(v->value));
			} else if (!strcasecmp(v->name, "prewink")) {
				cur_prewink = atoi(v->value);
			} else if (!strcasecmp(v->name, "preflash")) {
				cur_preflash = atoi(v->value);
			} else if (!strcasecmp(v->name, "wink")) {
				cur_wink = atoi(v->value);
			} else if (!strcasecmp(v->name, "flash")) {
				cur_flash = atoi(v->value);
			} else if (!strcasecmp(v->name, "start")) {
				cur_start = atoi(v->value);
			} else if (!strcasecmp(v->name, "rxwink")) {
				cur_rxwink = atoi(v->value);
			} else if (!strcasecmp(v->name, "rxflash")) {
				cur_rxflash = atoi(v->value);
			} else if (!strcasecmp(v->name, "debounce")) {
				cur_debounce = atoi(v->value);
			} else if (!strcasecmp(v->name, "toneduration")) {
				int toneduration;
				int ctlfd;
				int res;
				struct dahdi_dialparams dps;

				ctlfd = open("/dev/dahdi/ctl", O_RDWR);
				if (ctlfd == -1) {
					cw_log(CW_LOG_ERROR, "Unable to open /dev/dahdi/ctl to set toneduration\n");
					return -1;
				}

				toneduration = atoi(v->value);
				if (toneduration > -1) {
					memset(&dps, 0, sizeof(dps));
					dps.dtmf_tonelen = dps.mfv1_tonelen = toneduration;
					res = ioctl(ctlfd, DAHDI_SET_DIALPARAMS, &dps);
					if (res < 0) {
						cw_log(CW_LOG_ERROR, "Invalid tone duration: %d ms\n", toneduration);
						return -1;
					}
				}
				close(ctlfd);
			} else if (!strcasecmp(v->name, "polarityonanswerdelay")) {
				global.polarityonanswerdelay = atoi(v->value);
			} else if (!strcasecmp(v->name, "answeronpolarityswitch")) {
				global.answeronpolarityswitch = cw_true(v->value);
			} else if (!strcasecmp(v->name, "hanguponpolarityswitch")) {
				global.hanguponpolarityswitch = cw_true(v->value);
			} else if (!strcasecmp(v->name, "sendcalleridafter")) {
				global.sendcalleridafter = atoi(v->value);
			} else if (!strcasecmp(v->name, "defaultcic")) {
				cw_copy_string(defaultcic, v->value, sizeof(defaultcic));
			} else if (!strcasecmp(v->name, "defaultozz")) {
				cw_copy_string(defaultozz, v->value, sizeof(defaultozz));
			} 
		} else 
			cw_log(CW_LOG_WARNING, "Ignoring %s\n", v->name);
		v = v->next;
	}
	if (!found_pseudo && reload == 0) {
	
		/* Make sure pseudo isn't a member of any groups if
		   we're automatically making it. */	
		cur_group = 0;
		cur_callergroup = 0;
		cur_pickupgroup = 0;
	
		tmp = mkintf(CHAN_PSEUDO, cur_signalling, cur_radio, NULL, reload);

		if (tmp) {
			if (option_verbose > 2)
				cw_verbose(VERBOSE_PREFIX_3 "Automatically generated pseudo channel\n");
		} else {
			cw_log(CW_LOG_WARNING, "Unable to register pseudo channel!\n");
		}
	}
	cw_mutex_unlock(&iflock);
	cw_config_destroy(cfg);
#ifdef ZAPATA_PRI
	if (!reload) {
		for (x=0;x<NUM_SPANS;x++) {
			if (pris[x].pvts[0]) {
				if (start_pri(pris + x)) {
					cw_log(CW_LOG_ERROR, "Unable to start D-channel on span %d\n", x + 1);
					return -1;
				} else if (option_verbose > 1)
					cw_verbose(VERBOSE_PREFIX_2 "Starting D-Channel on span %d\n", x + 1);
			}
		}
	}
#endif
	/* And start the monitor for the first time */
	restart_monitor();
	return 0;
}

static int load_module(void)
{
	int res;

#ifdef ZAPATA_PRI
	int y,i;
	memset(pris, 0, sizeof(pris));
	for (y=0;y<NUM_SPANS;y++) {
		cw_mutex_init(&pris[y].lock);
		pris[y].offset = -1;
		pris[y].master = CW_PTHREADT_NULL;
		for (i=0;i<NUM_DCHANS;i++)
			pris[y].fds[i] = -1;
	}
	pri_set_error(dahdi_pri_error);
	pri_set_message(dahdi_pri_message);
#endif
	res = setup_dahdi(0);
	if (res)
		return -1;

	if (cw_channel_register(&dahdi_tech)) {
		cw_log(CW_LOG_ERROR, "Unable to register channel class %s\n", dahdi_tech.type);
		return -1;
	}

#ifdef ZAPATA_PRI
	cw_cli_register_multiple(dahdi_pri_cli, arraysize(dahdi_pri_cli));
#endif
	cw_cli_register_multiple(dahdi_cli, arraysize(dahdi_cli));
	
	memset(round_robin, 0, sizeof(round_robin));

	cw_manager_action_register_multiple(manager_actions, arraysize(manager_actions));
	dahdidisableec_app = cw_register_function(dahdidisableec_name, action_dahdidisableec, dahdidisableec_synopsis, dahdidisableec_syntax, dahdidisableec_description);

	return res;
}

static int dahdi_sendtext(struct cw_channel *c, const char *text)
{
	uint8_t *buf,*mybuf;
	struct dahdi_pvt *p = c->tech_pvt;
	struct pollfd fds[1];
	int size,res,fd,len;
	//int bytes=0, x=0;
	int curindex;

	curindex = dahdi_get_index(c, p, 0);
	if (curindex < 0) {
		cw_log(CW_LOG_WARNING, "Huh?  I don't exist?\n");
		return -1;
	}
	if (!text[0]) return(0); /* if nothing to send, dont */
	if ((!p->tdd) && (!p->mate))
		return(0);  /* if not in TDD mode, just return */

	len = (p->tdd ? MAX_CALLERID_SIZE*3 : MAX_CALLERID_SIZE);
	buf = malloc(len);
	if (!buf) {
		cw_log(CW_LOG_ERROR, "MALLOC FAILED\n");
		return -1;
	}
	mybuf = buf;
	if (p->mate) {
		len = mate_generate(mybuf, len, text, CW_LAW(p));
		buf = mybuf;
	}
	else {
		len = tdd_generate(p->tdd, buf, len, text, CW_LAW(p));
		if (len < 1) {
			cw_log(CW_LOG_ERROR, "TDD generate (len %d) failed!!\n",(int)strlen(text));
			free(mybuf);
			return -1;
		}
	}

	fd = p->subs[curindex].dfd;
	while(len) {
		if (cw_check_hangup(c)) {
			free(mybuf);
			return -1;
		}
		size = len;
		if (size > READ_SIZE)
			size = READ_SIZE;
		fds[0].fd = fd;
		fds[0].events = POLLOUT | POLLPRI;
		fds[0].revents = 0;
		res = poll(fds, 1, -1);
		if (!res) {
			cw_log(CW_LOG_DEBUG, "poll (for write) ret. 0 on channel %d\n", p->channel);
			continue;
		}
		  /* if got exception */
		if (fds[0].revents & POLLPRI) return -1;
		if (!(fds[0].revents & POLLOUT)) {
			cw_log(CW_LOG_DEBUG, "write fd not ready on channel %d\n", p->channel);
			continue;
		}
		res = write(fd, buf, size);
		if (res != size) {
			if (res == -1) {
				free(mybuf);
				return -1;
			}
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
			break;
		}
		len -= size;
		buf += size;
	}
	free(mybuf);

	/* FIXME: we should do 50ms trailing silence now */

	return(0);
}

static int reload_module(void)
{
	if (setup_dahdi(1))
	{
		cw_log(CW_LOG_WARNING, "Reload of chan_zap.so is unsuccessful!\n");
		return -1;
	}
	return 0;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, desc)
