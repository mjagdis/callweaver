/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (c) 2004 - 2005, inAccess Networks
 *
 * Michael Manousos <manousos@inaccessnetworks.com>
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

/*!\file
 *
 * \brief Headerless G.726 (16/24/32/40kbps) data format for CallWeaver.
 * 
 * File name extensions:
 * \arg 40 kbps: g726-40
 * \arg 32 kbps: g726-32
 * \arg 24 kbps: g726-24
 * \arg 16 kbps: g726-16
 * \ingroup formats
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

#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/sched.h"
#include "callweaver/module.h"

#define RATE_40 0
#define RATE_32 1
#define RATE_24 2
#define RATE_16 3

/* We can only read/write chunks of FRAME_TIME ms G.726 data */
#define    FRAME_TIME    10    /* 10 ms size */

/* Frame sizes in bytes */
static int frame_size[4] =
{ 
    FRAME_TIME * 5,
    FRAME_TIME * 4,
    FRAME_TIME * 3,
    FRAME_TIME * 2
};

struct opbx_filestream
{
    /* Do not place anything before "reserved" */
    void *reserved[OPBX_RESERVED_POINTERS];
    /* This is what a filestream means to us */
    FILE *f;                                /* Open file descriptor */
    int rate;                               /* RATE_* defines */
    struct opbx_frame fr;                   /* Frame information */
    char waste[OPBX_FRIENDLY_OFFSET];       /* Buffer for sending frames, etc */
    char empty;                             /* Empty character */
    uint8_t g726[FRAME_TIME * 5];           /* G.726 encoded voice */
};

static struct opbx_format format40, format32, format24, format16;

static const char desc[] = "Raw G.726 (16/24/32/40kbps) data";

/*
 * Rate dependant format functions (open, rewrite)
 */
static struct opbx_filestream *g726_40_open(FILE *f)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;
    
    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
        tmp->rate = RATE_40;
        opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_G726, format40.name);
        tmp->fr.data = tmp->g726;
        /* datalen will vary for each frame */
    }
    else
    {
        opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
    }
    return tmp;
}

static struct opbx_filestream *g726_32_open(FILE *f)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;

    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
        tmp->rate = RATE_32;
        opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_G726, format32.name);
        tmp->fr.data = tmp->g726;
        /* datalen will vary for each frame */
    }
    else
    {
        opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
    }
    return tmp;
}

static struct opbx_filestream *g726_24_open(FILE *f)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;

    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
        tmp->rate = RATE_24;
        opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_G726, format24.name);
        tmp->fr.data = tmp->g726;
        /* datalen will vary for each frame */
    }
    else
    {
        opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
    }
    return tmp;
}

static struct opbx_filestream *g726_16_open(FILE *f)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;
    
    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
        opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_G726, format16.name);
        tmp->rate = RATE_16;
        tmp->fr.data = tmp->g726;
        /* datalen will vary for each frame */
    }
    else
    {
        opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
    }
    return tmp;
}

static struct opbx_filestream *g726_40_rewrite(FILE *f, const char *comment)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;
    
    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
        tmp->rate = RATE_40;
    }
    else
    {
        opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
    }
    return tmp;
}

static struct opbx_filestream *g726_32_rewrite(FILE *f, const char *comment)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;
    
    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
        tmp->rate = RATE_32;
    }
    else
    {
        opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
    }
    return tmp;
}

static struct opbx_filestream *g726_24_rewrite(FILE *f, const char *comment)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;
    
    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
        tmp->rate = RATE_24;
    }
    else
    {
        opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
    }
    return tmp;
}

static struct opbx_filestream *g726_16_rewrite(FILE *f, const char *comment)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;
    
    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
        tmp->rate = RATE_16;
    }
    else
    {
        opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
    }
    return tmp;
}

/*
 * Rate independent format functions (close, read, write)
 */
static void g726_close(struct opbx_filestream *s)
{
    fclose(s->f);
    free(s);
    s = NULL;
}

static struct opbx_frame *g726_read(struct opbx_filestream *s, int *whennext)
{
    int res;

