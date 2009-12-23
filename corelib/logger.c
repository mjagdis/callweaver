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
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define SYSLOG_NAMES /* so we can map syslog facilities names to their numeric values,
		        from <syslog.h> which is included by logger.h */
#include <syslog.h>


#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#define DEBUG_MUTEX_CANLOG 0

#include "callweaver/logger.h"
#include "callweaver/lock.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/config.h"
#include "callweaver/cli.h"
#include "callweaver/time.h"
#include "callweaver/utils.h"
#include "callweaver/manager.h"


#define MAX_MSG_QUEUE 200


#define DEFAULT_APPEND_HOSTNAME	0


static int cfg_appendhostname;
static char dateformat[256] = "%b %e %T";		/* Original CallWeaver Format */


CW_MUTEX_DEFINE_STATIC(loglock);

static struct {
	unsigned int queue_log:1;
	unsigned int event_log:1;
} logfiles = { 1, 1 };


struct logchannel {
	struct mansession *sess;
	int facility; 			/* syslog facility */
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


static int logger_manager_session(struct mansession *sess, const struct manager_event *event)
{
	static const int priorities[] = {
		[CW_EVENT_NUM_ERROR]	= LOG_ERR,
		[CW_EVENT_NUM_WARNING]	= LOG_WARNING,
		[CW_EVENT_NUM_NOTICE]	= LOG_NOTICE,
		[CW_EVENT_NUM_VERBOSE]	= LOG_INFO,
		[CW_EVENT_NUM_EVENT]	= LOG_INFO,
		[CW_EVENT_NUM_DTMF]	= LOG_INFO,
		[CW_EVENT_NUM_DEBUG]	= LOG_DEBUG,
	};
	static const struct {
		int i_iov;
		int l;
		const char *s;
	} keys[] = {
#define LENSTR(x)	sizeof(x) - 1, x
		{ 1, LENSTR("Level") },
		{ 0, LENSTR("Date") },
		{ 3, LENSTR("Thread ID") },
		{ 5, LENSTR("File") },
		{ 7, LENSTR("Line") },
		{ 9, LENSTR("Function") },
#undef LENSTR
	};
	struct iovec iov[] = {
		/*  0: Date      */ { .iov_base = "",    .iov_len = 0 },
		/*  1: Level     */ { .iov_base = "",    .iov_len = 0 },
		/*  2: [         */ { .iov_base = "[",   .iov_len = sizeof("]") - 1 },
		/*  3: Thread ID */ { .iov_base = "",    .iov_len = 0 },
		/*  4: ]:        */ { .iov_base = "]: ", .iov_len = sizeof("]: ") - 1 },
		/*  5: File      */ { .iov_base = "",    .iov_len = 0 },
		/*  6: :         */ { .iov_base = ":",   .iov_len = sizeof(":") - 1 },
		/*  7: Line      */ { .iov_base = "",    .iov_len = 0 },
		/*  8:           */ { .iov_base = " ",   .iov_len = sizeof(" ") - 1 },
		/*  9: Function  */ { .iov_base = "",    .iov_len = 0 },
		/* 10: :         */ { .iov_base = ": ",  .iov_len = sizeof(": ") - 1 },
		/* 11: Message   */ { .iov_base = "",    .iov_len = 0 },
		/* 12: \n        */ { .iov_base = "\n",  .iov_len = sizeof("\n") - 1 }
	};
	char *p;
	int i, j, n, level = 0;
	int res = 0;

	/* The first key-value pair will always be "Event: Log" so we can ignore that here */
	for (i = 1; i < event->count; i++) {
		if (event->map[(i << 1) + 1] - event->map[(i << 1) + 0] - 2 != sizeof("Message") - 1
		|| memcmp(event->data->data + event->map[(i << 1)], "Message", sizeof("Message") - 1)) {
			for (j = 0; j < arraysize(keys); j++) {
				if (event->map[(i << 1) + 1] - event->map[(i << 1)] - 2 == keys[j].l
				&& !strncmp(event->data->data + event->map[(i << 1)], keys[j].s, keys[j].l)) {
					iov[keys[j].i_iov].iov_base = event->data->data + event->map[(i << 1) + 1];
					iov[keys[j].i_iov].iov_len = event->map[(i << 1) + 2] - event->map[(i << 1) + 1] - 2;
					break;
				}
			}
		} else {
			if (sess->addr.sa.sa_family == AF_PATHNAME) {
				/* Strip off the numeric level prefix */
				while (iov[1].iov_len && isdigit(*(char *)iov[1].iov_base))
					iov[1].iov_base++, iov[1].iov_len--;
			} else {
				level = atol(iov[1].iov_base);
			}

			p = event->data->data + event->map[(i << 1) + 1] + 2;
			j = event->map[(i << 1) + 2] - event->map[(i << 1) + 1] - 2 - (sizeof("--END MESSAGE--\r\n") - 1);
			while (j > 0) {
				n = strcspn(p, "\r\n");

				if (sess->addr.sa.sa_family == AF_PATHNAME) {
					if (sess->fd >= 0 || (sess->fd = open_cloexec(sess->name + sizeof("file:") - 1, O_CREAT|O_WRONLY|O_APPEND|O_NOCTTY, 0644)) >= 0) {
						iov[11].iov_base = p;
						iov[11].iov_len = n;

						if (cw_writev_all(sess->fd, iov, arraysize(iov)) < 0) {
							cw_log(CW_LOG_ERROR, "Write to '%s' failed: %s", sess->name + sizeof("file:") - 1, strerror(errno));
							res = -1;
							goto out_error;
						}
					} else {
						cw_log(CW_LOG_ERROR, "Can't write to '%s': %s", sess->name + sizeof("file:") - 1, strerror(errno));
						res = -1;
						goto out_error;
					}
				} else {
					syslog(priorities[level], "[%.*s]: %.*s:%.*s %.*s: %.*s",
						(int)iov[3].iov_len, (char *)iov[3].iov_base,
						(int)iov[5].iov_len, (char *)iov[5].iov_base,
						(int)iov[7].iov_len, (char *)iov[7].iov_base,
						(int)iov[9].iov_len, (char *)iov[9].iov_base,
						n, p);
				}

				p += n;
				j -= n;
				if (*p == '\r')
					p++,j--;
				if (*p == '\n')
					p++,j--;
			}
		}
	}

out_error:
	return res;
}


static struct logchannel *make_logchannel(char *channel, char *components, int lineno)
{
	cw_address_t addr;
	struct logchannel *chan;
	char *facility;
#ifndef SOLARIS
	CODE *cptr;
#endif
	int logmask;

