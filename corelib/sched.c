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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

//#undef DEBUG_SCHED

#ifdef DEBUG_SCHEDULER
#define DEBUG_LOG(a) DEBUG_M(a)
#else
#define DEBUG_LOG(a) 
#endif

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
	opbx_sched_cb callback;		/* Callback */
};

struct sched_context {
	opbx_cond_t service;
	opbx_mutex_t lock;
	/* Number of events processed */
	int eventcnt;

	/* Number of outstanding schedule events */
	int schedcnt;

	/* Schedule entry and main queue */
 	struct sched *schedq;

	pthread_t tid;

#ifdef SCHED_MAX_CACHE
	/* Cache of unused schedule structures and how many */
	struct sched *schedc;
	int schedccnt;
#endif
};


static void *service_thread(void *data)
{
	struct sched_context *con = data;

	opbx_mutex_lock(&con->lock);
	pthread_cleanup_push((void (*)(void *))opbx_mutex_unlock, &con->lock);

	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		if (con->schedq) {
			struct timespec tick;
			tick.tv_sec = con->schedq->when.tv_sec;
			tick.tv_nsec = 1000 * con->schedq->when.tv_usec;
			while (opbx_cond_timedwait(&con->service, &con->lock, &tick) < 0 && errno == EINTR);
		} else {
			while (opbx_cond_wait(&con->service, &con->lock) < 0 && errno == EINTR);
		}

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		opbx_sched_runq(con);
	}

	pthread_cleanup_pop(1);
	return NULL;
}


void sched_context_destroy(struct sched_context *con)
{
	struct sched *s, *sl;

	if (!pthread_equal(con->tid, OPBX_PTHREADT_NULL)) {
		pthread_cancel(con->tid);
		pthread_join(con->tid, NULL);
		opbx_cond_destroy(&con->service);
	}

	opbx_mutex_lock(&con->lock);

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
	opbx_mutex_unlock(&con->lock);

	opbx_mutex_destroy(&con->lock);
	free(con);
}


static struct sched_context *context_create(void)
{
	struct sched_context *tmp;
	tmp = malloc(sizeof(struct sched_context));
	if (tmp) {
          	memset(tmp, 0, sizeof(struct sched_context));
		tmp->tid = OPBX_PTHREADT_NULL;
		opbx_mutex_init(&tmp->lock);
		tmp->eventcnt = 1;
		tmp->schedcnt = 0;
		tmp->schedq = NULL;
#ifdef SCHED_MAX_CACHE
		tmp->schedc = NULL;
		tmp->schedccnt = 0;
#endif
	}

	return tmp;
}


struct sched_context *sched_context_create(void)
{
	struct sched_context *tmp;

	tmp = context_create();

	if (tmp) {
		opbx_cond_init(&tmp->service, NULL);
		if (opbx_pthread_create(&tmp->tid, &global_attr_default, service_thread, tmp)) {
			opbx_log(OPBX_LOG_ERROR, "unable to start service thread: %s\n", strerror(errno));
			sched_context_destroy(tmp);
			tmp = NULL;
		}
	}

	return tmp;
}


struct sched_context *sched_manual_context_create(void)
{
	struct sched_context *tmp;

	tmp = context_create();

	return tmp;
}


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

int opbx_sched_wait(struct sched_context *con)
{
	/*
	 * Return the number of milliseconds 
	 * until the next scheduled event
	 */
	int ms;
#ifdef DEBUG_SCHED
	DEBUG_LOG(opbx_log(OPBX_LOG_DEBUG, "opbx_sched_wait()\n"));
#endif
	pthread_cleanup_push((void (*)(void *))opbx_mutex_unlock, &con->lock);
	opbx_mutex_lock(&con->lock);

	if (!con->schedq) {
		ms = -1;
	} else {
		ms = opbx_tvdiff_ms(con->schedq->when, opbx_tvnow());
		if (ms < 0)
			ms = 0;
	}

	pthread_cleanup_pop(1);
	return ms;
	
}


static void schedule(struct sched_context *con, struct sched *s)
{
	/*
	 * Take a sched structure and put it in the
	 * queue, such that the soonest event is
	 * first in the list. 
	 */
	 
	struct sched *last=NULL;
	struct sched *current=con->schedq;
	while(current) {
		if (SOONER(s->when, current->when))
			break;
		last = current;
		current = current->next;
	}
	/* Insert this event into the schedule */
	s->next = current;
	if (last) 
		last->next = s;
	else
		con->schedq = s;
	con->schedcnt++;

	if (!last && !pthread_equal(con->tid, OPBX_PTHREADT_NULL))
		opbx_cond_signal(&con->service);

}

/*
 * given the last event *tv and the offset in milliseconds 'when',
 * computes the next value,
 */
static void sched_settime(struct timeval *tv, int when)
{
	struct timeval now = opbx_tvnow();

	/*opbx_log(OPBX_LOG_DEBUG, "TV -> %lu,%lu\n", tv->tv_sec, tv->tv_usec);*/
	if (opbx_tvzero(*tv))	/* not supplied, default to now */
		*tv = now;
	*tv = opbx_tvadd(*tv, opbx_samp2tv(when, 1000));
	if (opbx_tvcmp(*tv, now) < 0) {
		opbx_log(OPBX_LOG_DEBUG, "Request to schedule in the past?!?!\n");
		*tv = now;
	}
}


