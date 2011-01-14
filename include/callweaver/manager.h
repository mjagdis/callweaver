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
 * \brief AMI - CallWeaver Management Interface
 * External call management support 
 */

#ifndef _CALLWEAVER_MANAGER_H
#define _CALLWEAVER_MANAGER_H

#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "callweaver/lock.h"
#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/module.h"
#include "callweaver/dynstr.h"
#include "callweaver/logger.h"
#include "callweaver/preprocessor.h"
#include "callweaver/connection.h"


/*!
  \file manager.h
  \brief The AMI - CallWeaver Manager Interface - is a TCP protocol created to 
	 manage CallWeaver with third-party software.

 Manager protocol packages are text fields of the form a: b.  There is
 always exactly one space after the colon.
 
 The first header type is the "Event" header.  Other headers vary from
 event to event.  Headers end with standard \r\n termination.
 The last line of the manager response or event is an empty line.
 (\r\n)
 
 ** Please try to re-use existing headers to simplify manager message parsing in clients.
    Don't re-use an existing header with a new meaning, please.
    You can find a reference of standard headers in
    doc/manager.txt
 
 */


struct cw_manager_message {
	struct cw_object obj;
	struct cw_dynstr ds;		/*!< The AMI formatted event data */
	size_t count;			/*!< The number of key/value pairs in this event */
	int map[0];			/*!< Offsets to the start of key and value strings in the msg data */
};


struct message {
	char *actionid;
	int hdrcount;
	struct {
		char *key;
		char *val;
	} header[80];
};


struct mansession {
	struct cw_object obj;
	struct cw_registry_entry *reg_entry;
	int authenticated;		/*!< Authentication status */
	int readperm;			/*!< Authorization for reading messages _from_ the manager */
	int writeperm;			/*!< Authorization for writing messages _to_ the manager */
	int send_events;
	int fd;
	pthread_mutex_t lock;
	pthread_cond_t activity;
	pthread_cond_t ack;
	struct message *m;
	int (*handler)(struct mansession *, const struct cw_manager_message *);
	struct cw_object *pvt_obj;
	unsigned int q_size, q_r, q_w, q_count, q_max, q_overflow;
	struct cw_manager_message **q;
	pthread_t reader_tid;
	pthread_t writer_tid;
	char username[80];		/*!< Logged in username */
	char challenge[10];		/*!< Authentication challenge */
	/* This must be last */
	struct sockaddr addr;
};


struct manager_action {
	struct cw_object obj;
	struct cw_registry_entry *reg_entry;
	const char *action;		/*!< Name of the action */
	int authority;			/*!< Permission required for action.  EVENT_FLAG_* */
	struct cw_manager_message *(*func)(struct mansession *sess, const struct message *req); /*!< Function to be called */
	const char *synopsis;		/*!< Short description of the action */
	const char *description;	/*!< Detailed description of the action */
};


extern struct cw_registry manager_session_registry;

extern CW_API_PUBLIC struct cw_registry manager_action_registry;


#define cw_manager_action_register(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	/* We know 0 refs means not initialized because we know how objs work \
	 * internally and we know that registration only happens while the \
	 * module lock is held. \
	 */ \
	if (!cw_object_refs(__ptr)) \
		cw_object_init_obj(&__ptr->obj, CW_OBJECT_CURRENT_MODULE, 0); \
	__ptr->reg_entry = cw_registry_add(&manager_action_registry, 0, &__ptr->obj); \
	0; \
})
#define cw_manager_action_unregister(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	if (__ptr->reg_entry) \
		cw_registry_del(&manager_action_registry, __ptr->reg_entry); \
	0; \
})


#define cw_manager_action_register_multiple(array, count) do { \
	const typeof(&(array)[0]) __aptr = &(array)[0]; \
	int i, n = (count); \
	for (i = 0; i < n; i++) \
		cw_manager_action_register(&__aptr[i]); \
} while (0)

#define cw_manager_action_unregister_multiple(array, count) do { \
	const typeof(&(array)[0]) __aptr = &(array)[0]; \
	int i, n = (count); \
	for (i = 0; i < n; i++) \
		cw_manager_action_unregister(&__aptr[i]); \
} while (0)


extern CW_API_PUBLIC int manager_str_to_eventmask(char *events);

