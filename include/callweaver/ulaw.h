/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 * \brief u-Law to Signed linear conversion
 */

#ifndef _OPENPBX_ULAW_H
#define _OPENPBX_ULAW_H

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

#endif /* _OPENPBX_ULAW_H */
