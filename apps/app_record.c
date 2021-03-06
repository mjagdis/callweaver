/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Matthew Fredrickson <creslin@digium.com>
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
 * \brief Trivial application to record a sound file
 *
 */
#include <stdio.h> 
#include <string.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/dsp.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"

static const char tdesc[] = "Trivial Record Application";

static void *record_app;
static const char record_name[] = "Record";
static const char record_synopsis[] = "Record to a file";
static const char record_syntax[] = "Record(filename.format[, silence[, maxduration[, options]]])";
static const char record_descrip[] = 
"Records from the channel into a given filename. If the file exists it will\n"
"be overwritten.\n"
"- 'format' is the format of the file type to be recorded (wav, gsm, etc).\n"
"- 'silence' is the number of seconds of silence to allow before returning.\n"
"- 'maxduration' is the maximum recording duration in seconds. If missing\n"
"or 0 there is no maximum.\n"
"- 'options' may contain any of the following letters:\n"
"     's' : skip recording if the line is not yet answered\n"
"     'n' : do not answer, but record anyway if line not yet answered\n"
"     'a' : append to existing recording rather than replacing\n"
"     't' : use alternate '*' terminator key instead of default '#'\n"
"     'q' : quiet (do not play a beep tone)\n"
"\n"
"If filename contains '%d', these characters will be replaced with a number\n"
"incremented by one each time the file is recorded. \n\n"
"Use 'show file formats' to see the available formats on your system\n\n"
"User can press '#' to terminate the recording and continue to the next priority.\n\n"
"Returns -1 when the user hangs up.\n";


