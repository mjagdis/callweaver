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
 *
 * \brief Tone detection routines
 *
 */
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdio.h>
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>

#include <spandsp/expose.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/frame.h"
#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/dsp.h"

#include "core/ulaw.h"
#include "core/alaw.h"

/* Number of goertzels for progress detect */
#define GSAMP_SIZE_NA       183     /* North America - 350, 440, 480, 620, 950, 1400, 1800 Hz */
#define GSAMP_SIZE_CR       188     /* Costa Rica, Brazil - Only care about 425 Hz */
#define GSAMP_SIZE_UK       160     /* UK disconnect goertzel feed - should trigger 400Hz */

#define PROG_MODE_NA        0
#define PROG_MODE_CR        1
#define PROG_MODE_UK        2

/* For US modes */
#define HZ_350  0
#define HZ_440  1
#define HZ_480  2
#define HZ_620  3
#define HZ_950  4
#define HZ_1400 5
#define HZ_1800 6

/* For CR/BR modes */
#define HZ_425  0

/* For UK mode */
#define HZ_400  0

static struct progalias
{
    char *name;
    int mode;
} aliases[] =
{
    { "us", PROG_MODE_NA },
    { "ca", PROG_MODE_NA },
    { "cr", PROG_MODE_CR },
    { "br", PROG_MODE_CR },
    { "uk", PROG_MODE_UK },
};

static struct progress
{
    int size;
    int freqs[7];
} modes[] =
{
    { GSAMP_SIZE_NA, { 350, 440, 480, 620, 950, 1400, 1800 } },    /* North America */
    { GSAMP_SIZE_CR, { 425 } },
    { GSAMP_SIZE_UK, { 400 } },
};

#define DEFAULT_THRESHOLD    512

#define BUSY_PERCENT        10      /* The percentage difference between the two last silence periods */
#define BUSY_PAT_PERCENT    7       /* The percentage difference between measured and actual pattern */
#define BUSY_THRESHOLD      100     /* Max number of ms difference between max and min times in busy */
#define BUSY_MIN            75      /* Busy must be at least 80 ms in half-cadence */
#define BUSY_MAX            3100    /* Busy can't be longer than 3100 ms in half-cadence */

/* Remember last 15 units */
#define DSP_HISTORY         15

#define TONE_THRESH         10.0f   /* How much louder the tone should be than channel energy */
#define TONE_MIN_THRESH     1.0e8f  /* How much tone there should be at least to attempt */
#define COUNT_THRESH        3       /* Need at least 50ms of stuff to count it */
#define UK_HANGUP_THRESH    60      /* This is the threshold for the UK */

#define BUSYDETECT

#if !defined(BUSYDETECT_MARTIN)  &&  !defined(BUSYDETECT)  &&  !defined(BUSYDETECT_COMPARE_TONE_AND_SILENCE)
#define BUSYDETECT_MARTIN
#endif

struct cw_dsp
{
    struct cw_frame f;
    int threshold;
    int totalsilence;
    int totalnoise;
    int features;
    int busy_maybe;
    int busycount;
    int busy_tonelength;
    int busy_quietlength;
    int noise_history[DSP_HISTORY];
    int silence_history[DSP_HISTORY];
    goertzel_state_t freqs[7];
    int freqcount;
    int gsamps;
    int gsamp_size;
    int progmode;
    int tstate;
    int tcount;
    int digit_mode;
    int possible_digit;
    int mute_lag;
    float genergy;
    dtmf_rx_state_t dtmf_rx;
    modem_connect_tones_rx_state_t fax_ced_rx;
    modem_connect_tones_rx_state_t fax_cng_rx;
    bell_mf_rx_state_t bell_mf_rx;
};

static inline int pair_there(float p1, float p2, float i1, float i2, float e)
{
    /* See if p1 and p2 are there, relative to i1 and i2 and total energy */
    /* Make sure absolute levels are high enough */
    if ((p1 < TONE_MIN_THRESH)  ||  (p2 < TONE_MIN_THRESH))
        return 0;
    /* Amplify ignored stuff */
    i2 *= TONE_THRESH;
    i1 *= TONE_THRESH;
    e *= TONE_THRESH;
    /* Check first tone */
    if ((p1 < i1)  ||  (p1 < i2)  ||  (p1 < e))
        return 0;
    /* And second */
    if ((p2 < i1)  ||  (p2 < i2)  ||  (p2 < e))
        return 0;
    /* Guess it's there... */
    return 1;
}

