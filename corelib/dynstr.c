/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009 - 2010, Eris Associates Limited, UK
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/dynstr.h"
#include "callweaver/utils.h"


int cw_dynstr_vprintf(struct cw_dynstr *ds_p, const char *fmt, va_list ap)
{
	while (!ds_p->error) {
		va_list aq;
		char *data;
		size_t size;
		size_t used;

		data = ds_p->data + ds_p->used;
		size = ds_p->size - ds_p->used;

		va_copy(aq, ap);
		used = vsnprintf(data, size, fmt, aq);
		va_end(aq);

		/* FIXME: only ancient libcs have *printf functions that return -1 if the
		 * buffer isn't big enough. If we can even compile with such a beast at
		 * all we should have a compile time check for this.
		 */
		if (unlikely((int)used == -1))
			used = size + 255;

		/* Did it fit? If so we're done. */
		if (ds_p->used + used < ds_p->size) {
			ds_p->used += used;
			break;
		}

		/* Ok, we know what we need now so allocate it and go round again */
		cw_dynstr_need(ds_p, used + 1);
	}

	return ds_p->error;
}

int cw_dynstr_printf(struct cw_dynstr *ds_p, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = cw_dynstr_vprintf(ds_p, fmt, ap);
	va_end(ap);

	return ret;
}
