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
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP		 { 0x4d555458, \
													   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
														 0x20 } }
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
#else
#define PTHREAD_MUTEX_INIT_VALUE	PTHREAD_MUTEX_INITIALIZER
#endif /* PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP */

#ifdef SOLARIS
#define CW_MUTEX_INIT_W_CONSTRUCTORS
#endif


extern CW_API_PUBLIC pthread_mutexattr_t  global_mutexattr_errorcheck;
extern CW_API_PUBLIC pthread_mutexattr_t  global_mutexattr_recursive;
extern CW_API_PUBLIC pthread_mutexattr_t  global_mutexattr_simple;


#ifdef DEBUG_MUTEX

#ifndef DEBUG_MUTEX_CANLOG
#  define DEBUG_MUTEX_CANLOG 1
#endif

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define CW_MUTEX_INIT_VALUE { \
	.mutex = PTHREAD_MUTEX_INIT_VALUE, \
	.reentrancy = 0, \
	.file = { NULL }, \
	.lineno = { 0 }, \
	.func = { NULL }, \
	.tinfo = NULL, \
}

#define CW_MAX_REENTRANCY 10

struct cw_pthread_info;

struct cw_mutex_info {
	pthread_mutex_t mutex;
	int reentrancy;
	const char *file[CW_MAX_REENTRANCY];
	int lineno[CW_MAX_REENTRANCY];
	const char *func[CW_MAX_REENTRANCY];
	struct cw_pthread_info *tinfo;
};

typedef struct cw_mutex_info cw_mutex_t;

#define cw_cond_t pthread_cond_t

extern CW_API_PUBLIC int cw_mutex_init_attr_debug(cw_mutex_t *t, pthread_mutexattr_t *attr, int canlog, const char *filename, int lineno, const char *func, const char *mutex_name);

extern CW_API_PUBLIC int cw_mutex_destroy_debug(cw_mutex_t *t, int canlog, const char *filename, int lineno, const char *func, const char *mutex_name);

#if defined(CW_MUTEX_INIT_W_CONSTRUCTORS)
/* if CW_MUTEX_INIT_W_CONSTRUCTORS is defined, use file scope
 constructors/destructors to create/destroy mutexes.  */
#define __CW_MUTEX_DEFINE(scope,mutex) \
	scope cw_mutex_t mutex = CW_MUTEX_INIT_VALUE; \
