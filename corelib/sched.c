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
 * \brief Scheduler Routines (from cheops-NG)
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

struct sched {
	struct sched *next;		/* Next event in the list */
	int id; 			/* ID number of event */
	struct timeval when;		/* Absolute time event should take place */
	int resched;			/* When to reschedule */
	int variable;		/* Use return value from callback to reschedule */
	void *data; 			/* Data */
	cw_sched_cb callback;		/* Callback */
};

struct sched_context {
	cw_mutex_t lock;
	cw_cond_t timed;
	cw_cond_t untimed;
	int timed_present;

	/* Number of events processed */
	int eventcnt;

	/* Number of outstanding schedule events */
	int schedcnt;

	/* Schedule entry and main queue */
 	struct sched *schedq;

#ifdef SCHED_MAX_CACHE
	/* Cache of unused schedule structures and how many */
	struct sched *schedc;
	int schedccnt;
#endif

	int nthreads;
	pthread_t tid[0];
};


static struct sched *sched_alloc(struct sched_context *con)
{
	/*
	 * We keep a small cache of schedule entries
	 * to minimize the number of necessary malloc()'s
	 */
	struct sched *tmp;

#ifdef SCHED_MAX_CACHE
	if (con->schedc) {
		tmp = con->schedc;
		con->schedc = con->schedc->next;
		con->schedccnt--;
	} else
#endif
		tmp = malloc(sizeof(struct sched));
	return tmp;
}


static void sched_release(struct sched_context *con, struct sched *tmp)
{
	/*
	 * Add to the cache, or just free() if we
	 * already have too many cache entries
	 */

#ifdef SCHED_MAX_CACHE	 
	if (con->schedccnt < SCHED_MAX_CACHE) {
		tmp->next = con->schedc;
		con->schedc = tmp;
		con->schedccnt++;
	} else
#endif
	    free(tmp);
}


static void schedule(struct sched_context *con, struct sched *s)
{
	/*
	 * Take a sched structure and put it in the
	 * queue, such that the soonest event is
	 * first in the list. 
	 */
	 
	struct sched **s_p;

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
	con->schedcnt++;
}


static int cw_sched_add_variable_nolock(struct sched_context *con, int when, cw_sched_cb callback, void *data, int variable)
{
	/*
	 * Schedule callback(data) to happen when ms into the future
	 */
	struct sched *tmp;
	int res = -1;

	if ((tmp = sched_alloc(con))) {
		if ((tmp->id = con->eventcnt++) < 0)
			tmp->id = con->eventcnt = 0;
		tmp->callback = callback;
		tmp->data = data;
		tmp->resched = when;
		tmp->variable = variable;
		tmp->when = cw_tvadd(cw_tvnow(), cw_samp2tv(when, 1000));
		schedule(con, tmp);
		res = tmp->id;
	}

	return res;
}

int cw_sched_add_variable(struct sched_context *con, int when, cw_sched_cb callback, void *data, int variable)
{
	int res;

	cw_mutex_lock(&con->lock);
	res = cw_sched_add_variable_nolock(con, when, callback, data, variable);
	cw_mutex_unlock(&con->lock);
	return res;
}


static int cw_sched_del_nolock(struct sched_context *con, int id)
{
	/*
	 * Delete the schedule entry with number
	 * "id".  It's nearly impossible that there
	 * would be two or more in the list with that
	 * id.
	 */
	struct sched **s_p, *s;

	for (s_p = &con->schedq; *s_p; s_p = &(*s_p)->next) {
		if ((*s_p)->id == id) {
			s = *s_p;
			*s_p = s->next;
			con->schedcnt--;
			sched_release(con, s);
			return 0;
		}
	}

	if (option_debug)
		cw_log(CW_LOG_DEBUG, "Attempted to delete nonexistent schedule entry %d!\n", id);

	return -1;
}

int cw_sched_del(struct sched_context *con, int id)
{
	int res;

	cw_mutex_lock(&con->lock);
	res = cw_sched_del_nolock(con, id);
	cw_mutex_unlock(&con->lock);
	return res;
}


int cw_sched_modify_variable(struct sched_context *con, int id, int when, cw_sched_cb callback, void *data, int variable)
{
	cw_mutex_lock(&con->lock);
	cw_sched_del_nolock(con, id);
	id = cw_sched_add_variable_nolock(con, when, callback, data, variable);
	cw_mutex_unlock(&con->lock);
	return id;
}


long cw_sched_when(struct sched_context *con,int id)
{
	struct sched *s;
	long secs = -1;

	cw_mutex_lock(&con->lock);

	for (s = con->schedq; s; s = s->next) {
		if (s->id == id) {
			struct timeval now = cw_tvnow();
			secs = s->when.tv_sec - now.tv_sec;
			break;
		}
	}

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
	struct sched *current;
	int res;

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
			current = con->schedq;
			con->schedq = con->schedq->next;
			con->schedcnt--;

			cw_cond_signal(&con->untimed);
			cw_mutex_unlock(&con->lock);

			if ((res = current->callback(current->data))) {
				/* If they return non-zero, we should schedule them to be run again. */
				current->when = cw_tvadd(current->when, cw_samp2tv((current->variable ? res : current->resched), 1000));
				schedule(con, current);
			}

			cw_mutex_lock(&con->lock);

			if (!res) {
				/* No longer needed, so release it */
				sched_release(con, current);
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
		con->eventcnt = 1;
		con->schedcnt = 0;
		con->schedq = NULL;
#ifdef SCHED_MAX_CACHE
		con->schedc = NULL;
		con->schedccnt = 0;
#endif
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
	struct sched *s, *sl;
	int i;

	for (i = 0; i < con->nthreads; i++)
		if (!pthread_equal(con->tid[i], CW_PTHREADT_NULL))
			pthread_cancel(con->tid[i]);

	for (i = 0; i < con->nthreads; i++)
		if (!pthread_equal(con->tid[i], CW_PTHREADT_NULL))
			pthread_join(con->tid[i], NULL);

	cw_cond_destroy(&con->timed);
	cw_cond_destroy(&con->untimed);
	cw_mutex_lock(&con->lock);

#ifdef SCHED_MAX_CACHE
	/* Eliminate the cache */
	s = con->schedc;
	while(s) {
		sl = s;
		s = s->next;
		free(sl);
	}
#endif
	/* And the queue */
	s = con->schedq;
	while(s) {
		sl = s;
		s = s->next;
		free(sl);
	}
	/* And the context */
	cw_mutex_unlock(&con->lock);
	cw_mutex_destroy(&con->lock);
	free(con);
}
