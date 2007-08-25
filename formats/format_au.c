/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Andriy Pylypenko
 * Code based on format_wav.c by Mark Spencer
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
 * \brief Work with Sun Microsystems AU format.
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif
 
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/sched.h"
#include "callweaver/module.h"

#define BUF_SIZE                160

#define AU_HEADER_SIZE          24
#define AU_HEADER(var)          u_int32_t var[6]

#define AU_HDR_MAGIC_OFF        0
#define AU_HDR_HDR_SIZE_OFF     1
#define AU_HDR_DATA_SIZE_OFF    2
#define AU_HDR_ENCODING_OFF     3
#define AU_HDR_SAMPLE_RATE_OFF  4
#define AU_HDR_CHANNELS_OFF     5

#define AU_ENC_8BIT_ULAW        1

struct opbx_filestream
{
    void *reserved[OPBX_RESERVED_POINTERS];
    /* This is what a filestream means to us */
    FILE *f;                            /* Descriptor */
    struct opbx_channel *owner;
    struct opbx_frame fr;               /* Frame information */
    char waste[OPBX_FRIENDLY_OFFSET];   /* Buffer for sending frames, etc */
    char empty;                         /* Empty character */
    int16_t buf[BUF_SIZE];
};


static struct opbx_format format;

static const char desc[] = "Sun Microsystems AU format (signed linear)";


#define AU_MAGIC 0x2e736e64
#if __BYTE_ORDER == __BIG_ENDIAN
#define htoll(b) (b)
#define htols(b) (b)
#define ltohl(b) (b)
#define ltohs(b) (b)
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htoll(b)  \
          (((((b)      ) & 0xFF) << 24) | \
           ((((b) >>  8) & 0xFF) << 16) | \
           ((((b) >> 16) & 0xFF) <<  8) | \
           ((((b) >> 24) & 0xFF)      ))
#define htols(b) \
          (((((b)      ) & 0xFF) << 8) | \
           ((((b) >> 8) & 0xFF)      ))
#define ltohl(b) htoll(b)
#define ltohs(b) htols(b)
#else
#error "Endianess not defined"
#endif
#endif


static int check_header(FILE *f)
{
    AU_HEADER(header);
    u_int32_t magic;
    u_int32_t hdr_size;
    u_int32_t data_size;
    u_int32_t encoding;
    u_int32_t sample_rate;
    u_int32_t channels;

    if (fread(header, 1, AU_HEADER_SIZE, f) != AU_HEADER_SIZE)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (header)\n");
        return -1;
    }
    magic = ltohl(header[AU_HDR_MAGIC_OFF]);
    if (magic != (u_int32_t) AU_MAGIC)
    {
        opbx_log(OPBX_LOG_WARNING, "Bad magic: 0x%x\n", magic);
    }
/*  hdr_size = ltohl(header[AU_HDR_HDR_SIZE_OFF]);
    if (hdr_size < AU_HEADER_SIZE)*/
    hdr_size = AU_HEADER_SIZE;
/*  data_size = ltohl(header[AU_HDR_DATA_SIZE_OFF]); */
    encoding = ltohl(header[AU_HDR_ENCODING_OFF]);
    if (encoding != AU_ENC_8BIT_ULAW)
    {
        opbx_log(OPBX_LOG_WARNING, "Unexpected format: %d. Only 8bit ULAW allowed (%d)\n", encoding, AU_ENC_8BIT_ULAW);
        return -1;
    }
    sample_rate = ltohl(header[AU_HDR_SAMPLE_RATE_OFF]);
    if (sample_rate != 8000)
    {
        opbx_log(OPBX_LOG_WARNING, "Sample rate can only be 8000 not %d\n", sample_rate);
        return -1;
    }
    channels = ltohl(header[AU_HDR_CHANNELS_OFF]);
    if (channels != 1)
    {
        opbx_log(OPBX_LOG_WARNING, "Not in mono: channels=%d\n", channels);
        return -1;
    }
    /* Skip to data */
    fseek(f, 0, SEEK_END);
    data_size = ftell(f) - hdr_size;
    if (fseek(f, hdr_size, SEEK_SET) == -1)
    {
        opbx_log(OPBX_LOG_WARNING, "Failed to skip to data: %d\n", hdr_size);
        return -1;
    }
    return data_size;
}

static int update_header(FILE *f)
{
    off_t cur, end;
    u_int32_t datalen;
    int bytes;

    cur = ftell(f);
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    /* data starts 24 bytes in */
    bytes = end - AU_HEADER_SIZE;
    datalen = htoll(bytes);

    if (cur < 0)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to find our position\n");
        return -1;
    }
    if (fseek(f, AU_HDR_DATA_SIZE_OFF * sizeof(u_int32_t), SEEK_SET))
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to set our position\n");
        return -1;
    }
    if (fwrite(&datalen, 1, sizeof(datalen), f) != sizeof(datalen))
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to set write file size\n");
        return -1;
    }
    if (fseek(f, cur, SEEK_SET))
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to return to position\n");
        return -1;
    }
    return 0;
}

