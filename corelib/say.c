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

#define XYZZY

#include "say.h"

/* Forward declaration */
int wait_file(struct cw_channel *chan, const char *ints, const char *file, const char *lang);

int cw_say_character_str_full(struct cw_channel *chan, const char *str, const char *ints, const char *lang, int audiofd, int ctrlfd)
{
    const char *fn;
    char fnbuf[256];
    char ltr;
    int num = 0;
    int res = 0;

    while (str[num])
    {
        fn = NULL;
        switch (str[num])
        {
        case ('*'):
            fn = "digits/star";
            break;
        case ('#'):
            fn = "digits/pound";
            break;
        case ('!'):
            fn = "letters/exclaimation-point";
            break;
        case ('@'):
            fn = "letters/at";
            break;
        case ('$'):
            fn = "letters/dollar";
            break;
        case ('-'):
            fn = "letters/dash";
            break;
        case ('.'):
            fn = "letters/dot";
            break;
        case ('='):
            fn = "letters/equals";
            break;
        case ('+'):
            fn = "letters/plus";
            break;
        case ('/'):
            fn = "letters/slash";
            break;
        case (' '):
            fn = "letters/space";
            break;
        case ('0'):
        case ('1'):
        case ('2'):
        case ('3'):
        case ('4'):
        case ('5'):
        case ('6'):
        case ('7'):
        case ('8'):
        case ('9'):
            strcpy(fnbuf, "digits/X");
            fnbuf[7] = str[num];
            fn = fnbuf;
            break;
        default:
            ltr = str[num];
            if ('A' <= ltr && ltr <= 'Z')
                ltr += 'a' - 'A';		/* file names are all lower-case */
            strcpy(fnbuf, "letters/X");
            fnbuf[8] = ltr;
            fn = fnbuf;
            break;
        }
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream_full(chan, ints, audiofd, ctrlfd);
        cw_stopstream(chan);
        num++;
    }

    return res;
}

int cw_say_character_str(struct cw_channel *chan, const char *str, const char *ints, const char *lang)
{
    return cw_say_character_str_full(chan, str, ints, lang, -1, -1);
}

int cw_say_phonetic_str_full(struct cw_channel *chan, const char *str, const char *ints, const char *lang, int audiofd, int ctrlfd)
{
    const char *fn;
    char fnbuf[256];
    char ltr;
    int num = 0;
    int res = 0;

    while (str[num])
    {
        fn = NULL;
        switch (str[num])
        {
        case ('*'):
            fn = "digits/star";
            break;
        case ('#'):
            fn = "digits/pound";
            break;
        case ('!'):
            fn = "letters/exclaimation-point";
            break;
        case ('@'):
            fn = "letters/at";
            break;
        case ('$'):
            fn = "letters/dollar";
            break;
        case ('-'):
            fn = "letters/dash";
            break;
        case ('.'):
            fn = "letters/dot";
            break;
        case ('='):
            fn = "letters/equals";
            break;
        case ('+'):
            fn = "letters/plus";
            break;
        case ('/'):
            fn = "letters/slash";
            break;
        case (' '):
            fn = "letters/space";
            break;
        case ('0'):
        case ('1'):
        case ('2'):
        case ('3'):
        case ('4'):
        case ('5'):
        case ('6'):
        case ('7'):
        case ('8'):
            strcpy(fnbuf, "digits/X");
            fnbuf[7] = str[num];
            fn = fnbuf;
            break;
        default:	/* '9' falls here... */
            ltr = str[num];
            if ('A' <= ltr && ltr <= 'Z') ltr += 'a' - 'A';		/* file names are all lower-case */
            strcpy(fnbuf, "phonetic/X_p");
            fnbuf[9] = ltr;
            fn = fnbuf;
        }
        res = cw_streamfile(chan, fn, lang);
        if (!res)
            res = cw_waitstream_full(chan, ints, audiofd, ctrlfd);
        cw_stopstream(chan);
        num++;
    }

    return res;
}

int cw_say_phonetic_str(struct cw_channel *chan, const char *str, const char *ints, const char *lang)
{
    return cw_say_phonetic_str_full(chan, str, ints, lang, -1, -1);
}

int cw_say_digit_str_full(struct cw_channel *chan, const char *str, const char *ints, const char *lang, int audiofd, int ctrlfd)
{
    const char *fn;
    char fnbuf[256];
    int num = 0;
    int res = 0;

    while (str[num]  &&  !res)
    {
        fn = NULL;
        switch (str[num])
        {
        case ('*'):
            fn = "digits/star";
            break;
        case ('#'):
            fn = "digits/pound";
            break;
        case ('-'):
            fn = "digits/minus";
            break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            strcpy(fnbuf, "digits/X");
            fnbuf[7] = str[num];
            fn = fnbuf;
            break;
        }
        if (fn)
        {
            res = cw_streamfile(chan, fn, lang);
            if (!res)
                res = cw_waitstream_full(chan, ints, audiofd, ctrlfd);
            cw_stopstream(chan);
        }
        num++;
    }

    return res;
}

int cw_say_digit_str(struct cw_channel *chan, const char *str, const char *ints, const char *lang)
{
    return cw_say_digit_str_full(chan, str, ints, lang, -1, -1);
}

