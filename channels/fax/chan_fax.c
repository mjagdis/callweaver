/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Fax Channel Driver
 * 
 * Copyright (C) 2008, Eris Associates Limited, UK
 * Copyright (C) 2005 Anthony Minessale II
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
 * Anthony Minessale II <anthmct@yahoo.com>
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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <netinet/tcp.h>

#include <spandsp.h>

#include "callweaver/lock.h"
#include "callweaver/cli.h"
#include "callweaver/channel.h"
#include "callweaver/options.h"
#include "callweaver/logger.h"
#include "callweaver/astobj.h"
#include "callweaver/atexit.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"
#include "callweaver/pbx.h"
#include "callweaver/devicestate.h"
#include "callweaver/phone_no_utils.h"


static const char desc[] = "Fax Modem Interface";
static const char type[] = "Fax";
static const char tdesc[] = "Fax Modem Interface";


#define CONFIGFILE "chan_fax.conf"
#define SAMPLES 160


/*! Whether trace/debug code should be compiled in.
 * You almost certainly want this.
 */
#define TRACE

/*! Prefix for the names of trace file created when TRACE support
 * is compiled in and vblevel is set sufficiently high.
 * The prefix will have dte<unit> and fax<unit>-{rx,tx} appended
 * to create the actual names.
 */
#define TRACE_PREFIX	"/tmp/faxmodem-"


#define DEFAULT_MAX_FAXMODEMS	4
#define DEFAULT_DEV_PREFIX	"/dev/FAX"
#define DEFAULT_TIMEOUT		30000
#define DEFAULT_RING_STRATEGY	0
#define DEFAULT_CONTEXT		"chan_fax"
#define DEFAULT_VBLEVEL		0


static int cfg_modems;
static int cfg_ringstrategy;
static char *cfg_dev_prefix;
static char *cfg_context;
static int cfg_vblevel;
static char *cfg_cid_name;
static char *cfg_cid_num;


typedef enum {
	FAXMODEM_STATE_CLOSED = 0,
	FAXMODEM_STATE_ONHOOK,
	FAXMODEM_STATE_OFFHOOK,
	FAXMODEM_STATE_ACQUIRED,
	FAXMODEM_STATE_RINGING,
	FAXMODEM_STATE_CALLING,
	FAXMODEM_STATE_CONNECTED,
} faxmodem_state_t;

static const char *faxmodem_state[] =
{
	[FAXMODEM_STATE_CLOSED] =	"CLOSED",
	[FAXMODEM_STATE_ONHOOK] =	"ONHOOK",
	[FAXMODEM_STATE_OFFHOOK] =	"OFFHOOK",
	[FAXMODEM_STATE_ACQUIRED] =	"ACQUIRED",
	[FAXMODEM_STATE_RINGING] =	"RINGING",
	[FAXMODEM_STATE_CALLING] =	"CALLING",
	[FAXMODEM_STATE_CONNECTED] =	"CONNECTED",
};


struct faxmodem;

struct faxmodem {
	cw_mutex_t lock;
	cw_cond_t event;
	struct pollfd pfd;
	struct timespec tick;
	faxmodem_state_t state;
	t31_state_t t31_state;
	struct cw_frame frame;
	uint8_t fdata[CW_FRIENDLY_OFFSET + SAMPLES * sizeof(int16_t)];
	struct cw_channel *owner;
	char *cid_name;
	char *cid_num;
	int unit;
	pthread_t thread;
	pthread_t poll_thread;
	char devlink[128];
#ifdef TRACE
	struct timespec start;
	int debug_fax[2];
	int debug_dte;
#endif
};


static struct faxmodem *FAXMODEM_POOL;


#define IO_READ		"0"


static struct cw_frame frame_cng = {
	.frametype = CW_FRAME_DTMF,
	.subclass = 'f',
};


static int rr_next;


CW_MUTEX_DEFINE_STATIC(control_lock);


#if defined(_POSIX_TIMERS)
#if defined(_POSIX_MONOTONIC_CLOCK) && defined(__USE_XOPEN2K)
static clockid_t global_clock_monotonic = CLOCK_MONOTONIC;
#else
static clockid_t global_clock_monotonic = CLOCK_REALTIME;
#endif

static void cw_clock_init(void)
{
	struct timespec ts;

	if (clock_gettime(global_clock_monotonic, &ts))
		global_clock_monotonic = CLOCK_REALTIME;
}

#define cw_clock_gettime(clock_id, timespec_p) clock_gettime((clock_id), (timespec_p))
#else
struct timespec {
	time_t tv_sec;
	long tv_nsec;
};

#define global_clock_monotonic 0

#define cw_clock_init()

static inline int cw_clock_gettime(int clk, struct timespec *ts)
{
	struct timeval tv;

	if (!gettimeofday(&tv, NULL)) {
		ts->tv_sec = tv.tv_sec;
		ts->tv_nsec = 1000L * tv.tv_usec;
		return 0;
	}
	return -1;
}
#endif


static inline int cw_clock_diff_ms(struct timespec *end, struct timespec *start)
{
	return (end->tv_sec - start->tv_sec) * 1000L
		+ ((1000000000L + end->tv_nsec - start->tv_nsec) / 1000000) - 1000L;
}


static inline void cw_clock_add_ms(struct timespec *ts, int ms)
{
	ts->tv_nsec += 1000000L * ms;
	while (ts->tv_nsec >= 1000000000L) {
		ts->tv_nsec -= 1000000000L;
		ts->tv_sec++;
	}
}


