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


static const char desc[] = "Fax Modem Interface";
static const char type[] = "Fax";
static const char tdesc[] = "Fax Modem Interface";

#define CONFIGFILE "chan_fax.conf"
#define SAMPLES 160
#define MS 20

#define DEFAULT_MAX_FAXMODEMS	4
#define DEFAULT_DEV_PREFIX	"/dev/FAX"
#define DEFAULT_TIMEOUT		30000
#define DEFAULT_RING_STRATEGY	0
#define DEFAULT_CONTEXT		"chan_fax"
#define DEFAULT_VBLEVEL		0


static int cfg_timeout;
static int cfg_modems;
static int cfg_ringstrategy;
static char *cfg_dev_prefix;
static char *cfg_context;
static int cfg_vblevel;


typedef enum {
	FAXMODEM_STATE_CLOSED,
	FAXMODEM_STATE_ONHOOK,
	FAXMODEM_STATE_ACQUIRED,
	FAXMODEM_STATE_RINGING,
	FAXMODEM_STATE_ANSWERED,
	FAXMODEM_STATE_CALLING,
	FAXMODEM_STATE_CONNECTED,
	FAXMODEM_STATE_HANGUP,
	FAXMODEM_STATE_LAST
} faxmodem_state_t;

static const char *faxmodem_state[] =
{
	[FAXMODEM_STATE_CLOSED] =	"CLOSED",
	[FAXMODEM_STATE_ONHOOK] =	"ONHOOK",
	[FAXMODEM_STATE_ACQUIRED] =	"ACQUIRED",
	[FAXMODEM_STATE_RINGING] =	"RINGING",
	[FAXMODEM_STATE_ANSWERED] =	"ANSWERED",
	[FAXMODEM_STATE_CALLING] =	"CALLING",
	[FAXMODEM_STATE_CONNECTED] =	"CONNECTED",
	[FAXMODEM_STATE_HANGUP] =	"HANGUP",
	[FAXMODEM_STATE_LAST] =		"UNKNOWN",
};


struct faxmodem;

struct faxmodem {
	int unit;
	t31_state_t t31_state;
	unsigned int flags;
	int master;
	char devlink[128];
	faxmodem_state_t state;
	int psock;
#ifdef DO_TRACE
	int debug[2];
#endif
	pthread_t thread;
	pthread_t media_thread;
	cw_cond_t data_cond;
	struct cw_channel *owner;					/* Pointer to my owner (the abstract channel object) */
	struct cw_frame frame;						/* Frame for Writing */
	short fdata[(SAMPLES * 2) + CW_FRIENDLY_OFFSET];
	int flen;
};


static struct faxmodem *FAXMODEM_POOL;


#define IO_READ		"1"
#define IO_HUP		"0"
#define IO_PROD		"2"
#define IO_CNG		"3"


static struct cw_frame frame_cng = {
	.frametype = CW_FRAME_DTMF,
	.subclass = 'f',
};


/* some flags */
typedef enum {
	TFLAG_EVENT = (1 << 1),
} TFLAGS;


static int rr_next;


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
static int faxmodem_init(struct faxmodem *fm, const char *device_prefix);
static struct cw_channel *channel_new(struct faxmodem *fm);
static int dsp_buffer_size(int bitrate, struct timeval tv, int lastsize);
static void *faxmodem_media_thread(void *obj);
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


static int t31_at_tx_handler(at_state_t *s, void *user_data, const uint8_t *buf, size_t len)
{
	struct faxmodem *fm = user_data;
	ssize_t n;

	if (cw_carefulwrite(fm->master, (char *)buf, len, 100) < 0)
		cw_log(CW_LOG_ERROR, "Failed to write all of %d bytes to %s\n", len, fm->devlink);

	return len;
}


