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

static int say_number_full(struct cw_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
    int res = 0;
    int playh = 0;
    char fn[256] = "";

    if (!num)
        return cw_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

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
        else if (playh)
        {
            snprintf(fn, sizeof(fn), "digits/hundred");
            playh = 0;
        }
        else if (num < 10)
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
                snprintf(fn, sizeof(fn), "digits/%d", (num/100));
                playh++;
                num -= ((num / 100) * 100);
            }
            else
            {
                if (num < 1000000)   /* 1,000,000 */
                {
                    res = say_number_full(chan, num / 1000, ints, language, options, audiofd, ctrlfd);
                    if (res)
                        return res;
                    num = num % 1000;
                    snprintf(fn, sizeof(fn), "digits/thousand");
                }
                else
                {
                    if (num < 1000000000)  	/* 1,000,000,000 */
                    {
                        res = say_number_full(chan, num / 1000000, ints, language, options, audiofd, ctrlfd);
                        if (res)
                            return res;
                        num = num % 1000000;
                        snprintf(fn, sizeof(fn), "digits/million");
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

static int say_date_with_format(struct cw_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tz)
{
    struct tm tm;
    int res=0, offset, sndoffset;
    char sndfile[256], nextmsg[256];

    cw_localtime(&t,&tm,tz);

    for (offset=0 ; format[offset] != '\0' ; offset++)
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
            res = wait_file(chan,ints, sndfile,lang);
            break;
        case 'A':
        case 'a':
            /* Sunday - Saturday */
            snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
            res = wait_file(chan, ints, nextmsg, lang);
            break;
        case 'B':
        case 'b':
        case 'h':
            /* January - December */
            snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
            res = wait_file(chan, ints, nextmsg, lang);
            break;
        case 'm':
            /* First - Twelfth */
            snprintf(nextmsg, sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
            res = wait_file(chan, ints, nextmsg, lang);
            break;
        case 'd':
        case 'e':
            /* First - Thirtyfirst */
            if (!(tm.tm_mday % 10) || (tm.tm_mday < 10))
            {
                snprintf(nextmsg, sizeof(nextmsg), "digits/h-%d", tm.tm_mday);
                res = wait_file(chan, ints, nextmsg, lang);
            }
            else
            {
                snprintf(nextmsg, sizeof(nextmsg), "digits/h-%dh", tm.tm_mday - (tm.tm_mday % 10));
                res = wait_file(chan, ints, nextmsg, lang);
                if (!res)
                {
                    snprintf(nextmsg, sizeof(nextmsg), "digits/h-%d", tm.tm_mday % 10);
                    res = wait_file(chan, ints, nextmsg, lang);
                }
            }
            break;
        case 'Y':
            /* Year */
            if (tm.tm_year > 99)
            {
                res = wait_file(chan,ints, "digits/2",lang);
                if (!res)
                {
                    res = wait_file(chan,ints, "digits/thousand",lang);
                }
                if (tm.tm_year > 100)
                {
                    if (!res)
                    {
                        snprintf(nextmsg, sizeof(nextmsg), "digits/%d", (tm.tm_year - 100) / 10);
                        res = wait_file(chan, ints, nextmsg, lang);
                        if (!res)
                        {
                            snprintf(nextmsg, sizeof(nextmsg), "digits/%d", (tm.tm_year - 100) % 10);
                            res = wait_file(chan, ints, nextmsg, lang);
                        }
                    }
                }
                if (!res)
                {
                    res = wait_file(chan,ints, "digits/year",lang);
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
                    res = wait_file(chan,ints, "digits/1",lang);
                    if (!res)
                    {
                        res = wait_file(chan,ints, "digits/9",lang);
                    }
                    if (!res)
                    {
                        if (tm.tm_year <= 9)
                        {
                            /* 1901 - 1909 */
                            res = wait_file(chan,ints, "digits/0",lang);
                            if (!res)
                            {
                                snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_year);
                                res = wait_file(chan, ints, nextmsg, lang);
                            }
                        }
                        else
                        {
                            /* 1910 - 1999 */
                            snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_year / 10);
                            res = wait_file(chan, ints, nextmsg, lang);
                            if (!res)
                            {
                                snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_year % 10);
                                res = wait_file(chan, ints, nextmsg, lang);
                            }
                        }
                    }
                }
                if (!res)
                {
                    res = wait_file(chan,ints, "digits/year",lang);
                }
            }
            break;
        case 'I':
        case 'l':
            /* 12-Hour */
            if (tm.tm_hour == 0)
                snprintf(nextmsg, sizeof(nextmsg), "digits/12");
            else if (tm.tm_hour > 12)
                snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
            else
                snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
            res = wait_file(chan, ints, nextmsg, lang);
            if (!res)
                res = wait_file(chan, ints, "digits/oclock",lang);
            break;
        case 'H':
        case 'k':
            /* 24-Hour */
            if (!(tm.tm_hour % 10) || tm.tm_hour < 10)
            {
                if (tm.tm_hour < 10)
                    res = wait_file(chan, ints, "digits/0", lang);
                snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
                res = wait_file(chan, ints, nextmsg, lang);
            }
            else
            {
                snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour - (tm.tm_hour % 10));
                res = wait_file(chan, ints, nextmsg, lang);
                if (!res)
                {
                    snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour % 10);
                    res = wait_file(chan, ints, nextmsg, lang);
                }
            }
            if (!res)
            {
                res = wait_file(chan, ints, "digits/oclock",lang);
            }
            break;
        case 'M':
            /* Minute */
            if (!(tm.tm_min % 10) || tm.tm_min < 10)
            {
                if (tm.tm_min < 10)
                {
                    res = wait_file(chan, ints, "digits/0", lang);
                }
                snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_min);
                res = wait_file(chan, ints, nextmsg, lang);
            }
            else
            {
                snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_min - (tm.tm_min % 10));
                res = wait_file(chan, ints, nextmsg, lang);
                if (!res)
                {
                    snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_min % 10);
                    res = wait_file(chan, ints, nextmsg, lang);
                }
            }
            if (!res)
                res = wait_file(chan, ints, "digits/minute",lang);
            break;
        case 'P':
        case 'p':
            /* AM/PM */
            if (tm.tm_hour > 11)
                snprintf(nextmsg, sizeof(nextmsg), "digits/p-m");
            else
                snprintf(nextmsg, sizeof(nextmsg), "digits/a-m");
            res = wait_file(chan, ints, nextmsg, lang);
            break;
        case 'Q':
            /* Shorthand for "Today", "Yesterday", or ABdY */
        {
            struct timeval now;
            struct tm tmnow;
            time_t beg_today;

            gettimeofday(&now,NULL);
            cw_localtime(&now.tv_sec,&tmnow,tz);
            /* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
            /* In any case, it saves not having to do cw_mktime() */
            beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
            if (beg_today < t)
            {
                /* Today */
                res = wait_file(chan,ints, "digits/today",lang);
            }
            else if (beg_today - 86400 < t)
            {
                /* Yesterday */
                res = wait_file(chan,ints, "digits/yesterday",lang);
            }
            else
            {
                res = cw_say_date_with_format(chan, t, ints, lang, "YBdA", tz);
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
            cw_localtime(&now.tv_sec,&tmnow,tz);
            /* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
            /* In any case, it saves not having to do cw_mktime() */
            beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
            if (beg_today < t)
            {
                /* Today */
            }
            else if ((beg_today - 86400) < t)
            {
                /* Yesterday */
                res = wait_file(chan,ints, "digits/yesterday",lang);
            }
            else if (beg_today - 86400 * 6 < t)
            {
                /* Within the last week */
                res = cw_say_date_with_format(chan, t, ints, lang, "A", tz);
            }
            else
            {
                res = cw_say_date_with_format(chan, t, ints, lang, "YBdA", tz);
            }
        }
            break;
        case 'R':
            res = cw_say_date_with_format(chan, t, ints, lang, "HM", tz);
            break;
        case 'S':
            /* Seconds */
            if (!(tm.tm_sec % 10) || tm.tm_sec < 10)
            {
                if (tm.tm_sec < 10)
                {
                    res = wait_file(chan, ints, "digits/0", lang);
                }
                snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
                res = wait_file(chan, ints, nextmsg, lang);
            }
            else
            {
                snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec - (tm.tm_sec % 10));
                res = wait_file(chan, ints, nextmsg, lang);
                if (!res)
                {
                    snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec % 10);
                    res = wait_file(chan, ints, nextmsg, lang);
                }
            }
            if (!res)
                res = wait_file(chan, ints, "digits/second", lang);
            break;
        case 'T':
            res = cw_say_date_with_format(chan, t, ints, lang, "HMS", tz);
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

