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
	struct opbx_generator_instance *gen = data;
	struct timespec tick;
	struct opbx_frame *f;
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

	opbx_log(OPBX_LOG_DEBUG, "%s: Generator thread started\n", gen->chan->name);

	f = gen->class->generate(gen->chan, gen->pvt, 160);
	while (f) {
		opbx_write(gen->chan, f);

		if (!opbx_tvzero(f->delivery)) {
			clk = CLOCK_REALTIME;
			tick.tv_sec = f->delivery.tv_sec;
			tick.tv_nsec = 1000L * f->delivery.tv_usec;
		} else if (f->len) {
			tick.tv_sec += f->len / 1000;
			tick.tv_nsec += 1000000L * (f->len % 1000);
		} else if (f->samples) {
			int n = (f->samples / f->samplerate);
			tick.tv_sec += n;
			tick.tv_nsec += 1000000L * ((1000 * (f->samples - n * f->samplerate)) / f->samplerate);
		} else {
			/* If we have a null frame whatever is generating data just wasn't
			 * ready for us so we need to give it some time.
			 * But wait! We might be real time. The data source might not be.
			 * So sched_yield() may not help - we _need_ to actually sleep.
			 * We'll choose an arbitrary 0.5ms.
			 */
			tick.tv_nsec += 500000L;
		}

		/* Normalize */
		if (tick.tv_nsec >= 1000000000L) {
			tick.tv_nsec -= 1000000000L;
			tick.tv_sec++;
		}

		opbx_fr_free(f);

		f = gen->class->generate(gen->chan, gen->pvt, 160);

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

#if !defined(__USE_XOPEN2K)
		while (opbx_cond_timedwait(&cond, &mutex, &tick) < 0 && errno == EINTR);
#else
		while (clock_nanosleep(clk, TIMER_ABSTIME, &tick, NULL) && errno == EINTR);
#endif

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	}

	opbx_log(OPBX_LOG_DEBUG, "%s: Generator self-deactivating\n", gen->chan->name);

	/* Next write on the channel should clean out the defunct generator */
	opbx_set_flag(gen->chan, OPBX_FLAG_WRITE_INT);

#if !defined(__USE_XOPEN2K)
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
#endif
	return NULL;
}


void opbx_generator_deactivate(struct opbx_generator_instance *gen)
{
	if (!gen->chan)
		return;

	opbx_mutex_lock(&gen->chan->lock);

	if (!pthread_equal(gen->tid, OPBX_PTHREADT_NULL)) {
		char name[OPBX_CHANNEL_NAME];
		struct opbx_generator_instance generator;

		opbx_log(OPBX_LOG_DEBUG, "%s: Trying to deactivate generator\n", gen->chan->name);

		opbx_copy_string(name, gen->chan->name, sizeof(name));
		generator = *gen;
		gen->tid = OPBX_PTHREADT_NULL;
		opbx_clear_flag(gen->chan, OPBX_FLAG_WRITE_INT);

		opbx_mutex_unlock(&gen->chan->lock);

		pthread_cancel(generator.tid);
		pthread_join(generator.tid, NULL);
		generator.class->release(generator.chan, generator.pvt);
		opbx_object_put(generator.class);
		opbx_log(OPBX_LOG_DEBUG, "%s: Generator stopped\n", name);
	} else
		opbx_mutex_unlock(&gen->chan->lock);
}


int opbx_generator_activate(struct opbx_channel *chan, struct opbx_generator_instance *gen, struct opbx_generator *class, void *params)
{
	opbx_mutex_lock(&chan->lock);

	while (!pthread_equal(gen->tid, OPBX_PTHREADT_NULL)) {
		opbx_mutex_unlock(&chan->lock);
		opbx_generator_deactivate(gen);
		opbx_mutex_lock(&chan->lock);
	}

	if ((gen->pvt = class->alloc(chan, params))) {
		gen->class = opbx_object_get(class);

		gen->chan = chan;
		if (opbx_pthread_create(&gen->tid, &global_attr_rr, opbx_generator_thread, gen)) {
			opbx_log(OPBX_LOG_ERROR, "%s: unable to start generator thread: %s\n", chan->name, strerror(errno));
			class->release(chan, gen->pvt);
			opbx_mutex_unlock(&chan->lock);
			opbx_object_put(class);
			return -1;
		}
	}
	/* It's down to the class allocator to log its problem */

	opbx_mutex_unlock(&chan->lock);
	return 0;
}
