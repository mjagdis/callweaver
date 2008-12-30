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
 * \brief Generic File Format Support.
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/atomic.h"
#include "callweaver/frame.h"
#include "callweaver/file.h"
#include "callweaver/cli.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/generator.h"
#include "callweaver/options.h"
#include "callweaver/translate.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/app.h"
#include "callweaver/pbx.h"


static const char *format_object_name(struct cw_object *obj)
{
	struct cw_format *it = container_of(obj, struct cw_format, obj);
	return it->name;
}

static int format_object_cmp(struct cw_object *a, struct cw_object *b)
{
	struct cw_format *format_a = container_of(a, struct cw_format, obj);
	struct cw_format *format_b = container_of(b, struct cw_format, obj);

	return strcmp(format_a->name, format_b->name);
}

const struct cw_object_isa cw_object_isa_format = {
	.name = format_object_name,
};

struct cw_registry format_registry = {
	.name = "Format",
	.lock = CW_MUTEX_INIT_VALUE,
	.cmp = format_object_cmp,
};


struct cw_filestream {
	struct cw_format *fmt;
	void *pvt;
	atomic_t running;
	int flags;
	mode_t mode;
	char *filename;
	char *realfilename;
	/* Video file stream */
	struct cw_filestream *vfs;
	/* Transparently translate from another format -- just once */
	struct cw_trans_pvt *trans;
	struct cw_tranlator_pvt *tr;
	int lastwriteformat;
	int lasttimeout;
	struct cw_channel *owner;
	/* Currently the generator instance is only used for video
	 * streams, audio streams use the generator instance in the
	 * channel struct. This is because channels are not currently
	 * reference counted so the channel clean up code needs to
	 * be able to find and deactivate a running generator.
	 * (At the moment you cannot have a video stream unless there
	 * is an accompanying audio stream)
	 */
	struct cw_generator_instance generator;
};


int cw_stopstream(struct cw_channel *chan)
{
	struct cw_filestream *fs;

	/* Stop a running stream if there is one */
	if ((fs = chan->stream)) {
		chan->stream = NULL;

		cw_generator_deactivate(&fs->owner->generator);
		if (fs->vfs)
			cw_generator_deactivate(&fs->vfs->generator);

		if (chan->oldwriteformat && cw_set_write_format(chan, chan->oldwriteformat))
			cw_log(CW_LOG_WARNING, "Unable to restore format back to %d\n", chan->oldwriteformat);

		cw_closestream(fs);
	}
	return 0;
}

int cw_writestream(struct cw_filestream *fs, struct cw_frame *f)
{
	struct cw_frame *trf;
	int res = -1;
	int alt=0;
	if (f->frametype == CW_FRAME_VIDEO) {
		if (fs->fmt->format < CW_FORMAT_MAX_AUDIO) {
			/* This is the audio portion.  Call the video one... */
			if (!fs->vfs && fs->filename) {
				/* XXX Support other video formats XXX */
				const char *type = "h263";
				fs->vfs = cw_writefile(fs->filename, type, NULL, fs->flags, 0, fs->mode);
				cw_log(CW_LOG_DEBUG, "Opened video output file\n");
			}
			if (fs->vfs)
				return cw_writestream(fs->vfs, f);
			/* Ignore */
			return 0;				
		} else {
			/* Might / might not have mark set */
			alt = 1;
		}
	} else if (f->frametype != CW_FRAME_VOICE) {
		cw_log(CW_LOG_WARNING, "Tried to write non-voice frame\n");
		return -1;
	}
	if (((fs->fmt->format | alt) & f->subclass) == f->subclass) {
		res =  fs->fmt->write(fs->pvt, f);
		if (res < 0) 
			cw_log(CW_LOG_WARNING, "Natural write failed\n");
		if (res > 0)
			cw_log(CW_LOG_WARNING, "Huh??\n");
		return res;
	} else {
		/* XXX If they try to send us a type of frame that isn't the normal frame, and isn't
		       the one we've setup a translator for, we do the "wrong thing" XXX */
		if (fs->trans && (f->subclass != fs->lastwriteformat)) {
			cw_translator_free_path(fs->trans);
			fs->trans = NULL;
		}
		if (!fs->trans) 
			fs->trans = cw_translator_build_path(fs->fmt->format, 8000, f->subclass, 8000);
		if (!fs->trans)
			cw_log(CW_LOG_WARNING, "Unable to translate to format %s, source format %s\n", fs->fmt->name, cw_getformatname(f->subclass));
		else {
			fs->lastwriteformat = f->subclass;
			res = 0;
			/* Get the translated frame but don't consume the original in case they're using it on another stream */
			trf = cw_translate(fs->trans, f, 0);
			if (trf) {
				res = fs->fmt->write(fs->pvt, trf);
				if (res) 
					cw_log(CW_LOG_WARNING, "Translated frame write failed\n");
			} else
				res = 0;
		}
		return res;
	}
}

