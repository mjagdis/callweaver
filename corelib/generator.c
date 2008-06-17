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
#include "callweaver/time.h"


/* Note: clock_nanosleep is an Advanced Realtime POSIX function.
 * With GNU libc it is present if you build with __USE_XOPEN2K. Other platforms
 * will need other tests. If clock_nanosleep is not present we fall back on
 * pthread_cond_timedwait. This is as good at getting accurate ticks but the
 * unavoidable mutex (un)locking around the (unused) condition will cost
 * some performance.
 */
static void *cw_generator_thread(void *data)
{
	struct cw_generator_instance *gen = data;
	struct timespec tick;
	struct cw_frame *f;
	clockid_t clk = global_clock_monotonic;
#if !defined(__USE_XOPEN2K)
	cw_cond_t cond;
	cw_mutex_t mutex;

	cw_cond_init(&cond, NULL);
	pthread_cleanup_push(cw_cond_destroy, &cond);
	cw_mutex_init(&mutex);
	pthread_cleanup_push(cw_mutex_destroy, &mutex);
	cw_mutex_lock(&mutex);
#endif

	cw_clock_gettime(clk, &tick);

	cw_log(CW_LOG_DEBUG, "%s: Generator thread started\n", gen->chan->name);

	f = gen->class->generate(gen->chan, gen->pvt, 160);
	while (f) {
		cw_write(gen->chan, &f);

		if (!cw_tvzero(f->delivery)) {
			clk = CLOCK_REALTIME;
			tick.tv_sec = f->delivery.tv_sec;
			tick.tv_nsec = 1000L * f->delivery.tv_usec;
		} else if (f->duration) {
			tick.tv_sec += f->duration / 1000;
			tick.tv_nsec += 1000000L * (f->duration % 1000);
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

		cw_fr_free(f);

		f = gen->class->generate(gen->chan, gen->pvt, 160);

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

#if !defined(__USE_XOPEN2K)
		while (cw_cond_timedwait(&cond, &mutex, &tick) == EINTR);
#else
		while (clock_nanosleep(clk, TIMER_ABSTIME, &tick, NULL) && errno == EINTR);
#endif

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	}

	cw_log(CW_LOG_DEBUG, "%s: Generator self-deactivating\n", gen->chan->name);

	/* Next write on the channel should clean out the defunct generator */
	cw_set_flag(gen->chan, CW_FLAG_WRITE_INT);

#if !defined(__USE_XOPEN2K)
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
#endif
	return NULL;
}


void cw_generator_deactivate(struct cw_generator_instance *gen)
{
	if (!gen->chan)
		return;

	cw_mutex_lock(&gen->chan->lock);

	if (!pthread_equal(gen->tid, CW_PTHREADT_NULL)) {
		char name[CW_CHANNEL_NAME];
		struct cw_generator_instance generator;

		cw_log(CW_LOG_DEBUG, "%s: Trying to deactivate generator\n", gen->chan->name);

		cw_copy_string(name, gen->chan->name, sizeof(name));
		generator = *gen;
		gen->tid = CW_PTHREADT_NULL;
		cw_clear_flag(gen->chan, CW_FLAG_WRITE_INT);

		cw_mutex_unlock(&gen->chan->lock);

		pthread_cancel(generator.tid);
		pthread_join(generator.tid, NULL);
		generator.class->release(generator.chan, generator.pvt);
		cw_object_put(generator.class);
		cw_log(CW_LOG_DEBUG, "%s: Generator stopped\n", name);
	} else
		cw_mutex_unlock(&gen->chan->lock);
}


int cw_generator_activate(struct cw_channel *chan, struct cw_generator_instance *gen, struct cw_generator *class, void *params)
{
	cw_mutex_lock(&chan->lock);

	while (!pthread_equal(gen->tid, CW_PTHREADT_NULL)) {
		cw_mutex_unlock(&chan->lock);
		cw_generator_deactivate(gen);
		cw_mutex_lock(&chan->lock);
	}

	if ((gen->pvt = class->alloc(chan, params))) {
		gen->class = cw_object_get(class);

		gen->chan = chan;
		if (cw_pthread_create(&gen->tid, &global_attr_rr, cw_generator_thread, gen)) {
			cw_log(CW_LOG_ERROR, "%s: unable to start generator thread: %s\n", chan->name, strerror(errno));
			class->release(chan, gen->pvt);
			cw_mutex_unlock(&chan->lock);
			cw_object_put(class);
			return -1;
		}
	}
	/* It's down to the class allocator to log its problem */

	cw_mutex_unlock(&chan->lock);
	return 0;
}
