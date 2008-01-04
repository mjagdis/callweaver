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
 * \brief Flat, binary, ulaw PCM file format.
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

#define BUF_SIZE 160        /* 160 samples */

struct pvt
{
    FILE *f;                                /* Descriptor */
    struct timeval last;
    struct opbx_frame fr;                   /* Frame information */
    uint8_t buf[OPBX_FRIENDLY_OFFSET + BUF_SIZE];                  /* Output Buffer */
};

static struct opbx_format format;

static const char desc[] = "Raw uLaw 8kHz audio support (PCM)";

static void *pcm_open(FILE *f)
{
    struct pvt *tmp;

    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->f = f;
        opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_ULAW, format.name);
        tmp->fr.offset = OPBX_FRIENDLY_OFFSET;
        tmp->fr.data = &tmp->buf[OPBX_FRIENDLY_OFFSET];
        return tmp;
    }

    opbx_log(OPBX_LOG_ERROR, "Out of memory\n");
    return NULL;
}

static void *pcm_rewrite(FILE *f, const char *comment)
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

static void pcm_close(void *data)
{
    struct pvt *pvt = data;

    fclose(pvt->f);
    free(pvt);
}

static struct opbx_frame *pcm_read(void *data, int *whennext)
{
    struct pvt *pvt = data;
    int res;
    int delay;

    if ((res = fread(pvt->fr.data, 1, BUF_SIZE, pvt->f)) < 1)
    {
        if (res)
            opbx_log(OPBX_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }
    pvt->fr.samples = res;
    pvt->fr.datalen = res;
    delay = pvt->fr.samples;
    *whennext = delay;
    return &pvt->fr;
}

static int pcm_write(void *data, struct opbx_frame *f)
{
    struct pvt *pvt = data;
    int res;

    if (f->frametype != OPBX_FRAME_VOICE)
    {
        opbx_log(OPBX_LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != OPBX_FORMAT_ULAW)
    {
        opbx_log(OPBX_LOG_WARNING, "Asked to write non-ulaw frame (%d)!\n", f->subclass);
        return -1;
    }
    if ((res = fwrite(f->data, 1, f->datalen, pvt->f)) != f->datalen)
    {
        opbx_log(OPBX_LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
        return -1;
    }
    return 0;
}

static int pcm_seek(void *data, long sample_offset, int whence)
{
    struct pvt *pvt = data;
    off_t offset=0;
    off_t min;
    off_t cur;
    off_t max;

    min = 0;
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
        offset = (offset > max)  ?  max  :  offset;
    /* always protect against seeking past begining. */
    offset = (offset < min)  ?  min  :  offset;
    return fseek(pvt->f, offset, SEEK_SET);
}

static int pcm_trunc(void *data)
{
    struct pvt *pvt = data;

    return ftruncate(fileno(pvt->f), ftell(pvt->f));
}

static long pcm_tell(void *data)
{
    struct pvt *pvt = data;

    return ftell(pvt->f);
}

static char *pcm_getcomment(void *data)
{
    return NULL;
}


static struct opbx_format format = {
	.name = "pcm",
	.exts = "pcm|ulaw|ul|mu",
	.format = OPBX_FORMAT_ULAW,
	.open = pcm_open,
	.rewrite = pcm_rewrite,
	.write = pcm_write,
	.seek = pcm_seek,
	.trunc = pcm_trunc,
	.tell = pcm_tell,
	.read = pcm_read,
	.close = pcm_close,
	.getcomment = pcm_getcomment,
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
