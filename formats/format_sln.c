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

#define BUF_SIZE 320        /* 160 samples */

struct opbx_filestream
{
    void *reserved[OPBX_RESERVED_POINTERS];
    /* This is what a filestream means to us */
    FILE *f;                                /* Descriptor */
    struct opbx_channel *owner;
    struct opbx_frame fr;                   /* Frame information */
    char waste[OPBX_FRIENDLY_OFFSET];       /* Buffer for sending frames, etc */
    char empty;                             /* Empty character */
    uint8_t buf[BUF_SIZE];                  /* Output Buffer */
    struct timeval last;
};


static struct opbx_format format;

static const char desc[] = "Raw Signed Linear Audio support (SLN)";


static struct opbx_filestream *slinear_open(FILE *f)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;

    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
        opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, format.name);
        tmp->fr.data = tmp->buf;
        /* datalen will vary for each frame */
    }
    else
    {
        opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
    }
    return tmp;
}

static struct opbx_filestream *slinear_rewrite(FILE *f, const char *comment)
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
        opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
    }
    return tmp;
}

static void slinear_close(struct opbx_filestream *s)
{
    fclose(s->f);
    free(s);
    s = NULL;
}

static struct opbx_frame *slinear_read(struct opbx_filestream *s, int *whennext)
{
    int res;
    int delay;
    /* Send a frame from the file to the appropriate channel */

    opbx_fr_init_ex(&s->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, NULL);
    s->fr.offset = OPBX_FRIENDLY_OFFSET;
    s->fr.data = s->buf;
    if ((res = fread(s->buf, 1, BUF_SIZE, s->f)) < 1)
    {
        if (res)
            opbx_log(OPBX_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }
    s->fr.samples = res/sizeof(int16_t);
    s->fr.datalen = res;
    delay = s->fr.samples;
    *whennext = delay;
    return &s->fr;
}

static int slinear_write(struct opbx_filestream *fs, struct opbx_frame *f)
{
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
    if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen)
    {
        opbx_log(OPBX_LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
        return -1;
    }
    return 0;
}

static int slinear_seek(struct opbx_filestream *fs, long sample_offset, int whence)
{
    off_t offset=0,min,cur,max;

    min = 0;
    sample_offset <<= 1;
    cur = ftell(fs->f);
    fseek(fs->f, 0, SEEK_END);
    max = ftell(fs->f);
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
    return fseek(fs->f, offset, SEEK_SET)/sizeof(int16_t);
}

static int slinear_trunc(struct opbx_filestream *fs)
{
    return ftruncate(fileno(fs->f), ftell(fs->f));
}

static long slinear_tell(struct opbx_filestream *fs)
{
    off_t offset;
    offset = ftell(fs->f);
    return offset / 2;
}

static char *slinear_getcomment(struct opbx_filestream *s)
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
