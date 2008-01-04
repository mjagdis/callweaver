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
 * \brief Convenient Application Routines
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <regex.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/file.h"
#include "callweaver/app.h"
#include "callweaver/dsp.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/indications.h"

#define MAX_OTHER_FORMATS 10


/* 
This function presents a dialtone and reads an extension into 'collect' 
which must be a pointer to a **pre-initilized** array of char having a 
size of 'size' suitable for writing to.  It will collect no more than the smaller 
of 'maxlen' or 'size' minus the original strlen() of collect digits.
*/
int opbx_app_dtget(struct opbx_channel *chan, const char *context, char *collect, size_t size, int maxlen, int timeout) 
{
	struct tone_zone_sound *ts;
	int res=0, x=0;

	if(maxlen > size)
		maxlen = size;
	
	if(!timeout && chan->pbx)
		timeout = chan->pbx->dtimeout;
	else if(!timeout)
		timeout = 5;
	
	ts = opbx_get_indication_tone(chan->zone,"dial");
	if (ts && ts->data[0])
		res = opbx_playtones_start(chan, 0, ts->data, 0);
	else 
		opbx_log(OPBX_LOG_NOTICE,"Huh....? no dial for indications?\n");
	
	for (x = strlen(collect); strlen(collect) < maxlen; ) {
		res = opbx_waitfordigit(chan, timeout);
		if (!opbx_ignore_pattern(context, collect))
			opbx_playtones_stop(chan);
		if (res < 1)
			break;
		collect[x++] = res;
		if (!opbx_matchmore_extension(chan, context, collect, 1, chan->cid.cid_num)) {
			if (collect[x-1] == '#') {
				/* Not a valid extension, ending in #, assume the # was to finish dialing */
				collect[x-1] = '\0';
			}
			break;
		}
	}
	if (res >= 0) {
		if (opbx_exists_extension(chan, context, collect, 1, chan->cid.cid_num))
			res = 1;
		else
			res = 0;
	}
	return res;
}

int opbx_app_getdata(struct opbx_channel *c, char *prompt, char *s, int maxlen, int timeout)
{
	int res,to,fto;
	/* XXX Merge with full version? XXX */
	if (maxlen)
		s[0] = '\0';
	if (prompt) {
		res = opbx_streamfile(c, prompt, c->language);
		if (res < 0)
			return res;
		}
	fto = c->pbx ? c->pbx->rtimeout * 1000 : 6000;
	to = c->pbx ? c->pbx->dtimeout * 1000 : 2000;

	if (timeout > 0)
		fto = to = timeout;
	if (timeout < 0)
		fto = to = 1000000000;
	res = opbx_readstring(c, s, maxlen, to, fto, "#");
	return res;
} 


int opbx_app_getdata_full(struct opbx_channel *c, char *prompt, char *s, int maxlen, int timeout, int audiofd, int ctrlfd)
{
	int res,to,fto;
	if (prompt) {
		res = opbx_streamfile(c, prompt, c->language);
		if (res < 0)
			return res;
	}
	fto = 6000;
	to = 2000;
	if (timeout > 0) 
		fto = to = timeout;
	if (timeout < 0) 
		fto = to = 1000000000;
	res = opbx_readstring_full(c, s, maxlen, to, fto, "#", audiofd, ctrlfd);
	return res;
}

int opbx_app_getvoice(struct opbx_channel *c, char *dest, char *dstfmt, char *prompt, int silence, int maxsec)
{
	int res;
	struct opbx_filestream *writer;
	int rfmt;
	int totalms=0, total;
	
	struct opbx_frame *f;
	struct opbx_dsp *sildet;
	/* Play prompt if requested */
	if (prompt) {
		res = opbx_streamfile(c, prompt, c->language);
		if (res < 0)
			return res;
		res = opbx_waitstream(c,"");
		if (res < 0)
			return res;
	}
	rfmt = c->readformat;
	res = opbx_set_read_format(c, OPBX_FORMAT_SLINEAR);
	if (res < 0) {
		opbx_log(OPBX_LOG_WARNING, "Unable to set to linear mode, giving up\n");
		return -1;
	}
	sildet = opbx_dsp_new();
	if (!sildet) {
		opbx_log(OPBX_LOG_WARNING, "Unable to create silence detector :(\n");
		return -1;
	}
	writer = opbx_writefile(dest, dstfmt, "Voice file", 0, 0, 0666);
	if (!writer) {
		opbx_log(OPBX_LOG_WARNING, "Unable to open file '%s' in format '%s' for writing\n", dest, dstfmt);
		opbx_dsp_free(sildet);
		return -1;
	}
	for(;;) {
		if ((res = opbx_waitfor(c, 2000)) < 0) {
			opbx_log(OPBX_LOG_NOTICE, "Waitfor failed while recording file '%s' format '%s'\n", dest, dstfmt);
			break;
		}
		if (res) {
			f = opbx_read(c);
			if (!f) {
				opbx_log(OPBX_LOG_NOTICE, "Hungup while recording file '%s' format '%s'\n", dest, dstfmt);
				break;
			}
			if ((f->frametype == OPBX_FRAME_DTMF) && (f->subclass == '#')) {
				/* Ended happily with DTMF */
				opbx_fr_free(f);
				break;
			} else if (f->frametype == OPBX_FRAME_VOICE) {
				opbx_dsp_silence(sildet, f, &total); 
				if (total > silence) {
					/* Ended happily with silence */
					opbx_fr_free(f);
					break;
				}
				totalms += f->samples / 8;
				if (totalms > maxsec * 1000) {
					/* Ended happily with too much stuff */
					opbx_log(OPBX_LOG_NOTICE, "Constraining voice on '%s' to %d seconds\n", c->name, maxsec);
					opbx_fr_free(f);
					break;
				}
				res = opbx_writestream(writer, f);
				if (res < 0) {
					opbx_log(OPBX_LOG_WARNING, "Failed to write to stream at %s!\n", dest);
					opbx_fr_free(f);
					break;
				}

			}
			opbx_fr_free(f);
		}
	}
	res = opbx_set_read_format(c, rfmt);
	if (res)
		opbx_log(OPBX_LOG_WARNING, "Unable to restore read format on '%s'\n", c->name);
	opbx_dsp_free(sildet);
	opbx_closestream(writer);
	return 0;
}

