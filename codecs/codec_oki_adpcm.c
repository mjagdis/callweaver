/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Implements the 32kbps Oki ADPCM codec, widely used for things like
 * voice mail and IVR, since it is the main codec used by Dialogic.
 *
 * Copyright (c) 2001 - 2005 Digium, Inc.
 * All rights reserved.
 *
 * Karl Sackett <krs@linux-support.net>, 2001-03-21
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
 * \brief codec_oki_adpcm.c - translate between signed linear and Dialogic ADPCM
 * 
 */
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/logger.h"
#include "callweaver/module.h"
#include "callweaver/config.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"

#include "core/translate.h"
#include "callweaver/translate.h"

#define BUFFER_SIZE   8096    /* size for the translation buffers */


static int useplc = 0;


/* Sample 10ms of ADPCM frame data */
static uint8_t adpcm_ex[] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*
 * Private workspace for translating signed linear signals to ADPCM.
 */
struct oki_adpcm_encoder_pvt
{
    struct cw_frame f;
    char offset[CW_FRIENDLY_OFFSET];   /* Space to build offset */
    int16_t inbuf[BUFFER_SIZE];           /* Unencoded signed linear values */
    uint8_t outbuf[BUFFER_SIZE];  /* Encoded ADPCM, two nibbles to a word */
    oki_adpcm_state_t oki_state;
    int tail;
};

/*
 * Private workspace for translating ADPCM signals to signed linear.
 */
struct oki_adpcm_decoder_pvt
{
    struct cw_frame f;
    char offset[CW_FRIENDLY_OFFSET];    /* Space to build offset */
    int16_t outbuf[BUFFER_SIZE];    /* Decoded signed linear values */
    oki_adpcm_state_t oki_state;
    int tail;
    plc_state_t plc;
};

/*
 * Create a new instance of adpcm_decoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */
static void *okiadpcmtolin_new(void)
{
    struct oki_adpcm_decoder_pvt *tmp;
  
    if ((tmp = malloc(sizeof(*tmp))) == NULL)
        return NULL;
    memset(tmp, 0, sizeof(*tmp));
    oki_adpcm_init(&tmp->oki_state, 32000);
    tmp->tail = 0;
    plc_init(&tmp->plc);
    return tmp;
}

/*
 * Create a new instance of adpcm_encoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */
static void *lintookiadpcm_new(void)
{
    struct oki_adpcm_encoder_pvt *tmp;
  
    if ((tmp = malloc(sizeof(*tmp))) == NULL)
        return NULL;
    memset(tmp, 0, sizeof(*tmp));
    oki_adpcm_init(&tmp->oki_state, 32000);
    tmp->tail = 0;
    return tmp;
}

/*
 * Take an input buffer with packed 4-bit ADPCM values and put decoded PCM in outbuf, 
 * if there is room left.
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  tmp->tail is the number of packed values in the buffer.
 */
