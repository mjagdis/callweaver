/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, OpenPBX.org.
 *
 * Contributed by Carlos Antunes <cmantunes@gmail.com>
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
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

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

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

static char *tdesc = "Noise Generation";

static char *app = "WhiteNoise";

static char *synopsis = "Generates white noise";

static char *descrip =
"WhiteNoise(level[, timeout]): Generates white noise at 'level' dBov's for "
"'timeout' seconds or indefinitely if timeout is absent or is zero.\n\nLevel "
"is a non-positive number. For example, WhiteNoise(0.0) generates white noise "
"at full power, while WhiteNoise(-3.0) generates white noise at half full "
"power. Every -3dBov's reduces white noise power in half. Full power is the "
"maximum amount of power the channel is able to carry.\n";

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
 * A square wave of amplitude 32767 transmits the maximum amount of power
 * on a signed linear coded channel. This amount of power is therefore
 * defined to be 0dBov and is equal to (32767)^2. Given that sigma^2 is the
 * amount of power of a normally distributed random signal, the maximum
 * value of sigma, corresponding to 0dBov, will therefore be 32767.
 */
#define NOISE_MAX_SIGMA (SIGNAL_MAX_LEVEL)

#ifdef LOW_MEMORY
struct noise_params
{
	/* factor used to scale random numbers down to desired level */
	float scalefactor;

	/* state for our fast uniform random number generator */
	uint32_t rngstate;
};

/*
 * 'Quick-n-dirty' uniform random number generator. It is about 6 times
 * faster than the random() function call provided by standard lib.
 * Returns pseudo-random numbers between 0 and 2^32-1 inclusive.
 * As seen on "Numerical Recipes in C, 2nd Edition", Chapter 7, page 284.
 */
#define FAST_UNI_RNG_MAX (4294967295UL)
#define FAST_UNI_RNG_HALF (FAST_UNI_RNG_MAX / 2)
static inline uint32_t fast_uniform_rng(uint32_t *state)
{
	*state = 1664525UL * (*state) + 1013904223UL;
	return *state;
}
#else
struct noise_params
{
	/*
	 * Factor used to scale pregenerated samples down to desired level.
	 * This is a fixed point number with the number of fractional bits
	 * given by SCALE_FACTOR_FRAC_BITS as defined below.
	 */
	int32_t scalefactor;
};

/*
 * We pregenerate 64k of white noise samples that will be used instead of
 * generating the samples continously and wasting CPU cycles. The buffer
 * below stores these pregenerated samples (in signed linear format).
 * WARNING: Reducing the size of the buffer will cause problems.
 */
#define NUM_PREGENERATED_SAMPLES (65536L)
static int16_t pregeneratedsamples[NUM_PREGENERATED_SAMPLES];

/*
 * We cannot pregenerate samples at 0dBov because 30% of those would be
 * clipped away by the signed linear channel limits. As such, we pregenerate
 * samples at -10dBov which results in samples that are clipped less than
 * 0.3% of the time (these are the samples whose value is beyond 3*sigma).
 * See SCALE_FACTOR_FRAC_BITS below.
 */
#define PREGEN_SAMPLES_DBOV (-10)

/*
 * Number of fractional bits used in the fixed-point scale factor.
 * This depends on the value of PREGEN_SAMPLES_DBOV. To determine, we compute
 * exp10(-PREGEN_SAMPLES_DBOV * 0.05). Then, we see how many bits are necessary
 * to hold the integer part. The number of fractional bits will be 16 minus the
 * number of bits necessary to hold the integer part.
 * For example, for PREGEN_SAMPLES_DBOV equal to -10, exp10(-(-10) * 0.05) is
 * equal to 3.1623. The number of bits to hold the integer part is 2. As such,
 * the number of fractional bits of the scale factor is 14.
 */
#define SCALE_FACTOR_FRAC_BITS (14)

/*
 * The maximum value returned by random() according to the Single UNIX
 * Specification. Linux and Solaris are both compliant. Are there any
 * known exceptions?
 */
#ifndef RANDOM_MAX
#define RANDOM_MAX (2147483647L)
#endif

/*
 * We need a nice, not too expensive, gaussian random number generator.
 * It generates two random numbers at a time, which is great.
 * From http://www.taygeta.com/random/gaussian.html
 */
static void box_muller_rng(double stddev, double *rn1, double *rn2)
{
	const double twicerandmaxinv = 2.0 / RANDOM_MAX;
	double x1, x2, w, temp;
	 
	do
	{
		x1 = random() * twicerandmaxinv - 1.0;
		x2 = random() * twicerandmaxinv - 1.0;
		w = x1 * x1 + x2 * x2;
	}
	while (w >= 1.0 || w == 0.0);
	temp = -log(w) / w;
	w = stddev * sqrt(temp + temp);
	*rn1 = x1 * w;
	*rn2 = x2 * w;
}
#endif /* LOW_MEMORY */