static int (*opbx_has_request_t38_func)(const struct opbx_channel *chan) = NULL;

void opbx_install_t38_functions( int (*has_request_t38_func)(const struct opbx_channel *chan) )
{
	opbx_has_request_t38_func = has_request_t38_func;
}

void opbx_uninstall_t38_functions(void)
{
	opbx_has_request_t38_func = NULL;
}

int opbx_app_request_t38(const struct opbx_channel *chan)
{
    if (opbx_has_request_t38_func)
		return opbx_has_request_t38_func(chan);
    return 0;
}



static int (*opbx_has_voicemail_func)(const char *mailbox, const char *folder) = NULL;
static int (*opbx_messagecount_func)(const char *mailbox, int *newmsgs, int *oldmsgs) = NULL;

void opbx_install_vm_functions(int (*has_voicemail_func)(const char *mailbox, const char *folder),
			      int (*messagecount_func)(const char *mailbox, int *newmsgs, int *oldmsgs))
{
	opbx_has_voicemail_func = has_voicemail_func;
	opbx_messagecount_func = messagecount_func;
}

void opbx_uninstall_vm_functions(void)
{
	opbx_has_voicemail_func = NULL;
	opbx_messagecount_func = NULL;
}

int opbx_app_has_voicemail(const char *mailbox, const char *folder)
{
	static int warned = 0;
	if (opbx_has_voicemail_func)
		return opbx_has_voicemail_func(mailbox, folder);

	if ((option_verbose > 2) && !warned) {
		opbx_verbose(VERBOSE_PREFIX_3 "Message check requested for mailbox %s/folder %s but voicemail not loaded.\n", mailbox, folder ? folder : "INBOX");
		warned++;
	}
	return 0;
}


int opbx_app_messagecount(const char *mailbox, int *newmsgs, int *oldmsgs)
{
	static int warned = 0;
	if (newmsgs)
		*newmsgs = 0;
	if (oldmsgs)
		*oldmsgs = 0;
	if (opbx_messagecount_func)
		return opbx_messagecount_func(mailbox, newmsgs, oldmsgs);

	if (!warned && (option_verbose > 2)) {
		warned++;
		opbx_verbose(VERBOSE_PREFIX_3 "Message count requested for mailbox %s but voicemail not loaded.\n", mailbox);
	}

	return 0;
}

int opbx_dtmf_stream(struct opbx_channel *chan,struct opbx_channel *peer,char *digits,int between) 
{
	char *ptr;
	int res = 0;
	struct opbx_frame f;
	if (!between)
		between = 100;

	if (peer)
		res = opbx_autoservice_start(peer);

	if (!res) {
		res = opbx_waitfor(chan,100);
		if (res > -1) {
			for (ptr=digits; *ptr; ptr++) {
				if (*ptr == 'w') {
					res = opbx_safe_sleep(chan, 500);
					if (res) 
						break;
					continue;
				}
                opbx_fr_init_ex(&f, OPBX_FRAME_DTMF, *ptr, NULL);
				f.src = "opbx_dtmf_stream";
				if (strchr("0123456789*#abcdABCD",*ptr) == NULL)
                {
					opbx_log(OPBX_LOG_WARNING, "Illegal DTMF character '%c' in string. (0-9*#aAbBcCdD allowed)\n",*ptr);
				}
                else
                {
					res = opbx_write(chan, &f);
					if (res) 
						break;
					/* pause between digits */
					res = opbx_safe_sleep(chan,between);
					if (res) 
						break;
				}
			}
		}
		if (peer)
			res = opbx_autoservice_stop(peer);
	}
	return res;
}

