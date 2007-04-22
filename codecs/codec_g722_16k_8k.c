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
 * \brief codec_g722_16k_8k.c - translate between signed linear at 8k samples/second and ITU G.722
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

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision: 2254 $")

#include "callweaver/lock.h"
#include "callweaver/logger.h"
#include "callweaver/module.h"
#include "callweaver/config.h"
#include "callweaver/options.h"
#include "callweaver/translate.h"
#include "callweaver/channel.h"

#define BUFFER_SIZE   8096	/* size for the translation buffers */
#define BUF_SHIFT	5

OPBX_MUTEX_DEFINE_STATIC(localuser_lock);
static int localusecnt = 0;

static char *tdesc = "ITU G.722 to/from PCM16/8000 translator";

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
    struct opbx_frame f;
    uint8_t offset[OPBX_FRIENDLY_OFFSET];   /* Space to build offset */
    uint8_t outbuf[BUFFER_SIZE];  /* Encoded G722, two nibbles to a word */
    uint8_t next_flag;
    g722_encode_state_t g722_state;
    int tail;
};

/*
 * Private workspace for translating G722 signals to signed linear.
 */
struct g722_decoder_pvt
{
    struct opbx_frame f;
    uint8_t offset[OPBX_FRIENDLY_OFFSET];	/* Space to build offset */
    int16_t outbuf[BUFFER_SIZE];	/* Decoded signed linear values */
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

static struct opbx_translator_pvt *g722tolin_new(void)
{
    struct g722_decoder_pvt *tmp;
  
    if ((tmp = malloc(sizeof (struct g722_decoder_pvt))))
    {
	    memset(tmp, 0, sizeof(*tmp));
        localusecnt++;
        tmp->tail = 0;
        plc_init(&tmp->plc);
        g722_decode_init(&(tmp->g722_state), 64000, G722_PACKED | G722_SAMPLE_RATE_8000);
        opbx_update_use_count();
    }
    return (struct opbx_translator_pvt *) tmp;
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

static struct opbx_translator_pvt *lintog722_new(void)
{
    struct g722_encoder_pvt *tmp;
  
    if ((tmp = malloc(sizeof (struct g722_encoder_pvt))))
    {
	    memset(tmp, 0, sizeof(*tmp));
        localusecnt++;
        tmp->tail = 0;
        g722_encode_init(&(tmp->g722_state), 64000, G722_PACKED | G722_SAMPLE_RATE_8000);
        opbx_update_use_count();
    }
    return (struct opbx_translator_pvt *) tmp;
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

static int g722tolin_framein(struct opbx_translator_pvt *pvt, struct opbx_frame *f)
{
    struct g722_decoder_pvt *tmp = (struct g722_decoder_pvt *) pvt;

    if (f->datalen == 0)
    {
        /* Perform PLC with nominal framesize of 20ms/160 samples */
        if ((tmp->tail + 160) > BUFFER_SIZE)
        {
            opbx_log(LOG_WARNING, "Out of buffer space\n");
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
            opbx_log(LOG_WARNING, "Out of buffer space\n");
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

static struct opbx_frame *g722tolin_frameout(struct opbx_translator_pvt *pvt)
{
    struct g722_decoder_pvt *tmp = (struct g722_decoder_pvt *) pvt;

    if (!tmp->tail)
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

static int lintog722_framein(struct opbx_translator_pvt *pvt, struct opbx_frame *f)
{
    struct g722_encoder_pvt *tmp = (struct g722_encoder_pvt *) pvt;
  
    if ((tmp->tail + f->datalen/(2*sizeof(int16_t)) + 1) > BUFFER_SIZE)
    {
        opbx_log(LOG_WARNING, "Out of buffer space\n");
        return -1;
    }
    tmp->tail += g722_encode(&(tmp->g722_state),
                             tmp->outbuf + tmp->tail,
                             f->data,
                             f->datalen/sizeof(int16_t));
    return 0;
}

/*
 *  Convert a buffer of raw 16-bit signed linear PCM to a buffer
 *  of G722.
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  Leftover inbuf data gets packed, tail gets updated.
 */

static struct opbx_frame *lintog722_frameout(struct opbx_translator_pvt *pvt)
{
    struct g722_encoder_pvt *tmp = (struct g722_encoder_pvt *) pvt;
  
    if (!tmp->tail)
  	    return NULL;
    opbx_fr_init_ex(&tmp->f, OPBX_FRAME_VOICE, OPBX_FORMAT_G722, __PRETTY_FUNCTION__);
    tmp->f.samples = tmp->tail*2;
    tmp->f.offset = OPBX_FRIENDLY_OFFSET;
    tmp->f.data = tmp->outbuf;
    tmp->f.datalen = tmp->tail;

    tmp->tail = 0;
    return &tmp->f;
}

static struct opbx_frame *g722tolin_sample(void)
{
    static struct opbx_frame f;
 
    opbx_fr_init_ex(&f, OPBX_FRAME_VOICE, OPBX_FORMAT_G722, __PRETTY_FUNCTION__);
    f.datalen = sizeof(g722_ex);
    f.samples = sizeof(g722_ex)*2;
    f.data = (uint8_t *) g722_ex;
    return &f;
}

static struct opbx_frame *lintog722_sample(void)
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
static void g722_destroy(struct opbx_translator_pvt *pvt)
{
    free(pvt);
    localusecnt--;
    opbx_update_use_count();
}

/*
 * The complete translator for g722tolin.
 */
static struct opbx_translator g722tolin =
{
    "g722tolin8k",
    OPBX_FORMAT_G722,
    16000,
    OPBX_FORMAT_SLINEAR,
    8000,
    g722tolin_new,
    g722tolin_framein,
    g722tolin_frameout,
    g722_destroy,
    /* NULL */
    g722tolin_sample
};

/*
 * The complete translator for lintog722.
 */
static struct opbx_translator lintog722 =
{
    "lin8ktog722",
    OPBX_FORMAT_SLINEAR,
    8000,
    OPBX_FORMAT_G722,
    16000,
    lintog722_new,
    lintog722_framein,
    lintog722_frameout,
    g722_destroy,
    /* NULL */
    lintog722_sample
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
                    useplc = opbx_true(var->value) ? 1 : 0;
                    if (option_verbose > 2)
                        opbx_verbose(VERBOSE_PREFIX_3 "codec_g722_16k_8k: %susing generic PLC\n", useplc  ?  ""  :  "not ");
                }
                var = var->next;
            }
        }
        opbx_config_destroy(cfg);
    }
}

int reload(void)
{
    parse_config();
    return 0;
}

int unload_module(void)
{
    int res;
    
    opbx_mutex_lock(&localuser_lock);
    if ((res = opbx_unregister_translator(&lintog722)) == 0)
        res = opbx_unregister_translator(&g722tolin);
    if (localusecnt)
        res = -1;
    opbx_mutex_unlock(&localuser_lock);
    return res;
}

int load_module(void)
{
    int res;
 
    parse_config();
    if ((res = opbx_register_translator(&g722tolin)) == 0)
        res = opbx_register_translator(&lintog722);
    else
        opbx_unregister_translator(&g722tolin);
    return res;
}

/*
 * Return a description of this module.
 */
char *description(void)
{
    return tdesc;
}

int usecount (void)
{
    int res;
    STANDARD_USECOUNT (res);
    return res;
}
