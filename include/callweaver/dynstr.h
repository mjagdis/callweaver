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

/*! \brief a dynamic string suite */

#ifndef _CALLWEAVER_DYNSTR_H
#define _CALLWEAVER_DYNSTR_H

#include <stdarg.h>
#include <stdlib.h>

#include "callweaver/preprocessor.h"


#define CW_DYNSTR_DEFAULT_CHUNK 64


struct cw_dynstr {
	size_t used, size, chunk;
	unsigned char error:1;
	char *data;
};

/* \brief Static initializer for a dynamic string. */
#define CW_DYNSTR_INIT	{ \
	.used = 0, \
	.size = 0, \
	.chunk = CW_DYNSTR_DEFAULT_CHUNK, \
	.error = 0, \
	.data = NULL, \
}


/*! \brief Initialize a new dynamic string.
 *
 *	\param ds_p	dynamic string to initialize
 *	\param len	initial length
 *	\param chunk	allocations are rounded up to a multiple of this
 *			(this MUST be a power of 2 and non-zero)
 */
static inline void cw_dynstr_init(struct cw_dynstr *ds_p, size_t len, size_t chunk)
{
	ds_p->used = 0;
	ds_p->size = 0;
	ds_p->chunk = chunk - 1;
	ds_p->error = 0;
	ds_p->data = NULL;

	/* N.B. We don't set the error flag if this malloc fails because any
	 * initial length is just a hint how much might be needed. If this
	 * malloc doesn't happen we might still manage to malloc later when
	 * the space is actually needed.
	 */
	if (len && (ds_p->data = malloc(len)))
		ds_p->size = len;
}


/* \brief Reset a dynamic string to contain nothing but do NOT release
 * the memory associated with it.
 *
 *	\param ds_p	dynamic string to reset
 */
static inline void cw_dynstr_reset(struct cw_dynstr *ds_p)
	__attribute__ ((nonnull (1)));

static inline void cw_dynstr_reset(struct cw_dynstr *ds_p)
{
	ds_p->used = ds_p->error = 0;
}


/* \brief Grow the space allocated for a dynamic string to be at least the given size.
 *
 *	\param ds_p	dynamic string to reset
 *	\param len	minimum allocation required
 */
extern CW_API_PUBLIC void cw_dynstr_grow(struct cw_dynstr *ds_p, size_t len)
	__attribute__ ((nonnull (1)));


/* \brief Make sure a dynamic string has at least the given amount of free space
 * already allocated.
 *
 *	\param ds_p	dynamic string to reset
 *	\param len	minimum free space required
 */
static inline void cw_dynstr_need(struct cw_dynstr *ds_p, size_t len)
	__attribute__ ((nonnull (1)));

static inline void cw_dynstr_need(struct cw_dynstr *ds_p, size_t len)
{
	len += ds_p->used;
	if (len > ds_p->size)
		cw_dynstr_grow(ds_p, len);
}


/* \brief Reset a dynamic string to contain nothing and release all memory
 * that has been allocated to it.
 *
 *	\param ds_p	dynamic string to free
 */
static inline void cw_dynstr_free(struct cw_dynstr *ds_p)
	__attribute__ ((nonnull (1)));

static inline void cw_dynstr_free(struct cw_dynstr *ds_p)
{
	if (ds_p->data) {
		free(ds_p->data);
		ds_p->used = ds_p->size = ds_p->error = 0;
		ds_p->data = NULL;
	}
}


extern CW_API_PUBLIC int cw_dynstr_vprintf(struct cw_dynstr *ds_p, const char *fmt, va_list ap)
	__attribute__ ((__nonnull__ (1,2)));
extern CW_API_PUBLIC int cw_dynstr_printf(struct cw_dynstr *ds_p, const char *fmt, ...)
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
static __inline__ int cw_dynstr_tprintf(struct cw_dynstr *ds_p, size_t count, ...)
	__attribute__ ((always_inline, const, unused, no_instrument_function, nonnull (1)));
static __inline__ int cw_dynstr_tprintf(struct cw_dynstr *ds_p __attribute__((unused)), size_t count __attribute__((unused)), ...)
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

extern int cw_dynstr_tprintf(struct cw_dynstr *ds_p, size_t count, ...)
	__attribute__ ((__nonnull__ (1)));

extern char *cw_fmtval(const char *fmt, ...)
	__attribute__ ((__nonnull__ (1), __format__ (printf, 1,2)));

#endif

#endif /* _CALLWEAVER_DYNSTR_H */
