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
 * \brief CallWeaver Logger
 * 
 * Logging routines
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#ifdef STACK_BACKTRACES
#if defined(__linux__)
#include <execinfo.h>
#endif
#endif

#define SYSLOG_NAMES /* so we can map syslog facilities names to their numeric values,
		        from <syslog.h> which is included by logger.h */
#include <syslog.h>


#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/logger.h"
#include "callweaver/lock.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/config.h"
#include "callweaver/cli.h"
#include "callweaver/utils.h"
#include "callweaver/manager.h"


#define MAX_MSG_QUEUE 200

#if defined(__linux__)
#define GETTID() ((unsigned long)pthread_self())
#define TIDFMT "%lu"
#elif defined(__solaris__)
#define GETTID() ((unsigned int)pthread_self())
#define TIDFMT "%u"
#else
#define GETTID() ((long)getpid())
#define TIDFMT "%ld"
#endif

static char dateformat[256] = "%b %e %T";		/* Original CallWeaver Format */

CW_MUTEX_DEFINE_STATIC(loglock);

static struct {
	unsigned int queue_log:1;
	unsigned int event_log:1;
} logfiles = { 1, 1 };


static char hostname[MAXHOSTNAMELEN];

enum logtypes {
	LOGTYPE_SYSLOG,
	LOGTYPE_FILE,
};

struct logchannel {
	struct mansession *sess;
	int logmask;			/* What to log to this channel */
	int disabled;			/* If this channel is disabled or not */
	int facility; 			/* syslog facility */
	enum logtypes type;		/* Type of log channel */
	char filename[256];		/* Filename */
	struct logchannel *next;	/* Next channel in chain */
};

static struct logchannel *logchannels = NULL;

static FILE *eventlog = NULL;


static char *levels[] = {
	[CW_EVENT_NUM_ERROR]	= "ERROR",
	[CW_EVENT_NUM_WARNING]	= "WARNING",
	[CW_EVENT_NUM_NOTICE]	= "NOTICE",
	[CW_EVENT_NUM_VERBOSE]	= "VERBOSE",
	[CW_EVENT_NUM_EVENT]	= "EVENT",
	[CW_EVENT_NUM_DTMF]	= "DTMF",
	[CW_EVENT_NUM_DEBUG]	= "DEBUG",
	[CW_EVENT_NUM_PROGRESS]	= "PROGRESS",
};


static struct logchannel *make_logchannel(char *channel, char *components, int lineno)
{
	struct logchannel *chan;
	char *facility;
#ifndef SOLARIS
	CODE *cptr;
#endif

	if (cw_strlen_zero(channel))
		return NULL;

	if (!(chan = calloc(1, sizeof(struct logchannel))))
		return NULL;

	chan->logmask = manager_str_to_eventmask(components);

