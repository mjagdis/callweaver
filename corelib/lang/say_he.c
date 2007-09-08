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

/* 	Extra sounds needed:
 	1F: feminin 'one'
	ve: 'and'
	2hundred: 2 hundred
	2thousands: 2 thousand
	thousands: plural of 'thousand'
	3sF 'Smichut forms (female)
	4sF
	5sF
	6sF
	7sF
	8sF
	9sF
	3s 'Smichut' forms (male)
	4s
	5s
	6s
	7s
	9s
	10s
	11s
	12s
	13s
	14s
	15s
	16s
	17s
	18s
	19s

TODO: 've' should sometimed be 'hu':
* before 'shtaym' (2, F)
* before 'shnaym' (2, M)
* before 'shlosha' (3, M)
* before 'shmone' (8, M)
* before 'shlosim' (30)
* before 'shmonim' (80)

What about:
'sheva' (7, F)?
'tesha' (9, F)?
*/
#define SAY_NUM_BUF_SIZE 256
static int say_number_full(struct opbx_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
    int res = 0;
    int state = 0; /* no need to save anything */
    int mf = 1;    /* +1 = Masculin; -1 = Feminin */
    char fn[SAY_NUM_BUF_SIZE] = "";

    opbx_verbose(VERBOSE_PREFIX_3 "opbx_say_digits_full: started. "
                 "num: %d, options=\"%s\"\n",
                 num, options);
    if (!num)
        return opbx_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

    if (options && !strncasecmp(options, "f", 1))
        mf = -1;

    /* Do we have work to do? */
    while (!res  &&  (num  ||  (state > 0)))
    {
        /* first type of work: play a second sound. In this loop
         * we can only play one sound file at a time. Thus playing
         * a second one requires repeating the loop just for the
         * second file. The variable 'state' remembers where we were.
         * state==0 is the normal mode and it means that we continue
         * to check if the number num has yet anything left.
         */
        opbx_verbose(VERBOSE_PREFIX_3 "opbx_say_digits_full: num: %d, "
                     "state=%d, options=\"%s\", mf=%d\n",
                     num, state, options, mf);
        if (state == 1)
        {
            snprintf(fn, sizeof(fn), "digits/hundred");
            state = 0;
        }
        else if (state == 2)
        {
            snprintf(fn, sizeof(fn), "digits/ve");
            state = 0;
        }
        else if (state == 3)
        {
            snprintf(fn, sizeof(fn), "digits/thousands");
            state=0;
        }
        else if (num < 21)
        {
            if (mf < 0)
                snprintf(fn, sizeof(fn), "digits/%dF", num);
            else
                snprintf(fn, sizeof(fn), "digits/%d", num);
            num = 0;
        }
        else if (num < 100)
        {
            snprintf(fn, sizeof(fn), "digits/%d", (num/10)*10);
            num = num % 10;
            if (num>0) state=2;
        }
        else if (num < 200)
        {
            snprintf(fn, sizeof(fn), "digits/hundred");
            num = num - 100;
        }
        else if (num < 300)
        {
            snprintf(fn, sizeof(fn), "digits/hundred");
            num = num - 100;
        }
        else if (num < 1000)
        {
            snprintf(fn, sizeof(fn), "digits/%d", (num/100));
            state=1;
            num = num % 100;
        }
        else if (num < 2000)
        {
            snprintf(fn, sizeof(fn), "digits/thousand");
            num = num - 1000;
        }
        else if (num < 3000)
        {
            snprintf(fn, sizeof(fn), "digits/2thousand");
            num = num - 2000;
            if (num > 0)
                state = 2;
        }
        else if (num < 20000)
        {
            snprintf(fn, sizeof(fn), "digits/%ds",(num/1000));
            num = num % 1000;
            state=3;
        }
        else if (num < 1000000)
        {
            res = say_number_full(chan, num / 1000, ints, language, options, audiofd, ctrlfd);
            if (res)
                return res;
            snprintf(fn, sizeof(fn), "digits/thousand");
            num = num % 1000;
        }
        else if (num < 1000000000)
        {
            res = say_number_full(chan, num / 1000000, ints, language, options, audiofd, ctrlfd);
            if (res)
                return res;
            snprintf(fn, sizeof(fn), "digits/million");
            num = num % 1000000;
        }
        else
        {
            opbx_log(OPBX_LOG_DEBUG, "Number '%d' is too big for me\n", num);
            res = -1;
        }
        if (!res)
        {
            if (!opbx_streamfile(chan, fn, language))
            {
                if ((audiofd > -1)  &&  (ctrlfd > -1))
                    res = opbx_waitstream_full(chan, ints, audiofd, ctrlfd);
                else
                    res = opbx_waitstream(chan, ints);
            }
            opbx_stopstream(chan);
        }
    }
    return res;
}