int faxmodem_init(struct faxmodem *fm, const char *device_prefix)
{
	static int NEXT_ID = 0;
	char buf[256];

#ifdef HAVE_POSIX_OPENPT
	if ((fm->master = posix_openpt(O_RDWR | O_NOCTTY)) < 0) {
		cw_log(CW_LOG_ERROR, "Failed to get a pty: %s\n", strerror(errno));
		return -1;
	}

	/* The behaviour of grantpt is undefined if a SIGCHLD handler is installed.
	 * We can't guarantee that, but grantpt just sets permissions on the slave
	 * tty and since we expect that to be opened by a root owned faxgetty we
	 * can live without doing this.
	 */
	// grantpt(fm->master);
	unlockpt(fm->master);
#else
	int slave = -1;

	fm->master = -1;

	if (openpty(&fm->master, &slave, NULL, NULL, NULL)) {
		cw_log(CW_LOG_ERROR, "Failed to get a pty: %s\n", strerror(errno));
		return -1;
	}

	/* If we keep the slave open we'll likely get killed by a {fax}getty
	 * start up on it. Closing it means we're going to keep seeing POLLHUP
	 * until something else opens it. See channel/fax/chan_fax.c
	 */
	close(slave);
#endif
	ptsname_r(fm->master, buf, sizeof(buf));

	if (cfg_vblevel > 1)
		cw_verbose(VERBOSE_PREFIX_3 "Opened pty, slave device: %s\n", buf);

	snprintf(fm->devlink, sizeof(fm->devlink), "%s%d", device_prefix, NEXT_ID++);

	if (!unlink(fm->devlink) && cfg_vblevel > 1)
		cw_log(CW_LOG_WARNING, "Removed old %s\n", fm->devlink);

	if (symlink(buf, fm->devlink)) {
		cw_log(CW_LOG_ERROR, "Fatal error: failed to create %s symbolic link\n", fm->devlink);
		return -1;
	}

	if (cfg_vblevel > 1)
		cw_verbose(VERBOSE_PREFIX_3 "Created %s symbolic link\n", fm->devlink);

	if (fcntl(fm->master, F_SETFL, fcntl(fm->master, F_GETFL, 0) | O_NONBLOCK)) {
		cw_log(CW_LOG_ERROR, "Cannot set up non-blocking read on %s\n", ttyname(fm->master));
		return -1;
	}
	
	if (t31_init(&fm->t31_state, t31_at_tx_handler, fm, modem_control_handler, fm, 0, 0) < 0) {
		cw_log(CW_LOG_ERROR, "Cannot initialize the T.31 modem\n");
		return -1;
	}

	fm->state = FAXMODEM_STATE_CLOSED;
	
	if (cfg_vblevel > 0)
		cw_verbose(VERBOSE_PREFIX_1 "Fax Modem [%s] Ready\n", fm->devlink);
	return 0;
}


/* channel_new() make a new channel and fit it with a private object */
static struct cw_channel *channel_new(struct faxmodem *fm)
{
	struct cw_channel *chan = NULL;
	int fd[2];

