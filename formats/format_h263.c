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
 * \brief Save to raw, headerless h263 data.
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

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/sched.h"
#include "callweaver/module.h"

/* Some Ideas for this code came from makeh263e.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

struct pvt
{
    /* Believe it or not, we must decode/recode to account for the
       weird MS format */
    FILE *f; /* Descriptor */
    unsigned int lastts;
    struct cw_frame fr;                /* Frame information */
    uint8_t buf[CW_FRIENDLY_OFFSET + 16384];                /* Four Real h263 Frames */
};


static struct cw_format format;

static const char desc[] = "Raw h263 data";


static void *h263_open(FILE *f)
{
    struct pvt *tmp;
    unsigned int ts;

    if (fread(&ts, 1, sizeof(ts), f) < sizeof(ts))
    {
        cw_log(CW_LOG_WARNING, "Empty file!\n");
        return NULL;
    }
        
    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->f = f;
        cw_fr_init_ex(&tmp->fr, CW_FRAME_VIDEO, CW_FORMAT_H263);
        tmp->fr.offset = CW_FRIENDLY_OFFSET;
        tmp->fr.data = &tmp->buf[CW_FRIENDLY_OFFSET];
        return tmp;
    }

    cw_log(CW_LOG_ERROR, "Out of memory\n");
    return NULL;
}

static void *h263_rewrite(FILE *f, const char *comment)
{
    struct pvt *tmp;

    CW_UNUSED(comment);

    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->f = f;
        return tmp;
    }

    cw_log(CW_LOG_ERROR, "Out of memory\n");
    return NULL;
}

static void h263_close(void *data)
{
    struct pvt *pvt = data;

    fclose(pvt->f);
    free(pvt);
}

static struct cw_frame *h263_read(void *data, int *whennext)
{
    struct pvt *pvt = data;
    int res;
    int mark;
    unsigned short len;
    unsigned int ts;

    if ((res = fread(&len, 1, sizeof(len), pvt->f)) < 1)
        return NULL;

    len = ntohs(len);
    mark = len & 0x8000;
    len &= 0x7fff;

    if (len > sizeof(pvt->buf) - CW_FRIENDLY_OFFSET)
    {
        cw_log(CW_LOG_WARNING, "Length %d is too long\n", len);
        return NULL;
    }

    if ((res = fread(pvt->fr.data, 1, len, pvt->f)) != len)
    {
        if (res)
            cw_log(CW_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }

    pvt->fr.samples = pvt->lastts;
    pvt->fr.datalen = len;
    pvt->fr.subclass |= mark;

    if ((res = fread(&ts, 1, sizeof(ts), pvt->f)) == sizeof(ts))
    {
        pvt->lastts = ntohl(ts);
        *whennext = pvt->fr.duration = pvt->lastts * 4/45;
    }
    else
    {
        pvt->fr.duration = 1;
        *whennext = 0;
    }
    return &pvt->fr;
}

static int h263_write(void *data, struct cw_frame *f)
{
    struct pvt *pvt = data;
    int res;
    unsigned int ts;
    unsigned short len;
    int subclass;
    int mark = 0;

    if (f->frametype != CW_FRAME_VIDEO)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-video frame!\n");
        return -1;
    }
    subclass = f->subclass;
    if (subclass & 0x1)
        mark=0x8000;
    subclass &= ~0x1;
    if (subclass != CW_FORMAT_H263)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-h263 frame (%d)!\n", f->subclass);
        return -1;
    }
    ts = htonl(f->samples);
    if ((res = fwrite(&ts, 1, sizeof(ts), pvt->f)) != sizeof(ts))
    {
        cw_log(CW_LOG_WARNING, "Bad write (%d/4): %s\n", res, strerror(errno));
        return -1;
    }
    len = htons(f->datalen | mark);
    if ((res = fwrite(&len, 1, sizeof(len), pvt->f)) != sizeof(len))
    {
        cw_log(CW_LOG_WARNING, "Bad write (%d/2): %s\n", res, strerror(errno));
        return -1;
    }
    if ((res = fwrite(f->data, 1, f->datalen, pvt->f)) != f->datalen)
    {
        cw_log(CW_LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
        return -1;
    }
    return 0;
}

static char *h263_getcomment(void *data)
{
    CW_UNUSED(data);

    return NULL;
}

static int h263_seek(void *data, long sample_offset, int whence)
{
    CW_UNUSED(data);
    CW_UNUSED(sample_offset);
    CW_UNUSED(whence);

    return -1;
}

static int h263_trunc(void *data)
{
    struct pvt *pvt = data;

    /* Truncate file to current length */
    if (ftruncate(fileno(pvt->f), ftell(pvt->f)) < 0)
        return -1;
    return 0;
}

static long h263_tell(void *data)
{
    struct pvt *pvt = data;

    /* XXX This is totally bogus XXX */
    return (ftell(pvt->f)/20)*160;
}


static struct cw_format format = {
	.name = "h263",
	.exts = "h263",
	.format = CW_FORMAT_H263,
	.open = h263_open,
	.rewrite = h263_rewrite,
	.write = h263_write,
	.seek = h263_seek,
	.trunc = h263_trunc,
	.tell = h263_tell,
	.read = h263_read,
	.close = h263_close,
	.getcomment = h263_getcomment,
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