	if (cw_strlen_zero(channel))
		return NULL;

	if (!(chan = calloc(1, sizeof(struct logchannel))))
		return NULL;

	chan->facility = -1;

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
 		cptr = facilitynames;
		while (cptr->c_name) {
			if (!strcasecmp(facility, cptr->c_name)) {
		 		chan->facility = cptr->c_val;
				break;
			}
			cptr++;
		}
#else
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

		if (chan->facility == -1) {
			fprintf(stderr, "Logger Warning: bad syslog facility in logger.conf\n");
			free(chan);
			return NULL;
		}

		snprintf(chan->filename, sizeof(chan->filename), "%s", channel);
		addr.sa.sa_family = AF_INTERNAL;

		openlog("callweaver", LOG_PID, chan->facility);
	} else {
		snprintf(chan->filename, sizeof(chan->filename), "%s%s%s%s%s",
			(channel[0] != '/' ? cw_config_CW_LOG_DIR : ""),
			(channel[0] != '/' ? "/" : ""),
			channel,
			(cfg_appendhostname ? "." : ""),
			(cfg_appendhostname ? hostname : ""));
		addr.sa.sa_family = AF_PATHNAME;
	}

	cw_copy_string(addr.sun.sun_path, chan->filename, sizeof(addr.sun.sun_path));
	logmask = manager_str_to_eventmask(components);

	if (!(chan->sess = manager_session_start(logger_manager_session, -1, &addr, NULL, logmask, 0, logmask))) {
		/* Can't log here, since we're called with a lock */
		fprintf(stderr, "Logger Warning: Unable to start logging to '%s': %s\n", chan->filename, strerror(errno));
		free(chan);
		return NULL;
	}

	return chan;
}

void close_logger(void)
{
	struct logchannel *chan;
 
	cw_mutex_lock(&loglock);

	for (chan = logchannels; chan; chan = chan->next)
		manager_session_shutdown(chan->sess);

	while ((chan = logchannels)) {
		logchannels = chan->next;
		manager_session_end(chan->sess);
		free(chan);
	}

	cw_mutex_unlock(&loglock);
}


