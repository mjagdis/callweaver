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
 * \brief Work with WAV in the proprietary Microsoft format.
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

/* Some Ideas for this code came from makewave.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

#define BLOCKSIZE 160

struct pvt
{
    FILE *f; /* Descriptor */
    int bytes;
    int needsgain;
    int foffset;
    int lasttimeout;
    int maxlen;
    struct timeval last;
    struct cw_frame fr;                   /* Frame information */
    uint8_t buf[CW_FRIENDLY_OFFSET + BLOCKSIZE * sizeof(int16_t)];    
};


static struct cw_format format;

static const char desc[] = "Microsoft WAV format (8000hz Signed Linear)";


#define GAIN 2        /* 2^GAIN is the multiple to increase the volume by */

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


static int check_header(FILE *f)
{
    int type, size, formtype;
    int fmt, hsize;
    short filefmt, chans, bysam, bisam;
    int bysec;
    int freq;
    int data;
    if (fread(&type, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Read failed (type)\n");
        return -1;
    }
    if (fread(&size, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Read failed (size)\n");
        return -1;
    }
    size = ltohl(size);
    if (fread(&formtype, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Read failed (formtype)\n");
        return -1;
    }
    if (memcmp(&type, "RIFF", 4)) {
        cw_log(CW_LOG_WARNING, "Does not begin with RIFF\n");
        return -1;
    }
    if (memcmp(&formtype, "WAVE", 4)) {
        cw_log(CW_LOG_WARNING, "Does not contain WAVE\n");
        return -1;
    }
    if (fread(&fmt, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Read failed (fmt)\n");
        return -1;
    }
    if (memcmp(&fmt, "fmt ", 4)) {
        cw_log(CW_LOG_WARNING, "Does not say fmt\n");
        return -1;
    }
    if (fread(&hsize, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Read failed (formtype)\n");
        return -1;
    }
    if (ltohl(hsize) < 16) {
        cw_log(CW_LOG_WARNING, "Unexpected header size %d\n", ltohl(hsize));
        return -1;
    }
    if (fread(&filefmt, 1, 2, f) != 2) {
        cw_log(CW_LOG_WARNING, "Read failed (format)\n");
        return -1;
    }
    if (ltohs(filefmt) != 1) {
        cw_log(CW_LOG_WARNING, "Not a wav file %d\n", ltohs(filefmt));
        return -1;
    }
    if (fread(&chans, 1, 2, f) != 2) {
        cw_log(CW_LOG_WARNING, "Read failed (format)\n");
        return -1;
    }
    if (ltohs(chans) != 1) {
        cw_log(CW_LOG_WARNING, "Not in mono %d\n", ltohs(chans));
        return -1;
    }
    if (fread(&freq, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Read failed (freq)\n");
        return -1;
    }
    if (ltohl(freq) != 8000) {
        cw_log(CW_LOG_WARNING, "Unexpected freqency %d\n", ltohl(freq));
        return -1;
    }
    /* Ignore the byte frequency */
    if (fread(&bysec, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Read failed (BYTES_PER_SECOND)\n");
        return -1;
    }
    /* Check bytes per sample */
    if (fread(&bysam, 1, 2, f) != 2) {
        cw_log(CW_LOG_WARNING, "Read failed (BYTES_PER_SAMPLE)\n");
        return -1;
    }
    if (ltohs(bysam) != 2) {
        cw_log(CW_LOG_WARNING, "Can only handle 16bits per sample: %d\n", ltohs(bysam));
        return -1;
    }
    if (fread(&bisam, 1, 2, f) != 2) {
        cw_log(CW_LOG_WARNING, "Read failed (Bits Per Sample): %d\n", ltohs(bisam));
        return -1;
    }
    /* Skip any additional header */
    if (fseek(f,ltohl(hsize)-16,SEEK_CUR) == -1 ) {
        cw_log(CW_LOG_WARNING, "Failed to skip remaining header bytes: %d\n", ltohl(hsize)-16 );
        return -1;
    }
    /* Skip any facts and get the first data block */
    for(;;)
    { 
        char buf[4];
        
        /* Begin data chunk */
        if (fread(&buf, 1, 4, f) != 4) {
            cw_log(CW_LOG_WARNING, "Read failed (data)\n");
            return -1;
        }
        /* Data has the actual length of data in it */
        if (fread(&data, 1, 4, f) != 4) {
            cw_log(CW_LOG_WARNING, "Read failed (data)\n");
            return -1;
        }
        data = ltohl(data);
        if(memcmp(buf, "data", 4) == 0 ) 
            break;
        if(memcmp(buf, "fact", 4) != 0 ) {
            cw_log(CW_LOG_WARNING, "Unknown block - not fact or data\n");
            return -1;
        }
        if (fseek(f,data,SEEK_CUR) == -1 ) {
            cw_log(CW_LOG_WARNING, "Failed to skip fact block: %d\n", data );
            return -1;
        }
    }
#if 0
    curpos = lseek(fd, 0, SEEK_CUR);
    truelength = lseek(fd, 0, SEEK_END);
    lseek(fd, curpos, SEEK_SET);
    truelength -= curpos;
#endif    
    return data;
}

static int update_header(FILE *f)
{
    off_t cur,end;
    int datalen,filelen,bytes;
    
    
    cur = ftell(f);
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    /* data starts 44 bytes in */
    bytes = end - 44;
    datalen = htoll(bytes);
    /* chunk size is bytes of data plus 36 bytes of header */
    filelen = htoll(36 + bytes);
    
    if (cur < 0) {
        cw_log(CW_LOG_WARNING, "Unable to find our position\n");
        return -1;
    }
    if (fseek(f, 4, SEEK_SET)) {
        cw_log(CW_LOG_WARNING, "Unable to set our position\n");
        return -1;
    }
    if (fwrite(&filelen, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Unable to set write file size\n");
        return -1;
    }
    if (fseek(f, 40, SEEK_SET)) {
        cw_log(CW_LOG_WARNING, "Unable to set our position\n");
        return -1;
    }
    if (fwrite(&datalen, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Unable to set write datalen\n");
        return -1;
    }
    if (fseek(f, cur, SEEK_SET)) {
        cw_log(CW_LOG_WARNING, "Unable to return to position\n");
        return -1;
    }
    return 0;
}

static int write_header(FILE *f)
{
    unsigned int hz=htoll(8000);
    unsigned int bhz = htoll(16000);
    unsigned int hs = htoll(16);
    unsigned short fmt = htols(1);
    unsigned short chans = htols(1);
    unsigned short bysam = htols(2);
    unsigned short bisam = htols(16);
    unsigned int size = htoll(0);
    /* Write a wav header, ignoring sizes which will be filled in later */
    fseek(f,0,SEEK_SET);
    if (fwrite("RIFF", 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&size, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite("WAVEfmt ", 1, 8, f) != 8) {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&hs, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&fmt, 1, 2, f) != 2) {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&chans, 1, 2, f) != 2) {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&hz, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&bhz, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&bysam, 1, 2, f) != 2) {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&bisam, 1, 2, f) != 2) {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite("data", 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    if (fwrite(&size, 1, 4, f) != 4) {
        cw_log(CW_LOG_WARNING, "Unable to write header\n");
        return -1;
    }
    return 0;
}

static void *wav_open(FILE *f)
{
    struct pvt *tmp;
    int maxlen;

    if ((maxlen = check_header(f)) < 0)
        return NULL;

    if ((tmp = calloc(1, sizeof(*tmp))))
    {
        tmp->f = f;
        tmp->maxlen = maxlen;
        tmp->needsgain = 1;
        cw_fr_init_ex(&tmp->fr, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
        tmp->fr.offset = CW_FRIENDLY_OFFSET;
        tmp->fr.data = &tmp->buf[CW_FRIENDLY_OFFSET];
        tmp->bytes = 0;
        return tmp;
    }

    cw_log(CW_LOG_ERROR, "Out of memory\n");
    return NULL;
}

static void *wav_rewrite(FILE *f, const char *comment)
{
    struct pvt *tmp;

    CW_UNUSED(comment);

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
    
    if (pvt->f)
    {
        /* Pad to even length */
        if (pvt->bytes & 0x1)
            fwrite(&zero, 1, 1, pvt->f);
        fclose(pvt->f);
    }
    free(pvt);
}

static struct cw_frame *wav_read(void *data, int *whennext)
{
    struct pvt *pvt = data;
    int res;
    int delay;
    int x;
    int16_t tmp[(sizeof(pvt->buf) - CW_FRIENDLY_OFFSET) / sizeof(int16_t)];
    int16_t *out = (int16_t *)pvt->fr.data;
    int bytes = sizeof(tmp);
    off_t here;

    /* Send a frame from the file to the appropriate channel */
    here = ftell(pvt->f);
    if ((pvt->maxlen - here) < bytes)
        bytes = pvt->maxlen - here;
    if (bytes < 0)
        bytes = 0;

    if ((res = fread(tmp, 1, bytes, pvt->f)) <= 0)
    {
        if (res)
            cw_log(CW_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
        return NULL;
    }

#if __BYTE_ORDER == __BIG_ENDIAN
    for (x = 0;  x < sizeof(tmp)/sizeof(tmp[0]);  x++)
        tmp[x] = (tmp[x] << 8) | ((tmp[x] & 0xff00) >> 8);
#endif

    if (pvt->needsgain)
    {
        for (x = 0;  x < sizeof(tmp)/sizeof(tmp[0]);  x++)
        {
            if (tmp[x] & ((1 << GAIN) - 1))
            {
                /* If it has data down low, then it's not something we've artificially increased gain
                   on, so we don't need to gain adjust it */
                pvt->needsgain = 0;
            }
        }
    }
    if (pvt->needsgain)
    {
        for (x = 0;  x < sizeof(tmp)/sizeof(tmp[0]);  x++)
            out[x] = tmp[x] >> GAIN;
    }
    else
    {
        memcpy(pvt->fr.data, tmp, sizeof(tmp));
    }
            
    delay = res/sizeof(int16_t);

    pvt->fr.datalen = res;
    pvt->fr.samples = delay;
    *whennext = delay;
    return &pvt->fr;
}

static int wav_write(void *data, struct cw_frame *f)
{
    struct pvt *pvt = data;
    int res = 0;
    int x;
    int16_t tmp[8000];
    int16_t *tmpi;
    float tmpf;

    if (f->frametype != CW_FRAME_VOICE)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-voice frame!\n");
        return -1;
    }
    if (f->subclass != CW_FORMAT_SLINEAR)
    {
        cw_log(CW_LOG_WARNING, "Asked to write non-SLINEAR frame (%d)!\n", f->subclass);
        return -1;
    }
    if (f->datalen > sizeof(tmp))
    {
        cw_log(CW_LOG_WARNING, "Data length is too long\n");
        return -1;
    }
    if (!f->datalen)
        return -1;

#if 0
    printf("Data Length: %d\n", f->datalen);
#endif    

    if (pvt->buf)
    {
        tmpi = f->data;
        /* Volume adjust here to accomodate */
        for (x = 0;  x < f->datalen/2;  x++)
        {
            tmpf = ((float)tmpi[x]) * ((float)(1 << GAIN));
            if (tmpf > 32767.0)
                tmpf = 32767.0;
            if (tmpf < -32768.0)
                tmpf = -32768.0;
            tmp[x] = tmpf;
            tmp[x] &= ~((1 << GAIN) - 1);

#if __BYTE_ORDER == __BIG_ENDIAN
            tmp[x] = (tmp[x] << 8) | ((tmp[x] & 0xff00) >> 8);
#endif

        }
        if (pvt->f)
        {
            if ((fwrite(tmp, 1, f->datalen, pvt->f) != f->datalen))
            {
                cw_log(CW_LOG_WARNING, "Bad write (%d): %s\n", res, strerror(errno));
                return -1;
            }
        }
    }
    else
    {
        cw_log(CW_LOG_WARNING, "Cannot write data to file.\n");
        return -1;
    }
    
    pvt->bytes += f->datalen;
    update_header(pvt->f);
        
    return 0;
}

static int wav_seek(void *data, long sample_offset, int whence)
{
    struct pvt *pvt = data;
    off_t min;
    off_t max;
    off_t cur;
    long int offset = 0;
    long int samples;
    
    samples = sample_offset * 2; /* SLINEAR is 16 bits mono, so sample_offset * 2 = bytes */
    min = 44; /* wav header is 44 bytes */
    cur = ftell(pvt->f);
    fseek(pvt->f, 0, SEEK_END);
    max = ftell(pvt->f);
    if (whence == SEEK_SET)
        offset = samples + min;
    else if (whence == SEEK_CUR  ||  whence == SEEK_FORCECUR)
        offset = samples + cur;
    else if (whence == SEEK_END)
        offset = max - samples;
    if (whence != SEEK_FORCECUR)
        offset = (offset > max)  ?  max  :  offset;
    /* always protect the header space. */
    offset = (offset < min)  ?  min  :  offset;
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

    /* subtract header size to get samples, then divide by 2 for 16 bit samples */
    return (ftell(pvt->f) - 44)/2;
}

static char *wav_getcomment(void *data)
{
    CW_UNUSED(data);

    return NULL;
}


static struct cw_format format = {
	.name = "wav",
	.exts = "wav",
	.format = CW_FORMAT_SLINEAR,
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