int opbx_sched_add_variable(struct sched_context *con, int when, opbx_sched_cb callback, void *data, int variable)
{
	/*
	 * Schedule callback(data) to happen when ms into the future
	 */
	struct sched *tmp;
	int res = -1;

#ifdef DEBUG_SCHED
	DEBUG_LOG(opbx_log(OPBX_LOG_DEBUG, "opbx_sched_add_variable()\n"));
#endif
	if (!when) {
		opbx_log(OPBX_LOG_NOTICE, "Scheduled event in 0 ms?\n");
		return -1;
	}
	opbx_mutex_lock(&con->lock);
	if ((tmp = sched_alloc(con))) {
		tmp->id = con->eventcnt++;
		tmp->callback = callback;
		tmp->data = data;
		tmp->resched = when;
		tmp->variable = variable;
		tmp->when = opbx_tv(0, 0);
		sched_settime(&tmp->when, when);
		schedule(con, tmp);
		res = tmp->id;
	}
#ifdef DUMP_SCHEDULER
	/* Dump contents of the context while we have the lock so nothing gets screwed up by accident. */
	opbx_sched_dump(con);
#endif

	opbx_mutex_unlock(&con->lock);
	return res;
}

int opbx_sched_add(struct sched_context *con, int when, opbx_sched_cb callback, void *data)
{
	return opbx_sched_add_variable(con, when, callback, data, 0);
}

int opbx_sched_del(struct sched_context *con, int id)
{
	/*
	 * Delete the schedule entry with number
	 * "id".  It's nearly impossible that there
	 * would be two or more in the list with that
	 * id.
	 */
	struct sched *last=NULL, *s;
	int deleted = 0;
#ifdef DEBUG_SCHED
	DEBUG_LOG(opbx_log(OPBX_LOG_DEBUG, "opbx_sched_del()\n"));
#endif
	opbx_mutex_lock(&con->lock);
	s = con->schedq;
	while(s) {
		if (s->id == id) {
			if (last)
				last->next = s->next;
			else
				con->schedq = s->next;
			con->schedcnt--;
			sched_release(con, s);
			deleted = 1;
			break;
		}
		last = s;
		s = s->next;
	}

#ifdef DUMP_SCHEDULER
	/* Dump contents of the context while we have the lock so nothing gets screwed up by accident. */
	opbx_sched_dump(con);
#endif

	opbx_mutex_unlock(&con->lock);
	if (!deleted) {
		if (option_debug)
			opbx_log(OPBX_LOG_DEBUG, "Attempted to delete nonexistent schedule entry %d!\n", id);
		return -1;
	} else
		return 0;
}


void opbx_sched_dump(const struct sched_context *con)
{
	/*
	 * Dump the contents of the scheduler to
	 * stderr
	 */
	struct sched *q;
	struct timeval tv = opbx_tvnow();
#ifdef SCHED_MAX_CACHE
	opbx_log(OPBX_LOG_DEBUG, "CallWeaver Schedule Dump (%d in Q, %d Total, %d Cache)\n", con->schedcnt, con->eventcnt - 1, con->schedccnt);
#else
	opbx_log(OPBX_LOG_DEBUG, "CallWeaver Schedule Dump (%d in Q, %d Total)\n", con->schedcnt, con->eventcnt - 1);
#endif

	opbx_log(OPBX_LOG_DEBUG, "=============================================================\n");
	opbx_log(OPBX_LOG_DEBUG, "|ID    Callback          Data              Time  (sec:ms)   |\n");
	opbx_log(OPBX_LOG_DEBUG, "+-----+-----------------+-----------------+-----------------+\n");
 	for (q = con->schedq; q; q = q->next) {
 		struct timeval delta =  opbx_tvsub(q->when, tv);

		opbx_log(OPBX_LOG_DEBUG, "|%.4d | %-15p | %-15p | %.6ld : %.6ld |\n", 
			q->id,
			q->callback,
			q->data,
			delta.tv_sec,
			(long int)delta.tv_usec);
	}
	opbx_log(OPBX_LOG_DEBUG, "=============================================================\n");
	
}

int opbx_sched_runq(struct sched_context *con)
{
	/*
	 * Launch all events which need to be run at this time.
	 */
	struct sched *runq, **endq, *current;
	struct timeval tv;
	int x=0;
	int res;
#ifdef DEBUG_SCHED
	DEBUG_LOG(opbx_log(OPBX_LOG_DEBUG, "opbx_sched_runq()\n"));
#endif		

	opbx_mutex_lock(&con->lock);

	/* schedule all events which are going to expire within 1ms.
	 * We only care about millisecond accuracy anyway, so this will
	 * help us get more than one event at one time if they are very
	 * close together.
	 */
	tv = opbx_tvadd(opbx_tvnow(), opbx_tv(0, 1000));

	runq = con->schedq;
	endq = &runq;
	while (con->schedq && SOONER(con->schedq->when, tv)) {
		endq = &con->schedq->next;
		con->schedq = con->schedq->next;
		con->schedcnt--;
	}
	*endq = NULL;

	opbx_mutex_unlock(&con->lock);

	while ((current = runq)) {
		runq = runq->next;
		x++;

		res = current->callback(current->data);

		if (res) {
		 	/*
			 * If they return non-zero, we should schedule them to be
			 * run again.
			 */
			sched_settime(&current->when, (current->variable ? res : current->resched));
			schedule(con, current);
		} else {
			/* No longer needed, so release it */
		 	sched_release(con, current);
		}
	}

	return x;
}

long opbx_sched_when(struct sched_context *con,int id)
{
	struct sched *s;
	long secs;
#ifdef DEBUG_SCHED
	DEBUG_LOG(opbx_log(OPBX_LOG_DEBUG, "opbx_sched_when()\n"));
#endif
	opbx_mutex_lock(&con->lock);
	s=con->schedq;
	while (s!=NULL) {
		if (s->id==id) break;
		s=s->next;
	}
	secs=-1;
	if (s!=NULL) {
		struct timeval now = opbx_tvnow();
		secs=s->when.tv_sec-now.tv_sec;
	}
	opbx_mutex_unlock(&con->lock);
	return secs;
}
