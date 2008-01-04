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
 * \brief Old-style G.723 frame/timestamp format.
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif
 
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/sched.h"
#include "callweaver/module.h"

#define G723_MAX_SIZE 1024

struct pvt
{
    FILE *f;
    struct opbx_frame fr;
    uint8_t buf[OPBX_FRIENDLY_OFFSET + G723_MAX_SIZE];
};


static struct opbx_format format;
static const char desc[] = "G.723.1 Simple Timestamp File Format";

static void *g723_open(FILE *f)
{
    struct pvt *tmp;

    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->f = f;
        opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_G723_1, format.name);
        tmp->fr.offset = OPBX_FRIENDLY_OFFSET;
        tmp->fr.data = &tmp->buf[OPBX_FRIENDLY_OFFSET];
        return tmp;
    }

    opbx_log(OPBX_LOG_ERROR, "Out of memory\n");
    return NULL;
}

static void *g723_rewrite(FILE *f, const char *comment)
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

static struct opbx_frame *g723_read(void *data, int *whennext)
{
    struct pvt *pvt = data;
    unsigned short size;
    int res;
    int delay;

    /* Read the delay for the next packet, and schedule again if necessary */
    if (fread(&delay, 1, 4, pvt->f) == 4) 
        delay = ntohl(delay);
    else
        delay = -1;
    if (fread(&size, 1, 2, pvt->f) != 2)
    {
        /* Out of data, or the file is no longer valid.  In any case
           go ahead and stop the stream */
        return NULL;
    }
    /* Looks like we have a frame to read from here */
    size = ntohs(size);
    if (size > G723_MAX_SIZE - sizeof(struct opbx_frame))
    {
        opbx_log(OPBX_LOG_WARNING, "Size %d is invalid\n", size);
        /* The file is apparently no longer any good, as we
           shouldn't ever get frames even close to this 
           size.  */
        return NULL;
    }
    /* Read the data into the buffer */
    pvt->fr.datalen = size;
    if ((res = fread(pvt->fr.data, 1, size, pvt->f)) != size)
    {
        opbx_log(OPBX_LOG_WARNING, "Short read (%d of %d bytes) (%s)!\n", res, size, strerror(errno));
        return NULL;
    }
#if 0
        /* Average out frames <= 50 ms */
        if (delay < 50)
            pvt->fr.timelen = 30;
        else
            pvt->fr.timelen = delay;
#else
        pvt->fr.samples = 240;
#endif
    *whennext = pvt->fr.samples;
    return &pvt->fr;
}

static void g723_close(void *data)
{
    struct pvt *pvt = data;
    fclose(pvt->f);
    free(pvt);
}

static int g723_write(void *data, struct opbx_frame *f)
{
    struct pvt *pvt = data;
    u_int32_t delay;
    u_int16_t size;
    int res;

    if (f->frametype != OPBX_FRAME_VOICE)
    {
        opbx_log(OPBX_LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != OPBX_FORMAT_G723_1)
    {
        opbx_log(OPBX_LOG_WARNING, "Asked to write non-g723 frame!\n");
        return -1;
    }
    delay = 0;
    if (f->datalen <= 0)
    {
        opbx_log(OPBX_LOG_WARNING, "Short frame ignored (%d bytes long?)\n", f->datalen);
        return 0;
    }
    if ((res = fwrite(&delay, 1, 4, pvt->f)) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write delay: res=%d (%s)\n", res, strerror(errno));
        return -1;
    }
    size = htons(f->datalen);
    if ((res = fwrite(&size, 1, 2, pvt->f)) != 2)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write size: res=%d (%s)\n", res, strerror(errno));
        return -1;
    }
    if ((res = fwrite(f->data, 1, f->datalen, pvt->f)) != f->datalen)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write frame: res=%d (%s)\n", res, strerror(errno));
        return -1;
    }
    return 0;
}

static int g723_seek(void *data, long sample_offset, int whence)
{
    return -1;
}

static int g723_trunc(void *data)
{
    struct pvt *pvt = data;

    /* Truncate file to current length */
    if (ftruncate(fileno(pvt->f), ftell(pvt->f)) < 0)
        return -1;
    return 0;
}

static long g723_tell(void *data)
{
    return -1;
}

static char *g723_getcomment(void *data)
{
    return NULL;
}

static struct opbx_format format =
{
    .name = "g723.1",
    .exts = "g723.1|g723",
    .format = OPBX_FORMAT_G723_1,
    .open = g723_open,
    .rewrite = g723_rewrite,
    .write = g723_write,
    .seek = g723_seek,
    .trunc = g723_trunc,
    .tell = g723_tell,
    .read = g723_read,
    .close = g723_close,
    .getcomment = g723_getcomment,
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
