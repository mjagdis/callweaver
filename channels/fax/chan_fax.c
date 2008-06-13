/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Fax Modem Channel Driver
 * 
 * Copyright (C) 2008, Eris Associates Limited, UK
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
 *
 * Based on work:
 *     Copyright (C) 2005 Anthony Minessale II
 *     Anthony Minessale II <anthmct@yahoo.com>
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
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>

#include <spandsp.h>

#include "callweaver/lock.h"
#include "callweaver/cli.h"
#include "callweaver/channel.h"
#include "callweaver/options.h"
#include "callweaver/logger.h"
#include "callweaver/atexit.h"
#include "callweaver/module.h"
#include "callweaver/pbx.h"
#include "callweaver/phone_no_utils.h"


static const char type[] = "Fax";
static const char desc[] = "Fax Modem Interface";


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


#define CONFIGFILE "chan_fax.conf"		/*!< Name of the configuration file */
#define SAMPLES 160				/*!< Samples per frame */
#define DEFAULT_MAX_FAXMODEMS	4		/*!< Default number of faxmodems */
#define DEFAULT_DEV_PREFIX	"/dev/FAX"	/*!< Default prefix for device node names */
#define DEFAULT_RING_STRATEGY	0		/*!< Default ring strategy (round-robin) */
#define DEFAULT_CONTEXT		"chan_fax"	/*!< Default dialplan context to start in the PBX */
#define DEFAULT_VBLEVEL		0		/*!< Default verbosity */


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
	FAXMODEM_STATE_ANSWERED,
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
	[FAXMODEM_STATE_ANSWERED] =	"ANSWERED",
	[FAXMODEM_STATE_CALLING] =	"CALLING",
	[FAXMODEM_STATE_CONNECTED] =	"CONNECTED",
};


struct faxmodem {
	cw_mutex_t lock;
	struct pollfd pfd;
	faxmodem_state_t state;
	t31_state_t t31_state;
	struct cw_frame frame;
	uint8_t fdata[CW_FRIENDLY_OFFSET + SAMPLES * sizeof(int16_t)];
	struct cw_channel *owner;
	char *cid_name;
	char *cid_num;
	int unit;
	pthread_t thread;
	pthread_t clock_thread;
	char devlink[128];
#ifdef TRACE
	struct timeval start;
	int debug_fax[2];
	int debug_dte;
#endif
};


static struct faxmodem *FAXMODEM_POOL;


CW_MUTEX_DEFINE_STATIC(control_lock);


/* This has to be forward-defined because the requester function is required to
 * return a channel, not just an instance of the driver.
 */
static struct cw_channel *channel_new(struct faxmodem *fm);



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


static void *generic_pipe_clock_thread(void *obj)
{
	struct timespec ts;
	int fd = *(int *)obj;
#if !defined(__USE_XOPEN2K)
	const clockid_t clk = CLOCK_REALTIME;
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#else
	const clockid_t clk = global_clock_monotonic;
#endif

#if !defined(__USE_XOPEN2K)
	pthread_cleanup_push((void (*)(void *))pthread_cond_destroy, &cond);
	pthread_cleanup_push((void (*)(void *))pthread_mutex_destroy, &lock);
	pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, &lock);
	pthread_mutex_lock(&lock);
#endif

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	cw_clock_gettime(clk, &ts);

	for (;;) {
		char blah;

		write(fd, &blah, sizeof(blah));

		cw_clock_add_ms(&ts, SAMPLES / 8);

#if !defined(__USE_XOPEN2K)
		cw_cond_timedwait(&cond, &lock, &ts);
#else
		while (clock_nanosleep(clk, TIMER_ABSTIME, &ts, NULL) && errno == EINTR);
#endif
	}

#if !defined(__USE_XOPEN2K)
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
#endif
	return NULL;
}


/*! \defgroup dte Fax modem implementation (DTE side)
 * \{
 */