static int cw_dsp_call_progress(struct cw_dsp *dsp, int16_t *s, int len)
{
    int x;
    int y;
    int pass;
    int newstate = DSP_TONE_STATE_SILENCE;
    int res = 0;
    int thresh = COUNT_THRESH;

    while (len)
    {
        /* Take the lesser of the number of samples we need and what we have */
        pass = len;
        if (pass > dsp->gsamp_size - dsp->gsamps) 
            pass = dsp->gsamp_size - dsp->gsamps;
        for (x = 0;  x < pass;  x++)
        {
            for (y = 0;  y < dsp->freqcount;  y++) 
                goertzel_sample(&dsp->freqs[y], s[x]);
            dsp->genergy += s[x] * s[x];
        }
        s += pass;
        dsp->gsamps += pass;
        len -= pass;
        if (dsp->gsamps == dsp->gsamp_size)
        {
            float hz[7];
            for (y = 0;  y < 7;  y++)
                hz[y] = goertzel_result(&dsp->freqs[y]);
#if 0
            printf("\n350:     425:     440:     480:     620:     950:     1400:    1800:    Energy:   \n");
            printf("%.2e %.2e %.2e %.2e %.2e %.2e %.2e %.2e %.2e\n", 
                   hz[HZ_350], hz[HZ_425], hz[HZ_440], hz[HZ_480], hz[HZ_620], hz[HZ_950], hz[HZ_1400], hz[HZ_1800], dsp->genergy);
#endif
            switch (dsp->progmode)
            {
                case PROG_MODE_NA:
                    if (pair_there(hz[HZ_480], hz[HZ_620], hz[HZ_350], hz[HZ_440], dsp->genergy))
                    {
                        newstate = DSP_TONE_STATE_BUSY;
                    }
                    else if (pair_there(hz[HZ_440], hz[HZ_480], hz[HZ_350], hz[HZ_620], dsp->genergy))
                    {
                        newstate = DSP_TONE_STATE_RINGING;
                    }
                    else if (pair_there(hz[HZ_350], hz[HZ_440], hz[HZ_480], hz[HZ_620], dsp->genergy))
                    {
                        newstate = DSP_TONE_STATE_DIALTONE;
                    }
                    else if (hz[HZ_950] > TONE_MIN_THRESH * TONE_THRESH)
                    {
                        newstate = DSP_TONE_STATE_SPECIAL1;
                    }
                    else if (hz[HZ_1400] > TONE_MIN_THRESH * TONE_THRESH)
                    {
                        if (dsp->tstate == DSP_TONE_STATE_SPECIAL1)
                            newstate = DSP_TONE_STATE_SPECIAL2;
                    }
                    else if (hz[HZ_1800] > TONE_MIN_THRESH * TONE_THRESH)
                    {
                        if (dsp->tstate == DSP_TONE_STATE_SPECIAL2)
                            newstate = DSP_TONE_STATE_SPECIAL3;
                    }
                    else if (dsp->genergy > TONE_MIN_THRESH * TONE_THRESH)
                    {
                        newstate = DSP_TONE_STATE_TALKING;
                    }
                    break;

                case PROG_MODE_CR:
                    if (hz[HZ_425] > TONE_MIN_THRESH * TONE_THRESH)
                        newstate = DSP_TONE_STATE_RINGING;
                    break;

                case PROG_MODE_UK:
                    if (hz[HZ_400] > TONE_MIN_THRESH * TONE_THRESH)
                    {
                        newstate = DSP_TONE_STATE_HUNGUP;
                        thresh = UK_HANGUP_THRESH;
                    }
                    break;

                default:
                    cw_log(CW_LOG_WARNING, "Can't process in unknown prog mode '%d'\n", dsp->progmode);
                    break;
            }

            /* If we couldn't find anything better above we just have to
             * choose between silence and "talking" (not silence).
             */
            if (newstate == DSP_TONE_STATE_SILENCE && dsp->genergy > TONE_MIN_THRESH * TONE_THRESH)
                newstate = DSP_TONE_STATE_TALKING;

            if (newstate != dsp->tstate)
            {
                dsp->tstate = newstate;
                dsp->tcount = 0;
            }
            if (dsp->tcount < thresh)
            {
                dsp->tcount++;
                if (dsp->tcount == thresh)
                {
                    switch (dsp->tstate)
                    {
                        /* The first set occur during a call and may be legimately
                         * followed by other call progress indications.
                         */
                        case DSP_TONE_STATE_RINGING:
                            if ((dsp->features & DSP_PROGRESS_RINGING))
                                res = CW_CONTROL_RINGING;
                            break;

                       /* The final set all indicate that a call has ended in some
                        * way. There is no need to push further frames through the
                        * DSP for call progress detection.
                        */
                        default:
                            switch (dsp->tstate)
                            {
                                case DSP_TONE_STATE_TALKING:
                                    if ((dsp->features & DSP_PROGRESS_TALK))
                                        res = CW_CONTROL_ANSWER; /* FIXME: There should be a control frame for this */
                                    break;

                                case DSP_TONE_STATE_BUSY:
                                    if ((dsp->features & DSP_PROGRESS_BUSY))
                                        res = CW_CONTROL_BUSY;
                                    break;

                                case DSP_TONE_STATE_SPECIAL3:
                                    if ((dsp->features & DSP_PROGRESS_CONGESTION))
                                        res = CW_CONTROL_CONGESTION;
                                    break;

                                case DSP_TONE_STATE_HUNGUP:
                                    if ((dsp->features & DSP_FEATURE_CALL_PROGRESS))
                                        res = CW_CONTROL_HANGUP;
                                    break;
                            }
                            dsp->features &= ~DSP_FEATURE_CALL_PROGRESS;
                            break;
		    }
                }
            }

            /* Reset goertzel */                        
            for (x = 0;  x < 7;  x++)
                goertzel_reset(&dsp->freqs[x]);
            dsp->gsamps = 0;
            dsp->genergy = 0.0f;
        }
    }
    return res;
}