static int copy(const char *infile, const char *outfile)
{
	int ifd;
	int ofd;
	int res;
	int len;
	char buf[4096];

	if ((ifd = open(infile, O_RDONLY)) < 0) {
		cw_log(CW_LOG_WARNING, "Unable to open %s in read-only mode\n", infile);
		return -1;
	}
	if ((ofd = open(outfile, O_WRONLY | O_TRUNC | O_CREAT, 0600)) < 0) {
		cw_log(CW_LOG_WARNING, "Unable to open %s in write-only mode\n", outfile);
		close(ifd);
		return -1;
	}
	do {
		len = read(ifd, buf, sizeof(buf));
		if (len < 0) {
			cw_log(CW_LOG_WARNING, "Read failed on %s: %s\n", infile, strerror(errno));
			close(ifd);
			close(ofd);
			unlink(outfile);
		}
		if (len) {
			res = write(ofd, buf, len);
			if (res != len) {
				cw_log(CW_LOG_WARNING, "Write failed on %s (%d of %d): %s\n", outfile, res, len, strerror(errno));
				close(ifd);
				close(ofd);
				unlink(outfile);
			}
		}
	} while(len);
	close(ifd);
	close(ofd);
	return 0;
}

static char *build_filename(const char *filename, const char *ext)
{
	char *fn = NULL, type[16] = "";
	int fnsize = 0;

	if (!strcmp(ext, "wav49")) {
		cw_copy_string(type, "WAV", sizeof(type));
	} else {
		cw_copy_string(type, ext, sizeof(type));
	}

	if (filename[0] == '/') {
		fnsize = strlen(filename) + strlen(type) + 2;
		fn = malloc(fnsize);
		if (fn)
			snprintf(fn, fnsize, "%s.%s", filename, type);
	} else {
		fnsize = strlen(cw_config_CW_SOUNDS_DIR) + strlen(filename) + strlen(type) + 3;
		fn = malloc(fnsize);
		if (fn)
			snprintf(fn, fnsize, "%s/%s.%s", cw_config_CW_SOUNDS_DIR, filename, type);
	}

	return fn;
}

static int exts_compare(const char *exts, const char *type)
{
	char *stringp = NULL, *ext;
	char tmp[256];

	cw_copy_string(tmp, exts, sizeof(tmp));
	stringp = tmp;
	while ((ext = strsep(&stringp, "|,"))) {
		if (!strcmp(ext, type)) {
			return 1;
		}
	}

	return 0;
}

#define ACTION_EXISTS 1
#define ACTION_DELETE 2
#define ACTION_RENAME 3
#define ACTION_COPY   4

struct filehelper_args {
	const char *filename;
	const char *filename2;
	const char *fmt;
	int action;
	int res;
};

static int filehelper_one(struct cw_object *obj, void *data)
{
	struct cw_format *f = container_of(obj, struct cw_format, obj);
	struct filehelper_args *args = data;

	/* Check for a specific format */
	if (!args->fmt || exts_compare(f->exts, args->fmt)) {
		char *exts = cw_strdupa(f->exts);
		char *ext;
		/* Try each kind of extension */
		for (ext = strsep(&exts, "|,"); ext; ext = strsep(&exts, "|,")) {
			char *fn;
			if ((fn = build_filename(args->filename, ext))) {
				struct stat st;
				char *nfn;
				if (!stat(fn, &st)) {
					switch (args->action) {
					case ACTION_EXISTS:
						args->res |= f->format;
						break;
					case ACTION_DELETE:
						if (unlink(fn)) {
							args->res = -1;
							cw_log(CW_LOG_WARNING, "unlink(%s) failed: %s\n", fn, strerror(errno));
						}
						break;
					case ACTION_RENAME:
						if ((nfn = build_filename(args->filename2, ext))) {
							if (rename(fn, nfn)) {
								args->res = -1;
								cw_log(CW_LOG_WARNING, "rename(%s,%s) failed: %s\n", fn, nfn, strerror(errno));
							}
							free(nfn);
						} else {
							args->res = -1;
							cw_log(CW_LOG_WARNING, "Out of memory\n");
						}
						break;
					case ACTION_COPY:
						if ((nfn = build_filename(args->filename2, ext))) {
							if (copy(fn, nfn)) {
								args->res = -1;
								cw_log(CW_LOG_WARNING, "copy(%s,%s) failed: %s\n", fn, nfn, strerror(errno));
							}
							free(nfn);
						} else {
							args->res = -1;
							cw_log(CW_LOG_WARNING, "Out of memory\n");
						}
						break;
					default:
						cw_log(CW_LOG_WARNING, "Unknown helper %d\n", args->action);
						break;
					}
				}
				free(fn);
			}
		}
	}

	return 0;
}

