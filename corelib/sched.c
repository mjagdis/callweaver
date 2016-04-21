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
 *
 * \brief Scheduler Routines
 *
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/sched.h"
#include "callweaver/logger.h"
#include "callweaver/lock.h"
#include "callweaver/utils.h"
#include "callweaver/options.h"

/* Determine if a is sooner than b */
#define SOONER(a,b) (((b).tv_sec > (a).tv_sec) || \
					 (((b).tv_sec == (a).tv_sec) && ((b).tv_usec > (a).tv_usec)))

struct sched_context {
	cw_mutex_t lock;
	cw_cond_t timed;
	cw_cond_t untimed;
	int timed_present;

	/* Schedule entry and main queue */
	struct sched_state *schedq;

	int nthreads;
	pthread_t tid[0];
};


static void schedule(struct sched_context *con, struct sched_state *s)
{
	/* Take a sched structure and put it in the
	 * queue, such that the soonest event is
	 * first in the list. 
	 */
	 
	struct sched_state **s_p;

	for (s_p = &con->schedq; *s_p; s_p = &(*s_p)->next) {
		if (SOONER(s->when, (*s_p)->when))
			break;
	}

	/* If we insert at the head of the list we need to let a service thread
	 * know things have changed. If all the service threads are busy that
	 * isn't a problem.
	 */
	if (s_p == &con->schedq)
		cw_cond_signal(con->timed_present ? &con->timed : &con->untimed);

	/* Insert this event into the schedule */
	s->next = *s_p;
	*s_p = s;
}


static int cw_sched_add_variable_nolock(struct sched_context *con, struct sched_state *state, int when, cw_sched_cb callback, void *data, cw_schedfail_cb failed)
{
	int res = -1;

	if (!state->callback) {
		state->vers++;
		state->callback = callback;
		state->data = data;
		state->failed = failed;
		state->resched = when;
		state->when = cw_tvadd(cw_tvnow(), cw_samp2tv(when, 1000));
		schedule(con, state);
		res = 0;
	}

	return res;
}

int cw_sched_add_variable(struct sched_context *con, struct sched_state *state, int when, cw_sched_cb callback, void *data, cw_schedfail_cb failed)
{
	int res;

	cw_mutex_lock(&con->lock);
	res = cw_sched_add_variable_nolock(con, state, when, callback, data, failed);
	cw_mutex_unlock(&con->lock);

	return res;
}


static int cw_sched_del_nolock(struct sched_context *con, struct sched_state *state)
{
	struct sched_state **s_p, *s;
	int res = -1;

	state->vers++;

	if (state->callback) {
		for (s_p = &con->schedq; *s_p; s_p = &(*s_p)->next) {
			if ((*s_p) == state) {
				s = *s_p;
				*s_p = s->next;
				s->callback = NULL;
				res = 0;
				break;
			}
		}
	}

	return res;
}

int cw_sched_del(struct sched_context *con, struct sched_state *state)
{
	int res;

	cw_mutex_lock(&con->lock);
	res = cw_sched_del_nolock(con, state);
	cw_mutex_unlock(&con->lock);

	return res;
}


int cw_sched_modify_variable(struct sched_context *con, struct sched_state *state, int when, cw_sched_cb callback, void *data, cw_schedfail_cb failed)
{
	int res;

	cw_mutex_lock(&con->lock);
	res = cw_sched_del_nolock(con, state);
	cw_sched_add_variable_nolock(con, state, when, callback, data, failed);
	cw_mutex_unlock(&con->lock);

	return res;
}


long cw_sched_when(struct sched_context *con, struct sched_state *state)
{
	long secs;

	cw_mutex_lock(&con->lock);

	struct timeval now = cw_tvnow();
	secs = state->when.tv_sec - now.tv_sec;

	cw_mutex_unlock(&con->lock);

	return secs;
}


static void sched_mutex_unlock(void *mutex)
{
	cw_mutex_unlock(mutex);
}


static void *service_thread(void *data)
{
	struct timeval now;
	struct sched_context *con = data;

	cw_mutex_lock(&con->lock);
	pthread_cleanup_push(sched_mutex_unlock, &con->lock);

	/* We schedule all events which are going to expire within 1ms.
	 * We only care about millisecond accuracy anyway, so this will
	 * help us service events in batches where possible. Doing so
	 * may have cache advantages where the event handlers do similar
	 * work and will certainly reduce the sleep/wakeup overhead.
	 */
	now = cw_tvadd(cw_tvnow(), cw_tv(0, 1000));
	for (;;) {
		while (con->schedq && SOONER(con->schedq->when, now)) {
			struct sched_state *current = con->schedq;
			struct sched_state copy = *current;
			int res;

			con->schedq = con->schedq->next;

			current->callback = NULL;

			cw_cond_signal(&con->untimed);
			cw_mutex_unlock(&con->lock);

			res = copy.callback(copy.data);

			cw_mutex_lock(&con->lock);

			/* If they return non-zero and nothing else has taken charge of the
			 * schedule entry in the meantime we schedule them to be run again.
			 */
			if (copy.failed && res) {
				if (copy.vers != current->vers || cw_sched_add_variable_nolock(con, current, res, copy.callback, copy.data, copy.failed))
					copy.failed(copy.data);
			}

			now = cw_tvadd(cw_tvnow(), cw_tv(0, 1000));
		}

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		if (!con->timed_present && con->schedq) {
			struct timespec tick;
			tick.tv_sec = con->schedq->when.tv_sec;
			tick.tv_nsec = 1000 * con->schedq->when.tv_usec;
			con->timed_present = 1;
			while (cw_cond_timedwait(&con->timed, &con->lock, &tick) < 0 && errno == EINTR);
			con->timed_present = 0;
		} else {
			while (cw_cond_wait(&con->untimed, &con->lock) < 0 && errno == EINTR);
		}

		now = cw_tvadd(cw_tvnow(), cw_tv(0, 1000));
	}

	pthread_cleanup_pop(1);
	return NULL;
}


struct sched_context *sched_context_create(int nthreads)
{
	struct sched_context *con;
	int i, n;

	if ((con = malloc(sizeof(*con) + nthreads * sizeof(con->tid[0])))) {
		cw_cond_init(&con->timed, NULL);
		cw_cond_init(&con->untimed, NULL);
		cw_mutex_init_attr(&con->lock, &global_mutexattr_errorcheck);
		con->timed_present = 0;
		con->schedq = NULL;
		con->nthreads = nthreads;
		for (n = 0, i = 0; i < nthreads; i++) {
			con->tid[i] = CW_PTHREADT_NULL;
			if (!cw_pthread_create(&con->tid[i], &global_attr_default, service_thread, con))
				n++;
		}

		if (n != nthreads) {
			cw_log(CW_LOG_ERROR, "only %d of %d service threads started successfully\n", n, nthreads);
			if (n == 0) {
				sched_context_destroy(con);
				con = NULL;
			}
		}
	}

	return con;
}


void sched_context_destroy(struct sched_context *con)
{
	int i;

	for (i = 0; i < con->nthreads; i++)
		if (!pthread_equal(con->tid[i], CW_PTHREADT_NULL))
			pthread_cancel(con->tid[i]);

	for (i = 0; i < con->nthreads; i++)
		if (!pthread_equal(con->tid[i], CW_PTHREADT_NULL))
			pthread_join(con->tid[i], NULL);

	cw_cond_destroy(&con->timed);
	cw_cond_destroy(&con->untimed);
	cw_mutex_destroy(&con->lock);
	free(con);
}