static int __cw_dsp_silence(struct cw_dsp *dsp, int16_t amp[], int len, int *totalsilence)
{
    int accum;
    int x;
    int res;

    if (len < 2)
        return 0;
    /* Use crude HPF, to provide DC immunity. Of course this provides little immunity to other forms of channel
       pollution, but it sidesteps a lot of the real world problems. */
    accum = 0;
    for (x = 0;  x < len - 1;  x++)
        accum += abs(amp[x + 1] - amp[x]);
    accum /= (len - 1);
    if (accum < dsp->threshold)
    {
        /* Silent */
        dsp->totalsilence += len/8;
        if (dsp->totalnoise)
        {
            /* Move and save history */
            memmove(dsp->noise_history + DSP_HISTORY - dsp->busycount, dsp->noise_history + DSP_HISTORY - dsp->busycount +1, dsp->busycount*sizeof(dsp->noise_history[0]));
            dsp->noise_history[DSP_HISTORY - 1] = dsp->totalnoise;
            /* We don't want to check for busydetect that frequently */
        }
        dsp->totalnoise = 0;
        res = TRUE;
    }
    else
    {
        /* Not silent */
        dsp->totalnoise += len/8;
        if (dsp->totalsilence)
        {
            int silence1 = dsp->silence_history[DSP_HISTORY - 1];
            int silence2 = dsp->silence_history[DSP_HISTORY - 2];
            /* Move and save history */
            memmove(dsp->silence_history + DSP_HISTORY - dsp->busycount, dsp->silence_history + DSP_HISTORY - dsp->busycount + 1, dsp->busycount*sizeof(dsp->silence_history[0]));
            dsp->silence_history[DSP_HISTORY - 1] = dsp->totalsilence;
            /* Check if the previous sample differs only by BUSY_PERCENT from the one before it */
            if (silence1 < silence2)
                dsp->busy_maybe = (silence1 + silence1*BUSY_PERCENT/100 >= silence2);
            else
                dsp->busy_maybe = (silence1 - silence1*BUSY_PERCENT/100 <= silence2);
        }
        dsp->totalsilence = 0;
        res = FALSE;
    }
    if (totalsilence)
        *totalsilence = dsp->totalsilence;
    return res;
}

