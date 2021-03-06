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
 * \brief Image Management
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/dynstr.h"
#include "callweaver/sched.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/file.h"
#include "callweaver/image.h"
#include "callweaver/translate.h"
#include "callweaver/cli.h"
#include "callweaver/lock.h"


static int cw_imager_qsort_compare_by_name(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct cw_imager *imager_a = container_of(*objp_a, struct cw_imager, obj);
	const struct cw_imager *imager_b = container_of(*objp_b, struct cw_imager, obj);

	return strcmp(imager_a->name, imager_b->name);
}

static int imager_object_match(struct cw_object *obj, const void *pattern)
{
	struct cw_imager *img = container_of(obj, struct cw_imager, obj);
	const int *format = pattern;
	return !(img->format & *format);
}

struct cw_registry imager_registry = {
	.name = "Imager",
	.qsort_compare = cw_imager_qsort_compare_by_name,
	.match = imager_object_match,
};


int cw_supports_images(struct cw_channel *chan)
{
	if (!chan || !chan->tech)
		return 0;
	return 1;
}

static int file_exists(char *filename)
{
	int res;
	struct stat st;
	res = stat(filename, &st);
	if (!res)
		return st.st_size;
	return 0;
}


struct read_image_args {
	char *filename;
	char *lang;
	int format;
	struct cw_frame *frame;
};

static int read_image_try(struct cw_object *obj, void *data)
{
	struct cw_dynstr fname = CW_DYNSTR_INIT;
	struct cw_imager *img = container_of(obj, struct cw_imager, obj);
	struct read_image_args *args = data;
	int res = 0;

	if (img->format & args->format) {
		char *tmp = strdupa(img->exts);
		char *stringp = tmp;
		char *e;
		size_t mark;

		if (args->filename[0] != '/')
			cw_dynstr_printf(&fname, "%s/images/", cw_config[CW_VAR_DIR]);

		if (args->lang && args->lang[0])
			cw_dynstr_printf(&fname, "%s-%s.", args->filename, args->lang);
		else
			cw_dynstr_printf(&fname, "%s.", args->filename);

		mark = fname.used;

		while ((e = strsep(&stringp, "|,"))) {
			size_t len;

			cw_dynstr_printf(&fname, "%s", e);

			if ((len = file_exists(fname.data))) {
				int fd = open(fname.data, O_RDONLY);
				if (fd > -1) {
					if (!img->identify || img->identify(fd)) {
						lseek(fd, 0, SEEK_SET);
						args->frame = img->read_image(fd, len); 
					} else
						cw_log(CW_LOG_WARNING, "%s does not appear to be a %s file\n", fname.data, img->name);
					close(fd);
				} else
					cw_log(CW_LOG_WARNING, "Unable to open '%s': %s\n", fname.data, strerror(errno));

				res = 1;
				break;
			}

			cw_dynstr_truncate(&fname, mark);
		}

		cw_dynstr_free(&fname);
	}

	return res;
}

int cw_send_image(struct cw_channel *chan, char *filename)
{
	struct read_image_args args = { filename, chan->language, -1, NULL };

	if (!cw_registry_iterate(&imager_registry, read_image_try, &args)) {
		args.lang = NULL;
		if (!cw_registry_iterate(&imager_registry, read_image_try, &args))
			cw_log(CW_LOG_WARNING, "Image file '%s' not found\n", filename);
	}

	return (args.frame ? cw_queue_frame(chan, args.frame) : -1);
}


#define FORMAT "%10s %10s %50s %10s\n"
#define FORMAT2 "%10s %10s %50s %10s\n"

static int imager_print(struct cw_object *obj, void *data)
{
	struct cw_imager *img = container_of(obj, struct cw_imager, obj);
	struct cw_dynstr *ds_p = data;

	cw_dynstr_printf(ds_p, FORMAT2, img->name, img->exts, img->desc, cw_getformatname(img->format));
	return 0;
}

static int show_image_formats(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(argv);

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	cw_dynstr_printf(ds_p, FORMAT, "Name", "Extensions", "Description", "Format");
	cw_registry_iterate_ordered(&imager_registry, imager_print, ds_p);
	return RESULT_SUCCESS;
}

struct cw_clicmd show_images = {
	.cmda = { "show", "image", "formats" },
	.handler = show_image_formats,
	.summary = "Displays image formats",
	.usage = "Usage: show image formats\n"
	"       displays currently registered image formats (if any)\n",
};


int cw_image_init(void)
{
	cw_cli_register(&show_images);
	return 0;
}
