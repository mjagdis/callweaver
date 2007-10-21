/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Mike Jagdis <mjagdis@eris-associates.co.uk>
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

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/generator.h"
#include "callweaver/lock.h"


/* Note: clock_nanosleep is an Advanced Realtime POSIX function.
 * With GNU libc it is present if you build with __USE_XOPEN2K. Other platforms
 * will need other tests. If clock_nanosleep is not present we fall back on
 * pthread_cond_timedwait. This is as good at getting accurate ticks but the
 * unavoidable mutex (un)locking around the (unused) condition will cost
 * some performance.
 */
static void *opbx_generator_thread(void *data)
{
	struct opbx_channel *chan = data;
	struct timespec tick;
#if !defined(_POSIX_TIMERS)
	struct timeval tv;
#elif defined(_POSIX_MONOTONIC_CLOCK) && defined(__USE_XOPEN2K)
	clockid_t clk = CLOCK_MONOTONIC;
#else
	clockid_t clk = CLOCK_REALTIME;
#endif
#if !defined(__USE_XOPEN2K)
	opbx_cond_t cond;
	opbx_mutex_t mutex;

	opbx_cond_init(&cond, NULL);
	pthread_cleanup_push(opbx_cond_destroy, &cond);
	opbx_mutex_init(&mutex);
	pthread_cleanup_push(opbx_mutex_destroy, &mutex);
	opbx_mutex_lock(&mutex);
#endif

#if !defined(_POSIX_TIMERS)
	gettimeofday(&tv, NULL);
	tick.tv_sec = tv.tv_sec;
	tick.tv_nsec = 1000 * tv.tv_usec;
#else
	if (clock_gettime(clk, &tick)) {
		clk = CLOCK_REALTIME;
		clock_gettime(clk, &tick);
	}
#endif

	opbx_log(OPBX_LOG_DEBUG, "%s: Generator thread started\n", chan->name);

	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		tick.tv_sec += chan->generator.interval.tv_sec;
		tick.tv_nsec += chan->generator.interval.tv_nsec;
		if (tick.tv_nsec >= 1000000000L) {
			tick.tv_nsec -= 1000000000L;
			tick.tv_sec++;
		}

#if !defined(__USE_XOPEN2K)
		while (opbx_cond_timedwait(&cond, &mutex, &tick) < 0 && errno == EINTR);
#else
		while (clock_nanosleep(clk, TIMER_ABSTIME, &tick, NULL) && errno == EINTR);
#endif

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (chan->generator.class->generate(chan, chan->generator.pvt, chan->generator.gen_samp)) {
			opbx_log(OPBX_LOG_DEBUG, "%s: Generator self-deactivating\n", chan->name);
			break;
		}
	}

	/* Next write on the channel should clean out the defunct generator */
	opbx_set_flag(chan, OPBX_FLAG_WRITE_INT);

#if !defined(__USE_XOPEN2K)
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
#endif
	return NULL;
}


void opbx_generator_deactivate(struct opbx_channel *chan)
{
	opbx_mutex_lock(&chan->lock);

	if (!pthread_equal(chan->generator.tid, OPBX_PTHREADT_NULL)) {
		opbx_log(OPBX_LOG_DEBUG, "%s: Trying to deactivate generator\n", chan->name);

		pthread_cancel(chan->generator.tid);
		pthread_join(chan->generator.tid, NULL);
		opbx_clear_flag(chan, OPBX_FLAG_WRITE_INT);
		chan->generator.tid = OPBX_PTHREADT_NULL;
		chan->generator.class->release(chan, chan->generator.pvt);
		opbx_object_put(chan->generator.class);
		opbx_log(OPBX_LOG_DEBUG, "%s: Generator stopped\n", chan->name);
	}

	opbx_mutex_unlock(&chan->lock);
}


int opbx_generator_activate(struct opbx_channel *chan, struct opbx_generator *class, void *params)
{
	opbx_mutex_lock(&chan->lock);

	opbx_generator_deactivate(chan);

	if ((chan->generator.pvt = class->alloc(chan, params))) {
		chan->generator.class = opbx_object_get(class);
		chan->generator.gen_samp = (chan->gen_samples ? chan->gen_samples : 160);

		chan->generator.interval.tv_sec = 0;
		chan->generator.interval.tv_nsec = 1000 * ((1000000L * chan->generator.gen_samp) /  chan->samples_per_second);
		while (chan->generator.interval.tv_nsec >= 1000000000L) {
			chan->generator.interval.tv_nsec -= 1000000000L;
			chan->generator.interval.tv_sec++;
		}

		if (opbx_pthread_create(&chan->generator.tid, &global_attr_rr, opbx_generator_thread, chan)) {
			opbx_log(OPBX_LOG_ERROR, "%s: unable to start generator thread: %s\n", chan->name, strerror(errno));
			class->release(chan, chan->generator.pvt);
			opbx_mutex_unlock(&chan->lock);
			opbx_object_put(class);
			return -1;
		}
	}
	/* It's down to the class allocator to log its problem */

	opbx_mutex_unlock(&chan->lock);
	return 0;
}
