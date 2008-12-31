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


/* It is required that atomic_read() does not require taking a
 * lock internal to the atomic_t. Specifically, it must be
 * safe to call atomic_read even if the atomic_t has not yet
 * been set (in which case a data/bss atomic_t will be 0).
 */


#if defined(__i386__) || defined(x86_64)

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

static __inline__ int atomic_inc_and_test(atomic_t *v)
{
	unsigned char c;

	__asm__ __volatile__(
		"lock ; incl %0; sete %1"
		:"=m" (v->counter), "=qm" (c)
		:"m" (v->counter) : "memory");
	return c != 0;
}

static __inline__ void atomic_dec(atomic_t *v)
{
	__asm__ __volatile__(
		"lock ; decl %0"
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


/*
 * Atomic compare and exchange.  Compare old with *ptr. If identical,
 * store new in *ptr.  Return the initial value of *ptr.  Success is
 * indicated by comparing returned value with old.
 * Access to *ptr is serializable via the given mutex however this is
 * unused here since we have atomic cmpxchg support in the hardware.
 */
static inline unsigned long __cmpxchg(void *mutex,
	volatile void *ptr, unsigned long old_n, unsigned long new_n, int size)
{
	unsigned long prev;
	switch (size) {
	case 1:
		__asm__ __volatile__("lock ; cmpxchgb %b1,%2"
				     : "=a"(prev)
				     : "q"(new_n), "m"(*(volatile long *)(ptr)), "0"(old_n)
				     : "memory");
		return prev;
	case 2:
		__asm__ __volatile__("lock ; cmpxchgw %w1,%2"
				     : "=a"(prev)
				     : "q"(new_n), "m"(*(volatile long *)(ptr)), "0"(old_n)
				     : "memory");
		return prev;
#if defined(x86_64)
	case 4:
		__asm__ __volatile__("lock ; cmpxchgl %k1,%2"
				     : "=a"(prev)
				     : "q"(new_n), "m"(*(volatile long *)(ptr)), "0"(old_n)
				     : "memory");
		return prev;
	case 8:
		__asm__ __volatile__("lock ; cmpxchgq %1,%2"
				     : "=a"(prev)
				     : "q"(new_n), "m"(*(volatile long *)(ptr)), "0"(old_n)
				     : "memory");
		return prev;
#else
	case 4:
		__asm__ __volatile__("lock ; cmpxchgl %1,%2"
				     : "=a"(prev)
				     : "q"(new_n), "m"(*(volatile long *)(ptr)), "0"(old_n)
				     : "memory");
		return prev;
#endif
	}
	return old_n;
}

#define cmpxchg(mutex, ptr, old_n, new_n) \
	((__typeof__(*(ptr)))__cmpxchg((mutex), (ptr), (unsigned long)(old_n), (unsigned long)(new_n), sizeof(*(ptr))))

static inline int atomic_cmpxchg(atomic_t *v, int old_n, int new_n)
{
	return cmpxchg(NULL, &v->counter, old_n, new_n);
}


#else /* no arch specific support */

#include <stdint.h>

/* Atomic implementation using pthread mutexes */

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

static inline int atomic_inc_and_test(atomic_t *v)
{
	int ret;
	pthread_spin_lock(&v->lock);
	v->counter++;
	ret = (v->counter == 0);
	pthread_spin_unlock(&v->lock);
	return ret;
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


/*
 * Atomic compare and exchange.  Compare old with *ptr. If identical,
 * store new in *ptr.  Return the initial value of *ptr.  Success is
 * indicated by comparing returned value with old.
 * Access to *ptr is serialized via the given mutex.
 */
static inline unsigned long __cmpxchg(pthread_spinlock_t *mutex,
	volatile void *ptr, unsigned long old_n, unsigned long new_n, int size)
{
	int prev;

	pthread_spin_lock(mutex);
	switch (size) {
	case 1:
		prev = *(uint8_t *)ptr;
		if (*(uint8_t *)ptr == old_n)
			*(uint8_t *)ptr = new_n;
		break;
	case 2:
		prev = *(uint16_t *)ptr;
		if (*(uint16_t *)ptr == old_n)
			*(uint16_t *)ptr = new_n;
		break;
	case 4:
		prev = *(uint32_t *)ptr;
		if (*(uint32_t *)ptr == old_n)
			*(uint32_t *)ptr = new_n;
		break;
#if defined(UINT64_MAX)
	case 8:
		prev = *(uint64_t *)ptr;
		if (*(uint64_t *)ptr == old_n)
			*(uint64_t *)ptr = new_n;
		break;
#endif
	default:
		prev = old_n;
		break;
	}
	pthread_spin_unlock(mutex);
	return prev;
}

#define cmpxchg(mutex, ptr, old_n, new_n) \
	((__typeof__(*(ptr)))__cmpxchg((mutex), (ptr), (unsigned long)(old_n), (unsigned long)(new_n), sizeof(*(ptr))))


static inline int atomic_cmpxchg(atomic_t *v, int old_n, int new_n)
{
	return cmpxchg(&v->lock, &v->counter, old_n, new_n);
}


#endif


static inline int atomic_fetch_and_add(atomic_t *v, int i)
{
	int prev;

	do {
		prev = atomic_read(v);
	} while (atomic_cmpxchg(v, prev, prev + i) != prev);

	return prev;
}


static inline int atomic_fetch_and_sub(atomic_t *v, int i)
{
	int prev;

	do {
		prev = atomic_read(v);
	} while (atomic_cmpxchg(v, prev, prev - i) != prev);

	return prev;
}


#endif /* _CALLWEAVER_ATOMIC_H */