static int say_time(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    struct tm tm;
    int res = 0;
    int hour;
    int pm = 0;

    localtime_r(&t, &tm);
    hour = tm.tm_hour;
    if (!hour)
        hour = 12;
    else if (hour == 12)
        pm = 1;
    else if (hour > 12)
    {
        hour -= 12;
        pm = 1;
    }
    if (pm)
    {
        if (!res)
            res = cw_streamfile(chan, "digits/p-m", lang);
    }
    else
    {
        if (!res)
            res = cw_streamfile(chan, "digits/a-m", lang);
    }
    if (!res)
        res = cw_waitstream(chan, ints);
    if (!res)
        res = cw_say_number(chan, hour, ints, lang, (char *) NULL);
    if (!res)
        res = cw_streamfile(chan, "digits/oclock", lang);
    if (!res)
        res = cw_waitstream(chan, ints);
    if (!res)
        res = cw_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
    if (!res)
        res = cw_streamfile(chan, "digits/minute", lang);
    if (!res)
        res = cw_waitstream(chan, ints);
    return res;
}

static int say_datetime(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    struct tm tm;
    char fn[256];
    int res = 0;
    int hour;
    int pm = 0;

    localtime_r(&t, &tm);
    if (!res)
        res = cw_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
    if (!res)
    {
        snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream(chan, ints);
    }
    if (!res)
        res = cw_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
    if (!res)
    {
        snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream(chan, ints);
    }

    hour = tm.tm_hour;
    if (!hour)
        hour = 12;
    else if (hour == 12)
        pm = 1;
    else if (hour > 12)
    {
        hour -= 12;
        pm = 1;
    }
    if (pm)
    {
        if (!res)
            res = cw_streamfile(chan, "digits/p-m", lang);
    }
    else
    {
        if (!res)
            res = cw_streamfile(chan, "digits/a-m", lang);
    }
    if (!res)
        res = cw_waitstream(chan, ints);
    if (!res)
        res = cw_say_number(chan, hour, ints, lang, (char *) NULL);
    if (!res)
        res = cw_streamfile(chan, "digits/oclock", lang);
    if (!res)
        res = cw_waitstream(chan, ints);
    if (!res)
        res = cw_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
    if (!res)
        res = cw_streamfile(chan, "digits/minute", lang);
    if (!res)
        res = cw_waitstream(chan, ints);
    return res;
}

lang_specific_speech_t lang_specific_zh_TW =
{
    "zh_TW",
    say_number_full,
    NULL,
    NULL,
    say_time,
    say_datetime,
    NULL,
    say_date_with_format
};
