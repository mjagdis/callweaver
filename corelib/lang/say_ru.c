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

/*! \brief  determine last digits for thousands/millions (ru) */
static int get_lastdigits(int num)
{
    if (num < 20)
        return num;
    if (num < 100)
        return get_lastdigits(num % 10);
    if (num < 1000)
        return get_lastdigits(num % 100);
    return 0;	/* number too big */
}


/*! \brief  additional files:
	n00.gsm			(one hundred, two hundred, ...)
	thousand.gsm
	million.gsm
	thousands-i.gsm		(tisyachi)
	million-a.gsm		(milliona)
	thousands.gsm
	millions.gsm
	1f.gsm			(odna)
	2f.gsm			(dve)

	where 'n' from 1 to 9
*/
static int say_number_full(struct cw_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
    int res = 0;
    int lastdigits = 0;
    char fn[256] = "";

    if (!num)
        return cw_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

    while (!res  &&  (num))
    {
        if (num < 0)
        {
            snprintf(fn, sizeof(fn), "digits/minus");
            if (num > INT_MIN)
                num = -num;
            else
                num = 0;
        }
        else	if (num < 20)
        {
            if (options && strlen(options) == 1 && num < 3)
            {
                snprintf(fn, sizeof(fn), "digits/%d%s", num, options);
            }
            else
            {
                snprintf(fn, sizeof(fn), "digits/%d", num);
            }
            num = 0;
        }
        else	if (num < 100)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num - (num % 10));
            num %= 10;
        }
        else 	if (num < 1000)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num - (num % 100));
            num %= 100;
        }
        else 	if (num < 1000000)   /* 1,000,000 */
        {
            lastdigits = get_lastdigits(num / 1000);
            /* say thousands */
            if (lastdigits < 3)
                res = say_number_full(chan, num / 1000, ints, language, "f", audiofd, ctrlfd);
            else
                res = say_number_full(chan, num / 1000, ints, language, NULL, audiofd, ctrlfd);
            if (res)
                return res;
            if (lastdigits == 1)
            {
                snprintf(fn, sizeof(fn), "digits/thousand");
            }
            else if (lastdigits > 1 && lastdigits < 5)
            {
                snprintf(fn, sizeof(fn), "digits/thousands-i");
            }
            else
            {
                snprintf(fn, sizeof(fn), "digits/thousands");
            }
            num %= 1000;
        }
        else 	if (num < 1000000000)  	/* 1,000,000,000 */
        {
            lastdigits = get_lastdigits(num / 1000000);
            /* say millions */
            res = say_number_full(chan, num / 1000000, ints, language, NULL, audiofd, ctrlfd);
            if (res)
                return res;
            if (lastdigits == 1)
            {
                snprintf(fn, sizeof(fn), "digits/million");
            }
            else if (lastdigits > 1 && lastdigits < 5)
            {
                snprintf(fn, sizeof(fn), "digits/million-a");
            }
            else
            {
                snprintf(fn, sizeof(fn), "digits/millions");
            }
            num %= 1000000;
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
                if ((audiofd  > -1) && (ctrlfd > -1))
                    res = cw_waitstream_full(chan, ints, audiofd, ctrlfd);
                else
                    res = cw_waitstream(chan, ints);
            }
            cw_stopstream(chan);
        }
    }
    return res;
}

lang_specific_speech_t lang_specific_ru =
{
    "ru",
    say_number_full,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
