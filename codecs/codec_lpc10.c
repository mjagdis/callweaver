/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * The lpc10 code is from a library used by nautilus, modified to be a bit
 * nicer to the compiler.
 * See http://www.arl.wustl.edu/~jaf/ 
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
 * \brief Translate between signed linear and LPC10 (Linear Predictor Code)
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/config.h"
#include "callweaver/options.h"
#include "callweaver/module.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"

#include "core/translate.h"
#include "callweaver/translate.h"

/* Sample 22.5ms of linear frame data */
static int16_t slin_ex[] =
{
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

/* Sample frame of LPC10 data */
static uint8_t lpc10_ex[] =
{
    0x1, 0x8, 0x31, 0x8, 0x31, 0x80, 0x30
};

#define LPC10_BYTES_IN_COMPRESSED_FRAME (LPC10_BITS_IN_COMPRESSED_FRAME + 7)/8

OPBX_MUTEX_DEFINE_STATIC(localuser_lock);

static int localusecnt = 0;

static const char tdesc[] = "LPC10e to/from PCM16 (signed linear) translator";

static int useplc = 0;

struct opbx_translator_pvt
{
    union
    {
        lpc10_encode_state_t *enc;
        lpc10_decode_state_t *dec;
    } lpc10;
    struct opbx_frame f;
    /* Space to build offset */
    char offset[OPBX_FRIENDLY_OFFSET];
    /* Buffer for our outgoing frame */
    int16_t outbuf[8000];
    /* Enough to store a full second */
    int16_t buf[8000];
    int tail;
    int longer;
    plc_state_t plc;
};

#define lpc10_coder_pvt opbx_translator_pvt

static struct opbx_translator_pvt *lpc10_enc_new(void)
{
    struct lpc10_coder_pvt *tmp;

    if ((tmp = malloc(sizeof(struct lpc10_coder_pvt))) == NULL)
        return NULL;
    if ((tmp->lpc10.enc = lpc10_encode_init(NULL, FALSE)) == NULL)
    {
        free(tmp);
        return NULL;
    }
    tmp->tail = 0;
    tmp->longer = 0;
    localusecnt++;
    return tmp;
}

static struct opbx_translator_pvt *lpc10_dec_new(void)
{
    struct lpc10_coder_pvt *tmp;

    if ((tmp = malloc(sizeof(struct lpc10_coder_pvt))) == NULL)
        return NULL;
    if ((tmp->lpc10.dec = lpc10_decode_init(NULL, FALSE)) == NULL)
    {
        free(tmp);
        return NULL;
    }
    tmp->tail = 0;
    tmp->longer = 0;
    plc_init(&tmp->plc);
    localusecnt++;
    return tmp;
}

static struct opbx_frame *lintolpc10_sample(void)
{
    static struct opbx_frame f;

    opbx_fr_init_ex(&f, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, __PRETTY_FUNCTION__);
    f.datalen = sizeof(slin_ex);
    /* Assume 8000 Hz */
    f.samples = sizeof(slin_ex)/sizeof(int16_t);
    f.data = slin_ex;
    return &f;
}

static struct opbx_frame *lpc10tolin_sample(void)
{
    static struct opbx_frame f;

    opbx_fr_init_ex(&f, OPBX_FRAME_VOICE, OPBX_FORMAT_LPC10, __PRETTY_FUNCTION__);
    f.datalen = sizeof(lpc10_ex);
    /* All frames are 22 ms long (maybe a little more -- why did he choose
       LPC10_SAMPLES_PER_FRAME sample frames anyway?? */
    f.samples = LPC10_SAMPLES_PER_FRAME;
    f.data = lpc10_ex;
    return &f;
}

static struct opbx_frame *lpc10tolin_frameout(struct opbx_translator_pvt *tmp)
{
    if (tmp->tail == 0)
        return NULL;

    /* Signed linear is no particular frame size, so just send whatever
       we have in the buffer in one lump sum */
    opbx_fr_init_ex(&tmp->f, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, __PRETTY_FUNCTION__);
    tmp->f.datalen = tmp->tail*sizeof(int16_t);
    /* Assume 8000 Hz */
    tmp->f.samples = tmp->tail;
    tmp->f.offset = OPBX_FRIENDLY_OFFSET;
    tmp->f.data = tmp->buf;
    /* Reset tail pointer */
    tmp->tail = 0;

    return &tmp->f;    
}

static int lpc10tolin_framein(struct opbx_translator_pvt *tmp, struct opbx_frame *f)
{
    /* Assuming there's space left, decode into the current buffer at
       the tail location */
    int len = 0;
    int16_t *sd;

    if (f->datalen == 0)
    {
        /* Perform PLC with nominal framesize of LPC10_SAMPLES_PER_FRAME */
        if ((tmp->tail + LPC10_SAMPLES_PER_FRAME) > sizeof(tmp->buf)/sizeof(int16_t))
        {
            opbx_log(LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
        if (useplc)
        {
            plc_fillin(&tmp->plc, tmp->buf+tmp->tail, LPC10_SAMPLES_PER_FRAME);
            tmp->tail += LPC10_SAMPLES_PER_FRAME;
        }
        return 0;
    }

    while (len + LPC10_BYTES_IN_COMPRESSED_FRAME <= f->datalen)
    {
        if (tmp->tail + LPC10_SAMPLES_PER_FRAME >= sizeof(tmp->buf)/sizeof(int16_t))
        {
            opbx_log(LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
        sd = tmp->buf + tmp->tail;
        if (lpc10_decode(tmp->lpc10.dec, sd, f->data + len, 1) < LPC10_SAMPLES_PER_FRAME)
        {
            opbx_log(LOG_WARNING, "Invalid lpc10 data\n");
            return -1;
        }
        if (useplc)
            plc_rx(&tmp->plc, tmp->buf + tmp->tail, LPC10_SAMPLES_PER_FRAME);
            
        tmp->tail += LPC10_SAMPLES_PER_FRAME;
        len += LPC10_BYTES_IN_COMPRESSED_FRAME;
    }
    if (len != f->datalen) 
        opbx_log(LOG_WARNING, "Decoded %d, expected %d\n", len, f->datalen);
    return 0;
}

static int lintolpc10_framein(struct opbx_translator_pvt *tmp, struct opbx_frame *f)
{
    /* Just add the frames to our stream */
    /* XXX We should look at how old the rest of our stream is, and if it
       is too old, then we should overwrite it entirely, otherwise we can
       get artifacts of earlier talk that do not belong */
    if (tmp->tail + f->datalen > sizeof(tmp->buf)/sizeof(int16_t))
    {
        opbx_log(LOG_WARNING, "Out of buffer space\n");
        return -1;
    }
    memcpy((tmp->buf + tmp->tail), f->data, f->datalen);
    tmp->tail += f->datalen/2;
    return 0;
}

static struct opbx_frame *lintolpc10_frameout(struct opbx_translator_pvt *tmp)
{
    int consumed = 0;

    /* We can't work on anything less than a frame in size */
    if (tmp->tail < LPC10_SAMPLES_PER_FRAME)
        return NULL;
    /* Start with an empty frame */
    opbx_fr_init_ex(&tmp->f, OPBX_FRAME_VOICE, OPBX_FORMAT_LPC10, __PRETTY_FUNCTION__);
    while (tmp->tail >=  LPC10_SAMPLES_PER_FRAME)
    {
        if (tmp->f.datalen + LPC10_BYTES_IN_COMPRESSED_FRAME > sizeof(tmp->outbuf))
        {
            opbx_log(LOG_WARNING, "Out of buffer space\n");
            return NULL;
        }
        /* Encode a frame of data */
        lpc10_encode(tmp->lpc10.enc, ((uint8_t *)tmp->outbuf) + tmp->f.datalen, &tmp->buf[consumed], 1);
        tmp->f.datalen += LPC10_BYTES_IN_COMPRESSED_FRAME;
        tmp->f.samples += LPC10_SAMPLES_PER_FRAME;
        /* Use one of the two left over bits to record if this is a 22 or 23 ms frame...
           important for IAX use */
        tmp->longer = 1 - tmp->longer;
#if 0
        /* What the heck was this for? */
        ((char *)(tmp->f.data))[consumed - 1] |= tmp->longer;
#endif        
        tmp->tail -= LPC10_SAMPLES_PER_FRAME;
        consumed += LPC10_SAMPLES_PER_FRAME;
    }
    tmp->f.mallocd = 0;
    tmp->f.offset = OPBX_FRIENDLY_OFFSET;
    tmp->f.src = __PRETTY_FUNCTION__;
    tmp->f.data = tmp->outbuf;
    /* Move the data at the end of the buffer to the front */
    if (tmp->tail)
        memmove(tmp->buf, tmp->buf + consumed, tmp->tail*sizeof(int16_t));
    return &tmp->f;    
}

static void lpc10_destroy(struct opbx_translator_pvt *pvt)
{
    /* TODO: This makes assumptions about what happens in the LPC10 code */
    if (pvt->lpc10.enc)
        lpc10_encode_release(pvt->lpc10.enc);
    free(pvt);
    localusecnt--;
}

static opbx_translator_t lpc10tolin =
{
    .name = "lpc10tolin", 
    .src_format = OPBX_FORMAT_LPC10,
    .src_rate = 8000,
    .dst_format = OPBX_FORMAT_SLINEAR,
    .dst_rate = 8000,
    .newpvt = lpc10_dec_new,
    .framein = lpc10tolin_framein,
    .frameout = lpc10tolin_frameout,
    .destroy = lpc10_destroy,
    .sample = lpc10tolin_sample
};

static opbx_translator_t lintolpc10 =
{
    .name = "lintolpc10", 
    .src_format = OPBX_FORMAT_SLINEAR,
    .src_rate = 8000,
    .dst_format = OPBX_FORMAT_LPC10,
    .dst_rate = 8000,
    .newpvt = lpc10_enc_new,
    .framein = lintolpc10_framein,
    .frameout = lintolpc10_frameout,
    .destroy = lpc10_destroy,
    .sample = lintolpc10_sample
};

static void parse_config(void)
{
        struct opbx_config *cfg;
        struct opbx_variable *var;
        if ((cfg = opbx_config_load("codecs.conf"))) {
                if ((var = opbx_variable_browse(cfg, "plc"))) {
                        while (var) {
                               if (!strcasecmp(var->name, "genericplc")) {
                                       useplc = opbx_true(var->value) ? 1 : 0;
                                       if (option_verbose > 2)
                                               opbx_verbose(VERBOSE_PREFIX_3 "codec_lpc10: %susing generic PLC\n", useplc ? "" : "not ");
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
    opbx_translator_unregister(&lpc10tolin);
    opbx_translator_unregister(&lintolpc10);
    return res;
}

static int load_module(void)
{
    int res = 0;
    parse_config();
    opbx_translator_register(&lpc10tolin);
    opbx_translator_register(&lintolpc10);
    return res;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, tdesc)