static int cw_filehelper(const char *filename, const char *filename2, const char *fmt, int action)
{
	struct filehelper_args args = {
		filename, filename2, fmt, action, 0
	};

	args.res = (action == ACTION_EXISTS ? 0 : -1);
	cw_registry_iterate(&format_registry, filehelper_one, &args);
	return args.res;
}


struct fileopen_args {
	const char *filename;
	struct cw_channel *chan;
	const char *fmt;
	struct cw_filestream *s;
};

static int fileopen_one(struct cw_object *obj, void *data)
{
	struct cw_format *f = container_of(obj, struct cw_format, obj);
	struct fileopen_args *args = data;
	FILE *bfile;

	if (args->chan && (!(args->chan->writeformat & f->format) && !((f->format >= CW_FORMAT_MAX_AUDIO) && args->fmt)))
		return 0;

	/* Check for a specific format */
	if (!args->fmt || exts_compare(f->exts, args->fmt)) {
		char *exts = cw_strdupa(f->exts);
		char *ext;
		/* Try each kind of extension */
		for (ext = strsep(&exts, "|,"); ext; ext = strsep(&exts, "|,")) {
			char *fn;
			if ((fn = build_filename(args->filename, ext))) {
				if ((bfile = fopen(fn, "r"))) {
					if ((args->s->pvt = f->open(bfile))) {
						args->s->fmt = cw_object_dup(f);
						args->s->owner = args->chan;
						args->s->lasttimeout = -1;
						free(fn);
						return 1;
					}
					fclose(bfile);
				}
				free(fn);
			}
		}
	}

	return 0;
}

static struct cw_filestream *cw_fileopen(struct cw_channel *chan, const char *filename, const char *fmt)
{
	struct fileopen_args args = {
		.filename = filename,
		.chan = chan,
		.fmt = fmt,
	};

	if (!(args.s = calloc(1, sizeof(*args.s)))) {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		return NULL;
	}
	atomic_set(&args.s->running, 0);
	args.s->generator.tid = CW_PTHREADT_NULL;

	cw_registry_iterate(&format_registry, fileopen_one, &args);
	if (args.s->fmt)
		return args.s;

	free(args.s);
	return NULL;
}


struct cw_filestream *cw_openstream(struct cw_channel *chan, const char *filename, const char *preflang)
{
	return cw_openstream_full(chan, filename, preflang, 0);
}

struct cw_filestream *cw_openstream_full(struct cw_channel *chan, const char *filename, const char *preflang, int asis)
{
	/* This is a fairly complex routine.  Essentially we should do 
	   the following:
	   
	   1) Find which file handlers produce our type of format.
	   2) Look for a filename which it can handle.
	   3) If we find one, then great.  
	   4) If not, see what files are there
	   5) See what we can actually support
	   6) Choose the one with the least costly translator path and
	       set it up.
	*/
	int fmts = -1;
	char filename2[256]="";
	char filename3[256];
	char *endpart;
	int res;

	if (!asis) {
		/* do this first, otherwise we detect the wrong writeformat */
		cw_stopstream(chan);
		cw_generator_deactivate(&chan->generator);
	}
	if (cw_strlen_zero(preflang)) {
		preflang = DEFAULT_LANGUAGE;
	}

