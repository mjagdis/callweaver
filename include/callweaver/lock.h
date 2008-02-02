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
 * \brief General CallWeaver channel locking definitions.
 */

#ifndef _CALLWEAVER_LOCK_H
#define _CALLWEAVER_LOCK_H

#include <pthread.h>
#include <netdb.h>
#include <time.h>
#include <sys/param.h>

#include "callweaver/logger.h"

#define CW_PTHREADT_NULL (pthread_t) -1
#define CW_PTHREADT_STOP (pthread_t) -2

#ifdef __APPLE__
/* Provide the Linux initializers for MacOS X */
#define PTHREAD_MUTEX_RECURSIVE_NP			PTHREAD_MUTEX_RECURSIVE
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP		 { 0x4d555458, \
													   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
														 0x20 } }
#endif

#ifdef __CYGWIN__
#define PTHREAD_MUTEX_RECURSIVE_NP					PTHREAD_MUTEX_RECURSIVE
#endif

#ifdef BSD
#ifdef __GNUC__
#define CW_MUTEX_INIT_W_CONSTRUCTORS
#else
#define CW_MUTEX_INIT_ON_FIRST_USE
#endif
#endif /* BSD */

/* From now on, CallWeaver REQUIRES Recursive (not error checking) mutexes
   and will not run without them. */
#ifdef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define PTHREAD_MUTEX_INIT_VALUE	PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define CW_MUTEX_KIND			PTHREAD_MUTEX_RECURSIVE_NP
#else
#define PTHREAD_MUTEX_INIT_VALUE	PTHREAD_MUTEX_INITIALIZER
#define CW_MUTEX_KIND			PTHREAD_MUTEX_RECURSIVE
#endif /* PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP */

#ifdef SOLARIS
#define CW_MUTEX_INIT_W_CONSTRUCTORS
#endif

#ifdef DEBUG_THREADS

#define __cw_mutex_logger(...) { if (canlog) cw_log(CW_LOG_ERROR, __VA_ARGS__); else fprintf(stderr, __VA_ARGS__); }

#ifdef THREAD_CRASH
#define DO_THREAD_CRASH do { *((int *)(0)) = 1; } while(0)
#endif

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define CW_MUTEX_INIT_VALUE { PTHREAD_MUTEX_INIT_VALUE, { NULL }, { 0 }, 0, { NULL }, { 0 } }

#define CW_MAX_REENTRANCY 10

struct cw_mutex_info {
	pthread_mutex_t mutex;
	const char *file[CW_MAX_REENTRANCY];
	int lineno[CW_MAX_REENTRANCY];
	int reentrancy;
	const char *func[CW_MAX_REENTRANCY];
	pthread_t thread[CW_MAX_REENTRANCY];
};

typedef struct cw_mutex_info cw_mutex_t;

#define cw_cond_t pthread_cond_t

static inline int __cw_pthread_mutex_init_attr(const char *filename, int lineno, const char *func,
						const char *mutex_name, cw_mutex_t *t,
						pthread_mutexattr_t *attr) 
{
#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS
	int canlog = strcmp(filename, "logger.c");

	if ((t->mutex) != ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__cw_mutex_logger("%s line %d (%s): Error: mutex '%s' is already initialized.\n",
				   filename, lineno, func, mutex_name);
		__cw_mutex_logger("%s line %d (%s): Error: previously initialization of mutex '%s'.\n",
				   t->file, t->lineno, t->func, mutex_name);
#ifdef THREAD_CRASH
		DO_THREAD_CRASH;
#endif
		return 0;
	}
#endif

	t->file[0] = filename;
	t->lineno[0] = lineno;
	t->func[0] = func;
	t->thread[0]  = 0;
	t->reentrancy = 0;

	return pthread_mutex_init(&t->mutex, attr);
}

static inline int __cw_pthread_mutex_init(const char *filename, int lineno, const char *func,
					   const char *mutex_name, cw_mutex_t *t)
{
	static pthread_mutexattr_t  attr;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, CW_MUTEX_KIND);

	return __cw_pthread_mutex_init_attr(filename, lineno, func, mutex_name, t, &attr);
}

static inline int __cw_pthread_mutex_destroy(const char *filename, int lineno, const char *func,
						const char *mutex_name, cw_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c");

#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__cw_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
	}