static int write_header(FILE *f)
{
    AU_HEADER(header);

    header[AU_HDR_MAGIC_OFF] = htoll((u_int32_t) AU_MAGIC);
    header[AU_HDR_HDR_SIZE_OFF] = htoll(AU_HEADER_SIZE);
    header[AU_HDR_DATA_SIZE_OFF] = 0;
    header[AU_HDR_ENCODING_OFF] = htoll(AU_ENC_8BIT_ULAW);
    header[AU_HDR_SAMPLE_RATE_OFF] = htoll(8000);
    header[AU_HDR_CHANNELS_OFF] = htoll(1);

    /* Write an au header, ignoring sizes which will be filled in later */
    fseek(f, 0, SEEK_SET);
    if (fwrite(header, 1, AU_HEADER_SIZE, f) != AU_HEADER_SIZE)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    return 0;
}

static struct opbx_filestream *au_open(FILE *f)
{
    struct opbx_filestream *tmp;

    if ((tmp = malloc(sizeof(struct opbx_filestream))) == NULL)
    {
        opbx_log(OPBX_LOG_ERROR, "Out of memory\n");
        return NULL;
    }

    memset(tmp, 0, sizeof(struct opbx_filestream));
    if (check_header(f) < 0)
    {
        free(tmp);
        return NULL;
    }
    tmp->f = f;
    opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_ULAW, NULL);
    tmp->fr.data = tmp->buf;
    /* datalen will vary for each frame */
    tmp->fr.src = format.name;
    return tmp;
}

static struct opbx_filestream *au_rewrite(FILE *f, const char *comment)
{
    struct opbx_filestream *tmp;

    if ((tmp = malloc(sizeof(struct opbx_filestream))) == NULL)
    {
        opbx_log(OPBX_LOG_ERROR, "Out of memory\n");
        return NULL;
    }

    memset(tmp, 0, sizeof(struct opbx_filestream));
    if (write_header(f))
    {
        free(tmp);
        return NULL;
    }
    tmp->f = f;
    return tmp;
}

static void au_close(struct opbx_filestream *s)
{
    fclose(s->f);
    free(s);
}

static struct opbx_frame *au_read(struct opbx_filestream *s, int *whennext)
{
    int res;
    int delay;

    /* Send a frame from the file to the appropriate channel */
    opbx_fr_init_ex(&s->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_ULAW, NULL);
    s->fr.offset = OPBX_FRIENDLY_OFFSET;
    s->fr.data = s->buf;
    if ((res = fread(s->buf, 1, BUF_SIZE, s->f)) < 1)
    {
        if (res)
            opbx_log(OPBX_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }
    s->fr.samples = res;
    s->fr.datalen = res;
    delay = s->fr.samples;
    *whennext = delay;
    return &s->fr;
}

static int au_write(struct opbx_filestream *fs, struct opbx_frame *f)
{
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
    if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen)
    {
        opbx_log(OPBX_LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
        return -1;
    }
    update_header(fs->f);
    return 0;
}

static int au_seek(struct opbx_filestream *fs, long sample_offset, int whence)
{
    off_t min;
    off_t max;
    off_t cur;
    long int offset = 0;
    long int samples;
    
    samples = sample_offset;
    min = AU_HEADER_SIZE;
    cur = ftell(fs->f);
    fseek(fs->f, 0, SEEK_END);
    max = ftell(fs->f);
    if (whence == SEEK_SET)
        offset = samples + min;
    else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
        offset = samples + cur;
    else if (whence == SEEK_END)
        offset = max - samples;
    if (whence != SEEK_FORCECUR)
        offset = (offset > max)  ?  max  :  offset;
    /* always protect the header space. */
    offset = (offset < min)  ?  min  :  offset;
    return fseek(fs->f, offset, SEEK_SET);
}

static int au_trunc(struct opbx_filestream *fs)
{
    if (ftruncate(fileno(fs->f), ftell(fs->f)))
        return -1;
    return update_header(fs->f);
}

static long au_tell(struct opbx_filestream *fs)
{
    off_t offset;

    offset = ftell(fs->f);
    return offset - AU_HEADER_SIZE;
}

static char *au_getcomment(struct opbx_filestream *s)
{
    return NULL;
}

static struct opbx_format format =
{
    .name = "au",
    .exts = "au",
    .format = OPBX_FORMAT_ULAW,
    .open = au_open,
    .rewrite = au_rewrite,
    .write = au_write,
    .seek = au_seek,
    .trunc = au_trunc,
    .tell = au_tell,
    .read = au_read,
    .close = au_close,
    .getcomment = au_getcomment,
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
