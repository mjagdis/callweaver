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
 * \brief codec_alaw.c - translate between signed linear and alaw
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

#include "callweaver/callweaver_pcm.h"

#include "core/translate.h"
#include "callweaver/translate.h"

#define BUFFER_SIZE   8096    /* size for the translation buffers */


static int useplc = 0;

/* Sample frame data (re-using the Mu data is okay) */

/* Sample 10ms of linear frame data */
static int16_t slin_ulaw_ex[] =
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

/* Sample 10ms of alaw frame data */
static uint8_t alaw_slin_ex[] =
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

/*!
 * \brief Private workspace for translating signed linear signals to alaw.
 */
struct alaw_encoder_pvt
{
    struct cw_frame f;
    char offset[CW_FRIENDLY_OFFSET];   /*!< Space to build offset */
    unsigned char outbuf[BUFFER_SIZE];  /*!< Encoded alaw, two nibbles to a word */
    int tail;
};

/*!
 * \brief Private workspace for translating alaw signals to signed linear.
 */
struct alaw_decoder_pvt
{
    struct cw_frame f;
    char offset[CW_FRIENDLY_OFFSET];    /*!< Space to build offset */
    int16_t outbuf[BUFFER_SIZE];    /*!< Decoded signed linear values */
    int tail;
    plc_state_t plc;
};

/*!
 * \brief alawtolin_new
 *  Create a new instance of alaw_decoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */
static void *alawtolin_new(void)
{
    struct alaw_decoder_pvt *tmp;
  
    if ((tmp = malloc(sizeof(struct alaw_decoder_pvt))) == NULL)
        return NULL;
    memset(tmp, 0, sizeof(*tmp));
    tmp->tail = 0;
    plc_init(&tmp->plc);
    return tmp;
}

/*!
 * \brief lintoalaw_new
 *  Create a new instance of alaw_encoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */
static void *lintoalaw_new(void)
{
    struct alaw_encoder_pvt *tmp;
  
    if ((tmp = malloc(sizeof(struct alaw_encoder_pvt))) == NULL)
        return NULL;
    memset(tmp, 0, sizeof(*tmp));
    return tmp;
}

/*!
 * \brief alawtolin_framein
 *  Fill an input buffer with packed 4-bit alaw values if there is room
 *  left.
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  tmp->tail is the number of packed values in the buffer.
 */
