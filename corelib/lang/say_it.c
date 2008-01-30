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

static int say_number_full(struct cw_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
    int res = 0;
    int playh = 0;
    int tempnum = 0;
    char fn[256] = "";

    if (!num)
        return cw_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

    /*
    Italian support

    Like english, numbers up to 20 are a single 'word', and others
     compound, but with exceptions.
    For example 21 is not twenty-one, but there is a single word in 'it'.
    Idem for 28 (ie when a the 2nd part of a compund number
    starts with a vowel)

    There are exceptions also for hundred, thousand and million.
    In english 100 = one hundred, 200 is two hundred.
    In italian 100 = cento , like to say hundred (without one),
    200 and more are like english.

    Same applies for thousand:
    1000 is one thousand in en, 2000 is two thousand.
    In it we have 1000 = mille , 2000 = 2 mila

    For million(s) we use the plural, if more than one
    Also, one million is abbreviated in it, like on-million,
    or 'un milione', not 'uno milione'.
    So the right file is provided.
    */

    while (!res && (num || playh))
    {
        if (num < 0)
        {
            snprintf(fn, sizeof(fn), "digits/minus");
            if ( num > INT_MIN )
            {
                num = -num;
            }
            else
            {
                num = 0;
            }
        }
        else if (playh)
        {
            snprintf(fn, sizeof(fn), "digits/hundred");
            playh = 0;
        }
        else if (num < 20)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 21)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 28)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 31)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 38)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 41)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 48)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 51)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 58)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 61)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 68)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 71)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 78)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 81)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 88)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 91)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num == 98)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num < 100)
        {
            snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
            num -= ((num / 10) * 10);
        }
        else
        {
            if (num < 1000)
            {
                if ((num / 100) > 1)
                {
                    snprintf(fn, sizeof(fn), "digits/%d", (num/100));
                    playh++;
                }
                else
                {
                    snprintf(fn, sizeof(fn), "digits/hundred");
                }
                num -= ((num / 100) * 100);
            }
            else
            {
                if (num < 1000000)   /* 1,000,000 */
                {
                    if ((num/1000) > 1)
                        res = say_number_full(chan, num / 1000, ints, language, options, audiofd, ctrlfd);
                    if (res)
                        return res;
                    tempnum = num;
                    num = num % 1000;
                    if ((tempnum / 1000) < 2)
                        snprintf(fn, sizeof(fn), "digits/thousand");
                    else /* for 1000 it says mille, for >1000 (eg 2000) says mila */
                        snprintf(fn, sizeof(fn), "digits/thousands");
                }
                else
                {
                    if (num < 1000000000)   /* 1,000,000,000 */
                    {
                        if ((num / 1000000) > 1)
                            res = say_number_full(chan, num / 1000000, ints, language, options, audiofd, ctrlfd);
                        if (res)
                            return res;
                        tempnum = num;
                        num = num % 1000000;
                        if ((tempnum / 1000000) < 2)
                            snprintf(fn, sizeof(fn), "digits/million");
                        else
                            snprintf(fn, sizeof(fn), "digits/millions");
                    }
                    else
                    {
                        cw_log(CW_LOG_DEBUG, "Number '%d' is too big for me\n", num);
                        res = -1;
                    }
                }
            }
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
    }
    return res;
}

