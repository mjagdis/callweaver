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
 * \brief Translate between signed linear and GSM 06.10
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

#include "../formats/msgsm.h"

/* Sample 20ms of linear frame data */
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
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0008, 0x0000, 0x0000, 0x0000
};

/* Sample frame of GSM 06.10 data */
static uint8_t gsm_ex[] =
{
    0xda, 0xa6, 0xac, 0x2d, 0xa3, 0x50, 0x00, 0x49, 0x24, 0x92, 
    0x49, 0x24, 0x50, 0x40, 0x49, 0x24, 0x92, 0x37, 0x24, 0x52, 
    0x00, 0x49, 0x24, 0x92, 0x47, 0x24, 0x50, 0x80, 0x46, 0xe3, 
    0x6d, 0xb8, 0xdc
};

OPBX_MUTEX_DEFINE_STATIC(localuser_lock);
static int localusecnt=0;

static char *tdesc = "GSM06.10/PCM16 (signed linear) codec translator";

static int useplc = 0;

struct opbx_translator_pvt
{
    gsm0610_state_t *gsm;
    struct opbx_frame f;
    /* Space to build offset */
    char offset[OPBX_FRIENDLY_OFFSET];
    /* Buffer for our outgoing frame */
    int16_t outbuf[8000];
    /* Enough to store a full second */
    int16_t buf[8000];
    int tail;
    plc_state_t plc;
};

#define gsm_coder_pvt opbx_translator_pvt

static struct opbx_translator_pvt *gsm_new(void)
{
    struct gsm_coder_pvt *tmp;

    if ((tmp = malloc(sizeof(struct gsm_coder_pvt))) == NULL)
        return NULL;
    memset(tmp, 0, sizeof(*tmp));
    if ((tmp->gsm = gsm0610_init(NULL, GSM0610_PACKING_VOIP)) == NULL)
    {
        free(tmp);
        return NULL;
    }
    plc_init(&tmp->plc);
    localusecnt++;
    opbx_update_use_count();
    return tmp;
}

static struct opbx_frame *lintogsm_sample(void)
{
    static struct opbx_frame f;

    opbx_fr_init_ex(&f, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, __PRETTY_FUNCTION__);
    f.datalen = sizeof(slin_ex);
    /* Assume 8000 Hz */
    f.samples = sizeof(slin_ex)/sizeof(int16_t);
    f.data = slin_ex;
    return &f;
}

static struct opbx_frame *gsmtolin_sample(void)
{
    static struct opbx_frame f;

    opbx_fr_init_ex(&f, OPBX_FRAME_VOICE, OPBX_FORMAT_GSM, __PRETTY_FUNCTION__);
    f.datalen = sizeof(gsm_ex);
    /* All frames are 20 ms long */
    f.samples = 160;
    f.data = gsm_ex;
    return &f;
}

static struct opbx_frame *gsmtolin_frameout(struct opbx_translator_pvt *tmp)
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