int opbx_control_streamfile(struct opbx_channel *chan, const char *file,
			   const char *fwd, const char *rev,
			   const char *stop, const char *pause,
			   const char *restart, int skipms) 
{
	long elapsed = 0, last_elapsed = 0;
	char *breaks = NULL;
	char *end = NULL;
	int blen = 2;
	int res;

	if (stop)
		blen += strlen(stop);
	if (pause)
		blen += strlen(pause);
	if (restart)
		blen += strlen(restart);

	if (blen > 2) {
		breaks = alloca(blen + 1);
		breaks[0] = '\0';
		if (stop)
			strcat(breaks, stop);
		if (pause)
			strcat(breaks, pause);
		if (restart)
			strcat(breaks, restart);
	}
	if (chan->_state != OPBX_STATE_UP)
		res = opbx_answer(chan);

	if (chan)
		opbx_stopstream(chan);

	if (file) {
		if ((end = strchr(file,':'))) {
			if (!strcasecmp(end, ":end")) {
				*end = '\0';
				end++;
			}
		}
	}

	for (;;) {
		struct timeval started = opbx_tvnow();

		if (chan)
			opbx_stopstream(chan);
		res = opbx_streamfile(chan, file, chan->language);
		if (!res) {
			if (end) {
				opbx_seekstream(chan->stream, 0, SEEK_END);
				end=NULL;
			}
			res = 1;
			if (elapsed) {
				opbx_stream_fastforward(chan->stream, elapsed);
				last_elapsed = elapsed - 200;
			}
			if (res)
				res = opbx_waitstream_fr(chan, breaks, fwd, rev, skipms);
			else
				break;
		}

		if (res < 1)
			break;

		/* We go at next loop if we got the restart char */
		if (restart && strchr(restart, res)) {
			opbx_log(OPBX_LOG_DEBUG, "we'll restart the stream here at next loop\n");
			elapsed=0; /* To make sure the next stream will start at beginning */
			continue;
		}

		if (pause != NULL && strchr(pause, res)) {
			elapsed = opbx_tvdiff_ms(opbx_tvnow(), started) + last_elapsed;
			for(;;) {
				if (chan)
					opbx_stopstream(chan);
				res = opbx_waitfordigit(chan, 1000);
				if (res == 0)
					continue;
				else if (res == -1 || strchr(pause, res) || (stop && strchr(stop, res)))
					break;
			}
			if (res == *pause) {
				res = 0;
				continue;
			}
		}
		if (res == -1)
			break;

		/* if we get one of our stop chars, return it to the calling function */
		if (stop && strchr(stop, res)) {
			/* res = 0; */
			break;
		}
	}
	if (chan)
		opbx_stopstream(chan);

	return res;
}

int opbx_play_and_wait(struct opbx_channel *chan, const char *fn)
{
	int d;
	d = opbx_streamfile(chan, fn, chan->language);
	if (d)
		return d;
	d = opbx_waitstream(chan, OPBX_DIGIT_ANY);
	opbx_stopstream(chan);
	return d;
}

static int global_silence_threshold = 128;
static int global_maxsilence = 0;