static void *noise_alloc(struct opbx_channel *chan, void *data)
{
	/* level is noise level in dBov and is a non-positive number */
	double level = *(double *)data;

#ifdef LOW_MEMORY
	/*
	 * As we have to continuously generate samples based on a triangular
	 * distribution, we calculate a scale factor that transforms the
	 * interval [-FAST_UNI_RNG_HALF / 2, FAST_UNI_RNG_HALF / 2] into
	 * [-a, a] where '2a' is the base of the triangle used in our
	 * triangular distribution. Scale factor will be equal to full power
	 * when level is 0dBov.
	 */
	struct noise_params *pnp = malloc(sizeof (struct noise_params));

	if (pnp)
	{
		const double fullpower =
			2.0 * NOISE_MAX_SIGMA * sqrt(6.0) / FAST_UNI_RNG_HALF;
		pnp->rngstate = random(); /* seed RNG with a random value */
		pnp->scalefactor = fullpower * exp10(level * 0.05);
	}
	return pnp;
#else
	/*
	 * Pregenerated samples are generated with standard deviation
	 * (or sigma) corresponding to PREGEN_SAMPLES_DBOV. Here, we compute
	 * a scale factor that will be later used to scale the pregenerated
	 * samples to the desidered standard deviation (based on chosen dBov
	 * level). Given that this will involve multiplying all pregenerated
	 * samples by a constant, we use a fixed point signed 32-bit scale
	 * factor to speed up multiplication.
	 *
	 * When level is zero dBov's, the scale factor will be
	 * exp10(-PREGEN_SAMPLES_DBOV / 20). For example, if
	 * PREGEN_SAMPLES_DBOV is -10, then this scale factor is
	 * aproximately 3.1623. Multiplying by 2^SCALE_FACTOR_FRAC_BITS, the
	 * resulting scale factor will be a fixed point number. The
	 * leftmost SCALE_FACTOR_FRAC_BITS bits will represent the fractional
	 * part of scale factor. Seen as an integer, this scale factor will
	 * never exceed 65536.
	 */
	struct noise_params *pnp = malloc(sizeof (struct noise_params));

	if (pnp)
	{
		const double sf = exp10((level - PREGEN_SAMPLES_DBOV) * 0.05);
		pnp->scalefactor = floor((1 << SCALE_FACTOR_FRAC_BITS) * sf);
	}
	return pnp;
#endif /* !LOW_MEMORY */
}

static void noise_release(struct opbx_channel *chan, void *data)
{
	free((struct noise_params *)data);
}

static int noise_generate(struct opbx_channel *chan, void *data, int samples)
{
	struct noise_params *pnp = data;
#ifdef LOW_MEMORY
	long sampleamplitude;
#else
	uint16_t start;
#endif
	struct opbx_frame f;
	int16_t *buf, *pbuf;
	int len, i;

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
	 * a max value for 'a' of 32767.0 * Sqrt(6.0) = 80262. As such, when
	 * level is 0dbov (and scale factor is 1.0), we will be generating
	 * samples whose values fall between -80262 and 80262. These limits
	 * will be adjusted by the scale factor for other levels of noise.
	 * Obviously, the channel limits apply and samples will be clipped
	 * to appropriate levels.
	 */
	for (i = 0; i < samples; i++)
	{
		/*
		 * Function fast_uniform_rng returns an unsigned 32-bit int.
		 * Given that we need to add two of these random numbers to
		 * obtain a triangularly distributed random number, we divide
		 * each by 4 to avoid potential overflow problems. The result
		 * will be a 32-bit unsigned between 0 and FAST_UNI_RNG_HALF.
		 */
		sampleamplitude = fast_uniform_rng(&pnp->rngstate) >> 2;
		sampleamplitude += fast_uniform_rng(&pnp->rngstate) >> 2;

		/*
		 * We subtract FAST_UNI_RNG_HALF / 2 so that the random
		 * number will be on the interval
		 * [-FAST_UNI_RNG_HALF / 2, FAST_UNI_RNG_HALF / 2]
		 */
		sampleamplitude -= FAST_UNI_RNG_HALF / 2;

		/*
		 * Finally, we scale by scalefactor. This is a floating point
		 * multiplication because doing it with integers/fixed point
		 * alone is just way too complicated meaning that it wouldn't
		 * speed things up that much. (I'll be testing this assumption
		 * in the near future, though...)
		 */
		*(pbuf++) = (int16_t)(sampleamplitude * pnp->scalefactor);
	}
#else
	/*
	 * We are going to use pregenerated samples. But we start at
	 * different points on the pregenerated samples buffer every time
	 * to create a little bit more randomness. Given that 'start' is an
	 * unsigned 16-bit integer, we need NUM_PREGENERATED_SAMPLES to be
	 * (at least) 2^16 to avoid reading beyond the bounds of the buffer.
	 */
	start = random();
	for (i = 0; i < samples; i++)
	{
		*(pbuf++) = (pnp->scalefactor * pregeneratedsamples[start++])
                                   >> SCALE_FACTOR_FRAC_BITS;
	}
#endif

	/* Send it out */
	return opbx_write(chan, &f);
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
	if (opbx_generator_activate(chan, &noise_generator, &level) < 0)
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
	opbx_generator_deactivate(chan);
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
#ifdef LOW_MEMORY
	opbx_log(LOG_DEBUG,
                 "Using signed linear noise samples generated on demand\n");
#else
	/*
	 * Let's pregenerate all samples corresponding
	 * to level PREGEN_SAMPLES_DBOV
	 */
	double randomnumbers[2], sample;
	int16_t *ppregeneratedsamples;
	double noisesigma;
	long i, j;

	noisesigma = NOISE_MAX_SIGMA * exp10(PREGEN_SAMPLES_DBOV * 0.05);
	ppregeneratedsamples = &pregeneratedsamples[0];
	for (i = 0; i < NUM_PREGENERATED_SAMPLES; i += 2)
	{
		box_muller_rng(noisesigma,
                               &randomnumbers[0], &randomnumbers[1]);
		for (j = 0; j < 2; j++)
		{
			sample = floor(randomnumbers[j] + 0.5);
			if (sample > SIGNAL_MAX_LEVEL)
				sample = SIGNAL_MAX_LEVEL;
			else if (sample < SIGNAL_MIN_LEVEL)
				sample = SIGNAL_MIN_LEVEL;
			*(ppregeneratedsamples++) = sample;
		}

	}
	opbx_log(LOG_DEBUG, "Using %ld pregenerated signed linear noise "
                            "samples\n", NUM_PREGENERATED_SAMPLES);
#endif /* !LOW_MEMORY */
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