	if (!strncasecmp(channel, "syslog", 6)) {
		/*
		* syntax is:
		*  syslog.facility => level,level,level
		*/
		facility = strchr(channel, '.');
		if(!facility++ || !facility) {
			facility = "local0";
		}

#ifndef SOLARIS
		/*
 		* Walk through the list of facilitynames (defined in sys/syslog.h)
		* to see if we can find the one we have been given
		*/
		chan->facility = -1;
 		cptr = facilitynames;
		while (cptr->c_name) {
			if (!strcasecmp(facility, cptr->c_name)) {
		 		chan->facility = cptr->c_val;
				break;
			}
			cptr++;
		}
#else
		chan->facility = -1;
		if (!strcasecmp(facility, "kern")) 
			chan->facility = LOG_KERN;
		else if (!strcasecmp(facility, "USER")) 
			chan->facility = LOG_USER;
		else if (!strcasecmp(facility, "MAIL")) 
			chan->facility = LOG_MAIL;
		else if (!strcasecmp(facility, "DAEMON")) 
			chan->facility = LOG_DAEMON;
		else if (!strcasecmp(facility, "AUTH")) 
			chan->facility = LOG_AUTH;
		else if (!strcasecmp(facility, "SYSLOG")) 
			chan->facility = LOG_SYSLOG;
		else if (!strcasecmp(facility, "LPR")) 
			chan->facility = LOG_LPR;
		else if (!strcasecmp(facility, "NEWS")) 
			chan->facility = LOG_NEWS;
		else if (!strcasecmp(facility, "UUCP")) 
			chan->facility = LOG_UUCP;
		else if (!strcasecmp(facility, "CRON")) 
			chan->facility = LOG_CRON;
		else if (!strcasecmp(facility, "LOCAL0")) 
			chan->facility = LOG_LOCAL0;
		else if (!strcasecmp(facility, "LOCAL1")) 
			chan->facility = LOG_LOCAL1;
		else if (!strcasecmp(facility, "LOCAL2")) 
			chan->facility = LOG_LOCAL2;
		else if (!strcasecmp(facility, "LOCAL3")) 
			chan->facility = LOG_LOCAL3;
		else if (!strcasecmp(facility, "LOCAL4")) 
			chan->facility = LOG_LOCAL4;
		else if (!strcasecmp(facility, "LOCAL5")) 
			chan->facility = LOG_LOCAL5;
		else if (!strcasecmp(facility, "LOCAL6")) 
			chan->facility = LOG_LOCAL6;
		else if (!strcasecmp(facility, "LOCAL7")) 
			chan->facility = LOG_LOCAL7;
#endif /* Solaris */

		if (0 > chan->facility) {
			fprintf(stderr, "Logger Warning: bad syslog facility in logger.conf\n");
			free(chan);
			return NULL;
		}

		snprintf(chan->filename, sizeof(chan->filename), "%s", channel);

		if (!(chan->sess = manager_session_start(manager_session_log, -1, AF_INTERNAL, chan->filename, sizeof(chan->filename) - 1, chan->logmask, 0, chan->logmask))) {
			/* Can't log here, since we're called with a lock */
			fprintf(stderr, "Logger Warning: Unable to start syslog logging: %s\n", strerror(errno));
			free(chan);
			return NULL;
		}

		chan->type = LOGTYPE_SYSLOG;
		openlog("callweaver", LOG_PID, chan->facility);
	} else {
		int fd;

		if (channel[0] == '/') {
			if (!cw_strlen_zero(hostname))
				snprintf(chan->filename, sizeof(chan->filename) - 1,"%s.%s", channel, hostname);
			else
				cw_copy_string(chan->filename, channel, sizeof(chan->filename));
		}
		
		if (!cw_strlen_zero(hostname))
			snprintf(chan->filename, sizeof(chan->filename), "%s/%s.%s",(char *)cw_config_CW_LOG_DIR, channel, hostname);
		else
			snprintf(chan->filename, sizeof(chan->filename), "%s/%s", (char *)cw_config_CW_LOG_DIR, channel);

		fd = -1;
		if ((fd = open(chan->filename, O_WRONLY|O_APPEND)) < 0 || !(chan->sess = manager_session_start(manager_session_log, fd, AF_PATHNAME, chan->filename, sizeof(chan->filename) - 1, chan->logmask, 0, chan->logmask))) {
			if (fd >= 0)
				close(fd);
			/* Can't log here, since we're called with a lock */
			fprintf(stderr, "Logger Warning: Unable to open log file '%s': %s\n", chan->filename, strerror(errno));
			free(chan);
			return NULL;
		}
		chan->type = LOGTYPE_FILE;
	}

	return chan;
}

void close_logger(void)
{
	struct logchannel *chan, *cur;
 
	cw_mutex_lock(&loglock);
	chan = logchannels;
	while (chan) {
		cur = chan->next;
		manager_session_end(chan->sess);
		free(chan);
		chan = cur;
	}
	logchannels = NULL;
	cw_mutex_unlock(&loglock);
}