static int record_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct cw_dynstr filename = CW_DYNSTR_INIT;
	struct cw_filestream *s = '\0';
	struct localuser *u;
	struct cw_frame *f = NULL;
	char *ext = NULL;
	struct cw_dsp *sildet = NULL;   	/* silence detector dsp */
	int res = -1;
	int count = 0;
	int totalsilence = 0;
	int dspsilence = 0;
	int silence = 0;		/* amount of silence to allow */
	int gotsilence = 0;		/* did we timeout for silence? */
	int maxduration = 0;		/* max duration of recording in milliseconds */
	int gottimeout = 0;		/* did we timeout for maxduration exceeded? */
	int option_skip = 0;
	int option_noanswer = 0;
	int option_append = 0;
	int terminator = '#';
	int option_quiet = 0;
	int rfmt = 0;
	int flags;

	CW_UNUSED(result);

	if (argc < 1 || argc > 4 || !argv[0][0])
		return cw_function_syntax(record_syntax);

	LOCAL_USER_ADD(u);

	ext = strrchr(argv[0], '.'); /* to support filename with a . in the filename, not format */
	if (!ext)
		ext = strchr(argv[0], ':');
	if (ext) {
		*ext = '\0';
		ext++;
	}

	if (!ext) {
		cw_log(CW_LOG_WARNING, "No extension specified to filename!\n");
		goto out;
	}

	if (argc > 1) {
		silence = atoi(argv[1]);
		if (silence > 0)
			silence *= 1000;
		else if (silence < 0)
			silence = 0;
	}
	
	if (argc > 2) {
		maxduration = atoi(argv[2]);
		if (maxduration > 0)
			maxduration *= 1000;
		else if (maxduration < 0)
			maxduration = 0;
	}

	if (argc > 3) {
		/* Retain backwards compatibility with old style options */
		if (!strcasecmp(argv[3], "skip"))
			option_skip = 1;
		else if (!strcasecmp(argv[3], "noanswer"))
			option_noanswer = 1;
		else {
			if (strchr(argv[3], 's'))
				option_skip = 1;
			if (strchr(argv[3], 'n'))
				option_noanswer = 1;
			if (strchr(argv[3], 'a'))
				option_append = 1;
			if (strchr(argv[3], 't'))
				terminator = '*';
			if (strchr(argv[3], 'q'))
				option_quiet = 1;
		}
	}
	
	/* done parsing */
	
	/* these are to allow the use of the %d in the config file for a wild card of sort to
	  create a new file with the inputed name scheme */
	do {
		char *p = argv[0];

		cw_dynstr_reset(&filename);

		while (p[0]) {
			int n = strcspn(p, "%");
			cw_dynstr_printf(&filename, "%.*s", n, p);
			if (!p[n])
				break;
			p += n + 1;
			switch (p[0]) {
				case 'd':
					cw_dynstr_printf(&filename, "%d", count++);
					p++;
					break;
				default:
					cw_dynstr_printf(&filename, "%%%c", p[0]);
					if (p[0])
						p++;
					break;
			}
		}

		if (filename.error)
			goto out;
	} while (cw_fileexists(filename.data, ext, chan->language));

	pbx_builtin_setvar_helper(chan, "RECORDED_FILE", filename.data);

	res = 0;

	if (chan->_state != CW_STATE_UP) {
		if (option_skip) {
			/* At the user's option, skip if the line is not up */
			LOCAL_USER_REMOVE(u);
			res = 0;
			goto out;
		} else if (!option_noanswer) {
			/* Otherwise answer unless we're supposed to record while on-hook */
			res = cw_answer(chan);
		}
	}

	if (!res) {
		if (!option_quiet) {
			/* Some code to play a nice little beep to signify the start of the record operation */
			res = cw_streamfile(chan, "beep", chan->language);
			if (!res) {
				res = cw_waitstream(chan, "");
			} else {
				cw_log(CW_LOG_WARNING, "cw_streamfile failed on %s\n", chan->name);
			}
			cw_stopstream(chan);
		}
		
		/* The end of beep code.  Now the recording starts */
		
		if (silence > 0) {
			rfmt = chan->readformat;
			if ((res = cw_set_read_format(chan, CW_FORMAT_SLINEAR)) < 0) {
				cw_log(CW_LOG_WARNING, "Unable to set to linear mode, giving up\n");
				goto out;
			}
			if (!(sildet = cw_dsp_new())) {
				cw_log(CW_LOG_WARNING, "Unable to create silence detector :(\n");
				goto out;
			}
			cw_dsp_set_threshold(sildet, 256);
		} 

		flags = option_append ? O_CREAT|O_APPEND|O_WRONLY : O_CREAT|O_TRUNC|O_WRONLY;
		s = cw_writefile(filename.data, ext, NULL, flags , 0, 0644);
		
		if (s) {
			int waitres;

			/* Request a video update */
			cw_indicate(chan, CW_CONTROL_VIDUPDATE);

			if (maxduration <= 0)
				maxduration = -1;
			
			while ((waitres = cw_waitfor(chan, maxduration)) > -1) {
				if (maxduration > 0) {
					if (waitres == 0) {
						gottimeout = 1;
						break;
					}
					maxduration = waitres;
  				}
				
				f = cw_read(chan);
				if (!f) {
					res = -1;
					break;
				}
				if (f->frametype == CW_FRAME_VOICE) {
					res = cw_writestream(s, f);
					if (res) {
						cw_log(CW_LOG_WARNING, "Problem writing frame\n");
						cw_fr_free(f);
						break;
					}
					
					if (silence > 0) {
						dspsilence = 0;
						cw_dsp_silence(sildet, f, &dspsilence);
						if (dspsilence) {
							totalsilence = dspsilence;
						} else {
							totalsilence = 0;
						}
						if (totalsilence > silence) {
							/* Ended happily with silence */
							cw_fr_free(f);
							gotsilence = 1;
							break;
						}
					}
				}
				if (f->frametype == CW_FRAME_VIDEO) {
					res = cw_writestream(s, f);
					if (res) {
						cw_log(CW_LOG_WARNING, "Problem writing frame\n");
						cw_fr_free(f);
						break;
					}
				}
				if ((f->frametype == CW_FRAME_DTMF) &&
					(f->subclass == terminator)) {
					cw_fr_free(f);
					break;
				}
				cw_fr_free(f);
			}
			if (!f) {
				cw_log(CW_LOG_DEBUG, "Got hangup\n");
				res = -1;
			}
			
			if (gotsilence) {
				cw_stream_rewind(s, silence-1000);
				cw_truncstream(s);
			} else if (!gottimeout) {
				/* Strip off the last 1/4 second of it */
				cw_stream_rewind(s, 250);
				cw_truncstream(s);
			}
			cw_closestream(s);
		} else
			cw_log(CW_LOG_WARNING, "Could not create file %s\n", filename.data);
	} else
		cw_log(CW_LOG_WARNING, "Could not answer channel '%s'\n", chan->name);
	
	if ((silence > 0) && rfmt) {
		res = cw_set_read_format(chan, rfmt);
		if (res)
			cw_log(CW_LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
		if (sildet)
			cw_dsp_free(sildet);
	}

out:
	LOCAL_USER_REMOVE(u);
	cw_dynstr_free(&filename);
	return res;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(record_app);
	return res;
}

static int load_module(void)
{
	record_app = cw_register_function(record_name, record_exec, record_synopsis, record_syntax, record_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