/* TODO: this probably is not the correct format for doxygen remarks */

/*
 *
 * @seealso opbx_say_date_with_format_en for the details of the options
 *
 * Changes from the English version:
 *
 * * don't replicate in here the logic of say_number_full
 *
 * * year is always 4-digit (because it's simpler)
 *
 * * added c, x, and X. Mainly for my tests
 *
 * * The standard "long" format used in Hebrew is AdBY, rather than ABdY
 *
 * TODO:
 * * A "ha" is missing in the standard date format, before the 'd'.
 * * The numbers of 3000--19000 are not handled well
 **/
#define IL_DATE_STR "AdBY"
#define IL_TIME_STR "IMp"
#define IL_DATE_STR_FULL IL_DATE_STR " 'digits/at' " IL_TIME_STR
static int say_date_with_format(struct opbx_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
{
    /* TODO: This whole function is cut&paste from
     * opbx_say_date_with_format_en . Is that considered acceptable?
     **/
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
        case 'e': /* Day of the month */
            /* I'm not sure exactly what the parameters
             * audiofd and ctrlfd to
             * say_number_full mean, but it seems
             * safe to pass -1 there.
             *
             * At least in one of the pathes :-(
             */
            res = say_number_full(chan, tm.tm_mday, ints, lang, "m", -1, -1);
            break;
        case 'Y': /* Year */
            res = say_number_full(chan, tm.tm_year+1900, ints, lang, "f", -1, -1);
            break;
        case 'I':
        case 'l': /* 12-Hour */
        {
            int hour = tm.tm_hour;
            hour = hour%12;
            if (hour == 0)
                hour = 12;

            res = say_number_full(chan, hour, ints, lang, "f", -1, -1);
        }
        break;
        case 'H':
        case 'k': /* 24-Hour */
            /* With 'H' there is an 'oh' after a single-
             * digit hour */
            if ((format[offset] == 'H') &&
                    (tm.tm_hour <10)&&(tm.tm_hour>0)
               )   /* e.g. oh-eight */
            {
                res = wait_file(chan,ints, "digits/oh",lang);
            }

            res = say_number_full(chan, tm.tm_hour, ints, lang, "f", -1, -1);
            break;
        case 'M': /* Minute */
            res = say_number_full(chan, tm.tm_min, ints, lang, "f", -1, -1);
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
            /* Shorthand for "Today", "Yesterday", or "date" */
        case 'q':
            /* Shorthand for "" (today), "Yesterday", A
                             * (weekday), or "date" */
        {
            struct timeval now;
            struct tm tmnow;
            time_t beg_today;
            char todo = format[offset]; /* The letter to format*/

            gettimeofday(&now,NULL);
            opbx_localtime(&now.tv_sec,&tmnow,timezone);
            /* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
            /* In any case, it saves not having to do opbx_mktime() */
            beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
            if (beg_today < time)
            {
                /* Today */
                if (todo == 'Q')
                {
                    res = wait_file(chan,
                                    ints,
                                    "digits/today",
                                    lang);
                }
            }
            else if (beg_today - 86400 < time)
            {
                /* Yesterday */
                res = wait_file(chan,ints, "digits/yesterday",lang);
            }
            else if ((todo != 'Q') &&
                     (beg_today - 86400 * 6 < time))
            {
                /* Within the last week */
                res = say_date_with_format(chan, time, ints, lang, "A", timezone);
            }
            else
            {
                res = say_date_with_format(chan, time, ints, lang, IL_DATE_STR, timezone);
            }
        }
            break;
        case 'R':
            res = say_date_with_format(chan, time, ints, lang, "HM", timezone);
            break;
        case 'S': /* Seconds */
            res = say_number_full(chan, tm.tm_sec, ints, lang, "f", -1, -1);
            break;
        case 'T':
            res = say_date_with_format(chan, time, ints, lang, "HMS", timezone);
            break;
            /* c, x, and X seem useful for testing. Not sure
                         * if thiey're good for the general public */
        case 'c':
            res = say_date_with_format(chan, time, ints, lang, IL_DATE_STR_FULL, timezone);
            break;
        case 'x':
            res = say_date_with_format(chan, time, ints, lang, IL_DATE_STR, timezone);
            break;
        case 'X': /* Currently not locale-dependent...*/
            res = say_date_with_format(chan, time, ints, lang, IL_TIME_STR, timezone);
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
        {
            break;
        }
    }
    return res;
}

lang_specific_speech_t lang_specific_he =
{
    "he",
    say_number_full,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    say_date_with_format
};