#ifdef BUSYDETECT_MARTIN
int cw_dsp_busydetect(struct cw_dsp *dsp)
{
    int res = 0;
    int x;
    int avgsilence = 0;
    int hitsilence = 0;
    int avgtone = 0;
    int hittone = 0;

    if (!dsp->busy_maybe)
        return res;
    for (x = DSP_HISTORY - dsp->busycount;  x < DSP_HISTORY;  x++)
    {
        avgsilence += dsp->silence_history[x];
        avgtone += dsp->noise_history[x];
    }
    avgsilence /= dsp->busycount;
    avgtone /= dsp->busycount;
    for (x = DSP_HISTORY - dsp->busycount;  x < DSP_HISTORY;  x++)
    {
        if (avgsilence > dsp->silence_history[x])
        {
            if (avgsilence - (avgsilence*BUSY_PERCENT/100) <= dsp->silence_history[x])
                hitsilence++;
        }
        else
        {
            if (avgsilence + (avgsilence*BUSY_PERCENT/100) >= dsp->silence_history[x])
                hitsilence++;
        }
        if (avgtone > dsp->noise_history[x])
        {
            if (avgtone - (avgtone*BUSY_PERCENT/100) <= dsp->noise_history[x])
                hittone++;
        }
        else
        {
            if (avgtone + (avgtone*BUSY_PERCENT/100) >= dsp->noise_history[x])
                hittone++;
        }
    }
    if ((hittone >= dsp->busycount - 1)
        &&
        (hitsilence >= dsp->busycount - 1)
        && 
        (avgtone >= BUSY_MIN  &&  avgtone <= BUSY_MAX)
        && 
        (avgsilence >= BUSY_MIN  &&  avgsilence <= BUSY_MAX))
    {
#ifdef BUSYDETECT_COMPARE_TONE_AND_SILENCE
        if (avgtone > avgsilence)
        {
            if (avgtone - avgtone*BUSY_PERCENT/100 <= avgsilence)
                res = 1;
        }
        else
        {
            if (avgtone + avgtone*BUSY_PERCENT/100 >= avgsilence)
                res = 1;
        }
#else
        res = 1;
#endif
    }
    /* If we know the expected busy tone length, check we are in the range */
    if (res  &&  (dsp->busy_tonelength > 0))
    {
        if (abs(avgtone - dsp->busy_tonelength) > (dsp->busy_tonelength*BUSY_PAT_PERCENT/100))
            res = 0;
    }
    /* If we know the expected busy tone silent-period length, check we are in the range */
    if (res  &&  (dsp->busy_quietlength > 0))
    {
        if (abs(avgsilence - dsp->busy_quietlength) > (dsp->busy_quietlength*BUSY_PAT_PERCENT/100))
            res = 0;
    }
#if 1
    if (res)
        cw_log(CW_LOG_DEBUG, "cw_dsp_busydetect detected busy, avgtone: %d, avgsilence %d\n", avgtone, avgsilence);
#endif
    return res;
}
#endif

#ifdef BUSYDETECT
int cw_dsp_busydetect(struct cw_dsp *dsp)
{
    int x;
    int res;
    int max;
    int min;

    res = 0;
    if (dsp->busy_maybe)
    {
        dsp->busy_maybe = FALSE;
        min = 9999;
        max = 0;
        for (x = DSP_HISTORY - dsp->busycount;  x < DSP_HISTORY;  x++)
        {
            if (dsp->silence_history[x] < min)
                min = dsp->silence_history[x];
            if (dsp->noise_history[x] < min)
                min = dsp->noise_history[x];
            if (dsp->silence_history[x] > max)
                max = dsp->silence_history[x];
            if (dsp->noise_history[x] > max)
                max = dsp->noise_history[x];
        }
        if ((max - min < BUSY_THRESHOLD) && (max < BUSY_MAX) && (min > BUSY_MIN))
            res = 1;
    }
    return res;
}
#endif

