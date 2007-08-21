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


#ifdef __linux__

/* Atomic implementation using assembly optimizations from the kernel */

/* x86_64 requires CONFIG_SMP to enable the lock prefixing, i386 does
 * it right. Sigh...
 */
#define CONFIG_SMP

#include <asm/atomic.h>

#define atomic_destroy(x)	/* No-op */


#else /* __linux__ */

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

static inline void atomic_add(int i, atomic_t *v)
{
	pthread_spin_lock(&v->lock);
	v->counter += i;
	pthread_spin_unlock(&v->lock);
}

static inline void atomic_sub(int i, atomic_t *v)
{
	pthread_spin_lock(&v->lock);
	v->counter -= i;
	pthread_spin_unlock(&v->lock);
}

static inline int atomic_sub_and_test(int i, atomic_t *v)
{
	int ret;
	pthread_spin_lock(&v->lock);
	v->counter -= i;
	ret = (v->counter == 0);
	pthread_spin_unlock(&v->lock);
	return ret;
}

static inline void atomic_inc(atomic_t *v)
{
	pthread_spin_lock(&v->lock);
	v->counter++;
	pthread_spin_unlock(&v->lock);
}

static inline void atomic_dec(atomic_t *v)
{
	pthread_spin_lock(&v->lock);
	v->counter--;
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

static inline int atomic_inc_and_test(atomic_t *v)
{
	int ret;
	pthread_spin_lock(&v->lock);
	v->counter++;
	ret = (v->counter == 0);
	pthread_spin_unlock(&v->lock);
	return ret;
}

static inline int atomic_add_negative(int i, atomic_t *v)
{
	int ret;
	pthread_spin_lock(&v->lock);
	v->counter += i;
	ret = (v->counter < 0);
	pthread_spin_unlock(&v->lock);
	return ret;
}

static inline int atomic_add_return(int i, atomic_t *v)
{
	int ret;
	pthread_spin_lock(&v->lock);
	v->counter += i;
	ret = v->counter;
	pthread_spin_unlock(&v->lock);
	return ret;
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
	return atomic_add_return(-i, v);
}

static inline int atomic_add_unless(atomic_t *v, int a, int u)
{
	int ret;
	pthread_spin_lock(&v->lock);
	ret = (v->counter != u);
	if (ret)
		v->counter += a;
	pthread_spin_unlock(&v->lock);
	return ret;
}

#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

#define atomic_inc_return(v)  (atomic_add_return(1,v))
#define atomic_dec_return(v)  (atomic_sub_return(1,v))

#endif /* __linux__ */

#endif /* _CALLWEAVER_ATOMIC_H */
