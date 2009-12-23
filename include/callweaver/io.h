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

/*! \file
 * \brief I/O Event Management
 */

#ifndef _CALLWEAVER_IO_H
#define _CALLWEAVER_IO_H


#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif


#ifdef HAVE_EPOLL

#include <sys/epoll.h>


#define CW_IO_IN 	EPOLLIN		/*! Input ready */
#define CW_IO_OUT 	EPOLLOUT	/*! Output ready */
#define CW_IO_PRI	EPOLLPRI	/*! Priority input ready */

/* Implicitly polled for */
#define CW_IO_ERR	EPOLLERR	/*! Error condition (errno or getsockopt) */
#define CW_IO_HUP	EPOLLHUP	/*! Hangup */


typedef int cw_io_context_t;

#define CW_IO_CONTEXT_NONE	(-1)

struct cw_io_rec;


/*
 * A CallWeaver IO callback takes its io_rec, a file descriptor, list of events, and
 * callback data as arguments and returns 0 if it should not be
 * run again, or non-zero if it should be run again.
 */
typedef int (*cw_io_cb)(struct cw_io_rec *ior, int fd, short events, void *cbdata);


struct cw_io_rec {
	cw_io_cb callback;		/* What is to be called */
	void *data; 			/* Data to be passed */
	int fd;
};


static inline void cw_io_init(struct cw_io_rec *ior, cw_io_cb callback, void *data)
{
	ior->callback = callback;
	ior->data = data;
	ior->fd = -1;
}


#define cw_io_isactive(ior)	((ior)->fd != -1)


/*! Creates a context
 *
 * Create a context for I/O operations
 *
 * \param slots	number of slots (file descriptors) to allocate initially (the number grows as necessary)
 *
 * \return the created I/O context
 */
#define cw_io_context_create(slots)	epoll_create(slots)


/*! Destroys a context
 *
 * \param ioc	context to destroy
 *
 * Destroy an I/O context and release any associated memory
 */
#define cw_io_context_destroy(ioc)	close(ioc)


/*! Adds an IO context
 *
 * \param ioc		context to use
 * \param fd		fd to monitor
 * \param events	events to wait for
 *
 * \return 0 on success or -1 on failure
 */
#define cw_io_add(ioc, ior, filedesc, mask) ({ \
	const typeof(ior) __ior = (ior); \
	const typeof(filedesc) __filedesc = (filedesc); \
	struct epoll_event ev = { \
		.events = (mask), \
		.data = { .ptr = __ior }, \
	}; \
	int ret; \
 \
	if (!(ret = epoll_ctl((ioc), EPOLL_CTL_ADD, __filedesc, &ev))) \
		__ior->fd = __filedesc; \
	ret; \
})


/*! Removes an IO context
 *
 * \param ioc	io_context to remove it from
 * \param ior	io_rec to remove
 *
 * \return 0 on success or -1 on failure.
 */
#define cw_io_remove(ioc, ior) ({ \
	struct epoll_event ev; \
	const typeof(ior) __ior = (ior); \
	int ret; \
 \
	/* N.B. the event struct is ignored but prior to Linux 2.6.9 NULL was not allowed */ \
	if (!(ret = epoll_ctl((ioc), EPOLL_CTL_DEL, __ior->fd, &ev))) \
		__ior->fd = -1; \
	ret; \
})


/*! Waits for IO
 *
 * \param ioc		context to act upon
 * \param howlong	how many milliseconds to wait
 *
 * Wait for I/O to happen, returning after
 * howlong milliseconds, and after processing
 * any necessary I/O.  Returns the number of
 * I/O events which took place.
 */
extern CW_API_PUBLIC int cw_io_run(cw_io_context_t ioc, int howlong);


#else /* HAVE_EPOLL */


#include <sys/poll.h>
#include <limits.h>


#define CW_IO_IN 	POLLIN		/*! Input ready */
#define CW_IO_OUT 	POLLOUT		/*! Output ready */
#define CW_IO_PRI	POLLPRI		/*! Priority input ready */

/* Implicitly polled for */
#define CW_IO_ERR	POLLERR		/*! Error condition (errno or getsockopt) */
#define CW_IO_HUP	POLLHUP		/*! Hangup */

typedef struct io_context *cw_io_context_t;

#define CW_IO_CONTEXT_NONE	NULL

struct cw_io_rec;


/*
 * An CallWeaver IO callback takes its id, a file descriptor, list of events, and
 * callback data as arguments and returns 0 if it should not be
 * run again, or non-zero if it should be run again.
 */
typedef int (*cw_io_cb)(struct cw_io_rec *ior, int fd, short events, void *cbdata);


struct cw_io_rec {
	cw_io_cb callback;		/* What is to be called */
	void *data; 			/* Data to be passed */
	unsigned int id; 		/* ID number */
};


static inline void cw_io_init(struct cw_io_rec *ior, cw_io_cb callback, void *data)
{
	ior->callback = callback;
	ior->data = data;
	ior->id = UINT_MAX;
}


#define cw_io_isactive(ior)	((ior)->id != UINT_MAX)


/*! Creates a context
 *
 * Create a context for I/O operations
 *
 * \param slots	number of slots (file descriptors) to allocate initially (the number grows as necessary)
 *
 * \return the created I/O context
 */
extern CW_API_PUBLIC cw_io_context_t cw_io_context_create(int slots);

/*! Destroys a context
 *
 * \param ioc	context to destroy
 *
 * Destroy an I/O context and release any associated memory
 */
extern CW_API_PUBLIC void cw_io_context_destroy(cw_io_context_t ioc);


/*! Adds an IO context
 *
 * \param ioc		which context to use
 * \param fd		which fd to monitor
 * \param events	events to wait for
 *
 * \return 0 on success or -1 on failure
 */
extern CW_API_PUBLIC int cw_io_add(cw_io_context_t ioc, struct cw_io_rec *ior, int fd, short events);


/*! Removes an IO context
 *
 * \param ioc	io_context to remove it from
 * \param ior	io_rec to remove
 *
 * \return 0 on success or -1 on failure.
 */
extern CW_API_PUBLIC void cw_io_remove(cw_io_context_t ioc, struct cw_io_rec *ior);


/*! Waits for IO
 *
 * \param ioc		context to act upon
 * \param howlong	how many milliseconds to wait
 *
 * Wait for I/O to happen, returning after
 * howlong milliseconds, and after processing
 * any necessary I/O.  Returns the number of
 * I/O events which took place.
 */
extern CW_API_PUBLIC int cw_io_run(cw_io_context_t ioc, int howlong);


#endif /* HAVE_EPOLL */


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_IO_H */
