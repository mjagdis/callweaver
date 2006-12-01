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

#if defined(__linux__) || defined(__FreeBSD__)
#include <time.h>
#else
#include <sys/time.h>
#endif

#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <openpbx/timer.h>
#include <openpbx/logger.h>

#ifdef __FreeBSD__
#ifdef HAVE_POSIX_TIMERS
typedef union sigval sigval_t;
#endif /* HAVE_POSIX_TIMERS */
#endif /* __FreeBSD__ */


#ifdef USE_GENERIC_TIMERS
void * _timer_thread(void *parg);
#endif /* USE_GENERIC_TIMERS */


#ifdef HAVE_POSIX_TIMERS
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
#endif /* HAVE_POSIX_TIMERS */

static void _set_interval(opbx_timer_t *t, unsigned long interval)
{
	struct timespec res;
	int st;
	long sec, nano;

	/* Set the interval */
	if (!interval)
#ifdef HAVE_POSIX_TIMERS
		t->interval = 1000000;  /* default 1s */
	else 
		t->interval = interval;
#endif /* HAVE_POSIX_TIMERS */

#if defined(USE_GENERIC_TIMERS) & !defined(HAVE_POSIX_TIMERS)
	interval = 1000;  /* default 1ms */

	struct __opbx_timer_t *ot = (struct __opbx_timer_t *) t->impl_data;
	
	ot->ts.tv_nsec = (interval % 1000000) * 1000;
	ot->ts.tv_sec = interval / 1000000;
#endif /* USE_GENERIC_TIMERS */

	/* Check if the timer has a good enough resolution */
#ifdef HAVE_CLOCK_GETRES
	st = clock_getres(CLOCK_MONOTONIC, &res);
	if (st == -1)
		st = clock_getres(CLOCK_REALTIME, &res);
	if (st == -1) {
		opbx_log(LOG_WARNING, "Couldn't get resolution of "
			 "timer. Timer could be unreliable!\n");
	} else {
#endif // HAVE_CLOCK_GETRES
		/* Calculate the time in seconds and nanoseconds 
		 * since the value we got is in microseconds */
#ifdef HAVE_POSIX_TIMERS
		nano = (t->interval % 1000000) * 1000;
		sec = t->interval / 1000000;
#endif /* HAVE_POSIX_TIMERS */

#if defined(USE_GENERIC_TIMERS) & !defined(HAVE_POSIX_TIMERS)
		nano = (interval % 1000000) * 1000;
		sec = interval / 1000000;
#endif /* USE_GENERIC_TIMERS */
		if (sec < 1 && nano > 0 && nano < res.tv_nsec) {
			static long complained = 1000000000L;
			if (nano < complained) {
				complained = nano;
				opbx_log(LOG_WARNING, "Requested a timer with %ld "
					 "nanosecond interval, but system timer "
					 "reports a resolution of %ld nanosec. "
					 "Timing may be unreliable!\n", nano, 
					 res.tv_nsec);
			}
			
			/* Reset the interval to a sane value */
#ifdef HAVE_POSIX_TIMERS
			t->interval = (res.tv_nsec / 1000) + 1;
#endif /* HAVE_POSIX_TIMERS */

#if defined(USE_GENERIC_TIMERS) & !defined(HAVE_POSIX_TIMERS)
		    interval = (res.tv_nsec / 1000) + 1;
		    ot->ts.tv_nsec = (interval % 1000000) * 1000;
		    ot->ts.tv_sec = interval / 1000000;
#endif /* USE_GENERIC_TIMERS */
		}

#ifdef HAVE_CLOCK_GETRES
	}
#endif // HAVE_CLOCK_GETRES
}

#ifdef HAVE_POSIX_TIMERS
static int _timer_create(opbx_timer_t *t, opbx_timer_type_t type, 
			 unsigned long interval, opbx_timer_func *func, 
			 void *user_data, void (*hdlr)(sigval_t))