int cw_dsp_silence(struct cw_dsp *dsp, struct cw_frame *f, int *totalsilence)
{
    int16_t *amp;
    uint8_t *data;
    int len;
    int x;

    if (f->frametype != CW_FRAME_VOICE)
    {
        cw_log(CW_LOG_WARNING, "Can't calculate silence on a non-voice frame\n");
        return 0;
    }
    data = f->data;
    len = 0;
    switch (f->subclass)
    {
    case CW_FORMAT_SLINEAR:
        amp = f->data;
        len = f->datalen/sizeof(int16_t);
        break;
    case CW_FORMAT_ULAW:
        amp = alloca(f->datalen*sizeof(int16_t));
        len = f->datalen;
        for (x = 0;  x < len;  x++) 
            amp[x] = CW_MULAW(data[x]);
        break;
    case CW_FORMAT_ALAW:
        amp = alloca(f->datalen*sizeof(int16_t));
        len = f->datalen;
        for (x = 0;  x < len;  x++) 
            amp[x] = CW_ALAW(data[x]);
        break;
    default:
        cw_log(CW_LOG_WARNING, "Silence detection is not supported on codec %s. Use RFC2833\n", cw_getformatname(f->subclass));
        return 0;
    }
    return __cw_dsp_silence(dsp, amp, len, totalsilence);
}

struct cw_frame *cw_dsp_process(struct cw_channel *chan, struct cw_dsp *dsp, struct cw_frame *af)
{
    char digit_buf[10];
    int x;
    int samples;
    int dtmf_status;
    int squelch = FALSE;
    int16_t *amp;

    if (!af || af->frametype != CW_FRAME_VOICE)
        return af;

    /* Make sure we have short data */
    switch (af->subclass)
    {
    case CW_FORMAT_SLINEAR:
        amp = af->data;
        samples = af->datalen / sizeof(int16_t);
        break;
    case CW_FORMAT_ULAW:
        amp = alloca(af->datalen * sizeof(int16_t));
        samples = af->datalen;
        for (x = 0;  x < af->datalen;  x++) 
            amp[x] = CW_MULAW(((uint8_t *)af->data)[x]);
        break;
    case CW_FORMAT_ALAW:
        amp = alloca(af->datalen * sizeof(int16_t));
        samples = af->datalen;
        for (x = 0;  x < af->datalen;  x++) 
            amp[x] = CW_ALAW(((uint8_t *)af->data)[x]);
        break;
    default:
        cw_log(CW_LOG_WARNING, "Tone detection is not supported on codec %s. Use RFC2833\n", cw_getformatname(af->subclass));
        return af;
    }

    if ((dsp->features & DSP_FEATURE_SILENCE_SUPPRESS)  &&  __cw_dsp_silence(dsp, amp, samples, NULL))
    {
        dsp->f.frametype = CW_FRAME_NULL;
        return &dsp->f;
    }

    if ((dsp->features & DSP_FEATURE_BUSY_DETECT)  &&  cw_dsp_busydetect(dsp))
    {
        chan->_softhangup |= CW_SOFTHANGUP_DEV;
        dsp->f.frametype = CW_FRAME_CONTROL;
        dsp->f.subclass = CW_CONTROL_BUSY;
        cw_log(CW_LOG_DEBUG, "Requesting Hangup because the busy tone was detected on channel %s\n", chan->name);
        return &dsp->f;
    }

