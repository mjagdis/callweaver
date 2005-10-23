/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Carlos Antunes
 *
 * Carlos Antunes <cmantunes@gmail.com>
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
 * Digital Resonator - a fast and efficient way to produce low cost sinewaves
 *
 * How to use:
 *
 * Start by initializing an opaque digital_resonator struct by submitting to
 * the function digital_resonator_init, along with the requested frequency,
 * amplitude and sampling frequency one is working with. After this is done,
 * just call digital_resonator_get_sample repeatedly with a pointer to the
 * digital_resonator struct variable. Every time you call, you get the next
 * sample.
 *
 * Example:
 *
 * struct digital_resonator dr;
 * digital_resonator_init(&dr, 1000, 67, 8000);
 * sample_1 = digital_resonator_get_sample(&dr);
 * sample_2 = digital_resonator_get_sample(&dr);
 *   .
 *   .
 *   .
 * sample_n = digital_resonator_get_sample(&dr);
 *
 * The function digital_resonator_reinit may be used to reinitialize the
 * digital resonator if sampling frequency hasn't changed.
 */

#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "include/openpbx.h"
#include "openpbx/resonator.h"

OPENPBX_FILE_VERSION("$HeadURL$", "$Revision$")

/* Initial paramaters for the digital resonator */
void digital_resonator_init(struct digital_resonator *dr, uint16_t requested_frequency, int16_t requested_amplitude, uint16_t sampling_frequency)
{
	double halfk;
	int32_t k;

	halfk = cos(2.0 * M_PI * requested_frequency / sampling_frequency);
	k = floor(16384.0 * 2.0 * halfk + 0.5);
	dr->k = k;
	if (k == 32768L)
	{
		/* Special case: requested_frequency is zero */
		dr->xnm1 = 0;
		dr->xnm2 = 0;
	}
	else if (k == -32768L)
	{
		/* Special case: requested_frequency is half sampling freq */
		dr->xnm1 = requested_amplitude;
		dr->xnm2 = -requested_amplitude;
	}
	else
	{
		dr->xnm1 = 0;
		dr->xnm2 = -requested_amplitude * sqrt(1.0 - halfk * halfk);
	}

	/*
	 * These are the sample limits to avoid a runaway resonator.
	 * Note that negative amplitudes are allowed; that's why we
	 * use fabsf(requested_amplitude) for upper limit
	 */
	dr->upper_limit = abs(requested_amplitude);
	dr->lower_limit = -dr->upper_limit;

	/* Let's keep this for reinit if necessary */
	dr->cur_freq = requested_frequency;
	dr->cur_ampl = requested_amplitude;
	dr->cur_samp = sampling_frequency;
}

/*
 * Reinitialize paramaters for the digital resonator. If sampling
 * frequency has changed, use digital_resonator_init instead
 */
void digital_resonator_reinit(struct digital_resonator *dr, uint16_t new_frequency, int16_t new_amplitude)
{
	/*
	 * If new frequency equals current frequency, we can do
	 * a soft reinit if current amplitude is not zero.
	 */
	if (new_frequency == dr->cur_freq && dr->cur_ampl != 0) {
		/* We can reinit. Just scale last two samples
		 * to adjust for new amplitude, if necessary */
		if (new_amplitude != dr->cur_ampl) {
			double scalefactor;

			scalefactor = new_amplitude / (double)dr->cur_ampl;
			dr->xnm1 = floor(0.5 + dr->xnm1 * scalefactor);
			dr->xnm2 = floor(0.5 + dr->xnm2 * scalefactor);
			dr->cur_ampl = new_amplitude;
			dr->upper_limit = abs(new_amplitude);
			dr->lower_limit = -dr->upper_limit;
		}
	} else
		   digital_resonator_init(dr, new_frequency, new_amplitude, dr->cur_samp);
}

/* This is where we generate the "next" sample and update state */
int16_t digital_resonator_get_sample(struct digital_resonator *dr)
{
	int32_t sample;

	sample = ((dr->k * (int32_t)dr->xnm1) >> 14) - dr->xnm2;
	if (sample > dr->upper_limit)
		sample = dr->upper_limit;
	else if (sample < dr->lower_limit)
		sample = dr->lower_limit;
	dr->xnm2 = dr->xnm1;
	dr->xnm1 = sample;
	return sample;
}

