//#define DO_TRACE
/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Fax Channel Driver
 * 
 * Copyright (C) 2005 Anthony Minessale II
 *
 * Anthony Minessale II <anthmct@yahoo.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

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
#include <netinet/tcp.h>

#include "callweaver/lock.h"
#include "callweaver/cli.h"
#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/astobj.h"
#include "callweaver/atexit.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"
#include "callweaver/pbx.h"
#include "callweaver/devicestate.h"
#include "faxmodem.h"


static const char desc[] = "Fax Modem Interface";
static const char type[] = "Fax";
static const char tdesc[] = "Fax Modem Interface";

#define CONFIGFILE "chan_fax.conf"
#define DSP_BUFFER_MAXSIZE 2048
#define SAMPLES 160
#define MS 20

#define DEFAULT_MAX_FAXMODEMS	4
#define DEFAULT_DEV_PREFIX	"/dev/FAX"
#define DEFAULT_TIMEOUT		30000
#define DEFAULT_RING_STRATEGY	0
#define DEFAULT_CONTEXT		"chan_fax"
#define DEFAULT_VBLEVEL		0

#define VBPREFIX "CHAN FAX: "

static int cfg_timeout;
static int cfg_modems;
static int cfg_ringstrategy;
static char *cfg_dev_prefix;
static char *cfg_context;
static int cfg_vblevel;

static struct faxmodem *FAXMODEM_POOL;

#define IO_READ		"1"
#define IO_HUP		"0"
#define IO_PROD		"2"

/* some flags */
typedef enum {
	TFLAG_OUTBOUND = (1 << 0),
	TFLAG_EVENT = (1 << 1),
	TFLAG_DATA = (1 << 2)
} TFLAGS;


static int rr_next;

/* The following object is where you can attach your technology-specific state data.
 * Add as many members as you like and the data will be available to you in all of the methods.
 *
 * In the 'requester' method you need to allocate both a channel and a private_object
 * In in the 'hangup' method you must detach and destroy the private_object but not the channel
 * that is done for you.  Somewhat of an asyncronous life cycle *shrug*
 */

struct private_object {
	unsigned int flags;							/* FLAGS */
	struct cw_frame frame;						/* Frame for Writing */
	short fdata[(SAMPLES * 2) + CW_FRIENDLY_OFFSET];
	int flen;
	struct cw_channel *owner;					/* Pointer to my owner (the abstract channel object) */
	struct faxmodem *fm;
#ifdef DO_TRACE
	int debug[2];
#endif
        char *cid_num;
        char *cid_name;

	/* If this is true it means we already sent a hangup-statusmessage
	 * to our user, so don't bother with it when hanging up
	 */
        int hangup_msg_sent; 

	/* Condition variable for signalling new data */
	cw_cond_t data_cond;
};


CW_MUTEX_DEFINE_STATIC(control_lock);
CW_MUTEX_DEFINE_STATIC(data_lock);

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
static int dsp_buffer_size(int bitrate, struct timeval tv, int lastsize);
static void *faxmodem_media_thread(void *obj);
static int control_handler(struct faxmodem *fm, int op, const char *num);
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


/* channel_new() make a new channel and fit it with a private object */
static struct cw_channel *channel_new(struct faxmodem *fm)
{
	struct private_object *tech_pvt;
	struct cw_channel *chan = NULL;