static void  __attribute__ ((constructor)) init_##mutex(void) \
{ \
	cw_mutex_init_attr_debug(&mutex, &global_mutexattr_recursive, 1, __FILE__, __LINE__, __PRETTY_FUNCTION__, #mutex); \
} \
static void  __attribute__ ((destructor)) fini_##mutex(void) \
{ \
	cw_mutex_destroy_debug(&mutex, 1, __FILE__, __LINE__, __PRETTY_FUNCTION__, #mutex); \
}
#elif defined(CW_MUTEX_INIT_ON_FIRST_USE)
/* if CW_MUTEX_INIT_ON_FIRST_USE is defined, mutexes are initialized
 on first use.  The performance impact on FreeBSD should be small since
 the pthreads library does this itself to initialize errror checking
 (defaulty type) mutexes.  If neither is defined, the pthreads librariy
 does the initialization itself on first use. */ 
#define __CW_MUTEX_DEFINE(scope,mutex) \
	scope cw_mutex_t mutex = CW_MUTEX_INIT_VALUE
#else /* CW_MUTEX_INIT_W_CONSTRUCTORS */
/* By default, use static initialization of mutexes.*/ 
#define __CW_MUTEX_DEFINE(scope,mutex) \
	scope cw_mutex_t mutex = CW_MUTEX_INIT_VALUE
#endif /* CW_MUTEX_INIT_W_CONSTRUCTORS */

extern CW_API_PUBLIC int cw_mutex_lock_debug(cw_mutex_t *t, int canlog, const char *filename, int lineno, const char *func, const char *mutex_name);

extern CW_API_PUBLIC int cw_mutex_trylock_debug(cw_mutex_t *t, int canlog, const char *filename, int lineno, const char *func, const char *mutex_name);

extern CW_API_PUBLIC int cw_mutex_unlock_debug(cw_mutex_t *t, int canlog, const char *filename, int lineno, const char *func, const char *mutex_name);


extern CW_API_PUBLIC int cw_cond_wait_debug(cw_cond_t *cond, cw_mutex_t *t, int canlog, const char *filename, int lineno, const char *func, const char *cond_name, const char *mutex_name);

extern CW_API_PUBLIC int cw_cond_timedwait_debug(cw_cond_t *cond, cw_mutex_t *t, const struct timespec *abstime, int canlog, const char *filename, int lineno, const char *func, const char *cond_name, const char *mutex_name);


#define cw_mutex_init_attr(pmutex, attr)     cw_mutex_init_attr_debug(pmutex, attr, DEBUG_MUTEX_CANLOG, __FILE__, __LINE__, __PRETTY_FUNCTION__, #pmutex)
#define cw_mutex_init(pmutex)                cw_mutex_init_attr_debug(pmutex, &global_mutexattr_recursive, DEBUG_MUTEX_CANLOG, __FILE__, __LINE__, __PRETTY_FUNCTION__, #pmutex)
#define cw_mutex_destroy(a)                  cw_mutex_destroy_debug(a, DEBUG_MUTEX_CANLOG, __FILE__, __LINE__, __PRETTY_FUNCTION__, #a)
#define cw_mutex_lock(a)                     cw_mutex_lock_debug(a, DEBUG_MUTEX_CANLOG, __FILE__, __LINE__, __PRETTY_FUNCTION__, #a)
#define cw_mutex_trylock(a)                  cw_mutex_trylock_debug(a, DEBUG_MUTEX_CANLOG, __FILE__, __LINE__, __PRETTY_FUNCTION__, #a)
#define cw_mutex_unlock(a)                   cw_mutex_unlock_debug(a, DEBUG_MUTEX_CANLOG, __FILE__, __LINE__, __PRETTY_FUNCTION__, #a)

#define cw_cond_init(cond, cond_attr)        pthread_cond_init((cond), (cond_attr))
#define cw_cond_signal(cond)                 pthread_cond_signal((cond))
#define cw_cond_broadcast(cond)              pthread_cond_broadcast((cond))
#define cw_cond_destroy(cond)                pthread_cond_destroy((cond))

#define cw_cond_wait(cond, mutex)            cw_cond_wait_debug(cond, mutex, DEBUG_MUTEX_CANLOG, __FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, #mutex)
#define cw_cond_timedwait(cond, mutex, time) cw_cond_timedwait_debug(cond, mutex, time, DEBUG_MUTEX_CANLOG, __FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, #mutex)

#else /* !DEBUG_MUTEX */


#define CW_MUTEX_INIT_VALUE	PTHREAD_MUTEX_INIT_VALUE


#define cw_mutex_t pthread_mutex_t


#define cw_mutex_init_attr(pmutex, attr) pthread_mutex_init(pmutex, attr)
#define cw_mutex_init(pmutex)            pthread_mutex_init(pmutex, &global_mutexattr_recursive)

#define cw_pthread_mutex_init(pmutex,a) pthread_mutex_init(pmutex,a)

#define cw_mutex_unlock(pmutex)  pthread_mutex_unlock((pmutex))
#define cw_mutex_destroy(pmutex) pthread_mutex_destroy((pmutex))

#if defined(CW_MUTEX_INIT_W_CONSTRUCTORS)
/* if CW_MUTEX_INIT_W_CONSTRUCTORS is defined, use file scope
 constructors/destructors to create/destroy mutexes.  */ 
#define __CW_MUTEX_DEFINE(scope,mutex) \
	scope cw_mutex_t mutex = CW_MUTEX_INIT_VALUE; \
static void  __attribute__ ((constructor)) init_##mutex(void) \
{ \
	cw_mutex_init(&mutex, &global_mutexattr_recursive); \
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
	if (*__m == (cw_mutex_t)CW_MUTEX_INIT_VALUE) \
		cw_mutex_init(__m); \
	pthread_mutex_lock(__m); \
})
#define cw_mutex_trylock(pmutex) ({ \
	const cw_mutex_t *__m = (pmutex); \
	if (*__m == (cw_mutex_t)CW_MUTEX_INIT_VALUE) \
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

#endif /* !DEBUG_MUTEX */


#define CW_MUTEX_DEFINE_STATIC(mutex) __CW_MUTEX_DEFINE(static,mutex)
#define CW_MUTEX_DEFINE_EXPORTED(mutex) __CW_MUTEX_DEFINE(/**/,mutex)

#define CW_MUTEX_INITIALIZER __use_CW_MUTEX_DEFINE_STATIC_rather_than_CW_MUTEX_INITIALIZER__

#define gethostbyname __gethostbyname__is__not__reentrant__use__cw_gethostbyname__instead__
#ifndef __linux__
#define pthread_create __use_cw_pthread_create_instead__
#endif


#endif /* _CALLWEAVER_LOCK_H */
