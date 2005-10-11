/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 *
 * Work with WAV in the proprietary Microsoft format.
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

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/lock.h"
#include "openpbx/channel.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/sched.h"
#include "openpbx/module.h"
#include "openpbx/confdefs.h"

/* Some Ideas for this code came from makewave.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

struct opbx_filestream {
	void *reserved[OPBX_RESERVED_POINTERS];
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	int bytes;
	int needsgain;
	struct opbx_frame fr;				/* Frame information */
	char waste[OPBX_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	short buf[160];	
	int foffset;
	int lasttimeout;
	int maxlen;
	struct timeval last;
};


OPBX_MUTEX_DEFINE_STATIC(wav_lock);
static int glistcnt = 0;

static char *name = "wav";
static char *desc = "Microsoft WAV format (8000hz Signed Linear)";
static char *exts = "wav";

#define BLOCKSIZE 160

#define GAIN 2		/* 2^GAIN is the multiple to increase the volume by */

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


static int check_header(int fd)
{
	int type, size, formtype;
	int fmt, hsize;
	short format, chans, bysam, bisam;
	int bysec;
	int freq;
	int data;
	if (read(fd, &type, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (type)\n");
		return -1;
	}
	if (read(fd, &size, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (size)\n");
		return -1;
	}
	size = ltohl(size);
	if (read(fd, &formtype, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (formtype)\n");
		return -1;
	}
	if (memcmp(&type, "RIFF", 4)) {
		opbx_log(LOG_WARNING, "Does not begin with RIFF\n");
		return -1;
	}
	if (memcmp(&formtype, "WAVE", 4)) {
		opbx_log(LOG_WARNING, "Does not contain WAVE\n");
		return -1;
	}
	if (read(fd, &fmt, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (fmt)\n");
		return -1;
	}
	if (memcmp(&fmt, "fmt ", 4)) {
		opbx_log(LOG_WARNING, "Does not say fmt\n");
		return -1;
	}
	if (read(fd, &hsize, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (formtype)\n");
		return -1;
	}
	if (ltohl(hsize) < 16) {
		opbx_log(LOG_WARNING, "Unexpected header size %d\n", ltohl(hsize));
		return -1;
	}
	if (read(fd, &format, 2) != 2) {
		opbx_log(LOG_WARNING, "Read failed (format)\n");
		return -1;
	}
	if (ltohs(format) != 1) {
		opbx_log(LOG_WARNING, "Not a wav file %d\n", ltohs(format));
		return -1;
	}
	if (read(fd, &chans, 2) != 2) {
		opbx_log(LOG_WARNING, "Read failed (format)\n");
		return -1;
	}
	if (ltohs(chans) != 1) {
		opbx_log(LOG_WARNING, "Not in mono %d\n", ltohs(chans));
		return -1;
	}
	if (read(fd, &freq, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (freq)\n");
		return -1;
	}
	if (ltohl(freq) != 8000) {
		opbx_log(LOG_WARNING, "Unexpected freqency %d\n", ltohl(freq));
		return -1;
	}
	/* Ignore the byte frequency */
	if (read(fd, &bysec, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (BYTES_PER_SECOND)\n");
		return -1;
	}
	/* Check bytes per sample */
	if (read(fd, &bysam, 2) != 2) {
		opbx_log(LOG_WARNING, "Read failed (BYTES_PER_SAMPLE)\n");
		return -1;
	}
	if (ltohs(bysam) != 2) {
		opbx_log(LOG_WARNING, "Can only handle 16bits per sample: %d\n", ltohs(bysam));
		return -1;
	}
	if (read(fd, &bisam, 2) != 2) {
		opbx_log(LOG_WARNING, "Read failed (Bits Per Sample): %d\n", ltohs(bisam));
		return -1;
	}
	/* Skip any additional header */
	if ( lseek(fd,ltohl(hsize)-16,SEEK_CUR) == -1 ) {
		opbx_log(LOG_WARNING, "Failed to skip remaining header bytes: %d\n", ltohl(hsize)-16 );
		return -1;
	}
	/* Skip any facts and get the first data block */
	for(;;)
	{ 
            char buf[4];
	    
	    /* Begin data chunk */
	    if (read(fd, &buf, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (data)\n");
		return -1;
	    }
	    /* Data has the actual length of data in it */
	    if (read(fd, &data, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (data)\n");
		return -1;
	    }
	    data = ltohl(data);
	    if( memcmp(buf, "data", 4) == 0 ) break;
	    if( memcmp(buf, "fact", 4) != 0 ) {
		opbx_log(LOG_WARNING, "Unknown block - not fact or data\n");
		return -1;
	    }
	    if ( lseek(fd,data,SEEK_CUR) == -1 ) {
		opbx_log(LOG_WARNING, "Failed to skip fact block: %d\n", data );
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

static int update_header(int fd)
{
	off_t cur,end;
	int datalen,filelen,bytes;
	
	
	cur = lseek(fd, 0, SEEK_CUR);
	end = lseek(fd, 0, SEEK_END);
	/* data starts 44 bytes in */
	bytes = end - 44;
	datalen = htoll(bytes);
	/* chunk size is bytes of data plus 36 bytes of header */
	filelen = htoll(36 + bytes);
	
	if (cur < 0) {
		opbx_log(LOG_WARNING, "Unable to find our position\n");
		return -1;
	}
	if (lseek(fd, 4, SEEK_SET) != 4) {
		opbx_log(LOG_WARNING, "Unable to set our position\n");
		return -1;
	}
	if (write(fd, &filelen, 4) != 4) {
		opbx_log(LOG_WARNING, "Unable to set write file size\n");
		return -1;
	}
	if (lseek(fd, 40, SEEK_SET) != 40) {
		opbx_log(LOG_WARNING, "Unable to set our position\n");
		return -1;
	}
	if (write(fd, &datalen, 4) != 4) {
		opbx_log(LOG_WARNING, "Unable to set write datalen\n");
		return -1;
	}
	if (lseek(fd, cur, SEEK_SET) != cur) {
		opbx_log(LOG_WARNING, "Unable to return to position\n");
		return -1;
	}
	return 0;
}

static int write_header(int fd)
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
	lseek(fd,0,SEEK_SET);
	if (write(fd, "RIFF", 4) != 4) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &size, 4) != 4) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, "WAVEfmt ", 8) != 8) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &hs, 4) != 4) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &fmt, 2) != 2) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &chans, 2) != 2) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &hz, 4) != 4) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &bhz, 4) != 4) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &bysam, 2) != 2) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &bisam, 2) != 2) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, "data", 4) != 4) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &size, 4) != 4) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	return 0;
}

