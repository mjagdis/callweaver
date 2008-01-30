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

typedef struct
{
    char *separator_dziesiatek;
    char *cyfry[10];
    char *cyfry2[10];
    char *setki[10];
    char *dziesiatki[10];
    char *nastki[10];
    char *rzedy[3][3];
} odmiana;

static char *pl_rzad_na_tekst(odmiana *odm, int i, int rzad)
{
    if (rzad==0)
        return "";

    if (i==1)
        return odm->rzedy[rzad - 1][0];
    if ((i > 21 || i < 11) &&  i%10 > 1 && i%10 < 5)
        return odm->rzedy[rzad - 1][1];
    else
        return odm->rzedy[rzad - 1][2];
}

static char *pl_append(char* buffer, char* str)
{
    strcpy(buffer, str);
    buffer += strlen(str);
    return buffer;
}

static void pl_odtworz_plik(struct cw_channel *chan, const char *language, int audiofd, int ctrlfd, const char *ints, char *fn)
{
    char file_name[255] = "digits/";
    strcat(file_name, fn);
    cw_log(CW_LOG_DEBUG, "Trying to play: %s\n", file_name);
    if (!cw_streamfile(chan, file_name, language))
    {
        if ((audiofd > -1) && (ctrlfd > -1))
            cw_waitstream_full(chan, ints, audiofd, ctrlfd);
        else
            cw_waitstream(chan, ints);
    }
    cw_stopstream(chan);
}

static void powiedz(struct cw_channel *chan, const char *language, int audiofd, int ctrlfd, const char *ints, odmiana *odm, int rzad, int i)
{
    /* Initialise variables to allow compilation on Debian-stable, etc */
    int m1000E6 = 0;
    int i1000E6 = 0;
    int m1000E3 = 0;
    int i1000E3 = 0;
    int m1000 = 0;
    int i1000 = 0;
    int m100 = 0;
    int i100 = 0;

    if (i == 0 && rzad > 0)
    {
        return;
    }
    if (i == 0)
    {
        pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->cyfry[0]);
    }

    m1000E6 = i % 1000000000;
    i1000E6 = i / 1000000000;

    powiedz(chan, language, audiofd, ctrlfd, ints, odm, rzad+3, i1000E6);

    m1000E3 = m1000E6 % 1000000;
    i1000E3 = m1000E6 / 1000000;

    powiedz(chan, language, audiofd, ctrlfd, ints, odm, rzad+2, i1000E3);

    m1000 = m1000E3 % 1000;
    i1000 = m1000E3 / 1000;

    powiedz(chan, language, audiofd, ctrlfd, ints, odm, rzad+1, i1000);

    m100 = m1000 % 100;
    i100 = m1000 / 100;

    if (i100 > 0)
        pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->setki[i100]);

    if (m100 > 0  &&  m100 <= 9)
    {
        if (m1000>0)
            pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->cyfry2[m100]);
        else
            pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->cyfry[m100]);
    }
    else if (m100 % 10 == 0)
    {
        pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->dziesiatki[m100 / 10]);
    }
    else if (m100 <= 19)
    {
        pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->nastki[m100 % 10]);
    }
    else if (m100 != 0)
    {
        if (odm->separator_dziesiatek[0] == ' ')
        {
            pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->dziesiatki[m100 / 10]);
            pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->cyfry2[m100 % 10]);
        }
        else
        {
            char buf[10];
            char *b = buf;

            b = pl_append(b, odm->dziesiatki[m100 / 10]);
            b = pl_append(b, odm->separator_dziesiatek);
            b = pl_append(b, odm->cyfry2[m100 % 10]);
            pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, buf);
        }
    }

    if (rzad > 0)
        pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, pl_rzad_na_tekst(odm, i, rzad));
}

