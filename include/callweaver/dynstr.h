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

/*! \brief a dynamic string suite */

#ifndef _CALLWEAVER_DYNSTR_H
#define _CALLWEAVER_DYNSTR_H

#include <stdarg.h>
#include <stdlib.h>

#include "callweaver/preprocessor.h"


#define CW_DYNSTR_DEFAULT_CHUNK 64


struct cw_dynstr {
	size_t size, chunk, used;
	unsigned char error:1;
	char data[0];
};


/*! \brief Allocate a new dynamic string
 *
 *	\param len	initial length
 *	\param chunk	allocations are rounded up to a multiple of this if non zero
 *			(this MUST be a power of 2)
 */
static inline struct cw_dynstr *cw_dynstr_alloc(size_t len, size_t chunk)
{
	struct cw_dynstr *ds;

	if ((ds = malloc(sizeof(*ds) + len))) {
		ds->size = len;
		ds->chunk = chunk - 1;
		ds->used = 0;
		ds->error = 0;
	}

	return ds;
}


static inline void cw_dynstr_reset(struct cw_dynstr **ds_p)
	__attribute__ ((nonnull (1)));

static inline void cw_dynstr_reset(struct cw_dynstr **ds_p)
{
	if (*ds_p)
		(*ds_p)->used = (*ds_p)->error = 0;
}


extern CW_API_PUBLIC int cw_dynstr_grow(struct cw_dynstr **ds_p, size_t len)
	__attribute__ ((nonnull (1)));


static inline int cw_dynstr_need(struct cw_dynstr **ds_p, size_t len)
	__attribute__ ((nonnull (1)));

static inline int cw_dynstr_need(struct cw_dynstr **ds_p, size_t len)
{
	if (*ds_p)
		len += (*ds_p)->used;
	if (!(*ds_p) || len > (*ds_p)->size)
		cw_dynstr_grow(ds_p, len);
	return !(*ds_p) || (*ds_p)->error;
}


static inline void cw_dynstr_free(struct cw_dynstr **ds_p)
	__attribute__ ((nonnull (1)));

static inline void cw_dynstr_free(struct cw_dynstr **ds_p)
{
	if (*ds_p) {
		free(*ds_p);
		*ds_p = NULL;
	}
}


extern CW_API_PUBLIC int cw_dynstr_vprintf(struct cw_dynstr **ds_p, const char *fmt, va_list ap)
	__attribute__ ((__nonnull__ (1,2)));
extern CW_API_PUBLIC int cw_dynstr_printf(struct cw_dynstr **ds_p, const char *fmt, ...)
	__attribute__ ((__nonnull__ (1,2), __format__ (printf, 2,3)));


/* If you are looking at this trying to fix a weird compile error
 * check the count is a constant integer corresponding to the
 * number of cw_fmtval() arguments, that there are _only_
 * cw_fmtval() arguments after the count (there can be no
 * expressions wrapping cw_fmtval to select one or the other
 * for instance) and that you are not missing a comma after a
 * cw_fmtval().
 * In particular note:
 *
 *    this is legal
 *        if (...)
 *            cw_dynstr_tprintf(...,
 *                cw_fmtval(...),
 *                ...
 *            );
 *        else
 *            cw_dynstr_tprintf(...,
 *                cw_fmtval(...),
 *                ...
 *            );
 *
 *    but this is not
 *        cw_dynstr_tprintf(...,
 *            (... ? cw_fmtval(...) : cw_fmtval(...)),
 *            ...
 *        );
 *
 *    although this is
 *        cw_dynstr_tprintf(...,
 *            cw_fmtval(..., (... ? a : b)),
 *            ...
 *        );
 */

#ifndef CW_DEBUG_COMPILE

/* These are deliberately empty. They only exist to allow compile time
 * syntax checking of _almost_ the actual code rather than the preprocessor
 * expansion. They will be optimized out.
 * Note that we only get _almost_ there. Specifically there is no way to
 * stop the preprocessor eating line breaks so you way get told arg 3
 * doesn't match the format string, but not which cw_fmtval in the
 * list it is talking about. If you can't spot it try compiling
 * with CW_DEBUG_COMPILE defined. This breaks expansion completely
 * so you get accurate line numbers for errors and warnings but then
 * the compiled code will have references to non-existent functions.
 */
static __inline__ int cw_dynstr_tprintf(struct cw_dynstr **ds_p, size_t count, ...)
	__attribute__ ((always_inline, const, unused, no_instrument_function, nonnull (1)));
static __inline__ int cw_dynstr_tprintf(struct cw_dynstr **ds_p __attribute__((unused)), size_t count __attribute__((unused)), ...)
{
	return 0;
}
static __inline__ char *cw_fmtval(const char *fmt, ...)
	__attribute__ ((always_inline, const, unused, no_instrument_function, nonnull (1), format (printf, 1,2)));
static __inline__ char *cw_fmtval(const char *fmt __attribute__((unused)), ...)
{
	return NULL;
}

#  define CW_TPRINTF_DEBRACKET_cw_fmtval(fmt, ...)	fmt, ## __VA_ARGS__
#  define CW_TPRINTF_DO(op, ...)			op(__VA_ARGS__)
#  define CW_TPRINTF_FMT(n, a)				CW_TPRINTF_DO(CW_TPRINTF_FMT_I, n, CW_CPP_CAT(CW_TPRINTF_DEBRACKET_, a))
#  define CW_TPRINTF_FMT_I(n, fmt, ...)			fmt
#  define CW_TPRINTF_ARGS(n, a)				CW_TPRINTF_DO(CW_TPRINTF_ARGS_I, n, CW_CPP_CAT(CW_TPRINTF_DEBRACKET_, a))
#  define CW_TPRINTF_ARGS_I(n, fmt, ...)		, ## __VA_ARGS__

#  define cw_dynstr_tprintf(ds_p, count, ...) ({ \
	(void)cw_dynstr_tprintf(ds_p, count, \
		__VA_ARGS__ \
	); \
	cw_dynstr_printf(ds_p, \
		CW_CPP_CAT(CW_CPP_ITERATE_, count)(0, CW_TPRINTF_FMT, __VA_ARGS__) \
		CW_CPP_CAT(CW_CPP_ITERATE_, count)(0, CW_TPRINTF_ARGS, __VA_ARGS__) \
	); \
   })

#else

extern int cw_dynstr_tprintf(struct cw_dynstr **ds_p, size_t count, ...)
	__attribute__ ((__nonnull__ (1)));

extern char *cw_fmtval(const char *fmt, ...)
	__attribute__ ((__nonnull__ (1), __format__ (printf, 1,2)));

#endif

#endif /* _CALLWEAVER_DYNSTR_H */
