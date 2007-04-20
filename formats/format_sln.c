/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Anthony Minessale
 * Anthony Minessale (anthmct@yahoo.com)
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
 * \brief RAW SLINEAR Format
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

OPENPBX_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/channel.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/sched.h"
#include "callweaver/module.h"
#include "confdefs.h"

#define BUF_SIZE 320		/* 320 samples */

struct opbx_filestream {
	void *reserved[OPBX_RESERVED_POINTERS];
	/* This is what a filestream means to us */
	FILE *f; /* Descriptor */
	struct opbx_channel *owner;
	struct opbx_frame fr;				/* Frame information */
	char waste[OPBX_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char buf[BUF_SIZE];				/* Output Buffer */
	struct timeval last;
};


OPBX_MUTEX_DEFINE_STATIC(slinear_lock);
static int glistcnt = 0;

static char *name = "sln";
static char *desc = "Raw Signed Linear Audio support (SLN)";
static char *exts = "sln|raw";

static struct opbx_filestream *slinear_open(FILE *f)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct opbx_filestream *tmp;
	if ((tmp = malloc(sizeof(struct opbx_filestream)))) {
		memset(tmp, 0, sizeof(struct opbx_filestream));
		if (opbx_mutex_lock(&slinear_lock)) {
			opbx_log(LOG_WARNING, "Unable to lock slinear list\n");
			free(tmp);
			return NULL;
		}
		tmp->f = f;
        opbx_fr_init_ex(&tmp->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, name);
		tmp->fr.data = tmp->buf;
		/* datalen will vary for each frame */
		glistcnt++;
		opbx_mutex_unlock(&slinear_lock);
		opbx_update_use_count();
	}
	return tmp;
}

static struct opbx_filestream *slinear_rewrite(FILE *f, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct opbx_filestream *tmp;
	if ((tmp = malloc(sizeof(struct opbx_filestream)))) {
		memset(tmp, 0, sizeof(struct opbx_filestream));
		if (opbx_mutex_lock(&slinear_lock)) {
			opbx_log(LOG_WARNING, "Unable to lock slinear list\n");
			free(tmp);
			return NULL;
		}
		tmp->f = f;
		glistcnt++;
		opbx_mutex_unlock(&slinear_lock);
		opbx_update_use_count();
	} else
		opbx_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void slinear_close(struct opbx_filestream *s)
{
	if (opbx_mutex_lock(&slinear_lock)) {
		opbx_log(LOG_WARNING, "Unable to lock slinear list\n");
		return;
	}
	glistcnt--;
	opbx_mutex_unlock(&slinear_lock);
	opbx_update_use_count();
	fclose(s->f);
	free(s);
	s = NULL;
}

static struct opbx_frame *slinear_read(struct opbx_filestream *s, int *whennext)
{
	int res;
	int delay;
	/* Send a frame from the file to the appropriate channel */

    opbx_fr_init_ex(&s->fr, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, NULL);
	s->fr.offset = OPBX_FRIENDLY_OFFSET;
	s->fr.data = s->buf;
	if ((res = fread(s->buf, 1, BUF_SIZE, s->f)) < 1)
    {
		if (res)
			opbx_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	s->fr.samples = res/sizeof(int16_t);
	s->fr.datalen = res;
	delay = s->fr.samples;
	*whennext = delay;
	return &s->fr;
}

static int slinear_write(struct opbx_filestream *fs, struct opbx_frame *f)
{
	int res;
	if (f->frametype != OPBX_FRAME_VOICE) {
		opbx_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != OPBX_FORMAT_SLINEAR) {
		opbx_log(LOG_WARNING, "Asked to write non-slinear frame (%d)!\n", f->subclass);
		return -1;
	}
	if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen) {
			opbx_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
			return -1;
	}
	return 0;
}

static int slinear_seek(struct opbx_filestream *fs, long sample_offset, int whence)
{
	off_t offset=0,min,cur,max;

	min = 0;
	sample_offset <<= 1;
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
	/* always protect against seeking past begining. */
	offset = (offset < min)?min:offset;
	return fseek(fs->f, offset, SEEK_SET) / 2;
}

static int slinear_trunc(struct opbx_filestream *fs)
{
	return ftruncate(fileno(fs->f), ftell(fs->f));
}

static long slinear_tell(struct opbx_filestream *fs)
{
	off_t offset;
	offset = ftell(fs->f);
	return offset / 2;
}

static char *slinear_getcomment(struct opbx_filestream *s)
{
	return NULL;
}

int load_module()
{
	return opbx_format_register(name, exts, OPBX_FORMAT_SLINEAR,
								slinear_open,
								slinear_rewrite,
								slinear_write,
								slinear_seek,
								slinear_trunc,
								slinear_tell,
								slinear_read,
								slinear_close,
								slinear_getcomment);
								
								
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



