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

#include <stdarg.h>


#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif


typedef enum {
	CW_LOG_ERROR    = 0,
	CW_LOG_WARNING,
	CW_LOG_NOTICE,
	CW_LOG_VERBOSE,
	CW_LOG_EVENT,
	CW_LOG_DTMF,
	CW_LOG_DEBUG,
	CW_LOG_PROGRESS,
} cw_log_level;

typedef enum {
	CW_EVENT_NUM_SYSTEM  = CW_LOG_PROGRESS + 1, /* System events such as module load/unload */
	CW_EVENT_NUM_CALL,                          /* Call event, such as state change, etc */
	CW_EVENT_NUM_COMMAND,                       /* Commands */
	CW_EVENT_NUM_AGENT,                         /* Read/set agent info */
	CW_EVENT_NUM_USER,                          /* Read/set user info */
} cw_event_num;


typedef enum {
	EVENT_FLAG_ERROR    = (1 << CW_LOG_ERROR),
	EVENT_FLAG_WARNING  = (1 << CW_LOG_WARNING),
	EVENT_FLAG_NOTICE   = (1 << CW_LOG_NOTICE),
	EVENT_FLAG_VERBOSE  = (1 << CW_LOG_VERBOSE),
	EVENT_FLAG_EVENT    = (1 << CW_LOG_EVENT),
	EVENT_FLAG_DTMF     = (1 << CW_LOG_DTMF),
	EVENT_FLAG_DEBUG    = (1 << CW_LOG_DEBUG),
	EVENT_FLAG_PROGRESS = (1 << CW_LOG_PROGRESS),

	EVENT_FLAG_SYSTEM   = (1 << CW_EVENT_NUM_SYSTEM) , /* System events such as module load/unload */
	EVENT_FLAG_CALL	    = (1 << CW_EVENT_NUM_CALL),    /* Call event, such as state change, etc */
	EVENT_FLAG_COMMAND  = (1 << CW_EVENT_NUM_COMMAND), /* Ability to read/set commands */
	EVENT_FLAG_AGENT    = (1 << CW_EVENT_NUM_AGENT),   /* Ability to read/set agent info */
	EVENT_FLAG_USER	    = (1 << CW_EVENT_NUM_USER),    /* Ability to read/set user info */

	EVENT_FLAG_LOG_ALL  = (EVENT_FLAG_ERROR | EVENT_FLAG_WARNING | EVENT_FLAG_NOTICE | EVENT_FLAG_VERBOSE | EVENT_FLAG_EVENT | EVENT_FLAG_DTMF | EVENT_FLAG_DEBUG),
} cw_event_flag;


/*! Used for sending a log message */
/*!
	\brief This is the standard logger function.  Probably the only way you will invoke it would be something like this:
	cw_log(CW_LOG_WHATEVER, "Problem with the %s Captain.  We should get some more.  Will %d be enough?\n", "flux capacitor", 10);
	where WHATEVER is one of ERROR, DEBUG, EVENT, NOTICE, or WARNING depending
	on which log you wish to output to. These are implemented as macros, that
	will provide the function with the needed arguments.

	\param file	Will be provided by the LOG_* macro
	\param line	Will be provided by the LOG_* macro
	\param function	Will be provided by the LOG_* macro
 	\param level	Type of log event
	\param fmt	This is what is important.  The format is the same as your favorite breed of printf.  You know how that works, right? :-)
 */
extern CW_API_PUBLIC void cw_log_internal(const char *file, int line, const char *function, cw_log_level level, const char *fmt, ...)
	__attribute__ ((format (printf, 5, 6)));
#define cw_log(level, fmt, ...)	cw_log_internal(__FILE__, __LINE__, __PRETTY_FUNCTION__, level, fmt, ## __VA_ARGS__)

extern CW_API_PUBLIC void cw_queue_log(const char *queuename, const char *callid, const char *agent, const char *event, const char *fmt, ...)
	__attribute__ ((format (printf, 5, 6)));

/*! Send a verbose message (based on verbose level) 
 	\brief This works like cw_log, but prints verbose messages to the console depending on verbosity level set.
 	cw_verbose(VERBOSE_PREFIX_3 "Whatever %s is happening\n", "nothing");
 	This will print the message to the console if the verbose level is set to a level >= 3
 	Note the abscence of a comma after the VERBOSE_PREFIX_3.  This is important.
 	VERBOSE_PREFIX_1 through VERBOSE_PREFIX_3 are defined.
 */
#define cw_verbose(...)		cw_log(CW_LOG_VERBOSE, __VA_ARGS__)


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_LOGGER_H */
