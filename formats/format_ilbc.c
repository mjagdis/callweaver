/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Brian K. West <brian@bkw.org>
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
 * Save to raw, headerless iLBC data.
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
#include "openpbx/endian.h"

/* Some Ideas for this code came from makeg729e.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

struct opbx_filestream {
	void *reserved[OPBX_RESERVED_POINTERS];
	/* Believe it or not, we must decode/recode to account for the
	   weird MS format */
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	struct opbx_frame fr;				/* Frame information */
	char waste[OPBX_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char ilbc[50];				/* One Real iLBC Frame */
};


OPBX_MUTEX_DEFINE_STATIC(ilbc_lock);
static int glistcnt = 0;

static char *name = "iLBC";
static char *desc = "Raw iLBC data";
static char *exts = "ilbc";

static struct opbx_filestream *ilbc_open(int fd)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct opbx_filestream *tmp;
	if ((tmp = malloc(sizeof(struct opbx_filestream)))) {
		memset(tmp, 0, sizeof(struct opbx_filestream));
		if (opbx_mutex_lock(&ilbc_lock)) {
			opbx_log(LOG_WARNING, "Unable to lock ilbc list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		tmp->fr.data = tmp->ilbc;
		tmp->fr.frametype = OPBX_FRAME_VOICE;
		tmp->fr.subclass = OPBX_FORMAT_ILBC;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		glistcnt++;
		opbx_mutex_unlock(&ilbc_lock);
		opbx_update_use_count();
	}
	return tmp;
}

static struct opbx_filestream *ilbc_rewrite(int fd, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct opbx_filestream *tmp;
	if ((tmp = malloc(sizeof(struct opbx_filestream)))) {
		memset(tmp, 0, sizeof(struct opbx_filestream));
		if (opbx_mutex_lock(&ilbc_lock)) {
			opbx_log(LOG_WARNING, "Unable to lock ilbc list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		glistcnt++;
		opbx_mutex_unlock(&ilbc_lock);
		opbx_update_use_count();
	} else
		opbx_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void ilbc_close(struct opbx_filestream *s)
{
	if (opbx_mutex_lock(&ilbc_lock)) {
		opbx_log(LOG_WARNING, "Unable to lock ilbc list\n");
		return;
	}
	glistcnt--;
	opbx_mutex_unlock(&ilbc_lock);
	opbx_update_use_count();
	close(s->fd);
	free(s);
	s = NULL;
}

static struct opbx_frame *ilbc_read(struct opbx_filestream *s, int *whennext)
{
	int res;
	/* Send a frame from the file to the appropriate channel */
	s->fr.frametype = OPBX_FRAME_VOICE;
	s->fr.subclass = OPBX_FORMAT_ILBC;
	s->fr.offset = OPBX_FRIENDLY_OFFSET;
	s->fr.samples = 240;
	s->fr.datalen = 50;
	s->fr.mallocd = 0;
	s->fr.data = s->ilbc;
	if ((res = read(s->fd, s->ilbc, 50)) != 50) {
		if (res)
			opbx_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	*whennext = s->fr.samples;
	return &s->fr;
}

static int ilbc_write(struct opbx_filestream *fs, struct opbx_frame *f)
{
	int res;
	if (f->frametype != OPBX_FRAME_VOICE) {
		opbx_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != OPBX_FORMAT_ILBC) {
		opbx_log(LOG_WARNING, "Asked to write non-iLBC frame (%d)!\n", f->subclass);
		return -1;
	}
	if (f->datalen % 50) {
		opbx_log(LOG_WARNING, "Invalid data length, %d, should be multiple of 50\n", f->datalen);
		return -1;
	}
	if ((res = write(fs->fd, f->data, f->datalen)) != f->datalen) {
			opbx_log(LOG_WARNING, "Bad write (%d/50): %s\n", res, strerror(errno));
			return -1;
	}
	return 0;
}

static char *ilbc_getcomment(struct opbx_filestream *s)
{
	return NULL;
}

static int ilbc_seek(struct opbx_filestream *fs, long sample_offset, int whence)
{
	long bytes;
	off_t min,cur,max,offset=0;
	min = 0;
	cur = lseek(fs->fd, 0, SEEK_CUR);
	max = lseek(fs->fd, 0, SEEK_END);
	
	bytes = 50 * (sample_offset / 240);
	if (whence == SEEK_SET)
		offset = bytes;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = cur + bytes;
	else if (whence == SEEK_END)
		offset = max - bytes;
	if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	}
	/* protect against seeking beyond begining. */
	offset = (offset < min)?min:offset;
	if (lseek(fs->fd, offset, SEEK_SET) < 0)
		return -1;
	return 0;
}

static int ilbc_trunc(struct opbx_filestream *fs)
{
	/* Truncate file to current length */
	if (ftruncate(fs->fd, lseek(fs->fd, 0, SEEK_CUR)) < 0)
		return -1;
	return 0;
}

static long ilbc_tell(struct opbx_filestream *fs)
{
	off_t offset;
	offset = lseek(fs->fd, 0, SEEK_CUR);
	return (offset/50)*240;
}

int load_module()
{
	return opbx_format_register(name, exts, OPBX_FORMAT_ILBC,
								ilbc_open,
								ilbc_rewrite,
								ilbc_write,
								ilbc_seek,
								ilbc_trunc,
								ilbc_tell,
								ilbc_read,
								ilbc_close,
								ilbc_getcomment);
								
								
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



