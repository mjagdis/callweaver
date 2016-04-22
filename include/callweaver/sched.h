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
 * \brief Scheduler Routines
 */

#ifndef _CALLWEAVER_SCHED_H
#define _CALLWEAVER_SCHED_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! Max num of schedule structs */
/*!
 * The max number of schedule structs to keep around
 * for use.  Undefine to disable schedule structure
 * caching. (Only disable this on very low memory 
 * machines)
 */
#define SCHED_MAX_CACHE 128

struct sched_context;

/*! New schedule context
 *
 * \param nthreads number of service threads to start
 * Create a scheduling context
 * Returns a malloc'd sched_context structure, NULL on failure
 */
extern CW_API_PUBLIC struct sched_context *sched_context_create(int nthreads);

/*! Destroys a schedule context
 *
 * \param c Context to free
 * Destroys (free's) the given sched_context structure
 * Returns 0 on success, -1 on failure
 */
extern CW_API_PUBLIC void sched_context_destroy(struct sched_context *c);


/*! Callback for a scheduler
 *
 * A scheduler callback takes a pointer with callback data and
 * returns a 0 if it should not be run again, or non-zero if it should be
 * rescheduled to run again
 */
typedef int (*cw_sched_cb)(void *data);
#define CW_SCHED_CB(a) ((cw_sched_cb)(a))

/*! Callback for a reschedule failure
 *
 * A reschdule failure callback takes a pointer to the same callback
 * data as the scheduled callback and is called if the scheduled
 * callback returns a reschedule interval but the reschedule fails.
 */
typedef void (*cw_schedfail_cb)(void *data);


/*! State describing a scheduled job.
 *
 * Note that the contents of thise need to be public so the size is
 * known but you are NOT allowed to access the contents outside of
 * the schedule context's lock - which is not public. i.e. don't look,
 * don't touch, just use the functions below.
 */
struct sched_state {
	/* These are internal and should not be accessed other than by the scheduler */
	struct sched_state *next;	/* Next event in the list */
	cw_sched_cb callback;		/* Callback */
	void *data; 			/* Data */
	int vers;			/* Internal version counter */
	cw_schedfail_cb failed;		/* Reschedule failure callback */
	struct timespec when;		/* Absolute time event should take place */
};


/*! Initialize a sched_state */
static inline void cw_sched_state_init(struct sched_state *state)
{
	state->callback = NULL;
}


/*! Test whether a sched_state is currently scheduled
 *
 * N.B. The return is not guaranteed - the job may fire and thus become
 * unscheduled between checking the state and acting on the return!
 */
static inline int cw_sched_state_scheduled(struct sched_state *state)
{
	return state->callback != NULL;
}


/*! Adds a scheduled event
 *
 * \param con      scheduler context
 * \param state    the state this scheduled job is to be associated with
 * \param ms       how many milliseconds to wait for event to occur
 * \param callback function to call when the amount of time expires
 * \param data     data to pass to the callback
 * \param variable if true, the result value of callback function will be used for rescheduling
 *
 * Schedule an event to take place at some point in the future. The callback
 * will be called with data as the argument, ms milliseconds in the
 * future (approximately)
 * If callback returns 0, no further events will be re-scheduled
 */
extern CW_API_PUBLIC int cw_sched_add_variable(struct sched_context *con, struct sched_state *state, int ms, cw_sched_cb callback, void *data, cw_schedfail_cb failed);
#define cw_sched_add(con, state, ms, callback, data) cw_sched_add_variable(con, state, ms, callback, data, NULL)

/*! Deletes a scheduled event
 *
 * \param con   scheduler context
 * \param state where the state of this scheduled job is stored
 *
 * Remove this scheduled job from the queue of scheduled jobs so
 * that it will not be run.
 *
 * Returns 0 if the scheduled job could be deleted, -1 if it was not found
 */
extern CW_API_PUBLIC int cw_sched_del(struct sched_context *con, struct sched_state *state);

/*! Atomically modifies a scheduled job or adds it if it was not already scheduled
 *
 * \param con      scheduler context
 * \param state    where the state of this scheduled job is stored
 * \param ms       how many milliseconds to wait for event to occur
 * \param callback function to call when the amount of time expires
 * \param data     data to pass to the callback
 * \param variable if true, the result value of callback function will be used for rescheduling
 *
 * Returns 0 if the scheduled job was modified, -1 if it was added
 */
extern CW_API_PUBLIC int cw_sched_modify_variable(struct sched_context *con, struct sched_state *state, int ms, cw_sched_cb callback, void *data, cw_schedfail_cb failed);
#define cw_sched_modify(con, state, ms, callback, data) cw_sched_modify_variable(con, state, ms, callback, data, NULL)

/*! Returns the number of seconds before an event takes place
 *
 * \param con      scheduler context
 * \param state    where the state of this scheduled job is stored
 */
extern CW_API_PUBLIC long cw_sched_when(struct sched_context *con, struct sched_state *state);


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_SCHED_H */
