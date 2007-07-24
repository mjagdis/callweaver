/* $Id: event.h,v 1.7 2004/05/29 14:26:24 mjt Exp $
 * Timer and I/O Event header
 * Author: Michael Tokarev <mjt@corpit.ru>
 * Licence: LGPL.
 */

#if !defined(_EVENT_H)
#define _EVENT_H

#include <sys/types.h>

struct ev_ct;
struct ev_tm;

/*! Time in milliseconds since 1st Jan 1970 */
typedef long long int ev_time_t;

#define EV_SELECT	0x01
#define EV_POLL		0x02
#define EV_EPOLL	0x04
#define EV_KQUEUE	0x08
#define EV_DEVPOLL	0x10
#define EV_ADVANCED	(EV_EPOLL | EV_KQUEUE | EV_DEVPOLL)

/* Waiting for I/O */
#define EV_IN	0x01
#define EV_PRI	0x02
#define EV_OUT	0x04

typedef void ev_io_cbck_f(struct ev_ct *ct, void *user_data, int revents, int fd);

typedef void ev_tm_cbck_f(struct ev_ct *ct, void *user_data, struct ev_tm *tmr);

struct ev_tm
{
    struct ev_tm *evtm_prev;
    struct ev_tm *evtm_next;
    ev_time_t evtm_when;
    ev_tm_cbck_f *evtm_cbck;
    void *evtm_data;
};

extern ev_time_t ev_now;
extern time_t ev_time;

#if defined(__cplusplus)
extern "C"
{
#endif

/* Get the current time in ev_time_t form */
ev_time_t ev_gettime(void);

/* Find the maximum permitted FD */
int ev_fdlimit(void);

/* Initialise the event handling scheme */
int ev_init(int maxfdhint, int type);

/* Release the event handling scheme */
void ev_free(void);

/* Create a new event handling context. */
struct ev_ct *ev_ct_new(int maxfdhint, int type);

/* Free an event handling context. */
void ev_ct_free(struct ev_ct *ct);

/* Find the name of the event handling method which has been chosen */
const char *ev_method_name(const struct ev_ct *ct);

/* Find the event handling method which has been chosen */
int ev_method(const struct ev_ct *ct);

/* Single shot wait for events */
int ev_wait(struct ev_ct *ct, int timeout);

/* Register an FD with the event handling sceheme */
int ev_io_add(struct ev_ct *ct, int fd, int events, ev_io_cbck_f *cb, void *user_data);

/* Modify the registeration of an FD with the event handling sceheme */
int ev_io_mod(struct ev_ct *ct, int fd, int events, ev_io_cbck_f *cb, void *user_data);

/* Deregister an FD from the event handling sceheme */
int ev_io_del(struct ev_ct *ct, int fd);

/* Report the number of FDs currently registered */
int ev_io_count(const struct ev_ct *ct);

/* Start a new timer, with an absolute timeout. */
struct ev_tm *ev_timer_add(struct ev_ct *ct,
                           struct ev_tm *tmr,
                           ev_time_t when,
                           ev_tm_cbck_f *cbck,
                           void *user_data);

/* Start a new timer, with a relative timeout in ms. */
struct ev_tm *ev_ms_timer_add(struct ev_ct *ct,
                              struct ev_tm *tmr,
                              int ms_timeout,
                              ev_tm_cbck_f *cb,
                              void *user_data);

/* Start a new timer, with a relative timeout in seconds. */
struct ev_tm *ev_sec_timer_add(struct ev_ct *ct,
                               struct ev_tm *tmr,
                               int s_timeout,
                               ev_tm_cbck_f *cb,
                               void *user_data);

/* Stop a timer */
int ev_timer_del(struct ev_ct *ct, struct ev_tm *tmr);

/* Report the number of outstanding timers */
int ev_timer_count(const struct ev_ct *ct);

/* Return a pointer to the first timer in the list */
ev_time_t ev_timer_first(const struct ev_ct *ct);

/* Report the time from now, in milliseconds, until the earliest timer will expire */
int ev_timer_timeout(const struct ev_ct *ct);

#if defined(__cplusplus)
}
#endif

#endif
