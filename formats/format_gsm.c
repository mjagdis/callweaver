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
 * \brief Save to raw, headerless GSM data.
 * 
 */
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/sched.h"
#include "callweaver/module.h"

/* Some Ideas for this code came from makegsme.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

/* silent gsm frame */
static const uint8_t gsm_silence[] = /* 33 */
{
    0xD8,0x20,0xA2,0xE1,0x5A,0x50,0x00,0x49,0x24,0x92,0x49,0x24,0x50,0x00,0x49,
    0x24,0x92,0x49,0x24,0x50,0x00,0x49,0x24,0x92,0x49,0x24,0x50,0x00,0x49,0x24,
    0x92,0x49,0x24
};

struct pvt
{
    FILE *f;                                /* Descriptor */
    struct cw_frame fr;                   /* Frame information */
    uint8_t buf[CW_FRIENDLY_OFFSET + 66];                        /* Two GSM Frames */
};


static struct cw_format format;

static const char desc[] = "Raw GSM data";

static int repack_gsm0610_wav49_to_voip(uint8_t d[], const uint8_t c[])
{
    gsm0610_frame_t frame[2];
    int n[2];

    gsm0610_unpack_wav49(frame, c);
    n[0] = gsm0610_pack_voip(d, &frame[0]);
    n[1] = gsm0610_pack_voip(d + n[0], &frame[1]);
    return n[0] + n[1];
}

static void *gsm_open(FILE *f)
{
    struct pvt *tmp;

    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->f = f;
        cw_fr_init_ex(&tmp->fr, CW_FRAME_VOICE, CW_FORMAT_GSM);
        tmp->fr.offset = CW_FRIENDLY_OFFSET;
        tmp->fr.data = &tmp->buf[CW_FRIENDLY_OFFSET];
        return tmp;
    }

    cw_log(CW_LOG_ERROR, "Out of memory\n");
    return NULL;
}

static void *gsm_rewrite(FILE *f, const char *comment)
{
    struct pvt *tmp;

    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->f = f;
        return tmp;
    }

    cw_log(CW_LOG_ERROR, "Out of memory\n");
    return NULL;
}

static void gsm_close(void *data)
{
    struct pvt *pvt = data;

    fclose(pvt->f);
    free(pvt);
}

static struct cw_frame *gsm_read(void *data, int *whennext)
{
    struct pvt *pvt = data;
    int res;

    pvt->fr.samples = 160;
    pvt->fr.datalen = 33;
    if ((res = fread(pvt->fr.data, 1, 33, pvt->f)) != 33)
    {
        if (res)
            cw_log(CW_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }

    *whennext = 160;
    return &pvt->fr;
}

static int gsm_write(void *data, struct cw_frame *f)
{
    struct pvt *pvt = data;
    int res;
    int len;
    
    if (f->frametype != CW_FRAME_VOICE)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != CW_FORMAT_GSM)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-GSM frame (%d)!\n", f->subclass);
        return -1;
    }
    if (!(f->datalen % 65))
    {
        /* This is in WAV49 format. It needs to be converted */

        for (len = 0;  len < f->datalen;  len += 65)
        {
            repack_gsm0610_wav49_to_voip(pvt->buf, (const uint8_t *)f->data + len);
            if ((res = fwrite(pvt->buf, 1, 66, pvt->f)) != 66)
            {
                cw_log(CW_LOG_WARNING, "Bad write (%d/66): %s\n", res, strerror(errno));
                return -1;
            }
        }
    }
    else
    {
        if (f->datalen % 33)
        {
            cw_log(CW_LOG_WARNING, "Invalid data length, %d, should be multiple of 33\n", f->datalen);
            return -1;
        }
        if ((res = fwrite(f->data, 1, f->datalen, pvt->f)) != f->datalen)
        {
            cw_log(CW_LOG_WARNING, "Bad write (%d/33): %s\n", res, strerror(errno));
            return -1;
        }
    }
    return 0;
}

static int gsm_seek(void *data, long sample_offset, int whence)
{
    struct pvt *pvt = data;
    off_t offset = 0;
    off_t min;
    off_t cur;
    off_t max;
    off_t distance;
    
    min = 0;
    cur = ftell(pvt->f);
    fseek(pvt->f, 0, SEEK_END);
    max = ftell(pvt->f);
    /* have to fudge to frame here, so not fully to sample */
    distance = (sample_offset/160) * 33;
    if (whence == SEEK_SET)
        offset = distance;
    else if (whence == SEEK_CUR  ||  whence == SEEK_FORCECUR)
        offset = distance + cur;
    else if (whence == SEEK_END)
        offset = max - distance;
    /* Always protect against seeking past the begining. */
    offset = (offset < min)  ?  min  :  offset;
    if (whence != SEEK_FORCECUR)
    {
        offset = (offset > max)  ?  max  :  offset;
    }
    else if (offset > max)
    {
        int i;

        fseek(pvt->f, 0, SEEK_END);
        for (i = 0;  i < (offset - max)/33;  i++)
            fwrite(gsm_silence, 1, 33, pvt->f);
    }
    return fseek(pvt->f, offset, SEEK_SET);
}

static int gsm_trunc(void *data)
{
    struct pvt *pvt = data;

    return ftruncate(fileno(pvt->f), ftell(pvt->f));
}

static long gsm_tell(void *data)
{
    struct pvt *pvt = data;

    return (ftell(pvt->f)/33)*160;
}

static char *gsm_getcomment(void *data)
{
    return NULL;
}

static struct cw_format format =
{
    .name = "gsm",
    .exts = "gsm",
    .format = CW_FORMAT_GSM,
    .open = gsm_open,
    .rewrite = gsm_rewrite,
    .write = gsm_write,
    .seek = gsm_seek,
    .trunc = gsm_trunc,
    .tell = gsm_tell,
    .read = gsm_read,
    .close = gsm_close,
    .getcomment = gsm_getcomment,
};

static int load_module(void)
{
    cw_format_register(&format);
    return 0;
}

static int unload_module(void)
{
    cw_format_unregister(&format);
    return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
