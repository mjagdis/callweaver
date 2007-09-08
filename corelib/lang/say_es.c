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
 Requires a few new audios:
   1F.gsm: feminine 'una'
   21.gsm thru 29.gsm, cien.gsm, mil.gsm, millon.gsm, millones.gsm, 100.gsm, 200.gsm, 300.gsm, 400.gsm, 500.gsm, 600.gsm, 700.gsm, 800.gsm, 900.gsm, y.gsm
 */
static int say_number_full(struct opbx_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
    int res = 0;
    int playa = 0;
    int mf = 0;                            /* +1 = male; -1 = female */
    char fn[256] = "";

    if (!num)
        return opbx_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

    if (options)
    {
        if (!strncasecmp(options, "f", 1))
            mf = -1;
        else if (!strncasecmp(options, "m", 1))
            mf = 1;
    }

    while (!res  &&  num)
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
        else if (playa)
        {
            snprintf(fn, sizeof(fn), "digits/and");
            playa = 0;
        }
        else if (num == 1)
        {
            if (mf < 0)
                snprintf(fn, sizeof(fn), "digits/%dF", num);
            else if (mf > 0)
                snprintf(fn, sizeof(fn), "digits/%dM", num);
            else
                snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num < 31)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num < 100)
        {
            snprintf(fn, sizeof(fn), "digits/%d", (num/10)*10);
            num -= ((num/10)*10);
            if (num)
                playa++;
        }
        else if (num == 100)
        {
            snprintf(fn, sizeof(fn), "digits/100");
            num = 0;
        }
        else if (num < 200)
        {
            snprintf(fn, sizeof(fn), "digits/100-and");
            num -= 100;
        }
        else
        {
            if (num < 1000)
            {
                snprintf(fn, sizeof(fn), "digits/%d", (num/100)*100);
                num -= ((num/100)*100);
            }
            else if (num < 2000)
            {
                num = num % 1000;
                snprintf(fn, sizeof(fn), "digits/thousand");
            }
            else
            {
                if (num < 1000000)
                {
                    res = say_number_full(chan, num / 1000, ints, language, options, audiofd, ctrlfd);
                    if (res)
                        return res;
                    num %= 1000;
                    snprintf(fn, sizeof(fn), "digits/thousand");
                }
                else
                {
                    if (num < 2147483640)
                    {
                        if ((num/1000000) == 1)
                        {
                            res = say_number_full(chan, num / 1000000, ints, language, "M", audiofd, ctrlfd);
                            if (res)
                                return res;
                            snprintf(fn, sizeof(fn), "digits/million");
                        }
                        else
                        {
                            res = say_number_full(chan, num / 1000000, ints, language, options, audiofd, ctrlfd);
                            if (res)
                                return res;
                            snprintf(fn, sizeof(fn), "digits/millions");
                        }
                        num = num % 1000000;
                    }
                    else
                    {
                        opbx_log(OPBX_LOG_DEBUG, "Number '%d' is too big for me\n", num);
                        res = -1;
                    }
                }
            }
        }

        if (!res)
        {
            if (!opbx_streamfile(chan, fn, language))
            {
                if ((audiofd > -1) && (ctrlfd > -1))
                    res = opbx_waitstream_full(chan, ints, audiofd, ctrlfd);
                else
                    res = opbx_waitstream(chan, ints);
            }
            opbx_stopstream(chan);

        }

    }
    return res;
}

static int say_date_with_format(struct opbx_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
{
    struct tm tm;
    int res = 0;
    int offset;
    int sndoffset;
    char sndfile[256];
    char nextmsg[256];

    opbx_localtime(&time, &tm, timezone);

    for (offset = 0;  format[offset] != '\0';  offset++)
    {
        opbx_log(OPBX_LOG_DEBUG, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
        switch (format[offset])
        {
            /* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
        case '\'':
            /* Literal name of a sound file */
            sndoffset=0;
            for (sndoffset=0 ; (format[++offset] != '\'') && (sndoffset < 256) ; sndoffset++)
                sndfile[sndoffset] = format[offset];
            sndfile[sndoffset] = '\0';
            snprintf(nextmsg,sizeof(nextmsg), "%s", sndfile);
            res = wait_file(chan,ints,nextmsg,lang);
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
            /* First - Thirtyfirst */
            res = opbx_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
            break;
        case 'Y':
            /* Year */
            res = opbx_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
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
            res = opbx_say_number(chan, tm.tm_hour, ints, lang, NULL);
            break;
        case 'M':
            /* Minute */
            res = opbx_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
            break;
        case 'P':
        case 'p':
            /* AM/PM */
            if (tm.tm_hour > 12)
                res = wait_file(chan, ints, "digits/p-m", lang);
            else if (tm.tm_hour  && tm.tm_hour < 12)
                res = wait_file(chan, ints, "digits/a-m", lang);
            break;
        case 'Q':
            /* Shorthand for "Today", "Yesterday", or ABdY */
        {
            struct timeval now;
            struct tm tmnow;
            time_t beg_today;

            gettimeofday(&now,NULL);
            opbx_localtime(&now.tv_sec,&tmnow,timezone);
            /* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
            /* In any case, it saves not having to do opbx_mktime() */
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
                res = opbx_say_date_with_format(chan, time, ints, lang, "'digits/es-el' Ad 'digits/es-de' B 'digits/es-de' Y", timezone);
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
            opbx_localtime(&now.tv_sec,&tmnow,timezone);
            /* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
            /* In any case, it saves not having to do opbx_mktime() */
            beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
            if (beg_today < time)
            {
                /* Today */
                res = wait_file(chan,ints, "digits/today",lang);
            }
            else if ((beg_today - 86400) < time)
            {
                /* Yesterday */
                res = wait_file(chan,ints, "digits/yesterday",lang);
            }
            else if (beg_today - 86400 * 6 < time)
            {
                /* Within the last week */
                res = opbx_say_date_with_format(chan, time, ints, lang, "A", timezone);
            }
            else
            {
                res = opbx_say_date_with_format(chan, time, ints, lang, "'digits/es-el' Ad 'digits/es-de' B 'digits/es-de' Y", timezone);
            }
        }
            break;
        case 'R':
            res = opbx_say_date_with_format(chan, time, ints, lang, "H 'digits/y' M", timezone);
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
            res = opbx_say_date_with_format(chan, time, ints, lang, "HMS", timezone);
            break;
        case ' ':
        case '	':
            /* Just ignore spaces and tabs */
            break;
        default:
            /* Unknown character */
            opbx_log(OPBX_LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
        }
        /* Jump out on DTMF */
        if (res)
            break;
    }
    return res;
}

lang_specific_speech_t lang_specific_es =
{
    "es",
    say_number_full,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    say_date_with_format
};