	if (!(tech_pvt = calloc(1, sizeof(*tech_pvt)))) {
		cw_log(CW_LOG_ERROR, "Can't allocate a private structure.\n");
	} else {
		int fd[2];

		if (pipe(fd)) {
			free(tech_pvt);
			cw_log(CW_LOG_ERROR, "Can't allocate a pipe: %s\n", strerror(errno));
		} else if (!(chan = cw_channel_alloc(1))) {
			close(fd[0]);
			close(fd[1]);
			free(tech_pvt);
			cw_log(CW_LOG_ERROR, "Can't allocate a channel.\n");
		} else {
			chan->type = type;
			chan->tech = &technology;
			chan->tech_pvt = tech_pvt;
			snprintf(chan->name, sizeof(chan->name), "%s/%d-%04lx", type, fm->unit, cw_random() & 0xffff);
			chan->writeformat = chan->rawwriteformat = chan->readformat = chan->nativeformats = CW_FORMAT_SLINEAR;

			tech_pvt->fm = fm;
			tech_pvt->owner = chan;
			cw_cond_init(&tech_pvt->data_cond, 0);

			cw_fr_init_ex(&tech_pvt->frame, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
			tech_pvt->frame.offset = CW_FRIENDLY_OFFSET;
			tech_pvt->frame.data = tech_pvt->fdata + CW_FRIENDLY_OFFSET;

			fm->user_data = chan;
			chan->fds[0] = fd[0];
			fm->psock = fd[1];
		}
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
				cw_verbose(VBPREFIX  "acquire considering: %d state: %d\n", unit, FAXMODEM_POOL[unit].state);
				if (FAXMODEM_POOL[unit].state == FAXMODEM_STATE_ONHOOK) {
					fm = &FAXMODEM_POOL[unit];
					fm->state = FAXMODEM_STATE_ACQUIRED;
					break;
				}
			}
			rr_next = (rr_next + 1) % cfg_modems;
		}
	}

	cw_mutex_unlock(&control_lock);

	if (fm) {
		if (!(chan = channel_new(fm))) {
			cw_log(CW_LOG_ERROR, "Can't allocate a channel\n");
			fm->state = FAXMODEM_STATE_ONHOOK;
		}
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
	char buf[80];
	struct timeval start, alert, now;
	pthread_t tid;
	struct private_object *tech_pvt;
	time_t u_now;

	tech_pvt = self->tech_pvt;

	/* Remember callerid so we can send it to our user.
	 * Without this the callerid is wrong unless the 'o' option is used
	 * in Dial() */
	if (tech_pvt->cid_name) 
	    free(tech_pvt->cid_name);
	if (tech_pvt->cid_num) 
	    free(tech_pvt->cid_num);

	if (self->cid.cid_name)
	    tech_pvt->cid_name = strdup(self->cid.cid_name);
	else
	    tech_pvt->cid_name = 0;

	if (self->cid.cid_num)
	    tech_pvt->cid_num = strdup(self->cid.cid_num);
	else
	    tech_pvt->cid_num = 0;

	cw_setstate(self, CW_STATE_RINGING);
	tech_pvt->fm->state = FAXMODEM_STATE_RINGING;

	time(&u_now);
	cw_carefulwrite(tech_pvt->fm->master, buf, strftime(buf, sizeof(buf), "\r\nDATE=%m%d\r\nTIME=%H%M\r\n", localtime(&u_now)), 100);
	cw_cli(tech_pvt->fm->master, "NAME=%s\r\n", tech_pvt->cid_name);
	cw_cli(tech_pvt->fm->master, "NMBR=%s\r\n", tech_pvt->cid_num);
	cw_cli(tech_pvt->fm->master, "NDID=%s\r\n", tech_pvt->fm->digits);

	gettimeofday(&now, NULL);
	start = alert = now;
	do {
		if (cw_tvdiff_ms(now, alert) > 5000)
			alert = now;
		if (cw_tvdiff_ms(now, alert) == 0)
			t31_call_event(&tech_pvt->fm->t31_state, AT_CALL_EVENT_ALERTING);

		usleep(100000);
		gettimeofday(&now, NULL);
	} while (tech_pvt->fm->state == FAXMODEM_STATE_RINGING && cw_tvdiff_ms(now, start) < timeout);

	if (tech_pvt->fm->state == FAXMODEM_STATE_ANSWERED) {
		t31_call_event(&tech_pvt->fm->t31_state, AT_CALL_EVENT_ANSWERED);
		tech_pvt->fm->state = FAXMODEM_STATE_CONNECTED;
		cw_setstate(tech_pvt->owner, CW_STATE_UP);
		if (!cw_pthread_create(&tid, &global_attr_rr_detached, faxmodem_media_thread, tech_pvt))
			return 0;
	}
	return -1;
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
	struct private_object *tech_pvt = self->tech_pvt;

	if (tech_pvt) {
#ifdef DO_TRACE
		close(tech_pvt->debug[0]);
		close(tech_pvt->debug[1]);
#endif
		if (!tech_pvt->hangup_msg_sent)
			cw_cli(tech_pvt->fm->master, "NO CARRIER\r\n");

		tech_pvt->fm->state = FAXMODEM_STATE_ONHOOK;
		t31_call_event(&tech_pvt->fm->t31_state, AT_CALL_EVENT_HANGUP);

		close(self->fds[0]);
		close(tech_pvt->fm->psock);
		tech_pvt->fm->psock = -1;

		tech_pvt->fm->user_data = NULL;

		if (tech_pvt->cid_name)
			free(tech_pvt->cid_name);
		if (tech_pvt->cid_num)
			free(tech_pvt->cid_num);

		free(tech_pvt);
	}

	return 0;
}

