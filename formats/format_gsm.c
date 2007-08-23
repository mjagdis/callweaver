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
 * \brief Save to raw, headerless GSM data.
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

#include "msgsm.h"

/* Some Ideas for this code came from makegsme.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

/* silent gsm frame */
/* begin binary data: */
char gsm_silence[] = /* 33 */
{
    0xD8,0x20,0xA2,0xE1,0x5A,0x50,0x00,0x49,0x24,0x92,0x49,0x24,0x50,0x00,0x49,
    0x24,0x92,0x49,0x24,0x50,0x00,0x49,0x24,0x92,0x49,0x24,0x50,0x00,0x49,0x24,
    0x92,0x49,0x24
};

/* end binary data. size = 33 bytes */

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
    unsigned char gsm[66];                /* Two Real GSM Frames */
};


static struct opbx_format format;

static const char desc[] = "Raw GSM data";


static struct opbx_filestream *gsm_open(FILE *f)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;

    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        tmp->f = f;
        opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_GSM, format.name);
        tmp->fr.data = tmp->gsm;
        /* datalen will vary for each frame */
    }
    return tmp;
}

static struct opbx_filestream *gsm_rewrite(FILE *f, const char *comment)
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

static void gsm_close(struct opbx_filestream *s)
{
    fclose(s->f);
    free(s);
}

static struct opbx_frame *gsm_read(struct opbx_filestream *s, int *whennext)
{
    int res;

    opbx_fr_init_ex(&s->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_GSM, NULL);
    s->fr.offset = OPBX_FRIENDLY_OFFSET;
    s->fr.samples = 160;
    s->fr.datalen = 33;
    s->fr.data = s->gsm;
    if ((res = fread(s->gsm, 1, 33, s->f)) != 33)
    {
        if (res)
            opbx_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }
    *whennext = 160;
    return &s->fr;
}

static int gsm_write(struct opbx_filestream *fs, struct opbx_frame *f)
{
    int res;
    uint8_t gsm[66];
    
    if (f->frametype != OPBX_FRAME_VOICE)
    {
        opbx_log(LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != OPBX_FORMAT_GSM)
    {
        opbx_log(LOG_WARNING, "Asked to write non-GSM frame (%d)!\n", f->subclass);
        return -1;
    }
    if (!(f->datalen % 65))
    {
        /* This is in MSGSM format, need to be converted */
        int len=0;

        while (len < f->datalen)
        {
            conv65(f->data + len, gsm);
            if ((res = fwrite(gsm, 1, 66, fs->f)) != 66)
            {
                opbx_log(LOG_WARNING, "Bad write (%d/66): %s\n", res, strerror(errno));
                return -1;
            }
            len += 65;
        }
    }
    else
    {
        if (f->datalen % 33)
        {
            opbx_log(LOG_WARNING, "Invalid data length, %d, should be multiple of 33\n", f->datalen);
            return -1;
        }
        if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen)
        {
            opbx_log(LOG_WARNING, "Bad write (%d/33): %s\n", res, strerror(errno));
            return -1;
        }
    }
    return 0;
}

static int gsm_seek(struct opbx_filestream *fs, long sample_offset, int whence)
{
    off_t offset = 0;
    off_t min;
    off_t cur;
    off_t max;
    off_t distance;
    
    min = 0;
    cur = ftell(fs->f);
    fseek(fs->f, 0, SEEK_END);
    max = ftell(fs->f);
    /* have to fudge to frame here, so not fully to sample */
    distance = (sample_offset/160) * 33;
    if(whence == SEEK_SET)
        offset = distance;
    else if(whence == SEEK_CUR || whence == SEEK_FORCECUR)
        offset = distance + cur;
    else if(whence == SEEK_END)
        offset = max - distance;
    /* Always protect against seeking past the begining. */
    offset = (offset < min)  ?  min  :  offset;
    if (whence != SEEK_FORCECUR)
    {
        offset = (offset > max)?max:offset;
    }
    else if (offset > max)
    {
        int i;
        fseek(fs->f, 0, SEEK_END);
        for (i = 0;  i < (offset - max)/33;  i++)
            fwrite(gsm_silence, 1, 33, fs->f);
    }
    return fseek(fs->f, offset, SEEK_SET);
}

static int gsm_trunc(struct opbx_filestream *fs)
{
    return ftruncate(fileno(fs->f), ftell(fs->f));
}

static long gsm_tell(struct opbx_filestream *fs)
{
    off_t offset;
    offset = ftell(fs->f);
    return (offset/33)*160;
}

static char *gsm_getcomment(struct opbx_filestream *s)
{
    return NULL;
}


static struct opbx_format format = {
	.name = "gsm",
	.exts = "gsm",
	.format = OPBX_FORMAT_GSM,
	.open = gsm_open,
	.rewrite = gsm_rewrite,
	.write = gsm_write,
	.seek = gsm_seek,
	.trunc = gsm_trunc,
	.tell = gsm_tell,
	.read = gsm_read,
	.close = gsm_close,
	.getcomment = gsm_getcomment,
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