static int alawtolin_framein(void *pvt, struct cw_frame *f)
{
    struct alaw_decoder_pvt *tmp = (struct alaw_decoder_pvt *) pvt;
    int x;
    unsigned char *b;

    if (f->datalen == 0) {
        /* perform PLC with nominal framesize of 20ms/160 samples */
        if ((tmp->tail + 160)*sizeof(int16_t) > sizeof(tmp->outbuf)) {
            cw_log(CW_LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
        if (useplc) {
            plc_fillin(&tmp->plc, tmp->outbuf+tmp->tail, 160);
            tmp->tail += 160;
        }
        return 0;
    }

    if ((tmp->tail + f->datalen)*sizeof(int16_t) > sizeof(tmp->outbuf)) {
        cw_log(CW_LOG_WARNING, "Out of buffer space\n");
        return -1;
    }

    /* Reset ssindex and signal to frame's specified values */
    b = f->data;
    for (x = 0;  x < f->datalen;  x++)
        tmp->outbuf[tmp->tail + x] = CW_ALAW(b[x]);

    if (useplc)
        plc_rx(&tmp->plc, tmp->outbuf+tmp->tail, f->datalen);

    tmp->tail += f->datalen;
    return 0;
}

/*!
 * \brief alawtolin_frameout
 *  Convert 4-bit alaw encoded signals to 16-bit signed linear.
 *
 * Results:
 *  Converted signals are placed in tmp->f.data, tmp->f.datalen
 *  and tmp->f.samples are calculated.
 *
 * Side effects:
 *  None.
 */
static struct cw_frame *alawtolin_frameout(void *pvt)
{
    struct alaw_decoder_pvt *tmp = (struct alaw_decoder_pvt *) pvt;

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

/*!
 * \brief lintoalaw_framein
 *  Fill an input buffer with 16-bit signed linear PCM values.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  tmp->tail is number of signal values in the input buffer.
 */
static int lintoalaw_framein(void *pvt, struct cw_frame *f)
{
    struct alaw_encoder_pvt *tmp = (struct alaw_encoder_pvt *) pvt;
    int x;
    int16_t *s;
  
    if (tmp->tail + f->datalen/sizeof(int16_t) >= sizeof(tmp->outbuf))
    {
        cw_log(CW_LOG_WARNING, "Out of buffer space\n");
        return -1;
    }
    s = f->data;
    for (x = 0;  x < f->datalen/sizeof(int16_t);  x++) 
        tmp->outbuf[x + tmp->tail] = CW_LIN2A(s[x]);
    tmp->tail += f->datalen/sizeof(int16_t);
    return 0;
}

/*!
 * \brief lintoalaw_frameout
 *  Convert a buffer of raw 16-bit signed linear PCM to a buffer
 *  of 4-bit alaw packed two to a byte (Big Endian).
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  Leftover inbuf data gets packed, tail gets updated.
 */
static struct cw_frame *lintoalaw_frameout(void *pvt)
{
    struct alaw_encoder_pvt *tmp = (struct alaw_encoder_pvt *) pvt;
  
    if (tmp->tail == 0)
        return NULL;

    cw_fr_init_ex(&tmp->f, CW_FRAME_VOICE, CW_FORMAT_ALAW);
    tmp->f.samples = tmp->tail;
    tmp->f.offset = CW_FRIENDLY_OFFSET;
    tmp->f.data = tmp->outbuf;
    tmp->f.datalen = tmp->tail;

    tmp->tail = 0;
    return &tmp->f;
}

/*!
 * \brief alawtolin_sample
 */
static struct cw_frame *alawtolin_sample(void)
{
    static struct cw_frame f;
  
    cw_fr_init_ex(&f, CW_FRAME_VOICE, CW_FORMAT_ALAW);
    f.datalen = sizeof(alaw_slin_ex);
    f.samples = sizeof(alaw_slin_ex);
    f.data = alaw_slin_ex;
    return &f;
}

/*!
 * \brief lintoalaw_sample
 */
static struct cw_frame *lintoalaw_sample(void)
{
    static struct cw_frame f;
  
    cw_fr_init_ex(&f, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
    f.datalen = sizeof(slin_ulaw_ex);
    /* Assume 8000 Hz */
    f.samples = sizeof(slin_ulaw_ex)/sizeof(int16_t);
    f.data = slin_ulaw_ex;
    return &f;
}

/*!
 * \brief The complete translator for alawtolin.
 */
static cw_translator_t alawtolin =
{
    .name = "alawtolin",
    .src_format = CW_FORMAT_ALAW,
    .src_rate = 8000,
    .dst_format = CW_FORMAT_SLINEAR,
    .dst_rate = 8000,
    .newpvt = alawtolin_new,
    .framein = alawtolin_framein,
    .frameout = alawtolin_frameout,
    .destroy = free,
    .sample = alawtolin_sample
};

/*!
 * \brief The complete translator for lintoalaw.
 */
static cw_translator_t lintoalaw =
{
    .name = "lintoalaw",
    .src_format = CW_FORMAT_SLINEAR,
    .src_rate = 8000,
    .dst_format = CW_FORMAT_ALAW,
    .dst_rate = 8000,
    .newpvt = lintoalaw_new,
    .framein = lintoalaw_framein,
    .frameout = lintoalaw_frameout,
    .destroy = free,
    .sample = lintoalaw_sample
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
                    useplc = cw_true(var->value)  ?  1  :  0;
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_3 "codec_alaw: %susing generic PLC\n", useplc  ?  ""  :  "not ");
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
    cw_translator_unregister(&alawtolin);
    cw_translator_unregister(&lintoalaw);
    return 0;
}

static int load_module(void)
{
    parse_config();
    cw_translator_register(&alawtolin);
    cw_translator_register(&lintoalaw);
    return 0;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, "A-law to/from PCM16 translator");