	/* Verify custom prompt first so it override default one */
	snprintf(filename2, sizeof(filename2), "%s-custom/%s", preflang, filename);
	fmts = cw_fileexists(filename2, NULL, NULL);
	if (!fmts && preflang && !cw_strlen_zero(preflang)) {
		snprintf(filename2, sizeof(filename2), "%s/%s", preflang, filename);
		fmts = cw_fileexists(filename2, NULL, NULL);
	}
				
	
	/* previous way to check sounds location (to keep backward compability, including voicemail) */
	if (!fmts && preflang && !cw_strlen_zero(preflang)) {
		strncpy(filename3, filename, sizeof(filename3) - 1);
		endpart = strrchr(filename3, '/');
		if (endpart) {
			*endpart = '\0';
			endpart++;
			snprintf(filename2, sizeof(filename2), "%s/%s/%s", filename3, preflang, endpart);
		} else
			snprintf(filename2, sizeof(filename2), "%s/%s", preflang, filename);

		strncpy(filename2, filename, sizeof(filename2)-1);
		fmts = cw_fileexists(filename2, NULL, NULL);
	}
	if (!fmts) {
		cw_log(CW_LOG_WARNING, "File %s does not exist in any format\n", filename);
		return NULL;
	}
	chan->oldwriteformat = chan->writeformat;
	/* Set the channel to a format we can work with */
	res = cw_set_write_format(chan, fmts);
	
 	chan->stream = cw_fileopen(chan, filename2, NULL);
	if (chan->stream)
		return chan->stream;

	cw_set_write_format(chan, chan->oldwriteformat);
	return NULL;
}

static struct cw_filestream *cw_openvstream(struct cw_channel *chan, const char *filename, const char *preflang)
{
	/* This is a fairly complex routine.  Essentially we should do 
	   the following:
	   
	   1) Find which file handlers produce our type of format.
	   2) Look for a filename which it can handle.
	   3) If we find one, then great.  
	   4) If not, see what files are there
	   5) See what we can actually support
	   6) Choose the one with the least costly translator path and
	       set it up.
		   
	*/
	char filename2[256];
	char lang2[MAX_LANGUAGE];
	/* XXX H.263 only XXX */
	char *fmt = "h263";
	struct cw_filestream *fs;
	int fmts = 0;

	if (!cw_strlen_zero(preflang)) {
		snprintf(filename2, sizeof(filename2), "%s/%s", preflang, filename);
		fmts = cw_fileexists(filename2, fmt, NULL);
		if (!fmts) {
			cw_copy_string(lang2, preflang, sizeof(lang2));
			snprintf(filename2, sizeof(filename2), "%s/%s", lang2, filename);
			fmts = cw_fileexists(filename2, fmt, NULL);
		}
	}
	if (!fmts) {
		cw_copy_string(filename2, filename, sizeof(filename2));
		fmts = cw_fileexists(filename2, fmt, NULL);
	}
	if (!fmts) {
		return NULL;
	}

 	fs = cw_fileopen(chan, filename2, fmt);
	if (fs)
		return fs;

	cw_log(CW_LOG_WARNING, "File %s has video but couldn't be opened\n", filename);
	return NULL;
}

struct cw_frame *cw_readframe(struct cw_filestream *s)
{
	struct cw_frame *f = NULL;
	int whennext = 0;	
	if (s && s->fmt)
		f = s->fmt->read(s->pvt, &whennext);
	return f;
}


static void filestream_release(struct cw_channel *chan, void *params)
{
//	struct cw_filestream *fs = params;
}

static void *filestream_alloc(struct cw_channel *chan, void *params)
{
	struct cw_filestream *fs = params;

	atomic_inc(&fs->running);
	return fs;
}

static struct cw_frame *filestream_generate(struct cw_channel *chan, void *data, int samples)
{
	struct cw_filestream *fs = data;
	struct cw_frame *f;
	int whennext = 0;

	f = fs->fmt->read(fs->pvt, &whennext);
	if (!f)
		atomic_dec(&fs->running);
	return f;
}

static struct cw_generator filestream_generator =
{
	alloc: filestream_alloc,
	release: filestream_release,
	generate: filestream_generate,
};


int cw_playstream(struct cw_filestream *fs)
{
	if (fs->vfs)
		cw_generator_activate(fs->vfs->owner, &fs->vfs->generator, &filestream_generator, fs->vfs);
	cw_generator_activate(fs->owner, &fs->owner->generator, &filestream_generator, fs);
	return 0;
}

int cw_seekstream(struct cw_filestream *fs, long sample_offset, int whence)
{
	return fs->fmt->seek(fs->pvt, sample_offset, whence);
}

int cw_truncstream(struct cw_filestream *fs)
{
	return fs->fmt->trunc(fs->pvt);
}

long cw_tellstream(struct cw_filestream *fs)
{
	return fs->fmt->tell(fs->pvt);
}

int cw_stream_fastforward(struct cw_filestream *fs, long ms)
{
	/* I think this is right, 8000 samples per second, 1000 ms a second so 8
	 * samples per ms  */
	long samples = ms * 8;
	return cw_seekstream(fs, samples, SEEK_CUR);
}

