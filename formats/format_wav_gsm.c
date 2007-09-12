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
 * \brief Save GSM in the proprietary Microsoft format.
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
#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/sched.h"
#include "callweaver/module.h"

/* Some Ideas for this code came from makewave.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

static const uint8_t wav49_silence[] = /* 65 */
{
    0x48,0x17,0xD6,0x84,0x02,0x80,0x24,0x49,0x92,0x24,0x89,0x02,0x80,0x24,0x49,
    0x92,0x24,0x89,0x02,0x80,0x24,0x49,0x92,0x24,0x89,0x02,0x80,0x24,0x49,0x92,
    0x24,0x09,0x82,0x74,0x61,0x4D,0x28,0x00,0x48,0x92,0x24,0x49,0x92,0x28,0x00,
    0x48,0x92,0x24,0x49,0x92,0x28,0x00,0x48,0x92,0x24,0x49,0x92,0x28,0x00,0x48,
    0x92,0x24,0x49,0x92,0x00
};

struct opbx_filestream
{
    void *reserved[OPBX_RESERVED_POINTERS];
    /* Believe it or not, we must decode/recode to account for the
       weird MS format */
    /* This is what a filestream means to us */
    FILE *f; /* Descriptor */
    struct opbx_frame fr;               /* Frame information */
    char waste[OPBX_FRIENDLY_OFFSET];   /* Buffer for sending frames, etc */
    char empty;                         /* Empty character */
    unsigned char gsm[66];              /* Two Real GSM Frames */
    int foffset;
    int secondhalf;                     /* Are we on the second half */
    struct timeval last;
};

static struct opbx_format format;

static const char desc[] = "Microsoft WAV format (Proprietary GSM)";

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htoll(b) (b)
#define htols(b) (b)
#define ltohl(b) (b)
#define ltohs(b) (b)
#else
#if __BYTE_ORDER == __BIG_ENDIAN
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

static int repack_gsm0610_wav49_to_voip(uint8_t d[], const uint8_t c[])
{
    gsm0610_frame_t frame[2];
    int n[2];

    gsm0610_unpack_wav49(frame, c);
    n[0] = gsm0610_pack_voip(d, &frame[0]);
    n[1] = gsm0610_pack_voip(d + n[0], &frame[1]);
    return n[0] + n[1];
}

static int repack_gsm0610_voip_to_wav49(uint8_t c[], const uint8_t d[])
{
    gsm0610_frame_t frame[2];
    int n;
 
    n = gsm0610_unpack_voip(&frame[0], d);
    gsm0610_unpack_voip(&frame[1], d + n);
    n = gsm0610_pack_wav49(c, frame);
    return n;
}