static void init_logger_chain(void)
{
	struct logchannel *chan;
	struct cw_config *cfg;
	struct cw_variable *var;
	char *s;

	close_logger();
	closelog();

	cfg = cw_config_load("logger.conf");
	
	/* If no config file, we're fine */
	if (!cfg)
		return;
	
	cw_mutex_lock(&loglock);
	if ((s = cw_variable_retrieve(cfg, "general", "appendhostname"))) {
		if(cw_true(s)) {
			if(gethostname(hostname, sizeof(hostname)-1)) {
				cw_copy_string(hostname, "unknown", sizeof(hostname));
				cw_log(CW_LOG_WARNING, "What box has no hostname???\n");
			}
		} else
			hostname[0] = '\0';
	} else
		hostname[0] = '\0';
	if ((s = cw_variable_retrieve(cfg, "general", "dateformat"))) {
		cw_copy_string(dateformat, s, sizeof(dateformat));
	} else
		cw_copy_string(dateformat, "%b %e %T", sizeof(dateformat));
	if ((s = cw_variable_retrieve(cfg, "general", "queue_log"))) {
		logfiles.queue_log = cw_true(s);
	}
	if ((s = cw_variable_retrieve(cfg, "general", "event_log"))) {
		logfiles.event_log = cw_true(s);
	}

	var = cw_variable_browse(cfg, "logfiles");
	while (var) {
		if (strcasecmp(var->name, "console")) {
			chan = make_logchannel(var->name, var->value, var->lineno);
			if (chan) {
				chan->next = logchannels;
				logchannels = chan;
			}
		}
		var = var->next;
	}

	cw_config_destroy(cfg);
	cw_mutex_unlock(&loglock);
}

static FILE *qlog = NULL;
CW_MUTEX_DEFINE_STATIC(qloglock);

void cw_queue_log(const char *queuename, const char *callid, const char *agent, const char *event, const char *fmt, ...)
{
	va_list ap;
	cw_mutex_lock(&qloglock);
	if (qlog) {
		va_start(ap, fmt);
		fprintf(qlog, "%ld|%s|%s|%s|%s|", (long)time(NULL), callid, queuename, agent, event);
		vfprintf(qlog, fmt, ap);
		fprintf(qlog, "\n");
		va_end(ap);
		fflush(qlog);
	}
	cw_mutex_unlock(&qloglock);
}

static void queue_log_init(void)
{
	char filename[256];
	int reloaded = 0;

	cw_mutex_lock(&qloglock);
	if (qlog) {
		reloaded = 1;
		fclose(qlog);
		qlog = NULL;
	}
	snprintf(filename, sizeof(filename), "%s/%s", (char *)cw_config_CW_LOG_DIR, "queue_log");
	if (logfiles.queue_log) {
		qlog = fopen(filename, "a");
	}
	cw_mutex_unlock(&qloglock);
	if (reloaded) 
		cw_queue_log("NONE", "NONE", "NONE", "CONFIGRELOAD", "%s", "");
	else
		cw_queue_log("NONE", "NONE", "NONE", "QUEUESTART", "%s", "");
}

int reload_logger(int rotate)
{
	char old[CW_CONFIG_MAX_PATH] = "";
	char new[CW_CONFIG_MAX_PATH];
	FILE *myf;
	int x;

	cw_mutex_lock(&loglock);
	if (eventlog) 
		fclose(eventlog);
	else 
		rotate = 0;
	eventlog = NULL;

	mkdir((char *)cw_config_CW_LOG_DIR, 0755);
	snprintf(old, sizeof(old), "%s/%s", (char *)cw_config_CW_LOG_DIR, EVENTLOG);

	if (logfiles.event_log) {
		if (rotate) {
			for (x=0;;x++) {
				snprintf(new, sizeof(new), "%s/%s.%d", (char *)cw_config_CW_LOG_DIR, EVENTLOG,x);
				myf = fopen((char *)new, "r");
				if (myf) 	/* File exists */
					fclose(myf);
				else
					break;
			}
	
			/* do it */
			if (rename(old,new))
				fprintf(stderr, "Unable to rename file '%s' to '%s'\n", old, new);
		}

		eventlog = fopen(old, "a");
	}

	cw_mutex_unlock(&loglock);

	queue_log_init();
	init_logger_chain();

	if (logfiles.event_log) {
		if (eventlog) {
			cw_log(CW_LOG_EVENT, "Restarted CallWeaver Event Logger\n");
			if (option_verbose)
				cw_verbose("CallWeaver Event Logger restarted\n");
			return 0;
		} else 
			cw_log(CW_LOG_ERROR, "Unable to create event log: %s\n", strerror(errno));
	} else 
		return 0;
	return -1;
}

static int handle_logger_reload(int fd, int argc, char *argv[])
{
	if(reload_logger(0)) {
		cw_cli(fd, "Failed to reload the logger\n");
		return RESULT_FAILURE;
	} else
		return RESULT_SUCCESS;
}

