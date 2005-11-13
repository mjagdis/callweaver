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

#include "openpbx/alaw.h"

#define AMI_MASK 0x55

static inline unsigned char linear2alaw (short int linear)
{
    int mask;
    int seg;
    int pcm_val;
    static int seg_end[8] =
    {
         0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF
    };
    
    pcm_val = linear;
    if (pcm_val >= 0)
    {
        /* Sign (7th) bit = 1 */
        mask = AMI_MASK | 0x80;
    }
    else
    {
        /* Sign bit = 0 */
        mask = AMI_MASK;
        pcm_val = -pcm_val;
    }

    /* Convert the scaled magnitude to segment number. */
    for (seg = 0;  seg < 8;  seg++)
    {
        if (pcm_val <= seg_end[seg])
	    break;
    }
    /* Combine the sign, segment, and quantization bits. */
    return  ((seg << 4) | ((pcm_val >> ((seg)  ?  (seg + 3)  :  4)) & 0x0F)) ^ mask;
}
/*- End of function --------------------------------------------------------*/

static inline short int alaw2linear(unsigned char alaw)
{
    int i;
    int seg;

    alaw ^= AMI_MASK;
    i = ((alaw & 0x0F) << 4);
    seg = (((int) alaw & 0x70) >> 4);
    if (seg)
        i = (i + 0x100) << (seg - 1);
    return (short int) ((alaw & 0x80)  ?  i  :  -i);
}

uint8_t __opbx_lin2a[8192];
int16_t __opbx_alaw[256];

void opbx_alaw_init(void)
{
	int i;
	
    /* Set up A-law conversion table */
	for (i = 0;  i < 256;  i++)
		__opbx_alaw[i] = alaw_to_linear(i);
	/* Set up the reverse (A-law) conversion table */
	for(i = -32768; i < 32768; i++)
		__opbx_lin2a[((unsigned short)i) >> 3] = linear_to_alaw(i);
}