/********************CHANNEL METHOD PROTOTYPES********************/
static struct cw_channel *tech_requester(const char *type, int format, void *data, int *cause);
static int tech_devicestate(void *data);
static int tech_send_digit(struct cw_channel *self, char digit);
static int tech_call(struct cw_channel *self, char *dest, int timeout);
static int tech_hangup(struct cw_channel *self);
static int tech_answer(struct cw_channel *self);
static struct cw_frame *tech_read(struct cw_channel *self);
static struct cw_frame *tech_exception(struct cw_channel *self);
static int tech_write(struct cw_channel *self, struct cw_frame *frame);
static int tech_indicate(struct cw_channel *self, int condition);
static int tech_fixup(struct cw_channel *oldchan, struct cw_channel *newchan);
static int tech_send_html(struct cw_channel *self, int subclass, const char *data, int datalen);
static int tech_send_text(struct cw_channel *self, const char *text);
static int tech_send_image(struct cw_channel *self, struct cw_frame *frame);

/* Helper Function Prototypes */
static struct cw_channel *channel_new(struct faxmodem *fm);
static int modem_control_handler(t31_state_t *t31, void *user_data, int op, const char *num);
static void *faxmodem_thread(void *obj);
static void activate_fax_modems(void);
static void deactivate_fax_modems(void);


static const struct cw_channel_tech technology = {
	.type = type,
	.description = tdesc,
	.capabilities = -1,
	.requester = tech_requester,
	.devicestate = tech_devicestate,
	.send_digit = tech_send_digit,
	.call = tech_call,
	.hangup = tech_hangup,
	.answer = tech_answer,
	.read = tech_read,
	.write = tech_write,
	.exception = tech_exception,
	.indicate = tech_indicate,
	.fixup = tech_fixup,
	.send_html = tech_send_html,
	.send_text = tech_send_text,
	.send_image = tech_send_image,
};


static void spandsp(int level, const char *msg)
{
	if (level == SPAN_LOG_ERROR)
		cw_log(CW_LOG_ERROR, "%s", msg);
	else if (level == CW_LOG_WARNING)
		cw_log(CW_LOG_WARNING, "%s", msg);
	else
		cw_log(CW_LOG_DEBUG, "%s", msg);
}


static int t31_at_tx_handler(at_state_t *s, void *user_data, const uint8_t *buf, size_t len)
{
	struct faxmodem *fm = user_data;

#ifdef TRACE
	if (cfg_vblevel > 2 && fm->debug_dte) {
		char msg[256];
		unsigned char *p;
		int togo = len;
		ssize_t n;

		for (p = (unsigned char *)buf; togo >= 2; p++, togo--) {
			if (p[0] > ' ' && p[0] <= 127 && p[1] > ' ' && p[1] <= 127) {
				unsigned char *q;

				for (q = p + 2, togo -= 2; togo && *q >= ' ' && *q <= 127; q++, togo--);
				cw_log(CW_LOG_DEBUG, "%s -> %.*s\n", fm->devlink, q - p, p);
				n = snprintf(msg, sizeof(msg), "-> %.*s\n", q - p, p);
				write(fm->debug_dte, msg, n);
				p = q;
			}
			p++, togo--;
		}
	}
#endif

	if (write(fm->pfd.fd, buf, len) != len) {
		cw_log(CW_LOG_ERROR, "%s: DTE overrun - failed to write all of %d bytes\n", fm->devlink, len);
		tcflush(fm->pfd.fd, TCOFLUSH);
	}

	return len;
}


/* channel_new() make a new channel and fit it with a private object */
static struct cw_channel *channel_new(struct faxmodem *fm)
{
	struct cw_channel *chan = NULL;

	if (!(chan = cw_channel_alloc(1))) {
		cw_log(CW_LOG_ERROR, "%s: can't allocate a channel\n", fm->devlink);
	} else {
		chan->type = type;
		chan->tech = &technology;
		chan->tech_pvt = fm;
		snprintf(chan->name, sizeof(chan->name), "%s/%d-%04lx", type, fm->unit, cw_random() & 0xffff);
		chan->writeformat = chan->rawwriteformat = chan->readformat = chan->nativeformats = CW_FORMAT_SLINEAR;

		if (fm->cid_name)
			chan->cid.cid_name = strdup(fm->cid_name);
		else if (cfg_cid_name)
			chan->cid.cid_name = strdup(cfg_cid_name);

		if (fm->cid_num)
			chan->cid.cid_num = strdup(fm->cid_num);
		else if (cfg_cid_num)
			chan->cid.cid_num = strdup(cfg_cid_num);

		fm->owner = chan;
#ifdef TRACE
		if (cfg_vblevel > 3) {
			char buf[256];
			snprintf(buf, sizeof(buf), "%sfax%d-rx.sln", TRACE_PREFIX, fm->unit);
			fm->debug_fax[0] = open(buf, O_RDWR | O_CREAT | O_TRUNC, 0666);
			snprintf(buf, sizeof(buf), "%sfax%d-tx.sln", TRACE_PREFIX, fm->unit);
			fm->debug_fax[1] = open(buf, O_RDWR | O_CREAT | O_TRUNC, 0666);
		}
		cw_clock_gettime(CLOCK_REALTIME, &fm->start);
#endif
	}
	
	return chan;
}


/********************CHANNEL METHOD LIBRARY********************
 * This is the actual functions described by the prototypes above.
 *
 */

/*--- tech_requester: parse 'data' a url-like destination string, allocate a channel and a private structure
 * and return the newly-setup channel.
 */
static struct cw_channel *tech_requester(const char *type, int format, void *data, int *cause)
{
	struct cw_channel *chan = NULL;
	struct faxmodem *fm;
	int unit = -1;
	char *p = data, *q;

	if ((q = strchr(p, '/')))
		p = q + 1;
	if (isdigit(*p))
		unit = atoi(p);

	cw_mutex_lock(&control_lock);

