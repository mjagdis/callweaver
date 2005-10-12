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
 */

/*
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
*/
#include <math.h>

#include "include/openpbx.h"
#include "openpbx/resonator.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision: 1 $")

/* Initial paramaters for the digital resonator */
void digital_resonator_init(struct digital_resonator *dr, float requested_frequency, float requested_amplitude, float sampling_frequency)
{
	/*
	 * User must do sanity check. Not verifying here if
	 * requested frequency < 1/2 * sampling_frequency.
	 * Resonator breaks at or after 1/2 * sampling_frequency.
	 */
	float halfk;

	halfk = cosf((M_PI + M_PI) * requested_frequency / sampling_frequency);
	dr->k = halfk + halfk;
	dr->xnm1 = 0.0f;
	dr->xnm2 = -requested_amplitude * sqrtf(1.0f - halfk * halfk);

	/*
	 * These are the sample limits to avoid a runaway resonator.
	 * Note that negative amplitudes are allowed; that's why we
	 * use fabsf(requested_amplitude) for upper limit
	 */
	dr->upper_limit = fabsf(requested_amplitude);
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
void digital_resonator_reinit(struct digital_resonator *dr, float new_frequency, float new_amplitude)
{
	/*
	 * If new frequency equals current frequency, we can do
	 * a soft reinit if current amplitude is not zero.
	 */
	if (new_frequency == dr->cur_freq && dr->cur_ampl != 0.0f) {
		/* We can reinit. Just scale last two samples
		 * to adjust for new amplitude, if necessary */
		if (new_amplitude != dr->cur_ampl) {
			float scalefactor;

			scalefactor = new_amplitude / dr->cur_ampl;
			dr->xnm1 *= scalefactor;
			dr->xnm2 *= scalefactor;
			dr->cur_ampl = new_amplitude;
			dr->upper_limit = fabsf(new_amplitude);
			dr->lower_limit = -dr->upper_limit;
		}
	} else
		   digital_resonator_init(dr, new_frequency, new_amplitude, dr->cur_samp);
}

/* this is where we generate the "next" sample and update state */
float digital_resonator_get_sample(struct digital_resonator *dr)
{
	float sample;

	sample = dr->k * dr->xnm1 - dr->xnm2;
	if (sample > dr->upper_limit)
		sample = dr->upper_limit;
	else if (sample < dr->lower_limit)
		sample = dr->lower_limit;
	dr->xnm2 = dr->xnm1;
	dr->xnm1 = sample;
	return sample;
}

