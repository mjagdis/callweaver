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
extern void cw_ulaw_init(void);

/*! converts signed linear to mulaw */
extern CW_API_PUBLIC uint8_t __cw_lin2mu[16384];

/*! converts mulaw to signed linear */
extern CW_API_PUBLIC int16_t __cw_mulaw[256];

#define CW_LIN2MU(a) (__cw_lin2mu[((unsigned short)(a)) >> 2])
#define CW_MULAW(a) (__cw_mulaw[(a)])



/*! Init the ulaw conversion stuff */
/*!
 * To init the ulaw to slinear conversion stuff, this needs to be run.
 */
extern void cw_alaw_init(void);

/*! converts signed linear to alaw */
extern CW_API_PUBLIC uint8_t __cw_lin2a[8192];

/*! converts alaw to signed linear */
extern CW_API_PUBLIC int16_t __cw_alaw[256];


#define CW_LIN2A(a) (__cw_lin2a[((unsigned short)(a)) >> 3])
#define CW_ALAW(a) (__cw_alaw[(int)(a)])

#endif
