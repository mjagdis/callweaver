/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 2006
 * Bartek (eGnarF) Kania
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 * \brief OpenPBX timer implementation
 * This abstracts the timer functionality and allows one to implement them
 * in the best way possible on each system.
 * Currently only a repeating timer is implemented.
 */

#ifdef __linux__
#include <time.h>
#else
#include <sys/time.h>
#endif
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <openpbx/timer.h>
#include <openpbx/logger.h>

/* The sighandler is called when the clock is fired */
static void sighandler(sigval_t sigval)
{
	opbx_timer_t *t = (opbx_timer_t*) sigval.sival_ptr;
       
	/* Run our client's handler function */
	(*t->func)(t, t->user_data);
}

/* A special case of the signal handler that destroys the timer after
 * the signal is processed */
static void sighandler_and_destroyer(sigval_t sigval)
{
	opbx_timer_t *t = (opbx_timer_t*) sigval.sival_ptr;
       
	/* Run our client's handler function */
	(*t->func)(t, t->user_data);

	/* Destroy timer */
	opbx_timer_destroy(t);
}

static void _set_interval(opbx_timer_t *t, unsigned long interval)
{
	struct timespec res;
	int st;
	long sec, nano;

	/* Set the interval */
	if (!interval)
		t->interval = 1000000;  /* default 1s */
	else 
		t->interval = interval;

	/* Check if the timer has a good enough resolution */
	st = clock_getres(CLOCK_MONOTONIC, &res);
	if (st == -1)
		st = clock_getres(CLOCK_REALTIME, &res);
	if (st == -1) {
		opbx_log(LOG_WARNING, "Couldn't get resolution of "
			 "timer. Timer could be unreliable!\n");
	} else {
		/* Calculate the time in seconds and nanoseconds 
		 * since the value we got is in microseconds */
		nano = (t->interval % 1000000) * 1000;
		sec = t->interval / 1000000;
		if (sec < 1 && nano > 0 && nano < res.tv_nsec) {
			opbx_log(LOG_WARNING, "Requested a timer with %ld "
				 "nanosecond interval, but system timer "
				 "reports a resolution of %ld nanosec. "
				 "Timing may be unreliable!\n", nano, 
				 res.tv_nsec);
			
			/* Reset the interval to a sane value */
			t->interval = (res.tv_nsec / 1000) + 1;
		}
	}
}

static int _timer_create(opbx_timer_t *t, opbx_timer_type_t type, 
			 unsigned long interval, opbx_timer_func *func, 
			 void *user_data, void (*hdlr)(sigval_t))
{
	char buf[128];
	struct sigevent evp;

	/* Set timer data */
	memset(t, 0, sizeof(opbx_timer_t));
	t->type = type;
	t->interval = interval;
	t->func = func;
	t->user_data = user_data;
	t->active = 1;

	/* Set the interval */
	_set_interval(t, interval);

	/* Call our handler function when the timer fires */
	memset(&evp, 0, sizeof(evp));
	evp.sigev_notify = SIGEV_THREAD;
	evp.sigev_value.sival_ptr = (void*)t;
	evp.sigev_notify_function = hdlr;
	evp.sigev_notify_attributes = 0;

	/* We REALLY prefer monotonic, but you can't have it all! */
	if (timer_create(CLOCK_MONOTONIC, &evp, &t->timer_id) == -1) {
		opbx_log(LOG_DEBUG, "CLOCK_MONOTONIC didn't work, trying "
			 "CLOCK_REALTIME\n");
		if (timer_create(CLOCK_REALTIME, &evp, &t->timer_id) == -1) {
			opbx_log(LOG_ERROR, "Error creating monotonic timer: "
				 "%s\n", strerror_r(errno, buf, 128));
			return -1;
		}
	}

	return 0;
}

int opbx_repeating_timer_create(opbx_timer_t *t, unsigned long interval, 
				opbx_timer_func *func, void *user_data)
{
	return _timer_create(t, OPBX_TIMER_REPEATING, interval, func, 
			    user_data, sighandler);
}
int opbx_oneshot_timer_create(opbx_timer_t *t, unsigned long interval, 
			      opbx_timer_func *func, void *user_data)
{
	return _timer_create(t, OPBX_TIMER_ONESHOT, interval, func, 
			    user_data, sighandler);
}

void opbx_timer_destroy(opbx_timer_t *t)
{
	if (t->active) {
		timer_delete(t->timer_id);
		opbx_log(LOG_DEBUG, "Destroying timer 0x%lx!\n", 
			 (unsigned long)t);
	} else
		opbx_log(LOG_DEBUG, "Attempted to destroy inactive timer "
			 "0x%lx!\n", (unsigned long)t);
	t->active = 0;
}

int opbx_timer_start(opbx_timer_t *t)
{
	char buf[128];
	struct itimerspec spec;
	long nano, sec;

	/* Calculate the time in seconds and nanoseconds 
	 * since the value we got is in microseconds */
	nano = (t->interval % 1000000) * 1000;
	sec = t->interval / 1000000;

	/* Set up the timing interval for this timer */
	memset(&spec, 0, sizeof(spec));
	spec.it_value.tv_sec = sec;
	spec.it_value.tv_nsec = nano;

	/* Repeating timer needs an interval */
	if (t->type == OPBX_TIMER_REPEATING) {
		spec.it_interval.tv_sec = sec;
		spec.it_interval.tv_nsec = nano;
	}

	opbx_log(LOG_DEBUG, "Timer 0x%lx set to %ld.%ld repeat "
		 "%ld.%ld\n", (unsigned long)t,
		 spec.it_value.tv_sec, spec.it_value.tv_nsec, 
		 spec.it_interval.tv_sec, spec.it_interval.tv_nsec);

	/* Actually start the timer */
	if (timer_settime(t->timer_id, 0, &spec, 0) == -1) {
		opbx_log(LOG_ERROR, "Error starting timer: %s\n",
			 strerror_r(errno, buf, 128));
		return -1;
	}	

	return 0;
}

int opbx_timer_stop(opbx_timer_t *t)
{
	char buf[128];
	struct itimerspec spec;

	/* Stop the timer by resetting it to zero-time */
	memset(&spec, 0, sizeof(spec));

	/* Actually stop the timer */
	if (timer_settime(t->timer_id, 0, &spec, 0) == -1) {
		opbx_log(LOG_ERROR, "Error stopping timer: %s\n",
			 strerror_r(errno, buf, 128));
		return -1;
	}	
	return 0;
}

int opbx_simple_timer(opbx_timer_t *t, unsigned long interval,
		      opbx_timer_func *func, void *user_data)
{
	int res;

	/* Don't bother creating timer for 0-length interval */
	if (interval == 0) {
		opbx_log(LOG_DEBUG, "Not creating 0-interval timer\n");
		return -1;
	}

	res = _timer_create(t, OPBX_TIMER_ONESHOT, interval, func, user_data,
			   sighandler_and_destroyer);
	if (!res)
		res = opbx_timer_start(t);

	return res;
}

int opbx_timer_newtime(opbx_timer_t *t, unsigned long interval)
{
	_set_interval(t, interval);
	return opbx_timer_start(t);
}
