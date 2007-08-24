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
 * \brief Flat, binary, alaw PCM file format.
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
#include <sys/times.h>
#include <sys/types.h>
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

#define BUF_SIZE 160		/* 160 samples */

/* #define REALTIME_WRITE */

struct opbx_filestream {
	void *reserved[OPBX_RESERVED_POINTERS];
	/* Believe it or not, we must decode/recode to account for the
	   weird MS format */
	/* This is what a filestream means to us */
	FILE *f; /* Descriptor */
	struct opbx_frame fr;				/* Frame information */
	char waste[OPBX_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char buf[BUF_SIZE];				/* Output Buffer */
#ifdef REALTIME_WRITE
	unsigned long start_time;
#endif
};


static struct opbx_format format;

static const char desc[] = "Raw aLaw 8khz PCM Audio support";


#if 0
/* Returns time in msec since system boot. */
static unsigned long get_time(void)
{
	struct tms buf;
	clock_t cur;

	cur = times( &buf );
	if( cur < 0 )
	{
		opbx_log(OPBX_LOG_WARNING, "Cannot get current time\n" );
		return 0;
	}
	return cur * 1000 / sysconf( _SC_CLK_TCK );
}
#endif

static struct opbx_filestream *pcm_open(FILE *f)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct opbx_filestream *tmp;
	if ((tmp = malloc(sizeof(struct opbx_filestream)))) {
		memset(tmp, 0, sizeof(struct opbx_filestream));
		tmp->f = f;
		tmp->fr.data = tmp->buf;
		opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_ALAW, format.name);
		/* datalen will vary for each frame */
#ifdef REALTIME_WRITE
		tmp->start_time = get_time();
#endif
	}
	return tmp;
}

static struct opbx_filestream *pcm_rewrite(FILE *f, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct opbx_filestream *tmp;
	if ((tmp = malloc(sizeof(struct opbx_filestream)))) {
		memset(tmp, 0, sizeof(struct opbx_filestream));
		tmp->f = f;
#ifdef REALTIME_WRITE
		tmp->start_time = get_time();
#endif
	} else
		opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void pcm_close(struct opbx_filestream *s)
{
	fclose(s->f);
	free(s);
	s = NULL;
}

static struct opbx_frame *pcm_read(struct opbx_filestream *s, int *whennext)
{
	int res;
	/* Send a frame from the file to the appropriate channel */

	opbx_fr_init_ex(&s->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_ALAW, NULL);
	s->fr.offset = OPBX_FRIENDLY_OFFSET;
	s->fr.data = s->buf;
	if ((res = fread(s->buf, 1, BUF_SIZE, s->f)) < 1) {
		if (res)
			opbx_log(OPBX_LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	s->fr.samples = res;
	s->fr.datalen = res;
	*whennext = s->fr.samples;
	return &s->fr;
}

static int pcm_write(struct opbx_filestream *fs, struct opbx_frame *f)
{
	int res;
#ifdef REALTIME_WRITE
	unsigned long cur_time;
	unsigned long fpos;
	struct stat stat_buf;
#endif

	if (f->frametype != OPBX_FRAME_VOICE) {
		opbx_log(OPBX_LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != OPBX_FORMAT_ALAW) {
		opbx_log(OPBX_LOG_WARNING, "Asked to write non-alaw frame (%d)!\n", f->subclass);
		return -1;
	}

#ifdef REALTIME_WRITE
	cur_time = get_time();
	fpos = ( cur_time - fs->start_time ) * 8;	/* 8 bytes per msec */
	/* Check if we have written to this position yet. If we have, then increment pos by one frame
	*  for some degree of protection against receiving packets in the same clock tick.
	*/
	
	fstat(fileno(fs->f), &stat_buf );
	if (stat_buf.st_size > fpos ) {
		fpos += f->datalen;	/* Incrementing with the size of this current frame */
	}

	if (stat_buf.st_size < fpos) {
		/* fill the gap with 0x55 rather than 0. */
		char buf[ 512 ];
		unsigned long cur, to_write;

		cur = stat_buf.st_size;
		if (fseek(fs->f, cur, SEEK_SET) < 0) {
			opbx_log(OPBX_LOG_WARNING, "Cannot seek in file: %s\n", strerror(errno) );
			return -1;
		}
		memset(buf, 0x55, 512);
		while (cur < fpos) {
			to_write = fpos - cur;
			if (to_write > 512) {
				to_write = 512;
			}
			fwrite(buf, 1, to_write, fs->f);
			cur += to_write;
		}
	}


	if (fseek(s->f, fpos, SEEK_SET) < 0) {
		opbx_log(OPBX_LOG_WARNING, "Cannot seek in file: %s\n", strerror(errno) );
		return -1;
	}
#endif	/* REALTIME_WRITE */
	
	if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen) {
			opbx_log(OPBX_LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
			return -1;
	}
	return 0;
}

static int pcm_seek(struct opbx_filestream *fs, long sample_offset, int whence)
{
	off_t offset=0,min,cur,max;

	min = 0;
	cur = ftell(fs->f);
	fseek(fs->f, 0, SEEK_END);
	max = ftell(fs->f);
	if (whence == SEEK_SET)
		offset = sample_offset;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = sample_offset + cur;
	else if (whence == SEEK_END)
		offset = max - sample_offset;
	if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	}
	/* Always protect against seeking past begining */
	offset = (offset < min)?min:offset;
	return fseek(fs->f, offset, SEEK_SET);
}

static int pcm_trunc(struct opbx_filestream *fs)
{
	return ftruncate(fileno(fs->f), ftell(fs->f));
}

static long pcm_tell(struct opbx_filestream *fs)
{
	off_t offset;
	offset = ftell(fs->f);
	return offset;
}


static char *pcm_getcomment(struct opbx_filestream *s)
{
	return NULL;
}


static struct opbx_format format = {
	.name = "alaw",
	.exts = "alaw|al",
	.format = OPBX_FORMAT_ALAW,
	.open = pcm_open,
	.rewrite = pcm_rewrite,
	.write = pcm_write,
	.seek = pcm_seek,
	.trunc = pcm_trunc,
	.tell = pcm_tell,
	.read = pcm_read,
	.close = pcm_close,
	.getcomment = pcm_getcomment,
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