	if (FAXMODEM_POOL) {
		if (unit >= 0 && unit < cfg_modems) {
			fm = &FAXMODEM_POOL[unit];
		} else {
			int x;

			if (cfg_ringstrategy == 1)
				rr_next = 0;

			for (x = 0; x < cfg_modems; x++) {
				unit = (rr_next + x) % cfg_modems;
				cw_mutex_lock(&FAXMODEM_POOL[unit].lock);
				cw_log(CW_LOG_DEBUG, "acquire considering: unit %d state: %s\n", unit, faxmodem_state[FAXMODEM_POOL[unit].state]);
				if (FAXMODEM_POOL[unit].state == FAXMODEM_STATE_ONHOOK) {
					fm = &FAXMODEM_POOL[unit];
					fm->state = FAXMODEM_STATE_ACQUIRED;
					break;
				}
				cw_mutex_unlock(&FAXMODEM_POOL[unit].lock);
			}
			rr_next = (rr_next + 1) % cfg_modems;
		}
	}

	cw_mutex_unlock(&control_lock);

	if (fm) {
		if ((chan = channel_new(fm))) {
#ifdef TRACE
			if (fm->debug_dte < 0 && cfg_vblevel > 2) {
				char buf[256];
				snprintf(buf, sizeof(buf), "%sdte%d", TRACE_PREFIX, fm->unit);
				fm->debug_dte = open(buf, O_RDWR | O_CREAT | O_TRUNC, 0666);
			}
		} else {
#endif
			cw_log(CW_LOG_ERROR, "%s: can't allocate a channel\n", fm->devlink);
			fm->state = FAXMODEM_STATE_ONHOOK;
		}
		cw_mutex_unlock(&FAXMODEM_POOL[unit].lock);
	} else {
		cw_log(CW_LOG_ERROR, "No Modems Available!\n");
	}

	return chan;
}

/*--- tech_devicestate: Part of the device state notification system ---*/
static int tech_devicestate(void *data)
{
	/* return one of.........
	 * CW_DEVICE_UNKNOWN
	 * CW_DEVICE_NOT_INUSE
	 * CW_DEVICE_INUSE
	 * CW_DEVICE_BUSY
	 * CW_DEVICE_INVALID
	 * CW_DEVICE_UNAVAILABLE
	 */

	int res = CW_DEVICE_UNKNOWN;

	return res;
}



/*--- tech_senddigit: Send a DTMF character */
static int tech_send_digit(struct cw_channel *self, char digit)
{
	return 0;
}

/*--- tech_call: Initiate a call on my channel 
 * 'dest' has been passed telling you where to call
 * but you may already have that information from the requester method
 * not sure why it sends it twice, maybe it changed since then *shrug*
 * You also have timeout (in ms) so you can tell how long the caller
 * is willing to wait for the call to be complete.
 */

static int tech_call(struct cw_channel *self, char *dest, int timeout)
{
	struct tm tm;
	char buf[sizeof("0000+0000")];
	struct faxmodem *fm;
	time_t u_now;

	fm = self->tech_pvt;

	/* Next DTE alerting event will be... _now_ */
	cw_clock_gettime(CLOCK_REALTIME, &fm->tick);

	strftime(buf, sizeof(buf), "%m%d+%H%M", localtime_r(&fm->tick.tv_sec, &tm));
	buf[4] = '\000';

	cw_mutex_lock(&fm->lock);
	at_reset_call_info(&fm->t31_state.at_state);
	at_set_call_info(&fm->t31_state.at_state, "DATE", buf);
	at_set_call_info(&fm->t31_state.at_state, "TIME", buf+5);
	if ((self->cid.cid_pres & CW_PRES_RESTRICTION) == CW_PRES_ALLOWED) {
		at_set_call_info(&fm->t31_state.at_state, "NAME", self->cid.cid_name);
		at_set_call_info(&fm->t31_state.at_state, "NMBR", self->cid.cid_num);
	} else if ((self->cid.cid_pres & CW_PRES_RESTRICTION) == CW_PRES_RESTRICTED)
		at_set_call_info(&fm->t31_state.at_state, "NMBR", "P");
	else
		at_set_call_info(&fm->t31_state.at_state, "NMBR", "O");
	at_set_call_info(&fm->t31_state.at_state, "ANID", self->cid.cid_ani);
	at_set_call_info(&fm->t31_state.at_state, "NDID", self->cid.cid_dnid);

	fm->state = FAXMODEM_STATE_RINGING;
	cw_setstate(self, CW_STATE_RINGING);
	cw_cond_broadcast(&fm->event);
	cw_mutex_unlock(&fm->lock);

	return 0;
}

/*--- tech_hangup: end a call on my channel 
 * Now is your chance to tear down and free the private object
 * from the channel it's about to be freed so you must do so now
 * or the object is lost.  Well I guess you could tag it for reuse
 * or for destruction and let a monitor thread deal with it too.
 * during the load_module routine you have every right to start up
 * your own fancy schmancy bunch of threads or whatever else 
 * you want to do.
 */
static int tech_hangup(struct cw_channel *self)
{
	struct faxmodem *fm = self->tech_pvt;

	cw_mutex_lock(&fm->lock);
#ifdef TRACE
	if (fm->debug_fax[0] >= 0)
		close(fm->debug_fax[0]);
	if (fm->debug_fax[1] >= 0)
		close(fm->debug_fax[1]);
	if (fm->debug_dte >= 0)
		close(fm->debug_dte);
#endif
	if (fm->state == FAXMODEM_STATE_CALLING)
		t31_call_event(&fm->t31_state, AT_CALL_EVENT_NO_ANSWER);
	else if (fm->state != FAXMODEM_STATE_CLOSED && fm->state != FAXMODEM_STATE_ONHOOK)
		t31_call_event(&fm->t31_state, AT_CALL_EVENT_HANGUP);

	fm->owner = NULL;

	pthread_setschedparam(fm->thread, SCHED_OTHER, &global_sched_param_default);
	fm->state = FAXMODEM_STATE_ONHOOK;

	cw_mutex_unlock(&fm->lock);

	self->tech_pvt = NULL;

	return 0;
}


/*--- tech_answer: answer a call on my channel
 * if being 'answered' means anything special to your channel
 * now is your chance to do it!
 */
