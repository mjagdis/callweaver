/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Anthony Minessale
 * Anthony Minessale (anthmct@yahoo.com)
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
 * \brief RAW SLINEAR Format
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

#define BUF_SIZE 160

struct pvt
{
    FILE *f;                                /* Descriptor */
    struct timeval last;
    struct opbx_frame fr;                   /* Frame information */
    uint8_t buf[OPBX_FRIENDLY_OFFSET + BUF_SIZE * sizeof(int16_t)];                  /* Output Buffer */
};


static struct opbx_format format;

static const char desc[] = "Raw Signed Linear Audio support (SLN)";


static void *slinear_open(FILE *f)
{
    struct pvt *tmp;

    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->f = f;
        opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, format.name);
        tmp->fr.offset = OPBX_FRIENDLY_OFFSET;
        tmp->fr.data = &tmp->buf[OPBX_FRIENDLY_OFFSET];
        return tmp;
    }

    opbx_log(OPBX_LOG_ERROR, "Out of memory\n");
    return NULL;
}

static void *slinear_rewrite(FILE *f, const char *comment)
{
    struct pvt *tmp;

    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->f = f;
        return tmp;
    }

    opbx_log(OPBX_LOG_ERROR, "Out of memory\n");
    return NULL;
}

static void slinear_close(void *data)
{
    struct pvt *pvt = data;

    fclose(pvt->f);
    free(pvt);
}

static struct opbx_frame *slinear_read(void *data, int *whennext)
{
    struct pvt *pvt = data;
    int res;
    int delay;
    /* Send a frame from the file to the appropriate channel */

    if ((res = fread(pvt->fr.data, 1, BUF_SIZE * sizeof(int16_t), pvt->f)) < 1)
    {
        if (res)
            opbx_log(OPBX_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }
    pvt->fr.samples = res / sizeof(int16_t);
    pvt->fr.datalen = res;
    delay = pvt->fr.samples;
    *whennext = delay;
    return &pvt->fr;
}

static int slinear_write(void *data, struct opbx_frame *f)
{
    struct pvt *pvt = data;
    int res;

    if (f->frametype != OPBX_FRAME_VOICE)
    {
        opbx_log(OPBX_LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != OPBX_FORMAT_SLINEAR)
    {
        opbx_log(OPBX_LOG_WARNING, "Asked to write non-slinear frame (%d)!\n", f->subclass);
        return -1;
    }
    if ((res = fwrite(f->data, 1, f->datalen, pvt->f)) != f->datalen)
    {
        opbx_log(OPBX_LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
        return -1;
    }
    return 0;
}

static int slinear_seek(void *data, long sample_offset, int whence)
{
    struct pvt *pvt = data;
    off_t offset=0,min,cur,max;

    min = 0;
    sample_offset <<= 1;
    cur = ftell(pvt->f);
    fseek(pvt->f, 0, SEEK_END);
    max = ftell(pvt->f);
    if (whence == SEEK_SET)
        offset = sample_offset;
    else if (whence == SEEK_CUR  ||  whence == SEEK_FORCECUR)
        offset = sample_offset + cur;
    else if (whence == SEEK_END)
        offset = max - sample_offset;
    if (whence != SEEK_FORCECUR)
    {
        offset = (offset > max)  ?  max  :  offset;
    }
    /* always protect against seeking past begining. */
    offset = (offset < min)  ?  min  :  offset;
    return fseek(pvt->f, offset, SEEK_SET)/sizeof(int16_t);
}

static int slinear_trunc(void *data)
{
    struct pvt *pvt = data;

    return ftruncate(fileno(pvt->f), ftell(pvt->f));
}

static long slinear_tell(void *data)
{
    struct pvt *pvt = data;

    return ftell(pvt->f) / 2;
}

static char *slinear_getcomment(void *data)
{
    return NULL;
}

static struct opbx_format format =
{
    .name = "sln",
    .exts = "sln|raw",
    .format = OPBX_FORMAT_SLINEAR,
    .open = slinear_open,
    .rewrite = slinear_rewrite,
    .write = slinear_write,
    .seek = slinear_seek,
    .trunc = slinear_trunc,
    .tell = slinear_tell,
    .read = slinear_read,
    .close = slinear_close,
    .getcomment = slinear_getcomment,
};

static int load_module(void)
{
    opbx_format_register(&format);
    return 0;
}

static int unload_module(void)
{
    opbx_format_unregister(&format);
    return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