/*! Get header from mananger transaction */
extern CW_API_PUBLIC char *cw_manager_msg_header(const struct message *msg, const char *key);


/*! \brief send a message to a manager session
 *
 * \param sess     the manager session to send to
 * \param req      the request being responded to
 * \param resp_p   an indirect reference to the message to be sent
 *
 * The message is a referenced counted object. In all cases cw_manager_send
 * consumes a reference and sets the given pointer to NULL.
 *
 * \return 0 on success, 1 on queue overflow, -1 if the message was flagged as in error
 */
extern CW_API_PUBLIC int cw_manager_send(struct mansession *sess, const struct message *req, struct cw_manager_message **resp_p);

extern CW_API_PUBLIC struct cw_manager_message *cw_manager_response(const char *resp, const char *msgstr);

extern CW_API_PUBLIC struct mansession *manager_session_start(int (* const handler)(struct mansession *, const struct cw_manager_message *), int fd, const struct sockaddr *addr, socklen_t addrlen, struct cw_object *pvt_obj, int readperm, int writeperm, int send_events);
extern CW_API_PUBLIC void manager_session_shutdown(struct mansession *sess);
extern CW_API_PUBLIC void manager_session_end(struct mansession *sess);

extern int manager_session_ami(struct mansession *sess, const struct cw_manager_message *event);

/*! Reload manager configuration */
extern int manager_reload(void);

/*! Called by CallWeaver initialization */
extern int init_manager(void);


/* If you are looking at this trying to fix a weird compile error
 * check the count is a constant integer corresponding to the
 * number of cw_msg_tuple() arguments, that there are _only_
 * cw_msg_tuple() arguments after the count (there can be no
 * expressions wrapping cw_msg_tuple to select one or the other
 * for instance) and that you are not missing a comma after a
 * cw_msg_tuple().
 * In particular note:
 *
 *    this is legal
 *        if (...)
 *            cw_manager_event(...,
 *                cw_msg_tuple(...),
 *                ...
 *            );
 *        else
 *            cw_manager_event(...,
 *                cw_msg_tuple(...),
 *                ...
 *            );
 *
 *    but this is not
 *        cw_manager_event(...,
 *            (... ? cw_msg_tuple(...) : cw_msg_tuple(...)),
 *            ...
 *        );
 *
 *    although this is
 *        cw_manager_event(...,
 *            cw_msg_tuple(..., (... ? a : b)),
 *            ...
 *        );
 */

/*! \brief create a callweaver manager message
 *      \note The actual parameters passed are hidden by the wrapping macro below
 *      \param msg_p		An indirect reference to the msg to be created or extended
 *      \param count		Number of key-value tuples
 *      \param map		Map of offsets for keys and values
 *      \param fmt		printf style format string
 */
extern CW_API_PUBLIC int cw_manager_msg_func(struct cw_manager_message **msg_p, size_t count, int map[], const char *fmt, ...)
	__attribute__ ((format (printf, 4,5)));

/*! \brief send a callweaver manager event
 *      \note The actual parameters passed are hidden by the wrapping macro
 *      \param category	Event category, matches manager authorization
 *      \param count		Number of key-value tuples
 *      \param map		Map of offsets for keys and values
 *      \param fmt		printf style format string
 */
extern CW_API_PUBLIC void cw_manager_event_func(int category, size_t count, int map[], const char *fmt, ...)
	__attribute__ ((format (printf, 4,5)));


#ifndef CW_DEBUG_COMPILE

/* These are deliberately empty. They only exist to allow compile time
 * syntax checking of _almost_ the actual code rather than the preprocessor
 * expansion. They will be optimized out.
 * Note that we only get _almost_ there. Specifically there is no way to
 * stop the preprocessor eating line breaks so you way get told arg 3
 * doesn't match the format string, but not which cw_msg_tuple in the
 * manager_event is talking about. If you can't spot it try compiling
 * with CW_DEBUG_COMPILE defined. This breaks expansion completely
 * so you get accurate line numbers for errors and warnings but then
 * the compiled code will have references to non-existent functions.
 */
static __inline__ int cw_manager_msg(struct cw_manager_message **msg, size_t count, ...)
	__attribute__ ((always_inline, const, unused, no_instrument_function, nonnull (1)));