int opbx_play_and_record(struct opbx_channel *chan, const char *playfile, const char *recordfile, int maxtime, const char *fmt, int *duration, int silencethreshold, int maxsilence, const char *path)
{
	int d;
	char *fmts;
	char comment[256];
	int x, fmtcnt=1, res=-1,outmsg=0;
	struct opbx_frame *f;
	struct opbx_filestream *others[MAX_OTHER_FORMATS];
	char *sfmt[MAX_OTHER_FORMATS];
	char *stringp=NULL;
	time_t start, end;
	struct opbx_dsp *sildet=NULL;   	/* silence detector dsp */
	int totalsilence = 0;
	int dspsilence = 0;
	int gotsilence = 0;		/* did we timeout for silence? */
	int rfmt=0;

	if (silencethreshold < 0)
		silencethreshold = global_silence_threshold;

	if (maxsilence < 0)
		maxsilence = global_maxsilence;

	/* barf if no pointer passed to store duration in */
	if (duration == NULL) {
		opbx_log(OPBX_LOG_WARNING, "Error play_and_record called without duration pointer\n");
		return -1;
	}

	opbx_log(OPBX_LOG_DEBUG,"play_and_record: %s, %s, '%s'\n", playfile ? playfile : "<None>", recordfile, fmt);
	snprintf(comment,sizeof(comment),"Playing %s, Recording to: %s on %s\n", playfile ? playfile : "<None>", recordfile, chan->name);

	if (playfile) {
		d = opbx_play_and_wait(chan, playfile);
		if (d > -1)
			d = opbx_streamfile(chan, "beep",chan->language);
		if (!d)
			d = opbx_waitstream(chan,"");
		if (d < 0)
			return -1;
	}

	fmts = opbx_strdupa(fmt);

	stringp=fmts;
	strsep(&stringp, "|,");
	opbx_log(OPBX_LOG_DEBUG,"Recording Formats: sfmts=%s\n", fmts);
	sfmt[0] = opbx_strdupa(fmts);

	while((fmt = strsep(&stringp, "|,"))) {
		if (fmtcnt > MAX_OTHER_FORMATS - 1) {
			opbx_log(OPBX_LOG_WARNING, "Please increase MAX_OTHER_FORMATS in app_voicemail.c\n");
			break;
		}
		sfmt[fmtcnt++] = opbx_strdupa(fmt);
	}

	time(&start);
	end=start;  /* pre-initialize end to be same as start in case we never get into loop */
	for (x=0;x<fmtcnt;x++) {
		others[x] = opbx_writefile(recordfile, sfmt[x], comment, O_TRUNC, 0, 0700);
		opbx_verbose( VERBOSE_PREFIX_3 "x=%d, open writing:  %s format: %s, %p\n", x, recordfile, sfmt[x], others[x]);

		if (!others[x]) {
			break;
		}
	}

	if (path)
		opbx_unlock_path(path);


	
	if (maxsilence > 0) {
		sildet = opbx_dsp_new(); /* Create the silence detector */
		if (!sildet) {
			opbx_log(OPBX_LOG_WARNING, "Unable to create silence detector :(\n");
			return -1;
		}
		opbx_dsp_set_threshold(sildet, silencethreshold);
		rfmt = chan->readformat;
		res = opbx_set_read_format(chan, OPBX_FORMAT_SLINEAR);
		if (res < 0) {
			opbx_log(OPBX_LOG_WARNING, "Unable to set to linear mode, giving up\n");
			opbx_dsp_free(sildet);
			return -1;
		}
	}
	/* Request a video update */
	opbx_indicate(chan, OPBX_CONTROL_VIDUPDATE);

	if (x == fmtcnt) {
	/* Loop forever, writing the packets we read to the writer(s), until
	   we read a # or get a hangup */
		f = NULL;
		for(;;) {
		 	res = opbx_waitfor(chan, 2000);
			if (!res) {
				opbx_log(OPBX_LOG_DEBUG, "One waitfor failed, trying another\n");
				/* Try one more time in case of masq */
			 	res = opbx_waitfor(chan, 2000);
				if (!res) {
					opbx_log(OPBX_LOG_WARNING, "No audio available on %s??\n", chan->name);
					res = -1;
				}
			}

			if (res < 0) {
				f = NULL;
				break;
			}
			f = opbx_read(chan);
			if (!f)
				break;
			if (f->frametype == OPBX_FRAME_VOICE) {
				/* write each format */
				for (x=0;x<fmtcnt;x++) {
					res = opbx_writestream(others[x], f);
				}

				/* Silence Detection */
				if (maxsilence > 0) {
					dspsilence = 0;
					opbx_dsp_silence(sildet, f, &dspsilence);
					if (dspsilence)
						totalsilence = dspsilence;
					else
						totalsilence = 0;

					if (totalsilence > maxsilence) {
						/* Ended happily with silence */
						if (option_verbose > 2)
							opbx_verbose( VERBOSE_PREFIX_3 "Recording automatically stopped after a silence of %d seconds\n", totalsilence/1000);
						opbx_fr_free(f);
						gotsilence = 1;
						outmsg=2;
						break;
					}
				}
				/* Exit on any error */
				if (res) {
					opbx_log(OPBX_LOG_WARNING, "Error writing frame\n");
					opbx_fr_free(f);
					break;
				}
			} else if (f->frametype == OPBX_FRAME_VIDEO) {
				/* Write only once */
				opbx_writestream(others[0], f);
			} else if (f->frametype == OPBX_FRAME_DTMF) {
				if (f->subclass == '#') {
					if (option_verbose > 2)
						opbx_verbose( VERBOSE_PREFIX_3 "User ended message by pressing %c\n", f->subclass);
					res = '#';
					outmsg = 2;
					opbx_fr_free(f);
					break;
				}
				if (f->subclass == '0') {
				/* Check for a '0' during message recording also, in case caller wants operator */
					if (option_verbose > 2)
						opbx_verbose(VERBOSE_PREFIX_3 "User cancelled by pressing %c\n", f->subclass);
					res = '0';
					outmsg = 0;
					opbx_fr_free(f);
					break;
				}
			}
			if (maxtime) {
				time(&end);
				if (maxtime < (end - start)) {
					if (option_verbose > 2)
						opbx_verbose( VERBOSE_PREFIX_3 "Took too long, cutting it short...\n");
					outmsg = 2;
					res = 't';
					opbx_fr_free(f);
					break;
				}
			}
			opbx_fr_free(f);
		}
		if (end == start) time(&end);
		if (!f) {
			if (option_verbose > 2)
				opbx_verbose( VERBOSE_PREFIX_3 "User hung up\n");
			res = -1;
			outmsg=1;
		}
	} else {
		opbx_log(OPBX_LOG_WARNING, "Error creating writestream '%s', format '%s'\n", recordfile, sfmt[x]);
	}

	*duration = end - start;

	for (x=0;x<fmtcnt;x++) {
		if (!others[x])
			break;
		if (res > 0) {
			if (totalsilence)
				opbx_stream_rewind(others[x], totalsilence-200);
			else
				opbx_stream_rewind(others[x], 200);
		}
		opbx_truncstream(others[x]);
		opbx_closestream(others[x]);
	}
	if (rfmt) {
		if (opbx_set_read_format(chan, rfmt)) {
			opbx_log(OPBX_LOG_WARNING, "Unable to restore format %s to channel '%s'\n", opbx_getformatname(rfmt), chan->name);
		}
	}
	if (outmsg > 1) {
		/* Let them know recording is stopped */
		if(!opbx_streamfile(chan, "auth-thankyou", chan->language))
			opbx_waitstream(chan, "");
	}
	if (sildet)
		opbx_dsp_free(sildet);
	return res;
}