static int tech_answer(struct cw_channel *self)
{
	pthread_t tid;
	struct faxmodem *fm = self->tech_pvt;

	if (cfg_vblevel > 0)
		cw_log(CW_LOG_DEBUG, "%s: connected\n", fm->devlink);

	cw_mutex_lock(&fm->lock);

	pthread_setschedparam(fm->thread, SCHED_RR, &global_sched_param_rr);
	fm->state = FAXMODEM_STATE_CONNECTED;
	t31_call_event(&fm->t31_state, AT_CALL_EVENT_CONNECTED);
	fm->frame.ts = fm->frame.seq_no = 0;
	cw_clock_gettime(CLOCK_REALTIME, &fm->tick);
	cw_clock_add_ms(&fm->tick, SAMPLES / 8);

#if 0
	cw_queue_frame(self, &frame_cng);
#endif

	cw_cond_broadcast(&fm->event);
	cw_mutex_unlock(&fm->lock);

	return 0;
}


/*--- tech_read: Read an audio frame from my channel.
 * You need to read data from your channel and convert/transfer the
 * data into a newly allocated struct cw_frame object
 */
static struct cw_frame *tech_read(struct cw_channel *self)
{
	struct faxmodem *fm = self->tech_pvt;

	return &fm->frame;
}

/*--- tech_write: Write an audio frame to my channel
 * Yep, this is the opposite of tech_read, you need to examine
 * a frame and transfer the data to your technology's audio stream.
 * You do not have any responsibility to destroy this frame and you should
 * consider it to be read-only.
 */