static __inline__ int cw_manager_msg(struct cw_manager_message **msg __attribute__((unused)), size_t count __attribute__((unused)), ...)
{
	return 0;
}
static __inline__ void cw_manager_event(int category, const char *event, size_t count, ...)
	__attribute__ ((always_inline, const, unused, no_instrument_function, nonnull (2)));
static __inline__ void cw_manager_event(int category __attribute__((unused)), const char *event __attribute__((unused)), size_t count __attribute__((unused)), ...)
{
}
static __inline__ char *cw_msg_tuple(const char *key, const char *fmt, ...)
	__attribute__ ((always_inline, const, unused, no_instrument_function, nonnull (1,2), format (printf, 2,3)));
static __inline__ char *cw_msg_tuple(const char *key __attribute__((unused)), const char *fmt __attribute__((unused)), ...)
{
	return NULL;
}
static __inline__ char *cw_msg_vtuple(const char *key, const char *fmt, ...)
	__attribute__ ((always_inline, const, unused, no_instrument_function, nonnull (1,2), format (printf, 2,3)));
static __inline__ char *cw_msg_vtuple(const char *key __attribute__((unused)), const char *fmt __attribute__((unused)), ...)
{
	return NULL;
}

#  define CW_ME_DEBRACKET_cw_msg_tuple(key, fmt, ...)	key, fmt, ## __VA_ARGS__
#  define CW_ME_DEBRACKET_cw_msg_vtuple(key, fmt, ...)	key, fmt, ## __VA_ARGS__
#  define CW_ME_DO(op, ...)				op(__VA_ARGS__)
#  define CW_ME_FMT(n, a)				CW_ME_DO(CW_ME_FMT_I, n, CW_CPP_CAT(CW_ME_DEBRACKET_, a))
#  define CW_ME_FMT_I(n, key, fmt, ...)			"%s: %n" fmt "\r\n%n"
#  define CW_ME_ARGS(n, a)				CW_ME_DO(CW_ME_ARGS_I, n, CW_CPP_CAT(CW_ME_DEBRACKET_, a))
#  define CW_ME_ARGS_I(n, key, fmt, ...)		key, &map[(n << 1) + 1], ## __VA_ARGS__, &map[(n << 1) + 2],

#  define cw_manager_msg(msg_p, count, ...) ({ \
	(void)cw_manager_msg(msg_p, count, \
		__VA_ARGS__ \
	); \
	int map[(count << 1) + 1] = { 0 }; \
	cw_manager_msg_func(msg_p, count, map, \
		CW_CPP_CAT(CW_CPP_ITERATE_, count)(0, CW_ME_FMT, __VA_ARGS__) "%s", \
		CW_CPP_CAT(CW_CPP_ITERATE_, count)(0, CW_ME_ARGS, __VA_ARGS__) \
		"\r\n" \
	); \
   })

#  define cw_manager_event(category, event, count, ...) ({ \
	(void)cw_manager_event(category, event, count, \
		__VA_ARGS__ \
	); \
	int map[((count + 1) << 1) + 1] = { 0 }; \
	cw_manager_event_func(category, count + 1, map, \
		CW_ME_FMT_I(0, "Event", "%s", event) \
		CW_CPP_CAT(CW_CPP_ITERATE_, count)(1, CW_ME_FMT, __VA_ARGS__) "%s", \
		CW_ME_ARGS_I(0, "Event", "%s", event) \
		CW_CPP_CAT(CW_CPP_ITERATE_, count)(1, CW_ME_ARGS, __VA_ARGS__) \
		"\r\n" \
	); \
   })

#else

/* N.B. Under CW_DEBUG_COMPILE we aren't expected to actually link
 * (or even generate a .o for that matter) so these don't exist anywhere.
 */
extern int cw_manager_msg(struct cw_manager_message **msg, size_t count, ...)
	__attribute__ ((nonnull (1)));
extern void cw_manager_event(int category, const char *event, size_t count, ...)
	__attribute__ ((nonnull (2)));

extern char *cw_msg_tuple(const char *key, const char *fmt, ...)
	__attribute__ ((nonnull (1,2), format (printf, 2,3)));
extern char *cw_msg_vtuple(const char *key, const char *fmt, ...)
	__attribute__ ((nonnull (1,2), format (printf, 2,3)));

#endif


#endif /* _CALLWEAVER_MANAGER_H */
