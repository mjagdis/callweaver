/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * WaitForSilence Application by David C. Troy <dave@popvox.com>
 * Version 1.00 2004-01-29
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
 * \brief Wait for Silence
 *   - Waits for up to 'x' milliseconds of silence, 'y' times
 *   - WaitForSilence(500,2) will wait for 1/2 second of silence, twice
 *   - WaitForSilence(1000,1) will wait for 1 second of silence, once
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/dsp.h"
#include "callweaver/module.h"
#include "callweaver/options.h"

static const char tdesc[] = "Wait For Silence";

static void *waitforsilence_app;
static const char waitforsilence_name[] = "WaitForSilence";
static const char waitforsilence_synopsis[] = "Waits for a specified amount of silence";
static const char waitforsilence_syntax[] = "WaitForSilence(x[, y])";
static const char waitforsilence_descrip[] = 
"Wait for Silence: Waits for up to 'x' \n"
"milliseconds of silence, 'y' times or 1 if omitted\n"
"Set the channel variable WAITSTATUS with to one of these values:"
"SILENCE - if silence of x ms was detected"
"TIMEOUT - if silence of x ms was not detected."
"Examples:\n"
"  - WaitForSilence(500, 2) will wait for 1/2 second of silence, twice\n"
"  - WaitForSilence(1000) will wait for 1 second of silence, once\n";


static int do_waiting(struct cw_channel *chan, int maxsilence) {

	struct cw_frame *f;
	int totalsilence = 0;
	int dspsilence = 0;
	int gotsilence = 0; 
	static int silencethreshold = 64;
	int rfmt = 0;
	int res = 0;
	struct cw_dsp *sildet;	 /* silence detector dsp */
	time_t start, now;
	time(&start);

	rfmt = chan->readformat; /* Set to linear mode */
	res = cw_set_read_format(chan, CW_FORMAT_SLINEAR);
	if (res < 0) {
		cw_log(CW_LOG_WARNING, "Unable to set to linear mode, giving up\n");
		return -1;
	}

	sildet = cw_dsp_new(); /* Create the silence detector */
	if (!sildet) {
		cw_log(CW_LOG_WARNING, "Unable to create silence detector :(\n");
		return -1;
	}
	cw_dsp_set_threshold(sildet, silencethreshold);

	/* Await silence... */
	f = NULL;
	for(;;) {
		res = cw_waitfor(chan, 2000);
		if (!res) {
			cw_log(CW_LOG_WARNING, "One waitfor failed, trying another\n");
			/* Try one more time in case of masq */
			res = cw_waitfor(chan, 2000);
			if (!res) {
				cw_log(CW_LOG_WARNING, "No audio available on %s??\n", chan->name);
				res = -1;
			}
		}

		if (res < 0) {
			f = NULL;
			break;
		}
		f = cw_read(chan);
		if (!f)
			break;
		if (f->frametype == CW_FRAME_VOICE) {
			dspsilence = 0;
			cw_dsp_silence(sildet, f, &dspsilence);
			if (dspsilence) {
				totalsilence = dspsilence;
				time(&start);
			} else {
				totalsilence = 0;
			}

			if (totalsilence >= maxsilence) {
				if (option_verbose > 2)
					cw_verbose(VERBOSE_PREFIX_3 "Exiting with %dms silence > %dms required\n", totalsilence, maxsilence);
				/* Ended happily with silence */
				gotsilence = 1;
				pbx_builtin_setvar_helper(chan, "WAITSTATUS", "SILENCE");
				cw_log(CW_LOG_DEBUG, "WAITSTATUS was set to SILENCE\n");
				cw_fr_free(f);
				break;
			} else if ( difftime(time(&now),start) >= maxsilence/1000 ) {
				pbx_builtin_setvar_helper(chan, "WAITSTATUS", "TIMEOUT");
				cw_log(CW_LOG_DEBUG, "WAITSTATUS was set to TIMEOUT\n");
				cw_fr_free(f);
				break;
			}
		}
		cw_fr_free(f);
	}
	if (rfmt && cw_set_read_format(chan, rfmt)) {
		cw_log(CW_LOG_WARNING, "Unable to restore format %s to channel '%s'\n", cw_getformatname(rfmt), chan->name);
	}
	cw_dsp_free(sildet);
	return gotsilence;
}

static int waitforsilence_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct localuser *u;
	int maxsilence;
	int iterations = 1, i;
	int res = 1;

	CW_UNUSED(result);

	if (argc < 0 || argc > 2 || (argc > 0 && !isdigit(argv[0][0])) || (argc == 2 && !isdigit(argv[1][0])))
		return cw_function_syntax(waitforsilence_syntax);

	maxsilence = (argc > 0 ? atoi(argv[0]) : 1000);
	iterations = (argc > 1 ? atoi(argv[1]) : 1);

	LOCAL_USER_ADD(u);

	res = cw_answer(chan); /* Answer the channel */

	if (option_verbose > 2)
		cw_verbose(VERBOSE_PREFIX_3 "Waiting %d time(s) for %d ms silence\n", iterations, maxsilence);

	res = 1;
	for (i=0; (i<iterations) && (res == 1); i++) {
		res = do_waiting(chan, maxsilence);
	}

	LOCAL_USER_REMOVE(u);
	if (res > 0)
		res = 0;
	return res;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(waitforsilence_app);
	return res;
}

static int load_module(void)
{
	waitforsilence_app = cw_register_function(waitforsilence_name, waitforsilence_exec, waitforsilence_synopsis, waitforsilence_syntax, waitforsilence_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
