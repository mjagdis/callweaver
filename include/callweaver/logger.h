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

/*!
  \file logger.h
  \brief Support for logging to various files, console and syslog
	Configuration in file logger.conf
*/

#ifndef _CALLWEAVER_LOGGER_H
#define _CALLWEAVER_LOGGER_H

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdarg.h>


#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif


#define CW_EVENT_NUM_ERROR		0
#define CW_EVENT_NUM_WARNING		1
#define CW_EVENT_NUM_NOTICE		2
#define CW_EVENT_NUM_VERBOSE		3
#define CW_EVENT_NUM_EVENT		4
#define CW_EVENT_NUM_DTMF		5
#define CW_EVENT_NUM_DEBUG		6

#define CW_EVENT_NUM_SYSTEM 		7 	/* System events such as module load/unload */
#define CW_EVENT_NUM_CALL		8 	/* Call event, such as state change, etc */
#define CW_EVENT_NUM_COMMAND		9 	/* Ability to read/set commands */
#define CW_EVENT_NUM_AGENT		10 	/* Ability to read/set agent info */
#define CW_EVENT_NUM_USER		11 	/* Ability to read/set user info */


#define EVENT_FLAG_SYSTEM 		(1 << CW_EVENT_NUM_SYSTEM) 	/* System events such as module load/unload */
#define EVENT_FLAG_CALL			(1 << CW_EVENT_NUM_CALL) 	/* Call event, such as state change, etc */
#define EVENT_FLAG_COMMAND		(1 << CW_EVENT_NUM_COMMAND) 	/* Ability to read/set commands */
#define EVENT_FLAG_AGENT		(1 << CW_EVENT_NUM_AGENT) 	/* Ability to read/set agent info */
#define EVENT_FLAG_USER			(1 << CW_EVENT_NUM_USER) 	/* Ability to read/set user info */

#define EVENT_FLAG_ERROR		(1 << CW_EVENT_NUM_ERROR)
#define EVENT_FLAG_WARNING		(1 << CW_EVENT_NUM_WARNING)
#define EVENT_FLAG_NOTICE		(1 << CW_EVENT_NUM_NOTICE)
#define EVENT_FLAG_VERBOSE		(1 << CW_EVENT_NUM_VERBOSE)
#define EVENT_FLAG_EVENT		(1 << CW_EVENT_NUM_EVENT)
#define EVENT_FLAG_DTMF			(1 << CW_EVENT_NUM_DTMF)
#define EVENT_FLAG_DEBUG		(1 << CW_EVENT_NUM_DEBUG)

#define EVENT_FLAG_LOG_ALL		(EVENT_FLAG_ERROR | EVENT_FLAG_WARNING | EVENT_FLAG_NOTICE | EVENT_FLAG_VERBOSE | EVENT_FLAG_EVENT | EVENT_FLAG_DTMF | EVENT_FLAG_DEBUG)


typedef enum {
	__CW_LOG_ERROR   = CW_EVENT_NUM_ERROR,
	__CW_LOG_WARNING = CW_EVENT_NUM_WARNING,
	__CW_LOG_NOTICE  = CW_EVENT_NUM_NOTICE,
	__CW_LOG_VERBOSE = CW_EVENT_NUM_VERBOSE,
	__CW_LOG_EVENT   = CW_EVENT_NUM_EVENT,
	__CW_LOG_DTMF    = CW_EVENT_NUM_DTMF,
	__CW_LOG_DEBUG   = CW_EVENT_NUM_DEBUG,
} cw_log_level;

#define EVENTLOG "event_log"

#define DEBUG_M(a) { \
	a; \
}

/*! Used for sending a log message */
/*!
	\brief This is the standard logger function.  Probably the only way you will invoke it would be something like this:
	cw_log(CW_LOG_WHATEVER, "Problem with the %s Captain.  We should get some more.  Will %d be enough?\n", "flux capacitor", 10);
	where WHATEVER is one of ERROR, DEBUG, EVENT, NOTICE, or WARNING depending
	on which log you wish to output to. These are implemented as macros, that
	will provide the function with the needed arguments.

 	\param level	Type of log event
	\param file	Will be provided by the LOG_* macro
	\param line	Will be provided by the LOG_* macro
	\param function	Will be provided by the LOG_* macro
	\param fmt	This is what is important.  The format is the same as your favorite breed of printf.  You know how that works, right? :-)
 */
extern void cw_log(cw_log_level level, const char *file, int line, const char *function, const char *fmt, ...)
	__attribute__ ((format (printf, 5, 6)));

extern void cw_backtrace(int levels);

extern void cw_queue_log(const char *queuename, const char *callid, const char *agent, const char *event, const char *fmt, ...)
	__attribute__ ((format (printf, 5, 6)));

/*! Send a verbose message (based on verbose level) 
 	\brief This works like cw_log, but prints verbose messages to the console depending on verbosity level set.
 	cw_verbose(VERBOSE_PREFIX_3 "Whatever %s is happening\n", "nothing");
 	This will print the message to the console if the verbose level is set to a level >= 3
 	Note the abscence of a comma after the VERBOSE_PREFIX_3.  This is important.
 	VERBOSE_PREFIX_1 through VERBOSE_PREFIX_3 are defined.
 */
#define cw_verbose(...)		cw_log(CW_LOG_VERBOSE, __VA_ARGS__)


#define _A_ __FILE__, __LINE__, __PRETTY_FUNCTION__

#define CW_LOG_DEBUG      __CW_LOG_DEBUG,   _A_
#define CW_LOG_EVENT      __CW_LOG_EVENT,   _A_
#define CW_LOG_NOTICE     __CW_LOG_NOTICE,  _A_
#define CW_LOG_WARNING    __CW_LOG_WARNING, _A_
#define CW_LOG_ERROR      __CW_LOG_ERROR,   _A_
#define CW_LOG_VERBOSE    __CW_LOG_VERBOSE, _A_
#define CW_LOG_DTMF       __CW_LOG_DTMF,    _A_

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_LOGGER_H */
