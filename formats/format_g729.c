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
 * \brief Save to raw, headerless G729 data.
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif
 
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/sched.h"
#include "callweaver/module.h"

/* Some Ideas for this code came from makeg729e.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

struct pvt
{
    /* Believe it or not, we must decode/recode to account for the
       weird MS format */
    FILE *f;                                /* Descriptor */
    struct cw_frame fr;                   /* Frame information */
    uint8_t buf[CW_FRIENDLY_OFFSET + 20];                       /* Two Real G729 Frames */
};

static struct cw_format format;

static const char desc[] = "Raw G729 data";

static void *g729_open(FILE *f)
{
    struct pvt *tmp;
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    
    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->f = f;
        cw_fr_init_ex(&tmp->fr, CW_FRAME_VOICE, CW_FORMAT_G729A, format.name);
        tmp->fr.offset = CW_FRIENDLY_OFFSET;
        tmp->fr.data = &tmp->buf[CW_FRIENDLY_OFFSET];
        return tmp;
    }

    cw_log(CW_LOG_ERROR, "Out of memory\n");
    return NULL;
}

static void *g729_rewrite(FILE *f, const char *comment)
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

static void g729_close(void *data)
{
    struct pvt *pvt = data;

    fclose(pvt->f);
    free(pvt);
}

static struct cw_frame *g729_read(void *data, int *whennext)
{
    struct pvt *pvt = data;
    int res;

    /* Send a frame from the file to the appropriate channel */
    pvt->fr.samples = 160;
    pvt->fr.datalen = 20;

    if ((res = fread(pvt->fr.data, 1, 20, pvt->f)) != 20)
    {
        if (res && (res != 10))
            cw_log(CW_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }

    *whennext = pvt->fr.samples;
    return &pvt->fr;
}

static int g729_write(void *data, struct cw_frame *f)
{
    struct pvt *pvt = data;
    int res;

    if (f->frametype != CW_FRAME_VOICE)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != CW_FORMAT_G729A)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-G729 frame (%d)!\n", f->subclass);
        return -1;
    }
    if (f->datalen % 10)
    {
        cw_log(CW_LOG_WARNING, "Invalid data length, %d, should be multiple of 10\n", f->datalen);
        return -1;
    }
    if ((res = fwrite(f->data, 1, f->datalen, pvt->f)) != f->datalen)
    {
        cw_log(CW_LOG_WARNING, "Bad write (%d/10): %s\n", res, strerror(errno));
        return -1;
    }
    return 0;
}

static char *g729_getcomment(void *data)
{
    return NULL;
}

static int g729_seek(void *data, long sample_offset, int whence)
{
    struct pvt *pvt = data;
    long bytes;

    off_t min,cur,max,offset=0;
    min = 0;
    cur = ftell(pvt->f);
    fseek(pvt->f, 0, SEEK_END);
    max = ftell(pvt->f);
    
    bytes = 20 * (sample_offset / 160);
    if (whence == SEEK_SET)
        offset = bytes;
    else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
        offset = cur + bytes;
    else if (whence == SEEK_END)
        offset = max - bytes;
    if (whence != SEEK_FORCECUR)
    {
        offset = (offset > max)  ?  max:offset;
    }
    /* protect against seeking beyond begining. */
    offset = (offset < min)  ?  min:offset;
    if (fseek(pvt->f, offset, SEEK_SET) < 0)
        return -1;
    return 0;
}

static int g729_trunc(void *data)
{
    struct pvt *pvt = data;

    /* Truncate file to current length */
    if (ftruncate(fileno(pvt->f), ftell(pvt->f)) < 0)
        return -1;
    return 0;
}

static long g729_tell(void *data)
{
    struct pvt *pvt = data;

    return (ftell(pvt->f)/20)*160;
}

static struct cw_format format =
{
    .name = "g729",
    .exts = "g729",
    .format = CW_FORMAT_G729A,
    .open = g729_open,
    .rewrite = g729_rewrite,
    .write = g729_write,
    .seek = g729_seek,
    .trunc = g729_trunc,
    .tell = g729_tell,
    .read = g729_read,
    .close = g729_close,
    .getcomment = g729_getcomment,
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