	if (pipe(fd)) {
		cw_log(CW_LOG_ERROR, "Can't allocate a pipe: %s\n", strerror(errno));
	} else if (!(chan = cw_channel_alloc(1))) {
		close(fd[0]);
		close(fd[1]);
		cw_log(CW_LOG_ERROR, "Can't allocate a channel.\n");
	} else {
		chan->type = type;
		chan->tech = &technology;
		chan->tech_pvt = fm;
		snprintf(chan->name, sizeof(chan->name), "%s/%d-%04lx", type, fm->unit, cw_random() & 0xffff);
		chan->writeformat = chan->rawwriteformat = chan->readformat = chan->nativeformats = CW_FORMAT_SLINEAR;

		fm->owner = chan;
		cw_cond_init(&fm->data_cond, 0);

		cw_fr_init_ex(&fm->frame, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
		fm->frame.offset = CW_FRIENDLY_OFFSET;
		fm->frame.data = fm->fdata + CW_FRIENDLY_OFFSET;

		chan->fds[0] = fd[0];
		fm->psock = fd[1];
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
				cw_verbose(VERBOSE_PREFIX_3 "acquire considering: %d state: %d\n", unit, FAXMODEM_POOL[unit].state);
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
	struct iovec iov[8];
	struct timeval start, alert, now;
	pthread_t tid;
	struct faxmodem *fm;
	time_t u_now;
	int i;

	fm = self->tech_pvt;

	cw_setstate(self, CW_STATE_RINGING);
	fm->state = FAXMODEM_STATE_RINGING;

	time(&u_now);
	iov[0].iov_base = buf;
	iov[0].iov_len = strftime(buf, sizeof(buf), "\r\nDATE=%m%d\r\nTIME=%H%M", localtime(&u_now));
	i = 1;
	if (self->cid.cid_name) {
		iov[i].iov_base = "\r\nNAME=";
		iov[i++].iov_len = sizeof("\r\nNAME=") - 1;
		iov[i].iov_base = self->cid.cid_name;
		iov[i++].iov_len = strlen(self->cid.cid_name);
	}
	if (self->cid.cid_num) {
		iov[i].iov_base = "\r\nNMBR=";
		iov[i++].iov_len = sizeof("\r\nNMBR=") - 1;
		iov[i].iov_base = self->cid.cid_num;
		iov[i++].iov_len = strlen(self->cid.cid_num);
	}
	if (self->cid.cid_dnid) {
		iov[i].iov_base = "\r\nNDID=";
		iov[i++].iov_len = sizeof("\r\nNDID=") - 1;
		iov[i].iov_base = self->cid.cid_dnid;
		iov[i++].iov_len = strlen(self->cid.cid_dnid);
	}
	iov[i].iov_base = "\r\n";
	iov[i++].iov_len = sizeof("\r\n") - 1;
	cw_carefulwritev(fm->master, iov, i, 100);

	gettimeofday(&now, NULL);
	start = alert = now;
	do {
		if (cw_tvdiff_ms(now, alert) > 5000)
			alert = now;
		if (cw_tvdiff_ms(now, alert) == 0)
			t31_call_event(&fm->t31_state, AT_CALL_EVENT_ALERTING);

		usleep(100000);
		gettimeofday(&now, NULL);
	} while (fm->state == FAXMODEM_STATE_RINGING && cw_tvdiff_ms(now, start) < timeout);

	if (fm->state == FAXMODEM_STATE_ANSWERED) {
		t31_call_event(&fm->t31_state, AT_CALL_EVENT_ANSWERED);
		fm->state = FAXMODEM_STATE_CONNECTED;
		cw_setstate(fm->owner, CW_STATE_UP);
		if (!cw_pthread_create(&tid, &global_attr_rr_detached, faxmodem_media_thread, fm))
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
	struct faxmodem *fm = self->tech_pvt;

	if (fm) {
#ifdef DO_TRACE
		close(fm->debug[0]);
		close(fm->debug[1]);
#endif
		fm->state = FAXMODEM_STATE_ONHOOK;
		t31_call_event(&fm->t31_state, AT_CALL_EVENT_HANGUP);

		close(self->fds[0]);
		close(fm->psock);
		fm->psock = -1;

		self->tech_pvt = NULL;
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
	struct faxmodem *fm = obj;
	struct timeval lastdtedata = {0,0}, now = {0,0}, reference = {0,0};
	int ms = 0;
	int avail, lastmodembufsize = 0, flowoff = 0;
	char modembuf[T31_TX_BUF_LEN];
	struct timespec abstime;
	int gotlen = 0;
	short *frame_data = fm->fdata + CW_FRIENDLY_OFFSET;

	if (cfg_vblevel > 1)
		cw_verbose(VERBOSE_PREFIX_3 "MEDIA THREAD ON %s\n", fm->devlink);

	gettimeofday(&reference, NULL);	
	while (fm->state == FAXMODEM_STATE_CONNECTED) {
		fm->flen = 0;
		do {
			gotlen = t31_tx((t31_state_t*)&fm->t31_state, 
					frame_data + fm->flen, SAMPLES - fm->flen);
			fm->flen += gotlen;
		} while (fm->flen < SAMPLES && gotlen > 0);
			
		if (!fm->flen) {
			fm->flen = SAMPLES;
			memset(frame_data, 0, SAMPLES * 2);
		}
		fm->frame.samples = fm->flen;
		fm->frame.datalen = fm->frame.samples * 2;
		write(fm->psock, IO_READ, 1);

#ifdef DO_TRACE
		write(fm->debug[1], frame_data, fm->flen * 2);
#endif

		reference = cw_tvadd(reference, cw_tv(0, MS * 1000));
		while ((ms = cw_tvdiff_ms(reference, cw_tvnow())) > 0) {
			abstime.tv_sec = time(0) + 1;
			abstime.tv_nsec = 0;

			cw_mutex_lock(&data_lock);
			cw_cond_timedwait(&fm->data_cond, &data_lock, &abstime);
			cw_mutex_unlock(&data_lock);
		}
		
		gettimeofday(&now, NULL);
	
		avail = T31_TX_BUF_LEN - dsp_buffer_size(fm->t31_state.bit_rate, lastdtedata, lastmodembufsize);
		if (flowoff && avail >= T31_TX_BUF_LEN / 2) {
			char xon[1];
			xon[0] = 0x11;
			write(fm->master, xon, 1);
			flowoff = 0;
			if (cfg_vblevel > 1) {
				cw_verbose(VERBOSE_PREFIX_3 "%s XON, %d bytes available\n", fm->devlink, avail);
			}
		}
		if (cw_test_flag(fm, TFLAG_EVENT) && !flowoff) {
			ssize_t len;
			cw_clear_flag(fm, TFLAG_EVENT);
			do {
				len = read(fm->master, modembuf, avail);
				if (len > 0) {
					t31_at_rx((t31_state_t*)&fm->t31_state,
						  modembuf, len);
					avail -= len;
				}
			} while (len > 0 && avail > 0);
			lastmodembufsize = T31_TX_BUF_LEN - avail;
			lastdtedata = now;
			if (!avail) {
				char xoff[1];
				xoff[0] = 0x13;
				write(fm->master, xoff, 1);
				flowoff = 1;
				if (cfg_vblevel > 1) {
					cw_verbose(VERBOSE_PREFIX_3 "%s XOFF\n", fm->devlink);
				}
			}
		}

		usleep(100);
		sched_yield();
	}

	if (cfg_vblevel > 1)
		cw_verbose(VERBOSE_PREFIX_3 "MEDIA THREAD OFF %s\n", fm->devlink);

	return NULL;
}

/*--- tech_answer: answer a call on my channel
 * if being 'answered' means anything special to your channel
 * now is your chance to do it!
 */
static int tech_answer(struct cw_channel *self)
{
	pthread_t tid;
	struct faxmodem *fm = self->tech_pvt;

	if (cfg_vblevel > 1)
		cw_verbose(VERBOSE_PREFIX_2 "Connected %s\n", fm->devlink);

	fm->state = FAXMODEM_STATE_CONNECTED;
	t31_call_event(&fm->t31_state, AT_CALL_EVENT_CONNECTED);

	write(fm->psock, IO_CNG, 1);

	if (!cw_pthread_create(&tid, &global_attr_rr_detached, faxmodem_media_thread, fm))
		return 0;
	return -1;
}


/*--- tech_read: Read an audio frame from my channel.
 * You need to read data from your channel and convert/transfer the
 * data into a newly allocated struct cw_frame object
 */
static struct cw_frame *tech_read(struct cw_channel *self)
{
	struct faxmodem *fm = self->tech_pvt;
	pthread_t tid;
	int res;
	char cmd;

	res = read(self->fds[0], &cmd, sizeof(cmd));

	if (res < 0 || cmd == IO_HUP[0])
		return NULL;

	if (cmd == IO_CNG)
		return &frame_cng;

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
	struct faxmodem *fm = self->tech_pvt;
	int res = 0;
	//int gotlen;

	if (frame->frametype != CW_FRAME_VOICE) {
		return 0;
	}

#ifdef DO_TRACE
	write(fm->debug[0], frame->data, frame->datalen);
#endif

	res = t31_rx((t31_state_t*)&fm->t31_state,
		     frame->data, frame->samples);
	
	/* Signal new data to media thread */
	cw_mutex_lock(&data_lock);
	cw_cond_signal(&fm->data_cond);
	cw_mutex_unlock(&data_lock);
	
	//write(fm->psock, IO_PROD, 1);


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
                cw_verbose(VERBOSE_PREFIX_3 "Indication %d on %s\n", condition, self->name);

	switch(condition) {
		case CW_CONTROL_RINGING:
		case CW_CONTROL_ANSWER:
		case CW_CONTROL_PROGRESS:
			break;
		case CW_CONTROL_BUSY:
		case CW_CONTROL_CONGESTION:
			t31_call_event(&fm->t31_state, AT_CALL_EVENT_BUSY);
			cw_softhangup(self, CW_SOFTHANGUP_EXPLICIT);
			break;
		default:
			if (cfg_vblevel > 1)
				cw_verbose(VERBOSE_PREFIX_3 "UNKNOWN Indication %d on %s\n", condition, self->name);
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
	int res = 0;

	if (cfg_vblevel > 1)
		cw_verbose(VERBOSE_PREFIX_3 "Control Handler %s [op = %d]\n", fm->devlink, op);

	cw_mutex_lock(&control_lock);

	do {
		if (op == AT_MODEM_CONTROL_CALL) {
			struct cw_channel *chan = NULL;
			int cause;
		    
			if (fm->state != FAXMODEM_STATE_ONHOOK) {
				cw_log(CW_LOG_ERROR, "Invalid State! [%s]\n", faxmodem_state[fm->state]);
				res = -1;
				break;
			}
			if (!(chan = channel_new(fm))) {
				cw_log(CW_LOG_ERROR, "Can't allocate a channel\n");
				res = -1;
				break;
			} else {
				struct faxmodem *fm = chan->tech_pvt;

				cw_copy_string(chan->context, cfg_context, sizeof(chan->context));
				cw_copy_string(chan->exten, num, sizeof(chan->exten));
#ifdef DOTRACE
				fm->debug[0] = open("/tmp/cap-in.raw", O_WRONLY|O_CREAT, 00660);
				fm->debug[1] = open("/tmp/cap-out.raw", O_WRONLY|O_CREAT, 00660);
#endif
				if (cfg_vblevel > 1)
					cw_verbose(VERBOSE_PREFIX_3 "Call Started %s %s@%s\n", chan->name, chan->exten, chan->context);

				fm->state = FAXMODEM_STATE_CALLING;
				cw_setstate(chan, CW_STATE_RINGING);
				if (cw_pbx_start(chan)) {
					cw_log(CW_LOG_ERROR, "Unable to start PBX on %s\n", chan->name);
					cw_hangup(chan);
				}
			}
		} else if (op == AT_MODEM_CONTROL_ANSWER) { 
			if (fm->state != FAXMODEM_STATE_RINGING) {
				cw_log(CW_LOG_ERROR, "Invalid State! [%s]\n", faxmodem_state[fm->state]);
				res = -1;
				break;
			}
			if (cfg_vblevel > 1)
				cw_verbose(VERBOSE_PREFIX_3 "Answered %s", fm->devlink);
			fm->state = FAXMODEM_STATE_ANSWERED;
		} else if (op == AT_MODEM_CONTROL_HANGUP) {
			if (fm->psock > -1) {
				if (fm->owner) {
					struct cw_channel *chan = fm->owner;
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
		cw_verbose(VERBOSE_PREFIX_3 "Thread ended for %s\n", fm->devlink);
}

static void *faxmodem_thread(void *obj)
{
	char buf[1024], tmp[80];
	struct pollfd pfd;
	struct faxmodem *fm = obj;
	int res;

	pthread_cleanup_push(faxmodem_thread_cleanup, fm);

	if (!faxmodem_init(fm, cfg_dev_prefix)) {
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
				fm->state = FAXMODEM_STATE_CLOSED;
				sleep(1);
				continue;
			}

			if (fm->state == FAXMODEM_STATE_CLOSED)
				fm->state = FAXMODEM_STATE_ONHOOK;

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
					cw_verbose(VERBOSE_PREFIX_3 "Command on %s [%s]\n", fm->devlink, tmp);
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
				cw_verbose(VERBOSE_PREFIX_3 "Starting Fax Modem SLOT %d\n", x);
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
				cw_verbose(VERBOSE_PREFIX_3 "Stopping Fax Modem SLOT %d\n", x);
			pthread_cancel(FAXMODEM_POOL[x].thread);
		}
	}

	/* Wait for Threads to die */
	for(x = 0; x < cfg_modems; x++) {
		if (!pthread_equal(FAXMODEM_POOL[x].thread, CW_PTHREADT_NULL)) {
			if (cfg_vblevel > 2)
				cw_verbose(VERBOSE_PREFIX_3 "Stopped Fax Modem SLOT %d\n", x);
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