static int gsmtolin_framein(struct opbx_translator_pvt *tmp, struct opbx_frame *f)
{
    /* Assuming there's space left, decode into the current buffer at
       the tail location.  Read in as many frames as there are */
    int x;
    uint8_t data[66];
    int msgsm = 0;
    
    if (f->datalen == 0)
    {
        /* Perform PLC with nominal framesize of 20ms/160 samples */
        if ((tmp->tail + 160) > sizeof(tmp->buf)/sizeof(int16_t))
        {
            opbx_log(LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
        if (useplc)
        {
            plc_fillin(&tmp->plc, tmp->buf+tmp->tail, 160);
            tmp->tail += 160;
        }
        return 0;
    }

    if ((f->datalen%33)  &&  (f->datalen%65))
    {
        opbx_log(LOG_WARNING, "Huh?  A GSM frame that isn't a multiple of 33 or 65 bytes long from %s (%d)?\n", f->src, f->datalen);
        return -1;
    }
    
    if (f->datalen%65 == 0) 
        msgsm = 1;
        
    for (x = 0;  x < f->datalen;  x += (msgsm  ?  65  :  33))
    {
        if (msgsm)
        {
            /* Translate MSGSM format to Real GSM format before feeding in */
            conv65(f->data + x, data);
            if (tmp->tail + 320 < sizeof(tmp->buf)/sizeof(int16_t))
            {    
                if (gsm0610_decode(tmp->gsm, tmp->buf + tmp->tail, data, 1) != 160)
                {
                    opbx_log(LOG_WARNING, "Invalid GSM data (1)\n");
                    return -1;
                }
                tmp->tail += 160;
                if (gsm0610_decode(tmp->gsm, tmp->buf + tmp->tail, data + 33, 1) != 160)
                {
                    opbx_log(LOG_WARNING, "Invalid GSM data (2)\n");
                    return -1;
                }
                tmp->tail += 160;
            }
            else
            {
                opbx_log(LOG_WARNING, "Out of (MS) buffer space\n");
                return -1;
            }
        }
        else
        {
            if (tmp->tail + 160 < sizeof(tmp->buf)/sizeof(int16_t))
            {
                if (gsm0610_decode(tmp->gsm, tmp->buf + tmp->tail, f->data + x, 1) != 160)
                {
                    opbx_log(LOG_WARNING, "Invalid GSM data\n");
                    return -1;
                }
                tmp->tail += 160;
            }
            else
            {
                opbx_log(LOG_WARNING, "Out of buffer space\n");
                return -1;
            }
        }
    }

    /* just add the last 20ms frame; there must have been at least one */
    if (useplc)
        plc_rx(&tmp->plc, tmp->buf + tmp->tail - 160, 160);

    return 0;
}

static int lintogsm_framein(struct opbx_translator_pvt *tmp, struct opbx_frame *f)
{
    /* Just add the frames to our stream */
    /* XXX We should look at how old the rest of our stream is, and if it
       is too old, then we should overwrite it entirely, otherwise we can
       get artifacts of earlier talk that do not belong */
    if (tmp->tail + f->datalen/sizeof(int16_t) >= sizeof(tmp->buf)/sizeof(int16_t))
    {
        opbx_log(LOG_WARNING, "Out of buffer space\n");
        return -1;
    }
    memcpy((tmp->buf + tmp->tail), f->data, f->datalen);
    tmp->tail += f->datalen/sizeof(int16_t);
    return 0;
}

static struct opbx_frame *lintogsm_frameout(struct opbx_translator_pvt *tmp)
{
    int x = 0;

    /* We can't work on anything less than a frame in size */
    if (tmp->tail < 160)
        return NULL;
    opbx_fr_init_ex(&tmp->f, OPBX_FRAME_VOICE, OPBX_FORMAT_GSM, __PRETTY_FUNCTION__);
    tmp->f.offset = OPBX_FRIENDLY_OFFSET;
    tmp->f.data = tmp->outbuf;

    while (tmp->tail >= 160)
    {
        if ((x + 1)*33 >= sizeof(tmp->outbuf))
        {
            opbx_log(LOG_WARNING, "Out of buffer space\n");
            break;
        }
        /* Encode a frame of data */
        gsm0610_encode(tmp->gsm, ((uint8_t *) tmp->outbuf) + (x * 33), tmp->buf, 1);
        /* Assume 8000 Hz -- 20 ms */
        tmp->tail -= 160;
        /* Move the data at the end of the buffer to the front */
        if (tmp->tail)
            memmove(tmp->buf, tmp->buf + 160, tmp->tail*sizeof(int16_t));
        x++;
    }
    tmp->f.datalen = x*33;
    tmp->f.samples = x*160;
    return &tmp->f;    
}

static void gsm_destroy_stuff(struct opbx_translator_pvt *pvt)
{
    if (pvt->gsm)
        gsm0610_release(pvt->gsm);
    free(pvt);
    localusecnt--;
    opbx_update_use_count();
}

static opbx_translator_t gsmtolin =
{
    "gsmtolin", 
    OPBX_FORMAT_GSM,
    8000,
    OPBX_FORMAT_SLINEAR,
    8000,
    gsm_new,
    gsmtolin_framein,
    gsmtolin_frameout,
    gsm_destroy_stuff,
    gsmtolin_sample
};

static opbx_translator_t lintogsm =
{
    "lintogsm", 
    OPBX_FORMAT_SLINEAR,
    8000,
    OPBX_FORMAT_GSM,
    8000,
    gsm_new,
    lintogsm_framein,
    lintogsm_frameout,
    gsm_destroy_stuff,
    lintogsm_sample
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
                       opbx_verbose(VERBOSE_PREFIX_3 "codec_gsm: %susing generic PLC\n", useplc  ?  ""  :  "not ");
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
    res = opbx_unregister_translator(&lintogsm);
    if (!res)
        res = opbx_unregister_translator(&gsmtolin);
    if (localusecnt)
        res = -1;
    opbx_mutex_unlock(&localuser_lock);
    return res;
}

int load_module(void)
{
    int res;

    parse_config();
    res = opbx_register_translator(&gsmtolin);
    if (!res) 
        res=opbx_register_translator(&lintogsm);
    else
        opbx_unregister_translator(&gsmtolin);
    return res;
}

char *description(void)
{
    return tdesc;
}

int usecount(void)
{
    int res;

    STANDARD_USECOUNT(res);
    return res;
}
