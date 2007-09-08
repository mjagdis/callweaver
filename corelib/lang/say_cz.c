/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * George Konstantoulakis <gkon@inaccessnetworks.com>
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

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

#ifdef SOLARIS
#include <iso/limits_iso.h>
#endif

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/say.h"
#include "callweaver/lock.h"
#include "callweaver/localtime.h"
#include "callweaver/utils.h"

#include "say.h"

static int exp10_int(int power)
{
    int x, res= 1;

    for (x = 0;  x < power;  x++)
        res *= 10;
    return res;
}

/* files needed:
 * 1m,2m - gender male
 * 1w,2w - gender female
 * 3,4,...,20
 * 30,40,...,90
 *
 * hundereds - 100 - sto, 200 - 2ste, 300,400 3,4sta, 500,600,...,900 5,6,...9set
 *
 * for each number 10^(3n + 3) exist 3 files represented as:
 * 		1 tousand = jeden tisic = 1_E3
 * 		2,3,4 tousands = dva,tri,ctyri tisice = 2-3_E3
 * 		5,6,... tousands = pet,sest,... tisic = 5_E3
 *
 * 		million = _E6
 * 		miliard = _E9
 * 		etc...
 *
 * thousand, milion are  gender male, so 1 and 2 is 1m 2m
 * miliard is gender female, so 1 and 2 is 1w 2w
 */
static int say_number_full(struct opbx_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
    int res = 0;
    int playh = 0;
    char fn[256] = "";
    int hundered = 0;
    int left = 0;
    int length = 0;

    /* options - w = woman, m = man, n = neutral. Defaultl is woman */
    if (!options)
        options = "w";

    if (!num)
        return opbx_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

    while (!res  &&  (num  ||  playh))
    {
        if (num < 0)
        {
            snprintf(fn, sizeof(fn), "digits/minus");
            if (num > INT_MIN)
                num = -num;
            else
                num = 0;
        }
        else if (num < 3)
        {
            snprintf(fn, sizeof(fn), "digits/%d%c",num,options[0]);
            playh = 0;
            num = 0;
        }
        else if (num < 20)
        {
            snprintf(fn, sizeof(fn), "digits/%d",num);
            playh = 0;
            num = 0;
        }
        else if (num < 100)
        {
            snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
            num -= ((num / 10) * 10);
        }
        else if (num < 1000)
        {
            hundered = num / 100;
            if ( hundered == 1 )
            {
                snprintf(fn, sizeof(fn), "digits/1sto");
            }
            else if ( hundered == 2 )
            {
                snprintf(fn, sizeof(fn), "digits/2ste");
            }
            else
            {
                res = say_number_full(chan,hundered,ints,language,options,audiofd,ctrlfd);
                if (res)
                    return res;
                if (hundered == 3  ||  hundered == 4)
                    snprintf(fn, sizeof(fn), "digits/sta");
                else if (hundered > 4)
                    snprintf(fn, sizeof(fn), "digits/set");
            }
            num -= (hundered * 100);
        }
        else   /* num > 1000 */
        {
            length = (int) log10(num) + 1;
            while ((length % 3) != 1)
                length--;
            left = num / (exp10_int(length - 1));
            if (left == 2)
            {
                switch (length - 1)
                {
                case 9:
                    options = "w";  /* 1,000,000,000 gender female */
                    break;
                default :
                    options = "m"; /* others are male */
                }
            }
            if (left > 1)	  /* we dont say "one thousand" but only thousand */
            {
                res = say_number_full(chan,left,ints,language,options,audiofd,ctrlfd);
                if (res)
                    return res;
            }
            if (left >= 5)   /* >= 5 have the same declesion */
            {
                snprintf(fn, sizeof(fn), "digits/5_E%d",length-1);
            }
            else if ( left >= 2 && left <= 4 )
            {
                snprintf(fn, sizeof(fn), "digits/2-4_E%d",length-1);
            }
            else   /* left == 1 */
            {
                snprintf(fn, sizeof(fn), "digits/1_E%d",length-1);
            }
            num -= left * (exp10_int(length - 1));
        }
        if (!res)
        {
            if (!opbx_streamfile(chan, fn, language))
            {
                if ((audiofd > -1) && (ctrlfd > -1))
                {
                    res = opbx_waitstream_full(chan, ints, audiofd, ctrlfd);
                }
                else
                {
                    res = opbx_waitstream(chan, ints);
                }
            }
            opbx_stopstream(chan);
        }
    }
    return res;
}

lang_specific_speech_t lang_specific_cz =
{
    "cz",
    say_number_full,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
