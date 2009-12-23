/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Based on frompcm.c and topcm.c from the Emiliano MIPL browser/
 * interpreter.  See http://www.bsdtelephony.com.mx
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
 * \brief codec_g722.c - translate between signed linear and ITU G.722
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

#define BUFFER_SIZE     8096    /* size for the translation buffers */
#define BUF_SHIFT       5


static int useplc = 0;

/* Sample frame data */

/* 10ms of G.722 at 64kbps */
static const uint8_t g722_ex[] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*
 * Private workspace for translating signed linear signals to G722.
 */
struct g722_encoder_pvt
{
    struct cw_frame f;
    uint8_t offset[CW_FRIENDLY_OFFSET];   /* Space to build offset */
    uint8_t outbuf[BUFFER_SIZE];            /* Encoded G722 */
    uint8_t next_flag;
    g722_encode_state_t g722_state;
    int tail;
};

/*
 * Private workspace for translating G722 signals to signed linear.
 */
struct g722_decoder_pvt
{
    struct cw_frame f;
    uint8_t offset[CW_FRIENDLY_OFFSET];   /* Space to build offset */
    int16_t outbuf[BUFFER_SIZE];            /* Decoded signed linear values */
    g722_decode_state_t g722_state;
    int tail;
    plc_state_t plc;
};

/*
 *  Create a new instance of g722_decoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */

static void *g722tolin_new(void)
{
    struct g722_decoder_pvt *tmp;
  
    if ((tmp = malloc(sizeof (struct g722_decoder_pvt))) == NULL)
        return NULL;
    memset(tmp, 0, sizeof(*tmp));
    g722_decode_init(&(tmp->g722_state), 64000, G722_PACKED);
    plc_init(&tmp->plc);
    return tmp;
}

/*
 *  Create a new instance of g722_encoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */

static void *lintog722_new(void)
{
    struct g722_encoder_pvt *tmp;
  
    if ((tmp = malloc(sizeof (struct g722_encoder_pvt))) == NULL)
        return NULL;
    memset(tmp, 0, sizeof(*tmp));
    g722_encode_init(&(tmp->g722_state), 64000, G722_PACKED);
    return tmp;
}

/*
 *  Fill an input buffer with G722 values if there is room left.
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  tmp->tail is the number of packed values in the buffer.
 */

static int g722tolin_framein(void *pvt, struct cw_frame *f)
{
    struct g722_decoder_pvt *tmp = (struct g722_decoder_pvt *) pvt;

    if (f->datalen == 0)
    {
        /* Perform PLC with nominal framesize of 20ms/320 samples */
        if ((tmp->tail + 320) > BUFFER_SIZE)
        {
            cw_log(CW_LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
        if (useplc)
        {
            plc_fillin(&tmp->plc, tmp->outbuf + tmp->tail, 320);
            tmp->tail += 320;
        }
    }
    else
    {
        if ((tmp->tail + f->datalen*2) > BUFFER_SIZE)
        {
            cw_log(CW_LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
        tmp->tail += g722_decode(&(tmp->g722_state),
                                 tmp->outbuf + tmp->tail,
                                 (const uint8_t *) f->data,
                                 f->datalen);
        if (useplc)
            plc_rx(&tmp->plc, tmp->outbuf + tmp->tail - f->datalen*2, f->datalen*2);
    }
    return 0;
}

/*
 *  Convert G722 encoded signals to 16-bit signed linear.
 *
 * Results:
 *  Converted signals are placed in tmp->f.data, tmp->f.datalen
 *  and tmp->f.samples are calculated.
 *
 * Side effects:
 *  None.
 */

static struct cw_frame *g722tolin_frameout(void *pvt)
{
    struct g722_decoder_pvt *tmp = (struct g722_decoder_pvt *) pvt;

    if (tmp->tail == 0)
        return NULL;

    cw_fr_init_ex(&tmp->f, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
    tmp->f.datalen = tmp->tail*2;
    tmp->f.samples = tmp->tail;
    tmp->f.offset = CW_FRIENDLY_OFFSET;
    tmp->f.data = tmp->outbuf;

    tmp->tail = 0;
    return &tmp->f;
}

/*
 *  Fill an input buffer with 16-bit signed linear PCM values.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  tmp->tail is number of signal values in the input buffer.
 */

static int lintog722_framein(void *pvt, struct cw_frame *f)
{
    struct g722_encoder_pvt *tmp = (struct g722_encoder_pvt *) pvt;
  
    if ((tmp->tail + f->datalen/(2*sizeof(int16_t)) + 1) > BUFFER_SIZE)
    {
        cw_log(CW_LOG_WARNING, "Out of buffer space\n");
        return -1;
    }
    tmp->tail += g722_encode(&(tmp->g722_state),
                             tmp->outbuf + tmp->tail,
                             f->data,
                             f->datalen/sizeof(int16_t));
    return 0;
}

/*
 *  Convert a buffer of raw 16-bit signed linear PCM to a buffer of G722.
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  Leftover inbuf data gets packed, tail gets updated.
 */

static struct cw_frame *lintog722_frameout(void *pvt)
{
    struct g722_encoder_pvt *tmp = (struct g722_encoder_pvt *) pvt;
  
    if (tmp->tail == 0)
        return NULL;
    cw_fr_init_ex(&tmp->f, CW_FRAME_VOICE, CW_FORMAT_G722);
    tmp->f.samples = tmp->tail*2;
    tmp->f.offset = CW_FRIENDLY_OFFSET;
    tmp->f.data = tmp->outbuf;
    tmp->f.datalen = tmp->tail;

    tmp->tail = 0;
    return &tmp->f;
}

static struct cw_frame *g722tolin_sample(int *i)
{
    static struct cw_frame f;
 
    cw_fr_init_ex(&f, CW_FRAME_VOICE, CW_FORMAT_G722);
    f.datalen = sizeof(g722_ex);
    f.samples = sizeof(g722_ex)*2;
    f.data = (uint8_t *) g722_ex;
    return &f;
}

/*
 * The complete translator for g722tolin.
 */
static cw_translator_t g722tolin =
{
    .name = "g722tolin",
    .src_format = CW_FORMAT_G722,
    .src_rate = 16000,
    .dst_format = CW_FORMAT_SLINEAR,
    .dst_rate = 16000,
    .newpvt = g722tolin_new,
    .framein = g722tolin_framein,
    .frameout = g722tolin_frameout,
    .destroy = free,
    .sample = g722tolin_sample
};

/*
 * The complete translator for lintog722.
 */
static cw_translator_t lintog722 =
{
    .name = "lintog722",
    .src_format = CW_FORMAT_SLINEAR,
    .src_rate = 16000,
    .dst_format = CW_FORMAT_G722,
    .dst_rate = 16000,
    .newpvt = lintog722_new,
    .framein = lintog722_framein,
    .frameout = lintog722_frameout,
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
                        cw_verbose(VERBOSE_PREFIX_3 "codec_g722: %susing generic PLC\n", useplc  ?  ""  :  "not ");
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
    cw_translator_unregister(&g722tolin);
    cw_translator_unregister(&lintog722);
    return 0;
}

static int load_module(void)
{
    parse_config();
    cw_translator_register(&g722tolin);
    cw_translator_register(&lintog722);
    return 0;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, "ITU G.722 to/from PCM16 translator")