int cw_stream_rewind(struct cw_filestream *fs, long ms)
{
	long samples = ms * 8;
	samples = samples * -1;
	return cw_seekstream(fs, samples, SEEK_CUR);
}

int cw_closestream(struct cw_filestream *f)
{
	char *cmd = NULL;
	size_t size = 0;

	if (f->vfs)
		cw_closestream(f->vfs);

	if (f->trans)
		cw_translator_free_path(f->trans);

	f->fmt->close(f->pvt);
	if (f->realfilename && f->filename) {
		size = strlen(f->filename) + strlen(f->realfilename) + 15;
		cmd = alloca(size);
		memset(cmd,0,size);
		snprintf(cmd,size,"/bin/mv -f %s %s",f->filename,f->realfilename);
		cw_safe_system(cmd);
	}

	if (f->filename)
		free(f->filename);
	if (f->realfilename)
		free(f->realfilename);

	cw_object_put(f->fmt);
	free(f);
	return 0;
}


int cw_fileexists(const char *filename, const char *fmt, const char *preflang)
{
	char filename2[256];
	char tmp[256];
	char *postfix;
	char *prefix;
	char *c;
	char lang2[MAX_LANGUAGE];
	int res = 0;

	if (!cw_strlen_zero(preflang)) {
		/* Insert the language between the last two parts of the path */
		cw_copy_string(tmp, filename, sizeof(tmp));
		c = strrchr(tmp, '/');
		if (c) {
			*c = '\0';
			postfix = c+1;
			prefix = tmp;
			snprintf(filename2, sizeof(filename2), "%s/%s/%s", prefix, preflang, postfix);
		} else {
			postfix = tmp;
			prefix="";
			snprintf(filename2, sizeof(filename2), "%s/%s", preflang, postfix);
		}
		res = cw_filehelper(filename2, NULL, fmt, ACTION_EXISTS);
		if (res == 0) {
			char *stringp=NULL;
			cw_copy_string(lang2, preflang, sizeof(lang2));
			stringp=lang2;
			strsep(&stringp, "_");
			/* If language is a specific locality of a language (like es_MX), strip the locality and try again */
			if (strcmp(lang2, preflang)) {
				if (cw_strlen_zero(prefix)) {
					snprintf(filename2, sizeof(filename2), "%s/%s", lang2, postfix);
				} else {
					snprintf(filename2, sizeof(filename2), "%s/%s/%s", prefix, lang2, postfix);
				}
				res = cw_filehelper(filename2, NULL, fmt, ACTION_EXISTS);
			}
		}
	}

	/* Fallback to no language (usually winds up being American English) */
	if (res == 0) {
		res = cw_filehelper(filename, NULL, fmt, ACTION_EXISTS);
	}
	return res;
}

int cw_filedelete(const char *filename, const char *fmt)
{
	return cw_filehelper(filename, NULL, fmt, ACTION_DELETE);
}

int cw_filerename(const char *filename, const char *filename2, const char *fmt)
{
	return cw_filehelper(filename, filename2, fmt, ACTION_RENAME);
}

int cw_filecopy(const char *filename, const char *filename2, const char *fmt)
{
	return cw_filehelper(filename, filename2, fmt, ACTION_COPY);
}

int cw_streamfile(struct cw_channel *chan, const char *filename, const char *preflang)
{
	struct cw_filestream *fs;

	fs = cw_openstream(chan, filename, preflang);
	fs->vfs = cw_openvstream(chan, filename, preflang);
	if (fs->vfs)
		cw_log(CW_LOG_DEBUG, "Ooh, found a video stream, too\n");
	if (fs){
		if (cw_playstream(fs))
			return -1;
#if 1
		if (option_verbose > 2)
			cw_verbose(VERBOSE_PREFIX_3 "Playing '%s' (language '%s')\n", filename, preflang ? preflang : "default");
#endif
		return 0;
	}
	cw_log(CW_LOG_WARNING, "Unable to open %s (format %s): %s\n", filename, cw_getformatname(chan->nativeformats), strerror(errno));
	return -1;
}


struct cw_filestream *cw_readfile(const char *filename, const char *fmt, const char *comment, int flags, int check, mode_t mode)
{
	struct fileopen_args args = {
		.filename = filename,
		.fmt = fmt,
	};

	if (!(args.s = calloc(1, sizeof(*args.s)))) {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		return NULL;
	}
	atomic_set(&args.s->running, 0);
	args.s->generator.tid = CW_PTHREADT_NULL;

