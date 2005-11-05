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
 * Save GSM in the proprietary Microsoft format.
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

#include "openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL$", "$Revision$")

#include "openpbx/lock.h"
#include "openpbx/channel.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/sched.h"
#include "openpbx/module.h"
#include "confdefs.h"

#include "msgsm.h"

/* Some Ideas for this code came from makewave.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

/* begin binary data: */
char msgsm_silence[] = /* 65 */
{0x48,0x17,0xD6,0x84,0x02,0x80,0x24,0x49,0x92,0x24,0x89,0x02,0x80,0x24,0x49
,0x92,0x24,0x89,0x02,0x80,0x24,0x49,0x92,0x24,0x89,0x02,0x80,0x24,0x49,0x92
,0x24,0x09,0x82,0x74,0x61,0x4D,0x28,0x00,0x48,0x92,0x24,0x49,0x92,0x28,0x00
,0x48,0x92,0x24,0x49,0x92,0x28,0x00,0x48,0x92,0x24,0x49,0x92,0x28,0x00,0x48
,0x92,0x24,0x49,0x92,0x00};
/* end binary data. size = 65 bytes */

struct opbx_filestream {
	void *reserved[OPBX_RESERVED_POINTERS];
	/* Believe it or not, we must decode/recode to account for the
	   weird MS format */
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	struct opbx_frame fr;				/* Frame information */
	char waste[OPBX_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char gsm[66];				/* Two Real GSM Frames */
	int foffset;
	int secondhalf;						/* Are we on the second half */
	struct timeval last;
};


OPBX_MUTEX_DEFINE_STATIC(wav_lock);
static int glistcnt = 0;

static char *name = "wav49";
static char *desc = "Microsoft WAV format (Proprietary GSM)";
static char *exts = "WAV|wav49";

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
	int fmt, hsize, fact;
	short format, chans;
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
	if (ltohl(hsize) != 20) {
		opbx_log(LOG_WARNING, "Unexpected header size %d\n", ltohl(hsize));
		return -1;
	}
	if (read(fd, &format, 2) != 2) {
		opbx_log(LOG_WARNING, "Read failed (format)\n");
		return -1;
	}
	if (ltohs(format) != 49) {
		opbx_log(LOG_WARNING, "Not a GSM file %d\n", ltohs(format));
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
	if (read(fd, &freq, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (X_1)\n");
		return -1;
	}
	/* Ignore the two weird fields */
	if (read(fd, &freq, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (X_2/X_3)\n");
		return -1;
	}
	/* Ignore the byte frequency */
	if (read(fd, &freq, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (Y_1)\n");
		return -1;
	}
	/* Check for the word fact */
	if (read(fd, &fact, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (fact)\n");
		return -1;
	}
	if (memcmp(&fact, "fact", 4)) {
		opbx_log(LOG_WARNING, "Does not say fact\n");
		return -1;
	}
	/* Ignore the "fact value" */
	if (read(fd, &fact, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (fact header)\n");
		return -1;
	}
	if (read(fd, &fact, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (fact value)\n");
		return -1;
	}
	/* Check for the word data */
	if (read(fd, &data, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (data)\n");
		return -1;
	}
	if (memcmp(&data, "data", 4)) {
		opbx_log(LOG_WARNING, "Does not say data\n");
		return -1;
	}
	/* Ignore the data length */
	if (read(fd, &data, 4) != 4) {
		opbx_log(LOG_WARNING, "Read failed (data)\n");
		return -1;
	}
	return 0;
}

static int update_header(int fd)
{
	off_t cur,end,bytes;
	int datalen,filelen;
	
	cur = lseek(fd, 0, SEEK_CUR);
	end = lseek(fd, 0, SEEK_END);
	/* in a gsm WAV, data starts 60 bytes in */
	bytes = end - 60;
	datalen = htoll((bytes + 1) & ~0x1);
	filelen = htoll(52 + ((bytes + 1) & ~0x1));
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
	if (lseek(fd, 56, SEEK_SET) != 56) {
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
	if (write(fd, &x_1, 4) != 4) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &x_2, 2) != 2) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &x_3, 2) != 2) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, "fact", 4) != 4) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &fhs, 4) != 4) {
		opbx_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (write(fd, &y_1, 4) != 4) {
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
		if (check_header(fd)) {
			free(tmp);
			return NULL;
		}
		if (opbx_mutex_lock(&wav_lock)) {
			opbx_log(LOG_WARNING, "Unable to lock wav list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		tmp->fr.data = tmp->gsm;
		tmp->fr.frametype = OPBX_FRAME_VOICE;
		tmp->fr.subclass = OPBX_FORMAT_GSM;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		tmp->secondhalf = 0;
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
	if (lseek(s->fd, 0, SEEK_END) & 0x1)
		write(s->fd, &zero, 1);
	close(s->fd);
	free(s);
	s = NULL;
}

static struct opbx_frame *wav_read(struct opbx_filestream *s, int *whennext)
{
	int res;
	char msdata[66];
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = OPBX_FRAME_VOICE;
	s->fr.subclass = OPBX_FORMAT_GSM;
	s->fr.offset = OPBX_FRIENDLY_OFFSET;
	s->fr.samples = 160;
	s->fr.datalen = 33;
	s->fr.mallocd = 0;
	if (s->secondhalf) {
		/* Just return a frame based on the second GSM frame */
		s->fr.data = s->gsm + 33;
	} else {
		if ((res = read(s->fd, msdata, 65)) != 65) {
			if (res && (res != 1))
				opbx_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
			return NULL;
		}
		/* Convert from MS format to two real GSM frames */
		conv65(msdata, s->gsm);
		s->fr.data = s->gsm;
	}
	s->secondhalf = !s->secondhalf;
	*whennext = 160;
	return &s->fr;
}

static int wav_write(struct opbx_filestream *fs, struct opbx_frame *f)
{
	int res;
	char msdata[66];
	int len =0;
	int alreadyms=0;
	if (f->frametype != OPBX_FRAME_VOICE) {
		opbx_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != OPBX_FORMAT_GSM) {
		opbx_log(LOG_WARNING, "Asked to write non-GSM frame (%d)!\n", f->subclass);
		return -1;
	}
	if (!(f->datalen % 65)) 
		alreadyms = 1;
	while(len < f->datalen) {
		if (alreadyms) {
			fs->secondhalf = 0;
			if ((res = write(fs->fd, f->data + len, 65)) != 65) {
				opbx_log(LOG_WARNING, "Bad write (%d/65): %s\n", res, strerror(errno));
				return -1;
			}
			update_header(fs->fd);
			len += 65;
		} else {
			if (fs->secondhalf) {
				memcpy(fs->gsm + 33, f->data + len, 33);
				conv66(fs->gsm, msdata);
				if ((res = write(fs->fd, msdata, 65)) != 65) {
					opbx_log(LOG_WARNING, "Bad write (%d/65): %s\n", res, strerror(errno));
					return -1;
				}
				update_header(fs->fd);
			} else {
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
	off_t offset=0,distance,cur,min,max;
	min = 60;
	cur = lseek(fs->fd, 0, SEEK_CUR);
	max = lseek(fs->fd, 0, SEEK_END);
	/* I'm getting sloppy here, I'm only going to go to even splits of the 2
	 * frames, if you want tighter cuts use format_gsm, format_pcm, or format_wav */
	distance = (sample_offset/320) * 65;
	if(whence == SEEK_SET)
		offset = distance + min;
	else if(whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = distance + cur;
	else if(whence == SEEK_END)
		offset = max - distance;
	/* always protect against seeking past end of header */
	offset = (offset < min)?min:offset;
	if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	} else if (offset > max) {
		int i;
		lseek(fs->fd, 0, SEEK_END);
		for (i=0; i< (offset - max) / 65; i++) {
			write(fs->fd, msgsm_silence, 65);
		}
	}
	fs->secondhalf = 0;
	return lseek(fs->fd, offset, SEEK_SET);
}

static int wav_trunc(struct opbx_filestream *fs)
{
	if(ftruncate(fs->fd, lseek(fs->fd, 0, SEEK_CUR)))
		return -1;
	return update_header(fs->fd);
}

static long wav_tell(struct opbx_filestream *fs)
{
	off_t offset;
	offset = lseek(fs->fd, 0, SEEK_CUR);
	/* since this will most likely be used later in play or record, lets stick
	 * to that level of resolution, just even frames boundaries */
	return (offset - 52)/65*320;
}

static char *wav_getcomment(struct opbx_filestream *s)
{
	return NULL;
}

int load_module()
{
	return opbx_format_register(name, exts, OPBX_FORMAT_GSM,
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



