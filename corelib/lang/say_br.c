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

/*
   In addition to English, the following sounds are required:
      "1F", "2F"
      "100", "200", "300", "400", "500", "600", "700", "800", "900"
      "200F", "300F", "400F", "500F", "600F", "700F", "800F", "900F"
      "short-and", "million", "millions"
   Play a short "and" after each tens (20, 30,...,90 ) and each hundreds (100, 200,...,900) if continue
*/
static int say_number_full(struct cw_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
    int res = 0;
    int playa = 0;     /* play short-and */
    int mf = 1;        /* +1 = male; -1 = female */
    char fn[256] = "";

    if (!num)
        return cw_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

    if (options  &&  !strncasecmp(options, "f", 1))
        mf = -1;

    while (!res  &&  num)
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
            if (mf < 0)
                snprintf(fn, sizeof(fn), "digits/%dF", num);
            else
                snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num < 20)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num < 100)
        {
            snprintf(fn, sizeof(fn), "digits/%d", (num / 10) * 10);
            num %= 10;
            if (num)
                playa = 1;
        }
        else if (num == 100)
        {
            snprintf(fn, sizeof(fn), "digits/hundred");
            num = 0;
        }
        else if (num < 200)
        {
            snprintf(fn, sizeof(fn), "digits/100");
            num %= 100;
            playa = 1;
        }
        else if (num < 1000)
        {
            if (mf < 0)
                snprintf(fn, sizeof(fn), "digits/%dF", (num / 100) * 100);
            else
                snprintf(fn, sizeof(fn), "digits/%d", (num / 100) * 100);
            num %= 100;
            if (num)
                playa = 1;
        }
        else if (num < 1000000)
        {
            res = say_number_full(chan, (num / 1000), ints, language, options, audiofd, ctrlfd);
            snprintf(fn, sizeof(fn), "digits/1000");
            num = num % 1000;
        }
        else if (num < 1000000000)
        {
            res = say_number_full(chan, (num / 1000000), ints, language, options, audiofd, ctrlfd );
            if (num < 2000000)
                snprintf(fn, sizeof(fn), "digits/million");
            else
                snprintf(fn, sizeof(fn), "digits/millions");
            num = num % 1000000;
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
                if ((audiofd > -1) && (ctrlfd > -1))
                    res = cw_waitstream_full(chan, ints, audiofd, ctrlfd);
                else
                    res = cw_waitstream(chan, ints);
            }
            cw_stopstream(chan);
        }
        if (!res  &&  playa)
        {
            res = wait_file(chan, ints, "digits/short-and", language);
            cw_stopstream(chan);
            playa = 0;
        }
    }
    return res;
}

lang_specific_speech_t lang_specific_br =
{
    "br",
    say_number_full,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