/*! Write a message generated by SpanDSP to the Callweaver logging system */
static void spandsp_log(int level, const char *msg)
{
	if (level == SPAN_LOG_ERROR)
		cw_log(CW_LOG_ERROR, "%s", msg);
	else if (level == SPAN_LOG_WARNING)
		cw_log(CW_LOG_WARNING, "%s", msg);
	else
		cw_log(CW_LOG_DEBUG, "%s", msg);
}


/*! Write data to the DTE */
static int t31_at_tx_handler(at_state_t *s, void *user_data, const uint8_t *buf, size_t len)
{
	struct faxmodem *fm = user_data;

#ifdef TRACE
	if (cfg_vblevel > 2) {
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
				if (fm->debug_dte >= 0)
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


/*! Handle a modem control command (AT...) sent by the DTE */
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

#ifdef DOTRACE
				fm->debug[0] = open("/tmp/cap-in.raw", O_WRONLY|O_CREAT, 00660);
				fm->debug[1] = open("/tmp/cap-out.raw", O_WRONLY|O_CREAT, 00660);
#endif
				if (cfg_vblevel > 0)
					cw_log(CW_LOG_DEBUG, "%s: calling %s@%s\n", fm->devlink, chan->exten, chan->context);

				fm->state = FAXMODEM_STATE_CALLING;

				cw_mutex_unlock(&fm->lock);

				cw_mutex_lock(&chan->lock);
				cw_copy_string(chan->context, cfg_context, sizeof(chan->context));
				cw_copy_string(chan->exten, num, sizeof(chan->exten));
				cw_setstate(chan, CW_STATE_RINGING);
				if (cw_pbx_start(chan)) {
					cw_log(CW_LOG_ERROR, "%s: unable to start PBX\n", fm->devlink);
					cw_hangup(chan);
					cw_mutex_unlock(&chan->lock);

					cw_mutex_lock(&fm->lock);
					fm->state = FAXMODEM_STATE_ONHOOK;
					t31_call_event(&fm->t31_state, AT_CALL_EVENT_BUSY);
					cw_mutex_unlock(&fm->lock);
					return -1;
				}
				cw_mutex_unlock(&chan->lock);
				return 0;
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
			/* Drop through to answer if we go off hook while ringing */

		case AT_MODEM_CONTROL_ANSWER:
			if (fm->state != FAXMODEM_STATE_RINGING) {
				cw_log(CW_LOG_ERROR, "%s: invalid state %s!\n", fm->devlink, faxmodem_state[fm->state]);
				res = -1;
			} else {
				if (cfg_vblevel > 0)
					cw_log(CW_LOG_DEBUG, "%s: answered\n", fm->devlink);

				gettimeofday(&fm->frame.delivery, NULL);
				fm->start = fm->frame.delivery;
				fm->frame.ts = fm->frame.seq_no = 0;

				if (!cw_pthread_create(&fm->clock_thread, &global_attr_rr, generic_pipe_clock_thread, &fm->owner->alertpipe[1]))
					fm->state = FAXMODEM_STATE_ANSWERED;
				else
					cw_log(CW_LOG_ERROR, "%s: failed to start TX clock thread: %s\n", fm->devlink, strerror(errno));
			}
			break;

		case AT_MODEM_CONTROL_ONHOOK:
		case AT_MODEM_CONTROL_HANGUP:
			if (cfg_vblevel > 0)
				cw_log(CW_LOG_DEBUG, "%s: hang up\n", fm->devlink);

			fm->state = FAXMODEM_STATE_ONHOOK;
			break;

		case AT_MODEM_CONTROL_RNG:
			if (num && cfg_vblevel > 0)
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

	cw_mutex_unlock(&fm->lock);
	return res;
}


static void faxmodem_thread_cleanup(void *obj)
{
	struct faxmodem *fm = obj;

	fm->state = FAXMODEM_STATE_CLOSED;

	/* If there's a clock thread it must be stopped now. There shouldn't be
	 * because nothing should cancel the faxmodem_thread while a channel
	 * is in existance.
	 */
	if (!pthread_equal(fm->clock_thread, CW_PTHREADT_NULL)) {
		pthread_cancel(fm->clock_thread);
		pthread_join(fm->clock_thread, NULL);
		fm->clock_thread = CW_PTHREADT_NULL;
	}

	/* Close the DTE and remove the symlink to the pty slave */
	close(fm->pfd.fd);
	unlink(fm->devlink);

	/* We are already unlocked - there's a separate cleanup that ensures that. */
	cw_mutex_destroy(&fm->lock);

	if (cfg_vblevel > 1)
		cw_log(CW_LOG_DEBUG, "%s: thread ended\n", fm->devlink);
}


/*! Initialize the given fax modem then loop continuously handling input from
 * the DTE until we are cancelled.
 */
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

	/* If we keep the slave open we'll likely get killed when a {fax}getty
	 * starts up on it. Closing it means we're going to keep seeing POLLHUP
	 * until something else opens it. But this is the case whenever the
	 * slave is closed and reopened and is dealt with in the loop below.
	 * Note that there is a window between us opening the pty and us closing
	 * the slave during which a getty could start on the pty and kill us.
	 * That shouldn't happen because we haven't added the symlink that we
	 * use for the device name of the fax modem yet but there's still a
	 * tiny chance a getty might have opened an old symlink and only just
	 * got scheduled again. That's why POSIX implemented pty support
	 * differently.
	 */
	close(ret);
#endif
	ptsname_r(fm->pfd.fd, buf, sizeof(buf));

	if (cfg_vblevel > 1)
		cw_log(CW_LOG_DEBUG, "%s: opened pty, slave device: %s\n", fm->devlink, buf);

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
		span_log_set_message_handler(&fm->t31_state.logging, spandsp_log);
		span_log_set_level(&fm->t31_state.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_SHOW_VARIANT | SPAN_LOG_DEBUG_3);
	}

	if (cfg_vblevel > 0)
		cw_verbose(VERBOSE_PREFIX_1 "%s: fax modem ready\n", fm->devlink);

	pthread_cleanup_push(faxmodem_thread_cleanup, fm);
	cw_mutex_init(&fm->lock);

	fm->pfd.events = POLLIN;

	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		/* If the DTE closed the slave side we expect to keep getting POLLHUP until
		 * something reopens it. Therefore the first POLLHUP (or POLLERR or POLLNVAL)
		 * takes us to "CLOSED" and then we sleep for a second before each successive
		 * poll until we get POLLIN or timeout.
		 */
		if (fm->state == FAXMODEM_STATE_CLOSED)
			sleep(1);

		/* If we have a call coming in we want to say signal a RING every now and
		 * again. We don't need to care about precise timing so it doesn't matter
		 * if commands from the DTE interrupt us and reset our timeout. Normally
		 * the only thing we'd expect from the DTE at this point would be ATA.
		 */
		ret = poll(&fm->pfd, 1, (fm->state == FAXMODEM_STATE_RINGING ? 5000 : -1));

		pthread_testcancel();
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		pthread_cleanup_push((void (*)(void *))cw_mutex_unlock_func, &fm->lock);
		cw_mutex_lock(&fm->lock);

		if ((ret == -1 && errno != EINTR) || (ret == 1 && fm->pfd.revents & (POLLHUP | POLLERR | POLLNVAL))) {
			/* Anything abnormal drops us into the closed state */
			/* FIXME: What if a channel was between ONHOOK and CONNECTED? We really want
			 * to signal a hangup but we'd have to drop and reaquire fm->lock
			 * around the cw_setstate.
			 */
			fm->state = FAXMODEM_STATE_CLOSED;
		} else if (fm->state == FAXMODEM_STATE_CLOSED) {
			/* We were closed due (probably) to POLLHUP. Now something's opened
			 * the DTE side of the modem again.
			 */
			fm->state = FAXMODEM_STATE_ONHOOK;
		} else if (ret != 1) {
			/* We timed out. If we're ringing (and we should be since we timed out) send
			 * a RING to the DTE.
			 */
			if (fm->state == FAXMODEM_STATE_RINGING)
				t31_call_event(&fm->t31_state, AT_CALL_EVENT_ALERTING);
		} else { /* ret == 1 */
			/* There's data from the DTE. Read it and process it.
			 * FIXME: If we're connected but waiting for TX to send data we'll just spin
			 * here. That's "unfortunate".
			 */
			avail = T31_TX_BUF_LEN - fm->t31_state.tx_in_bytes + fm->t31_state.tx_out_bytes - 1;
			if (fm->state != FAXMODEM_STATE_CONNECTED
			|| (fm->state == FAXMODEM_STATE_CONNECTED && !fm->t31_state.tx_holding && avail)) {
				int len;
				while (avail > 0 && (len = read(fm->pfd.fd, modembuf, avail)) > 0) {
#ifdef TRACE
					/* Log AT commands for debugging */
					if (cfg_vblevel > 2) {
						char *p;
						int togo = len;
						ssize_t n;

						for (p = modembuf; togo >= 2; p++, togo--) {
							if ((p[0] == 'a' || p[0] == 'A') && (p[1] == 't' || p[1] == 'T')) {
								char *q;

								for (q = p + 2, togo -= 2; togo && *q != '\r' && *q != '\n'; q++, togo--);
								cw_log(CW_LOG_DEBUG, "%s <- %.*s\n", fm->devlink, q - p, p);
								n = snprintf(buf, sizeof(buf), "<- %.*s\n", q - p, p);
								if (fm->debug_dte >= 0)
									write(fm->debug_dte, buf, n);
								p = q;
							}
							p++, togo--;
						}
					}
#endif
					t31_at_rx(&fm->t31_state, modembuf, len);
					avail -= len;
				}
			}
		}

		pthread_cleanup_pop(1);
	}

	pthread_cleanup_pop(1);
	return NULL;
}

