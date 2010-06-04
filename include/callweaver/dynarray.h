/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Eris Associates Limited, UK
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

/*! \brief a dynamic array suite */

#ifndef _CALLWEAVER_DYNARRAY_H
#define _CALLWEAVER_DYNARRAY_H

#include <string.h>


#define CW_DYNARRAY_DEFAULT_CHUNK (64 - 1)


#define CW_DYNARRAY(type) \
	struct { \
		type *data;		/* The array itself */ \
		size_t used;		/* Unused by cw_dynarray functions. \
					 * May be used by the app for the number of elements used. \
					 */ \
		size_t size;		/* The number of elements allocated for the array */ \
		size_t chunk;		/* Requests for space are rounded to a multiple of this (in elements) \
					 * It MUST be a power of 2 and non-zero
					 */ \
		unsigned char error:1;	/* Set true if a malloc error has occurred */ \
	}

typedef CW_DYNARRAY(void) cw_dynarray_generic_t;


/* \brief Static initializer for a dynamic array. */
#define CW_DYNARRAY_INIT { \
	.data = NULL, \
	.used = 0, \
	.size = 0, \
	.chunk = CW_DYNARRAY_DEFAULT_CHUNK, \
	.error = 0, \
}


/*! \brief Initialize a new dynamic array.
 *
 *	\param da_p	dynamic array to initialize
 *	\param nmemb	initial number of elements
 *	\param chunk	allocations are rounded up to a multiple of this number of BYTES
 *			(this MUST be a power of 2)
 */
#define cw_dynarray_init(da_p, nmemb, chunk) ({ \
	typeof(da_p) __ptr_init = (da_p); \
	int __nmemb_init = (nmemb); \
	int __chunk = (chunk); \
	memset(__ptr_init, 0, sizeof(*__ptr_init)); \
	__ptr_init->chunk = (__chunk ? __chunk - 1 : CW_DYNARRAY_DEFAULT_CHUNK); \
	if (__nmemb_init) \
		cw_dynarray_need(__ptr_init, __nmemb_init); \
	__ptr_init->error; \
})


/* \brief Reset a dynamic array to contain nothing but do NOT release
 * the memory associated with it.
 *
 *	\param da_p	dynamic array to reset
 */
#define cw_dynarray_reset(da_p) ({ \
	typeof(da_p) __ptr = (da_p); \
	__ptr->used = __ptr->error = 0; \
})


/* \brief Make sure a dynamic array has at least the given number of elements allocated but unused.
 *
 *	\param da_p	dynamic array to check
 *	\param nmemb	minimum number of elements required to be allocated but unused
 */
#define cw_dynarray_need(da_p, nmemb) ({ \
	typeof(da_p) __ptr = (da_p); \
	size_t __nmemb = __ptr->used + (nmemb); \
	if (__nmemb > __ptr->size) { \
		typeof(__ptr->data) __ndata[2] = { __ptr->data, NULL }; \
		size_t __space = ((__nmemb * sizeof(__ptr->data[0])) + __ptr->chunk) & (~__ptr->chunk); \
		/* Note: the special handling for size 0 is because the dynstr implementation \
		 * does not allow the data pointer to be NULL. The additional check on the size \
		 * of the element being 1 allows the condition to be optimized out at compile time \
		 * for anything other than arrays of characters. \
		 */ \
		if ((__ndata[0] = realloc(__ndata[(__ptr->size == 0 && sizeof(__ptr->data[0]) == 1 ? 1 : 0)], __space))) { \
			__ptr->size = __space / sizeof(*__ptr->data); \
			__ptr->data = __ndata[0]; \
		} else \
			__ptr->error = 1; \
	} \
	__ptr->error; \
})


/* \brief Reset a dynamic array to contain nothing and release all memory
 * that has been allocated to it.
 *
 *	\param da_p	dynamic array to free
 */
#define cw_dynarray_free(da_p) ({ \
	typeof(da_p) __ptr = (da_p); \
	if (__ptr->size) { \
		free(__ptr->data); \
		__ptr->data = NULL; \
	} \
	__ptr->used = __ptr->size = __ptr->error = 0; \
})


#endif /* _CALLWEAVER_DYNARRAY_H */