#endif

	res = pthread_mutex_trylock(&t->mutex);
	switch (res) {
	case 0:
		pthread_mutex_unlock(&t->mutex);
		break;
	case EINVAL:
		__cw_mutex_logger("%s line %d (%s): Error: attempt to destroy invalid mutex '%s'.\n",
				  filename, lineno, func, mutex_name);
		break;
	case EBUSY:
		__cw_mutex_logger("%s line %d (%s): Error: attempt to destroy locked mutex '%s'.\n",
				   filename, lineno, func, mutex_name);
		__cw_mutex_logger("%s line %d (%s): Error: '%s' was locked here.\n",
				   t->file[t->reentrancy-1], t->lineno[t->reentrancy-1], t->func[t->reentrancy-1], mutex_name);
		break;
	}

	if ((res = pthread_mutex_destroy(&t->mutex)))
		__cw_mutex_logger("%s line %d (%s): Error destroying mutex: %s\n",
				   filename, lineno, func, strerror(res));
#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
	else
		t->mutex = PTHREAD_MUTEX_INIT_VALUE;
#endif
	t->file[0] = filename;
	t->lineno[0] = lineno;
	t->func[0] = func;

	return res;
}

#if defined(CW_MUTEX_INIT_W_CONSTRUCTORS)
/* if CW_MUTEX_INIT_W_CONSTRUCTORS is defined, use file scope
 constrictors/destructors to create/destroy mutexes.  */
#define __CW_MUTEX_DEFINE(scope,mutex) \
	scope cw_mutex_t mutex = CW_MUTEX_INIT_VALUE; \
static void  __attribute__ ((constructor)) init_##mutex(void) \
{ \
	cw_mutex_init(&mutex); \
} \
static void  __attribute__ ((destructor)) fini_##mutex(void) \
{ \
	cw_mutex_destroy(&mutex); \
}
#elif defined(CW_MUTEX_INIT_ON_FIRST_USE)
/* if CW_MUTEX_INIT_ON_FIRST_USE is defined, mutexes are created on
 first use.  The performance impact on FreeBSD should be small since
 the pthreads library does this itself to initialize errror checking
 (defaulty type) mutexes.  If nither is defined, the pthreads librariy
 does the initialization itself on first use. */ 
#define __CW_MUTEX_DEFINE(scope,mutex) \
	scope cw_mutex_t mutex = CW_MUTEX_INIT_VALUE
#else /* CW_MUTEX_INIT_W_CONSTRUCTORS */
/* By default, use static initialization of mutexes.*/ 
#define __CW_MUTEX_DEFINE(scope,mutex) \
	scope cw_mutex_t mutex = CW_MUTEX_INIT_VALUE
#endif /* CW_MUTEX_INIT_W_CONSTRUCTORS */

static inline int __cw_pthread_mutex_lock(const char *filename, int lineno, const char *func,
                                           const char* mutex_name, cw_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c");

#if defined(CW_MUTEX_INIT_W_CONSTRUCTORS) || defined(CW_MUTEX_INIT_ON_FIRST_USE)
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS
		cw_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				 filename, lineno, func, mutex_name);
#endif
		cw_mutex_init(t);
	}
#endif /* defined(CW_MUTEX_INIT_W_CONSTRUCTORS) || defined(CW_MUTEX_INIT_ON_FIRST_USE) */

#ifdef DETECT_DEADLOCKS
	{
		time_t seconds = time(NULL);
		time_t current;
		do {
			res = pthread_mutex_trylock(&t->mutex);
			if (res == EBUSY) {
				current = time(NULL);
				if ((current - seconds) && (!((current - seconds) % 5))) {
					__cw_mutex_logger("%s line %d (%s): Deadlock? waited %d sec for mutex '%s'?\n",
							   filename, lineno, func, (int)(current - seconds), mutex_name);
					__cw_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
							   *t->file, *t->lineno, *t->func, mutex_name);
				}
				usleep(200);
			}
		} while (res == EBUSY);
	}
#else
	res = pthread_mutex_lock(&t->mutex);