static struct opbx_filestream *wav_open(int fd)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct opbx_filestream *tmp;
	if ((tmp = malloc(sizeof(struct opbx_filestream)))) {
		memset(tmp, 0, sizeof(struct opbx_filestream));
		if ((tmp->maxlen = check_header(fd)) < 0) {
			free(tmp);
			return NULL;
		}
		if (opbx_mutex_lock(&wav_lock)) {
			opbx_log(LOG_WARNING, "Unable to lock wav list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		tmp->needsgain = 1;
		tmp->fr.data = tmp->buf;
		tmp->fr.frametype = OPBX_FRAME_VOICE;
		tmp->fr.subclass = OPBX_FORMAT_SLINEAR;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		tmp->bytes = 0;
		glistcnt++;
		opbx_mutex_unlock(&wav_lock);
		opbx_update_use_count();
	}
	return tmp;
}

static struct opbx_filestream *wav_rewrite(int fd, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct opbx_filestream *tmp;
	if ((tmp = malloc(sizeof(struct opbx_filestream)))) {
		memset(tmp, 0, sizeof(struct opbx_filestream));
		if (write_header(fd)) {
			free(tmp);
			return NULL;
		}
		if (opbx_mutex_lock(&wav_lock)) {
			opbx_log(LOG_WARNING, "Unable to lock wav list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		glistcnt++;
		opbx_mutex_unlock(&wav_lock);
		opbx_update_use_count();
	} else
		opbx_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void wav_close(struct opbx_filestream *s)
{
	char zero = 0;
	if (opbx_mutex_lock(&wav_lock)) {
		opbx_log(LOG_WARNING, "Unable to lock wav list\n");
		return;
	}
	glistcnt--;
	opbx_mutex_unlock(&wav_lock);
	opbx_update_use_count();
	/* Pad to even length */
	if (s->bytes & 0x1)
		write(s->fd, &zero, 1);
	close(s->fd);
	free(s);
	s = NULL;
}

static struct opbx_frame *wav_read(struct opbx_filestream *s, int *whennext)
{
	int res;
	int delay;
	int x;
	short tmp[sizeof(s->buf) / 2];
	int bytes = sizeof(tmp);
	off_t here;
	/* Send a frame from the file to the appropriate channel */
	here = lseek(s->fd, 0, SEEK_CUR);
	if ((s->maxlen - here) < bytes)
		bytes = s->maxlen - here;
	if (bytes < 0)
		bytes = 0;
/* 	opbx_log(LOG_DEBUG, "here: %d, maxlen: %d, bytes: %d\n", here, s->maxlen, bytes); */
	
	if ( (res = read(s->fd, tmp, bytes)) <= 0 ) {
		if (res) {
			opbx_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		}
		return NULL;
	}

#if __BYTE_ORDER == __BIG_ENDIAN
	for( x = 0; x < sizeof(tmp)/2; x++) tmp[x] = (tmp[x] << 8) | ((tmp[x] & 0xff00) >> 8);
#endif

	if (s->needsgain) {
		for (x=0;x<sizeof(tmp)/2;x++)
			if (tmp[x] & ((1 << GAIN) - 1)) {
				/* If it has data down low, then it's not something we've artificially increased gain
				   on, so we don't need to gain adjust it */
				s->needsgain = 0;
			}
	}
	if (s->needsgain) {
		for (x=0;x<sizeof(tmp)/2;x++) {
			s->buf[x] = tmp[x] >> GAIN;
		}
	} else {
		memcpy(s->buf, tmp, sizeof(s->buf));
	}
			
	delay = res / 2;
	s->fr.frametype = OPBX_FRAME_VOICE;
	s->fr.subclass = OPBX_FORMAT_SLINEAR;
	s->fr.offset = OPBX_FRIENDLY_OFFSET;
	s->fr.datalen = res;
	s->fr.data = s->buf;
	s->fr.mallocd = 0;
	s->fr.samples = delay;
	*whennext = delay;
	return &s->fr;
}

static int wav_write(struct opbx_filestream *fs, struct opbx_frame *f)
{
	int res = 0;
	int x;
	short tmp[8000], *tmpi;
	float tmpf;
	if (f->frametype != OPBX_FRAME_VOICE) {
		opbx_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != OPBX_FORMAT_SLINEAR) {
		opbx_log(LOG_WARNING, "Asked to write non-SLINEAR frame (%d)!\n", f->subclass);
		return -1;
	}
	if (f->datalen > sizeof(tmp)) {
		opbx_log(LOG_WARNING, "Data length is too long\n");
		return -1;
	}
	if (!f->datalen)
		return -1;

#if 0
	printf("Data Length: %d\n", f->datalen);
#endif	

	if (fs->buf) {
		tmpi = f->data;
		/* Volume adjust here to accomodate */
		for (x=0;x<f->datalen/2;x++) {
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
		if ((write (fs->fd, tmp, f->datalen) != f->datalen) ) {
			opbx_log(LOG_WARNING, "Bad write (%d): %s\n", res, strerror(errno));
			return -1;
		}
	} else {
		opbx_log(LOG_WARNING, "Cannot write data to file.\n");
		return -1;
	}
	
	fs->bytes += f->datalen;
	update_header(fs->fd);
		
	return 0;

}

static int wav_seek(struct opbx_filestream *fs, long sample_offset, int whence)
{
	off_t min,max,cur;
	long offset=0,samples;
	
	samples = sample_offset * 2; /* SLINEAR is 16 bits mono, so sample_offset * 2 = bytes */
	min = 44; /* wav header is 44 bytes */
	cur = lseek(fs->fd, 0, SEEK_CUR);
	max = lseek(fs->fd, 0, SEEK_END);
	if (whence == SEEK_SET)
		offset = samples + min;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = samples + cur;
	else if (whence == SEEK_END)
		offset = max - samples;
        if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	}
	/* always protect the header space. */
	offset = (offset < min)?min:offset;
	return lseek(fs->fd,offset,SEEK_SET);
}

static int wav_trunc(struct opbx_filestream *fs)
{
	if(ftruncate(fs->fd, lseek(fs->fd,0,SEEK_CUR)))
		return -1;
	return update_header(fs->fd);
}

static long wav_tell(struct opbx_filestream *fs)
{
	off_t offset;
	offset = lseek(fs->fd, 0, SEEK_CUR);
	/* subtract header size to get samples, then divide by 2 for 16 bit samples */
	return (offset - 44)/2;
}

static char *wav_getcomment(struct opbx_filestream *s)
{
	return NULL;
}

int load_module()
{
	return opbx_format_register(name, exts, OPBX_FORMAT_SLINEAR,
								wav_open,
								wav_rewrite,
								wav_write,
								wav_seek,
								wav_trunc,
								wav_tell,
								wav_read,
								wav_close,
								wav_getcomment);
								
								
}

int unload_module()
{
	return opbx_format_unregister(name);
}	

int usecount()
{
	return glistcnt;
}

char *description()
{
	return desc;
}