static int dsp_buffer_size(int bitrate, struct timeval tv, int lastsize)
{
	int us;
	double cleared;

	if (!lastsize) return 0;	// the buffer has been idle
	us = cw_tvdiff_ms(cw_tvnow(), tv);
	if (us <= 0) return 0;	// no time has passed
	cleared = ((double) bitrate * ((double) us / 1000000)) / 8;
	return cleared >= lastsize ? 0 : lastsize - cleared;
}

static void *faxmodem_media_thread(void *obj)
{
	struct private_object *tech_pvt = obj;
	struct faxmodem *fm = tech_pvt->fm;
	struct timeval lastdtedata = {0,0}, now = {0,0}, reference = {0,0};
	int ms = 0;
	int avail, lastmodembufsize = 0, flowoff = 0;
	char modembuf[DSP_BUFFER_MAXSIZE];
	struct timespec abstime;
	int gotlen = 0;
	short *frame_data = tech_pvt->fdata + CW_FRIENDLY_OFFSET;

	if (cfg_vblevel > 1)
		cw_verbose(VBPREFIX  "MEDIA THREAD ON %s\n", fm->devlink);

	gettimeofday(&reference, NULL);	
	while (fm->state == FAXMODEM_STATE_CONNECTED) {
		tech_pvt->flen = 0;
		do {
			gotlen = t31_tx((t31_state_t*)&fm->t31_state, 
					frame_data + tech_pvt->flen, SAMPLES - tech_pvt->flen);
			tech_pvt->flen += gotlen;
		} while (tech_pvt->flen < SAMPLES && gotlen > 0);
			
		if (!tech_pvt->flen) {
			tech_pvt->flen = SAMPLES;
			memset(frame_data, 0, SAMPLES * 2);
		}
		tech_pvt->frame.samples = tech_pvt->flen;
		tech_pvt->frame.datalen = tech_pvt->frame.samples * 2;
		write(tech_pvt->fm->psock, IO_READ, 1);

#ifdef DO_TRACE
		write(tech_pvt->debug[1], frame_data, tech_pvt->flen * 2);
#endif

		reference = cw_tvadd(reference, cw_tv(0, MS * 1000));
		while ((ms = cw_tvdiff_ms(reference, cw_tvnow())) > 0) {
			abstime.tv_sec = time(0) + 1;
			abstime.tv_nsec = 0;

			cw_mutex_lock(&data_lock);
			cw_cond_timedwait(&tech_pvt->data_cond, &data_lock,
					    &abstime);
			cw_mutex_unlock(&data_lock);
		}
		
		gettimeofday(&now, NULL);
	
		avail = DSP_BUFFER_MAXSIZE - dsp_buffer_size(fm->t31_state.bit_rate, lastdtedata, lastmodembufsize);
		if (flowoff && avail >= DSP_BUFFER_MAXSIZE / 2) {
			char xon[1];
			xon[0] = 0x11;
			write(fm->psock, xon, 1);
			flowoff = 0;
			if (cfg_vblevel > 1) {
				cw_verbose(VBPREFIX "%s XON, %d bytes available\n", fm->devlink, avail);
			}
		}
		if (cw_test_flag(fm, TFLAG_EVENT) && !flowoff) {
			ssize_t len;
			cw_clear_flag(fm, TFLAG_EVENT);
			do {
				len = read(fm->psock, modembuf, avail);
				if (len > 0) {
					t31_at_rx((t31_state_t*)&fm->t31_state,
						  modembuf, len);
					avail -= len;
				}
			} while (len > 0 && avail > 0);
			lastmodembufsize = DSP_BUFFER_MAXSIZE - avail;
			lastdtedata = now;
			if (!avail) {
				char xoff[1];
				xoff[0] = 0x13;
				write(fm->psock, xoff, 1);
				flowoff = 1;
				if (cfg_vblevel > 1) {
					cw_verbose(VBPREFIX "%s XOFF\n", fm->devlink);
				}
			}
		}

		usleep(100);
		sched_yield();
	}

	if (cfg_vblevel > 1)
		cw_verbose(VBPREFIX  "MEDIA THREAD OFF %s\n", fm->devlink);

	return NULL;
}

/*--- tech_answer: answer a call on my channel
 * if being 'answered' means anything special to your channel
 * now is your chance to do it!
 */