#endif /* DETECT_DEADLOCKS */

	if (!res) {
		if (t->reentrancy < CW_MAX_REENTRANCY) {
			t->file[t->reentrancy] = filename;
			t->lineno[t->reentrancy] = lineno;
			t->func[t->reentrancy] = func;
			t->thread[t->reentrancy] = pthread_self();
			t->reentrancy++;
		} else {
			__cw_mutex_logger("%s line %d (%s): '%s' really deep reentrancy!\n",
							   filename, lineno, func, mutex_name);
		}
	} else {
		__cw_mutex_logger("%s line %d (%s): Error obtaining mutex: %s\n",
				   filename, lineno, func, strerror(res));
#ifdef THREAD_CRASH
		DO_THREAD_CRASH;
#endif
	}

	return res;
}

static inline int __cw_pthread_mutex_trylock(const char *filename, int lineno, const char *func,
                                              const char* mutex_name, cw_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c");

#if defined(CW_MUTEX_INIT_W_CONSTRUCTORS) || defined(CW_MUTEX_INIT_ON_FIRST_USE)
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS

		__cw_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
#endif
		cw_mutex_init(t);
	}
#endif /* defined(CW_MUTEX_INIT_W_CONSTRUCTORS) || defined(CW_MUTEX_INIT_ON_FIRST_USE) */

	if (!(res = pthread_mutex_trylock(&t->mutex))) {
		if (t->reentrancy < CW_MAX_REENTRANCY) {
			t->file[t->reentrancy] = filename;
			t->lineno[t->reentrancy] = lineno;
			t->func[t->reentrancy] = func;
			t->thread[t->reentrancy] = pthread_self();
			t->reentrancy++;
		} else {
			__cw_mutex_logger("%s line %d (%s): '%s' really deep reentrancy!\n",
							   filename, lineno, func, mutex_name);
		}
	}

	return res;
}

static inline int __cw_pthread_mutex_unlock(const char *filename, int lineno, const char *func,
					     const char *mutex_name, cw_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c");

#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__cw_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
	}
#endif

	if (t->reentrancy && !pthread_equal(t->thread[t->reentrancy-1], pthread_self())) {
		__cw_mutex_logger("%s line %d (%s): attempted unlock mutex '%s' without owning it!\n",
				   filename, lineno, func, mutex_name);
		__cw_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
				   t->file[t->reentrancy-1], t->lineno[t->reentrancy-1], t->func[t->reentrancy-1], mutex_name);
#ifdef THREAD_CRASH
		DO_THREAD_CRASH;
#endif
	}

	if (--t->reentrancy < 0) {
		__cw_mutex_logger("%s line %d (%s): mutex '%s' freed more times than we've locked!\n",
				   filename, lineno, func, mutex_name);
		t->reentrancy = 0;
	}

	if (t->reentrancy < CW_MAX_REENTRANCY) {
		t->file[t->reentrancy] = NULL;
		t->lineno[t->reentrancy] = 0;
		t->func[t->reentrancy] = NULL;
		t->thread[t->reentrancy] = 0;
	}

	if ((res = pthread_mutex_unlock(&t->mutex))) {
		__cw_mutex_logger("%s line %d (%s): Error releasing mutex: %s\n", 
				   filename, lineno, func, strerror(res));
#ifdef THREAD_CRASH
		DO_THREAD_CRASH;
#endif
	}

	return res;
}

static inline int __cw_cond_wait(const char *filename, int lineno, const char *func,
				  const char *cond_name, const char *mutex_name,
				  cw_cond_t *cond, cw_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c");

#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__cw_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
	}
#endif

	if (t->reentrancy && !pthread_equal(t->thread[t->reentrancy-1], pthread_self())) {
		__cw_mutex_logger("%s line %d (%s): attempted unlock mutex '%s' without owning it!\n",
				   filename, lineno, func, mutex_name);
		__cw_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
				   t->file[t->reentrancy-1], t->lineno[t->reentrancy-1], t->func[t->reentrancy-1], mutex_name);
#ifdef THREAD_CRASH
		DO_THREAD_CRASH;