#endif /* HAVE_POSIX_TIMERS */
#if defined(USE_GENERIC_TIMERS) & !defined(HAVE_POSIX_TIMERS)
static int _timer_create(opbx_timer_t *t, opbx_timer_type_t type, 
			 unsigned long interval, opbx_timer_func *func, 
			 void *user_data)
#endif /* USE_GENERIC_TIMERS*/
{
#ifdef HAVE_POSIX_TIMERS
	char buf[128];
	struct sigevent evp;
#endif /* HAVE_POSIX_TIMERS */

	/* Set timer data */
	memset(t, 0, sizeof(opbx_timer_t));

#ifdef HAVE_POSIX_TIMERS
	t->type = type;
	t->interval = interval;
	t->active = 1;
#endif /* HAVE_POSIX_TIMERS */
	t->func = func;
	t->user_data = user_data;
#if defined(USE_GENERIC_TIMERS) & !defined(HAVE_POSIX_TIMERS)
	/* Set impl timer data */
	t->impl_data = malloc (sizeof(struct __opbx_timer_t));
	memset(t->impl_data, 0, sizeof(struct __opbx_timer_t));

	struct __opbx_timer_t *ot = (struct __opbx_timer_t *)t->impl_data;

	ot->type = type;
	ot->active = 0;
#endif /* USE_GENERIC_TIMERS */

	/* Set the interval */
	_set_interval(t, interval);

#ifdef HAVE_POSIX_TIMERS
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
#if defined(HAVE_STRERROR_R)
			opbx_log(LOG_ERROR, "Error creating monotonic timer: "
				 "%s\n", strerror_r(errno, buf, 128));
#else
			opbx_log(LOG_ERROR, "Error creating monotonic timer"
				 "\n");
#endif
			return -1;
		}
	}
#endif /* HAVE_POSIX_TIMERS */

#ifdef TIMER_DEBUG
	opbx_log(LOG_DEBUG, "Created timer 0x%lx\n", 
		 (unsigned long)t);
#endif		 
	return 0;
}

int opbx_repeating_timer_create(opbx_timer_t *t, unsigned long interval, 
				opbx_timer_func *func, void *user_data)
{
#ifdef HAVE_POSIX_TIMERS
	return _timer_create(t, OPBX_TIMER_REPEATING, interval, func, 
			    user_data, sighandler);
#endif /* HAVE_POSIX_TIMERS */
#ifdef USE_GENERIC_TIMERS
	return _timer_create(t, OPBX_TIMER_REPEATING, interval, func, user_data);
#endif /* USE_GENERIC_TIMERS */
}
int opbx_oneshot_timer_create(opbx_timer_t *t, unsigned long interval, 
			      opbx_timer_func *func, void *user_data)
{
#ifdef HAVE_POSIX_TIMERS
	return _timer_create(t, OPBX_TIMER_ONESHOT, interval, func, 
			    user_data, sighandler);
#endif /* HAVE_POSIX_TIMERS */
#ifdef USE_GENERIC_TIMERS
	return _timer_create(t, OPBX_TIMER_ONESHOT, interval, func, user_data);
#endif /* USE_GENERIC_TIMERS */
}

