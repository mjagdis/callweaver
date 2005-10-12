/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, OpenPBX.org.
 *
 * Contributed by Carlos Antunes <cmantunes@gmail.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
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
 * Just generate white noise 
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision: 1.0 $")

#include "openpbx/lock.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/app.h"

static char *tdesc = "White Noise Generator Application";

static char *app = "WhiteNoise";

static char *synopsis = "Generates white noise";

static char *descrip = "WhiteNoise(level[, timeout]): Generates white noise at 'level' dBov's for 'timeout' seconds or indefinitely if timeout is absent or is zero.\n\nLevel is a non-positive number. For example, WhiteNoise(0.0) generates white noise at full power, while WhiteNoise(-3.0) generates white noise at half full power. Every -3dBov's reduces white noise power in half. Full power in this case is defined as noise that overloads the channel roughly 0.3\% of the time. Note that values below -69dBov's start to give out silence frequently, resulting in intermittent noise, i.e, alternating periods of silence and noise.\n";

static const struct opbx_frame framedefaults = {
	.frametype = OPBX_FRAME_VOICE,
	.subclass = OPBX_FORMAT_SLINEAR,
	.offset = OPBX_FRIENDLY_OFFSET,
	.mallocd = 0,
	.data = NULL,
	.datalen = 0,
	.samples = 0,
	.src = "whitenoise",
	.delivery.tv_sec = 0,
	.delivery.tv_usec = 0,
	.prev = NULL,
	.next = NULL
};

#ifndef LOW_MEMORY
/*
 * We pregenerate 64k of white noise samples that will be used instead of
 * generating the samples continously and wasting CPU cycles. The buffer
 * below stores these pregenerated samples.
 *
 */
static float pregeneratedsamples[65536L];
#endif

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

/*
 * We need a nice, not too expensive, gaussian random number generator.
 * It generates two random numbers at a time, which is great.
 * From http://www.taygeta.com/random/gaussian.html
 */
static void box_muller_rng(float stddev, float *rn1, float *rn2)
{
	const float twicerandmaxinv = 2.0 / RAND_MAX;
	float x1, x2, w, temp;
	 
	do {
		x1 = random() * twicerandmaxinv - 1.0f;
		x2 = random() * twicerandmaxinv - 1.0f;
		w = x1 * x1 + x2 * x2;
	} while (w >= 1.0f);
	temp = logf(w) / w;
	w = stddev * sqrtf(-(temp + temp));
	*rn1 = x1 * w;
	*rn2 = x2 * w;
}

static void *noise_alloc(struct opbx_channel *chan, void *data)
{
	float level = *(float *)data; /* level is noise level in dBov */
	float *pnoisestddev; /* pointer to calculated noise standard dev */
	const float maxsigma = 32767.0 / 3.0;

	/*
	 * When level is zero (full power, by definition) standard deviation
	 * (sigma) is calculated so that 3 * sigma equals max sample value
	 * before overload. For signed linear, which is what we use, this
	 * value is 32767. The max value of sigma will therefore be
	 * 32767.0 / 3.0. This guarantees that roughly 99.7% of the samples
	 * generated will be between -32767 and +32767. The rest, 0.3%,
	 * will be clipped to comform to the channel limits, i.e., +/-32767.
	 * 
	 */
	pnoisestddev = malloc(sizeof (float));
	if(pnoisestddev)
		*pnoisestddev = maxsigma * exp10f(level * 0.05f);
	return pnoisestddev;
}

static void noise_release(struct opbx_channel *chan, void *data)
{
	free((float *)data);
}

