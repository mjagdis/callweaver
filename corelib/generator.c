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


struct opbx_generator_instance {
	struct opbx_channel *chan;
	struct opbx_generator *class;
	void *pvt;
	int gen_samp;
	struct timespec interval;
};


/* Note: clock_nanosleep is an Advanced Realtime POSIX function.
 * With GNU libc it is present if you build with __USE_XOPEN2K. Other platforms
 * will need other tests. If clock_nanosleep is not present we fall back on
 * pthread_cond_timedwait. This is as good at getting accurate ticks but the
 * unavoidable mutex (un)locking around the (unused) condition will cost
 * some performance.
 */
#undef _POSIX_MONOTONIC_CLOCK
static void *opbx_generator_thread(void *data)
{
	struct opbx_generator_instance *inst = data;
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
	opbx_mutex_init(&mutex);
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

	opbx_log(OPBX_LOG_DEBUG, "%s: Generator thread started\n", inst->chan->name);

	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		tick.tv_sec += inst->interval.tv_sec;
		tick.tv_nsec += inst->interval.tv_nsec;
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

		if (inst->class->generate(inst->chan, inst->pvt, inst->gen_samp)) {
			opbx_log(OPBX_LOG_DEBUG, "%s: Generator self-deactivating\n", inst->chan->name);
			break;
		}
	}

	opbx_log(OPBX_LOG_DEBUG, "%s: Generator thread shut down\n", inst->chan->name);
	inst->class->release(inst->chan, inst->pvt);
	opbx_object_put(inst->class);
	free(inst);
#if !defined(__USE_XOPEN2K)
	opbx_mutex_destroy(&mutex);
	opbx_cond_destroy(&cond);
#endif
	return NULL;
}


void opbx_generator_deactivate(struct opbx_channel *chan)
{
	if (!pthread_equal(chan->pgenerator_thread, OPBX_PTHREADT_NULL)) {
		opbx_log(OPBX_LOG_DEBUG, "%s: Trying to deactivate generator\n", chan->name);

		opbx_mutex_lock(&chan->lock);
		pthread_cancel(chan->pgenerator_thread);
		pthread_join(chan->pgenerator_thread, NULL);
		chan->pgenerator_thread = OPBX_PTHREADT_NULL;
		opbx_mutex_unlock(&chan->lock);

		opbx_log(OPBX_LOG_DEBUG, "%s: Generator stopped\n", chan->name);
	}
}


int opbx_generator_activate(struct opbx_channel *chan, struct opbx_generator *class, void *params)
{
	struct opbx_generator_instance *inst;

	opbx_mutex_lock(&chan->lock);

	opbx_generator_deactivate(chan);

	if ((inst = malloc(sizeof( *inst)))) {
		if ((inst->pvt = class->alloc(chan, params))) {
			inst->chan = chan;
			inst->class = opbx_object_get(class);
			inst->gen_samp = (chan->gen_samples ? chan->gen_samples : 160);

			inst->interval.tv_sec = 0;
			inst->interval.tv_nsec = 1000 * ((1000000L * inst->gen_samp) /  chan->samples_per_second);
			while (inst->interval.tv_nsec >= 1000000000L) {
				inst->interval.tv_nsec -= 1000000000L;
				inst->interval.tv_sec++;
			}

			if (opbx_pthread_create(&chan->pgenerator_thread, &global_attr_default, opbx_generator_thread, inst)) {
				opbx_log(OPBX_LOG_ERROR, "%s: unable to start generator thread: %s\n", chan->name, strerror(errno));
				opbx_mutex_unlock(&chan->lock);
				class->release(chan, inst->pvt);
				opbx_object_put(class);
				free(inst);
				return -1;
			}
			opbx_mutex_unlock(&chan->lock);
			return 0;
		}
		/* It's down to the class allocator to log its problem */
	} else
		opbx_log(OPBX_LOG_ERROR, "Out of memory\n");

	opbx_mutex_unlock(&chan->lock);
	return -1;
}