static int handle_logger_rotate(int fd, int argc, char *argv[])
{
	cw_log(CW_LOG_WARNING, "built-in log rotation is deprecated. Please use the system log rotation and restart logger with 'logger reload'. See contrib in the source for sample logrotate files.\n");
	if(reload_logger(1)) {
		cw_cli(fd, "Failed to reload the logger and rotate log files\n");
		return RESULT_FAILURE;
	} else
		return RESULT_SUCCESS;
}

/*--- handle_logger_show_channels: CLI command to show logging system 
 	configuration */
static int handle_logger_show_channels(int fd, int argc, char *argv[])
{
#define FORMATL	"%-35.35s %-8.8s %-9.9s "
	struct logchannel *chan;

	cw_mutex_lock(&loglock);

	chan = logchannels;
	cw_cli(fd,FORMATL, "Channel", "Type", "Status");
	cw_cli(fd, "Configuration\n");
	cw_cli(fd,FORMATL, "-------", "----", "------");
	cw_cli(fd, "-------------\n");
	while (chan) {
		cw_cli(fd, FORMATL, chan->filename, (chan->type == LOGTYPE_SYSLOG ? "Syslog" : "File"), (chan->disabled ? "Disabled" : "Enabled"));
		cw_cli(fd, " - ");
		if (chan->logmask & (1 << __CW_LOG_DEBUG)) 
			cw_cli(fd, "Debug ");
		if (chan->logmask & (1 << __CW_LOG_DTMF)) 
			cw_cli(fd, "DTMF ");
		if (chan->logmask & (1 << __CW_LOG_VERBOSE)) 
			cw_cli(fd, "Verbose ");
		if (chan->logmask & (1 << __CW_LOG_WARNING)) 
			cw_cli(fd, "Warning ");
		if (chan->logmask & (1 << __CW_LOG_NOTICE)) 
			cw_cli(fd, "Notice ");
		if (chan->logmask & (1 << __CW_LOG_ERROR)) 
			cw_cli(fd, "Error ");
		if (chan->logmask & (1 << __CW_LOG_EVENT)) 
			cw_cli(fd, "Event ");
		cw_cli(fd, "\n");
		chan = chan->next;
	}
	cw_cli(fd, "\n");

	cw_mutex_unlock(&loglock);
 		
	return RESULT_SUCCESS;
}


static char logger_reload_help[] =
"Usage: logger reload\n"
"       Reloads the logger subsystem state.  Use after restarting syslogd(8) if you are using syslog logging.\n";

static char logger_rotate_help[] =
"Usage: logger rotate\n"
"       Rotates and Reopens the log files.\n";

static char logger_show_channels_help[] =
"Usage: logger show channels\n"
"       Show configured logger channels.\n";

static struct cw_clicmd logger_show_channels_cli = {
	.cmda = { "logger", "show", "channels", NULL }, 
	.handler = handle_logger_show_channels,
	.summary = "List configured log channels",
	.usage = logger_show_channels_help,
};

static struct cw_clicmd reload_logger_cli = {
	.cmda = { "logger", "reload", NULL }, 
	.handler = handle_logger_reload,
	.summary = "Reopens the log files",
	.usage = logger_reload_help,
};

static struct cw_clicmd rotate_logger_cli = {
	.cmda = { "logger", "rotate", NULL }, 
	.handler = handle_logger_rotate,
	.summary = "Rotates and reopens the log files",
	.usage = logger_rotate_help,
};


int init_logger(void)
{
	char tmp[256];

	/* register the relaod logger cli command */
	cw_cli_register(&reload_logger_cli);
	cw_cli_register(&rotate_logger_cli);
	cw_cli_register(&logger_show_channels_cli);

	/* initialize queue logger */
	queue_log_init();

	/* create log channels */
	init_logger_chain();

	/* create the eventlog */
	if (logfiles.event_log) {
		mkdir((char *)cw_config_CW_LOG_DIR, 0755);
		snprintf(tmp, sizeof(tmp), "%s/%s", (char *)cw_config_CW_LOG_DIR, EVENTLOG);
		eventlog = fopen((char *)tmp, "a");
		if (eventlog) {
			cw_log(CW_LOG_EVENT, "Started CallWeaver Event Logger\n");
			if (option_verbose)
				cw_verbose("CallWeaver Event Logger Started %s\n",(char *)tmp);
			return 0;
		} else 
			cw_log(CW_LOG_ERROR, "Unable to create event log: %s\n", strerror(errno));
	} else
		return 0;

	return -1;
}