	cw_registry_iterate(&format_registry, fileopen_one, &args);
	if (args.s->fmt)
		return args.s;

	free(args.s);
	return NULL;
}


struct writefile_args {
	const char *filename;
	const char *type;
	const char *comment;
	int flags, myflags;
	mode_t mode;
	struct cw_filestream *s;
};

static int writefile_one(struct cw_object *obj, void *data)
{
	struct cw_format *f = container_of(obj, struct cw_format, obj);
	struct writefile_args *args = data;
	/* compiler claims this variable can be used before initialization... */
	FILE *bfile = NULL;
	char *fn, *orig_fn = NULL;
	char *buf = NULL;
	size_t size = 0;

	if (exts_compare(f->exts, args->type)) {
		if ((fn = build_filename(args->filename, args->type))) {
			int fd = open(fn, args->flags | args->myflags, args->mode);
			if (fd > -1) {
				/* fdopen() the resulting file stream */
				bfile = fdopen(fd, ((args->flags | args->myflags) & O_RDWR) ? "w+" : "w");
				if (!bfile) {
					cw_log(CW_LOG_WARNING, "Whoa, fdopen failed: %s!\n", strerror(errno));
					close(fd);
					fd = -1;
				}
			}
		
			if (option_cache_record_files && (fd > -1)) {
				char *c;

				fclose(bfile);
				/*
				  We touch orig_fn just as a place-holder so other things (like vmail) see the file is there.
				  What we are really doing is writing to record_cache_dir until we are done then we will mv the file into place.
				*/
				orig_fn = cw_strdupa(fn);
				for (c = fn; *c; c++)
					if (*c == '/')
						*c = '_';

				size = strlen(fn) + strlen(record_cache_dir) + 2;
				buf = alloca(size);
				strcpy(buf, record_cache_dir);
				strcat(buf, "/");
				strcat(buf, fn);
				free(fn);
				fn = buf;
				fd = open(fn, args->flags | args->myflags, args->mode);
				if (fd > -1) {
					/* fdopen() the resulting file stream */
					bfile = fdopen(fd, ((args->flags | args->myflags) & O_RDWR) ? "w+" : "w");
					if (!bfile) {
						cw_log(CW_LOG_WARNING, "Whoa, fdopen failed: %s!\n", strerror(errno));
						close(fd);
						fd = -1;
					}
				}
			}
			if (fd > -1) {
				if ((args->s->pvt = f->rewrite(bfile, args->comment))) {
					args->s->fmt = cw_object_dup(f);
					args->s->trans = NULL;
					args->s->flags = args->flags;
					args->s->mode = args->mode;
					if (orig_fn) {
						args->s->realfilename = strdup(orig_fn);
						args->s->filename = strdup(fn);
					} else {
						args->s->realfilename = NULL;
						args->s->filename = strdup(args->filename);
					}
					args->s->vfs = NULL;
				} else {
					cw_log(CW_LOG_WARNING, "Unable to rewrite %s\n", fn);
					close(fd);
					if (orig_fn) {
						unlink(fn);
						unlink(orig_fn);
					}
				}
			} else if (errno != EEXIST) {
				cw_log(CW_LOG_WARNING, "Unable to open file %s: %s\n", fn, strerror(errno));
				if (orig_fn)
					unlink(orig_fn);
			}
			/* if buf != NULL then fn is already free and pointing to it */
			if (!buf)
				free(fn);
		} else
			cw_log(CW_LOG_ERROR, "Out of memory\n");
		return 1;
	}

	return 0;
}

struct cw_filestream *cw_writefile(const char *filename, const char *type, const char *comment, int flags, int check, mode_t mode)
{
	struct writefile_args args = {
		.filename = filename,
		.type = type,
		.comment = comment,
		.flags = flags,
		.myflags = O_WRONLY | O_CREAT,
		.mode = mode,
	};

	if (!(args.s = calloc(1, sizeof(*args.s)))) {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		return NULL;
	}
	atomic_set(&args.s->running, 0);
	args.s->generator.tid = CW_PTHREADT_NULL;

	/* set the O_TRUNC flag if and only if there is no O_APPEND specified
	 * We really can't use O_APPEND as it will break WAV header updates
	 */
	if (flags & O_APPEND)
		args.flags &= ~O_APPEND;
	else
		args.myflags |= O_TRUNC;

	cw_registry_iterate(&format_registry, writefile_one, &args);

	if (args.s->fmt)
		return args.s;

