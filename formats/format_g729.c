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

struct opbx_filestream
{
    void *reserved[OPBX_RESERVED_POINTERS];
    /* Believe it or not, we must decode/recode to account for the
       weird MS format */
    /* This is what a filestream means to us */
    FILE *f; /* Descriptor */
    struct opbx_frame fr;                /* Frame information */
    char waste[OPBX_FRIENDLY_OFFSET];    /* Buffer for sending frames, etc */
    char empty;                            /* Empty character */
    unsigned char g729[20];                /* Two Real G729 Frames */
};

static struct opbx_format format;

static const char desc[] = "Raw G729 data";

static struct opbx_filestream *g729_open(FILE *f)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;
    
    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
        opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_G729A, NULL);
        tmp->fr.data = tmp->g729;
        /* datalen will vary for each frame */
        tmp->fr.src = format.name;
    }
    return tmp;
}

static struct opbx_filestream *g729_rewrite(FILE *f, const char *comment)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;
    if ((tmp = malloc(sizeof(struct opbx_filestream)))) {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
    } else
        opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
    return tmp;
}

static void g729_close(struct opbx_filestream *s)
{
    fclose(s->f);
    free(s);
    s = NULL;
}

static struct opbx_frame *g729_read(struct opbx_filestream *s, int *whennext)
{
    int res;

    /* Send a frame from the file to the appropriate channel */
    opbx_fr_init_ex(&s->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_G729A, NULL);
    s->fr.offset = OPBX_FRIENDLY_OFFSET;
    s->fr.samples = 160;
    s->fr.datalen = 20;
    s->fr.data = s->g729;

    if ((res = fread(s->g729, 1, 20, s->f)) != 20)
    {
        if (res && (res != 10))
            opbx_log(OPBX_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }
    *whennext = s->fr.samples;
    return &s->fr;
}

static int g729_write(struct opbx_filestream *fs, struct opbx_frame *f)
{
    int res;
    if (f->frametype != OPBX_FRAME_VOICE) {
        opbx_log(OPBX_LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != OPBX_FORMAT_G729A) {
        opbx_log(OPBX_LOG_WARNING, "Asked to write non-G729 frame (%d)!\n", f->subclass);
        return -1;
    }
    if (f->datalen % 10) {
        opbx_log(OPBX_LOG_WARNING, "Invalid data length, %d, should be multiple of 10\n", f->datalen);
        return -1;
    }
    if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen) {
            opbx_log(OPBX_LOG_WARNING, "Bad write (%d/10): %s\n", res, strerror(errno));
            return -1;
    }
    return 0;
}

static char *g729_getcomment(struct opbx_filestream *s)
{
    return NULL;
}

static int g729_seek(struct opbx_filestream *fs, long sample_offset, int whence)
{
    long bytes;
    off_t min,cur,max,offset=0;
    min = 0;
    cur = ftell(fs->f);
    fseek(fs->f, 0, SEEK_END);
    max = ftell(fs->f);
    
    bytes = 20 * (sample_offset / 160);
    if (whence == SEEK_SET)
        offset = bytes;
    else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
        offset = cur + bytes;
    else if (whence == SEEK_END)
        offset = max - bytes;
    if (whence != SEEK_FORCECUR) {
        offset = (offset > max)?max:offset;
    }
    /* protect against seeking beyond begining. */
    offset = (offset < min)?min:offset;
    if (fseek(fs->f, offset, SEEK_SET) < 0)
        return -1;
    return 0;
}

static int g729_trunc(struct opbx_filestream *fs)
{
    /* Truncate file to current length */
    if (ftruncate(fileno(fs->f), ftell(fs->f)) < 0)
        return -1;
    return 0;
}

static long g729_tell(struct opbx_filestream *fs)
{
    off_t offset;
    offset = ftell(fs->f);
    return (offset/20)*160;
}


static struct opbx_format format = {
	.name = "g729",
	.exts = "g729",
	.format = OPBX_FORMAT_G729A,
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
	opbx_format_register(&format);
	return 0;
}

static int unload_module(void)
{
	opbx_format_unregister(&format);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