/*
 * send log messages to syslog and/or the console
 */
void cw_log(cw_log_level level, const char *file, int line, const char *function, const char *fmt, ...)
{
	char buf[BUFSIZ];
	char date[256];
	char *msg;
	struct tm tm;
	time_t now;
	va_list ap;
	int datelen, msglen;
	
	/* don't display LOG_DEBUG messages unless option_verbose _or_ option_debug
	   are non-zero; LOG_DEBUG messages can still be displayed if option_debug
	   is zero, if option_verbose is non-zero (this allows for 'level zero'
	   LOG_DEBUG messages to be displayed, if the logmask on any channel
	   allows it)
	*/
	if (!option_verbose && !option_debug && (level == __CW_LOG_DEBUG)) {
		return;
	}

	/* Ignore anything other than the currently debugged file if there is one */
	if ((level == __CW_LOG_DEBUG) && !cw_strlen_zero(debug_filename) && strcasecmp(debug_filename, file))
		return;

	time(&now);
	localtime_r(&now, &tm);
	if (!(datelen = strftime(date, sizeof(date), dateformat, &tm)))
		date[0] = '\0';

	va_start(ap, fmt);
	if ((msglen = vsnprintf(buf, sizeof(buf), fmt, ap)) >= sizeof(buf))
		msglen = sizeof(buf) - 1;
	va_end(ap);

	/* FIXME: strip leading and trailing newlines. Really we need to audit all messages. */
	for (msg = buf; *msg == '\n' || *msg == '\r'; msg++,msglen--);
	while (msglen > 0 && (msg[msglen - 1] == '\n' || msg[msglen - 1] == '\r')) msg[--msglen] = '\0';

	if (level == __CW_LOG_EVENT) {
		cw_mutex_lock(&loglock);
		if (logfiles.event_log) {
			fprintf(eventlog, "%s callweaver[%d]: %s", date, getpid(), msg);
			fflush(eventlog);
		}
		cw_mutex_unlock(&loglock);
	}

	if (logchannels) {
		manager_event(1 << level, "Log", "Timestamp: %ld\r\nLevel: %d\r\nThread ID: " TIDFMT "\r\nFile: %s\r\nLine: %d\r\nFunction: %s\r\nDate Len: %d\r\nMessage Len: %d\r\nMessage: %s %s[" TIDFMT "]: %s:%d %s: %s\r\n", now, level, GETTID(), file, line, function, datelen + 1, msglen, date, levels[level], GETTID(), file, line, function, msg);
	} else {
		/* 
		 * we don't have the logger chain configured yet,
		 * so just log to stdout 
		*/
		if (level != __CW_LOG_VERBOSE)
			fprintf(stdout, "%s %s[" TIDFMT "]: %s:%d %s: %s", date, levels[level], GETTID(), file, line, function, msg);
	}
}

void cw_backtrace(int levels)
{
#if defined(STACK_BACKTRACES) && defined(__linux__)
	int count=0, i=0;
	void **addresses;
	char **strings;

	addresses = calloc(levels, sizeof(void *));
	if (addresses) {
		count = backtrace(addresses, levels);
		strings = backtrace_symbols(addresses, count);
		if (strings) {
			cw_log(CW_LOG_WARNING, "Got %d backtrace record%c\n", count, count != 1 ? 's' : ' ');
			for (i=0; i < count ; i++) {
				cw_log(CW_LOG_WARNING, "#%d: [%08X] %s\n", i, (unsigned int)addresses[i], strings[i]);
			}
			free(strings);
		} else {
			cw_log(CW_LOG_WARNING, "Could not allocate memory for backtrace\n");
		}
		free(addresses);
	} else {
		cw_log(CW_LOG_WARNING, "Could not allocate memory for backtrace\n");
	}
#else
	cw_log(CW_LOG_WARNING, "Must compile with gcc optimizations at -O1 or lower for stack backtraces\n");
#endif
}