static void init_logger_chain(void)
{
	struct logchannel *chan;
	struct cw_config *cfg;
	struct cw_variable *var;
	char *s;

	cfg_appendhostname = DEFAULT_APPEND_HOSTNAME;

	close_logger();
	closelog();

	cfg = cw_config_load("logger.conf");
	
	/* If no config file, we're fine */
	if (!cfg)
		return;
	
	cw_mutex_lock(&loglock);
	if ((s = cw_variable_retrieve(cfg, "general", "appendhostname")))
		cfg_appendhostname = cw_true(s);
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
#define FORMATL	"%-35.35s %-8.8s"
	struct logchannel *chan;

	cw_cli(fd, FORMATL " %s\n" FORMATL " %s\n",
		"Channel", "Type", "Configuration\n",
		"-------", "----", "-------------\n");

	cw_mutex_lock(&loglock);

	for (chan = logchannels; chan; chan = chan->next) {
		cw_cli(fd, FORMATL, chan->filename, (chan->facility == -1 ? "File" : "Syslog"));
		if (chan->sess->send_events & (1 << __CW_LOG_DEBUG)) 
			cw_cli(fd, "Debug ");
		if (chan->sess->send_events & (1 << __CW_LOG_DTMF)) 
			cw_cli(fd, "DTMF ");
		if (chan->sess->send_events & (1 << __CW_LOG_VERBOSE)) 
			cw_cli(fd, "Verbose ");
		if (chan->sess->send_events & (1 << __CW_LOG_WARNING)) 
			cw_cli(fd, "Warning ");
		if (chan->sess->send_events & (1 << __CW_LOG_NOTICE)) 
			cw_cli(fd, "Notice ");
		if (chan->sess->send_events & (1 << __CW_LOG_ERROR)) 
			cw_cli(fd, "Error ");
		if (chan->sess->send_events & (1 << __CW_LOG_EVENT)) 
			cw_cli(fd, "Event ");
		cw_cli(fd, "\n");
	}

	cw_mutex_unlock(&loglock);

	cw_cli(fd, "\n");
 		
	return RESULT_SUCCESS;
}


static const char logger_reload_help[] =
"Usage: logger reload\n"
"       Reloads the logger subsystem state.  Use after restarting syslogd(8) if you are using syslog logging.\n";

static const char logger_rotate_help[] =
"Usage: logger rotate\n"
"       Rotates and Reopens the log files.\n";

static const char logger_show_channels_help[] =
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
	struct timespec now;
	va_list ap;
	int msglen;

	/* don't display LOG_DEBUG messages unless option_verbose _or_ option_debug
	   are non-zero; LOG_DEBUG messages can still be displayed if option_debug
	   is zero, if option_verbose is non-zero (this allows for 'level zero'
	   LOG_DEBUG messages to be displayed, if the logmask on any channel
	   allows it)
	*/
	if (!option_verbose && !option_debug && (level == __CW_LOG_DEBUG))
		return;

	/* We only want the base name... */
	if ((msg = strrchr(file, '/')))
		file = msg + 1;

	/* Ignore anything other than the currently debugged file if there is one */
	if ((level == __CW_LOG_DEBUG) && !cw_strlen_zero(debug_filename) && strcasecmp(debug_filename, file))
		return;

	cw_clock_gettime(CLOCK_REALTIME, &now);
	localtime_r(&now.tv_sec, &tm);
	if (!(msglen = cw_strftime(date, sizeof(date), dateformat, &tm, &now, 0)))
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
			fprintf(eventlog, "%s callweaver[%d]: %s\n", date, getpid(), msg);
			fflush(eventlog);
		}
		cw_mutex_unlock(&loglock);
	}

	if (logchannels) {
		cw_manager_event(1 << level, "Log",
			8,
			cw_me_field("Timestamp", "%lu.%09lu", now.tv_sec, now.tv_nsec),
			cw_me_field("Date",      "%s",      date),
			cw_me_field("Level",     "%d %s",   level, levels[level]),
			cw_me_field("Thread ID", TIDFMT,    GETTID()),
			cw_me_field("File",      "%s",      file),
			cw_me_field("Line",      "%d",      line),
			cw_me_field("Function",  "%s",      function),
			cw_me_field("Message",   "\r\n%s\r\n--END MESSAGE--",  msg)
		);
	} else {
		/* 
		 * we don't have the logger chain configured yet,
		 * so just log to stdout 
		*/
		if (level != __CW_LOG_VERBOSE)
			fprintf(stdout, "%s %s[" TIDFMT "]: %s:%d %s: %s\n", date, levels[level], GETTID(), file, line, function, msg);
	}
}
