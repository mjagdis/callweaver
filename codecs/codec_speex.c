/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
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
 * \brief Translate between signed linear and Speex (Open Codec)
 *
 * http://www.speex.org
 * \note This work was motivated by Jeremy McNamara 
 * hacked to be configurable by anthm and bkw 9/28/2004
 */
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <speex/speex.h>

/* We require a post 1.1.8 version of Speex to enable preprocessing
   and better type handling */   
#ifdef _SPEEX_TYPES_H
#include <speex/speex_preprocess.h>
#endif

static int quality = 3;
static int complexity = 2;
static int enhancement = 0;
static int vad = 0;
static int vbr = 0;
static float vbr_quality = 4;
static int abr = 0;
static int dtx = 0;

static int preproc = 0;
static int pp_vad = 0;
static int pp_agc = 0;
static float pp_agc_level = 8000;
static int pp_denoise = 0;
static int pp_dereverb = 0;
static float pp_dereverb_decay = 0.4;
static float pp_dereverb_level = 0.3;

#define TYPE_SILENCE 0x2
#define TYPE_HIGH     0x0
#define TYPE_LOW     0x1
#define TYPE_MASK     0x3

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/module.h"
#include "callweaver/config.h"
#include "callweaver/options.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"

#include "core/translate.h"
#include "callweaver/translate.h"


/* Sample frame of Speex data */
static unsigned char speex_ex[] =
{
    0x2e, 0x8e, 0x0f, 0x9a, 0x20, 0000, 0x01, 0x7f, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0x91, 0000, 0xbf, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xdc, 0x80, 0x5f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0x98, 0x7f, 0xff, 0xff, 0xff, 0xe8, 0xff, 0xf7, 0x80
};


struct speex_coder_pvt
{
    void *speex;
    struct cw_frame f;
    SpeexBits bits;
    int framesize;
    /* Space to build offset */
    char offset[CW_FRIENDLY_OFFSET];
#ifdef _SPEEX_TYPES_H
    SpeexPreprocessState *pp;
    /* Buffer for our outgoing frame */
    spx_int16_t outbuf[8000];
    /* Enough to store a full second */
    spx_int16_t buf[8000];
#else
    int16_t outbuf[8000];
    int16_t buf[8000];
#endif

    int tail;
    int silent_state;
};


static void *lintospeex_new(void)
{
    struct speex_coder_pvt *tmp;

    if ((tmp = malloc(sizeof(struct speex_coder_pvt))) == NULL)
        return NULL;
    if ((tmp->speex = speex_encoder_init(&speex_nb_mode)) == NULL)
    {
        free(tmp);
        return NULL;
    }
    speex_bits_init(&tmp->bits);
    speex_bits_reset(&tmp->bits);
    speex_encoder_ctl(tmp->speex, SPEEX_GET_FRAME_SIZE, &tmp->framesize);
    speex_encoder_ctl(tmp->speex, SPEEX_SET_COMPLEXITY, &complexity);
#ifdef _SPEEX_TYPES_H
    if (preproc)
    {
        tmp->pp = speex_preprocess_state_init(tmp->framesize, 8000);
        speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_VAD, &pp_vad);
        speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_AGC, &pp_agc);
        speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_AGC_LEVEL, &pp_agc_level);
        speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_DENOISE, &pp_denoise);
        speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_DEREVERB, &pp_dereverb);
        speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_DEREVERB_DECAY, &pp_dereverb_decay);
        speex_preprocess_ctl(tmp->pp, SPEEX_PREPROCESS_SET_DEREVERB_LEVEL, &pp_dereverb_level);
    }
