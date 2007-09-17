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
 *
 * \brief Full-featured outgoing call spool support
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <utime.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/options.h"
#include "callweaver/utils.h"

/*
 * pbx_spool is similar in spirit to qcall, but with substantially enhanced functionality...
 * The spool file contains a header 
 */

static const char tdesc[] = "Outgoing Spool Support";
static char qdir[255];

struct outgoing {
	char fn[256];
	/* Current number of retries */
	int retries;
	/* Maximum number of retries permitted */
	int maxretries;
	/* How long to wait between retries (in seconds) */
	int retrytime;
	/* How long to wait for an answer */
	int waittime;
	/* PID which is currently calling */
	int callingpid;
	
	/* What to connect to outgoing */
	char tech[256];
	char dest[256];
	
	/* If application */
	char app[256];
	char data[256];

	/* If extension/context/priority */
	char exten[256];
	char context[256];
	int priority;

	/* CallerID Information */
	char cid_num[256];
	char cid_name[256];

	/* Variables and Functions */
	struct opbx_variable *vars;
	
	/* Maximum length of call */
	int maxlen;
};

static void init_outgoing(struct outgoing *o)
{
	memset(o, 0, sizeof(struct outgoing));
	o->priority = 1;
	o->retrytime = 300;
	o->waittime = 45;
}

static void free_outgoing(struct outgoing *o)
{
	free(o);
}

static int apply_outgoing(struct outgoing *o, char *fn, FILE *f)
{
	char buf[256];
	char *c, *c2;
	int lineno = 0;
	struct opbx_variable *var;

	while(fgets(buf, sizeof(buf), f)) {
		lineno++;
		/* Trim comments */
		c = buf;
		while ((c = strchr(c, '#'))) {
			if ((c == buf) || (*(c-1) == ' ') || (*(c-1) == '\t'))
				*c = '\0';
			else
				c++;
		}

		c = buf;
		while ((c = strchr(c, ';'))) {
			if ((c > buf) && (c[-1] == '\\')) {
				memmove(c - 1, c, strlen(c) + 1);
				c++;
			} else {
				*c = '\0';
				break;
			}
		}

		/* Trim trailing white space */
		while(!opbx_strlen_zero(buf) && buf[strlen(buf) - 1] < 33)
			buf[strlen(buf) - 1] = '\0';
		if (!opbx_strlen_zero(buf)) {
			c = strchr(buf, ':');
			if (c) {
				*c = '\0';
				c++;
				while ((*c) && (*c < 33))
					c++;
#if 0
				printf("'%s' is '%s' at line %d\n", buf, c, lineno);
#endif
				if (!strcasecmp(buf, "channel")) {
					strncpy(o->tech, c, sizeof(o->tech) - 1);
					if ((c2 = strchr(o->tech, '/'))) {
						*c2 = '\0';
						c2++;
						strncpy(o->dest, c2, sizeof(o->dest) - 1);
					} else {
						opbx_log(OPBX_LOG_NOTICE, "Channel should be in form Tech/Dest at line %d of %s\n", lineno, fn);
						o->tech[0] = '\0';
					}
				} else if (!strcasecmp(buf, "callerid")) {
					opbx_callerid_split(c, o->cid_name, sizeof(o->cid_name), o->cid_num, sizeof(o->cid_num));
				} else if (!strcasecmp(buf, "application")) {
					strncpy(o->app, c, sizeof(o->app) - 1);
				} else if (!strcasecmp(buf, "data")) {
					strncpy(o->data, c, sizeof(o->data) - 1);
				} else if (!strcasecmp(buf, "maxretries")) {
					if (sscanf(c, "%d", &o->maxretries) != 1) {
						opbx_log(OPBX_LOG_WARNING, "Invalid max retries at line %d of %s\n", lineno, fn);
						o->maxretries = 0;
					}
				} else if (!strcasecmp(buf, "context")) {
					strncpy(o->context, c, sizeof(o->context) - 1);
				} else if (!strcasecmp(buf, "extension")) {
					strncpy(o->exten, c, sizeof(o->exten) - 1);
				} else if (!strcasecmp(buf, "priority")) {
					if ((sscanf(c, "%d", &o->priority) != 1) || (o->priority < 1)) {
						opbx_log(OPBX_LOG_WARNING, "Invalid priority at line %d of %s\n", lineno, fn);
						o->priority = 1;
					}
				} else if (!strcasecmp(buf, "retrytime")) {
					if ((sscanf(c, "%d", &o->retrytime) != 1) || (o->retrytime < 1)) {
						opbx_log(OPBX_LOG_WARNING, "Invalid retrytime at line %d of %s\n", lineno, fn);
						o->retrytime = 300;
					}
				} else if (!strcasecmp(buf, "waittime")) {
					if ((sscanf(c, "%d", &o->waittime) != 1) || (o->waittime < 1)) {
						opbx_log(OPBX_LOG_WARNING, "Invalid retrytime at line %d of %s\n", lineno, fn);
						o->waittime = 45;
					}
				} else if (!strcasecmp(buf, "retry")) {
					o->retries++;
				} else if (!strcasecmp(buf, "startretry")) {
					if (sscanf(c, "%d", &o->callingpid) != 1) {
						opbx_log(OPBX_LOG_WARNING, "Unable to retrieve calling PID!\n");
						o->callingpid = 0;
					}
				} else if (!strcasecmp(buf, "endretry") || !strcasecmp(buf, "abortretry")) {
					o->callingpid = 0;
					o->retries++;
				} else if (!strcasecmp(buf, "delayedretry")) {
				} else if (!strcasecmp(buf, "setvar") || !strcasecmp(buf, "set")) {
					c2 = c;
					strsep(&c2, "=");
					var = opbx_variable_new(c, c2);
					if (var) {
						var->next = o->vars;
						o->vars = var;
					}
				} else if (!strcasecmp(buf, "account")) {
					var = opbx_variable_new("CDR(accountcode|r)", c);
					if (var) {	
						var->next = o->vars;
						o->vars = var;
					}
				} else {
					opbx_log(OPBX_LOG_WARNING, "Unknown keyword '%s' at line %d of %s\n", buf, lineno, fn);
				}
			} else
				opbx_log(OPBX_LOG_NOTICE, "Syntax error at line %d of %s\n", lineno, fn);
		}
	}
	strncpy(o->fn, fn, sizeof(o->fn) - 1);
	if (opbx_strlen_zero(o->tech) || opbx_strlen_zero(o->dest) || (opbx_strlen_zero(o->app) && opbx_strlen_zero(o->exten))) {
		opbx_log(OPBX_LOG_WARNING, "At least one of app or extension must be specified, along with tech and dest in file %s\n", fn);
		return -1;
	}
	return 0;
}

