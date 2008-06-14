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
 * \brief Time-related functions and macros
 */

#ifndef _CALLWEAVER_TIME_H
#define _CALLWEAVER_TIME_H

#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "callweaver/inline_api.h"


#ifdef _POSIX_TIMERS

extern clockid_t global_clock_monotonic;

#define cw_clock_gettime(clock_id, timespec_p) clock_gettime((clock_id), (timespec_p))

#else /* _POSIX_TIMERS */

struct timespec {
	time_t tv_sec;
	long tv_nsec;
};

#define global_clock_monotonic 0

CW_INLINE_API(
int cw_clock_gettime(int clk, struct timespec *ts),
{
	struct timeval tv;

	if (!gettimeofday(&tv, NULL)) {
		ts->tv_sec = tv.tv_sec;
		ts->tv_nsec = 1000L * tv.tv_usec;
		return 0;
	}
	return -1;
}
)

#endif /* _POSIX_TIMERS */


CW_INLINE_API(
int cw_clock_diff_ms(struct timespec *end, struct timespec *start),
{
	return (end->tv_sec - start->tv_sec) * 1000L
		+ ((1000000000L + end->tv_nsec - start->tv_nsec) / 1000000) - 1000L;
}
)


CW_INLINE_API(
void cw_clock_add_ms(struct timespec *ts, int ms),
{
	ts->tv_nsec += 1000000L * ms;
	while (ts->tv_nsec >= 1000000000L) {
		ts->tv_nsec -= 1000000000L;
		ts->tv_sec++;
	}
}
)


/* We have to let the compiler learn what types to use for the elements of a
   struct timeval since on linux, it's time_t and suseconds_t, but on *BSD,
   they are just a long. */
extern struct timeval tv;
typedef typeof(tv.tv_sec) cw_time_t;
typedef typeof(tv.tv_usec) cw_suseconds_t;

/*!
 * \brief Computes the difference (in microseconds) between two \c struct \c timeval instances.
 * \param end the beginning of the time period
 * \param start the end of the time period
 * \return the difference in microseconds
 */
CW_INLINE_API(
int cw_tvdiff(struct timeval end, struct timeval start),
{
	/* the offset by 1,000,000 below is intentional...
	   it avoids differences in the way that division
	   is handled for positive and negative numbers, by ensuring
	   that the divisor is always positive
	*/
	return  ((end.tv_sec - start.tv_sec) * 1000000) +
		(end.tv_usec - start.tv_usec);
}
)

/*!
 * \brief Computes the difference (in milliseconds) between two \c struct \c timeval instances.
 * \param end the beginning of the time period
 * \param start the end of the time period
 * \return the difference in milliseconds
 */
CW_INLINE_API(
int cw_tvdiff_ms(struct timeval end, struct timeval start),
{
	/* the offset by 1,000,000 below is intentional...
	   it avoids differences in the way that division
	   is handled for positive and negative numbers, by ensuring
	   that the divisor is always positive
	*/
	return  ((end.tv_sec - start.tv_sec) * 1000) +
		(((1000000 + end.tv_usec - start.tv_usec) / 1000) - 1000);
}
)

/*!
 * \brief Returns true if the argument is 0,0
 */
CW_INLINE_API(
int cw_tvzero(const struct timeval t),
{
	return (t.tv_sec == 0 && t.tv_usec == 0);
}
)

/*!
 * \brief Compres two \c struct \c timeval instances returning
 * -1, 0, 1 if the first arg is smaller, equal or greater to the second.
 */
CW_INLINE_API(
int cw_tvcmp(struct timeval _a, struct timeval _b),
{
	if (_a.tv_sec < _b.tv_sec)
		return -1;
	if (_a.tv_sec > _b.tv_sec)
		return 1;
	/* now seconds are equal */
	if (_a.tv_usec < _b.tv_usec)
		return -1;
	if (_a.tv_usec > _b.tv_usec)
		return 1;
	return 0;
}
)

/*!
 * \brief Returns true if the two \c struct \c timeval arguments are equal.
 */
CW_INLINE_API(
int cw_tveq(struct timeval _a, struct timeval _b),
{
	return (_a.tv_sec == _b.tv_sec && _a.tv_usec == _b.tv_usec);
}
)

/*!
 * \brief Returns current timeval. Meant to replace calls to gettimeofday().
 */
CW_INLINE_API(
struct timeval cw_tvnow(void),
{
	struct timeval t;
	gettimeofday(&t, NULL);
	return t;
}
)

/*!
 * \brief Returns the sum of two timevals a + b
 */
struct timeval cw_tvadd(struct timeval a, struct timeval b);

/*!
 * \brief Returns the difference of two timevals a - b
 */
struct timeval cw_tvsub(struct timeval a, struct timeval b);

/*!
 * \brief Returns a timeval from sec, usec
 */
#if 0
CW_INLINE_API(
struct timeval cw_tv(int sec, int usec),
{
	struct timeval t = { sec, usec};
	return t;
}
)
#endif
CW_INLINE_API(
struct timeval cw_tv(cw_time_t sec, cw_suseconds_t usec),
{
	struct timeval t;
	t.tv_sec = sec;
	t.tv_usec = usec;
	return t;
}
)

/*!
 * \brief Returns a timeval corresponding to the duration of n samples at rate r.
 * Useful to convert samples to timevals, or even milliseconds to timevals
 * in the form cw_samp2tv(milliseconds, 1000)
 */
CW_INLINE_API(
struct timeval cw_samp2tv(unsigned int _nsamp, unsigned int _rate),
{
	return cw_tv(_nsamp / _rate, (_nsamp % _rate) * (1000000 / _rate));
}
)

#endif /* _CALLWEAVER_TIME_H */