	cw_log(CW_LOG_WARNING, "No such format '%s'\n", type);
	return NULL;
}


int cw_waitstream(struct cw_channel *c, const char *breakon)
{
	/* XXX Maybe I should just front-end cw_waitstream_full ? XXX */
	int res;
	struct cw_frame *fr;

	if (!breakon)
		breakon = "";

	while (atomic_read(&c->stream->running) || (c->stream->vfs && atomic_read(&c->stream->vfs->running))) {
		res = cw_waitfor(c, 10000);
		if (res < 0) {
			cw_log(CW_LOG_WARNING, "Select failed (%s)\n", strerror(errno));
			return res;
		} else if (res > 0) {
			fr = cw_read(c);
			if (!fr) {
#if 0
				cw_log(CW_LOG_DEBUG, "Got hung up\n");
#endif
				return -1;
			}
			
			switch(fr->frametype) {
			case CW_FRAME_DTMF:
				res = fr->subclass;
				if (strchr(breakon, res)) {
					cw_fr_free(fr);
					return res;
				}
				break;
			case CW_FRAME_CONTROL:
				switch(fr->subclass) {
				case CW_CONTROL_HANGUP:
                                case CW_CONTROL_BUSY:
                                case CW_CONTROL_CONGESTION:
					cw_fr_free(fr);
					return -1;
				case CW_CONTROL_RINGING:
				case CW_CONTROL_ANSWER:
				case CW_CONTROL_VIDUPDATE:
					/* Unimportant */
					break;
				default:
					cw_log(CW_LOG_WARNING, "Unexpected control subclass '%d'\n", fr->subclass);
				}
			}
			/* Ignore */
			cw_fr_free(fr);
		}
	}
	return (c->_softhangup ? -1 : 0);
}

int cw_waitstream_fr(struct cw_channel *c, const char *breakon, const char *forward, const char *rewind, int ms)
{
	int res;
	struct cw_frame *fr;

	if (!breakon)
		breakon = "";
	if (!forward)
		forward = "";
	if (!rewind)
		rewind = "";
	
	while (atomic_read(&c->stream->running) || (c->stream->vfs && atomic_read(&c->stream->vfs->running))) {
		res = cw_waitfor(c, 10000);
		if (res < 0) {
			cw_log(CW_LOG_WARNING, "Select failed (%s)\n", strerror(errno));
			return res;
		} else if (res > 0) {
			fr = cw_read(c);
			if (!fr) {
#if 0
				cw_log(CW_LOG_DEBUG, "Got hung up\n");
#endif
				return -1;
			}
			
			switch(fr->frametype) {
			case CW_FRAME_DTMF:
				res = fr->subclass;
				if (strchr(forward,res)) {
					cw_stream_fastforward(c->stream, ms);
				} else if (strchr(rewind,res)) {
					cw_stream_rewind(c->stream, ms);
				} else if (strchr(breakon, res)) {
					cw_fr_free(fr);
					return res;
				}					
				break;
			case CW_FRAME_CONTROL:
				switch(fr->subclass) {
			    	case CW_CONTROL_HANGUP:
                                case CW_CONTROL_BUSY:
                                case CW_CONTROL_CONGESTION:
					cw_fr_free(fr);
					return -1;
				case CW_CONTROL_RINGING:
				case CW_CONTROL_ANSWER:
					/* Unimportant */
					break;
				default:
					cw_log(CW_LOG_WARNING, "Unexpected control subclass '%d'\n", fr->subclass);
				}
			}
			/* Ignore */
			cw_fr_free(fr);
		}
	}
	return (c->_softhangup ? -1 : 0);
}