static void safe_append(struct outgoing *o, time_t now, char *s)
{
	int fd;
	FILE *f;
	struct utimbuf tbuf;
	fd = open(o->fn, O_WRONLY|O_APPEND);
	if (fd > -1) {
		f = fdopen(fd, "a");
		if (f) {
			fprintf(f, "%s: %ld %d (%ld)\n", s, (long)opbx_mainpid, o->retries, (long) now);
			fclose(f);
		} else
			close(fd);
		/* Update the file time */
		tbuf.actime = now;
		tbuf.modtime = now + o->retrytime;
		if (utime(o->fn, &tbuf))
			opbx_log(OPBX_LOG_WARNING, "Unable to set utime on %s: %s\n", o->fn, strerror(errno));
	}
}

static void *attempt_thread(void *data)
{
	struct outgoing *o = (struct outgoing *) data;
	int res, reason;
	if (!opbx_strlen_zero(o->app)) {
		if (option_verbose > 2)
			opbx_verbose(VERBOSE_PREFIX_3 "Attempting call on %s/%s for application %s(%s) (Retry %d)\n", o->tech, o->dest, o->app, o->data, o->retries);
		res = opbx_pbx_outgoing_app(o->tech, OPBX_FORMAT_SLINEAR, o->dest, o->waittime * 1000, o->app, o->data, &reason, 2 /* wait to finish */, o->cid_num, o->cid_name, o->vars, NULL);
	} else {
		if (option_verbose > 2)
			opbx_verbose(VERBOSE_PREFIX_3 "Attempting call on %s/%s for %s@%s:%d (Retry %d)\n", o->tech, o->dest, o->exten, o->context,o->priority, o->retries);
		res = opbx_pbx_outgoing_exten(o->tech, OPBX_FORMAT_SLINEAR, o->dest, o->waittime * 1000, o->context, o->exten, o->priority, &reason, 2 /* wait to finish */, o->cid_num, o->cid_name, o->vars, NULL);
	}
	if (res) {
		opbx_log(OPBX_LOG_NOTICE, "Call failed to go through, reason %d\n", reason);
		if (o->retries >= o->maxretries + 1) {
			/* Max retries exceeded */
			opbx_log(OPBX_LOG_EVENT, "Queued call to %s/%s expired without completion after %d attempt%s\n", o->tech, o->dest, o->retries - 1, ((o->retries - 1) != 1) ? "s" : "");
			unlink(o->fn);
		} else {
			/* Notate that the call is still active */
			safe_append(o, time(NULL), "EndRetry");
		}
	} else {
		opbx_log(OPBX_LOG_NOTICE, "Call completed to %s/%s\n", o->tech, o->dest);
		opbx_log(OPBX_LOG_EVENT, "Queued call to %s/%s completed\n", o->tech, o->dest);
		unlink(o->fn);
	}
	free_outgoing(o);
	return NULL;
}

static void launch_service(struct outgoing *o)
{
	pthread_t t;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
 	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (opbx_pthread_create(&t, &attr, attempt_thread, o) == -1) {
		opbx_log(OPBX_LOG_WARNING, "Unable to create thread :(\n");
		free_outgoing(o);
	}
}