static int tech_answer(struct cw_channel *self)
{
	pthread_t tid;
	struct private_object *tech_pvt;

	tech_pvt = self->tech_pvt;

	if (cfg_vblevel > 1)
		cw_verbose(VBPREFIX  "Connected %s\n", tech_pvt->fm->devlink);

	tech_pvt->fm->state = FAXMODEM_STATE_CONNECTED;
	t31_call_event(&tech_pvt->fm->t31_state, AT_CALL_EVENT_CONNECTED);

	if (!cw_pthread_create(&tid, &global_attr_rr_detached, faxmodem_media_thread, tech_pvt))
		return 0;
	return -1;
}


/*--- tech_read: Read an audio frame from my channel.
 * You need to read data from your channel and convert/transfer the
 * data into a newly allocated struct cw_frame object
 */
static struct cw_frame *tech_read(struct cw_channel *self)
{
	pthread_t tid;
	struct private_object *tech_pvt;
	int res;
	char cmd;

	tech_pvt = self->tech_pvt;
	res = read(self->fds[0], &cmd, sizeof(cmd));

	if (res < 0 || cmd == IO_HUP[0]) {
		cw_softhangup(tech_pvt->owner, CW_SOFTHANGUP_EXPLICIT);
		return NULL;
	}

	return &tech_pvt->frame;
}

/*--- tech_write: Write an audio frame to my channel
 * Yep, this is the opposite of tech_read, you need to examine
 * a frame and transfer the data to your technology's audio stream.
 * You do not have any responsibility to destroy this frame and you should
 * consider it to be read-only.
 */