#endif
	}

	if (--t->reentrancy < 0) {
		__cw_mutex_logger("%s line %d (%s): mutex '%s' freed more times than we've locked!\n",
				   filename, lineno, func, mutex_name);
		t->reentrancy = 0;
	}

	if (t->reentrancy < CW_MAX_REENTRANCY) {
		t->file[t->reentrancy] = NULL;
		t->lineno[t->reentrancy] = 0;
		t->func[t->reentrancy] = NULL;
		t->thread[t->reentrancy] = 0;
	}

	if ((res = pthread_cond_wait(cond, &t->mutex))) {
		__cw_mutex_logger("%s line %d (%s): Error waiting on condition mutex '%s'\n", 
				   filename, lineno, func, strerror(res));
#ifdef THREAD_CRASH
		DO_THREAD_CRASH;
#endif
	} else {
		if (t->reentrancy < CW_MAX_REENTRANCY) {
			t->file[t->reentrancy] = filename;
			t->lineno[t->reentrancy] = lineno;
			t->func[t->reentrancy] = func;
			t->thread[t->reentrancy] = pthread_self();
			t->reentrancy++;
		} else {
			__cw_mutex_logger("%s line %d (%s): '%s' really deep reentrancy!\n",
							   filename, lineno, func, mutex_name);
		}
	}

	return res;
}

static inline int __cw_cond_timedwait(const char *filename, int lineno, const char *func,
				       const char *cond_name, const char *mutex_name, cw_cond_t *cond,
				       cw_mutex_t *t, const struct timespec *abstime)
{
	int res;
	int canlog = strcmp(filename, "logger.c");

#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__cw_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
	}
#endif

	if (t->reentrancy && !pthread_equal(t->thread[t->reentrancy-1], pthread_self())) {
		__cw_mutex_logger("%s line %d (%s): attempted unlock mutex '%s' without owning it!\n",
				   filename, lineno, func, mutex_name);
		__cw_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
				   t->file[t->reentrancy-1], t->lineno[t->reentrancy-1], t->func[t->reentrancy-1], mutex_name);
#ifdef THREAD_CRASH
		DO_THREAD_CRASH;
#endif
	}

	if (--t->reentrancy < 0) {
		__cw_mutex_logger("%s line %d (%s): mutex '%s' freed more times than we've locked!\n",
				   filename, lineno, func, mutex_name);
		t->reentrancy = 0;
	}

	if (t->reentrancy < CW_MAX_REENTRANCY) {
		t->file[t->reentrancy] = NULL;
		t->lineno[t->reentrancy] = 0;
		t->func[t->reentrancy] = NULL;
		t->thread[t->reentrancy] = 0;
	}

	res = pthread_cond_timedwait(cond, &t->mutex, abstime);
	if (res && res != ETIMEDOUT) {
		__cw_mutex_logger("%s line %d (%s): Error waiting on condition mutex '%s'\n", 
				   filename, lineno, func, strerror(res));
#ifdef THREAD_CRASH
		DO_THREAD_CRASH;
#endif
	} else {
		if (t->reentrancy < CW_MAX_REENTRANCY) {
			t->file[t->reentrancy] = filename;
			t->lineno[t->reentrancy] = lineno;
			t->func[t->reentrancy] = func;
			t->thread[t->reentrancy] = pthread_self();
			t->reentrancy++;
		} else {
			__cw_mutex_logger("%s line %d (%s): '%s' really deep reentrancy!\n",
							   filename, lineno, func, mutex_name);
		}
	}

	return res;
}


#define cw_mutex_init(pmutex)                __cw_pthread_mutex_init(__FILE__, __LINE__, __PRETTY_FUNCTION__, #pmutex, pmutex)
#define cw_mutex_destroy(a)                  __cw_pthread_mutex_destroy(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)
#define cw_mutex_lock(a)                     __cw_pthread_mutex_lock(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)
#define cw_mutex_unlock(a)                   __cw_pthread_mutex_unlock(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)
#define cw_mutex_trylock(a)                  __cw_pthread_mutex_trylock(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)

#define cw_cond_init(cond, cond_attr)        pthread_cond_init((cond), (cond_attr))
#define cw_cond_signal(cond)                 pthread_cond_signal((cond))
#define cw_cond_broadcast(cond)              pthread_cond_broadcast((cond))
#define cw_cond_destroy(cond)                pthread_cond_destroy((cond))

#define cw_cond_wait(cond, mutex)            __cw_cond_wait(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, #mutex, cond, mutex)
#define cw_cond_timedwait(cond, mutex, time) __cw_cond_timedwait(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, #mutex, cond, mutex, time)

#else /* !DEBUG_THREADS */


#define CW_MUTEX_INIT_VALUE	PTHREAD_MUTEX_INIT_VALUE


#define cw_mutex_t pthread_mutex_t

