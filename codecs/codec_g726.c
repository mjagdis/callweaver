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
 * \brief codec_g726.c - translate between signed linear and ITU G.726-32kbps
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

OPBX_MUTEX_DEFINE_STATIC(localuser_lock);
static int localusecnt = 0;

static const char tdesc[] = "ITU G.726-32kbps G726 to/from PCM16 translator";

static int useplc = 0;

/* Sample frame data */

/* Sample 10ms of linear frame data */
static const int16_t slin_ex[] =
{
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

/* 10ms of G.726 at 32kbps */
static const uint8_t g726_ex[] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*
 * Private workspace for translating signed linear signals to G726.
 */
struct g726_encoder_pvt
{
    struct opbx_frame f;
    uint8_t offset[OPBX_FRIENDLY_OFFSET];   /* Space to build offset */
    uint8_t outbuf[BUFFER_SIZE];  /* Encoded G726, two nibbles to a word */
    uint8_t next_flag;
    g726_state_t g726_state;
    int tail;
};

/*
 * Private workspace for translating G726 signals to signed linear.
 */
struct g726_decoder_pvt
{
    struct opbx_frame f;
    uint8_t offset[OPBX_FRIENDLY_OFFSET];    /* Space to build offset */
    int16_t outbuf[BUFFER_SIZE];    /* Decoded signed linear values */
    g726_state_t g726_state;
    int tail;
    plc_state_t plc;
};

/*
 *  Create a new instance of g726_decoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */
static struct opbx_translator_pvt *g726tolin_new(void)
{
    struct g726_decoder_pvt *tmp;
  
    if ((tmp = malloc(sizeof (struct g726_decoder_pvt))) == NULL)
        return NULL;
    memset(tmp, 0, sizeof(*tmp));
    g726_init(&(tmp->g726_state), 32000, G726_ENCODING_LINEAR, G726_PACKING_LEFT);
    plc_init(&tmp->plc);
    localusecnt++;
    return (struct opbx_translator_pvt *) tmp;
}

/*
 *  Create a new instance of g726_encoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */

static struct opbx_translator_pvt *lintog726_new(void)
{
    struct g726_encoder_pvt *tmp;
  
    if ((tmp = malloc(sizeof (struct g726_encoder_pvt))) == NULL)
        return NULL;
    memset(tmp, 0, sizeof(*tmp));
    g726_init(&(tmp->g726_state), 32000, G726_ENCODING_LINEAR, G726_PACKING_LEFT);
    localusecnt++;
    return (struct opbx_translator_pvt *) tmp;
}

/*
 *  Fill an input buffer with packed 4-bit G726 values if there is room
 *  left.
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  tmp->tail is the number of packed values in the buffer.
 */
static int g726tolin_framein(struct opbx_translator_pvt *pvt, struct opbx_frame *f)
{
    struct g726_decoder_pvt *tmp = (struct g726_decoder_pvt *) pvt;

    if (f->datalen == 0)
    {
        /* Perform PLC with nominal framesize of 20ms/160 samples */
        if ((tmp->tail + 160) > BUFFER_SIZE)
        {
            opbx_log(OPBX_LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
        if (useplc)
        {
            plc_fillin(&tmp->plc, tmp->outbuf + tmp->tail, 160);
            tmp->tail += 160;
        }
    }
    else
    {
        if ((tmp->tail + f->datalen*2) > BUFFER_SIZE)
        {
            opbx_log(OPBX_LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
        tmp->tail += g726_decode(&(tmp->g726_state),
                                 tmp->outbuf + tmp->tail,
                                 (const uint8_t *) f->data,
                                 f->datalen);
        if (useplc)
            plc_rx(&tmp->plc, tmp->outbuf + tmp->tail - f->datalen*2, f->datalen*2);
    }
    return 0;
}

/*
 *  Convert 4-bit G726 encoded signals to 16-bit signed linear.
 *
 * Results:
 *  Converted signals are placed in tmp->f.data, tmp->f.datalen
 *  and tmp->f.samples are calculated.
 *
 * Side effects:
 *  None.
 */
static struct opbx_frame *g726tolin_frameout(struct opbx_translator_pvt *pvt)
{
    struct g726_decoder_pvt *tmp = (struct g726_decoder_pvt *) pvt;

    if (tmp->tail == 0)
        return NULL;

    opbx_fr_init_ex(&tmp->f, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, __PRETTY_FUNCTION__);
    tmp->f.datalen = tmp->tail*2;
    tmp->f.samples = tmp->tail;
    tmp->f.offset = OPBX_FRIENDLY_OFFSET;
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
static int lintog726_framein(struct opbx_translator_pvt *pvt, struct opbx_frame *f)
{
    struct g726_encoder_pvt *tmp = (struct g726_encoder_pvt *) pvt;
  
    if ((tmp->tail + f->datalen/(2*sizeof(int16_t)) + 1) > BUFFER_SIZE)
    {
        opbx_log(OPBX_LOG_WARNING, "Out of buffer space\n");
        return -1;
    }
    tmp->tail += g726_encode(&(tmp->g726_state),
                             tmp->outbuf + tmp->tail,
                             f->data,
                             f->datalen/sizeof(int16_t));
    return 0;
}

/*
 *  Convert a buffer of raw 16-bit signed linear PCM to a buffer
 *  of 4-bit G726 packed two to a byte (Big Endian).
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  Leftover inbuf data gets packed, tail gets updated.
 */
static struct opbx_frame *lintog726_frameout(struct opbx_translator_pvt *pvt)
{
    struct g726_encoder_pvt *tmp = (struct g726_encoder_pvt *) pvt;
  
    if (tmp->tail == 0)
        return NULL;
    opbx_fr_init_ex(&tmp->f, OPBX_FRAME_VOICE, OPBX_FORMAT_G726, __PRETTY_FUNCTION__);
    tmp->f.samples = tmp->tail*2;
    tmp->f.offset = OPBX_FRIENDLY_OFFSET;
    tmp->f.data = tmp->outbuf;
    tmp->f.datalen = tmp->tail;

    tmp->tail = 0;
    return &tmp->f;
}

static struct opbx_frame *g726tolin_sample(void)
{
    static struct opbx_frame f;
 
    opbx_fr_init_ex(&f, OPBX_FRAME_VOICE, OPBX_FORMAT_G726, __PRETTY_FUNCTION__);
    f.datalen = sizeof(g726_ex);
    f.samples = sizeof(g726_ex)*2;
    f.data = (uint8_t *) g726_ex;
    return &f;
}

static struct opbx_frame *lintog726_sample(void)
{
    static struct opbx_frame f;
  
    opbx_fr_init_ex(&f, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, __PRETTY_FUNCTION__);
    f.datalen = sizeof (slin_ex);
    /* Assume 8000 Hz */
    f.samples = sizeof (slin_ex)/sizeof(int16_t);
    f.data = (int16_t *) slin_ex;
    return &f;
}

/*
 *  Destroys a private workspace.
 *
 * Results:
 *  It's gone!
 *
 * Side effects:
 *  None.
 */
static void g726_destroy(struct opbx_translator_pvt *pvt)
{
    free(pvt);
    localusecnt--;
}

/*
 * The complete translator for g726tolin.
 */
static opbx_translator_t g726tolin =
{
    .name = "g726tolin",
    .src_format = OPBX_FORMAT_G726,
    .src_rate = 8000,
    .dst_format = OPBX_FORMAT_SLINEAR,
    .dst_rate = 8000,
    .newpvt = g726tolin_new,
    .framein = g726tolin_framein,
    .frameout = g726tolin_frameout,
    .destroy = g726_destroy,
    .sample = g726tolin_sample
};

/*
 * The complete translator for lintog726.
 */
static opbx_translator_t lintog726 =
{
    .name = "lintog726",
    .src_format = OPBX_FORMAT_SLINEAR,
    .src_rate = 8000,
    .dst_format = OPBX_FORMAT_G726,
    .dst_rate = 8000,
    .newpvt = lintog726_new,
    .framein = lintog726_framein,
    .frameout = lintog726_frameout,
    .destroy = g726_destroy,
    .sample = lintog726_sample
};

static void parse_config(void)
{
    struct opbx_config *cfg;
    struct opbx_variable *var;
  
    if ((cfg = opbx_config_load("codecs.conf")))
    {
        if ((var = opbx_variable_browse(cfg, "plc")))
        {
            while (var)
            {
                if (!strcasecmp(var->name, "genericplc"))
                {
                    useplc = opbx_true(var->value)  ?  1  :  0;
                    if (option_verbose > 2)
                        opbx_verbose(VERBOSE_PREFIX_3 "codec_g726: %susing generic PLC\n", useplc  ?  ""  :  "not ");
                }
                var = var->next;
            }
        }
        opbx_config_destroy(cfg);
    }
}

static int reload_module(void)
{
    parse_config();
    return 0;
}

static int unload_module(void)
{
    int res = 0;
    
    opbx_mutex_lock(&localuser_lock);
    if (localusecnt)
        res = -1;
    opbx_mutex_unlock(&localuser_lock);
    opbx_translator_unregister(&g726tolin);
    opbx_translator_unregister(&lintog726);
    return res;
}

static int load_module(void)
{
    int res = 0;
 
    parse_config();
    opbx_translator_register(&g726tolin);
    opbx_translator_register(&lintog726);
    return res;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, tdesc)
