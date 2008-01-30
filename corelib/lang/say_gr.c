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

/*
 * digits/female-[1..4] : "Mia, dyo , treis, tessereis"
 */
static int say_number_female(int num, struct cw_channel *chan, const char *ints, const char *lang)
{
    int tmp;
    int left;
    int res;
    char fn[256] = "";

    /* cw_log(CW_LOG_DEBUG, "\n\n Saying number female %s %d \n\n",lang, num); */
    if (num < 5)
    {
        snprintf(fn, sizeof(fn), "digits/female-%d", num);
        res = wait_file(chan, ints, fn, lang);
    }
    else if (num < 13)
    {
        res = cw_say_number(chan, num, ints, lang, (char *) NULL);
    }
    else if (num <100 )
    {
        tmp = (num/10) * 10;
        left = num - tmp;
        snprintf(fn, sizeof(fn), "digits/%d", tmp);
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream(chan, ints);
        if (left)
            say_number_female(left, chan, ints, lang);

    }
    else
    {
        return -1;
    }
    return res;
}

/*
 *  	A list of the files that you need to create
 ->  	digits/xilia = "xilia"
 ->  	digits/myrio = "ekatomyrio"
 ->  	digits/thousands = "xiliades"
 ->  	digits/millions = "ektatomyria"
 ->  	digits/[1..12]   :: A pronunciation of th digits form 1 to 12 e.g. "tria"
 ->  	digits/[10..100]  :: A pronunciation of the tens from 10 to 90
															 e,g 80 = "ogdonta"
						 Here we must note that we use digits/tens/100 to utter "ekato"
						 and digits/hundred-100 to utter "ekaton"
 ->  	digits/hundred-[100...1000] :: A pronunciation of  hundreds from 100 to 1000 e.g 400 =
																		 "terakosia". Here again we use hundreds/1000 for "xilia"
						 and digits/thousnds for "xiliades"
*/

