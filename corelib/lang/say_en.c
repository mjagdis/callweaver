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

static int say_enumeration_full(struct cw_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
    int res = 0;
    int t = 0;
    char fn[256] = "";

    while (!res  &&  num)
    {
        if (num < 0)
        {
            snprintf(fn, sizeof(fn), "digits/minus"); /* kind of senseless for enumerations, but our best effort for error checking */
            if (num > INT_MIN)
                num = -num;
            else
                num = 0;
        }
        else if (num < 20)
        {
            snprintf(fn, sizeof(fn), "digits/h-%d", num);
            num = 0;
        }
        else if (num < 100)
        {
            int tens = num / 10;

            num = num % 10;
            if (num == 0)
                snprintf(fn, sizeof(fn), "digits/h-%d", (tens * 10));
            else
                snprintf(fn, sizeof(fn), "digits/%d", (tens * 10));
        }
        else if (num < 1000)
        {
            int hundreds = num / 100;

            num = num % 100;
            if (hundreds > 1  ||  t == 1)
                res = say_number_full(chan, hundreds, ints, language, options, audiofd, ctrlfd);
            if (res)
                return res;
            if (num)
                snprintf(fn, sizeof(fn), "digits/hundred");
            else
                snprintf(fn, sizeof(fn), "digits/h-hundred");
        }
        else if (num < 1000000)
        {
            int thousands = num / 1000;

            num = num % 1000;
            if (thousands > 1  ||  t == 1)
                res = say_number_full(chan, thousands, ints, language, options, audiofd, ctrlfd);
            if (res)
                return res;
            if (num)
                snprintf(fn, sizeof(fn), "digits/thousand");
            else
                snprintf(fn, sizeof(fn), "digits/h-thousand");
            t = 1;
        }
        else if (num < 1000000000)
        {
            int millions = num / 1000000;

            num = num % 1000000;
            t = 1;
            res = say_number_full(chan, millions, ints, language, options, audiofd, ctrlfd);
            if (res)
                return res;
            if (num)
                snprintf(fn, sizeof(fn), "digits/million");
            else
                snprintf(fn, sizeof(fn), "digits/h-million");
        }
        else if (num < INT_MAX)
        {
            int billions = num / 1000000000;

            num = num % 1000000000;
            t = 1;
            res = say_number_full(chan, billions, ints, language, options, audiofd, ctrlfd);
            if (res)
                return res;
            if (num)
                snprintf(fn, sizeof(fn), "digits/billion");
            else
                snprintf(fn, sizeof(fn), "digits/h-billion");
        }
        else if (num == INT_MAX)
        {
            snprintf(fn, sizeof(fn), "digits/h-last");
            num = 0;
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
    }
    return res;
}

static int say_date(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    struct tm tm;
    char fn[256];
    int res = 0;

    cw_localtime(&t, &tm, NULL);
    if (!res)
    {
        snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream(chan, ints);
    }
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
        res = cw_waitstream(chan, ints);
    if (!res)
        res = cw_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
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
            /* Month enumerated */
            res = cw_say_enumeration(chan, (tm.tm_mon + 1), ints, lang, (char *) NULL);
            break;
        case 'd':
        case 'e':
            /* First - Thirtyfirst */
            res = cw_say_enumeration(chan, tm.tm_mday, ints, lang, (char *) NULL);
            break;
        case 'Y':
            /* Year */
            if (tm.tm_year > 99)
            {
                res = cw_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
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
                    res = wait_file(chan,ints, "digits/19",lang);
                    if (!res)
                    {
                        if (tm.tm_year <= 9)
                        {
                            /* 1901 - 1909 */
                            res = wait_file(chan,ints, "digits/oh",lang);
                            if (!res)
                            {
                                snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_year);
                                res = wait_file(chan,ints,nextmsg,lang);
                            }
                        }
                        else if (tm.tm_year <= 20)
                        {
                            /* 1910 - 1920 */
                            snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_year);
                            res = wait_file(chan,ints,nextmsg,lang);
                        }
                        else
                        {
                            /* 1921 - 1999 */
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
            if (format[offset] == 'H')
            {
                /* e.g. oh-eight */
                if (tm.tm_hour < 10)
                {
                    res = wait_file(chan,ints, "digits/oh",lang);
                }
            }
            else
            {
                /* e.g. eight */
                if (tm.tm_hour == 0)
                {
                    res = wait_file(chan,ints, "digits/oh",lang);
                }
            }
            if (!res)
            {
                if (tm.tm_hour != 0)
                {
                    int remainder = tm.tm_hour;
                    if (tm.tm_hour > 20)
                    {
                        res = wait_file(chan,ints, "digits/20",lang);
                        remainder -= 20;
                    }
                    if (!res)
                    {
                        snprintf(nextmsg,sizeof(nextmsg), "digits/%d", remainder);
                        res = wait_file(chan,ints,nextmsg,lang);
                    }
                }
            }
            break;
        case 'M':
        case 'N':
            /* Minute */
            if (tm.tm_min == 0)
            {
                if (format[offset] == 'M')
                {
                    res = wait_file(chan, ints, "digits/oclock", lang);
                }
                else
                {
                    res = wait_file(chan, ints, "digits/hundred", lang);
                }
            }
            else if (tm.tm_min < 10)
            {
                res = wait_file(chan,ints, "digits/oh",lang);
                if (!res)
                {
                    snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_min);
                    res = wait_file(chan,ints,nextmsg,lang);
                }
            }
            else
            {
                res = cw_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
            }
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
                res = cw_say_date_with_format(chan, time, ints, lang, "ABdY", timezone);
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
                res = cw_say_date_with_format(chan, time, ints, lang, "ABdY", timezone);
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
            else
            {
                res = cw_say_number(chan, tm.tm_sec, ints, lang, (char *) NULL);
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

static int say_time(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    struct tm tm;
    int res = 0;
    int hour;
    int pm = 0;

    localtime_r(&t,&tm);
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
    if (!res)
        res = cw_say_number(chan, hour, ints, lang, (char *) NULL);

    if (tm.tm_min > 9)
    {
        if (!res)
            res = cw_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
    }
    else if (tm.tm_min)
    {
        if (!res)
            res = cw_streamfile(chan, "digits/oh", lang);
        if (!res)
            res = cw_waitstream(chan, ints);
        if (!res)
            res = cw_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
    }
    else
    {
        if (!res)
            res = cw_streamfile(chan, "digits/oclock", lang);
        if (!res)
            res = cw_waitstream(chan, ints);
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
    return res;
}

static int say_datetime(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    struct tm tm;
    char fn[256];
    int res = 0;
    int hour;
    int pm = 0;

    localtime_r(&t,&tm);
    if (!res)
    {
        snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream(chan, ints);
    }
    if (!res)
    {
        snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream(chan, ints);
    }
    if (!res)
        res = cw_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);

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
    if (!res)
        res = cw_say_number(chan, hour, ints, lang, (char *) NULL);

    if (tm.tm_min > 9)
    {
        if (!res)
            res = cw_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
    }
    else if (tm.tm_min)
    {
        if (!res)
            res = cw_streamfile(chan, "digits/oh", lang);
        if (!res)
            res = cw_waitstream(chan, ints);
        if (!res)
            res = cw_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
    }
    else
    {
        if (!res)
            res = cw_streamfile(chan, "digits/oclock", lang);
        if (!res)
            res = cw_waitstream(chan, ints);
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
        res = cw_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
    return res;
}

static int say_datetime_from_now(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    int res = 0;
    time_t nowt;
    int daydiff;
    struct tm tm;
    struct tm now;
    char fn[256];

    time(&nowt);

    localtime_r(&t,&tm);
    localtime_r(&nowt,&now);
    daydiff = now.tm_yday - tm.tm_yday;
    if ((daydiff < 0)  ||  (daydiff > 6))
    {
        /* Day of month and month */
        if (!res)
        {
            snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
            res = cw_streamfile(chan, fn, lang);
            if (!res)
                res = cw_waitstream(chan, ints);
        }
        if (!res)
            res = cw_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
    }
    else if (daydiff)
    {
        /* Just what day of the week */
        if (!res)
        {
            snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
            res = cw_streamfile(chan, fn, lang);
            if (!res)
                res = cw_waitstream(chan, ints);
        }
    } /* Otherwise, it was today */
    if (!res)
        res = cw_say_time(chan, t, ints, lang);
    return res;
}

lang_specific_speech_t lang_specific_en =
{
    "en",
    say_number_full,
    say_enumeration_full,
    say_date,
    say_time,
    say_datetime,
    say_datetime_from_now,
    say_date_with_format
};
