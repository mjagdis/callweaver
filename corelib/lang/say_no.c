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

/* New files:
 In addition to American English, the following sounds are required:  "and", "1N"
 */
static int say_number_full(struct cw_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
    int res = 0;
    int playh = 0;
    int playa = 0;
    int cn = 1;		/* +1 = commune; -1 = neuter */
    char fn[256] = "";

    if (!num)
        return cw_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

    if (options  &&  !strncasecmp(options, "n", 1))
        cn = -1;

    while (!res  &&  (num  ||  playh  ||  playa))
    {
        /* The grammar for Norwegian numbers is the same as for English except
        * for the following:
        * - 1 exists in both commune ("en", file "1") and neuter ("ett", file "1N")
        *   "and" before the last two digits, i.e. 2034 is "two thousand and
        *   thirty-four" and 1000012 is "one million and twelve".
        */
        if (num < 0)
        {
            snprintf(fn, sizeof(fn), "digits/minus");
            if (num > INT_MIN)
                num = -num;
            else
                num = 0;
        }
        else if (playh)
        {
            snprintf(fn, sizeof(fn), "digits/hundred");
            playh = 0;
        }
        else if (playa)
        {
            snprintf(fn, sizeof(fn), "digits/and");
            playa = 0;
        }
        else if (num == 1 && cn == -1)
        {
            snprintf(fn, sizeof(fn), "digits/1N");
            num = 0;
        }
        else if (num < 20)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num < 100)
        {
            snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
            num -= ((num / 10) * 10);
        }
        else if (num < 1000)
        {
            int hundreds = num / 100;
            if (hundreds == 1)
                snprintf(fn, sizeof(fn), "digits/1N");
            else
                snprintf(fn, sizeof(fn), "digits/%d", (num / 100));

            playh++;
            num -= 100 * hundreds;
            if (num)
                playa++;
        }
        else if (num < 1000000)
        {
            res = say_number_full(chan, num / 1000, ints, language, "n", audiofd, ctrlfd);
            if (res)
                return res;
            snprintf(fn, sizeof(fn), "digits/thousand");
            num = num % 1000;
            if (num && num < 100)
                playa++;
        }
        else if (num < 1000000000)
        {
            int millions = num / 1000000;
            res = say_number_full(chan, millions, ints, language, "c", audiofd, ctrlfd);
            if (res)
                return res;
            snprintf(fn, sizeof(fn), "digits/million");
            num = num % 1000000;
            if (num && num < 100)
                playa++;
        }
        else
        {
            cw_log(CW_LOG_DEBUG, "Number '%d' is too big for me\n", num);
            res = -1;
        }

        if (!res)
        {
            if (!cw_streamfile(chan, fn, language))
            {
                if ((audiofd > -1)  &&  (ctrlfd > -1))
                    res = cw_waitstream_full(chan, ints, audiofd, ctrlfd);
                else
                    res = cw_waitstream(chan, ints);
            }
            cw_stopstream(chan);
        }
    }
    return res;
}

lang_specific_speech_t lang_specific_no =
{
    "no",
    say_number_full,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
