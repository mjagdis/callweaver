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

/* Some Ideas for this code came from makeh263e.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

struct opbx_filestream
{
    void *reserved[OPBX_RESERVED_POINTERS];
    /* Believe it or not, we must decode/recode to account for the
       weird MS format */
    /* This is what a filestream means to us */
    FILE *f; /* Descriptor */
    unsigned int lastts;
    struct opbx_frame fr;                /* Frame information */
    char waste[OPBX_FRIENDLY_OFFSET];    /* Buffer for sending frames, etc */
    char empty;                            /* Empty character */
    unsigned char h263[16384];                /* Four Real h263 Frames */
};


static struct opbx_format format;

static const char desc[] = "Raw h263 data";


static struct opbx_filestream *h263_open(FILE *f)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;
    unsigned int ts;
    int res;
    if ((res = fread(&ts, 1, sizeof(ts), f)) < sizeof(ts))
    {
        opbx_log(LOG_WARNING, "Empty file!\n");
        return NULL;
    }
        
    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
        opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VIDEO, OPBX_FORMAT_H263, format.name);
        tmp->fr.data = tmp->h263;
        /* datalen will vary for each frame */
    }
    return tmp;
}

static struct opbx_filestream *h263_rewrite(FILE *f, const char *comment)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;

    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
    }
    else
    {
        opbx_log(LOG_WARNING, "Out of memory\n");
    }
    return tmp;
}

static void h263_close(struct opbx_filestream *s)
{
    fclose(s->f);
    free(s);
    s = NULL;
}

static struct opbx_frame *h263_read(struct opbx_filestream *s, int *whennext)
{
    int res;
    int mark=0;
    unsigned short len;
    unsigned int ts;

    /* Send a frame from the file to the appropriate channel */
    opbx_fr_init_ex(&s->fr, OPBX_FRAME_VIDEO, OPBX_FORMAT_H263, NULL);
    s->fr.offset = OPBX_FRIENDLY_OFFSET;
    s->fr.data = s->h263;
    if ((res = fread(&len, 1, sizeof(len), s->f)) < 1)
        return NULL;
    len = ntohs(len);
    if (len & 0x8000)
    {
        mark = 1;
    }
    len &= 0x7fff;
    if (len > sizeof(s->h263))
    {
        opbx_log(LOG_WARNING, "Length %d is too long\n", len);
        return NULL;
    }
    if ((res = fread(s->h263, 1, len, s->f)) != len)
    {
        if (res)
            opbx_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }
    s->fr.samples = s->lastts;
    s->fr.datalen = len;
    s->fr.subclass |= mark;
    s->fr.delivery.tv_sec = 0;
    s->fr.delivery.tv_usec = 0;
    if ((res = fread(&ts, 1, sizeof(ts), s->f)) == sizeof(ts))
    {
        s->lastts = ntohl(ts);
        *whennext = s->lastts * 4/45;
    }
    else
    {
        *whennext = 0;
    }
    return &s->fr;
}

static int h263_write(struct opbx_filestream *fs, struct opbx_frame *f)
{
    int res;
    unsigned int ts;
    unsigned short len;
    int subclass;
    int mark = 0;

    if (f->frametype != OPBX_FRAME_VIDEO)
    {
        opbx_log(LOG_WARNING, "Asked to write non-video frame!\n");
        return -1;
    }
    subclass = f->subclass;
    if (subclass & 0x1)
        mark=0x8000;
    subclass &= ~0x1;
    if (subclass != OPBX_FORMAT_H263)
    {
        opbx_log(LOG_WARNING, "Asked to write non-h263 frame (%d)!\n", f->subclass);
        return -1;
    }
    ts = htonl(f->samples);
    if ((res = fwrite(&ts, 1, sizeof(ts), fs->f)) != sizeof(ts))
    {
        opbx_log(LOG_WARNING, "Bad write (%d/4): %s\n", res, strerror(errno));
        return -1;
    }
    len = htons(f->datalen | mark);
    if ((res = fwrite(&len, 1, sizeof(len), fs->f)) != sizeof(len))
    {
        opbx_log(LOG_WARNING, "Bad write (%d/2): %s\n", res, strerror(errno));
        return -1;
    }
    if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen)
    {
        opbx_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
        return -1;
    }
    return 0;
}

static char *h263_getcomment(struct opbx_filestream *s)
{
    return NULL;
}

static int h263_seek(struct opbx_filestream *fs, long sample_offset, int whence)
{
    /* No way Jose */
    return -1;
}

static int h263_trunc(struct opbx_filestream *fs)
{
    /* Truncate file to current length */
    if (ftruncate(fileno(fs->f), ftell(fs->f)) < 0)
        return -1;
    return 0;
}

static long h263_tell(struct opbx_filestream *fs)
{
    /* XXX This is totally bogus XXX */
    off_t offset;
    offset = ftell(fs->f);
    return (offset/20)*160;
}


static struct opbx_format format = {
	.name = "h263",
	.exts = "h263",
	.format = OPBX_FORMAT_H263,
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
	opbx_format_register(&format);
	return 0;
}

static int unload_module(void)
{
	opbx_format_unregister(&format);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
