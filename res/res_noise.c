#define LOW_MEMORY

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
 * Generate white noise
 *
 * Pregenerates samples of normally distributed noise when LOW_MEMORY not
 * defined and generates triangularly distributes noise samples on the fly
 * when LOW_MEMORY is defined. The use of pregenerated samples is much less
 * CPU intensive and should be used whenever possible except in systems where
 * memory is at a premium.
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL$", "$Revision$")

#include "openpbx/lock.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/app.h"

static char *tdesc = "White Noise Generation";

static char *app = "WhiteNoise";

static char *synopsis = "Generates white noise";

static char *descrip =
"WhiteNoise(level[, timeout]): Generates white noise at 'level' dBov's for "
"'timeout' seconds or indefinitely if timeout is absent or is zero.\n\nLevel "
"is a non-positive number. For example, WhiteNoise(0.0) generates white noise "
"at full power, while WhiteNoise(-3.0) generates white noise at half full "
"power. Every -3dBov's reduces white noise power in half. Full power in this "
"case is defined as noise that overloads the channel roughly 0.3\% of the "
"time. Note that values below -69dBov's start to give out silence frequently, "
"resulting in intermittent noise, i.e, alternating periods of silence and "
"noise.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static const struct opbx_frame framedefaults =
{
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

/* These are the max and min signal levels for signed linear */
#define SIGNAL_MAX_LEVEL (32767)
#define SIGNAL_MIN_LEVEL (-SIGNAL_MAX_LEVEL)

/*
 * When white noise level is zero dBov (full power, by definition) standard
 * deviation (sigma) is calculated so that 3 * sigma equals max sample value
 * before overload. For signed linear, which is what we use, this value is
 * 32767. The max value of sigma will therefore be 32767.0 / 3.0. This
 * guarantees that roughly 99.7% of the samples generated will be between
 * -32767 and +32767. The rest, 0.3%, will be clipped to conform to the
 * channel limits, i.e., +/-32767.
 */
#define NOISE_MAX_SIGMA (SIGNAL_MAX_LEVEL / 3)

#ifndef LOW_MEMORY
/*
 * We pregenerate 64k of white noise samples that will be used instead of
 * generating the samples continously and wasting CPU cycles. The buffer
 * below stores these pregenerated samples (in signed linear format).
 * WARNING: Reducing the size of the buffer will cause problems.
 */
#define NUM_PREGENERATED_SAMPLES (65536L)
static int16_t pregeneratedsamples[NUM_PREGENERATED_SAMPLES];

/*
 * We need a nice, not too expensive, gaussian random number generator.
 * It generates two random numbers at a time, which is great.
 * From http://www.taygeta.com/random/gaussian.html
 */
static void box_muller_rng(double stddev, double *rn1, double *rn2)
{
	const double twicerandmaxinv = 2.0 / RAND_MAX;
	double x1, x2, w, temp;
	 
	do
	{
		x1 = random() * twicerandmaxinv - 1.0;
		x2 = random() * twicerandmaxinv - 1.0;
		w = x1 * x1 + x2 * x2;
	}
	while (w >= 1.0);
	temp = -log(w) / w;
	w = stddev * sqrt(temp + temp);
	*rn1 = x1 * w;
	*rn2 = x2 * w;
}
#endif /* NOT LOW_MEMORY */

static void *noise_alloc(struct opbx_channel *chan, void *data)
{
	/* level is noise level in dBov */
	double level = *(double *)data;

#ifdef LOW_MEMORY
	/*
	 * As we have to continuously generate samples based on a triangular
	 * distribution, we calculate a scale factor that transforms the
	 * interval [-RAND_MAX / 2, RAND_MAX / 2] into [-a, a] where '2a' is
	 * the base of the triangle used in our triangular distribution.
	 * Scale factor will be equal to full power when level is 0dBov.
	 */
	const double fullpower = 2.0 * NOISE_MAX_SIGMA * sqrt(6.0) / RAND_MAX;
	float *pscalefactor = malloc(sizeof (float));
#else
	/*
	 * Pregenerated samples are generated with max standard deviation.
	 * Here, we compute a scale factor that will be later used to scale
	 * the pregenerated samples to the desidered standard deviation.
	 * Given that this will involve multiplying all pregenerated samples
	 * by a constant, we use a fixed point signed 32-bit scale factor
	 * to speed up multiplication. When level is zero, the scale factor
	 * will be 1.0. We place the fixed point right in the middle of the
	 * 32-bit quantity, between bits 15 and 16. Quantity 1.0 will
	 * therefore be represented as 2^16 (or 1 << 16).
	 */
	const double fullpower = 65536.0;
	int32_t *pscalefactor = malloc(sizeof (int32_t));
#endif /* LOW_MEMORY */

	if (pscalefactor)
	{
		*pscalefactor = fullpower * exp10(level * 0.05);
	}
	return pscalefactor;
}

static void noise_release(struct opbx_channel *chan, void *data)
{
#ifdef LOW_MEMORY
	free((float *)data);
#else
	free((int32_t *)data);
#endif
}

static int noise_generate(struct opbx_channel *chan, void *data, int len, int samples)
{
#ifdef LOW_MEMORY
	float scalefactor = *(float *)data;
	long sampleamplitude;
#else
	int32_t scalefactor = *(int32_t *)data;
	uint16_t start;
#endif
	struct opbx_frame f;
	int16_t *buf, *pbuf;
	int i;

	/* Allocate enough space for samples.
	 * Remember that slin uses signed dword samples */
	len = samples * sizeof (int16_t);
	if(!(buf = alloca(len)))
	{
		opbx_log(LOG_ERROR,
			"Unable to allocate buffer to generate %d samples\n",
			samples);
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
	/*
	 * We need to generate samples every time we are called. To speed
	 * things up, instead of generating normally distributed samples, we
	 * generate samples that are distributed according to a triangular
	 * distribution. This requires two calls to a uniform random number
	 * generator. The standard deviation of triangularly distributed
	 * samples is given by 'a / sqrt(6)' where '2a' is the base of the
	 * triangle and '1/a' is its height (area is therefore 1). Given that
	 * we want to maintain consistent power levels between samples
	 * generated according to normal and triangular distributions, we get
	 * a max value for 'a' of (32767.0 / 3.0) * Sqrt(6.0) = 26754. As
	 * such, when level is 0dbov (and scale factor is 1.0), we will be
	 * generating samples whose values fall between -26754 and 26754.
	 * These limits will be adjusted by the scale factor for other levels
	 * of noise.
	 */
	for (i = 0; i < samples; i++)
	{
		/*
		 * Function random() returns a long int. Given that we need
		 * to add two of these random numbers to obtain a triangularly
		 * distributed random number, we divide each by 2 to avoid
		 * potential overflow problems. The result will be a long int
		 * between 0 and RAND_MAX.
		 */
		sampleamplitude = (random() >> 1) + (random() >> 1);

		/*
		 * We subtract RAND_MAX / 2 so that random number will be
		 * on the interval [-RAND_MAX / 2, RAND_MAX / 2]
		 */
		sampleamplitude -= RAND_MAX / 2;

		/*
		 * Finally, we scale by scalefactor. This is a floating point
		 * multiplication because doing it with integers/fixed point
		 * alone is just way too complicated and doesn't speed things
		 * up that much
		 */
		*(pbuf++) = (int16_t)(sampleamplitude * scalefactor);
	}
#else
	/*
	 * We are going to use pregenerated samples. But we start at
	 * different points on the pregenerated samples buffer every time
	 * to create a little bit more randomness. Given that 'start' is a
	 * unsigned 16-bit integer, we need NUM_PREGENERATED_SAMPLES to be
	 * (at least) 2^16 to avoid reading beyond the bounds of the buffer.
	 */
	start = random() * (NUM_PREGENERATED_SAMPLES / (float)RAND_MAX);
	for (i = 0; i < samples; i++)
	{
		*(pbuf++) = (scalefactor * pregeneratedsamples[start++]) >> 16;
	}
#endif

	/* Send it out */
	if (opbx_write(chan, &f) < 0)
	{
		opbx_log(LOG_ERROR, "Failed to write frame to channel '%s'\n",
			 chan->name);
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
	opbx_log(LOG_ERROR,
		 "Invalid argument(s) '%s'; WhiteNoise requires non-positive "
		 "floating-point argument for noise level in dBov's and "
		 "optional non-negative floating-point argument for timeout "
		 "in seconds.\n", arg);
	return -1;
}

static int noise_exec(struct opbx_channel *chan, void *data)
{

	struct localuser *u;
	char *excessdata;
	double level;
	double timeout;
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
	argc = opbx_separate_app_args(argdata, '|', argv,
				      sizeof (argv) / sizeof (argv[0]));
	if (!argc)
		return usage("<null>");
	else if (argc > 2) 
		return usage(data);
	else
	{
		/* Extract first argument and ensure we have
		 * a valid noise level argument value        */
		argv[0] = opbx_trim_blanks(argv[0]);
		level = strtod(argv[0], &excessdata);
		if ((excessdata && *excessdata) || level > 0)
			return usage(data);

		/* Extract second argument, if available, and validate
		 * timeout is non-negative. Zero timeout means no timeout */
		timeout = 0;
		if (argc == 2)
		{
			argv[1] = opbx_trim_blanks(argv[1]);
			timeout = strtod(argv[1], &excessdata);
			if ((excessdata && *excessdata) || timeout < 0)
				return usage(data);

			/* Convert timeout to milliseconds
			 * and ensure minimum of 20ms      */
			timeout = round(timeout * 1000.0);
			if (timeout > 0 && timeout < 20)
				timeout = 20;
		}
	}

	opbx_log(LOG_DEBUG, "Setting up white noise generator with level "
			    "%.1lfdBov's and %.0lfms %stimeout\n", level,
			    timeout, timeout == 0 ? "(no) " : "");
	LOCAL_USER_ADD(u);
	opbx_set_write_format(chan, OPBX_FORMAT_SLINEAR);
	if (chan->_state != OPBX_STATE_UP)
	{
		opbx_answer(chan);
	}
	if (opbx_activate_generator(chan, &noise_generator, &level) < 0)
	{
		opbx_log(LOG_WARNING,
			 "Failed to activate white noise generator on '%s'\n",
			 chan->name);
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
	/*
	 * Let's pregenerate all samples corresponding to level 0dBov.
	 */
	double randomnumbers[2], sample;
	double samplessum, samplessquaredsum;
	long i, j;
	int16_t *ppregeneratedsamples;

	ppregeneratedsamples = &pregeneratedsamples[0];
	samplessum = 0;
	samplessquaredsum = 0;
	for (i = 0; i < NUM_PREGENERATED_SAMPLES; i += 2)
	{
		box_muller_rng(NOISE_MAX_SIGMA,
				&randomnumbers[0], &randomnumbers[1]);
		for (j = 0; j < 2; j++)
		{
			sample = floor(randomnumbers[j] + 0.5);
			if (sample > SIGNAL_MAX_LEVEL)
				sample = SIGNAL_MAX_LEVEL;
			else if (sample < SIGNAL_MIN_LEVEL)
				sample = SIGNAL_MIN_LEVEL;
			*(ppregeneratedsamples++) = sample;
			samplessum += sample;
			samplessquaredsum += sample * sample;
		}

	}
	opbx_log(LOG_DEBUG,
		"Generated %ld signed linear noise samples with mean = %.0lf "
		"(should be between -128 and 128) and standard deviation = "
		"%.0lf (should be between 10831 and 11013)\n",
		NUM_PREGENERATED_SAMPLES,
		samplessum / NUM_PREGENERATED_SAMPLES,
		sqrt(samplessquaredsum / NUM_PREGENERATED_SAMPLES));
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
