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
 * Digital Resonator - to produce low cost sinewaves
 *
 */

#ifndef _OPENPBX_RESONATOR_H
#define _OPENPBX_RESONATOR_H

/* A digital resonator is a fast, efficient way of producing sinewaves */
struct digital_resonator {
	float k;		/* x(n) = k * x(n-1) - x(n-2) */
	float xnm1;		/* xnm1 = x(n-1) */
	float xnm2;		/* xnm2 = x(n-2) */
	float cur_freq;		/* frequency currently being generated */
	float cur_ampl;		/* current signal amplitude */
	float cur_samp;		/* current sampling frequency */
	float upper_limit;	/* samples upper limit */
	float lower_limit;	/* samples lower limit */
};

/* Initial paramaters for the digital resonator */
void digital_resonator_init(struct digital_resonator *dr, float requested_frequency, float requested_amplitude, float sampling_frequency);

/* Reinitialization paramaters for the digital resonator */
void digital_resonator_reinit(struct digital_resonator *dr, float new_frequency, float new_amplitude);

/* How we get samples after resonator is initialized */
inline float digital_resonator_get_sample(struct digital_resonator *dr);

#endif /* OPENPBX_RESONATOR_H */