    if ((dsp->features & DSP_FEATURE_DTMF_DETECT))
    {
        if ((dsp->digit_mode & DSP_DIGITMODE_MF))
        {
            bell_mf_rx(&dsp->bell_mf_rx, amp, samples);
            if (bell_mf_rx_get(&dsp->bell_mf_rx, digit_buf, 1))
            {
                dsp->f.frametype = CW_FRAME_DTMF;
                dsp->f.subclass = digit_buf[0];
                goto out_event;
            }
        }
        else
        {
            dtmf_rx(&dsp->dtmf_rx, amp, samples);
            dtmf_status = dtmf_rx_status(&dsp->dtmf_rx);
            /* A confirmed "in digit" status should cause mute to overhang */
            if (dtmf_status  &&  dtmf_status != 'x')
                dsp->mute_lag = 5;
            if (dsp->mute_lag)
            {
                if (--dsp->mute_lag)
                    squelch = TRUE;
                else
                {
                    dsp->possible_digit = FALSE;
                    if ((dsp->digit_mode & (DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX)))
                    {
                        signed char sc = 0;
                        cw_channel_setoption(chan, CW_OPTION_MUTECONF, &sc, 1);
                        goto out_audio;
                    }
                }
            }
            if ((dsp->digit_mode & (DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX))
            && !dsp->possible_digit && dtmf_status)
            {
                signed char sc = 1;
                /* Looks like we might have something.  
                   Request a conference mute for the moment */
                cw_channel_setoption(chan, CW_OPTION_MUTECONF, &sc, 1);
                dsp->possible_digit = TRUE;
                goto out_audio;
            }
            if (dtmf_rx_get(&dsp->dtmf_rx, digit_buf, 1))
            {
                dsp->f.frametype = CW_FRAME_DTMF;
                dsp->f.subclass = digit_buf[0];
                goto out_event;
            }
        }
    }

    if ((dsp->features & DSP_FEATURE_FAX_CNG_DETECT))
    {
        modem_connect_tones_rx(&dsp->fax_cng_rx, amp, samples);
        if (modem_connect_tones_rx_get(&dsp->fax_cng_rx))
        {
            dsp->f.frametype = CW_FRAME_DTMF;
            dsp->f.subclass = 'f';
            goto out_event;
        }
    }

    if ((dsp->features & DSP_FEATURE_FAX_CED_DETECT))
    {
        modem_connect_tones_rx(&dsp->fax_ced_rx, amp, samples);
        if (modem_connect_tones_rx_get(&dsp->fax_ced_rx))
        {
            dsp->f.frametype = CW_FRAME_DTMF;
            dsp->f.subclass = 'F';
            goto out_event;
        }
    }

    if ((dsp->features & DSP_FEATURE_CALL_PROGRESS))
    {
        dsp->f.frametype = CW_FRAME_CONTROL;
        if ((dsp->f.subclass = cw_dsp_call_progress(dsp, amp, samples)))
            goto out_event;
    }

out_audio:
    /* We know we have slinear or xlaw.
     * Slinear silence is 0, alaw and ulaw are both 0xff.
     */
    if (squelch)
        memset(af->data, (af->subclass != CW_FORMAT_SLINEAR ? -1 : 0), af->datalen);
    return af;

out_event:
    /* We know we have slinear or xlaw.
     * Slinear silence is 0, alaw and ulaw are both 0xff.
     */
    if (squelch)
        memset(af->data, (af->subclass != CW_FORMAT_SLINEAR ? -1 : 0), af->datalen);
    if (chan)
        cw_queue_frame(chan, af);
    cw_fr_free(af);
    return &dsp->f;
}

static void cw_dsp_prog_reset(struct cw_dsp *dsp)
{
    goertzel_descriptor_t desc;
    int max;
    int x;
    
    dsp->gsamp_size = modes[dsp->progmode].size;
    dsp->gsamps = 0;
    max = 0;
    for (x = 0;  x < sizeof(modes[dsp->progmode].freqs)/sizeof(modes[dsp->progmode].freqs[0]);  x++)
    {
        if (modes[dsp->progmode].freqs[x])
        {
            make_goertzel_descriptor(&desc, (float) modes[dsp->progmode].freqs[x], dsp->gsamp_size);
            goertzel_init(&dsp->freqs[x], &desc);
            max = x + 1;
        }
    }
    dsp->freqcount = max;
}

struct cw_dsp *cw_dsp_new(void)
{
    struct cw_dsp *dsp;

    if ((dsp = malloc(sizeof(*dsp))))
    {
        memset(dsp, 0, sizeof(*dsp));
        dsp->threshold = DEFAULT_THRESHOLD;
        dsp->features = DSP_FEATURE_SILENCE_SUPPRESS;
        dsp->busycount = DSP_HISTORY;

        cw_fr_init(&dsp->f);

        dtmf_rx_init(&dsp->dtmf_rx, NULL, NULL);
        dsp->mute_lag = 0;

        modem_connect_tones_rx_init(&dsp->fax_cng_rx,
                                    MODEM_CONNECT_TONES_FAX_CNG,
                                    NULL,
                                    NULL);
        modem_connect_tones_rx_init(&dsp->fax_ced_rx,
                                    MODEM_CONNECT_TONES_FAX_CED,
                                    NULL,
                                    NULL);

        /* Initialize initial DSP progress detect parameters */
        cw_dsp_prog_reset(dsp);
    }
    return dsp;
}