static int okiadpcmtolin_framein(void *pvt, struct cw_frame *f)
{
    struct oki_adpcm_decoder_pvt *tmp = (struct oki_adpcm_decoder_pvt *) pvt;
    int len;

    if (f->datalen == 0)
    {
        /* perform PLC with nominal framesize of 20ms/160 samples */
        if ((tmp->tail + 160) > sizeof(tmp->outbuf)/sizeof(int16_t))
        {
            cw_log(CW_LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
        if (useplc)
        {
            plc_fillin(&tmp->plc, tmp->outbuf+tmp->tail, 160);
            tmp->tail += 160;
        }
        return 0;
    }

    if (f->datalen*4 + tmp->tail*2 > sizeof(tmp->outbuf))
    {
        cw_log(CW_LOG_WARNING, "Out of buffer space\n");
        return -1;
    }

    len = oki_adpcm_decode(&tmp->oki_state, tmp->outbuf + tmp->tail, f->data, f->datalen);
    if (useplc)
        plc_rx(&tmp->plc, tmp->outbuf + tmp->tail, len);
    tmp->tail += len;

    return 0;
}

/*
 * Convert 4-bit ADPCM encoded signals to 16-bit signed linear.
 *
 * Results:
 *  Converted signals are placed in tmp->f.data, tmp->f.datalen
 *  and tmp->f.samples are calculated.
 *
 * Side effects:
 *  None.
 */
static struct cw_frame *okiadpcmtolin_frameout(void *pvt)
{
    struct oki_adpcm_decoder_pvt *tmp = (struct oki_adpcm_decoder_pvt *) pvt;

    if (tmp->tail == 0)
        return NULL;
    cw_fr_init_ex(&tmp->f, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
    tmp->f.datalen = tmp->tail*sizeof(int16_t);
    tmp->f.samples = tmp->tail;
    tmp->f.offset = CW_FRIENDLY_OFFSET;
    tmp->f.data = tmp->outbuf;

    tmp->tail = 0;
    return &tmp->f;
}

/*
 * Fill an input buffer with 16-bit signed linear PCM values.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  tmp->tail is number of signal values in the input buffer.
 */
static int lintookiadpcm_framein(void *pvt, struct cw_frame *f)
{
    struct oki_adpcm_encoder_pvt *tmp = (struct oki_adpcm_encoder_pvt *) pvt;

    if ((tmp->tail + f->datalen/sizeof(int16_t)) < (sizeof (tmp->inbuf)/sizeof(int16_t)))
    {
        memcpy (&tmp->inbuf[tmp->tail], f->data, f->datalen);
        tmp->tail += f->datalen/sizeof(int16_t);
    }
    else
    {
        cw_log(CW_LOG_WARNING, "Out of buffer space\n");
        return -1;
    }
    return 0;
}

/*
 * Convert a buffer of raw 16-bit signed linear PCM to a buffer
 * of 4-bit ADPCM packed two to a byte (Big Endian).
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  Leftover inbuf data gets packed, tail gets updated.
 */
static struct cw_frame *lintookiadpcm_frameout(void *pvt)
{
    struct oki_adpcm_encoder_pvt *tmp = (struct oki_adpcm_encoder_pvt *) pvt;
    int i_max;
    int enc_len;
  
    if (tmp->tail < 2)
        return NULL;

    i_max = tmp->tail & ~1; /* atomic size is 2 samples */
    enc_len = oki_adpcm_encode(&tmp->oki_state, tmp->outbuf, tmp->inbuf, i_max);
    cw_fr_init_ex(&tmp->f, CW_FRAME_VOICE, CW_FORMAT_OKI_ADPCM);
    tmp->f.samples = i_max;
    tmp->f.offset = CW_FRIENDLY_OFFSET;
    tmp->f.data = tmp->outbuf;
    tmp->f.datalen = enc_len;

    /*
     * If there is a signal left over (there should be no more than
     * one) move it to the beginning of the input buffer.
     */
    if (tmp->tail == i_max)
    {
        tmp->tail = 0;
    }
    else
    {
        tmp->inbuf[0] = tmp->inbuf[tmp->tail];
        tmp->tail = 1;
    }
    return &tmp->f;
}

static struct cw_frame *okiadpcmtolin_sample(int *i)
{
    static struct cw_frame f;
  
    CW_UNUSED(i);

    cw_fr_init_ex(&f, CW_FRAME_VOICE, CW_FORMAT_OKI_ADPCM);
    f.datalen = sizeof (adpcm_ex);
    f.samples = sizeof(adpcm_ex)*2;
    f.data = adpcm_ex;
    return &f;
}

/*
 * The complete translator for okiadpcmtoLin.
 */
static cw_translator_t okiadpcmtolin =
{
    .name = "okiadpcmtolin",
    .src_format = CW_FORMAT_OKI_ADPCM,
    .src_rate = 8000,
    .dst_format = CW_FORMAT_SLINEAR,
    .dst_rate = 8000,
    .newpvt = okiadpcmtolin_new,
    .framein = okiadpcmtolin_framein,
    .frameout = okiadpcmtolin_frameout,
    .destroy = free,
    .sample = okiadpcmtolin_sample
};

/*
 * The complete translator for Lintookiadpcm.
 */
static cw_translator_t lintookiadpcm =
{
    .name = "lintookiadpcm",
    .src_format = CW_FORMAT_SLINEAR,
    .src_rate = 8000,
    .dst_format = CW_FORMAT_OKI_ADPCM,
    .dst_rate = 8000,
    .newpvt = lintookiadpcm_new,
    .framein = lintookiadpcm_framein,
    .frameout = lintookiadpcm_frameout,
    .destroy = free,
    .sample = cw_translate_linear_sample
};

static void parse_config(void)
{
    struct cw_config *cfg;
    struct cw_variable *var;
  
    if ((cfg = cw_config_load("codecs.conf")))
    {
        if ((var = cw_variable_browse(cfg, "plc")))
        {
            while (var)
            {
                if (!strcasecmp(var->name, "genericplc"))
                {
                    useplc = cw_true(var->value) ? 1 : 0;
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_3 "codec_adpcm: %susing generic PLC\n", useplc ? "" : "not ");
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
    cw_translator_unregister(&okiadpcmtolin);
    cw_translator_unregister(&lintookiadpcm);
    return 0;
}

static int load_module(void)
{
    parse_config();
    cw_translator_register(&okiadpcmtolin);
    cw_translator_register(&lintookiadpcm);
    return 0;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, "Oki 32kbps ADPCM to/from PCM16 translator")
