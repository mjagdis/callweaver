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

/* New files:
 In addition to English, the following sounds are required: "1N", "millions", "and" and "1-and" through "9-and"
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

    if (options && !strncasecmp(options, "n",1)) cn = -1;

    while (!res && (num || playh || playa ))
    {
        /* The grammar for Danish numbers is the same as for English except
        * for the following:
        * - 1 exists in both commune ("en", file "1N") and neuter ("et", file "1")
        * - numbers 20 through 99 are said in reverse order, i.e. 21 is
        *   "one-and twenty" and 68 is "eight-and sixty".
        * - "million" is different in singular and plural form
        * - numbers > 1000 with zero as the third digit from last have an
        *   "and" before the last two digits, i.e. 2034 is "two thousand and
        *   four-and thirty" and 1000012 is "one million and twelve".
        */
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
            int ones = num % 10;
            if (ones)
            {
                snprintf(fn, sizeof(fn), "digits/%d-and", ones);
                num -= ones;
            }
            else
            {
                snprintf(fn, sizeof(fn), "digits/%d", num);
                num = 0;
            }
        }
        else
        {
            if (num < 1000)
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
            else
            {
                if (num < 1000000)
                {
                    res = say_number_full(chan, num / 1000, ints, language, "n", audiofd, ctrlfd);
                    if (res)
                        return res;
                    num = num % 1000;
                    snprintf(fn, sizeof(fn), "digits/thousand");
                }
                else
                {
                    if (num < 1000000000)
                    {
                        int millions = num / 1000000;

                        res = say_number_full(chan, millions, ints, language, "c", audiofd, ctrlfd);
                        if (res)
                            return res;
                        if (millions == 1)
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
                }
                if (num && num < 100)
                    playa++;
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

static int say_enumeration_full(struct cw_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
    /* options can be: '' or 'm' male gender; 'f' female gender; 'n' neuter gender; 'p' plural */
    int res = 0, t = 0;
    char fn[256] = "", fna[256] = "";
    char *gender;

    if (options && !strncasecmp(options, "f",1))
    {
        gender = "F";
    }
    else if (options && !strncasecmp(options, "n",1))
    {
        gender = "N";
    }
    else
    {
        gender = "";
    }

    if (!num)
        return cw_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

    while (!res && num)
    {
        if (num < 0)
        {
            snprintf(fn, sizeof(fn), "digits/minus"); /* kind of senseless for enumerations, but our best effort for error checking */
            if ( num > INT_MIN )
            {
                num = -num;
            }
            else
            {
                num = 0;
            }
        }
        else if (num < 100 && t)
        {
            snprintf(fn, sizeof(fn), "digits/and");
            t = 0;
        }
        else if (num < 20)
        {
            snprintf(fn, sizeof(fn), "digits/h-%d%s", num, gender);
            num = 0;
        }
        else if (num < 100)
        {
            int ones = num % 10;
            if (ones)
            {
                snprintf(fn, sizeof(fn), "digits/%d-and", ones);
                num -= ones;
            }
            else
            {
                snprintf(fn, sizeof(fn), "digits/h-%d%s", num, gender);
                num = 0;
            }
        }
        else if (num == 100 && t == 0)
        {
            snprintf(fn, sizeof(fn), "digits/h-hundred%s", gender);
            num = 0;
        }
        else if (num < 1000)
        {
            int hundreds = num / 100;
            num = num % 100;
            if (hundreds == 1)
                snprintf(fn, sizeof(fn), "digits/1N");
            else
                snprintf(fn, sizeof(fn), "digits/%d", hundreds);
            if (num)
                snprintf(fna, sizeof(fna), "digits/hundred");
            else
                snprintf(fna, sizeof(fna), "digits/h-hundred%s", gender);
            t = 1;
        }
        else 	if (num < 1000000)
        {
            int thousands = num / 1000;
            num = num % 1000;
            if (thousands == 1)
            {
                if (num)
                {
                    snprintf(fn, sizeof(fn), "digits/1N");
                    snprintf(fna, sizeof(fna), "digits/thousand");
                }
                else
                {
                    if (t)
                    {
                        snprintf(fn, sizeof(fn), "digits/1N");
                        snprintf(fna, sizeof(fna), "digits/h-thousand%s", gender);
                    }
                    else
                    {
                        snprintf(fn, sizeof(fn), "digits/h-thousand%s", gender);
                    }
                }
            }
            else
            {
                res = lang_specific_de.say_number_full(chan, thousands, ints, language, options, audiofd, ctrlfd);
                if (res)
                    return res;
                if (num)
                    snprintf(fn, sizeof(fn), "digits/thousand");
                else
                    snprintf(fn, sizeof(fn), "digits/h-thousand%s", gender);
            }
            t = 1;
        }
        else if (num < 1000000000)
        {
            int millions = num / 1000000;
            num = num % 1000000;
            if (millions == 1)
            {
                if (num)
                {
                    snprintf(fn, sizeof(fn), "digits/1F");
                    snprintf(fna, sizeof(fna), "digits/million");
                }
                else
                {
                    snprintf(fn, sizeof(fn), "digits/1N");
                    snprintf(fna, sizeof(fna), "digits/h-million%s", gender);
                }
            }
            else
            {
                res = lang_specific_de.say_number_full(chan, millions, ints, language, options, audiofd, ctrlfd);
                if (res)
                    return res;
                if (num)
                    snprintf(fn, sizeof(fn), "digits/millions");
                else
                    snprintf(fn, sizeof(fn), "digits/h-million%s", gender);
            }
            t = 1;
        }
        else if (num < INT_MAX)
        {
            int billions = num / 1000000000;
            num = num % 1000000000;
            if (billions == 1)
            {
                if (num)
                {
                    snprintf(fn, sizeof(fn), "digits/1F");
                    snprintf(fna, sizeof(fna), "digits/milliard");
                }
                else
                {
                    snprintf(fn, sizeof(fn), "digits/1N");
                    snprintf(fna, sizeof(fna), "digits/h-milliard%s", gender);
                }
            }
            else
            {
                res = lang_specific_de.say_number_full(chan, billions, ints, language, options, audiofd, ctrlfd);
                if (res)
                    return res;
                if (num)
                    snprintf(fn, sizeof(fna), "digits/milliards");
                else
                    snprintf(fn, sizeof(fna), "digits/h-milliard%s", gender);
            }
            t = 1;
        }
        else if (num == INT_MAX)
        {
            snprintf(fn, sizeof(fn), "digits/h-last%s", gender);
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
            if (!res)
            {
                if (strlen(fna) != 0 && !cw_streamfile(chan, fna, language))
                {
                    if ((audiofd > -1) && (ctrlfd > -1))
                    {
                        res = cw_waitstream_full(chan, ints, audiofd, ctrlfd);
                    }
                    else
                    {
                        res = cw_waitstream(chan, ints);
                    }
                }
                cw_stopstream(chan);
                strcpy(fna, "");
            }
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
        res = cw_say_enumeration(chan, tm.tm_mday, ints, lang, (char * ) NULL);
    if (!res)
        res = cw_waitstream(chan, ints);
    if (!res)
    {
        snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream(chan, ints);
    }
    if (!res)
    {
        /* Year */
        int year = tm.tm_year + 1900;
        if (year > 1999)  	/* year 2000 and later */
        {
            res = cw_say_number(chan, year, ints, lang, (char *) NULL);
        }
        else
        {
            if (year < 1100)
            {
                /* I'm not going to handle 1100 and prior */
                /* We'll just be silent on the year, instead of bombing out. */
            }
            else
            {
                /* year 1100 to 1999. will anybody need this?!? */
                snprintf(fn,sizeof(fn), "digits/%d", (year / 100) );
                res = wait_file(chan, ints, fn, lang);
                if (!res)
                {
                    res = wait_file(chan,ints, "digits/hundred", lang);
                    if (!res  &&  year % 100 != 0)
                        res = cw_say_number(chan, (year % 100), ints, lang, (char *) NULL);
                }
            }
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
            /* Month enumerated */
            res = cw_say_enumeration(chan, (tm.tm_mon + 1), ints, lang, "m");
            break;
        case 'd':
        case 'e':
            /* First - Thirtyfirst */
            res = cw_say_enumeration(chan, tm.tm_mday, ints, lang, "m");
            break;
        case 'Y':
            /* Year */
        {
            int year = tm.tm_year + 1900;
            if (year > 1999)  	/* year 2000 and later */
            {
                res = cw_say_number(chan, year, ints, lang, (char *) NULL);
            }
            else
            {
                if (year < 1100)
                {
                    /* I'm not going to handle 1100 and prior */
                    /* We'll just be silent on the year, instead of bombing out. */
                }
                else
                {
                    /* year 1100 to 1999. will anybody need this?!? */
                    /* say 1967 as 'nineteen hundred seven and sixty' */
                    snprintf(nextmsg,sizeof(nextmsg), "digits/%d", (year / 100) );
                    res = wait_file(chan,ints,nextmsg,lang);
                    if (!res)
                    {
                        res = wait_file(chan,ints, "digits/hundred",lang);
                        if (!res && year % 100 != 0)
                        {
                            res = cw_say_number(chan, (year % 100), ints, lang, (char *) NULL);
                        }
                    }
                }
            }
        }
        break;
        case 'I':
        case 'l':
            /* 12-Hour */
            res = wait_file(chan,ints,"digits/oclock",lang);
            if (tm.tm_hour == 0)
                snprintf(nextmsg,sizeof(nextmsg), "digits/12");
            else if (tm.tm_hour > 12)
                snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
            else
                snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour);
            if (!res)
            {
                res = wait_file(chan,ints,nextmsg,lang);
            }
            break;
        case 'H':
        case 'k':
            /* 24-Hour */
            res = wait_file(chan,ints,"digits/oclock",lang);
            if (!res)
            {
                res = cw_say_number(chan, tm.tm_hour, ints, lang, (char *) NULL);
            }
            break;
        case 'M':
            /* Minute */
            if (tm.tm_min > 0 || format[offset+ 1 ] == 'S' )   /* zero 'digits/0' only if seconds follow (kind of a hack) */
            {
                res = cw_say_number(chan, tm.tm_min, ints, lang, "f");
            }
            if ( !res && format[offset + 1] == 'S' )   /* minutes only if seconds follow (kind of a hack) */
            {
                if (tm.tm_min == 1)
                {
                    res = wait_file(chan,ints,"digits/minute",lang);
                }
                else
                {
                    res = wait_file(chan,ints,"digits/minutes",lang);
                }
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
            /* Shorthand for "Today", "Yesterday", or AdBY */
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
                res = cw_say_date_with_format(chan, time, ints, lang, "AdBY", timezone);
            }
        }
        break;
        case 'q':
            /* Shorthand for "" (today), "Yesterday", A (weekday), or AdBY */
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
                res = cw_say_date_with_format(chan, time, ints, lang, "AdBY", timezone);
            }
        }
        break;
        case 'R':
            res = cw_say_date_with_format(chan, time, ints, lang, "HM", timezone);
            break;
        case 'S':
            /* Seconds */
            res = wait_file(chan,ints, "digits/and",lang);
            if (!res)
            {
                res = cw_say_number(chan, tm.tm_sec, ints, lang, "f");
                if (!res)
                {
                    res = wait_file(chan,ints, "digits/seconds",lang);
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

lang_specific_speech_t lang_specific_da =
{
    "da",
    say_number_full,
    say_enumeration_full,
    say_date,
    NULL,
    NULL,
    NULL,
    say_date_with_format
};