void cw_dsp_free(struct cw_dsp *dsp)
{
    free(dsp);
}

void cw_dsp_set_features(struct cw_dsp *dsp, int features)
{
    dsp->features = features;
}

void cw_dsp_set_threshold(struct cw_dsp *dsp, int threshold)
{
    dsp->threshold = threshold;
}

void cw_dsp_set_busy_count(struct cw_dsp *dsp, int cadences)
{
    if (cadences < 4)
        cadences = 4;
    if (cadences > DSP_HISTORY)
        cadences = DSP_HISTORY;
    dsp->busycount = cadences;
}

void cw_dsp_set_busy_pattern(struct cw_dsp *dsp, int tonelength, int quietlength)
{
    dsp->busy_tonelength = tonelength;
    dsp->busy_quietlength = quietlength;
    cw_log(CW_LOG_DEBUG, "DSP busy pattern set to %d,%d\n", tonelength, quietlength);
}

void cw_dsp_digitreset(struct cw_dsp *dsp)
{
    dsp->possible_digit = FALSE;
    if (dsp->digit_mode & DSP_DIGITMODE_MF)
        bell_mf_rx_init(&dsp->bell_mf_rx, NULL, NULL);
    else
        dtmf_rx_init(&dsp->dtmf_rx, NULL, NULL);
    dsp->mute_lag = 0;
    modem_connect_tones_rx_init(&dsp->fax_cng_rx,
                                MODEM_CONNECT_TONES_FAX_CNG,
                                NULL,
                                NULL);
    modem_connect_tones_rx_init(&dsp->fax_ced_rx,
                                MODEM_CONNECT_TONES_FAX_CED,
                                NULL,
                                NULL);
}

void cw_dsp_reset(struct cw_dsp *dsp)
{
    int x;
    
    dsp->totalsilence = 0;
    dsp->gsamps = 0;
    for (x = 0;  x < 4;  x++)
        goertzel_reset(&dsp->freqs[x]);
    memset(dsp->silence_history, 0, sizeof(dsp->silence_history));
    memset(dsp->noise_history, 0, sizeof(dsp->noise_history));    
}

int cw_dsp_digitmode(struct cw_dsp *dsp, int digit_mode)
{
    int new_mode;
    int old_mode;
    
    old_mode = dsp->digit_mode & (DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX);
    new_mode = digit_mode & (DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX);
    if (old_mode != new_mode)
    {
        /* Must initialize structures if switching from MF to DTMF or vice-versa */
        if ((new_mode & DSP_DIGITMODE_MF))
            bell_mf_rx_init(&dsp->bell_mf_rx, NULL, NULL);
        else
            dtmf_rx_init(&dsp->dtmf_rx, NULL, NULL);
        dsp->mute_lag = 0;
        modem_connect_tones_rx_init(&dsp->fax_cng_rx,
                                    MODEM_CONNECT_TONES_FAX_CNG,
                                    NULL,
                                    NULL);
        modem_connect_tones_rx_init(&dsp->fax_ced_rx,
                                    MODEM_CONNECT_TONES_FAX_CED,
                                    NULL,
                                    NULL);
    }
    dtmf_rx_parms(&dsp->dtmf_rx, FALSE, 8, (digit_mode & DSP_DIGITMODE_RELAXDTMF)  ?  8  :  4, -99);
    dsp->digit_mode = digit_mode;
    return 0;
}

int cw_dsp_set_call_progress_zone(struct cw_dsp *dsp, char *zone)
{
    int x;
    
    for (x = 0;  x < sizeof(aliases)/sizeof(aliases[0]);  x++)
    {
        if (strcasecmp(aliases[x].name, zone) == 0)
        {
            dsp->progmode = aliases[x].mode;
            cw_dsp_prog_reset(dsp);
            return 0;
        }
    }
    return -1;
}