static inline int cw_mutex_init(cw_mutex_t *pmutex)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, CW_MUTEX_KIND);
	return pthread_mutex_init(pmutex, &attr);
}

#define cw_pthread_mutex_init(pmutex,a) pthread_mutex_init(pmutex,a)

#define cw_mutex_unlock(pmutex)  pthread_mutex_unlock((pmutex))
#define cw_mutex_destroy(pmutex) pthread_mutex_destroy((pmutex))

#if defined(CW_MUTEX_INIT_W_CONSTRUCTORS)
/* if CW_MUTEX_INIT_W_CONSTRUCTORS is defined, use file scope
 constrictors/destructors to create/destroy mutexes.  */ 
#define __CW_MUTEX_DEFINE(scope,mutex) \
	scope cw_mutex_t mutex = CW_MUTEX_INIT_VALUE; \
static void  __attribute__ ((constructor)) init_##mutex(void) \
{ \
	cw_mutex_init(&mutex); \
} \
static void  __attribute__ ((destructor)) fini_##mutex(void) \
{ \
	cw_mutex_destroy(&mutex); \
}

#define cw_mutex_lock(pmutex)    pthread_mutex_lock((pmutex))
#define cw_mutex_trylock(pmutex) pthread_mutex_trylock((pmutex))

#elif defined(CW_MUTEX_INIT_ON_FIRST_USE)
/* if CW_MUTEX_INIT_ON_FIRST_USE is defined, mutexes are created on
 first use.  The performance impact on FreeBSD should be small since
 the pthreads library does this itself to initialize errror checking
 (defaulty type) mutexes.*/ 
#define __CW_MUTEX_DEFINE(scope,mutex) \
	scope cw_mutex_t mutex = CW_MUTEX_INIT_VALUE

#define cw_mutex_lock(pmutex) ({ \
	const cw_mutex_t *__m = (pmutex); \
	if (*__m == (cw_mutex_t)CW_MUTEX_KIND) \
		cw_mutex_init(__m); \
	pthread_mutex_lock(__m); \
})
#define cw_mutex_trylock(pmutex) ({ \
	const cw_mutex_t *__m = (pmutex); \
	if (*__m == (cw_mutex_t)CW_MUTEX_KIND) \
		cw_mutex_init(__m); \
	pthread_mutex_trylock(__m); \
})
#else
/* By default, use static initialization of mutexes.*/ 
#define __CW_MUTEX_DEFINE(scope,mutex) \
	scope cw_mutex_t mutex = CW_MUTEX_INIT_VALUE

#define cw_mutex_lock(pmutex)    pthread_mutex_lock((pmutex))
#define cw_mutex_trylock(pmutex) pthread_mutex_trylock((pmutex))

#endif /* CW_MUTEX_INIT_W_CONSTRUCTORS */

#define cw_cond_t pthread_cond_t

#define cw_cond_init(cond, cond_attr)       pthread_cond_init((cond), (cond_attr))
#define cw_cond_signal(cond)                pthread_cond_signal((cond))
#define cw_cond_broadcast(cond)             pthread_cond_broadcast((cond))
#define cw_cond_destroy(cond)               pthread_cond_destroy((cond))
#define cw_cond_wait(cond, t)               pthread_cond_wait((cond), (t))
#define cw_cond_timedwait(cond, t, abstime) pthread_cond_timedwait((cond), (t), (abstime))

#endif /* !DEBUG_THREADS */


/* mutex unlock is sometimes used in a pthread_cleanup_push */
static inline int cw_mutex_unlock_func(cw_mutex_t *mutex)
{
	return cw_mutex_unlock(mutex);
}


#define CW_MUTEX_DEFINE_STATIC(mutex) __CW_MUTEX_DEFINE(static,mutex)
#define CW_MUTEX_DEFINE_EXPORTED(mutex) __CW_MUTEX_DEFINE(/**/,mutex)

#define CW_MUTEX_INITIALIZER __use_CW_MUTEX_DEFINE_STATIC_rather_than_CW_MUTEX_INITIALIZER__

#define gethostbyname __gethostbyname__is__not__reentrant__use__cw_gethostbyname__instead__
#ifndef __linux__
#define pthread_create __use_cw_pthread_create_instead__
#endif

#endif /* _CALLWEAVER_LOCK_H */