int cw_say_digits_full(struct cw_channel *chan, int num, const char *ints, const char *lang, int audiofd, int ctrlfd)
{
    char fn2[256];

    snprintf(fn2, sizeof(fn2), "%d", num);
    return cw_say_digit_str_full(chan, fn2, ints, lang, audiofd, ctrlfd);
}

int cw_say_digits(struct cw_channel *chan, int num, const char *ints, const char *lang)
{
    return cw_say_digits_full(chan, num, ints, lang, -1, -1);
}

int wait_file(struct cw_channel *chan, const char *ints, const char *file, const char *lang)
{
    int res;

    if ((res = cw_streamfile(chan, file, lang)))
        cw_log(CW_LOG_WARNING, "Unable to play message %s\n", file);
    if (!res)
        res = cw_waitstream(chan, ints);
    return res;
}

/*! \brief  cw_say_number_full: call language-specific functions */
/* Called from OGI */
int cw_say_number_full(struct cw_channel *chan, int num, const char *ints, const char *lang, const char *options, int audiofd, int ctrlfd)
{
    int i;

    for (i = 0;  lang_list[i];  i++)
    {
        if (strcasecmp(lang, lang_list[i]->tag) == 0)
        {
            if (lang_list[i]->say_number_full)
                return lang_list[i]->say_number_full(chan, num, ints, lang, options, audiofd, ctrlfd);
            break;
        }
    }
    /* Default to english */
    return lang_specific_en.say_number_full(chan, num, ints, lang, options, audiofd, ctrlfd);
}

/*! \brief  cw_say_number: call language-specific functions without file descriptors */
int cw_say_number(struct cw_channel *chan, int num, const char *ints, const char *language, const char *options)
{
    return cw_say_number_full(chan, num, ints, language, options, -1, -1);
}

/*! \brief  cw_say_enumeration_full: call language-specific functions */
/* Called from OGI */
int cw_say_enumeration_full(struct cw_channel *chan, int num, const char *ints, const char *lang, const char *options, int audiofd, int ctrlfd)
{
    int i;

    for (i = 0;  lang_list[i];  i++)
    {
        if (strcasecmp(lang, lang_list[i]->tag) == 0)
        {
            if (lang_list[i]->say_enumeration_full)
                return lang_list[i]->say_enumeration_full(chan, num, ints, lang, options, audiofd, ctrlfd);
            break;
        }
    }
    /* Default to english */
    return lang_specific_en.say_enumeration_full(chan, num, ints, lang, options, audiofd, ctrlfd);
}

/*! \brief  cw_say_enumeration: call language-specific functions without file descriptors */
int cw_say_enumeration(struct cw_channel *chan, int num, const char *ints, const char *language, const char *options)
{
    return(cw_say_enumeration_full(chan, num, ints, language, options, -1, -1));
}

int cw_say_date(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    int i;

    for (i = 0;  lang_list[i];  i++)
    {
        if (strcasecmp(lang, lang_list[i]->tag) == 0)
        {
            if (lang_list[i]->say_date)
                return lang_list[i]->say_date(chan, t, ints, lang);
            break;
        }
    }
    /* Default to English */
    return lang_specific_en.say_date(chan, t, ints, lang);
}

int cw_say_date_with_format(struct cw_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
{
    int i;

    for (i = 0;  lang_list[i];  i++)
    {
        if (strcasecmp(lang, lang_list[i]->tag) == 0)
        {
            if (lang_list[i]->say_date_with_format)
                return lang_list[i]->say_date_with_format(chan, time, ints, lang, format, timezone);
            break;
        }
    }
    /* Default to English */
    return lang_specific_en.say_date_with_format(chan, time, ints, lang, format, timezone);
}

int cw_say_time(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    int i;

    for (i = 0;  lang_list[i];  i++)
    {
        if (strcasecmp(lang, lang_list[i]->tag) == 0)
        {
            if (lang_list[i]->say_time(chan, t, ints, lang))
                return lang_list[i]->say_time(chan, t, ints, lang);
            break;
        }
    }
    /* Default to English */
    return lang_specific_en.say_time(chan, t, ints, lang);
}

int cw_say_datetime(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    int i;

    for (i = 0;  lang_list[i];  i++)
    {
        if (strcasecmp(lang, lang_list[i]->tag) == 0)
        {
            if (lang_list[i]->say_datetime)
                return lang_list[i]->say_datetime(chan, t, ints, lang);
            break;
        }
    }
    /* Default to English */
    return lang_specific_en.say_datetime(chan, t, ints, lang);
}

int cw_say_datetime_from_now(struct cw_channel *chan, time_t t, const char *ints, const char *lang)
{
    int i;

    for (i = 0;  lang_list[i];  i++)
    {
        if (strcasecmp(lang, lang_list[i]->tag) == 0)
        {
            if (lang_list[i]->say_datetime_from_now)
                return lang_list[i]->say_datetime_from_now(chan, t, ints, lang);
            break;
        }
    }
    /* Default to English */
    return lang_specific_en.say_datetime_from_now(chan, t, ints, lang);
}