void opbx_timer_destroy(opbx_timer_t *t)
{
#ifdef HAVE_POSIX_TIMERS
	if (t->active && t->timer_id) {
	    timer_delete(t->timer_id);
#endif /* HAVE_POSIX_TIMERS */

#ifdef USE_GENERIC_TIMERS
	struct __opbx_timer_t *ot = (struct __opbx_timer_t *)t->impl_data;
	
	if(ot->active) {
	    ot->active = 0;
	    pthread_cancel(ot->opbx_timer_thread);
	    free(t->impl_data);
	    t->impl_data = NULL;
#endif /* USE_GENERIC_TIMERS */
		opbx_log(LOG_DEBUG, "Destroying timer 0x%lx!\n", 
			 (unsigned long)t);
	} else
		opbx_log(LOG_DEBUG, "Attempted to destroy inactive timer "
			 "0x%lx!\n", (unsigned long)t);
#ifdef HAVE_POSIX_TIMERS
	t->active = 0;
#endif /* HAVE_POSIX_TIMERS */
}


int opbx_timer_start(opbx_timer_t *t)
{
#ifdef HAVE_POSIX_TIMERS
	char buf[128];
	struct itimerspec spec;
	long nano, sec;
	
	if (!t->timer_id) {
#endif /* HAVE_POSIX_TIMERS */

#ifdef USE_GENERIC_TIMERS
	if(!t->impl_data) {
#endif /* USE_GENERIC_TIMERS */
		opbx_log(LOG_ERROR, "Attempted to start nonexistent timer!\n");
		return -1;
	}
	
#ifdef HAVE_POSIX_TIMERS
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

#ifdef TIMER_DEBUG
	opbx_log(LOG_DEBUG, "Timer 0x%lx set to %ld.%ld repeat "
		 "%ld.%ld\n", (unsigned long)t,
		 spec.it_value.tv_sec, spec.it_value.tv_nsec, 
		 spec.it_interval.tv_sec, spec.it_interval.tv_nsec);
#endif
	/* Actually start the timer */
	if (timer_settime(t->timer_id, 0, &spec, 0) == -1) {
#if defined(HAVE_STRERROR_R)
		opbx_log(LOG_ERROR, "Error starting timer: %s\n",
			 strerror_r(errno, buf, 128));
#else
		opbx_log(LOG_ERROR, "Error starting timer\n");
#endif
		return -1;
	}	
#endif /* HAVE_POSIX_TIMERS */

#ifdef USE_GENERIC_TIMERS
	int ret;
	pthread_attr_t attr;
	struct __opbx_timer_t *ot = (struct __opbx_timer_t *) t->impl_data;
	
	/* Set timer active */
	ot->active = 1;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	
	if ((ret = pthread_create(&ot->opbx_timer_thread, &attr, _timer_thread, t))) {
	    if(ot->type == OPBX_TIMER_REPEATING)
		opbx_log(LOG_WARNING, "Failed to create thread for OPBX_TIMER_REPEATING: %d!\n", ret);
	    else if(ot->type == OPBX_TIMER_ONESHOT)
		opbx_log(LOG_WARNING, "Failed to create thread for OPBX_TIMER_ONESHOT: %d!\n", ret);
	    else 
		opbx_log(LOG_WARNING, "Failed to create thread for OPBX_TIMER_SIMPLE: %d!\n", ret);
		
	    free(t->impl_data);
	    t->impl_data = NULL;
	    return -1;
	}
#ifdef TIMER_DEBUG	
	if(ot->type == OPBX_TIMER_REPEATING) {
    	    opbx_log(LOG_DEBUG, "Timer 0x%lx set to %ld.%ld repeat "
        	"%ld.%ld\n", (unsigned long)t,
        	(long int)ot->ts.tv_sec, ot->ts.tv_nsec,
        	(long int)ot->ts.tv_sec, ot->ts.tv_nsec);
	} else {
	     opbx_log(LOG_DEBUG, "Timer 0x%lx set to %ld.%ld repeat "
        	"0.0\n", (unsigned long)t,
        	(long int)ot->ts.tv_sec, ot->ts.tv_nsec);
	}
#endif /* TIMER_DEBUG */
#endif /* USE_GENERIC_TIMERS */
	return 0;
}

int opbx_timer_stop(opbx_timer_t *t)
{
#ifdef HAVE_POSIX_TIMERS
	char buf[128];
	struct itimerspec spec;

	/* Stop the timer by resetting it to zero-time */
	memset(&spec, 0, sizeof(spec));

	/* Actually stop the timer */
	if (timer_settime(t->timer_id, 0, &spec, 0) == -1) {
#if defined(HAVE_STRERROR_R)
		opbx_log(LOG_ERROR, "Error stopping timer: %s\n",
			 strerror_r(errno, buf, 128));
#else
		opbx_log(LOG_ERROR, "Error stopping timer\n");
#endif
		return -1;
	}
#endif /* HAVE_POSIX_TIMERS */
#ifdef USE_GENERIC_TIMERS
	struct __opbx_timer_t *ot = (struct __opbx_timer_t *) t->impl_data;

#ifdef TIMER_DEBUG	
	opbx_log(LOG_DEBUG, "Timer 0x%lx set to inactive\n", 
		 (unsigned long)t);
#endif /* TIMER_DEBUG */
	/* Disable timer */
	ot->active = 0;
#endif /* USE_GENERIC_TIMERS */
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
#ifdef HAVE_POSIX_TIMERS
	res = _timer_create(t, OPBX_TIMER_ONESHOT, interval, func, user_data,
			   sighandler_and_destroyer);
#endif /* HAVE_POSIX_TIMERS */

#ifdef USE_GENERIC_TIMERS
	res = _timer_create(t, OPBX_TIMER_SIMPLE, interval, func, user_data);
#endif /* USE_GENERIC_TIMERS */
	if (!res)
		res = opbx_timer_start(t);

	return res;
}

int opbx_timer_newtime(opbx_timer_t *t, unsigned long interval)
{
	_set_interval(t, interval);
	return opbx_timer_start(t);
}

#ifdef USE_GENERIC_TIMERS
void * _timer_thread(void *parg) 
{
	opbx_timer_t *t = (opbx_timer_t *) parg;
	struct __opbx_timer_t *ot = (struct __opbx_timer_t *)t->impl_data;
	//pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	if(ot->type == OPBX_TIMER_ONESHOT || ot->type == OPBX_TIMER_SIMPLE ) {	    
	    if (nanosleep(&ot->ts,&ot->ts)) {
		if(errno != EINTR )
		    opbx_log(LOG_WARNING, "Requested a timer with %ld "
			 "nanosecond interval, but system timer "
			 "couldn't handled!\n"
			 "Timing may be unreliable!\n", 
			 ot->ts.tv_nsec);
			 
		return 0;
	    }
	    
	    if(t->func)
		t->func(t, t->user_data);
	    
	    if(ot->type == OPBX_TIMER_SIMPLE) {
		ot->active = 0;
		free(t->impl_data);
		t->impl_data = NULL;
	    }
#ifdef TIMER_DEBUG	    
	    opbx_log(LOG_DEBUG, "** Timer 0x%lx (%d) took %ld.%ld \n",
					 (unsigned long)t, 
					 ot->type, 
					 (long int)ot->ts.tv_sec, ot->ts.tv_nsec);
#endif /* TIMER_DEBUG */
	    return 0;
	    
	} else {
	    while(1) {
		pthread_testcancel();
		if(ot->active) {
			if(nanosleep(&ot->ts, &ot->ts)) {
			    if(errno != EINTR )
				opbx_log(LOG_WARNING, "Requested a timer with %ld "
				    "nanosecond interval, but system timer "
				    "couldn't handled!\n"
				    "Timing may be unreliable!\n", 
				     ot->ts.tv_nsec);
			    return 0;
			}
			if (t->func) {
				t->func(t, t->user_data);
			}
#ifdef TIMER_DEBUG
			opbx_log(LOG_DEBUG, "** Timer 0x%lx with type %d took %ld.%ld \n",
					 (unsigned long)t, 
					 ot->type, 
					 (long int)ot->ts.tv_sec, ot->ts.tv_nsec);
#endif /* TIMER_DEBUG */

		} else {
#ifdef __Darwin__
			pthread_yield_np();
#else
			pthread_yield();
#endif
		}
	    }
	}
	return 0;
}
#endif /* USE_GENERIC_TIMERS */