static int scan_service(char *fn, time_t now, time_t atime)
{
	struct outgoing *o;
	FILE *f;
	o = malloc(sizeof(struct outgoing));
	if (o) {
		init_outgoing(o);
		f = fopen(fn, "r+");
		if (f) {
			if (!apply_outgoing(o, fn, f)) {
#if 0
				printf("Filename: %s, Retries: %d, max: %d\n", fn, o->retries, o->maxretries);
#endif
				fclose(f);
				if (o->retries <= o->maxretries) {
					if (o->callingpid && (o->callingpid == opbx_mainpid)) {
						safe_append(o, time(NULL), "DelayedRetry");
						opbx_log(OPBX_LOG_DEBUG, "Delaying retry since we're currently running '%s'\n", o->fn);
					} else {
						/* Increment retries */
						o->retries++;
						/* If someone else was calling, they're presumably gone now
						   so abort their retry and continue as we were... */
						if (o->callingpid)
							safe_append(o, time(NULL), "AbortRetry");

						safe_append(o, now, "StartRetry");
						launch_service(o);
					}
					now += o->retrytime;
					return now;
				} else {
					opbx_log(OPBX_LOG_EVENT, "Queued call to %s/%s expired without completion after %d attempt%s\n", o->tech, o->dest, o->retries - 1, ((o->retries - 1) != 1) ? "s" : "");
					free_outgoing(o);
					unlink(fn);
					return 0;
				}
			} else {
				free_outgoing(o);
				opbx_log(OPBX_LOG_WARNING, "Invalid file contents in %s, deleting\n", fn);
				fclose(f);
				unlink(fn);
			}
		} else {
			free_outgoing(o);
			opbx_log(OPBX_LOG_WARNING, "Unable to open %s: %s, deleting\n", fn, strerror(errno));
			unlink(fn);
		}
	} else
		opbx_log(OPBX_LOG_WARNING, "Out of memory :(\n");
	return -1;
}

static void *scan_thread(void *unused)
{
	char fn[256];
	struct stat st;
	DIR *dir;
	struct dirent *de;
	time_t last = 0, next = 0, now;
	int res;
	int dirstatfailed = 0;
	int diropenfailed = 0;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	for (;;) {
		/* Wait a sec */
		pthread_testcancel();
		sleep(1);
		time(&now);
		pthread_testcancel();

		if (stat(qdir, &st)) {
			if (!dirstatfailed) {
				pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
				opbx_log(OPBX_LOG_ERROR, "Unable to stat %s\n", qdir);
				pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
				dirstatfailed = 1;
			}
			continue;
		}
		dirstatfailed = 0;

		if ((st.st_mtime == last) && (!next || (now <= next)))
			continue;
		next = 0;
		last = st.st_mtime;

		if (!(dir = opendir(qdir))) {
			if (!diropenfailed) {
				pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
				opbx_log(OPBX_LOG_ERROR, "Unable to open directory %s: %s\n", qdir, strerror(errno));
				pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
				diropenfailed = 1;
			}
			continue;
		}
		diropenfailed = 0;

		pthread_cleanup_push((void (*)(void *))closedir, dir);

		while ((de = readdir(dir))) {
			if (de->d_name[0] == '.')
				continue;

			snprintf(fn, sizeof(fn), "%s/%s", qdir, de->d_name);
			if (stat(fn, &st)) {
				pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
				opbx_log(OPBX_LOG_ERROR, "Unable to stat %s: %s, deleting\n", fn, strerror(errno));
				pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
				unlink(fn);
				continue;
			}

			if (S_ISREG(st.st_mode)) {
				if (st.st_mtime <= now) {
					pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
					res = scan_service(fn, now, st.st_atime);
					if (res > 0) {
						/* Update next service time */
						if (!next || (res < next))
							next = res;
					} else if (res)
						opbx_log(OPBX_LOG_ERROR, "Failed to scan service '%s'\n", fn);
					pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
				} else {
					/* Update "next" update if necessary */
					if (!next || (st.st_mtime < next))
						next = st.st_mtime;
				}
			}
		}

		pthread_cleanup_pop(1);
	}
	return NULL;
}


pthread_t scan_thread_id = OPBX_PTHREADT_NULL;

static int unload_module(void)
{
	int res = 0;

	if (!pthread_equal(scan_thread_id, OPBX_PTHREADT_NULL)) {
		res |= pthread_cancel(scan_thread_id);
    		res |= pthread_kill(scan_thread_id, SIGURG);
		scan_thread_id = OPBX_PTHREADT_NULL;
	}
	return res;
}

static int load_module(void)
{
	pthread_attr_t attr;

	snprintf(qdir, sizeof(qdir), "%s/%s", opbx_config_OPBX_SPOOL_DIR, "outgoing");
	if (mkdir(qdir, 0700) && (errno != EEXIST)) {
		opbx_log(OPBX_LOG_WARNING, "Unable to create queue directory %s -- outgoing spool disabled\n", qdir);
		return 0;
	}

	pthread_attr_init(&attr);
 	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (opbx_pthread_create(&scan_thread_id, &attr, scan_thread, NULL) == -1) {
		opbx_log(OPBX_LOG_WARNING, "Unable to create thread :(\n");
		return -1;
	}
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