static int tech_write(struct cw_channel *self, struct cw_frame *frame)
{
	struct private_object *tech_pvt;
	int res = 0;
	//int gotlen;

	if (frame->frametype != CW_FRAME_VOICE) {
		return 0;
	}

	tech_pvt = self->tech_pvt;

#ifdef DO_TRACE
	write(tech_pvt->debug[0], frame->data, frame->datalen);
#endif

	res = t31_rx((t31_state_t*)&tech_pvt->fm->t31_state,
		     frame->data, frame->samples);
	
	/* Signal new data to media thread */
	cw_mutex_lock(&data_lock);
	cw_set_flag(tech_pvt, TFLAG_DATA);
	cw_cond_signal(&tech_pvt->data_cond);
	cw_mutex_unlock(&data_lock);
	
	//write(tech_pvt->fm->psock, IO_PROD, 1);


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
	struct private_object *tech_pvt;
	int res = 0;
	int hangup = 0;

	tech_pvt = self->tech_pvt;
        if (cfg_vblevel > 1) {
                cw_verbose(VBPREFIX  "Indication %d on %s\n", condition,
			     self->name);
        }

	switch(condition) {
	case CW_CONTROL_RINGING:
	case CW_CONTROL_ANSWER:
	case CW_CONTROL_PROGRESS:
	    hangup = 0;
	    break;
	case CW_CONTROL_BUSY:
	case CW_CONTROL_CONGESTION:
	    cw_cli(tech_pvt->fm->master, "BUSY\r\n");
	    hangup = 1;
	    break;
	default:
	    if (cfg_vblevel > 1)
                cw_verbose(VBPREFIX  "UNKNOWN Indication %d on %s\n", 
			     condition,
                             self->name);
	}

	if (hangup) {
	    if (cfg_vblevel > 1) {
                cw_verbose(VBPREFIX  "Hanging up because of indication %d "
			     "on %s\n", condition, self->name);
	    }	    
	    tech_pvt->hangup_msg_sent = 1;
	    cw_softhangup(self, CW_SOFTHANGUP_EXPLICIT);
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


static int control_handler(struct faxmodem *fm, int op, const char *num)
{
	int res = 0;

	if (cfg_vblevel > 1)
		cw_verbose(VBPREFIX  "Control Handler %s [op = %d]\n", fm->devlink, op);

	cw_mutex_lock(&control_lock);

	if (fm->state == FAXMODEM_STATE_INIT)
		fm->state = FAXMODEM_STATE_ONHOOK;

	do {
		if (op == AT_MODEM_CONTROL_CALL) {
			struct cw_channel *chan = NULL;
			int cause;
		    
			if (fm->state != FAXMODEM_STATE_ONHOOK) {
				cw_log(CW_LOG_ERROR, "Invalid State! [%s]\n", faxmodem_state2name(fm->state));
				res = -1;
				break;
			}
			if (!(chan = channel_new(fm))) {
				cw_log(CW_LOG_ERROR, "Can't allocate a channel\n");
				res = -1;
				break;
			} else {
				struct private_object *tech_pvt = chan->tech_pvt;

				faxmodem_set_flag(fm, FAXMODEM_FLAG_ATDT);
				cw_copy_string(fm->digits, num, sizeof(fm->digits));
				cw_copy_string(chan->context, cfg_context, sizeof(chan->context));
				cw_copy_string(chan->exten, fm->digits, sizeof(chan->exten));
				cw_set_flag(tech_pvt, TFLAG_OUTBOUND);
#ifdef DOTRACE
				tech_pvt->debug[0] = open("/tmp/cap-in.raw", O_WRONLY|O_CREAT, 00660);
				tech_pvt->debug[1] = open("/tmp/cap-out.raw", O_WRONLY|O_CREAT, 00660);
#endif
				if (cfg_vblevel > 1)
					cw_verbose(VBPREFIX  "Call Started %s %s@%s\n", chan->name, chan->exten, chan->context);

				fm->state = FAXMODEM_STATE_CALLING;
				cw_setstate(chan, CW_STATE_RINGING);
				if (cw_pbx_start(chan)) {
					cw_log(CW_LOG_ERROR, "Unable to start PBX on %s\n", chan->name);
					cw_hangup(chan);
				}
			}
		} else if (op == AT_MODEM_CONTROL_ANSWER) { 
			if (fm->state != FAXMODEM_STATE_RINGING) {
				cw_log(CW_LOG_ERROR, "Invalid State! [%s]\n", faxmodem_state2name(fm->state));
				res = -1;
				break;
			}
			if (cfg_vblevel > 1)
				cw_verbose(VBPREFIX  "Answered %s", fm->devlink);
			fm->state = FAXMODEM_STATE_ANSWERED;
		} else if (op == AT_MODEM_CONTROL_HANGUP) {
			if (fm->psock > -1) {
				if (fm->user_data) {
					struct cw_channel *chan = fm->user_data;
					cw_softhangup(chan, CW_SOFTHANGUP_EXPLICIT);
					write(fm->psock, IO_HUP, 1);
				}
			} else
				fm->state = FAXMODEM_STATE_ONHOOK;

			t31_call_event(&fm->t31_state, AT_CALL_EVENT_HANGUP);
		}
	} while (0);
	
	cw_mutex_unlock(&control_lock);
	return res;
}


static void faxmodem_thread_cleanup(void *obj)
{
	struct faxmodem *fm = obj;

	if (fm->master > -1) {
		close(fm->master);
		fm->master = -1;
	}

	if (fm->devlink[0])
		unlink(fm->devlink);

	if (cfg_vblevel > 1)
		cw_verbose(VBPREFIX  "Thread ended for %s\n", fm->devlink);
}

static void *faxmodem_thread(void *obj)
{
	char buf[1024], tmp[80];
	struct pollfd pfd;
	struct faxmodem *fm = obj;
	int res;

	pthread_cleanup_push(faxmodem_thread_cleanup, fm);

	if (!faxmodem_init(fm, control_handler, cfg_dev_prefix)) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		pfd.fd = fm->master;
		pfd.events = POLLIN;

		for (;;) {
			res = poll(&pfd, 1, 1000);

			if (!res || (res == -1 && errno == EINTR))
				continue;

			if (res == -1 || pfd.revents & (POLLERR | POLLNVAL))
				break;

			if (pfd.revents & POLLHUP) {
				sleep(1);
				continue;
			}

			pthread_testcancel();

			cw_set_flag(fm, TFLAG_EVENT);
			res = read(fm->master, buf, sizeof(buf)-1);
			buf[res] = '\0';
			t31_at_rx(&fm->t31_state, buf, res);
			memset(tmp, 0, sizeof(tmp));

			/* Copy the AT command for debugging */
			if (strstr(buf, "AT") || strstr(buf, "at")) {
				int x;
				int l = res < (sizeof(tmp)-1) ? res : sizeof(tmp)-1;
				strncpy(tmp, buf, l);
				for (x = 0; x < l; x++) {
					if (tmp[x] == '\r' || tmp[x] == '\n')
						tmp[x] = '\0';
				}
				if (!cw_strlen_zero(tmp) && cfg_vblevel > 0) {
					pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
					cw_verbose(VBPREFIX  "Command on %s [%s]\n", fm->devlink, tmp);
					pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
				}
			}
		}

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		cw_log(CW_LOG_WARNING, "Poll on master for %s gave res %d, revents 0x%04x\n", fm->devlink, res, pfd.revents);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	}

	pthread_cleanup_pop(1);
	return NULL;
}

static void activate_fax_modems(void)
{
	pthread_t tid;
	int x;

	cw_mutex_lock(&control_lock);

	if ((FAXMODEM_POOL = calloc(cfg_modems, sizeof(FAXMODEM_POOL[0])))) {
		for (x = 0; x < cfg_modems; x++) {
			if (cfg_vblevel > 1)
				cw_verbose(VBPREFIX  "Starting Fax Modem SLOT %d\n", x);
			FAXMODEM_POOL[x].unit = x;
			FAXMODEM_POOL[x].thread = CW_PTHREADT_NULL;
			cw_pthread_create(&FAXMODEM_POOL[x].thread, &global_attr_default, faxmodem_thread, &FAXMODEM_POOL[x]);
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
				cw_verbose(VBPREFIX  "Stopping Fax Modem SLOT %d\n", x);
			pthread_cancel(FAXMODEM_POOL[x].thread);
		}
	}

	/* Wait for Threads to die */
	for(x = 0; x < cfg_modems; x++) {
		if (!pthread_equal(FAXMODEM_POOL[x].thread, CW_PTHREADT_NULL)) {
			if (cfg_vblevel > 2)
				cw_verbose(VBPREFIX  "Stopped Fax Modem SLOT %d\n", x);
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
	cfg_timeout= DEFAULT_TIMEOUT;
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
					if (!strcasecmp(v->name, "modems")) {
						cfg_modems = atoi(v->value);
					} else if (!strcasecmp(v->name, "timeout-ms")) {
						cfg_timeout = atoi(v->value);
					} else if (!strcasecmp(v->name, "trap-seg")) {
						cw_log(CW_LOG_WARNING, "trap-seg is deprecated - remove it from your chan_fax.conf");
					} else if (!strcasecmp(v->name, "context")) {
						if (cfg_context)
							free(cfg_context);
						cfg_context = strdup(v->value);
					} else if (!strcasecmp(v->name, "vblevel")) {
						cfg_vblevel = atoi(v->value);
					} else if (!strcasecmp(v->name, "device-prefix")) {
						if (cfg_dev_prefix)
							free(cfg_dev_prefix);
					        cfg_dev_prefix = strdup(v->value);
					} else if (!strcasecmp(v->name, "ring-strategy")) {
					    if (!strcasecmp(v->value, "roundrobin"))
						cfg_ringstrategy = 0;
					    else
						cfg_ringstrategy = 1;

					}
				}
			}
		}
		cw_config_destroy(cfg);
	}
}


