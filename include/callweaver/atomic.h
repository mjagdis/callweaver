/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Eris Associates Limited, UK
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

/*! \file
 * \brief General CallWeaver atomic operations
 */

#ifndef _CALLWEAVER_ATOMIC_H
#define _CALLWEAVER_ATOMIC_H

#include <pthread.h>

#include "callweaver/lock.h"


#if defined(__i386__)

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
typedef struct { volatile int counter; } atomic_t;

#define atomic_set(v,i)		(((v)->counter) = (i))
#define atomic_destroy(x)	/* No-op */
#define atomic_read(v)		((v)->counter)

static __inline__ void atomic_inc(atomic_t *v)
{
	__asm__ __volatile__(
		"lock ; incl %0"
		:"=m" (v->counter)
		:"m" (v->counter));
}

static __inline__ int atomic_dec_and_test(atomic_t *v)
{
	unsigned char c;

	__asm__ __volatile__(
		"lock ; decl %0; sete %1"
		:"=m" (v->counter), "=qm" (c)
		:"m" (v->counter) : "memory");
	return c != 0;
}


#elif defined(__x86_64__)

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
typedef struct { volatile int counter; } atomic_t;

#define atomic_set(v,i)		(((v)->counter) = (i))
#define atomic_destroy(x)	/* No-op */
#define atomic_read(v)		((v)->counter)

static __inline__ void atomic_inc(atomic_t *v)
{
	__asm__ __volatile__(
		"lock ; incl %0"
		:"=m" (v->counter)
		:"m" (v->counter));
}

static __inline__ int atomic_dec_and_test(atomic_t *v)
{
	unsigned char c;

	__asm__ __volatile__(
		"lock ; decl %0; sete %1"
		:"=m" (v->counter), "=qm" (c)
		:"m" (v->counter) : "memory");
	return c != 0;
}


#else /* no arch specific support */

/* Atomic implementation using pthread mutexes */

/* The IEEE Std. 1003.1j-2000 introduces functions to implement spinlocks.
 * If we have an earlier 1003.1j we have to use mutexes.
 * If we use mutexes the current callweaver/lock.h forces us to use
 * opbx_mutexes. If DEBUG_THREADS is on opbx_mutexes are unnecessarily
 * heavyweight for what we want here :-(
 * To enable __USE_XOPEN2K (if available) in a GNU libc environment
 * you need to compile with either _POSIX_C_SOURCE >= 200112L,
 * _XOPEN_SOURCE >= 600 or _GNU_SOURCE. With autoconf generated
 * configs you will normally have _GNU_SOURCE defined if GNU
 * libc is in use.
 */
#ifndef __USE_XOPEN2K
#  define PTHREAD_PROCESS_SHARED	0
#  define PTHREAD_PROCESS_PRIVATE	0
#  define pthread_spinlock_t		opbx_mutex_t
#  define pthread_spin_init(l, a)	opbx_mutex_init(l)
#  define pthread_spin_destroy(l)	opbx_mutex_destroy(l)
#  define pthread_spin_lock(l)		opbx_mutex_lock(l)
#  define pthread_spin_unlock(l)	opbx_mutex_unlock(l)
#endif


typedef struct {
	pthread_spinlock_t lock;
	int counter;
} atomic_t;


static inline void atomic_set(atomic_t *v, int i)
{
	pthread_spin_init(&v->lock, PTHREAD_PROCESS_SHARED);
	v->counter = i;
}

static inline void atomic_destroy(atomic_t *v)
{
	pthread_spin_destroy(&v->lock);
}

#define atomic_read(v)		((v)->counter)

static inline void atomic_inc(atomic_t *v)
{
	pthread_spin_lock(&v->lock);
	v->counter++;
	pthread_spin_unlock(&v->lock);
}

static inline int atomic_dec_and_test(atomic_t *v)
{
	int ret;
	pthread_spin_lock(&v->lock);
	v->counter--;
	ret = (v->counter == 0);
	pthread_spin_unlock(&v->lock);
	return ret;
}


#endif /* __linux__ */

#endif /* _CALLWEAVER_ATOMIC_H */