static int noise_generate(struct opbx_channel *chan, void *data, int len, int samples)
{
#ifdef LOW_MEMORY
	float randomnumber[2];
	float sampleamplitude;
	int j;
#else
	uint16_t start;
#endif
	float noisestddev = *(float *)data;
	struct opbx_frame f;
	int16_t *buf, *pbuf;
	int i;

#ifdef LOW_MEMORY
	/* We need samples to be an even number */
	if (samples & 0x1) {
		opbx_log(LOG_ERROR, "Samples (%d) needs to be an even number\n", samples);
		return -1;
	}
#endif

	/* Allocate enough space for samples.
	 * Remember that slin uses signed dword samples */
	len = samples * sizeof (int16_t);
	if(!(buf = alloca(len))) {
		opbx_log(LOG_ERROR, "Unable to allocate buffer to generate %d samples\n", samples);
		return -1;
	}

	/* Setup frame */
	memcpy(&f, &framedefaults, sizeof (f));
	f.data = buf;
	f.datalen = len;
	f.samples = samples;

	/* Let's put together our frame "data" */
	pbuf = buf;

#ifdef LOW_MEMORY
	/* We need to generate samples every time we are called */
	for (i = 0; i < samples; i += 2) {
		box_muller_rng(noisestddev, &randomnumber[0], &randomnumber[1]);
		for (j = 0; j < 2; j++) {
			sampleamplitude = randomnumber[j];
			if (sampleamplitude > 32767.0f)
				sampleamplitude = 32767.0f;
			else if (sampleamplitude < -32767.0f)
				sampleamplitude = -32767.0f;
			*(pbuf++) = (int16_t)sampleamplitude;
		}
	}
#else
	/*
	 * We are going to use pregenerated samples. But we start at
	 * different points on the pregenerated samples buffer every time
	 * to create a little bit more randomness
	 *
	 */
	start = (uint16_t) (random() * (65536.0 / RAND_MAX));
	for (i = 0; i < samples; i++) {
		*(pbuf++) = (int16_t)(noisestddev * pregeneratedsamples[start++]);
	}
#endif

	/* Send it out */
	if (opbx_write(chan, &f) < 0)
	{
		opbx_log(LOG_ERROR, "Failed to write frame to channel '%s'\n", chan->name);
		return -1;
	}
	return 0;
}

static struct opbx_generator noise_generator = 
{
	alloc: noise_alloc,
	release: noise_release,
	generate: noise_generate,
} ;

static int usage(const char *arg)
{
	opbx_log(LOG_ERROR, "Invalid argument(s) '%s'; WhiteNoise requires non-positive floating-point argument for noise level in dBov's and optional non-negative floating-point argument for timeout in seconds.\n", arg);
	return -1;
}

static int noise_exec(struct opbx_channel *chan, void *data)
{

	struct localuser *u;
	char *excessdata;
	float level;
	float timeout;
	char *argdata;
	int argc;
	char *argv[3];
	int res;

	/* Verify we potentially have arguments and get local copy */
	if (data)
		argdata = opbx_strdupa(data);
	else
		return usage("<null>");

	/* Separate arguments */
	argc = opbx_separate_app_args(argdata, '|', argv, sizeof (argv) / sizeof (argv[0]));
	if (!argc)
		return usage("<null>");
	else if (argc > 2) 
		return usage(data);
	else {
		/* Extract first argument and ensure we have
		 * a valid noise level argument value        */
		argv[0] = opbx_trim_blanks(argv[0]);
		level = strtof(argv[0], &excessdata);
		if ((excessdata && *excessdata) || level > 0)
			return usage(data);

		/* Extract second argument, if available, and validate
		 * timeout is non-negative. Zero timeout means no timeout */
		timeout = 0;
		if (argc == 2) {
			argv[1] = opbx_trim_blanks(argv[1]);
			timeout = strtof(argv[1], &excessdata);
			if ((excessdata && *excessdata) || timeout < 0)
				return usage(data);

			/* Convert timeout to milliseconds
			 * and ensure minimum of 20ms      */
			timeout = roundf(timeout * 1000.0);
			if (timeout > 0 && timeout < 20)
				timeout = 20;
		}
	}

	opbx_log(LOG_DEBUG, "Setting up white noise generator with level %.1fdBov's and %.0fms %stimeout\n", level, timeout, timeout == 0 ? "(no) " : "");

	LOCAL_USER_ADD(u);
	opbx_set_write_format(chan, OPBX_FORMAT_SLINEAR);
	opbx_set_read_format(chan, OPBX_FORMAT_SLINEAR);
	if (chan->_state != OPBX_STATE_UP)
	{
		opbx_answer(chan);
	}
	if (opbx_activate_generator(chan, &noise_generator, &level) < 0)
	{
		opbx_log(LOG_WARNING, "Failed to activate white noise generator on '%s'\n",chan->name);
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	/* Just let the noise happen... */
	res = -1;
	if (timeout > 0)
		res = opbx_safe_sleep(chan, timeout);
	else 
		while(!opbx_safe_sleep(chan, 10000));
	opbx_deactivate_generator(chan);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return opbx_unregister_application(app);
}

int load_module(void)
{
#ifndef LOW_MEMORY
	/* Let's pregenerate all samples with std dev = 1.0 */
	int i;

	for (i = 0; i < sizeof (pregeneratedsamples) / sizeof (pregeneratedsamples[0]); i += 2)
	{
		box_muller_rng(1.0, &pregeneratedsamples[i], &pregeneratedsamples[i + 1]);

	}
#endif
	return opbx_register_application(app, noise_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}


