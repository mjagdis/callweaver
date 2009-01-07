/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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

/*! \file
 * \brief Say numbers and dates (maybe words one day too)
 */

#ifndef _CALLWEAVER_SAY_H
#define _CALLWEAVER_SAY_H

#include "callweaver/channel.h"
#include "callweaver/file.h"

#include <time.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* 
    This is what we will use in the future: 
    a pluggable engine so that languages each language has it's module.
    We don't use the language as it will be grabbed from the channel language.
*/

/*
struct cw_intl_say_engine_s {
    char *lang;                         // The lang we provide. 
    char *name;                         // The name of this implementation
    char *description;                  // A short description

    int (*say_number)(cw_channel_t *chan, int num, const char *break_char);
    int (*say_enumeration)(cw_channel_t *chan, int num, const char *break_char);
    int (*say_digits)(cw_channel_t *chan, int num, const char *break_char);
    int (*say_digit_str)(cw_channel_t *chan, const char *num, const char *break_char);
    int (*say_character_str)(cw_channel_t *chan, const char *num, const char *break_char);
    int (*say_phonetic_str)(cw_channel_t *chan, const char *num, const char *break_char);
    int (*say_datetime)(cw_channel_t *chan, time_t t, const char *break_char);
    int (*say_time)(cw_channel_t *chan, time_t t, const char *break_char);
    int (*say_date)(cw_channel_t *chan, time_t t, const char *break_char);
    int (*say_datetime_from_now)(cw_channel_t *chan, time_t t, const char *break_char);
    int (*_say_date_with_format)(cw_channel_t *chan, time_t t, const char *break_char, const char *format, const char *timezone);
};

typedef struct cw_intl_say_engine_s cw_intl_say_engine_t;

int cw_intl_say_register( cw_intl_say_engine_t *implementation );
int cw_intl_say_unregister( cw_intl_say_engine_t *implementation );

*/



/* says a number
 * \param chan channel to say them number on
 * \param num number to say on the channel
 * \param ints which dtmf to interrupt on
 * \param lang language to speak the number
 * \param options set to 'f' for female, 'm' for male, 'c' for commune, 'n' for neuter, 'p' for plural
 * Vocally says a number on a given channel
 * Returns 0 on success, DTMF digit on interrupt, -1 on failure
 */
extern CW_API_PUBLIC int cw_say_number(struct cw_channel *chan, int num, const char *ints, const char *lang, const char *options);

/* Same as above with audiofd for received audio and returns 1 on ctrlfd being readable */
extern CW_API_PUBLIC int cw_say_number_full(struct cw_channel *chan, int num, const char *ints, const char *lang, const char *options, int audiofd, int ctrlfd);

/* says an enumeration
 * \param chan channel to say them enumeration on
 * \param num number to say on the channel
 * \param ints which dtmf to interrupt on
 * \param lang language to speak the enumeration
 * \param options set to 'f' for female, 'm' for male, 'c' for commune, 'n' for neuter, 'p' for plural
 * Vocally says a enumeration on a given channel (first, sencond, third, forth, thirtyfirst, hundredth, ....) 
 * especially useful for dates and messages. says 'last' if num equals to INT_MAX
 * Returns 0 on success, DTMF digit on interrupt, -1 on failure
 */
extern CW_API_PUBLIC int cw_say_enumeration(struct cw_channel *chan, int num, const char *ints, const char *lang, const char *options);
extern CW_API_PUBLIC int cw_say_enumeration_full(struct cw_channel *chan, int num, const char *ints, const char *lang, const char *options, int audiofd, int ctrlfd);

/* says digits
 * \param chan channel to act upon
 * \param num number to speak
 * \param ints which dtmf to interrupt on
 * \param lang language to speak
 * Vocally says digits of a given number
 * Returns 0 on success, dtmf if interrupted, -1 on failure
 */
extern CW_API_PUBLIC int cw_say_digits(struct cw_channel *chan, int num, const char *ints, const char *lang);
extern CW_API_PUBLIC int cw_say_digits_full(struct cw_channel *chan, int num, const char *ints, const char *lang, int audiofd, int ctrlfd);

/* says digits of a string
 * \param chan channel to act upon
 * \param num string to speak
 * \param ints which dtmf to interrupt on
 * \param lang language to speak in
 * Vocally says the digits of a given string
 * Returns 0 on success, dtmf if interrupted, -1 on failure
 */
extern CW_API_PUBLIC int cw_say_digit_str(struct cw_channel *chan, const char *num, const char *ints, const char *lang);
extern CW_API_PUBLIC int cw_say_digit_str_full(struct cw_channel *chan, const char *num, const char *ints, const char *lang, int audiofd, int ctrlfd);
extern CW_API_PUBLIC int cw_say_character_str(struct cw_channel *chan, const char *num, const char *ints, const char *lang);
extern CW_API_PUBLIC int cw_say_character_str_full(struct cw_channel *chan, const char *num, const char *ints, const char *lang, int audiofd, int ctrlfd);
extern CW_API_PUBLIC int cw_say_phonetic_str(struct cw_channel *chan, const char *num, const char *ints, const char *lang);
extern CW_API_PUBLIC int cw_say_phonetic_str_full(struct cw_channel *chan, const char *num, const char *ints, const char *lang, int audiofd, int ctrlfd);

extern CW_API_PUBLIC int cw_say_datetime(struct cw_channel *chan, time_t t, const char *ints, const char *lang);

extern CW_API_PUBLIC int cw_say_time(struct cw_channel *chan, time_t t, const char *ints, const char *lang);

extern CW_API_PUBLIC int cw_say_date(struct cw_channel *chan, time_t t, const char *ints, const char *lang);

extern CW_API_PUBLIC int cw_say_datetime_from_now(struct cw_channel *chan, time_t t, const char *ints, const char *lang);

extern CW_API_PUBLIC int cw_say_date_with_format(struct cw_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *timezone);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_SAY_H */