static int say_number_full(struct cw_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
    int res = 0;
    char fn[256] = "";
    int i = 0;

    if (!num)
    {
        snprintf(fn, sizeof(fn), "digits/0");
        res = cw_streamfile(chan, fn, chan->language);
        if (!res)
            return  cw_waitstream(chan, ints);
    }

    while (!res  &&  num)
    {
        i++;
        if (num < 13)
        {
            snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num <= 100)
        {
            /* 13 < num <= 100  */
            snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
            num -= ((num / 10) * 10);
        }
        else if (num < 200)
        {
            /* 100 < num < 200 */
            snprintf(fn, sizeof(fn), "digits/hundred-100");
            num -= ((num / 100) * 100);
        }
        else if (num < 1000)
        {
            /* 200 < num < 1000 */
            snprintf(fn, sizeof(fn), "digits/hundred-%d", (num/100)*100);
            num -= ((num / 100) * 100);
        }
        else if (num < 2000)
        {
            snprintf(fn, sizeof(fn), "digits/xilia");
            num -= ((num / 1000) * 1000);
        }
        else
        {
            /* num >  1000 */
            if (num < 1000000)
            {
                res = say_number_full(chan, (num / 1000), ints, chan->language, "", audiofd, ctrlfd);
                if (res)
                    return res;
                num = num % 1000;
                snprintf(fn, sizeof(fn), "digits/thousands");
            }
            else
            {
                if (num < 1000000000)   /* 1,000,000,000 */
                {
                    res = say_number_full(chan, (num / 1000000), ints, chan->language, "", audiofd, ctrlfd);
                    if (res)
                        return res;
                    num = num % 1000000;
                    snprintf(fn, sizeof(fn), "digits/millions");
                }
                else
                {
                    cw_log(CW_LOG_DEBUG, "Number '%d' is too big for me\n", num);
                    res = -1;
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

/*
 * The format is  weekday - day - month -year
 *
 * A list of the files that you need to create
 * digits/day-[1..7]  : "Deytera .. Paraskeyh"
 * digits/months/1..12 : "Ianouariou .. Dekembriou"
													Attention the months are in
				"gekinh klhsh"
 */

static int say_date(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    struct tm tm;

    char fn[256];
    int res = 0;


    cw_localtime(&t,&tm,NULL);
    /* W E E K - D A Y */
    if (!res)
    {
        snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream(chan, ints);
    }
    /* D A Y */
    if (!res)
    {
        say_number_female(tm.tm_mday, chan, ints, lang);
    }
    /* M O N T H */
    if (!res)
    {
        snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream(chan, ints);
    }
    /* Y E A R */
    if (!res)
        res = cw_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
    return res;
}

/* A list of the files that you need to create
 * digits/female/1..4 : "Mia, dyo , treis, tesseris "
 * digits/kai : "KAI"
 * didgits : "h wra"
 * digits/p-m : "meta meshmbrias"
 * digits/a-m : "pro meshmbrias"
 */

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

    res = say_number_female(hour, chan, ints, lang);
    if (tm.tm_min)
    {
        if (!res)
            res = cw_streamfile(chan, "digits/kai", lang);
        if (!res)
            res = cw_waitstream(chan, ints);
        if (!res)
            res = cw_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
    }
    else
    {
        if (!res)
            res = cw_streamfile(chan, "digits/hwra", lang);
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
    localtime_r(&t,&tm);

    /* W E E K - D A Y */
    if (!res)
    {
        snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream(chan, ints);
    }
    /* D A Y */
    if (!res)
    {
        say_number_female(tm.tm_mday, chan, ints, lang);
    }
    /* M O N T H */
    if (!res)
    {
        snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream(chan, ints);
    }

    res = say_time(chan, t, ints, lang);
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
        case 'd':
        case 'e':
            /* first - thirtyfirst */
            say_number_female(tm.tm_mday, chan, ints, lang);
            break;
        case 'Y':
            /* Year */
            say_number_full(chan, 1900+tm.tm_year, ints, chan->language, "", -1, -1);
            break;
        case 'I':
        case 'l':
            /* 12-Hour */
            if (tm.tm_hour == 0)
                say_number_female(12, chan, ints, lang);
            else if (tm.tm_hour > 12)
                say_number_female(tm.tm_hour - 12, chan, ints, lang);
            else
                say_number_female(tm.tm_hour, chan, ints, lang);
            break;
        case 'H':
        case 'k':
            /* 24-Hour */
            say_number_female(tm.tm_hour, chan, ints, lang);
            break;
        case 'M':
            /* Minute */
            if (tm.tm_min)
            {
                if (!res)
                    res = cw_streamfile(chan, "digits/kai", lang);
                if (!res)
                    res = cw_waitstream(chan, ints);
                if (!res)
                    res = say_number_full(chan, tm.tm_min, ints, lang, "", -1, -1);
            }
            else
            {
                if (!res)
                    res = cw_streamfile(chan, "digits/oclock", lang);
                if (!res)
                    res = cw_waitstream(chan, ints);
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
                res = cw_say_date_with_format(chan, time, ints, lang, "AdBY", timezone);
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
                res = cw_say_date_with_format(chan, time, ints, lang, "AdBY", timezone);
            }
        }
            break;
        case 'R':
            res = cw_say_date_with_format(chan, time, ints, lang, "HM", timezone);
            break;
        case 'S':
            /* Seconds */
            snprintf(nextmsg,sizeof(nextmsg), "digits/kai");
            res = wait_file(chan,ints,nextmsg,lang);
            if (!res)
                res = say_number_full(chan, tm.tm_sec, ints, lang, "", -1, -1);
            if (!res)
                snprintf(nextmsg,sizeof(nextmsg), "digits/seconds");
            res = wait_file(chan,ints,nextmsg,lang);
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

lang_specific_speech_t lang_specific_gr =
{
    "gr",
    say_number_full,
    NULL,
    say_date,
    say_time,
    say_datetime,
    NULL,
    say_date_with_format
};