    /* Send a frame from the file to the appropriate channel */
    opbx_fr_init_ex(&s->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_G726, NULL);
    s->fr.offset = OPBX_FRIENDLY_OFFSET;
    s->fr.samples = 8*FRAME_TIME;
    s->fr.datalen = frame_size[s->rate];
    s->fr.data = s->g726;
    if ((res = fread(s->g726, 1, s->fr.datalen, s->f)) != s->fr.datalen)
    {
        if (res)
            opbx_log(OPBX_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }
    *whennext = s->fr.samples;
    return &s->fr;
}

static int g726_write(struct opbx_filestream *fs, struct opbx_frame *f)
{
    int res;
    
    if (f->frametype != OPBX_FRAME_VOICE)
    {
        opbx_log(OPBX_LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != OPBX_FORMAT_G726)
    {
        opbx_log(OPBX_LOG_WARNING, "Asked to write non-G726 frame (%d)!\n", 
                 f->subclass);
        return -1;
    }
    if (f->datalen % frame_size[fs->rate])
    {
        opbx_log(OPBX_LOG_WARNING, "Invalid data length %d, should be multiple of %d\n", 
                 f->datalen, frame_size[fs->rate]);
        return -1;
    }
    if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen)
    {
        opbx_log(OPBX_LOG_WARNING, "Bad write (%d/%d): %s\n", 
                 res, frame_size[fs->rate], strerror(errno));
        return -1;
    }
    return 0;
}

static char *g726_getcomment(struct opbx_filestream *s)
{
    return NULL;
}

static int g726_seek(struct opbx_filestream *fs, long sample_offset, int whence)
{
    return -1;
}

static int g726_trunc(struct opbx_filestream *fs)
{
    return -1;
}

static long g726_tell(struct opbx_filestream *fs)
{
    return -1;
}

static struct opbx_format format40 =
{
    .name = "g726-40",
    .exts = "g726-40",
    .format = OPBX_FORMAT_G726,
    .open = g726_40_open,
    .rewrite = g726_40_rewrite,
    .write = g726_write,
    .seek = g726_seek,
    .trunc = g726_trunc,
    .tell = g726_tell,
    .read = g726_read,
    .close = g726_close,
    .getcomment = g726_getcomment,
};

static struct opbx_format format32 =
{
    .name = "g726-32",
    .exts = "g726-32",
    .format = OPBX_FORMAT_G726,
    .open = g726_32_open,
    .rewrite = g726_32_rewrite,
    .write = g726_write,
    .seek = g726_seek,
    .trunc = g726_trunc,
    .tell = g726_tell,
    .read = g726_read,
    .close = g726_close,
    .getcomment = g726_getcomment,
};

static struct opbx_format format24 =
{
    .name = "g726-24",
    .exts = "g726-24",
    .format = OPBX_FORMAT_G726,
    .open = g726_24_open,
    .rewrite = g726_24_rewrite,
    .write = g726_write,
    .seek = g726_seek,
    .trunc = g726_trunc,
    .tell = g726_tell,
    .read = g726_read,
    .close = g726_close,
    .getcomment = g726_getcomment,
};

static struct opbx_format format16 =
{
    .name = "g726-16",
    .exts = "g726-16",
    .format = OPBX_FORMAT_G726,
    .open = g726_16_open,
    .rewrite = g726_16_rewrite,
    .write = g726_write,
    .seek = g726_seek,
    .trunc = g726_trunc,
    .tell = g726_tell,
    .read = g726_read,
    .close = g726_close,
    .getcomment = g726_getcomment,
};

/*
 * Module interface (load_module, unload_module)
 */
static int load_module(void)
{
    opbx_format_register(&format40);
    opbx_format_register(&format32);
    opbx_format_register(&format24);
    opbx_format_register(&format16);
    return 0;
}

static int unload_module(void)
{
    opbx_format_unregister(&format40);
    opbx_format_unregister(&format32);
    opbx_format_unregister(&format24);
    opbx_format_unregister(&format16);
    return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
