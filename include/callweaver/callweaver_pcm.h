/*
 * CallWeaver -- An open source telephony toolkit.
 *
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
 * 
 *
 */



#ifndef _CALLWEAVER_CALLWEAVER_PCM_H
#define _CALLWEAVER_CALLWEAVER_PCM_H



/*! Init the ulaw conversion stuff */
/*!
 * To init the ulaw to slinear conversion stuff, this needs to be run.
 */
extern void opbx_ulaw_init(void);

/*! converts signed linear to mulaw */
extern uint8_t __opbx_lin2mu[16384];

/*! converts mulaw to signed linear */
extern int16_t __opbx_mulaw[256];

#define OPBX_LIN2MU(a) (__opbx_lin2mu[((unsigned short)(a)) >> 2])
#define OPBX_MULAW(a) (__opbx_mulaw[(a)])



/*! Init the ulaw conversion stuff */
/*!
 * To init the ulaw to slinear conversion stuff, this needs to be run.
 */
extern void opbx_alaw_init(void);

/*! converts signed linear to alaw */
extern uint8_t __opbx_lin2a[8192];

/*! converts alaw to signed linear */
extern int16_t __opbx_alaw[256];


#define OPBX_LIN2A(a) (__opbx_lin2a[((unsigned short)(a)) >> 3])
#define OPBX_ALAW(a) (__opbx_alaw[(int)(a)])

#endif
