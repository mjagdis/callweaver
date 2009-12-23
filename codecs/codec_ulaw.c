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
 * \brief codec_ulaw.c - translate between signed linear and ulaw
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

#include "callweaver/callweaver_pcm.h"

#define BUFFER_SIZE   8096    /* size for the translation buffers */


static int useplc = 0;


/* Sample 10ms of ulaw frame data */
static uint8_t ulaw_slin_ex[] =
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
 * \brief Private workspace for translating signed linear signals to ulaw.
 */
struct ulaw_encoder_pvt
{
    struct cw_frame f;
    char offset[CW_FRIENDLY_OFFSET];      /*!< Space to build offset */
    uint8_t outbuf[BUFFER_SIZE];            /*!< Encoded ulaw, two nibbles to a word */
    int tail;
};

/*!
 * \brief Private workspace for translating ulaw signals to signed linear.
 */
struct ulaw_decoder_pvt
{
    struct cw_frame f;
    char offset[CW_FRIENDLY_OFFSET];      /*!< Space to build offset */
    int16_t outbuf[BUFFER_SIZE];            /*!< Decoded signed linear values */
    int tail;
    plc_state_t plc;
};

/*!
 * \brief ulawtolin_new
 *  Create a new instance of ulaw_decoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */

static void *ulawtolin_new(void)
{
    struct ulaw_decoder_pvt *tmp;
  
    if ((tmp = malloc(sizeof(struct ulaw_decoder_pvt))) == NULL)
        return NULL;
    memset(tmp, 0, sizeof(*tmp));
    plc_init(&tmp->plc);
    return tmp;
}

/*!
 * \brief lintoulaw_new
 *  Create a new instance of ulaw_encoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */
static void *lintoulaw_new(void)
{
    struct ulaw_encoder_pvt *tmp;
  
    if ((tmp = malloc(sizeof(struct ulaw_encoder_pvt))) == NULL)
        return NULL;
    memset(tmp, 0, sizeof(*tmp));
    tmp->tail = 0;
    return tmp;
}

/*!
 * \brief ulawtolin_framein
 *  Fill an input buffer with packed 4-bit ulaw values if there is room
 *  left.
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  tmp->tail is the number of packed values in the buffer.
 */
static int ulawtolin_framein(void *pvt, struct cw_frame *f)
{
    struct ulaw_decoder_pvt *tmp = (struct ulaw_decoder_pvt *) pvt;
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
        tmp->outbuf[tmp->tail + x] = CW_MULAW(b[x]);

    if (useplc)
        plc_rx(&tmp->plc, tmp->outbuf+tmp->tail, f->datalen);

    tmp->tail += f->datalen;
    return 0;
}

/*!
 * \brief ulawtolin_frameout
 *  Convert 4-bit ulaw encoded signals to 16-bit signed linear.
 *
 * Results:
 *  Converted signals are placed in tmp->f.data, tmp->f.datalen
 *  and tmp->f.samples are calculated.
 *
 * Side effects:
 *  None.
 */
static struct cw_frame *ulawtolin_frameout(void *pvt)
{
    struct ulaw_decoder_pvt *tmp = (struct ulaw_decoder_pvt *) pvt;

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
 * \brief lintoulaw_framein
 *  Fill an input buffer with 16-bit signed linear PCM values.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  tmp->tail is number of signal values in the input buffer.
 */
static int lintoulaw_framein(void *pvt, struct cw_frame *f)
{
    struct ulaw_encoder_pvt *tmp = (struct ulaw_encoder_pvt *) pvt;
    int x;
    int16_t *s;
  
    if (tmp->tail + f->datalen/sizeof(int16_t) >= sizeof(tmp->outbuf))
    {
        cw_log(CW_LOG_WARNING, "Out of buffer space\n");
        return -1;
    }
    s = f->data;
    for (x = 0;  x < f->datalen/sizeof(int16_t);  x++) 
        tmp->outbuf[x + tmp->tail] = CW_LIN2MU(s[x]);
    tmp->tail += f->datalen/sizeof(int16_t);
    return 0;
}

/*!
 * \brief lintoulaw_frameout
 *  Convert a buffer of raw 16-bit signed linear PCM to a buffer
 *  of 4-bit ulaw packed two to a byte (Big Endian).
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  Leftover inbuf data gets packed, tail gets updated.
 */
static struct cw_frame *lintoulaw_frameout(void *pvt)
{
    struct ulaw_encoder_pvt *tmp = (struct ulaw_encoder_pvt *) pvt;
  
    if (tmp->tail == 0)
        return NULL;
    
    cw_fr_init_ex(&tmp->f, CW_FRAME_VOICE, CW_FORMAT_ULAW);
    tmp->f.samples = tmp->tail;
    tmp->f.offset = CW_FRIENDLY_OFFSET;
    tmp->f.data = tmp->outbuf;
    tmp->f.datalen = tmp->tail;

    tmp->tail = 0;
    return &tmp->f;
}

/*!
 * \brief ulawtolin_sample
 */
static struct cw_frame *ulawtolin_sample(int *i)
{
    static struct cw_frame f;
  
    cw_fr_init_ex(&f, CW_FRAME_VOICE, CW_FORMAT_ULAW);
    f.datalen = sizeof (ulaw_slin_ex);
    f.samples = sizeof(ulaw_slin_ex);
    f.data = ulaw_slin_ex;
    return &f;
}

/*!
 * \brief The complete translator for ulawtolin.
 */
static cw_translator_t ulawtolin =
{
    .name = "ulawtolin",
    .src_format = CW_FORMAT_ULAW,
    .src_rate = 8000,
    .dst_format = CW_FORMAT_SLINEAR,
    .dst_rate = 8000,
    .newpvt = ulawtolin_new,
    .framein = ulawtolin_framein,
    .frameout = ulawtolin_frameout,
    .destroy = free,
    .sample = ulawtolin_sample
};

/*!
 * \brief The complete translator for lintoulaw.
 */
static cw_translator_t lintoulaw =
{
    .name = "lintoulaw",
    .src_format = CW_FORMAT_SLINEAR,
    .src_rate = 8000,
    .dst_format = CW_FORMAT_ULAW,
    .dst_rate = 8000,
    .newpvt = lintoulaw_new,
    .framein = lintoulaw_framein,
    .frameout = lintoulaw_frameout,
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
                    useplc = cw_true(var->value)  ?  1  :  0;
                    if (option_verbose > 2)
                        cw_verbose(VERBOSE_PREFIX_3 "codec_ulaw: %susing generic PLC\n", useplc  ?  ""  :  "not ");
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
    cw_translator_unregister(&ulawtolin);
    cw_translator_unregister(&lintoulaw);
    return 0;
}

static int load_module(void)
{
    parse_config();
    cw_translator_register(&ulawtolin);
    cw_translator_register(&lintoulaw);
    return 0;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, "Mu-law to/from PCM16 translator");
