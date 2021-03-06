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

#include "callweaver/dynstr.h"
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
} logfiles = { 1 };


struct logchannel {
	struct mansession *sess;
	int facility;
	const char *filename;
	struct logchannel *next;
};

static struct logchannel *logchannels = NULL;


static const char *levels[] = {
	[CW_LOG_ERROR]    = "ERROR",
	[CW_LOG_WARNING]  = "WARNING",
	[CW_LOG_NOTICE]   = "NOTICE",
	[CW_LOG_VERBOSE]  = "VERBOSE",
	[CW_LOG_DTMF]     = "DTMF",
	[CW_LOG_DEBUG]    = "DEBUG",
	[CW_LOG_PROGRESS] = "PROGRESS",
};


static int logger_manager_session(struct mansession *sess, const struct cw_manager_message *event)
{
	static const int priorities[] = {
		[CW_LOG_ERROR]   = LOG_ERR,
		[CW_LOG_WARNING] = LOG_WARNING,
		[CW_LOG_NOTICE]  = LOG_NOTICE,
		[CW_LOG_VERBOSE] = LOG_INFO,
		[CW_LOG_DTMF]    = LOG_INFO,
		[CW_LOG_DEBUG]   = LOG_DEBUG,
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
		/*  0: Date      */ { .iov_base = (void *)"",    .iov_len = 0 },
		/*  1: Level     */ { .iov_base = (void *)"",    .iov_len = 0 },
		/*  2: [         */ { .iov_base = (void *)"[",   .iov_len = sizeof("]") - 1 },
		/*  3: Thread ID */ { .iov_base = (void *)"",    .iov_len = 0 },
		/*  4: ]:        */ { .iov_base = (void *)"]: ", .iov_len = sizeof("]: ") - 1 },
		/*  5: File      */ { .iov_base = (void *)"",    .iov_len = 0 },
		/*  6: :         */ { .iov_base = (void *)":",   .iov_len = sizeof(":") - 1 },
		/*  7: Line      */ { .iov_base = (void *)"",    .iov_len = 0 },
		/*  8:           */ { .iov_base = (void *)" ",   .iov_len = sizeof(" ") - 1 },
		/*  9: Function  */ { .iov_base = (void *)"",    .iov_len = 0 },
		/* 10: :         */ { .iov_base = (void *)": ",  .iov_len = sizeof(": ") - 1 },
		/* 11: Message   */ { .iov_base = (void *)"",    .iov_len = 0 },
		/* 12: \n        */ { .iov_base = (void *)"\n",  .iov_len = sizeof("\n") - 1 }
	};
	char *p;
	int i, j, n, level = 0;
	int res = 0;

	/* The first key-value pair will always be "Event: Log" so we can ignore that here */
	for (i = 1; i < event->count; i++) {
		if (event->map[(i << 1) + 1] - event->map[(i << 1) + 0] - 2 != sizeof("Message") - 1
		|| memcmp(event->ds.data + event->map[(i << 1)], "Message", sizeof("Message") - 1)) {
			for (j = 0; j < arraysize(keys); j++) {
				if (event->map[(i << 1) + 1] - event->map[(i << 1)] - 2 == keys[j].l
				&& !strncmp(event->ds.data + event->map[(i << 1)], keys[j].s, keys[j].l)) {
					iov[keys[j].i_iov].iov_base = event->ds.data + event->map[(i << 1) + 1];
					iov[keys[j].i_iov].iov_len = event->map[(i << 1) + 2] - event->map[(i << 1) + 1] - 2;
					break;
				}
			}
		} else {
			if (sess->addr.sa_family == AF_PATHNAME) {
				/* Strip off the numeric level prefix */
				p = iov[1].iov_base;
				while (iov[1].iov_len && isdigit(*p))
					p++, iov[1].iov_len--;
				iov[1].iov_base = p;
			} else {
				level = atol(iov[1].iov_base);
			}

			p = event->ds.data + event->map[(i << 1) + 1] + 2;
			j = event->map[(i << 1) + 2] - event->map[(i << 1) + 1] - 2 - (sizeof("--END MESSAGE--\r\n") - 1);
			while (j > 0) {
				n = strcspn(p, "\r\n");

				if (sess->addr.sa_family == AF_PATHNAME) {
					const char *path = ((struct sockaddr_un *)&sess->addr)->sun_path;
					if (sess->fd >= 0 || (sess->fd = open_cloexec(path, O_CREAT|O_WRONLY|O_APPEND|O_NOCTTY, 0644)) >= 0) {
						iov[11].iov_base = p;
						iov[11].iov_len = n;

						if (cw_writev_all(sess->fd, iov, arraysize(iov)) < 0) {
							cw_log(CW_LOG_ERROR, "Write to '%s' failed: %s", path, strerror(errno));
							res = -1;
							goto out_error;
						}
					} else {
						cw_log(CW_LOG_ERROR, "Can't write to '%s': %s", path, strerror(errno));
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


static struct logchannel *make_logchannel(char *channel, char *components)
{
	struct logchannel *chan;
	const char *facility;
#ifndef SOLARIS
	CODE *cptr;
#endif
	int domain, logmask;

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
		if ((facility = strchr(channel, '.')))
			facility++;

		if (!facility || !facility[0])
			facility = "local0";

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

		chan->filename = strdup(channel);
		domain = AF_INTERNAL;

		openlog("callweaver", LOG_PID, chan->facility);
	} else {
		struct cw_dynstr ds = CW_DYNSTR_INIT;

		cw_dynstr_printf(&ds, "%s%s%s%s%s",
			(channel[0] != '/' ? cw_config[CW_LOG_DIR] : ""),
			(channel[0] != '/' ? "/" : ""),
			channel,
			(cfg_appendhostname ? "." : ""),
			(cfg_appendhostname ? hostname : ""));
		if (!ds.error)
			chan->filename = cw_dynstr_steal(&ds);
		cw_dynstr_free(&ds);
		domain = AF_PATHNAME;
	}

	if (chan->filename) {
		int l = strlen(chan->filename) + 1;
		socklen_t addrlen = CW_SOCKADDR_UN_SIZE(l);
		struct sockaddr_un *addr = alloca(l);

		addr->sun_family = domain;
		memcpy(addr->sun_path, chan->filename, l);
		logmask = manager_str_to_eventmask(components);

		if ((chan->sess = manager_session_start(logger_manager_session, -1, (struct sockaddr *)addr, addrlen, NULL, logmask, 0, logmask)))
			return chan;

		/* Can't log here, since we're called with a lock */
		fprintf(stderr, "Logger Warning: Unable to start logging to '%s': %s\n", chan->filename, strerror(errno));
	}

	free(chan);
	return NULL;
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

	var = cw_variable_browse(cfg, "logfiles");
	while (var) {
		if (strcasecmp(var->name, "console")) {
			chan = make_logchannel(var->name, var->value);
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
	int reloaded = 0;

	cw_mutex_lock(&qloglock);
	if (qlog) {
		reloaded = 1;
		fclose(qlog);
		qlog = NULL;
	}

	if (logfiles.queue_log) {
		struct cw_dynstr filename = CW_DYNSTR_INIT;

		cw_dynstr_printf(&filename, "%s/%s", cw_config[CW_LOG_DIR], "queue_log");
		if (!filename.error)
			qlog = fopen(filename.data, "a");
		cw_dynstr_free(&filename);
	}
	cw_mutex_unlock(&qloglock);
	if (reloaded) 
		cw_queue_log("NONE", "NONE", "NONE", "CONFIGRELOAD", "%s", "");
	else
		cw_queue_log("NONE", "NONE", "NONE", "QUEUESTART", "%s", "");
}


static int handle_logger_reload(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(ds_p);
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	mkdir(cw_config[CW_LOG_DIR], 0755);

	queue_log_init();
	init_logger_chain();

	return RESULT_SUCCESS;
}


/*--- handle_logger_show_channels: CLI command to show logging system 
 	configuration */
static int handle_logger_show_channels(struct cw_dynstr *ds_p, int argc, char *argv[])
{
#define FORMATL	"%-35.35s %-8.8s"
	struct logchannel *chan;

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_dynstr_printf(ds_p, FORMATL " %s\n" FORMATL " %s\n",
		"Channel", "Type", "Configuration\n",
		"-------", "----", "-------------\n");

	cw_mutex_lock(&loglock);

	for (chan = logchannels; chan; chan = chan->next) {
		cw_dynstr_printf(ds_p, FORMATL, chan->filename, (chan->facility == -1 ? "File" : "Syslog"));
		if (chan->sess->send_events & CW_EVENT_FLAG_DEBUG)
			cw_dynstr_printf(ds_p, "Debug ");
		if (chan->sess->send_events & CW_EVENT_FLAG_DTMF)
			cw_dynstr_printf(ds_p, "DTMF ");
		if (chan->sess->send_events & CW_EVENT_FLAG_VERBOSE)
			cw_dynstr_printf(ds_p, "Verbose ");
		if (chan->sess->send_events & CW_EVENT_FLAG_WARNING)
			cw_dynstr_printf(ds_p, "Warning ");
		if (chan->sess->send_events & CW_EVENT_FLAG_NOTICE)
			cw_dynstr_printf(ds_p, "Notice ");
		if (chan->sess->send_events & CW_EVENT_FLAG_ERROR)
			cw_dynstr_printf(ds_p, "Error ");
		cw_dynstr_printf(ds_p, "\n");
	}

	cw_mutex_unlock(&loglock);

	cw_dynstr_printf(ds_p, "\n");
 		
	return RESULT_SUCCESS;
}


static const char logger_reload_help[] =
"Usage: logger reload\n"
"       Reloads the logger subsystem state.  Use after restarting syslogd(8) if you are using syslog logging.\n";

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


int init_logger(void)
{
	/* register the relaod logger cli command */
	cw_cli_register(&reload_logger_cli);
	cw_cli_register(&logger_show_channels_cli);

	/* initialize queue logger */
	queue_log_init();

	/* create log channels */
	init_logger_chain();

	return 0;
}


/*
 * send log messages to syslog and/or the console
 */
void cw_log_internal(const char *file, int line, const char *function, cw_log_level level, const char *fmt, ...)
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
	if (!option_verbose && !option_debug && (level == CW_LOG_DEBUG))
		return;

	/* We only want the base name... */
	if ((msg = strrchr(file, '/')))
		file = msg + 1;

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

	if (logchannels) {
		cw_manager_event(1 << level, "Log",
			8,
			cw_msg_tuple("Timestamp", "%lu.%09lu", (unsigned long)now.tv_sec, (unsigned long)now.tv_nsec),
			cw_msg_tuple("Date",      "%s",      date),
			cw_msg_tuple("Level",     "%u %s",   (unsigned int)level, levels[level]),
			cw_msg_tuple("Thread ID", TIDFMT,    GETTID()),
			cw_msg_tuple("File",      "%s",      file),
			cw_msg_tuple("Line",      "%d",      line),
			cw_msg_tuple("Function",  "%s",      function),
			cw_msg_tuple("Message",   "\r\n%s\r\n--END MESSAGE--",  msg)
		);
	} else {
		/* 
		 * we don't have the logger chain configured yet,
		 * so just log to stdout 
		*/
		if (level != CW_LOG_VERBOSE)
			fprintf(stdout, "%s %s[" TIDFMT "]: %s:%d %s: %s\n", date, levels[level], GETTID(), file, line, function, msg);
	}
}