static int chan_fax_cli(int fd, int argc, char *argv[]) 
{
	if (argc > 1) {
		if (!strcasecmp(argv[1], "status")) {
			int x;
			cw_mutex_lock(&control_lock);
			for (x = 0; x < cfg_modems; x++) {
				cw_cli(fd, "SLOT %d %s [%s]\n", x, FAXMODEM_POOL[x].devlink, faxmodem_state2name(FAXMODEM_POOL[x].state));
			}
			cw_mutex_unlock(&control_lock);
		} else if (!strcasecmp(argv[1], "vblevel")) {
			if (argc > 2)
				cfg_vblevel = atoi(argv[2]);
			cw_cli(fd, "vblevel = %d\n", cfg_vblevel);
		}

	} else {
		cw_cli(fd, "Usage: fax [status]\n");
	}
	return 0;
}


static struct cw_clicmd  cli_chan_fax = {
	.cmda = { "fax", NULL },
	.handler = chan_fax_cli,
	.summary = "Fax Channel",
	.usage = "Fax Channel",
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

	faxmodem_clear_logger();
	cw_channel_unregister(&technology);
	cw_cli_unregister(&cli_chan_fax);

	if (cfg_dev_prefix)
		free(cfg_dev_prefix);
	if (cfg_context)
		free(cfg_context);

	cw_atexit_unregister(&fax_atexit);
}


static int load_module(void)
{
	parse_config();

	if (cfg_vblevel > 1) {
		faxmodem_set_logger((faxmodem_logger_t) cw_log, __CW_LOG_ERROR, __CW_LOG_WARNING, __CW_LOG_NOTICE);
	}

	cw_atexit_register(&fax_atexit);

	activate_fax_modems();

	if (cw_channel_register(&technology)) {
		cw_log(CW_LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	cw_cli_register(&cli_chan_fax);

	return 0;
}

static int unload_module(void)
{
	graceful_unload();
	return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
