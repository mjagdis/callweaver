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

struct pvt
{
    /* Believe it or not, we must decode/recode to account for the
       weird MS format */
    FILE *f; /* Descriptor */
    int foffset;
    int secondhalf;                     /* Are we on the second half */
    struct timeval last;
    struct cw_frame fr;               /* Frame information */
    uint8_t buf[CW_FRIENDLY_OFFSET + 66];              /* Two Real GSM Frames */
};

static struct cw_format format;

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
        cw_log(CW_LOG_WARNING, "Read failed (type)\n");
        return -1;
    }
    if (fread(&size, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Read failed (size)\n");
        return -1;
    }
    size = ltohl(size);
    if (fread(&formtype, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Read failed (formtype)\n");
        return -1;
    }
    if (memcmp(&type, "RIFF", 4))
    {
        cw_log(CW_LOG_WARNING, "Does not begin with RIFF\n");
        return -1;
    }
    if (memcmp(&formtype, "WAVE", 4))
    {
        cw_log(CW_LOG_WARNING, "Does not contain WAVE\n");
        return -1;
    }
    if (fread(&fmt, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Read failed (fmt)\n");
        return -1;
    }
    if (memcmp(&fmt, "fmt ", 4))
    {
        cw_log(CW_LOG_WARNING, "Does not say fmt\n");
        return -1;
    }
    if (fread(&hsize, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Read failed (formtype)\n");
        return -1;
    }
    if (ltohl(hsize) != 20)
    {
        cw_log(CW_LOG_WARNING, "Unexpected header size %d\n", ltohl(hsize));
        return -1;
    }
    if (fread(&format, 1, 2, f) != 2)
    {
        cw_log(CW_LOG_WARNING, "Read failed (format)\n");
        return -1;
    }
    if (ltohs(format) != 49)
    {
        cw_log(CW_LOG_WARNING, "Not a GSM file %d\n", ltohs(format));
        return -1;
    }
    if (fread(&chans, 1, 2, f) != 2)
    {
        cw_log(CW_LOG_WARNING, "Read failed (format)\n");
        return -1;
    }
    if (ltohs(chans) != 1)
    {
        cw_log(CW_LOG_WARNING, "Not in mono %d\n", ltohs(chans));
        return -1;
    }
    if (fread(&freq, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Read failed (freq)\n");
        return -1;
    }
    if (ltohl(freq) != 8000)
    {
        cw_log(CW_LOG_WARNING, "Unexpected freqency %d\n", ltohl(freq));
        return -1;
    }
    /* Ignore the byte frequency */
    if (fread(&freq, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Read failed (X_1)\n");
        return -1;
    }
    /* Ignore the two weird fields */
    if (fread(&freq, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Read failed (X_2/X_3)\n");
        return -1;
    }
    /* Ignore the byte frequency */
    if (fread(&freq, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Read failed (Y_1)\n");
        return -1;
    }
    /* Check for the word fact */
    if (fread(&fact, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Read failed (fact)\n");
        return -1;
    }
    if (memcmp(&fact, "fact", 4))
    {
        cw_log(CW_LOG_WARNING, "Does not say fact\n");
        return -1;
    }
    /* Ignore the "fact value" */
    if (fread(&fact, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Read failed (fact header)\n");
        return -1;
    }
    if (fread(&fact, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Read failed (fact value)\n");
        return -1;
    }
    /* Check for the word data */
    if (fread(&data, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Read failed (data)\n");
        return -1;
    }
    if (memcmp(&data, "data", 4))
    {
        cw_log(CW_LOG_WARNING, "Does not say data\n");
        return -1;
    }
    /* Ignore the data length */
    if (fread(&data, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Read failed (data)\n");
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
        cw_log(CW_LOG_WARNING, "Unable to find our position\n");
        return -1;
    }
    if (fseek(f, 4, SEEK_SET))
    {
        cw_log(CW_LOG_WARNING, "Unable to set our position\n");
        return -1;
    }
    if (fwrite(&filelen, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Unable to set write file size\n");
        return -1;
    }
    if (fseek(f, 56, SEEK_SET))
    {
        cw_log(CW_LOG_WARNING, "Unable to set our position\n");
        return -1;
    }
    if (fwrite(&datalen, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Unable to set write datalen\n");
        return -1;
    }
    if (fseek(f, cur, SEEK_SET))
    {
        cw_log(CW_LOG_WARNING, "Unable to return to position\n");
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
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&size, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite("WAVEfmt ", 1, 8, f) != 8)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&hs, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&fmt, 1, 2, f) != 2)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&chans, 1, 2, f) != 2)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&hz, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&bhz, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&x_1, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&x_2, 1, 2, f) != 2)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&x_3, 1, 2, f) != 2)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite("fact", 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&fhs, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&y_1, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite("data", 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&size, 1, 4, f) != 4)
    {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    return 0;
}

static void *wav_open(FILE *f)
{
    struct pvt *tmp;

    if (check_header(f))
        return NULL;

    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->f = f;
        cw_fr_init_ex(&tmp->fr, CW_FRAME_VOICE, CW_FORMAT_GSM);
        tmp->fr.offset = CW_FRIENDLY_OFFSET;
        tmp->fr.data = &tmp->buf[CW_FRIENDLY_OFFSET];
        tmp->secondhalf = 0;
        return tmp;
    }

    cw_log(CW_LOG_ERROR, "Out of memory\n");
    return NULL;
}

static void *wav_rewrite(FILE *f, const char *comment)
{
    struct pvt *tmp;

    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        if (write_header(f))
        {
            free(tmp);
            return NULL;
        }
        tmp->f = f;
        return tmp;
    }

    cw_log(CW_LOG_ERROR, "Out of memory\n");
    return NULL;
}

static void wav_close(void *data)
{
    struct pvt *pvt = data;
    char zero = 0;
    
    /* Pad to even length */
    fseek(pvt->f, 0, SEEK_END);
    if (ftell(pvt->f) & 0x1)
        fwrite(&zero, 1, 1, pvt->f);
    fclose(pvt->f);
    free(pvt);
}

static struct cw_frame *wav_read(void *data, int *whennext)
{
    uint8_t msdata[65];
    struct pvt *pvt = data;
    int res;

    /* Send a frame from the file to the appropriate channel */
    pvt->fr.samples = 160;
    pvt->fr.datalen = 33;
    if (pvt->secondhalf)
    {
        /* Just return a frame based on the second GSM frame */
        pvt->fr.data = &pvt->buf[CW_FRIENDLY_OFFSET + 33];
    }
    else
    {
        if ((res = fread(msdata, 1, 65, pvt->f)) != 65)
        {
            if (res  &&  (res != 1))
                cw_log(CW_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
            return NULL;
        }
        /* Convert from WAV49 format to two VoIP format GSM frames */
        pvt->fr.data = &pvt->buf[CW_FRIENDLY_OFFSET];
        repack_gsm0610_wav49_to_voip(&pvt->buf[CW_FRIENDLY_OFFSET], msdata);
    }
    pvt->secondhalf = !pvt->secondhalf;
    *whennext = 160;
    return &pvt->fr;
}

static int wav_write(void *data, struct cw_frame *f)
{
    uint8_t wav49_data[65];
    struct pvt *pvt = data;
    int res;
    int len = 0;
    int already_wav49;

    if (f->frametype != CW_FRAME_VOICE)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != CW_FORMAT_GSM)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-GSM frame (%d)!\n", f->subclass);
        return -1;
    }
    already_wav49 = ((f->datalen % 65) == 0);
    while (len < f->datalen)
    {
        if (already_wav49)
        {
            pvt->secondhalf = 0;
            if ((res = fwrite(f->data + len, 1, 65, pvt->f)) != 65)
            {
                cw_log(CW_LOG_WARNING, "Bad write (%d/65): %s\n", res, strerror(errno));
                return -1;
            }
            update_header(pvt->f);
            len += 65;
        }
        else
        {
            if (pvt->secondhalf)
            {
                memcpy(pvt->buf + 33, f->data + len, 33);
                /* Convert from two VoIP format GSM frames to WAV49 format */
                repack_gsm0610_voip_to_wav49(wav49_data, pvt->buf);
                if ((res = fwrite(wav49_data, 1, 65, pvt->f)) != 65)
                {
                    cw_log(CW_LOG_WARNING, "Bad write (%d/65): %s\n", res, strerror(errno));
                    return -1;
                }
                update_header(pvt->f);
            }
            else
            {
                /* Copy the data and do nothing */
                memcpy(pvt->buf, f->data + len, 33);
            }
            pvt->secondhalf = !pvt->secondhalf;
            len += 33;
        }
    }
    return 0;
}

static int wav_seek(void *data, long sample_offset, int whence)
{
    struct pvt *pvt = data;
    off_t offset = 0;
    off_t distance;
    off_t cur;
    off_t min;
    off_t max;
    
    min = 60;
    cur = ftell(pvt->f);
    fseek(pvt->f, 0, SEEK_END);
    max = ftell(pvt->f);
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

        fseek(pvt->f, 0, SEEK_END);
        for (i = 0;  i < (offset - max)/65;  i++)
            fwrite(wav49_silence, 1, 65, pvt->f);
    }
    pvt->secondhalf = 0;
    return fseek(pvt->f, offset, SEEK_SET);
}

static int wav_trunc(void *data)
{
    struct pvt *pvt = data;

    if (ftruncate(fileno(pvt->f), ftell(pvt->f)))
        return -1;
    return update_header(pvt->f);
}

static long wav_tell(void *data)
{
    struct pvt *pvt = data;

    /* since this will most likely be used later in play or record, lets stick
     * to that level of resolution, just even frames boundaries */
    return (ftell(pvt->f) - 52)/65*320;
}

static char *wav_getcomment(void *data)
{
    return NULL;
}

static struct cw_format format =
{
    .name = "wav49",
    .exts = "WAV|wav49",
    .format = CW_FORMAT_GSM,
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
    cw_format_register(&format);
    return 0;
}

static int unload_module(void)
{
    cw_format_unregister(&format);
    return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