static int check_header(FILE *f)
{
    int type;
    int size;
    int formtype;
    int fmt;
    int hsize;
    int fact;
    int16_t format;
    int16_t chans;
    int freq;
    int data;
    
    if (fread(&type, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (type)\n");
        return -1;
    }
    if (fread(&size, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (size)\n");
        return -1;
    }
    size = ltohl(size);
    if (fread(&formtype, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (formtype)\n");
        return -1;
    }
    if (memcmp(&type, "RIFF", 4))
    {
        opbx_log(OPBX_LOG_WARNING, "Does not begin with RIFF\n");
        return -1;
    }
    if (memcmp(&formtype, "WAVE", 4))
    {
        opbx_log(OPBX_LOG_WARNING, "Does not contain WAVE\n");
        return -1;
    }
    if (fread(&fmt, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (fmt)\n");
        return -1;
    }
    if (memcmp(&fmt, "fmt ", 4))
    {
        opbx_log(OPBX_LOG_WARNING, "Does not say fmt\n");
        return -1;
    }
    if (fread(&hsize, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (formtype)\n");
        return -1;
    }
    if (ltohl(hsize) != 20)
    {
        opbx_log(OPBX_LOG_WARNING, "Unexpected header size %d\n", ltohl(hsize));
        return -1;
    }
    if (fread(&format, 1, 2, f) != 2)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (format)\n");
        return -1;
    }
    if (ltohs(format) != 49)
    {
        opbx_log(OPBX_LOG_WARNING, "Not a GSM file %d\n", ltohs(format));
        return -1;
    }
    if (fread(&chans, 1, 2, f) != 2)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (format)\n");
        return -1;
    }
    if (ltohs(chans) != 1)
    {
        opbx_log(OPBX_LOG_WARNING, "Not in mono %d\n", ltohs(chans));
        return -1;
    }
    if (fread(&freq, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (freq)\n");
        return -1;
    }
    if (ltohl(freq) != 8000)
    {
        opbx_log(OPBX_LOG_WARNING, "Unexpected freqency %d\n", ltohl(freq));
        return -1;
    }
    /* Ignore the byte frequency */
    if (fread(&freq, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (X_1)\n");
        return -1;
    }
    /* Ignore the two weird fields */
    if (fread(&freq, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (X_2/X_3)\n");
        return -1;
    }
    /* Ignore the byte frequency */
    if (fread(&freq, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (Y_1)\n");
        return -1;
    }
    /* Check for the word fact */
    if (fread(&fact, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (fact)\n");
        return -1;
    }
    if (memcmp(&fact, "fact", 4))
    {
        opbx_log(OPBX_LOG_WARNING, "Does not say fact\n");
        return -1;
    }
    /* Ignore the "fact value" */
    if (fread(&fact, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (fact header)\n");
        return -1;
    }
    if (fread(&fact, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (fact value)\n");
        return -1;
    }
    /* Check for the word data */
    if (fread(&data, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (data)\n");
        return -1;
    }
    if (memcmp(&data, "data", 4))
    {
        opbx_log(OPBX_LOG_WARNING, "Does not say data\n");
        return -1;
    }
    /* Ignore the data length */
    if (fread(&data, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Read failed (data)\n");
        return -1;
    }
    return 0;
}

static int update_header(FILE *f)
{
    off_t cur;
    off_t end;
    off_t bytes;
    int datalen;
    int filelen;
    
    cur = ftell(f);
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    /* In a GSM0610 WAV file, data starts 60 bytes in */
    bytes = end - 60;
    datalen = htoll((bytes + 1) & ~0x1);
    filelen = htoll(52 + ((bytes + 1) & ~0x1));
    if (cur < 0)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to find our position\n");
        return -1;
    }
    if (fseek(f, 4, SEEK_SET))
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to set our position\n");
        return -1;
    }
    if (fwrite(&filelen, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to set write file size\n");
        return -1;
    }
    if (fseek(f, 56, SEEK_SET))
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to set our position\n");
        return -1;
    }
    if (fwrite(&datalen, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to set write datalen\n");
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
    unsigned int hz = htoll(8000);
    unsigned int bhz = htoll(1625);
    unsigned int hs = htoll(20);
    unsigned short fmt = htols(49);
    unsigned short chans = htols(1);
    unsigned int fhs = htoll(4);
    unsigned int x_1 = htoll(65);
    unsigned short x_2 = htols(2);
    unsigned short x_3 = htols(320);
    unsigned int y_1 = htoll(20160);
    unsigned int size = htoll(0);
    
    /* Write a GSM header, ignoring sizes which will be filled in later */
    if (fwrite("RIFF", 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&size, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite("WAVEfmt ", 1, 8, f) != 8)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&hs, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&fmt, 1, 2, f) != 2)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&chans, 1, 2, f) != 2)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&hz, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&bhz, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&x_1, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&x_2, 1, 2, f) != 2)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&x_3, 1, 2, f) != 2)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite("fact", 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&fhs, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&y_1, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite("data", 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&size, 1, 4, f) != 4)
    {
        opbx_log(OPBX_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    return 0;
}

static struct opbx_filestream *wav_open(FILE *f)
{
    struct opbx_filestream *tmp;

    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        if (check_header(f))
        {
            free(tmp);
            return NULL;
        }
        tmp->f = f;
        opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_GSM, format.name);
        tmp->fr.data = tmp->gsm;
        /* datalen will vary for each frame */
        tmp->secondhalf = 0;
    }
    return tmp;
}

static struct opbx_filestream *wav_rewrite(FILE *f, const char *comment)
{
    /* We don't have any header to read or anything really, but
       if we did, it would go here.  We also might want to check
       and be sure it's a valid file.  */
    struct opbx_filestream *tmp;

    if ((tmp = malloc(sizeof(struct opbx_filestream))))
    {
        memset(tmp, 0, sizeof(struct opbx_filestream));
        if (write_header(f))
        {
            free(tmp);
            return NULL;
        }
        tmp->f = f;
    }
    else
    {
        opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
    }
    return tmp;
}

static void wav_close(struct opbx_filestream *s)
{
    char zero = 0;
    
    /* Pad to even length */
    fseek(s->f, 0, SEEK_END);
    if (ftell(s->f) & 0x1)
        fwrite(&zero, 1, 1, s->f);
    fclose(s->f);
    free(s);
}

static struct opbx_frame *wav_read(struct opbx_filestream *s, int *whennext)
{
    int res;
    uint8_t msdata[65];

    /* Send a frame from the file to the appropriate channel */
    opbx_fr_init_ex(&s->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_GSM, NULL);
    s->fr.offset = OPBX_FRIENDLY_OFFSET;
    s->fr.samples = 160;
    s->fr.datalen = 33;
    if (s->secondhalf)
    {
        /* Just return a frame based on the second GSM frame */
        s->fr.data = s->gsm + 33;
    }
    else
    {
        if ((res = fread(msdata, 1, 65, s->f)) != 65)
        {
            if (res  &&  (res != 1))
                opbx_log(OPBX_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
            return NULL;
        }
        /* Convert from WAV49 format to two VoIP format GSM frames */
        repack_gsm0610_wav49_to_voip(s->gsm, msdata);
        s->fr.data = s->gsm;
    }
    s->secondhalf = !s->secondhalf;
    *whennext = 160;
    return &s->fr;
}

static int wav_write(struct opbx_filestream *fs, struct opbx_frame *f)
{
    int res;
    uint8_t wav49_data[65];
    int len = 0;
    int already_wav49;

    if (f->frametype != OPBX_FRAME_VOICE)
    {
        opbx_log(OPBX_LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != OPBX_FORMAT_GSM)
    {
        opbx_log(OPBX_LOG_WARNING, "Asked to write non-GSM frame (%d)!\n", f->subclass);
        return -1;
    }
    already_wav49 = ((f->datalen % 65) == 0);
    while (len < f->datalen)
    {
        if (already_wav49)
        {
            fs->secondhalf = 0;
            if ((res = fwrite(f->data + len, 1, 65, fs->f)) != 65)
            {
                opbx_log(OPBX_LOG_WARNING, "Bad write (%d/65): %s\n", res, strerror(errno));
                return -1;
            }
            update_header(fs->f);
            len += 65;
        }
        else
        {
            if (fs->secondhalf)
            {
                memcpy(fs->gsm + 33, f->data + len, 33);
                /* Convert from two VoIP format GSM frames to WAV49 format */
                repack_gsm0610_voip_to_wav49(wav49_data, fs->gsm);
                if ((res = fwrite(wav49_data, 1, 65, fs->f)) != 65)
                {
                    opbx_log(OPBX_LOG_WARNING, "Bad write (%d/65): %s\n", res, strerror(errno));
                    return -1;
                }
                update_header(fs->f);
            }
            else
            {
                /* Copy the data and do nothing */
                memcpy(fs->gsm, f->data + len, 33);
            }
            fs->secondhalf = !fs->secondhalf;
            len += 33;
        }
    }
    return 0;
}

static int wav_seek(struct opbx_filestream *fs, long sample_offset, int whence)
{
    off_t offset = 0;
    off_t distance;
    off_t cur;
    off_t min;
    off_t max;
    
    min = 60;
    cur = ftell(fs->f);
    fseek(fs->f, 0, SEEK_END);
    max = ftell(fs->f);
    /* I'm getting sloppy here, I'm only going to go to even splits of the 2
     * frames, if you want tighter cuts use format_gsm, format_pcm, or format_wav */
    distance = (sample_offset/320)*65;
    if (whence == SEEK_SET)
        offset = distance + min;
    else if(whence == SEEK_CUR  ||  whence == SEEK_FORCECUR)
        offset = distance + cur;
    else if(whence == SEEK_END)
        offset = max - distance;
    /* always protect against seeking past end of header */
    offset = (offset < min)  ?  min  :  offset;
    if (whence != SEEK_FORCECUR)
    {
        offset = (offset > max)  ?  max  :  offset;
    }
    else if (offset > max)
    {
        int i;

        fseek(fs->f, 0, SEEK_END);
        for (i = 0;  i < (offset - max)/65;  i++)
            fwrite(wav49_silence, 1, 65, fs->f);
    }
    fs->secondhalf = 0;
    return fseek(fs->f, offset, SEEK_SET);
}

static int wav_trunc(struct opbx_filestream *fs)
{
    if (ftruncate(fileno(fs->f), ftell(fs->f)))
        return -1;
    return update_header(fs->f);
}

static long wav_tell(struct opbx_filestream *fs)
{
    off_t offset;

    offset = ftell(fs->f);
    /* since this will most likely be used later in play or record, lets stick
     * to that level of resolution, just even frames boundaries */
    return (offset - 52)/65*320;
}

static char *wav_getcomment(struct opbx_filestream *s)
{
    return NULL;
}

static struct opbx_format format =
{
    .name = "wav49",
    .exts = "WAV|wav49",
    .format = OPBX_FORMAT_GSM,
    .open = wav_open,
    .rewrite = wav_rewrite,
    .write = wav_write,
    .seek = wav_seek,
    .trunc = wav_trunc,
    .tell = wav_tell,
    .read = wav_read,
    .close = wav_close,
    .getcomment = wav_getcomment,
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