#endif
    if (!abr  &&  !vbr)
    {
        speex_encoder_ctl(tmp->speex, SPEEX_SET_QUALITY, &quality);
        if (vad)
            speex_encoder_ctl(tmp->speex, SPEEX_SET_VAD, &vad);
    }
    if (vbr)
    {
        speex_encoder_ctl(tmp->speex, SPEEX_SET_VBR, &vbr);
        speex_encoder_ctl(tmp->speex, SPEEX_SET_VBR_QUALITY, &vbr_quality);
    }
    if (abr)
    {
        speex_encoder_ctl(tmp->speex, SPEEX_SET_ABR, &abr);
    }
    if (dtx)
        speex_encoder_ctl(tmp->speex, SPEEX_SET_DTX, &dtx); 
    tmp->tail = 0;
    tmp->silent_state = 0;
    return tmp;
}

static void *speextolin_new(void)
{
    struct speex_coder_pvt *tmp;

    if ((tmp = malloc(sizeof(struct speex_coder_pvt))) == NULL)
        return NULL;
    if (!(tmp->speex = speex_decoder_init(&speex_nb_mode)))
    {
        free(tmp);
        return NULL;
    }
    speex_bits_init(&tmp->bits);
    speex_decoder_ctl(tmp->speex, SPEEX_GET_FRAME_SIZE, &tmp->framesize);
    if (enhancement)
        speex_decoder_ctl(tmp->speex, SPEEX_SET_ENH, &enhancement);
    tmp->tail = 0;
    return tmp;
}

static struct cw_frame *speextolin_sample(int *i)
{
    static struct cw_frame f;

    CW_UNUSED(i);

    cw_fr_init_ex(&f, CW_FRAME_VOICE, CW_FORMAT_SPEEX);
    f.datalen = sizeof(speex_ex);
    /* All frames are 20 ms long */
    f.samples = 160;
    f.data = speex_ex;
    return &f;
}

static struct cw_frame *speextolin_frameout(void *pvt)
{
    struct speex_coder_pvt *tmp = (struct speex_coder_pvt *)pvt;

    if (tmp->tail == 0)
        return NULL;