int cw_waitstream_full(struct cw_channel *c, const char *breakon, int audiofd, int cmdfd)
{
	int res;
	int ms;
	int outfd;
	struct cw_frame *fr;
	struct cw_channel *rchan;

	if (!breakon)
		breakon = "";
	
	while (atomic_read(&c->stream->running) || (c->stream->vfs && atomic_read(&c->stream->vfs->running))) {
		ms = 10000;
		rchan = cw_waitfor_nandfds(&c, 1, &cmdfd, (cmdfd > -1) ? 1 : 0, NULL, &outfd, &ms);
		if (!rchan && (outfd < 0) && (ms)) {
			/* Continue */
			if (errno == EINTR)
				continue;
			cw_log(CW_LOG_WARNING, "Wait failed (%s)\n", strerror(errno));
			return -1;
		} else if (outfd > -1) {
			/* The FD we were watching has something waiting */
			return 1;
		} else if (rchan) {
			fr = cw_read(c);
			if (!fr) {
#if 0
				cw_log(CW_LOG_DEBUG, "Got hung up\n");
#endif
				return -1;
			}
			
			switch(fr->frametype) {
			case CW_FRAME_DTMF:
				res = fr->subclass;
				if (strchr(breakon, res)) {
					cw_fr_free(fr);
					return res;
				}
				break;
			case CW_FRAME_CONTROL:
				switch(fr->subclass) {
				case CW_CONTROL_HANGUP:
                                case CW_CONTROL_BUSY:
                                case CW_CONTROL_CONGESTION:
					cw_fr_free(fr);
					return -1;
				case CW_CONTROL_RINGING:
				case CW_CONTROL_ANSWER:
					/* Unimportant */
					break;
				default:
					cw_log(CW_LOG_WARNING, "Unexpected control subclass '%d'\n", fr->subclass);
				}
			case CW_FRAME_VOICE:
				/* Write audio if appropriate */
				if (audiofd > -1)
					write(audiofd, fr->data, fr->datalen);
			}
			/* Ignore */
			cw_fr_free(fr);
		}
	}
	return (c->_softhangup ? -1 : 0);
}

int cw_waitstream_exten(struct cw_channel *c, const char *context)
{
	/* Waitstream, with return in the case of a valid 1 digit extension */
	/* in the current or specified context being pressed */
	/* XXX Maybe I should just front-end cw_waitstream_full ? XXX */
	int res;
	struct cw_frame *fr;
	char exten[CW_MAX_EXTENSION];

	if (!context)
		context = c->context;

	while (atomic_read(&c->stream->running) || (c->stream->vfs && atomic_read(&c->stream->vfs->running))) {
		res = cw_waitfor(c, 10000);
		if (res < 0) {
			cw_log(CW_LOG_WARNING, "Select failed (%s)\n", strerror(errno));
			return res;
		} else if (res > 0) {
			fr = cw_read(c);
			if (!fr) {
#if 0
				cw_log(CW_LOG_DEBUG, "Got hung up\n");
#endif
				return -1;
			}
			
			switch(fr->frametype) {
			case CW_FRAME_DTMF:
				res = fr->subclass;
				snprintf(exten, sizeof(exten), "%c", res);
				if (cw_exists_extension(c, context, exten, 1, c->cid.cid_num)) {
					cw_fr_free(fr);
					return res;
				}
				break;
			case CW_FRAME_CONTROL:
				switch(fr->subclass) {
				case CW_CONTROL_HANGUP:
                                case CW_CONTROL_BUSY:
                                case CW_CONTROL_CONGESTION:
					cw_fr_free(fr);
					return -1;
				case CW_CONTROL_RINGING:
				case CW_CONTROL_ANSWER:
					/* Unimportant */
					break;
				default:
					cw_log(CW_LOG_WARNING, "Unexpected control subclass '%d'\n", fr->subclass);
				}
			}
			/* Ignore */
			cw_fr_free(fr);
		}
	}
	return (c->_softhangup ? -1 : 0);
}


#define FORMAT "%-10s %-10s %-20s\n"
#define FORMAT2 "%-10s %-10s %-20s\n"

struct show_file_formats_args {
	int fd;
	int count;
};

static int show_file_formats_one(struct cw_object *obj, void *data)
{
	struct cw_format *f = container_of(obj, struct cw_format, obj);
	struct show_file_formats_args *args = data;

	cw_cli(args->fd, FORMAT2, cw_getformatname(f->format), f->name, f->exts);
	args->count++;
	return 0;
}

static int show_file_formats(int fd, int argc, char *argv[])
{
	struct show_file_formats_args args = { fd, 0 };

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	cw_cli(fd, FORMAT, "Format", "Name", "Extensions");
	cw_registry_iterate(&format_registry, show_file_formats_one, &args);
	cw_cli(fd, "%d file formats registered.\n", args.count);

	return RESULT_SUCCESS;
}

#undef FORMAT
#undef FORMAT2


struct cw_clicmd show_file = {
	.cmda = { "show", "file", "formats" },
	.handler = show_file_formats,
	.summary = "Displays file formats",
	.usage = "Usage: show file formats\n"
	"       displays currently registered file formats (if any)\n",
};

int cw_file_init(void)
{
	if (!filestream_generator.is_initialized)
		cw_object_init(&filestream_generator, &cw_object_isa_generator, CW_OBJECT_CURRENT_MODULE, 0);

	cw_cli_register(&show_file);
	return 0;
}