int opbx_play_and_prepend(struct opbx_channel *chan, char *playfile, char *recordfile, int maxtime, char *fmt, int *duration, int beep, int silencethreshold, int maxsilence)
{
	int d = 0;
	char *fmts;
	char comment[256];
	int x, fmtcnt=1, res=-1,outmsg=0;
	struct opbx_frame *f;
	struct opbx_filestream *others[MAX_OTHER_FORMATS];
	struct opbx_filestream *realfiles[MAX_OTHER_FORMATS];
	char *sfmt[MAX_OTHER_FORMATS];
	char *stringp=NULL;
	time_t start, end;
	struct opbx_dsp *sildet;   	/* silence detector dsp */
	int totalsilence = 0;
	int dspsilence = 0;
	int gotsilence = 0;		/* did we timeout for silence? */
	int rfmt=0;	
	char prependfile[80];
	
	if (silencethreshold < 0)
		silencethreshold = global_silence_threshold;

	if (maxsilence < 0)
		maxsilence = global_maxsilence;

	/* barf if no pointer passed to store duration in */
	if (duration == NULL) {
		opbx_log(OPBX_LOG_WARNING, "Error play_and_prepend called without duration pointer\n");
		return -1;
	}

	opbx_log(OPBX_LOG_DEBUG,"play_and_prepend: %s, %s, '%s'\n", playfile ? playfile : "<None>", recordfile, fmt);
	snprintf(comment,sizeof(comment),"Playing %s, Recording to: %s on %s\n", playfile ? playfile : "<None>", recordfile, chan->name);

	if (playfile || beep) {	
		if (!beep)
			d = opbx_play_and_wait(chan, playfile);
		if (d > -1)
			d = opbx_streamfile(chan, "beep",chan->language);
		if (!d)
			d = opbx_waitstream(chan,"");
		if (d < 0)
			return -1;
	}
	opbx_copy_string(prependfile, recordfile, sizeof(prependfile));	
	strncat(prependfile, "-prepend", sizeof(prependfile) - strlen(prependfile) - 1);
			
	fmts = opbx_strdupa(fmt);
	
	stringp=fmts;
	strsep(&stringp, "|,");
	opbx_log(OPBX_LOG_DEBUG,"Recording Formats: sfmts=%s\n", fmts);	
	sfmt[0] = opbx_strdupa(fmts);
	
	while((fmt = strsep(&stringp, "|,"))) {
		if (fmtcnt > MAX_OTHER_FORMATS - 1) {
			opbx_log(OPBX_LOG_WARNING, "Please increase MAX_OTHER_FORMATS in app_voicemail.c\n");
			break;
		}
		sfmt[fmtcnt++] = opbx_strdupa(fmt);
	}

	time(&start);
	end=start;  /* pre-initialize end to be same as start in case we never get into loop */
	for (x=0;x<fmtcnt;x++) {
		others[x] = opbx_writefile(prependfile, sfmt[x], comment, O_TRUNC, 0, 0700);
		opbx_verbose( VERBOSE_PREFIX_3 "x=%d, open writing:  %s format: %s, %p\n", x, prependfile, sfmt[x], others[x]);
		if (!others[x]) {
			break;
		}
	}
	
	sildet = opbx_dsp_new(); /* Create the silence detector */
	if (!sildet) {
		opbx_log(OPBX_LOG_WARNING, "Unable to create silence detector :(\n");
		return -1;
	}
	opbx_dsp_set_threshold(sildet, silencethreshold);

	if (maxsilence > 0) {
		rfmt = chan->readformat;
		res = opbx_set_read_format(chan, OPBX_FORMAT_SLINEAR);
		if (res < 0) {
			opbx_log(OPBX_LOG_WARNING, "Unable to set to linear mode, giving up\n");
			return -1;
		}
	}
						
	if (x == fmtcnt) {
	/* Loop forever, writing the packets we read to the writer(s), until
	   we read a # or get a hangup */
		f = NULL;
		for(;;) {
		 	res = opbx_waitfor(chan, 2000);
			if (!res) {
				opbx_log(OPBX_LOG_DEBUG, "One waitfor failed, trying another\n");
				/* Try one more time in case of masq */
			 	res = opbx_waitfor(chan, 2000);
				if (!res) {
					opbx_log(OPBX_LOG_WARNING, "No audio available on %s??\n", chan->name);
					res = -1;
				}
			}
			
			if (res < 0) {
				f = NULL;
				break;
			}
			f = opbx_read(chan);
			if (!f)
				break;
			if (f->frametype == OPBX_FRAME_VOICE) {
				/* write each format */
				for (x=0;x<fmtcnt;x++) {
					if (!others[x])
						break;
					res = opbx_writestream(others[x], f);
				}
				
				/* Silence Detection */
				if (maxsilence > 0) {
					dspsilence = 0;
					opbx_dsp_silence(sildet, f, &dspsilence);
					if (dspsilence)
						totalsilence = dspsilence;
					else
						totalsilence = 0;
					
					if (totalsilence > maxsilence) {
					/* Ended happily with silence */
					if (option_verbose > 2) 
						opbx_verbose( VERBOSE_PREFIX_3 "Recording automatically stopped after a silence of %d seconds\n", totalsilence/1000);
					opbx_fr_free(f);
					gotsilence = 1;
					outmsg=2;
					break;
					}
				}
				/* Exit on any error */
				if (res) {
					opbx_log(OPBX_LOG_WARNING, "Error writing frame\n");
					opbx_fr_free(f);
					break;
				}
			} else if (f->frametype == OPBX_FRAME_VIDEO) {
				/* Write only once */
				opbx_writestream(others[0], f);
			} else if (f->frametype == OPBX_FRAME_DTMF) {
				/* stop recording with any digit */
				if (option_verbose > 2) 
					opbx_verbose( VERBOSE_PREFIX_3 "User ended message by pressing %c\n", f->subclass);
				res = 't';
				outmsg = 2;
				opbx_fr_free(f);
				break;
			}
			if (maxtime) {
				time(&end);
				if (maxtime < (end - start)) {
					if (option_verbose > 2)
						opbx_verbose( VERBOSE_PREFIX_3 "Took too long, cutting it short...\n");
					res = 't';
					outmsg=2;
					opbx_fr_free(f);
					break;
				}
			}
			opbx_fr_free(f);
		}
		if (end == start)
            time(&end);
		if (!f) {
			if (option_verbose > 2) 
				opbx_verbose( VERBOSE_PREFIX_3 "User hung up\n");
			res = -1;
			outmsg=1;
#if 0
			/* delete all the prepend files */
			for (x=0;x<fmtcnt;x++) {
				if (!others[x])
					break;
				opbx_closestream(others[x]);
				opbx_filedelete(prependfile, sfmt[x]);
			}
#endif
		}
	} else {
		opbx_log(OPBX_LOG_WARNING, "Error creating writestream '%s', format '%s'\n", prependfile, sfmt[x]); 
	}
	*duration = end - start;
#if 0
	if (outmsg > 1) {
#else
	if (outmsg) {
#endif
		struct opbx_frame *fr;
		for (x=0;x<fmtcnt;x++) {
			snprintf(comment, sizeof(comment), "Opening the real file %s.%s\n", recordfile, sfmt[x]);
			realfiles[x] = opbx_readfile(recordfile, sfmt[x], comment, O_RDONLY, 0, 0);
			if (!others[x] || !realfiles[x])
				break;
			if (totalsilence)
				opbx_stream_rewind(others[x], totalsilence-200);
			else
				opbx_stream_rewind(others[x], 200);
			opbx_truncstream(others[x]);
			/* add the original file too */
			while ((fr = opbx_readframe(realfiles[x]))) {
				opbx_writestream(others[x],fr);
			}
			opbx_closestream(others[x]);
			opbx_closestream(realfiles[x]);
			opbx_filerename(prependfile, recordfile, sfmt[x]);
#if 0
			opbx_verbose("Recording Format: sfmts=%s, prependfile %s, recordfile %s\n", sfmt[x],prependfile,recordfile);
#endif
			opbx_filedelete(prependfile, sfmt[x]);
		}
	}
	if (rfmt) {
		if (opbx_set_read_format(chan, rfmt)) {
			opbx_log(OPBX_LOG_WARNING, "Unable to restore format %s to channel '%s'\n", opbx_getformatname(rfmt), chan->name);
		}
	}
	if (outmsg) {
		if (outmsg > 1) {
			/* Let them know it worked */
			opbx_streamfile(chan, "auth-thankyou", chan->language);
			opbx_waitstream(chan, "");
		}
	}	
	return res;
}

/* Channel group core functions */

int opbx_app_group_split_group(char *data, char *group, int group_max, char *category, int category_max)
{
	int res=0;
	char tmp[256];
	char *grp=NULL, *cat=NULL;

	if (!opbx_strlen_zero(data)) {
		opbx_copy_string(tmp, data, sizeof(tmp));
		grp = tmp;
		cat = strchr(tmp, '@');
		if (cat) {
			*cat = '\0';
			cat++;
		}
	}

	if (!opbx_strlen_zero(grp))
		opbx_copy_string(group, grp, group_max);
	else
		res = -1;

	if (cat)
		snprintf(category, category_max, "%s_%s", GROUP_CATEGORY_PREFIX, cat);
	else
		opbx_copy_string(category, GROUP_CATEGORY_PREFIX, category_max);

	return res;
}

int opbx_app_group_set_channel(struct opbx_channel *chan, char *data)
{
	int res=0;
	char group[80] = "";
	char category[80] = "";

	if (!opbx_app_group_split_group(data, group, sizeof(group), category, sizeof(category))) {
		pbx_builtin_setvar_helper(chan, category, group);
	} else
		res = -1;

	return res;
}

int opbx_app_group_get_count(char *group, char *category)
{
	struct opbx_channel *chan;
	int count = 0;
	char *test;
	char cat[80];
	char *s;

	if (opbx_strlen_zero(group))
		return 0;

 	s = (!opbx_strlen_zero(category)) ? category : GROUP_CATEGORY_PREFIX;
	opbx_copy_string(cat, s, sizeof(cat));

	chan = NULL;
	while ((chan = opbx_channel_walk_locked(chan)) != NULL) {
 		test = pbx_builtin_getvar_helper(chan, cat);
		if (test && !strcasecmp(test, group))
 			count++;
		opbx_mutex_unlock(&chan->lock);
	}

	return count;
}

int opbx_app_group_match_get_count(char *groupmatch, char *category)
{
	regex_t regexbuf;
	struct opbx_channel *chan;
	int count = 0;
	char *test;
	char cat[80];
	char *s;

	if (opbx_strlen_zero(groupmatch))
		return 0;

	/* if regex compilation fails, return zero matches */
	if (regcomp(&regexbuf, groupmatch, REG_EXTENDED | REG_NOSUB))
		return 0;

	s = (!opbx_strlen_zero(category)) ? category : GROUP_CATEGORY_PREFIX;
	opbx_copy_string(cat, s, sizeof(cat));

	chan = NULL;
	while ((chan = opbx_channel_walk_locked(chan)) != NULL) {
		test = pbx_builtin_getvar_helper(chan, cat);
		if (test && !regexec(&regexbuf, test, 0, NULL, 0))
			count++;
		opbx_mutex_unlock(&chan->lock);
	}

	regfree(&regexbuf);

	return count;
}

int opbx_separate_app_args(char *buf, char delim, int max_args, char **argv)
{
	char *start;
	int argc;
	char c;

	if (option_debug && option_verbose > 6)
		opbx_log(OPBX_LOG_DEBUG, "delim='%c', args: %s\n", delim, buf);

	/* The last argv is reserved for NULL. This is required if you want
	 * to hand off an argv to exec(2) for example.
	 */
	max_args--;

	argc = 0;
	if (buf) {
		start = buf;
		do {
			char *next, *end;
			int parens, inquote;

			/* Skip leading white space */
			while (isspace(*start)) start++;

			next = end = start;

			/* Find the end of this arg. Backslash removes any special
			 * meaning from the next character. Otherwise quotes
			 * enclose strings and parentheses (outside any quoted
			 * string) must balance.
			 */
			inquote = parens = 0;
			for (; *next; next++) {
				if (*next == '\\') {
					if (!*(++next)) break;
				} else if (*next == '"') {
					inquote = !inquote;
					continue;
				} else if (*next == '(')
					parens++;
				else if (*next == ')')
					parens--;
				else if (*next == delim && !parens && !inquote)
					break;

				*(end++) = *next;
			}

			/* Note whether we hit a delimiter or '\0' in case
			 * we're about to overwrite it
			 */
			c = *next;

			/* Terminate and backtrack trimming off trailing whitespace */
			*end = '\0';
			while (end > start && isspace(end[-1]))
				*(--end) = '\0';

			/* Save the arg and its length if wanted */
			argv[argc] = start;
#if 0
			if (argl) argl[argc] = end - start;
#endif
			argc++;

			start = next + 1;
		} while (c && argc < max_args);
	}

	if (argc == 1 && !argv[0][0])
		argc--;

	argv[argc] = NULL;

	if (option_debug && option_verbose > 5) {
		int i;
		opbx_log(OPBX_LOG_DEBUG, "argc: %d\n", argc);
		for (i=0; i<argc; i++)
			opbx_log(OPBX_LOG_DEBUG, "argv[%d]: %s\n", i, argv[i]);
	}

	return argc;
}


enum OPBX_LOCK_RESULT opbx_lock_path(const char *path)
{
	char *s;
	char *fs;
	int res;
	int fd;
	time_t start;

	s = alloca(strlen(path) + 10);
	fs = alloca(strlen(path) + 20);

	snprintf(fs, strlen(path) + 19, "%s/.lock-%08lx", path, opbx_random());
	fd = open(fs, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0) {
		opbx_log(OPBX_LOG_ERROR,"Unable to create lock file '%s': %s\n", path, strerror(errno));
		return OPBX_LOCK_PATH_NOT_FOUND;
	}
	close(fd);

	snprintf(s, strlen(path) + 9, "%s/.lock", path);
	time(&start);
	while (((res = link(fs, s)) < 0) && (errno == EEXIST) && (time(NULL) - start < 5))
		usleep(1);

	unlink(fs);

	if (res) {
		opbx_log(OPBX_LOG_WARNING, "Failed to lock path '%s': %s\n", path, strerror(errno));
		return OPBX_LOCK_TIMEOUT;
	} else {
		unlink(fs);
		opbx_log(OPBX_LOG_DEBUG, "Locked path '%s'\n", path);
		return OPBX_LOCK_SUCCESS;
	}
}

int opbx_unlock_path(const char *path)
{
	char *s;
	int res;

	s = alloca(strlen(path) + 10);
	snprintf(s, strlen(path) + 9, "%s/%s", path, ".lock");

	if ((res = unlink(s)))
		opbx_log(OPBX_LOG_ERROR, "Could not unlock path '%s': %s\n", path, strerror(errno));
	else
		opbx_log(OPBX_LOG_DEBUG, "Unlocked path '%s'\n", path);

	return res;
}

int opbx_record_review(struct opbx_channel *chan, const char *playfile, const char *recordfile, int maxtime, const char *fmt, int *duration, const char *path) 
{
	int silencethreshold = 128; 
	int maxsilence=0;
	int res = 0;
	int cmd = 0;
	int max_attempts = 3;
	int attempts = 0;
	int recorded = 0;
	int message_exists = 0;
	/* Note that urgent and private are for flagging messages as such in the future */

	/* barf if no pointer passed to store duration in */
	if (duration == NULL) {
		opbx_log(OPBX_LOG_WARNING, "Error opbx_record_review called without duration pointer\n");
		return -1;
	}

	cmd = '3';	 /* Want to start by recording */

	while ((cmd >= 0) && (cmd != 't')) {
		switch (cmd) {
		case '1':
			if (!message_exists) {
				/* In this case, 1 is to record a message */
				cmd = '3';
				break;
			} else {
				opbx_streamfile(chan, "vm-msgsaved", chan->language);
				opbx_waitstream(chan, "");
				cmd = 't';
				return res;
			}
		case '2':
			/* Review */
			opbx_verbose(VERBOSE_PREFIX_3 "Reviewing the recording\n");
			opbx_streamfile(chan, recordfile, chan->language);
			cmd = opbx_waitstream(chan, OPBX_DIGIT_ANY);
			break;
		case '3':
			message_exists = 0;
			/* Record */
			if (recorded == 1)
				opbx_verbose(VERBOSE_PREFIX_3 "Re-recording\n");
			else	
				opbx_verbose(VERBOSE_PREFIX_3 "Recording\n");
			recorded = 1;
			cmd = opbx_play_and_record(chan, playfile, recordfile, maxtime, fmt, duration, silencethreshold, maxsilence, path);
			if (cmd == -1) {
			/* User has hung up, no options to give */
				return cmd;
			}
			if (cmd == '0') {
				break;
			} else if (cmd == '*') {
				break;
			} 
			else {
				/* If all is well, a message exists */
				message_exists = 1;
				cmd = 0;
			}
			break;
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case '*':
		case '#':
			cmd = opbx_play_and_wait(chan, "vm-sorry");
			break;
		default:
			if (message_exists) {
				cmd = opbx_play_and_wait(chan, "vm-review");
			}
			else {
				cmd = opbx_play_and_wait(chan, "vm-torerecord");
				if (!cmd)
					cmd = opbx_waitfordigit(chan, 600);
			}
			
			if (!cmd)
				cmd = opbx_waitfordigit(chan, 6000);
			if (!cmd) {
				attempts++;
			}
			if (attempts > max_attempts) {
				cmd = 't';
			}
		}
	}
	if (cmd == 't')
		cmd = 0;
	return cmd;
}


char *opbx_read_textfile(const char *filename)
{
	int fd;
	char *output=NULL;
	struct stat filesize;
	int count=0;
	int res;
	if(stat(filename,&filesize)== -1){
		opbx_log(OPBX_LOG_WARNING,"Error can't stat %s\n", filename);
		return NULL;
	}
	count=filesize.st_size + 1;
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		opbx_log(OPBX_LOG_WARNING, "Cannot open file '%s' for reading: %s\n", filename, strerror(errno));
		return NULL;
	}
	output=(char *)malloc(count);
	if (output) {
		res = read(fd, output, count - 1);
		if (res == count - 1) {
			output[res] = '\0';
		} else {
			opbx_log(OPBX_LOG_WARNING, "Short read of %s (%d of %d): %s\n", filename, res, count -  1, strerror(errno));
			free(output);
			output = NULL;
		}
	} else 
		opbx_log(OPBX_LOG_WARNING, "Out of memory!\n");
	close(fd);
	return output;
}

int opbx_parseoptions(const struct opbx_option *options, struct opbx_flags *flags, char **args, char *optstr)
{
	char *s;
	int curarg;
	int argloc;
	char *arg;
	int res = 0;

	flags->flags = 0;

	if (!optstr)
		return 0;

	s = optstr;
	while (*s) {
		curarg = *s & 0x7f;
		flags->flags |= options[curarg].flag;
		argloc = options[curarg].arg_index;
		s++;
		if (*s == '(') {
			/* Has argument */
			s++;
			arg = s;
			while (*s && (*s != ')')) s++;
			if (*s) {
				if (argloc)
					args[argloc - 1] = arg;
				*s = '\0';
				s++;
			} else {
				opbx_log(OPBX_LOG_WARNING, "Missing closing parenthesis for argument '%c' in string '%s'\n", curarg, arg);
				res = -1;
			}
		} else if (argloc)
			args[argloc - 1] = NULL;
	}
	return res;
}