    /* Signed linear is no particular frame size, so just send whatever
       we have in the buffer in one lump sum */
    cw_fr_init_ex(&tmp->f, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
    tmp->f.datalen = tmp->tail*sizeof(int16_t);
    /* Assume 8000 Hz */
    tmp->f.samples = tmp->tail;
    tmp->f.offset = CW_FRIENDLY_OFFSET;
    tmp->f.data = tmp->buf;

    /* Reset tail pointer */
    tmp->tail = 0;
    return &tmp->f;    
}

static int speextolin_framein(void *pvt, struct cw_frame *f)
{
    struct speex_coder_pvt *tmp = (struct speex_coder_pvt *)pvt;
    /* Assuming there's space left, decode into the current buffer at
       the tail location.  Read in as many frames as there are */
    int x;
    int res;
#ifdef _SPEEX_TYPES_H
    spx_int16_t out[1024];
#else
    float fout[1024];
#endif

    if (f->datalen == 0)
    {
        /* Native PLC interpolation */
        if (tmp->tail + tmp->framesize > sizeof(tmp->buf) / 2)
        {
            cw_log(CW_LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
#ifdef _SPEEX_TYPES_H
        speex_decode_int(tmp->speex, NULL, tmp->buf + tmp->tail);
#else
        speex_decode(tmp->speex, NULL, fout);
        for (x = 0;  x < tmp->framesize;  x++)
        {
            tmp->buf[tmp->tail + x] = fout[x];
        }
#endif
        tmp->tail += tmp->framesize;
        return 0;
    }

    /* Read in bits */
    speex_bits_read_from(&tmp->bits, f->data, f->datalen);
    for (;;)
    {
#ifdef _SPEEX_TYPES_H
        res = speex_decode_int(tmp->speex, &tmp->bits, out);
#else
        res = speex_decode(tmp->speex, &tmp->bits, fout);
#endif
        if (res < 0)
            break;
        if (tmp->tail + tmp->framesize < sizeof(tmp->buf) / 2)
        {
            for (x = 0;  x < tmp->framesize;  x++)
            {
#ifdef _SPEEX_TYPES_H
                tmp->buf[tmp->tail + x] = out[x];
#else
                tmp->buf[tmp->tail + x] = fout[x];
#endif
            }
            tmp->tail += tmp->framesize;
        }
        else
        {
            cw_log(CW_LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
        
    }
    return 0;
}

static int lintospeex_framein(void *pvt, struct cw_frame *f)
{
    struct speex_coder_pvt *tmp = (struct speex_coder_pvt *)pvt;

    /* Just add the frames to our stream */
    /* XXX We should look at how old the rest of our stream is, and if it
       is too old, then we should overwrite it entirely, otherwise we can
       get artifacts of earlier talk that do not belong */
    if (tmp->tail + f->datalen/2 < sizeof(tmp->buf) / 2)
    {
        memcpy((tmp->buf + tmp->tail), f->data, f->datalen);
        tmp->tail += f->datalen/2;
    }
    else
    {
        cw_log(CW_LOG_WARNING, "Out of buffer space\n");
        return -1;
    }
    return 0;
}

static struct cw_frame *lintospeex_frameout(void *pvt)
{
    struct speex_coder_pvt *tmp = (struct speex_coder_pvt *)pvt;
#ifndef _SPEEX_TYPES_H
    float fbuf[1024];
    int x;
#endif
    int len;
    int y = 0;
    int is_speech = 1;
    
    /* We can't work on anything less than a frame in size */
    if (tmp->tail < tmp->framesize)
        return NULL;
    cw_fr_init_ex(&tmp->f, CW_FRAME_VOICE, CW_FORMAT_SPEEX);
    tmp->f.offset = CW_FRIENDLY_OFFSET;
    tmp->f.data = tmp->outbuf;
    speex_bits_reset(&tmp->bits);
    while (tmp->tail >= tmp->framesize)
    {
#ifdef _SPEEX_TYPES_H
        /* Preprocess audio */
        if(preproc)
            is_speech = speex_preprocess(tmp->pp, tmp->buf, NULL);
        /* Encode a frame of data */
        if (is_speech)
        {
            /* If DTX enabled speex_encode returns 0 during silence */
            is_speech = speex_encode_int(tmp->speex, tmp->buf, &tmp->bits) || !dtx;
        }
        else
        {
            /* 5 zeros interpreted by Speex as silence (submode 0) */
            speex_bits_pack(&tmp->bits, 0, 5);
        }
#else
        /* Convert to floating point */
        for (x=0;x<tmp->framesize;x++)
            fbuf[x] = tmp->buf[x];
        /* Encode a frame of data */
        is_speech = speex_encode(tmp->speex, fbuf, &tmp->bits) || !dtx;
#endif
        /* Assume 8000 Hz -- 20 ms */
        tmp->tail -= tmp->framesize;
        /* Move the data at the end of the buffer to the front */
        if (tmp->tail)
            memmove(tmp->buf, tmp->buf + tmp->framesize, tmp->tail * 2);
        y++;
    }

    /* Use CW_FRAME_CNG to signify the start of any silence period */
    if (!is_speech)
    {
        if (tmp->silent_state)
            return NULL;
        tmp->silent_state = 1;
        speex_bits_reset(&tmp->bits);
        tmp->f.frametype = CW_FRAME_CNG;
    }
    else
    {
        tmp->silent_state = 0;
    }

    /* Terminate bit stream */
    speex_bits_pack(&tmp->bits, 15, 5);
    len = speex_bits_write(&tmp->bits, (char *)tmp->outbuf, sizeof(tmp->outbuf));
    tmp->f.datalen = len;
    tmp->f.samples = y * 160;
#if 0
    {
        static int fd = -1;
        
        if (fd < 0)
        {
            fd = open("speex.raw", O_WRONLY|O_TRUNC|O_CREAT);
            if (fd > -1)
            {
                write(fd, tmp->f.data, tmp->f.datalen);
                close(fd);
            }
        }
    }
#endif
    return &tmp->f;    
}

static void speextolin_destroy(void *pvt)
{
    struct speex_coder_pvt *tmp = (struct speex_coder_pvt *)pvt;

    speex_decoder_destroy(tmp->speex);
    speex_bits_destroy(&tmp->bits);
    free(tmp);
}

static void lintospeex_destroy(void *pvt)
{
    struct speex_coder_pvt *tmp = (struct speex_coder_pvt *)pvt;

#ifdef _SPEEX_TYPES_H
    if (preproc)
        speex_preprocess_state_destroy(tmp->pp);
#endif
    speex_encoder_destroy(tmp->speex);
    speex_bits_destroy(&tmp->bits);
    free(tmp);
}

static cw_translator_t speextolin =
{
    .name = "speextolin", 
    .src_format = CW_FORMAT_SPEEX, 
    .src_rate = 8000,
    .dst_format = CW_FORMAT_SLINEAR,
    .dst_rate = 8000,
    .newpvt = speextolin_new,
    .framein = speextolin_framein,
    .frameout = speextolin_frameout,
    .destroy = speextolin_destroy,
    .sample = speextolin_sample
};

static cw_translator_t lintospeex =
{
    .name = "lintospeex", 
    .src_format = CW_FORMAT_SLINEAR, 
    .src_rate = 8000,
    .dst_format = CW_FORMAT_SPEEX,
    .dst_rate = 8000,
    .newpvt = lintospeex_new,
    .framein = lintospeex_framein,
    .frameout = lintospeex_frameout,
    .destroy = lintospeex_destroy,
    .sample = cw_translate_linear_sample
};

static void parse_config(void) 
{
    struct cw_config *cfg;
    struct cw_variable *var;
    int res;
    float res_f;

    if ((cfg = cw_config_load("codecs.conf")))
    {
        if ((var = cw_variable_browse(cfg, "speex")))
        {
            while (var)
            {
                if (!strcasecmp(var->name, "quality"))
                {
                    res = atoi(var->value);
                    if (res > -1 && res < 11)
                    {
                        if (option_verbose > 2)
                            cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting Quality to %d\n",res);
                        quality = res;
                    }
                    else 
                        cw_log(CW_LOG_ERROR,"Error Quality must be 0-10\n");
                }
                else if (!strcasecmp(var->name, "complexity"))
                {
                    res = atoi(var->value);
                    if (res > -1 && res < 11)
                    {
                        if (option_verbose > 2)
                            cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting Complexity to %d\n",res);
                        complexity = res;
                    }
                    else 
                        cw_log(CW_LOG_ERROR,"Error! Complexity must be 0-10\n");
                }
                else if (!strcasecmp(var->name, "vbr_quality"))
                {
                    if (sscanf(var->value, "%f", &res_f) == 1 && res_f >= 0 && res_f <= 10)
                    {
                        if (option_verbose > 2)
                            cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting VBR Quality to %f\n",res_f);
                        vbr_quality = res_f;
                    }
                    else
                        cw_log(CW_LOG_ERROR,"Error! VBR Quality must be 0-10\n");
                }
                else if (!strcasecmp(var->name, "abr_quality"))
                {
                    cw_log(CW_LOG_ERROR,"Error! ABR Quality setting obsolete, set ABR to desired bitrate\n");
                }
                else if (!strcasecmp(var->name, "enhancement"))
                {
                    enhancement = cw_true(var->value) ? 1 : 0;
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Perceptual Enhancement Mode. [%s]\n",enhancement ? "on" : "off");
                }
                else if (!strcasecmp(var->name, "vbr"))
                {
                    vbr = cw_true(var->value) ? 1 : 0;
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: VBR Mode. [%s]\n",vbr ? "on" : "off");
                }
                else if (!strcasecmp(var->name, "abr"))
                {
                    res = atoi(var->value);
                    if (res >= 0)
                    {
                        if (option_verbose > 2)
                        {
                            if (res > 0)
                                cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting ABR target bitrate to %d\n",res);
                            else
                                cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Disabling ABR\n");
                        }
                        abr = res;
                    }
                    else 
                        cw_log(CW_LOG_ERROR,"Error! ABR target bitrate must be >= 0\n");
                }
                else if (!strcasecmp(var->name, "vad"))
                {
                    vad = cw_true(var->value) ? 1 : 0;
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: VAD Mode. [%s]\n",vad ? "on" : "off");
                }
                else if (!strcasecmp(var->name, "dtx"))
                {
                    dtx = cw_true(var->value) ? 1 : 0;
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: DTX Mode. [%s]\n",dtx ? "on" : "off");
                }
                else if (!strcasecmp(var->name, "preprocess"))
                {
                    preproc = cw_true(var->value) ? 1 : 0;
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Preprocessing. [%s]\n",preproc ? "on" : "off");
                }
                else if (!strcasecmp(var->name, "pp_vad"))
                {
                    pp_vad = cw_true(var->value) ? 1 : 0;
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Preprocessor VAD. [%s]\n",pp_vad ? "on" : "off");
                }
                else if (!strcasecmp(var->name, "pp_agc"))
                {
                    pp_agc = cw_true(var->value) ? 1 : 0;
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Preprocessor AGC. [%s]\n",pp_agc ? "on" : "off");
                }
                else if (!strcasecmp(var->name, "pp_agc_level"))
                {
                    if (sscanf(var->value, "%f", &res_f) == 1 && res_f >= 0)
                    {
                        if (option_verbose > 2)
                            cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting preprocessor AGC Level to %f\n",res_f);
                        pp_agc_level = res_f;
                    }
                    else
                        cw_log(CW_LOG_ERROR,"Error! Preprocessor AGC Level must be >= 0\n");
                }
                else if (!strcasecmp(var->name, "pp_denoise"))
                {
                    pp_denoise = cw_true(var->value) ? 1 : 0;
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Preprocessor Denoise. [%s]\n",pp_denoise ? "on" : "off");
                }
                else if (!strcasecmp(var->name, "pp_dereverb"))
                {
                    pp_dereverb = cw_true(var->value) ? 1 : 0;
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Preprocessor Dereverb. [%s]\n",pp_dereverb ? "on" : "off");
                }
                else if (!strcasecmp(var->name, "pp_dereverb_decay"))
                {
                    if (sscanf(var->value, "%f", &res_f) == 1 && res_f >= 0)
                    {
                        if (option_verbose > 2)
                            cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting preprocessor Dereverb Decay to %f\n",res_f);
                        pp_dereverb_decay = res_f;
                    } else
                        cw_log(CW_LOG_ERROR,"Error! Preprocessor Dereverb Decay must be >= 0\n");
                }
                else if (!strcasecmp(var->name, "pp_dereverb_level"))
                {
                    if (sscanf(var->value, "%f", &res_f) == 1 && res_f >= 0)
                    {
                        if (option_verbose > 2)
                            cw_verbose(VERBOSE_PREFIX_3 "CODEC SPEEX: Setting preprocessor Dereverb Level to %f\n",res_f);
                        pp_dereverb_level = res_f;
                    }
                    else
                        cw_log(CW_LOG_ERROR,"Error! Preprocessor Dereverb Level must be >= 0\n");
                }
                var = var->next;
            }
        }
        cw_config_destroy(cfg);
    }
}

static int reload_module(void) 
{
    parse_config();
    return 0;
}

static int unload_module(void)
{
    cw_translator_unregister(&speextolin);
    cw_translator_unregister(&lintospeex);
    return 0;
}

static int load_module(void)
{
    parse_config();
    cw_translator_register(&speextolin);
    cw_translator_register(&lintospeex);
    return 0;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, "Speex to/from PCM16 translator")