/* \} */


/*! \defgroup driver Fax modem implementation (Callweaver channel side)
 * \{
 */

/*! Request a channel with an instance of the specified driver type.
 *
 * \param type		Driver type
 * \param format	Acceptable formats
 * \param data		User supplied data (from <type>/<data>)
 * \param cause		To be filled in with the reason for failure if the channel could not be provided
 *
 * \return channel
 */
static struct cw_channel *tech_requester(const char *type, int format, void *data, int *cause)
{
	struct cw_channel *chan = NULL;
	struct faxmodem *fm = NULL;
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
			static int rr_next;
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


/*! Connect a channel to given destination or timeout.
 *
 * \param chan		Channel to use
 * \param dest		Destination to connect to
 * \param timeout	Number of seconds to give in after
 */
static int tech_call(struct cw_channel *chan, char *dest, int timeout)
{
	struct tm tm;
	char buf[sizeof("0000+0000")];
	struct faxmodem *fm;
	time_t u_now = time(NULL);

	fm = chan->tech_pvt;

	strftime(buf, sizeof(buf), "%m%d+%H%M", localtime_r(&u_now, &tm));
	buf[4] = '\000';

	cw_mutex_lock(&fm->lock);
	at_reset_call_info(&fm->t31_state.at_state);
	at_set_call_info(&fm->t31_state.at_state, "DATE", buf);
	at_set_call_info(&fm->t31_state.at_state, "TIME", buf+5);
	if ((chan->cid.cid_pres & CW_PRES_RESTRICTION) == CW_PRES_ALLOWED) {
		at_set_call_info(&fm->t31_state.at_state, "NAME", chan->cid.cid_name);
		at_set_call_info(&fm->t31_state.at_state, "NMBR", chan->cid.cid_num);
	} else if ((chan->cid.cid_pres & CW_PRES_RESTRICTION) == CW_PRES_RESTRICTED)
		at_set_call_info(&fm->t31_state.at_state, "NMBR", "P");
	else
		at_set_call_info(&fm->t31_state.at_state, "NMBR", "O");
	at_set_call_info(&fm->t31_state.at_state, "ANID", chan->cid.cid_ani);
	at_set_call_info(&fm->t31_state.at_state, "NDID", chan->cid.cid_dnid);

	fm->state = FAXMODEM_STATE_RINGING;
	cw_setstate(chan, CW_STATE_RINGING);
	pthread_kill(fm->thread, SIGURG);
	cw_mutex_unlock(&fm->lock);

	return 0;
}


/*! Hangup a channel.
 *
 * \param chan		Channel to hangup
 */
static int tech_hangup(struct cw_channel *chan)
{
	struct faxmodem *fm = chan->tech_pvt;

	cw_mutex_lock(&fm->lock);

	if (!pthread_equal(fm->clock_thread, CW_PTHREADT_NULL)) {
		pthread_t tid = fm->clock_thread;
		fm->clock_thread = CW_PTHREADT_NULL;

		cw_mutex_unlock(&fm->lock);
		pthread_cancel(tid);
		pthread_join(tid, NULL);
		cw_mutex_lock(&fm->lock);
	}

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

	fm->state = FAXMODEM_STATE_ONHOOK;

	cw_mutex_unlock(&fm->lock);

	chan->tech_pvt = NULL;

	return 0;
}


/*! Answer an incoming call from the driver instance attached to this channel
 *
 * \param chan		Channel to answer
 */
static int tech_answer(struct cw_channel *chan)
{
	struct faxmodem *fm = chan->tech_pvt;

	cw_mutex_lock(&fm->lock);

	gettimeofday(&fm->frame.delivery, NULL);
	fm->start = fm->frame.delivery;
	fm->frame.ts = fm->frame.seq_no = 0;

	if (!cw_pthread_create(&fm->clock_thread, &global_attr_rr, generic_pipe_clock_thread, &chan->alertpipe[1])) {
		if (cfg_vblevel > 0)
			cw_log(CW_LOG_DEBUG, "%s: connected\n", fm->devlink);
		fm->state = FAXMODEM_STATE_CONNECTED;
		t31_call_event(&fm->t31_state, AT_CALL_EVENT_CONNECTED);
	} else {
		cw_log(CW_LOG_ERROR, "%s: failed to start TX clock thread: %s\n", fm->devlink, strerror(errno));
	}

	cw_mutex_unlock(&fm->lock);

	return 0;
}


/*! Read a frame from a channel
 *
 * \param chan		Channel to read from
 *
 * \return Frame read
 */
static struct cw_frame *tech_read(struct cw_channel *chan)
{
	char buf[256];
	struct faxmodem *fm = chan->tech_pvt;
	struct cw_frame *fr = &fm->frame;
	uint16_t *frame_data = (int16_t *)(fm->fdata + CW_FRIENDLY_OFFSET);
	int samples;