static int say_date_with_format(struct cw_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
{
    struct tm tm;
    int res = 0;
    int offset;
    int sndoffset;
    char sndfile[256];
    char nextmsg[256];

    cw_localtime(&time, &tm, timezone);

    for (offset = 0;  format[offset] != '\0';  offset++)
    {
        cw_log(CW_LOG_DEBUG, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
        switch (format[offset])
        {
            /* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
        case '\'':
            /* Literal name of a sound file */
            sndoffset=0;
            for (sndoffset=0 ; (format[++offset] != '\'') && (sndoffset < 256) ; sndoffset++)
                sndfile[sndoffset] = format[offset];
            sndfile[sndoffset] = '\0';
            res = wait_file(chan,ints,sndfile,lang);
            break;
        case 'A':
        case 'a':
            /* Sunday - Saturday */
            snprintf(nextmsg,sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
            res = wait_file(chan,ints,nextmsg,lang);
            break;
        case 'B':
        case 'b':
        case 'h':
            /* January - December */
            snprintf(nextmsg,sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
            res = wait_file(chan,ints,nextmsg,lang);
            break;
        case 'm':
            /* First - Twelfth */
            snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
            res = wait_file(chan,ints,nextmsg,lang);
            break;
        case 'd':
        case 'e':
            /* First day of the month is spelled as ordinal */
            if (tm.tm_mday == 1)
            {
                snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mday);
                res = wait_file(chan,ints,nextmsg,lang);
            }
            else
            {
                if (!res)
                {
                    res = cw_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
                }
            }
            break;
        case 'Y':
            /* Year */
            if (tm.tm_year > 99)
            {
                res = wait_file(chan,ints, "digits/ore-2000",lang);
                if (tm.tm_year > 100)
                {
                    if (!res)
                    {
                        /* This works until the end of 2021 */
                        snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_year - 100);
                        res = wait_file(chan,ints,nextmsg,lang);
                    }
                }
            }
            else
            {
                if (tm.tm_year < 1)
                {
                    /* I'm not going to handle 1900 and prior */
                    /* We'll just be silent on the year, instead of bombing out. */
                }
                else
                {
                    res = wait_file(chan,ints, "digits/ore-1900",lang);
                    if ((!res) && (tm.tm_year != 0))
                    {
                        if (tm.tm_year <= 21)
                        {
                            /* 1910 - 1921 */
                            snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_year);
                            res = wait_file(chan,ints,nextmsg,lang);
                        }
                        else
                        {
                            /* 1922 - 1999, but sounds badly in 1928, 1931, 1938, etc... */
                            int ten, one;
                            ten = tm.tm_year / 10;
                            one = tm.tm_year % 10;
                            snprintf(nextmsg,sizeof(nextmsg), "digits/%d", ten * 10);
                            res = wait_file(chan,ints,nextmsg,lang);
                            if (!res)
                            {
                                if (one != 0)
                                {
                                    snprintf(nextmsg,sizeof(nextmsg), "digits/%d", one);
                                    res = wait_file(chan,ints,nextmsg,lang);
                                }
                            }
                        }
                    }
                }
            }
            break;
        case 'I':
        case 'l':
            /* 12-Hour */
            if (tm.tm_hour == 0)
                snprintf(nextmsg,sizeof(nextmsg), "digits/12");
            else if (tm.tm_hour > 12)
                snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
            else
                snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour);
            res = wait_file(chan,ints,nextmsg,lang);
            break;
        case 'H':
        case 'k':
            /* 24-Hour */
            if (tm.tm_hour == 0)
            {
                res = wait_file(chan,ints, "digits/ore-mezzanotte",lang);
            }
            else if (tm.tm_hour == 1)
            {
                res = wait_file(chan,ints, "digits/ore-una",lang);
            }
            else
            {
                res = cw_say_number(chan, tm.tm_hour, ints, lang, (char *) NULL);
            }
            break;
        case 'M':
            /* Minute */
            res = cw_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
            break;
        case 'P':
        case 'p':
            /* AM/PM */
            if (tm.tm_hour > 11)
                snprintf(nextmsg,sizeof(nextmsg), "digits/p-m");
            else
                snprintf(nextmsg,sizeof(nextmsg), "digits/a-m");
            res = wait_file(chan,ints,nextmsg,lang);
            break;
        case 'Q':
            /* Shorthand for "Today", "Yesterday", or ABdY */
        {
            struct timeval now;
            struct tm tmnow;
            time_t beg_today;

            gettimeofday(&now,NULL);
            cw_localtime(&now.tv_sec,&tmnow,timezone);
            /* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
            /* In any case, it saves not having to do cw_mktime() */
            beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
            if (beg_today < time)
            {
                /* Today */
                res = wait_file(chan,ints, "digits/today",lang);
            }
            else if (beg_today - 86400 < time)
            {
                /* Yesterday */
                res = wait_file(chan,ints, "digits/yesterday",lang);
            }
            else
            {
                res = cw_say_date_with_format(chan, time, ints, lang, "AdB", timezone);
            }
        }
        break;
        case 'q':
            /* Shorthand for "" (today), "Yesterday", A (weekday), or ABdY */
        {
            struct timeval now;
            struct tm tmnow;
            time_t beg_today;

            gettimeofday(&now,NULL);
            cw_localtime(&now.tv_sec,&tmnow,timezone);
            /* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
            /* In any case, it saves not having to do cw_mktime() */
            beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
            if (beg_today < time)
            {
                /* Today */
            }
            else if ((beg_today - 86400) < time)
            {
                /* Yesterday */
                res = wait_file(chan,ints, "digits/yesterday",lang);
            }
            else if (beg_today - 86400 * 6 < time)
            {
                /* Within the last week */
                res = cw_say_date_with_format(chan, time, ints, lang, "A", timezone);
            }
            else
            {
                res = cw_say_date_with_format(chan, time, ints, lang, "AdB", timezone);
            }
        }
        break;
        case 'R':
            res = cw_say_date_with_format(chan, time, ints, lang, "HM", timezone);
            break;
        case 'S':
            /* Seconds */
            if (tm.tm_sec == 0)
            {
                snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec);
                res = wait_file(chan,ints,nextmsg,lang);
            }
            else if (tm.tm_sec < 10)
            {
                res = wait_file(chan,ints, "digits/oh",lang);
                if (!res)
                {
                    snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec);
                    res = wait_file(chan,ints,nextmsg,lang);
                }
            }
            else if ((tm.tm_sec < 21) || (tm.tm_sec % 10 == 0))
            {
                snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec);
                res = wait_file(chan,ints,nextmsg,lang);
            }
            else
            {
                int ten, one;
                ten = (tm.tm_sec / 10) * 10;
                one = (tm.tm_sec % 10);
                snprintf(nextmsg,sizeof(nextmsg), "digits/%d", ten);
                res = wait_file(chan,ints,nextmsg,lang);
                if (!res)
                {
                    /* Fifty, not fifty-zero */
                    if (one != 0)
                    {
                        snprintf(nextmsg,sizeof(nextmsg), "digits/%d", one);
                        res = wait_file(chan,ints,nextmsg,lang);
                    }
                }
            }
            break;
        case 'T':
            res = cw_say_date_with_format(chan, time, ints, lang, "HMS", timezone);
            break;
        case ' ':
        case '	':
            /* Just ignore spaces and tabs */
            break;
        default:
            /* Unknown character */
            cw_log(CW_LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
        }
        /* Jump out on DTMF */
        if (res)
            break;
    }
    return res;
}

lang_specific_speech_t lang_specific_it =
{
    "it",
    say_number_full,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    say_date_with_format
};