static int say_number_full(struct cw_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
/*
Sounds needed:
0		zero
1		jeden
10		dziesiec
100		sto
1000		tysiac
1000000		milion
1000000000	miliard
1000000000.2	miliardy
1000000000.5	miliardow
1000000.2	miliony
1000000.5	milionow
1000.2		tysiace
1000.5		tysiecy
100m		stu
10m		dziesieciu
11		jedenascie
11m		jedenastu
12		dwanascie
12m		dwunastu
13		trzynascie
13m		trzynastu
14		czternascie
14m		czternastu
15		pietnascie
15m		pietnastu
16		szesnascie
16m		szesnastu
17		siedemnascie
17m		siedemnastu
18		osiemnascie
18m		osiemnastu
19		dziewietnascie
19m		dziewietnastu
1z		jedna
2		dwie
20		dwadziescia
200		dwiescie
200m		dwustu
20m		dwudziestu
2-1m		dwaj
2-2m		dwoch
2z		dwie
3		trzy
30		trzydziesci
300		trzysta
300m		trzystu
30m		trzydziestu
3-1m		trzej
3-2m		trzech
4		cztery
40		czterdziesci
400		czterysta
400m		czterystu
40m		czterdziestu
4-1m		czterej
4-2m		czterech
5		piec
50		piecdziesiat
500		piecset
500m		pieciuset
50m		piedziesieciu
5m		pieciu
6		szesc
60		szescdziesiat
600		szescset
600m		szesciuset
60m		szescdziesieciu
6m		szesciu
7		siedem
70		siedemdziesiat
700		siedemset
700m		siedmiuset
70m		siedemdziesieciu
7m		siedmiu
8		osiem
80		osiemdziesiat
800		osiemset
800m		osmiuset
80m		osiemdziesieciu
8m		osmiu
9		dziewiec
90		dziewiecdziesiat
900		dziewiecset
900m		dziewieciuset
90m		dziewiedziesieciu
9m		dziewieciu
and combinations of eg.: 20_1, 30m_3m, etc...

*/
{
    char *zenski_cyfry[] = {"0","1z", "2z", "3", "4", "5", "6", "7", "8", "9"};

    char *zenski_cyfry2[] = {"0","1", "2z", "3", "4", "5", "6", "7", "8", "9"};

    char *meski_cyfry[] = {"0","1", "2-1m", "3-1m", "4-1m", "5m",  /*"2-1mdwaj"*/ "6m", "7m", "8m", "9m"};

    char *meski_cyfry2[] = {"0","1", "2-2m", "3-2m", "4-2m", "5m", "6m", "7m", "8m", "9m"};

    char *meski_setki[] = {"", "100m", "200m", "300m", "400m", "500m", "600m", "700m", "800m", "900m"};

    char *meski_dziesiatki[] = {"", "10m", "20m", "30m", "40m", "50m", "60m", "70m", "80m", "90m"};

    char *meski_nastki[] = {"", "11m", "12m", "13m", "14m", "15m", "16m", "17m", "18m", "19m"};

    char *nijaki_cyfry[] = {"0","1", "2", "3", "4", "5", "6", "7", "8", "9"};

    char *nijaki_cyfry2[] = {"0","1", "2", "3", "4", "5", "6", "7", "8", "9"};

    char *nijaki_setki[] = {"", "100", "200", "300", "400", "500", "600", "700", "800", "900"};

    char *nijaki_dziesiatki[] = {"", "10", "20", "30", "40", "50", "60", "70", "80", "90"};

    char *nijaki_nastki[] = {"", "11", "12", "13", "14", "15", "16", "17", "18", "19"};

    char *rzedy[][3] = { {"1000", "1000.2", "1000.5"}, {"1000000", "1000000.2", "1000000.5"}, {"1000000000", "1000000000.2", "1000000000.5"}};

    /* Initialise variables to allow compilation on Debian-stable, etc */
    odmiana *o;

    static odmiana *odmiana_nieosobowa = NULL;
    static odmiana *odmiana_meska = NULL;
    static odmiana *odmiana_zenska = NULL;

    if (odmiana_nieosobowa == NULL)
    {
        odmiana_nieosobowa = (odmiana *) malloc(sizeof(odmiana));

        odmiana_nieosobowa->separator_dziesiatek = "_";

        memcpy(odmiana_nieosobowa->cyfry, nijaki_cyfry, sizeof(odmiana_nieosobowa->cyfry));
        memcpy(odmiana_nieosobowa->cyfry2, nijaki_cyfry2, sizeof(odmiana_nieosobowa->cyfry));
        memcpy(odmiana_nieosobowa->setki, nijaki_setki, sizeof(odmiana_nieosobowa->setki));
        memcpy(odmiana_nieosobowa->dziesiatki, nijaki_dziesiatki, sizeof(odmiana_nieosobowa->dziesiatki));
        memcpy(odmiana_nieosobowa->nastki, nijaki_nastki, sizeof(odmiana_nieosobowa->nastki));
        memcpy(odmiana_nieosobowa->rzedy, rzedy, sizeof(odmiana_nieosobowa->rzedy));
    }

    if (odmiana_zenska == NULL)
    {
        odmiana_zenska = (odmiana *) malloc(sizeof(odmiana));

        odmiana_zenska->separator_dziesiatek = " ";

        memcpy(odmiana_zenska->cyfry, zenski_cyfry, sizeof(odmiana_zenska->cyfry));
        memcpy(odmiana_zenska->cyfry2, zenski_cyfry2, sizeof(odmiana_zenska->cyfry));
        memcpy(odmiana_zenska->setki, nijaki_setki, sizeof(odmiana_zenska->setki));
        memcpy(odmiana_zenska->dziesiatki, nijaki_dziesiatki, sizeof(odmiana_zenska->dziesiatki));
        memcpy(odmiana_zenska->nastki, nijaki_nastki, sizeof(odmiana_zenska->nastki));
        memcpy(odmiana_zenska->rzedy, rzedy, sizeof(odmiana_zenska->rzedy));
    }

    if (odmiana_meska == NULL)
    {
        odmiana_meska = (odmiana *) malloc(sizeof(odmiana));

        odmiana_meska->separator_dziesiatek = " ";

        memcpy(odmiana_meska->cyfry, meski_cyfry, sizeof(odmiana_meska->cyfry));
        memcpy(odmiana_meska->cyfry2, meski_cyfry2, sizeof(odmiana_meska->cyfry));
        memcpy(odmiana_meska->setki, meski_setki, sizeof(odmiana_meska->setki));
        memcpy(odmiana_meska->dziesiatki, meski_dziesiatki, sizeof(odmiana_meska->dziesiatki));
        memcpy(odmiana_meska->nastki, meski_nastki, sizeof(odmiana_meska->nastki));
        memcpy(odmiana_meska->rzedy, rzedy, sizeof(odmiana_meska->rzedy));
    }

    if (options)
    {
        if (strncasecmp(options, "f", 1) == 0)
            o = odmiana_zenska;
        else if (strncasecmp(options, "m", 1) == 0)
            o = odmiana_meska;
        else
            o = odmiana_nieosobowa;
    }
    else
    {
        o = odmiana_nieosobowa;
    }
    powiedz(chan, language, audiofd, ctrlfd, ints, o, 0, num);
    return 0;
}

lang_specific_speech_t lang_specific_pl =
{
    "pl",
    say_number_full,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