	cw_mutex_lock(&fm->lock);

	if (fm->state == FAXMODEM_STATE_CONNECTED) {
		cw_fr_init_ex(&fm->frame, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
		fm->frame.offset = CW_FRIENDLY_OFFSET;
		fm->frame.data = frame_data;
		memset(frame_data, 0, SAMPLES * sizeof(int16_t));
		fm->frame.samples = 0;
		do {
			samples = t31_tx(&fm->t31_state, frame_data + fm->frame.samples, SAMPLES - fm->frame.samples);
			fm->frame.samples += samples;
		} while (fm->frame.samples < SAMPLES && samples > 0 && fm->state == FAXMODEM_STATE_CONNECTED);
	}

	/* t31_tx processes queued hangups so our state may have changed */
	switch (fm->state) {
		case FAXMODEM_STATE_CONNECTED:
			/* If the frame is short (because t31_tx is changing modem or just gave
			 * in completely) or empty we fill with silence - the other end may
			 * require it.
			 */
			fm->frame.samples = SAMPLES;
			fm->frame.len = SAMPLES / 8;
			cw_tvadd(fm->frame.delivery, cw_samp2tv(SAMPLES, 8000));
			fm->frame.ts += SAMPLES;
			fm->frame.seq_no++;
			fm->frame.datalen = SAMPLES * sizeof(int16_t);
#ifdef TRACE
			if (cfg_vblevel > 3) {
				int n = snprintf(buf, sizeof(buf), "<- audio: %dms %d samples: 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x...\n", cw_tvdiff_ms(fm->frame.delivery, fm->start), fm->frame.samples, frame_data[0], frame_data[1], frame_data[2], frame_data[3], frame_data[4], frame_data[5], frame_data[6], frame_data[7]);
				write(fm->debug_dte, buf, n);
				write(fm->debug_fax[1], frame_data, fm->frame.datalen);
			}
#endif
			break;

		case FAXMODEM_STATE_ANSWERED:
			t31_call_event(&fm->t31_state, AT_CALL_EVENT_ANSWERED);
			fm->state = FAXMODEM_STATE_CONNECTED;
			cw_fr_init_ex(&fm->frame, CW_FRAME_CONTROL, CW_CONTROL_ANSWER);
			break;

		default:
			/* We're onhook */
			fr = NULL;
			break;
	}

	cw_mutex_unlock(&fm->lock);
	return fr;
}


/*! Write a frame to a channel
 *
 * \param chan		Channel to write to
 * \param frame		Frame to write
 */
static int tech_write(struct cw_channel *chan, struct cw_frame *frame)
{
#ifdef TRACE
	char msg[256];
#endif
	struct faxmodem *fm = chan->tech_pvt;

	if (fm->state == FAXMODEM_STATE_CONNECTED) {
		switch (frame->frametype) {
			case CW_FRAME_VOICE:
				cw_mutex_lock(&fm->lock);
#ifdef TRACE
				if (cfg_vblevel > 3) {
					uint16_t *data = frame->data;
					int n;

					n = cw_tvdiff_ms(cw_tvnow(), fm->start);
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

					n = cw_tvdiff_ms(cw_tvnow(), fm->start);
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


static struct cw_frame *tech_exception(struct cw_channel *chan)
{
	return NULL;
}


/*! Indicate a condition (e.g. BUSY, RINGING, CONGESTION)
 *
 * \param chan		Channel to provide indication on
 * \param condition	Condition to be indicated
 */
static int tech_indicate(struct cw_channel *chan, int condition)
{
	struct faxmodem *fm = chan->tech_pvt;
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
			if (!pthread_equal(fm->clock_thread, CW_PTHREADT_NULL)) {
				pthread_cancel(fm->clock_thread);
				pthread_join(fm->clock_thread, NULL);
				fm->clock_thread = CW_PTHREADT_NULL;
			}
			fm->state = FAXMODEM_STATE_ONHOOK;
			cw_mutex_unlock(&fm->lock);
			break;
		default:
			if (cfg_vblevel > 1)
				cw_log(CW_LOG_WARNING, "%s: unknown indication %d\n", fm->devlink, condition);
	}

	return res;
}


/*! Fix up a channel
 *
 * \param oldchan	Old channel
 * \param newchan	New channel
 */
static int tech_fixup(struct cw_channel *oldchan, struct cw_channel *newchan)
{
	return 0;
}


static const struct cw_channel_tech technology = {
	.type = type,
	.description = desc,
	.capabilities = CW_FORMAT_SLINEAR,
	.requester = tech_requester,
	.call = tech_call,
	.hangup = tech_hangup,
	.answer = tech_answer,
	.read = tech_read,
	.write = tech_write,
	.exception = tech_exception,
	.indicate = tech_indicate,
	.fixup = tech_fixup,
};


/*! Create a new channel and attach the given fax modem instance to it */
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
#endif
	}
	
	return chan;
}

/* \} */


/*! \defgroup cli CLI commands
 * \{
 */

/*! Show the status of all configured fax modems */
static int chan_fax_status(int fd, int argc, char *argv[]) 
{
	int x;

	cw_mutex_lock(&control_lock);

	for (x = 0; x < cfg_modems; x++)
		cw_cli(fd, "SLOT %d %s [%s]\n", x, FAXMODEM_POOL[x].devlink, faxmodem_state[FAXMODEM_POOL[x].state]);

	cw_mutex_unlock(&control_lock);

	return 0;
}


/*! Show or set the verbosity level */
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

/* \} */


/*! \defgroup module Module Interface
 * \{
 */

static void activate_fax_modems(void)
{
	static int NEXT_ID = 0;
	pthread_t tid;
	int x;

	cw_mutex_lock(&control_lock);

	if ((FAXMODEM_POOL = calloc(cfg_modems, sizeof(FAXMODEM_POOL[0])))) {
		for (x = 0; x < cfg_modems; x++) {
			snprintf(FAXMODEM_POOL[x].devlink, sizeof(FAXMODEM_POOL[x].devlink), "%s%d", cfg_dev_prefix, NEXT_ID++);
			if (!unlink(FAXMODEM_POOL[x].devlink) && cfg_vblevel > 1)
				cw_log(CW_LOG_WARNING, "%s: removed old symbolic link\n", FAXMODEM_POOL[x].devlink);

			if (cfg_vblevel > 1)
				cw_verbose(VERBOSE_PREFIX_1 "Starting Fax Modem %s\n", FAXMODEM_POOL[x].devlink);

			FAXMODEM_POOL[x].unit = x;
			FAXMODEM_POOL[x].state = FAXMODEM_STATE_CLOSED;
			FAXMODEM_POOL[x].thread = CW_PTHREADT_NULL;
			FAXMODEM_POOL[x].clock_thread = CW_PTHREADT_NULL;

			cw_pthread_create(&FAXMODEM_POOL[x].thread, &global_attr_detached, faxmodem_thread, &FAXMODEM_POOL[x]);
		}
	}

	cw_mutex_unlock(&control_lock);
}


static void deactivate_fax_modems(void)
{
	int x;
	
	cw_mutex_lock(&control_lock);

	/* Tell the threads to die */
	for (x = 0; x < cfg_modems; x++) {
		if (!pthread_equal(FAXMODEM_POOL[x].thread, CW_PTHREADT_NULL)) {
			if (cfg_vblevel > 1)
				cw_verbose(VERBOSE_PREFIX_1 "Stopping Fax Modem SLOT %d\n", x);
			pthread_cancel(FAXMODEM_POOL[x].thread);
		}
	}

	/* Wait for the threads to die */
	for (x = 0; x < cfg_modems; x++) {
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


static void parse_config(void)
{
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


static void graceful_unload(void);

static struct cw_atexit fax_atexit = {
	.name = "FAX Terminate",
	.function = graceful_unload,
};

static void graceful_unload(void)
{
	deactivate_fax_modems();
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

	cw_channel_unregister(&technology);
	cw_cli_unregister_multiple(cli_chan_fax, arraysize(cli_chan_fax));

	if (cfg_dev_prefix)
		free(cfg_dev_prefix);
	if (cfg_context)
		free(cfg_context);
	if (cfg_cid_name)
		free(cfg_cid_name);
	if (cfg_cid_num)
		free(cfg_cid_num);

	cw_atexit_unregister(&fax_atexit);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, desc)

/* \} */
