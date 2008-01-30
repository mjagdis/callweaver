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

struct pvt
{
    FILE *f;                                /* Open file descriptor */
    int rate;                               /* RATE_* defines */
    struct cw_frame fr;                   /* Frame information */
    uint8_t buf[CW_FRIENDLY_OFFSET + FRAME_TIME * 5];           /* G.726 encoded voice */
};

static struct cw_format format40, format32, format24, format16;

static const char desc[] = "Raw G.726 (16/24/32/40kbps) data";

/*
 * Rate dependant format functions (open, rewrite)
 */
static void *g726_open(FILE *f, int rate, char *name)
{
    struct pvt *tmp;

    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->f = f;
        tmp->rate = rate;
        cw_fr_init_ex(&tmp->fr, CW_FRAME_VOICE, CW_FORMAT_G726, name);
        tmp->fr.offset = CW_FRIENDLY_OFFSET;
        tmp->fr.data = &tmp->buf[CW_FRIENDLY_OFFSET];
        return tmp;
    }

    cw_log(CW_LOG_ERROR, "Out of memory\n");
    return tmp;
}

static void *g726_40_open(FILE *f)
{
    return g726_open(f, RATE_40, format40.name);
}

static void *g726_32_open(FILE *f)
{
    return g726_open(f, RATE_32, format32.name);
}

static void *g726_24_open(FILE *f)
{
    return g726_open(f, RATE_24, format24.name);
}

static void *g726_16_open(FILE *f)
{
    return g726_open(f, RATE_16, format16.name);
}


static void *g726_rewrite(FILE *f, int rate, const char *comment)
{
    struct pvt *tmp;
    
    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->f = f;
        tmp->rate = rate;
        return tmp;
    }

    cw_log(CW_LOG_ERROR, "Out of memory\n");
    return NULL;
}

static void *g726_40_rewrite(FILE *f, const char *comment)
{
    return g726_rewrite(f, RATE_40, comment);
}

static void *g726_32_rewrite(FILE *f, const char *comment)
{
    return g726_rewrite(f, RATE_32, comment);
}

static void *g726_24_rewrite(FILE *f, const char *comment)
{
    return g726_rewrite(f, RATE_24, comment);
}

static void *g726_16_rewrite(FILE *f, const char *comment)
{
    return g726_rewrite(f, RATE_16, comment);
}


/*
 * Rate independent format functions (close, read, write)
 */
static void g726_close(void *data)
{
    struct pvt *pvt = data;

    fclose(pvt->f);
    free(pvt);
}

static struct cw_frame *g726_read(void *data, int *whennext)
{
    struct pvt *pvt = data;
    int res;

    /* Send a frame from the file to the appropriate channel */
    pvt->fr.samples = 8*FRAME_TIME;
    pvt->fr.datalen = frame_size[pvt->rate];
    if ((res = fread(pvt->fr.data, 1, pvt->fr.datalen, pvt->f)) != pvt->fr.datalen)
    {
        if (res)
            cw_log(CW_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }
    *whennext = pvt->fr.samples;
    return &pvt->fr;
}

static int g726_write(void *data, struct cw_frame *f)
{
    struct pvt *pvt = data;
    int res;
    
    if (f->frametype != CW_FRAME_VOICE)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != CW_FORMAT_G726)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-G726 frame (%d)!\n", 
                 f->subclass);
        return -1;
    }
    if (f->datalen % frame_size[pvt->rate])
    {
        cw_log(CW_LOG_WARNING, "Invalid data length %d, should be multiple of %d\n", 
                 f->datalen, frame_size[pvt->rate]);
        return -1;
    }
    if ((res = fwrite(f->data, 1, f->datalen, pvt->f)) != f->datalen)
    {
        cw_log(CW_LOG_WARNING, "Bad write (%d/%d): %s\n", 
                 res, frame_size[pvt->rate], strerror(errno));
        return -1;
    }
    return 0;
}

static char *g726_getcomment(void *data)
{
    return NULL;
}

static int g726_seek(void *data, long sample_offset, int whence)
{
    return -1;
}

static int g726_trunc(void *data)
{
    return -1;
}

static long g726_tell(void *data)
{
    return -1;
}

static struct cw_format format40 =
{
    .name = "g726-40",
    .exts = "g726-40",
    .format = CW_FORMAT_G726,
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

static struct cw_format format32 =
{
    .name = "g726-32",
    .exts = "g726-32",
    .format = CW_FORMAT_G726,
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

static struct cw_format format24 =
{
    .name = "g726-24",
    .exts = "g726-24",
    .format = CW_FORMAT_G726,
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

static struct cw_format format16 =
{
    .name = "g726-16",
    .exts = "g726-16",
    .format = CW_FORMAT_G726,
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
    cw_format_register(&format40);
    cw_format_register(&format32);
    cw_format_register(&format24);
    cw_format_register(&format16);
    return 0;
}

static int unload_module(void)
{
    cw_format_unregister(&format40);
    cw_format_unregister(&format32);
    cw_format_unregister(&format24);
    cw_format_unregister(&format16);
    return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
