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

/* Syntaxes supported, not really language codes.
      da    - Danish
      de    - German
      en    - English (US)
      en_GB - English (British)
      es    - Spanish, Mexican
      fr    - French
      he    - Hebrew
      it    - Italian
      nl    - Dutch
      no    - Norwegian
      pl    - Polish
      pt    - Portuguese
      se    - Swedish
      tw    - Taiwanese
      ru    - Russian

 Gender:
 For Some languages the numbers differ for gender and plural
 Use the option argument 'f' for female, 'm' for male and 'n' for neuter in languages like Portuguese, French, Spanish and German.
 use the option argument 'c' is for commune and 'n' for neuter gender in nordic languages like Danish, Swedish and Norwegian.
 use the option argument 'p' for plural enumerations like in German

 Date/Time functions currently have less languages supported than saynumber().

 Note that in future, we need to move to a model where we can differentiate further - e.g. between en_US & en_UK

 See contrib/i18n.testsuite.conf for some examples of the different syntaxes

 Portuguese sound files needed for Time/Date functions:
 pt-ah
 pt-ao
 pt-de
 pt-e
 pt-ora
 pt-meianoite
 pt-meiodia
 pt-sss

 Spanish sound files needed for Time/Date functions:
 es-de
 es-el

 Italian sound files needed for Time/Date functions:
 ore-una
 ore-mezzanotte

*/

typedef int (*opbx_say_number_full_t)(struct opbx_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
typedef int (*opbx_say_enumeration_full_t)(struct opbx_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
typedef int (*opbx_say_date_t)(struct opbx_channel *chan, time_t t, const char *ints, const char *lang);
typedef int (*opbx_say_time_t)(struct opbx_channel *chan, time_t t, const char *ints, const char *lang);
typedef int (*opbx_say_datetime_t)(struct opbx_channel *chan, time_t t, const char *ints, const char *lang);
typedef int (*opbx_say_datetime_from_now_t)(struct opbx_channel *chan, time_t t, const char *ints, const char *lang);
typedef int (*opbx_say_date_with_format_t)(struct opbx_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone);

typedef struct
{
    const char tag[5 + 1];
    opbx_say_number_full_t say_number_full;
    opbx_say_enumeration_full_t say_enumeration_full;
    opbx_say_date_t say_date;
    opbx_say_time_t say_time;
    opbx_say_datetime_t say_datetime;
    opbx_say_datetime_from_now_t say_datetime_from_now;
    opbx_say_date_with_format_t say_date_with_format;
} lang_specific_speech_t;

int wait_file(struct opbx_channel *chan, const char *ints, const char *file, const char *lang);

extern lang_specific_speech_t lang_specific_br;
extern lang_specific_speech_t lang_specific_cz;
extern lang_specific_speech_t lang_specific_da;
extern lang_specific_speech_t lang_specific_de;
extern lang_specific_speech_t lang_specific_en;
extern lang_specific_speech_t lang_specific_en_GB;
extern lang_specific_speech_t lang_specific_es;
extern lang_specific_speech_t lang_specific_fr;
extern lang_specific_speech_t lang_specific_gr;
extern lang_specific_speech_t lang_specific_he;
extern lang_specific_speech_t lang_specific_it;
extern lang_specific_speech_t lang_specific_nl;
extern lang_specific_speech_t lang_specific_no;
extern lang_specific_speech_t lang_specific_pl;
extern lang_specific_speech_t lang_specific_pt;
extern lang_specific_speech_t lang_specific_se;
extern lang_specific_speech_t lang_specific_zh_TW;
extern lang_specific_speech_t lang_specific_ru;

#if defined(XYZZY)
lang_specific_speech_t *lang_list[] =
{
    &lang_specific_br,
    &lang_specific_cz,
    &lang_specific_da,
    &lang_specific_de,
    &lang_specific_en,
    &lang_specific_en_GB,
    &lang_specific_es,
    &lang_specific_fr,
    &lang_specific_gr,
    &lang_specific_he,
    &lang_specific_it,
    &lang_specific_nl,
    &lang_specific_no,
    &lang_specific_pl,
    &lang_specific_pt,
    &lang_specific_se,
    &lang_specific_zh_TW,
    &lang_specific_ru,
    NULL
};
#endif
