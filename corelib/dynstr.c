/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Eris Associates Limited, UK
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


static int cw_dynstr_grow(struct cw_dynstr **ds_p, size_t len)
	__attribute__ ((nonnull (1)));


static int cw_dynstr_grow(struct cw_dynstr **ds_p, size_t len)
{
	struct cw_dynstr *nds;
	size_t nsize = sizeof(**ds_p) + len;

	if ((*ds_p) && (*ds_p)->chunk)
		nsize = (nsize | (*ds_p)->chunk) + 1;

	if ((nds = realloc(*ds_p, nsize))) {
		nds->size = nsize - sizeof(**ds_p);
		if (!(*ds_p)) {
			nds->chunk = CW_DYNSTR_DEFAULT_CHUNK;
			nds->used = nds->error = 0;
		}
		*ds_p = nds;
		return 0;
	}

	if ((*ds_p))
		(*ds_p)->error = 1;
	return 1;
}


int cw_dynstr_vprintf(struct cw_dynstr **ds_p, const char *fmt, va_list ap)
{
	while (!(*ds_p) || !(*ds_p)->error) {
		va_list aq;
		char *data;
		size_t size;
		size_t used;

		data = NULL;
		size = 0;
		if (*ds_p) {
			data = (*ds_p)->data + (*ds_p)->used;
			size = (*ds_p)->size - (*ds_p)->used;
		}

		va_copy(aq, ap);
		used = vsnprintf(data, size, fmt, aq);
		va_end(aq);

		/* FIXME: only ancient libcs have *printf functions that return -1 if the
		 * buffer isn't big enough. If we can even compile with such a beast at
		 * all we should have a compile time check for this.
		 */
		if (unlikely(used < 0))
			used = size + 255;

		if (*ds_p) {
			used += (*ds_p)->used;

			if (used < (*ds_p)->size) {
				(*ds_p)->used = used;
				break;
			}
		}

		cw_dynstr_grow(ds_p, used + 1);
		if (!(*ds_p))
			break;
	}

	return !(*ds_p) || (*ds_p)->error;
}

int cw_dynstr_printf(struct cw_dynstr **ds_p, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = cw_dynstr_vprintf(ds_p, fmt, ap);
	va_end(ap);

	return ret;
}
