/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * u-Law to Signed linear conversion
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <inttypes.h>
#include <time.h>
#include <spandsp.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL$", "$Revision$")

#include "openpbx/ulaw.h"

uint8_t __opbx_lin2mu[16384];
int16_t __opbx_mulaw[256];

void opbx_ulaw_init(void)
{
	int i;

	/* Set up mu-law conversion table */
	for (i = 0;  i < 256;  i++)
        __opbx_mulaw[i] = ulaw_to_linear(i);
	/* Set up the reverse (mu-law) conversion table */
	for (i = -32768; i < 32768; i++)
		__opbx_lin2mu[((unsigned short) i) >> 2] = linear_to_ulaw(i);
    return 0;
}
