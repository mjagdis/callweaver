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
#include <netinet/in.h>
#include <arpa/inet.h>

#include "callweaver/lock.h"
#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/module.h"

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
 
#define DEFAULT_MANAGER_PORT 5038	/* Default port for CallWeaver management via TCP */

#define EVENT_FLAG_SYSTEM 		(1 << 0) /* System events such as module load/unload */
#define EVENT_FLAG_CALL			(1 << 1) /* Call event, such as state change, etc */
#define EVENT_FLAG_LOG			(1 << 2) /* Log events */
#define EVENT_FLAG_VERBOSE		(1 << 3) /* Verbose messages */
#define EVENT_FLAG_COMMAND		(1 << 4) /* Ability to read/set commands */
#define EVENT_FLAG_AGENT		(1 << 5) /* Ability to read/set agent info */
#define EVENT_FLAG_USER                 (1 << 6) /* Ability to read/set user info */

/* Manager Helper Function */
typedef int (*manager_hook_t)(int, char *, char *); 

struct manager_custom_hook {
	/*! Identifier */
	char *file;
	/*! helper function */
	manager_hook_t helper;
	struct manager_custom_hook *next;
};

/*! Add a custom hook to be called when an event is fired */
/*! \param hook struct manager_custom_hook object to add
*/
extern void add_manager_hook(struct manager_custom_hook *hook);

/*! Delete a custom hook to be called when an event is fired */
/*! \param hook struct manager_custom_hook object to delete
*/
extern void del_manager_hook(struct manager_custom_hook *hook);

/* Export manager structures */
#define MAX_HEADERS 80
#define MAX_LEN 256

struct eventqent {
	struct eventqent *next;
	char eventdata[1];
};

struct mansession {
	struct cw_object obj;
	struct cw_registry_entry *reg_entry;
	int fd;				/*!< Socket */
	cw_mutex_t __lock;		/*!< Thread lock -- don't use in action callbacks, it's already taken care of  */
	int busy;			/*!< Whether or not we're busy doing an action */
	int dead;			/*!< Whether or not we're "dead" */
	pthread_t t;			/*!< Execution thread */
	struct sockaddr_in sin;		/*!< socket address */
	char *name;
	char username[80];		/*!< Logged in username */
	char challenge[10];		/*!< Authentication challenge */
	int authenticated;		/*!< Authentication status */
	int readperm;			/*!< Authorization for reading */
	int writeperm;			/*!< Authorization for writing */
	char inbuf[MAX_LEN];
	int inlen;
	int send_events;
	struct eventqent *eventq;	/*!< Queued events that we've not had the ability to send yet */
	int writetimeout;		/*!< Timeout for cw_carefulwrite() */
};


struct message {
	int hdrcount;
	char headers[MAX_HEADERS][MAX_LEN];
};

struct manager_action {
	struct cw_object obj;
	struct cw_registry_entry *reg_entry;
	const char *action;		/*!< Name of the action */
	int authority;			/*!< Permission required for action.  EVENT_FLAG_* */
	int (*func)(struct mansession *s, struct message *m); /*!< Function to be called */
	const char *synopsis;		/*!< Short description of the action */
	const char *description;	/*!< Detailed description of the action */
};


extern struct cw_registry manager_session_registry;
extern struct cw_registry manager_action_registry;


#define cw_manager_action_register(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	/* We know 0 refs means not initialized because we know how objs work \
	 * internally and we know that registration only happens while the \
	 * module lock is held. \
	 */ \
	if (!cw_object_refs(__ptr)) \
		cw_object_init_obj(&__ptr->obj, CW_OBJECT_CURRENT_MODULE, CW_OBJECT_NO_REFS); \
	__ptr->reg_entry = cw_registry_add(&manager_action_registry, &__ptr->obj); \
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


/*! External routines may send callweaver manager events this way */
/*! 	\param category	Event category, matches manager authorization
	\param event	Event name
	\param contents	Contents of event
*/ 
extern int manager_event(int category, char *event, char *contents, ...)
	__attribute__ ((format (printf, 3,4)));

/*! Get header from mananger transaction */
extern char *astman_get_header(struct message *m, char *var);

/*! Get a linked list of the Variable: headers */
struct cw_variable *astman_get_variables(struct message *m);

/*! Send error in manager transaction */
extern void astman_send_error(struct mansession *s, struct message *m, char *error);
extern void astman_send_response(struct mansession *s, struct message *m, char *resp, char *msg);
extern void astman_send_ack(struct mansession *s, struct message *m, char *msg);

/*! Called by CallWeaver initialization */
extern int init_manager(void);
/*! Called by CallWeaver initialization */
extern int reload_manager(void);

#endif /* _CALLWEAVER_MANAGER_H */