static int tech_write(struct cw_channel *self, struct cw_frame *frame)
{
#ifdef TRACE
	char msg[256];
	struct timespec now;
#endif
	struct faxmodem *fm = self->tech_pvt;

	if (fm->state == FAXMODEM_STATE_CONNECTED) {
		switch (frame->frametype) {
			case CW_FRAME_VOICE:
				cw_mutex_lock(&fm->lock);
#ifdef TRACE
				if (cfg_vblevel > 3) {
					uint16_t *data = frame->data;
					int n;

					cw_clock_gettime(CLOCK_REALTIME, &now);
					n = cw_clock_diff_ms(&now, &fm->start);
					write(fm->debug_fax[0], frame->data, frame->datalen);
					n = snprintf(msg, sizeof(msg), "-> audio: %dms %d samples: 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x...\n", n, frame->samples, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
					write(fm->debug_dte, msg, n);
				}
#endif

				t31_rx(&fm->t31_state, frame->data, frame->samples);
				cw_mutex_unlock(&fm->lock);
				break;

			case CW_FRAME_CNG: {
				static int16_t silence[SAMPLES];
				int samples = frame->len * 8;

				cw_mutex_lock(&fm->lock);
#ifdef TRACE
				if (cfg_vblevel > 3) {
					int n;
					int i = samples;

					cw_clock_gettime(CLOCK_REALTIME, &now);
					n = cw_clock_diff_ms(&now, &fm->start);
					for (; i >= SAMPLES; i -= SAMPLES)
						write(fm->debug_fax[0], silence, SAMPLES * sizeof(int16_t));
					if (i)
						write(fm->debug_fax[0], silence, samples * sizeof(int16_t));
					n = snprintf(msg, sizeof(msg), "-> audio: %dms %d samples: silence (%dms)\n", n, samples, frame->len);
					write(fm->debug_dte, msg, n);
				}
#endif
				for (; samples >= SAMPLES; samples -= SAMPLES)
					t31_rx(&fm->t31_state, silence, SAMPLES);
				if (samples)
					t31_rx(&fm->t31_state, silence, samples);
				cw_mutex_unlock(&fm->lock);
				break;
			}
		}
	}

	return 0;
}

/*--- tech_exception: Read an exception audio frame from my channel ---*/
static struct cw_frame *tech_exception(struct cw_channel *self)
{
	return NULL;
}

/*--- tech_indicate: Indicate a condition to my channel ---*/
static int tech_indicate(struct cw_channel *self, int condition)
{
	struct faxmodem *fm = self->tech_pvt;
	int res = 0;

        if (cfg_vblevel > 1)
                cw_log(CW_LOG_DEBUG, "%s: indication %d\n", fm->devlink, condition);

	switch (condition) {
		case -1:
		case CW_CONTROL_RINGING:
		case CW_CONTROL_ANSWER:
		case CW_CONTROL_PROGRESS:
		case CW_CONTROL_VIDUPDATE:
			break;
		case CW_CONTROL_BUSY:
		case CW_CONTROL_CONGESTION:
			cw_mutex_lock(&fm->lock);
			t31_call_event(&fm->t31_state, AT_CALL_EVENT_BUSY);
			/* N.B. Just because we are told to indicate busy is no reason
			 * to assume we were never connected.
			 */
			pthread_setschedparam(fm->thread, SCHED_OTHER, &global_sched_param_default);
			fm->state = FAXMODEM_STATE_ONHOOK;
			cw_cond_broadcast(&fm->event);
			cw_mutex_unlock(&fm->lock);
			break;
		default:
			if (cfg_vblevel > 1)
				cw_log(CW_LOG_WARNING, "%s: unknown indication %d\n", fm->devlink, condition);
	}

	return res;
}

/*--- tech_fixup: add any finishing touches to my channel if it is masqueraded---*/
static int tech_fixup(struct cw_channel *oldchan, struct cw_channel *newchan)
{
	return 0;
}

/*--- tech_send_html: Send html data on my channel ---*/
static int tech_send_html(struct cw_channel *self, int subclass, const char *data, int datalen)
{
	return 0;
}

/*--- tech_send_text: Send plain text data on my channel ---*/
static int tech_send_text(struct cw_channel *self, const char *text)
{
	return 0;
}

/*--- tech_send_image: Send image data on my channel ---*/
static int tech_send_image(struct cw_channel *self, struct cw_frame *frame) 
{
	return 0;
}


static int modem_control_handler(t31_state_t *t31, void *user_data, int op, const char *num)
{
	struct faxmodem *fm = user_data;
	struct cw_channel *chan = NULL;
	int res = 0;

	cw_mutex_lock(&fm->lock);

	switch (op) {
		case AT_MODEM_CONTROL_CALL:
			if (fm->state != FAXMODEM_STATE_ONHOOK && fm->state != FAXMODEM_STATE_OFFHOOK) {
				cw_log(CW_LOG_ERROR, "%s: invalid state %s!\n", fm->devlink, faxmodem_state[fm->state]);
				res = -1;
			} else if (!(chan = channel_new(fm))) {
				cw_log(CW_LOG_ERROR, "%s: can't allocate a channel\n", fm->devlink);
				res = -1;
			} else {
				struct faxmodem *fm = chan->tech_pvt;

				cw_copy_string(chan->context, cfg_context, sizeof(chan->context));
				cw_copy_string(chan->exten, num, sizeof(chan->exten));
#ifdef DOTRACE
				fm->debug[0] = open("/tmp/cap-in.raw", O_WRONLY|O_CREAT, 00660);
				fm->debug[1] = open("/tmp/cap-out.raw", O_WRONLY|O_CREAT, 00660);
#endif
				if (cfg_vblevel > 0)
					cw_log(CW_LOG_DEBUG, "%s: calling %s@%s\n", fm->devlink, chan->exten, chan->context);

				fm->state = FAXMODEM_STATE_CALLING;
				cw_setstate(chan, CW_STATE_RINGING);
				if (cw_pbx_start(chan)) {
					cw_log(CW_LOG_ERROR, "%s: unable to start PBX\n", fm->devlink);
					cw_hangup(chan);
				}
			}
			break;

		case AT_MODEM_CONTROL_OFFHOOK:
			if (cfg_vblevel > 0)
				cw_log(CW_LOG_DEBUG, "%s: off hook\n", fm->devlink);

			if (fm->state == FAXMODEM_STATE_ONHOOK) {
				fm->state = FAXMODEM_STATE_OFFHOOK;
				break;
			} else if (fm->state != FAXMODEM_STATE_RINGING) {
				res -1;
				break;
			}
			/* Drop through to answer */

		case AT_MODEM_CONTROL_ANSWER:
			if (fm->state != FAXMODEM_STATE_RINGING) {
				cw_log(CW_LOG_ERROR, "%s: invalid state %s!\n", fm->devlink, faxmodem_state[fm->state]);
				res = -1;
			} else {
				if (cfg_vblevel > 0)
					cw_log(CW_LOG_DEBUG, "%s: answered\n", fm->devlink);

				t31_call_event(&fm->t31_state, AT_CALL_EVENT_ANSWERED);
				fm->frame.ts = fm->frame.seq_no = 0;

				pthread_setschedparam(fm->thread, SCHED_RR, &global_sched_param_rr);
				fm->state = FAXMODEM_STATE_CONNECTED;
				cw_setstate(fm->owner, CW_STATE_UP);

				cw_clock_gettime(CLOCK_REALTIME, &fm->tick);
				cw_clock_add_ms(&fm->tick, SAMPLES / 8);
			}
			break;

		case AT_MODEM_CONTROL_HANGUP:
			if (cfg_vblevel > 0)
				cw_log(CW_LOG_DEBUG, "%s: hang up\n", fm->devlink);

			pthread_setschedparam(fm->thread, SCHED_OTHER, &global_sched_param_default);
			fm->state = FAXMODEM_STATE_ONHOOK;
			if (fm->owner)
				cw_softhangup(fm->owner, CW_SOFTHANGUP_EXPLICIT);
			break;

		case AT_MODEM_CONTROL_RNG:
			if (cfg_vblevel > 0)
				cw_log(CW_LOG_DEBUG, "%s: ringing\n", fm->devlink);
			break;

		case AT_MODEM_CONTROL_CTS:
			if (cfg_vblevel > 1)
				cw_log(CW_LOG_DEBUG, "%s: flow control %s\n", fm->devlink, (num ? "on" : "off"));

			t31_at_tx_handler(&fm->t31_state.at_state, fm, (num ? "\021" : "\023"), 1);
			break;

		case AT_MODEM_CONTROL_SETID:
			if (fm->cid_name) {
				free(fm->cid_name);
				fm->cid_name = NULL;
			}
			if (fm->cid_num) {
				free(fm->cid_num);
				fm->cid_num = NULL;
			}
			if (num && *num) {
				const char *p, *q;

				while (*num == ' ') num++;

				q = num + strlen(num) - 1;
				while (*q == ' ') q--;

				if ((p = strstr(num, "\",\""))) {
					const char *r = p;
					if (*num == '"') num++,r--;
					if ((fm->cid_name = malloc(r - num + 1 + 1))) {
						memcpy(fm->cid_name, num, r - num + 1);
						fm->cid_name[r - num + 1] = '\0';
					}
					num = p + 2;
				}
				if (*num == '"' && *q == '"') num++,q--;
				if ((fm->cid_num = malloc(q - num + 1 + 1))) {
					memcpy(fm->cid_num, num, q - num + 1);
					fm->cid_num[q - num + 1] = '\0';
				}
			}
			break;

		default:
			if (cfg_vblevel > 0)
				cw_log(CW_LOG_DEBUG, "%s: unknown control op = %d\n", fm->devlink, op);
			break;

	}

	cw_cond_broadcast(&fm->event);
	cw_mutex_unlock(&fm->lock);
	return res;
}


static void faxmodem_poll_thread_cleanup(void *obj)
{
	struct faxmodem *fm = obj;

	cw_mutex_unlock(&fm->lock);
}

static void *faxmodem_poll_thread(void *obj)
{
	struct faxmodem *fm = obj;

	cw_mutex_lock(&fm->lock);
	for (;;) {
		if (fm->state == FAXMODEM_STATE_CONNECTED) {
			pthread_cleanup_push(faxmodem_poll_thread_cleanup, fm);
			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
			cw_cond_wait(&fm->event, &fm->lock);
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
			pthread_cleanup_pop(0);
		} else {
			int res;

			cw_mutex_unlock(&fm->lock);
			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
			res = poll(&fm->pfd, 1, -1);
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
			cw_mutex_lock(&fm->lock);
			if (res == 1) {
				if (fm->pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
					if (fm->state == FAXMODEM_STATE_CLOSED) {
						sleep(1);
						continue;
					}
					fm->state = FAXMODEM_STATE_CLOSED;
				} else if (fm->state == FAXMODEM_STATE_CLOSED)
					fm->state = FAXMODEM_STATE_ONHOOK;
				cw_cond_broadcast(&fm->event);
			}
		}
	}
}


static void faxmodem_thread_cleanup(void *obj)
{
	struct faxmodem *fm = obj;

	fm->state = FAXMODEM_STATE_CLOSED;

	if (!pthread_equal(fm->poll_thread, CW_PTHREADT_NULL))
		pthread_cancel(fm->poll_thread);
	cw_mutex_unlock(&fm->lock);
	if (!pthread_equal(fm->poll_thread, CW_PTHREADT_NULL))
		pthread_join(fm->poll_thread, NULL);

	close(fm->pfd.fd);
	unlink(fm->devlink);

	cw_mutex_destroy(&fm->lock);
	cw_cond_destroy(&fm->event);

	if (cfg_vblevel > 1)
		cw_log(CW_LOG_DEBUG, "%s: thread ended\n", fm->devlink);
}

static void *faxmodem_thread(void *obj)
{
	uint8_t modembuf[T31_TX_BUF_LEN];
	char buf[256];
	struct faxmodem *fm = obj;
	int avail;
	int ret;

#ifdef HAVE_POSIX_OPENPT
	if ((fm->pfd.fd = posix_openpt(O_RDWR | O_NOCTTY)) < 0) {
		cw_log(CW_LOG_ERROR, "%s: failed to get a pty: %s\n", fm->devlink, strerror(errno));
		return NULL;
	}

	/* The behaviour of grantpt is undefined if a SIGCHLD handler is installed.
	 * We can't guarantee that, but grantpt just sets permissions on the slave
	 * tty and since we expect that to be opened by a root owned faxgetty we
	 * can live without doing this.
	 */
	// grantpt(fm->pfd.fd);
	unlockpt(fm->pfd.fd);
#else
	fm->pfd.fd = -1;

	if (openpty(&fm->pfd.fd, &ret, NULL, NULL, NULL)) {
		cw_log(CW_LOG_ERROR, "%s: failed to get a pty: %s\n", fm->devlink, strerror(errno));
		return NULL;
	}

	/* If we keep the slave open we'll likely get killed by a {fax}getty
	 * start up on it. Closing it means we're going to keep seeing POLLHUP
	 * until something else opens it. See channel/fax/chan_fax.c
	 */
	close(ret);
#endif
	ptsname_r(fm->pfd.fd, buf, sizeof(buf));

	if (cfg_vblevel > 1)
		cw_log(CW_LOG_DEBUG, "%s: opened pty, slave device: %s\n", fm->devlink, buf);

	if (!unlink(fm->devlink) && cfg_vblevel > 1)
		cw_log(CW_LOG_WARNING, "%s: removed old symbolic link\n", fm->devlink);

	if (fcntl(fm->pfd.fd, F_SETFL, fcntl(fm->pfd.fd, F_GETFL, 0) | O_NONBLOCK)) {
		close(fm->pfd.fd);
		cw_log(CW_LOG_ERROR, "%s: cannot set up non-blocking read on %s\n", fm->devlink, ttyname(fm->pfd.fd));
		return NULL;
	}

	if (t31_init(&fm->t31_state, t31_at_tx_handler, fm, modem_control_handler, fm, 0, 0) < 0) {
		close(fm->pfd.fd);
		cw_log(CW_LOG_ERROR, "%s: cannot initialize the T.31 modem\n", fm->devlink);
		return NULL;
	}

	if (symlink(buf, fm->devlink)) {
		close(fm->pfd.fd);
		cw_log(CW_LOG_ERROR, "%s: failed to create symbolic link\n", fm->devlink);
		return NULL;
	}

	if (cfg_vblevel > 1)
		cw_log(CW_LOG_DEBUG, "%s: created symbolic link\n", fm->devlink);

#ifdef TRACE
	fm->debug_dte = fm->debug_fax[0] = fm->debug_fax[1] = -1;

	if (cfg_vblevel > 2) {
		snprintf(buf, sizeof(buf), "%sdte%d", TRACE_PREFIX, fm->unit);
		fm->debug_dte = open(buf, O_RDWR | O_CREAT | O_TRUNC, 0666);
	}
#endif

	if (cfg_vblevel > 1) {
		span_log_set_message_handler(&fm->t31_state.logging, spandsp);
		span_log_set_level(&fm->t31_state.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_SHOW_VARIANT | SPAN_LOG_DEBUG_3);
	}

	if (cfg_vblevel > 0)
		cw_verbose(VERBOSE_PREFIX_1 "%s: fax modem ready\n", fm->devlink);

	fm->poll_thread = CW_PTHREADT_NULL;
	cw_cond_init(&fm->event, NULL);
	cw_mutex_init(&fm->lock);
	cw_mutex_lock(&fm->lock);
	pthread_cleanup_push(faxmodem_thread_cleanup, fm);

	fm->pfd.events = POLLIN;
	if (cw_pthread_create(&fm->poll_thread, &global_attr_detached, faxmodem_poll_thread, fm))
		return NULL;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	cw_clock_gettime(CLOCK_REALTIME, &fm->tick);

	for (;;) {
		/* When connected we know we are going to get data from the DTE, we know
		 * we want to send it up the stack in 20ms chunks and we know the pty
		 * has a buffer. So we process data when we want it rather than when
		 * it's available.
		 * When ringing we know that either an answer operation will signal the
		 * event condition or the timeout will tell us to re-alert. If the DTE
		 * wants to hang up this will be delayed until the next alert time.
		 */
		if (fm->state == FAXMODEM_STATE_CONNECTED || fm->state == FAXMODEM_STATE_RINGING)
			ret = cw_cond_timedwait(&fm->event, &fm->lock, &fm->tick);
		else
			ret = cw_cond_wait(&fm->event, &fm->lock);

		pthread_testcancel();

		if (fm->state == FAXMODEM_STATE_RINGING) {
			struct timespec now;

			if (ret == ETIMEDOUT) {
				t31_call_event(&fm->t31_state, AT_CALL_EVENT_ALERTING);
				fm->tick.tv_sec += 5;
			}
		}

		if (fm->state == FAXMODEM_STATE_CONNECTED) {
			uint16_t *frame_data = (int16_t *)(fm->fdata + CW_FRIENDLY_OFFSET);
			int samples;

			cw_fr_init_ex(&fm->frame, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
			fm->frame.offset = CW_FRIENDLY_OFFSET;
			fm->frame.data = frame_data;
			memset(frame_data, 0, SAMPLES * sizeof(int16_t));
			fm->frame.samples = 0;
			do {
				samples = t31_tx(&fm->t31_state, frame_data + fm->frame.samples, SAMPLES - fm->frame.samples);
				fm->frame.samples += samples;
			} while (fm->frame.samples < SAMPLES && samples > 0 && fm->state == FAXMODEM_STATE_CONNECTED);

			/* t31_tx can change state */
			if (fm->state == FAXMODEM_STATE_CONNECTED) {
				int blah;
				/* If the frame is short (because t31_tx is changing modem or just gave
				 * in completely) or empty we fill with silence - the other end may
				 * require it.
				 */
				fm->frame.samples = SAMPLES;
				fm->frame.len = SAMPLES / 8;
				fm->frame.ts += SAMPLES;
				fm->frame.seq_no++;
				fm->frame.delivery.tv_sec = fm->tick.tv_sec;
				fm->frame.delivery.tv_usec = fm->tick.tv_nsec / 1000;
				fm->frame.datalen = SAMPLES * sizeof(int16_t);
				/* We should be queuing the frame up for delivery really but we
				 * know the next frame is 20ms away we know how queuing works
				 * and we know the only problem with the read not happening before
				 * the next frame is corruption of the data stream. If the read
				 * can't be handled before the next frame is due we're probably
				 * overloaded and suffering other problems anyway so we'll avoid
				 * the copy and prod the alertpipe directly.
				 */
				//cw_queue_frame(fm->owner, &fm->frame);
				write(fm->owner->alertpipe[1], &blah, sizeof(blah));
#ifdef TRACE
				if (cfg_vblevel > 3) {
					int n;
					struct timespec now;

					write(fm->debug_fax[1], frame_data, fm->frame.datalen);
					n = snprintf(buf, sizeof(buf), "<- audio: %dms %d samples: 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x...\n", cw_clock_diff_ms(&fm->tick, &fm->start), fm->frame.samples, frame_data[0], frame_data[1], frame_data[2], frame_data[3], frame_data[4], frame_data[5], frame_data[6], frame_data[7]);
					write(fm->debug_dte, buf, n);
				}
#endif
			}

			cw_clock_add_ms(&fm->tick, SAMPLES / 8);
		}

		avail = T31_TX_BUF_LEN - fm->t31_state.tx_in_bytes + fm->t31_state.tx_out_bytes - 1;
		if (fm->state != FAXMODEM_STATE_CONNECTED
		|| (fm->state == FAXMODEM_STATE_CONNECTED && !fm->t31_state.tx_holding && avail)) {
			int len;
			while (avail > 0 && (len = read(fm->pfd.fd, modembuf, avail)) > 0) {
#ifdef TRACE
				/* Log AT commands for debugging */
				if (cfg_vblevel > 2 && fm->debug_dte >= 0) {
					char *p;
					int togo = len;
					ssize_t n;

					for (p = modembuf; togo >= 2; p++, togo--) {
						if ((p[0] == 'a' || p[0] == 'A') && (p[1] == 't' || p[1] == 'T')) {
							char *q;

							for (q = p + 2, togo -= 2; togo && *q != '\r' && *q != '\n'; q++, togo--);
							cw_log(CW_LOG_DEBUG, "%s <- %.*s\n", fm->devlink, q - p, p);
							n = snprintf(buf, sizeof(buf), "<- %.*s\n", q - p, p);
							write(fm->debug_dte, buf, n);
							p = q;
						}
						p++, togo--;
					}
				}
#endif
				t31_at_rx((t31_state_t*)&fm->t31_state, modembuf, len);
				avail -= len;
			}
		}
	}

	pthread_cleanup_pop(1);
	return NULL;
}

static void activate_fax_modems(void)
{
	static int NEXT_ID = 0;
	pthread_t tid;
	int x;

	cw_mutex_lock(&control_lock);

	if ((FAXMODEM_POOL = calloc(cfg_modems, sizeof(FAXMODEM_POOL[0])))) {
		for (x = 0; x < cfg_modems; x++) {
			snprintf(FAXMODEM_POOL[x].devlink, sizeof(FAXMODEM_POOL[x].devlink), "%s%d", cfg_dev_prefix, NEXT_ID++);

			if (cfg_vblevel > 1)
				cw_verbose(VERBOSE_PREFIX_1 "Starting Fax Modem %s\n", FAXMODEM_POOL[x].devlink);

			FAXMODEM_POOL[x].unit = x;
			FAXMODEM_POOL[x].state = FAXMODEM_STATE_CLOSED;
			FAXMODEM_POOL[x].thread = CW_PTHREADT_NULL;
			cw_pthread_create(&FAXMODEM_POOL[x].thread, &global_attr_detached, faxmodem_thread, &FAXMODEM_POOL[x]);
		}
	}

	cw_mutex_unlock(&control_lock);
}


static void deactivate_fax_modems(void)
{
	int x;
	
	cw_mutex_lock(&control_lock);

	for(x = 0; x < cfg_modems; x++) {
		if (!pthread_equal(FAXMODEM_POOL[x].thread, CW_PTHREADT_NULL)) {
			if (cfg_vblevel > 1)
				cw_verbose(VERBOSE_PREFIX_1 "Stopping Fax Modem SLOT %d\n", x);
			pthread_cancel(FAXMODEM_POOL[x].thread);
		}
	}

	/* Wait for Threads to die */
	for(x = 0; x < cfg_modems; x++) {
		if (!pthread_equal(FAXMODEM_POOL[x].thread, CW_PTHREADT_NULL)) {
			if (cfg_vblevel > 0)
				cw_verbose(VERBOSE_PREFIX_1 "Stopped Fax Modem SLOT %d\n", x);
			pthread_join(FAXMODEM_POOL[x].thread, NULL);
			FAXMODEM_POOL[x].thread = CW_PTHREADT_NULL;
		}
	}

	free(FAXMODEM_POOL);

	cw_mutex_unlock(&control_lock);

}

static void parse_config(void) {
	struct cw_config *cfg;
	char *entry;
	struct cw_variable *v;

	cfg_vblevel = DEFAULT_VBLEVEL;
	cfg_modems = DEFAULT_MAX_FAXMODEMS;
	cfg_ringstrategy = DEFAULT_RING_STRATEGY;
	if (cfg_dev_prefix)
		free(cfg_dev_prefix);
	cfg_dev_prefix = strdup(DEFAULT_DEV_PREFIX);
	if (cfg_context)
		free(cfg_context);
	cfg_context = strdup(DEFAULT_CONTEXT);

	if ((cfg = cw_config_load(CONFIGFILE))) {
		for (entry = cw_category_browse(cfg, NULL); entry != NULL; entry = cw_category_browse(cfg, entry)) {
			if (!strcasecmp(entry, "settings")) {
				for (v = cw_variable_browse(cfg, entry); v ; v = v->next) {
					if (!strcasecmp(v->name, "context")) {
						if (cfg_context)
							free(cfg_context);
						cfg_context = strdup(v->value);
					} else if (!strcasecmp(v->name, "callerid")) {
						char *name, *num;

						name = num = NULL;
						cw_callerid_parse(v->value, &name, &num);
						if (name)
							cfg_cid_name = strdup(name);
						if (num) {
							cw_shrink_phone_number(num);
							cfg_cid_num = strdup(num);
						}
					} else if (!strcasecmp(v->name, "device-prefix")) {
						if (cfg_dev_prefix)
							free(cfg_dev_prefix);
					        cfg_dev_prefix = strdup(v->value);
					} else if (!strcasecmp(v->name, "modems")) {
						cfg_modems = atoi(v->value);
					} else if (!strcasecmp(v->name, "ring-strategy")) {
					    if (!strcasecmp(v->value, "roundrobin"))
						cfg_ringstrategy = 0;
					    else
						cfg_ringstrategy = 1;
					} else if (!strcasecmp(v->name, "vblevel")) {
						cfg_vblevel = atoi(v->value);

					/* Deprecated options */
					} else if (!strcasecmp(v->name, "timeout-ms")) {
						cw_log(CW_LOG_WARNING, "timeout-ms is deprecated - remove it from your chan_fax.conf\n");
					} else if (!strcasecmp(v->name, "trap-seg")) {
						cw_log(CW_LOG_WARNING, "trap-seg is deprecated - remove it from your chan_fax.conf\n");
					}
				}
			}
		}
		cw_config_destroy(cfg);
	}
}


static int chan_fax_status(int fd, int argc, char *argv[]) 
{
	int x;

	cw_mutex_lock(&control_lock);

	for (x = 0; x < cfg_modems; x++)
		cw_cli(fd, "SLOT %d %s [%s]\n", x, FAXMODEM_POOL[x].devlink, faxmodem_state[FAXMODEM_POOL[x].state]);

	cw_mutex_unlock(&control_lock);

	return 0;
}


static int chan_fax_vblevel(int fd, int argc, char *argv[]) 
{
	if (argc > 2)
		cfg_vblevel = atoi(argv[2]);

	cw_cli(fd, "vblevel = %d\n", cfg_vblevel);

	return 0;
}


static struct cw_clicmd  cli_chan_fax[] = {
	{
		.cmda = { "fax", "status", NULL },
		.handler = chan_fax_status,
		.summary = "Show fax modem status",
		.usage = "Usage: fax status",
	},
	{
		.cmda = { "fax", "vblevel", NULL },
		.handler = chan_fax_vblevel,
		.summary = "Set/show fax modem vblevel",
		.usage = "Usage: fax vblevel [<n>]",
	},
};

/******************************* CORE INTERFACE ********************************************
 * These are module-specific interface functions that are common to every module
 * To be used to initilize/de-initilize, reload and track the use count of a loadable module. 
 */

static void graceful_unload(void);

static struct cw_atexit fax_atexit = {
	.name = "FAX Terminate",
	.function = graceful_unload,
};

static void graceful_unload(void)
{
	deactivate_fax_modems();

	cw_channel_unregister(&technology);
	cw_cli_unregister_multiple(cli_chan_fax, arraysize(cli_chan_fax));

	if (cfg_dev_prefix)
		free(cfg_dev_prefix);
	if (cfg_context)
		free(cfg_context);

	cw_atexit_unregister(&fax_atexit);
}


static int load_module(void)
{
	cw_clock_init();

	parse_config();

	cw_atexit_register(&fax_atexit);

	activate_fax_modems();

	if (cw_channel_register(&technology)) {
		cw_log(CW_LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	cw_cli_register_multiple(cli_chan_fax, arraysize(cli_chan_fax));

	return 0;
}

static int unload_module(void)
{
	graceful_unload();
	return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
