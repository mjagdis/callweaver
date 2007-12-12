/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Based on Asterisk written by Mark Spencer <markster@digium.com>
 *  Copyright (C) 1999 - 2005, Digium, Inc.
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


/* Doxygenified Copyright Header */
/*!
 * \mainpage CallWeaver -- An Open Source Telephony Toolkit
 *
 * \arg \ref DevDoc
 * \arg \ref ConfigFiles
 *
 * \section copyright Copyright and author
 *
 * Based on Asterisk written by Mark Spencer <markster@digium.com>
 *  Copyright (C) 1999 - 2005, Digium, Inc.
 * Asterisk is a trade mark registered by Digium, Inc.
 *
 * Also see \ref opbxCREDITS
 *
 * \section license License
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 *
 * \verbinclude LICENSE
 *
 */

/*! \file
  \brief Top level source file for CallWeaver  - the Open Source PBX. Implementation
  of PBX core functions and CLI interface.

 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/resource.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <semaphore.h>
#ifdef __linux__
# include <sys/prctl.h>
#endif
#include <regex.h>
#include <spandsp.h>
#include <vale/rfc3489.h>
#include <vale/udp.h>

#if  defined(__FreeBSD__) || defined( __NetBSD__ ) || defined(SOLARIS)
#include <netdb.h>
#endif

#ifdef __linux__
  /* Linux capability system calls are only prototyped if
   * _POSIX_SOURCE is undefined.
   */
# define OLD_POSIX_SOURCE _POSIX_SOURCE
# undef _POSIX_SOURCE
# include <sys/capability.h>
# define _POSIX_SOURCE OLD_POSIX_SOURCE
#endif

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/cli.h"
#include "callweaver/channel.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/atexit.h"
#include "callweaver/module.h"
#include "callweaver/image.h"
#include "callweaver/manager.h"
#include "callweaver/cdr.h"
#include "callweaver/pbx.h"
#include "callweaver/enum.h"
#include "callweaver/rtp.h"
#include "callweaver/udptl.h"
#include "callweaver/stun.h"
#include "callweaver/app.h"
#include "callweaver/lock.h"
#include "callweaver/utils.h"
#include "callweaver/file.h"
#include "callweaver/io.h"
#include "callweaver/lock.h"
#include "callweaver/config.h"
#include "callweaver/linkedlists.h"
#include "callweaver/devicestate.h"
#include "callweaver/directory_engine.h"
#include "callweaver/translate.h"
#include "callweaver/switch.h"

#include "callweaver/crypto.h"

#include "core/ulaw.h"
#include "core/alaw.h"

#include "libltdl/ltdl.h"

#include <readline/readline.h>
#include <readline/history.h>

/* defines various compile-time defaults */
#include "defaults.h"

#include "terminal.h"

#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#define PF_LOCAL PF_UNIX
#endif

#define OPBX_MAX_CONNECTS 128
#define NUM_MSGS 64


char hostname[MAXHOSTNAMELEN];
char shorthostname[MAXHOSTNAMELEN];

int option_verbose=0;
int option_debug=0;
int option_exec_includes=0;
int option_nofork=0;
int option_quiet=0;
int option_console=0;
int option_highpriority=0;
int option_remote=0;
int option_exec=0;
int option_initcrypto=0;
int option_nocolor = 0;
int option_dumpcore = 0;
int option_cache_record_files = 0;
int option_timestamp = 0;
int option_overrideconfig = 0;
int option_reconnect = 0;
int option_transcode_slin = 1;
int option_maxcalls = 0;
double option_maxload = 0.0;
int option_dontwarn = 0;
int option_priority_jumping = 1;
int fully_booted = 0;
char record_cache_dir[OPBX_CACHE_DIR_LEN] = opbxtmpdir_default;
char debug_filename[OPBX_FILENAME_MAX] = "";

int opbx_mainpid;
struct console {
	int fd;				/*!< File descriptor */
	int p[2];			/*!< Pipe */
	pthread_t t;			/*!< Thread of handler */
};

time_t opbx_startuptime;
time_t opbx_lastreloadtime;

static char *remotehostname;

struct console consoles[OPBX_MAX_CONNECTS];

char defaultlanguage[MAX_LANGUAGE] = DEFAULT_LANGUAGE;

char opbx_config_OPBX_CONFIG_DIR[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_CONFIG_FILE[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_SPOOL_DIR[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_MONITOR_DIR[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_VAR_DIR[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_LOG_DIR[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_OGI_DIR[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_DB[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_DB_DIR[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_KEY_DIR[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_PID[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_SOCKET[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_RUN_DIR[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_RUN_USER[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_RUN_GROUP[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_CTL_PERMISSIONS[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_CTL_OWNER[OPBX_CONFIG_MAX_PATH] = "\0";
char opbx_config_OPBX_CTL_GROUP[OPBX_CONFIG_MAX_PATH] = "\0";
char opbx_config_OPBX_CTL[OPBX_CONFIG_MAX_PATH] = "callweaver.ctl";
char opbx_config_OPBX_SYSTEM_NAME[20] = "";
char opbx_config_OPBX_SOUNDS_DIR[OPBX_CONFIG_MAX_PATH];
char opbx_config_OPBX_ENABLE_UNSAFE_UNLOAD[20] = "";

static char *_argv[256];
static int restart = 0;
static pthread_t consolethread = OPBX_PTHREADT_NULL;

static char random_state[256];

static pthread_t lthread = OPBX_PTHREADT_NULL;


static const char *atexit_registry_obj_name(struct opbx_object *obj)
{
	struct opbx_atexit *it = container_of(obj, struct opbx_atexit, obj);
	return it->name;
}

static int atexit_registry_obj_match(struct opbx_object *obj, const void *pattern)
{
	struct opbx_atexit *it = container_of(obj, struct opbx_atexit, obj);
	return (it->function == pattern);
}

struct opbx_registry atexit_registry = {
	.name = "At Exit",
	.obj_name = atexit_registry_obj_name,
	.obj_match = atexit_registry_obj_match,
	.lock = OPBX_MUTEX_INIT_VALUE,
};

static int opbx_run_atexit_one(struct opbx_object *obj, void *data)
{
	struct opbx_atexit *it = container_of(obj, struct opbx_atexit, obj);
	if (option_verbose > 2)
		opbx_verbose(VERBOSE_PREFIX_3 "atexit: run \"%s\"\n", it->name);
	/* Get the module now so it's pinned (atexits don't hold counted refs
	 * while registered)
	 */
	opbx_module_get(it->obj.module);
	it->function();
	/* We'd prefer not to put the module. If we are running atexits we're
	 * shutting down so there's no need to release modules. However, shutdowns
	 * can be cancelled...
	 */
	opbx_module_put(it->obj.module);
	return 0;
}

static void opbx_run_atexits(void)
{
	opbx_registry_iterate(&atexit_registry, opbx_run_atexit_one, NULL);
}


#if !defined(LOW_MEMORY)

static const char *file_version_registry_obj_name(struct opbx_object *obj)
{
	struct opbx_file_version *it = container_of(obj, struct opbx_file_version, obj);
	return it->file;
}

int file_version_registry_initialized = 0;

struct opbx_registry file_version_registry = {
	.name = "Files",
	.obj_name = file_version_registry_obj_name,
	.lock = OPBX_MUTEX_INIT_VALUE,
};

static char show_version_files_help[] = 
"Usage: show version files [like <pattern>]\n"
"       Shows the revision numbers of the files used to build this copy of CallWeaver.\n"
"       Optional regular expression pattern is used to filter the file list.\n";

/*! CLI command to list module versions */
struct handle_show_version_files_args {
	regex_t regexbuf;
	char *name;
	int fd;
	int havepattern;
	int count_files;
};

#define FORMAT "%-8.*s %.*s\n"

static int handle_show_version_files_one(struct opbx_object *obj, void *data)
{
	struct opbx_file_version *filever = container_of(obj, struct opbx_file_version, obj);
	struct handle_show_version_files_args *args = data;

	if (!((args->name && strcasecmp(filever->file, args->name))
	|| (args->havepattern && regexec(&args->regexbuf, filever->file, 0, NULL, 0)))) {
		char *file, *ver;
		int filelen, verlen;

		for (file = filever->file; isspace(*file); file++);
		filelen = strlen(file);
		if (!strncmp(file, "$HeadURL: ", 10)) {
			file += 10;
			filelen -= 10 + 2;
		}

		for (ver = filever->version; isspace(*ver); ver++);
		verlen = strlen(ver);
		if (!strncmp(ver, "$Revision: ", 11)) {
			ver += 11;
			verlen -= 11 + 2;
		}

		opbx_cli(args->fd, FORMAT, verlen, ver, filelen, file);
		args->count_files++;
		if (args->name)
			return 1;
	}

	return 0;
}

static int handle_show_version_files(int fd, int argc, char *argv[])
{
	struct handle_show_version_files_args args = {
		.fd = fd,
		.name = NULL,
		.havepattern = 0,
		.count_files = 0,
	};

	switch (argc) {
	case 5:
		if (!strcasecmp(argv[3], "like")) {
			if (regcomp(&args.regexbuf, argv[4], REG_EXTENDED | REG_NOSUB))
				return RESULT_SHOWUSAGE;
			args.havepattern = 1;
		} else
			return RESULT_SHOWUSAGE;
		break;
	case 4:
		args.name = argv[3];
		break;
	case 3:
		break;
	default:
		return RESULT_SHOWUSAGE;
	}

	opbx_cli(fd, FORMAT, 8, "Revision", 8, "SVN Path");
	opbx_cli(fd, FORMAT, 8, "--------", 8, "--------");
	opbx_registry_iterate(&file_version_registry, handle_show_version_files_one, &args);

	if (!args.name)
		opbx_cli(fd, "%d files listed.\n", args.count_files);

	if (args.havepattern)
		regfree(&args.regexbuf);

	return RESULT_SUCCESS;
}

#undef FORMAT


struct complete_show_version_files_args {
	char *word;
	int wordlen;
	int state;
	int which;
	char *ret;
};

static int complete_show_version_files_one(struct opbx_object *obj, void *data)
{
	struct opbx_file_version *filever = container_of(obj, struct opbx_file_version, obj);
	struct complete_show_version_files_args *args = data;

	if (!strncasecmp(args->word, filever->file, args->wordlen)) {
		if (++args->which > args->state) {
			args->ret = strdup(filever->file);
			return 1;
		}
	}

	return 0;
}

static char *complete_show_version_files(char *line, char *word, int pos, int state)
{
	struct complete_show_version_files_args args = {
		.word = word,
		.wordlen = strlen(word),
		.state = state,
		.which = 0,
		.ret = NULL,
	};

	if (pos != 3)
		return NULL;

	opbx_registry_iterate(&file_version_registry, complete_show_version_files_one, &args);
	return args.ret;
}

#endif /* ! LOW_MEMORY */


static int fdprint(int fd, const char *s)
{
	return write(fd, s, strlen(s));
}

/*! NULL handler so we can collect the child exit status */
static void null_sig_handler(int signal)
{
}

OPBX_MUTEX_DEFINE_STATIC(safe_system_lock);
static unsigned int safe_system_level = 0;
static struct sigaction safe_system_prev_handler;

int opbx_safe_system(const char *s)
{
    struct sigaction sa;
    pid_t pid;
    int x;
    int res;
    struct rusage rusage;
    int status;
    unsigned int level;

    /* keep track of how many opbx_safe_system() functions
       are running at this moment
    */
    opbx_mutex_lock(&safe_system_lock);
    level = safe_system_level++;

    /* only replace the handler if it has not already been done */
    if (level == 0) {
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_NOCLDSTOP;
        sa.sa_handler = null_sig_handler;
        sigaction(SIGCHLD, &sa, &safe_system_prev_handler);
    }

    opbx_mutex_unlock(&safe_system_lock);

    pid = fork();

    if (pid == 0)
    {
        /* Close file descriptors and launch system command */
        for (x = STDERR_FILENO + 1; x < 4096; x++)
            close(x);
        execl("/bin/sh", "/bin/sh", "-c", s, NULL);
        exit(1);
    }
    else if (pid > 0)
    {
        for (;;)
        {
            res = wait4(pid, &status, 0, &rusage);
            if (res > -1)
            {
                res = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                break;
            }
            else if (errno != EINTR)
                break;
        }
    }
    else
    {
        opbx_log(OPBX_LOG_WARNING, "Fork failed: %s\n", strerror(errno));
        res = -1;
    }

    opbx_mutex_lock(&safe_system_lock);
    level = --safe_system_level;

    /* only restore the handler if we are the last one */
    if (level == 0)
        sigaction(SIGCHLD, &safe_system_prev_handler, NULL);

    opbx_mutex_unlock(&safe_system_lock);

    return res;
}


/*! Urgent handler
 Called by soft_hangup to interrupt the poll, read, or other
 system call.  We don't actually need to do anything though.  
 Remember: Cannot EVER opbx_log from within a signal handler 
 */
static void urg_handler(int num)
{
}

static void child_handler(int sig)
{
	int status;

	/* Reap all dead children -- not just one */
	while (wait3(&status, WNOHANG, NULL) > 0) /* Nothing */;
}

/*! Set an X-term or screen title */
static void set_title(char *text)
{
	char *p;

	if ((p = getenv("TERM")) && strstr(p, "xterm")) {
		fprintf(stderr, "\033]2;%s\007", text);
		fflush(stderr);
	}
}


/*! We set ourselves to a high priority, that we might pre-empt everything
   else.  If your PBX has heavy activity on it, this is a good thing.  */
int opbx_set_priority(int pri)
{
	struct sched_param sched;
	memset(&sched, 0, sizeof(sched));
#ifdef __linux__
	if (pri) {  
		sched.sched_priority = 10;
		if (sched_setscheduler(0, SCHED_RR, &sched)) {
			opbx_log(OPBX_LOG_WARNING, "Unable to set high priority\n");
			return -1;
		} else
			if (option_verbose)
				opbx_verbose("Set to realtime thread\n");
	} else {
		sched.sched_priority = 0;
		if (sched_setscheduler(0, SCHED_OTHER, &sched)) {
			opbx_log(OPBX_LOG_WARNING, "Unable to set normal priority\n");
			return -1;
		}
	}
#else
	if (pri) {
		if (setpriority(PRIO_PROCESS, 0, -10) == -1) {
			opbx_log(OPBX_LOG_WARNING, "Unable to set high priority\n");
			return -1;
		} else
			if (option_verbose)
				opbx_verbose("Set to high priority\n");
	} else {
		if (setpriority(PRIO_PROCESS, 0, 0) == -1) {
			opbx_log(OPBX_LOG_WARNING, "Unable to set normal priority\n");
			return -1;
		}
	}
#endif
	return 0;
}


#define MKSTR(x)	# x
#define UINT_MAX_LEN	sizeof(MKSTR(UINT_MAX))

/* Flag to identify we got a TERM signal */
static int lockfile_got_term = 0;


/* Ignore all signals except for SIGTERM */
static void lockfile_sighandler(int sig)
{
	lockfile_got_term = 1;
}


static void lockfile_release(char *lockfile)
{
	if (unlink(lockfile))
		fprintf(stderr, "lockfile_release: %s: %s", lockfile, strerror(errno));
}


static int lockfile_rewrite(char *lockfile, pid_t pid)
{
	char buf[UINT_MAX_LEN + 1];
	int d, len;
	int err = 0;

	len = sprintf(buf, "%u\n", pid);

	if ((d = open(lockfile, O_WRONLY, 0)) < 0)
		err = errno;
	if (write(d, buf, len) != len && !err)
		err = errno;
	if (ftruncate(d, len) && !err)
		err = errno;
	if (close(d) && !err)
		err = errno;

	errno = err;
	return err;
}


static int lockfile_claim(char *lockfile)
{
	char buf[UINT_MAX_LEN + 1];
	struct sigaction oldsa[3];
	struct sigaction sa;
	struct stat st1, st2;
	char *pidfile;
	int d, len, err = 0, err2;
	int claimed = 0;

	lockfile_got_term = 0;

	sa.sa_handler = lockfile_sighandler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGQUIT, &sa, &oldsa[0]);
	sigaction(SIGTERM, &sa, &oldsa[1]);
	sigaction(SIGINT, &sa, &oldsa[2]);

	pidfile = alloca(strlen(lockfile) + 1 + UINT_MAX_LEN + 1);
	sprintf(pidfile, "%s.%d", lockfile, getpid());

	/* If our pid file exists we failed to unlink it
	 * earlier. If we cannot unlink it now we have
	 * to fail and wait until the fs is fixed - we
	 * can't trust the contents.
	 */
	if (unlink(pidfile) < 0 && errno != ENOENT) {
		fprintf(stderr, "unlink(\"%s\"): %s\n", pidfile, strerror(errno));
		goto out;
	}

	if ((d = open(pidfile, O_WRONLY|O_CREAT, 0600)) < 0) {
		fprintf(stderr, "open(\"%s\"): %s\n", pidfile, strerror(errno));
		goto out;
	}

	/* We need dev/inode for later checks.
	 * Remember: some filesystems (NFS in particular)
	 * may return short writes or errors on close.
	 */
	len = sprintf(buf, "%u\n", getpid());
	err = fstat(d, &st1);
	if (!err) {
		err = write(d, buf, len);
		if (err >= 0 && err != len) {
			errno = ENOSPC;
			err = -1;
		}
	}
	err = (err < 0 ? errno : 0);
	err2 = close(d);
	if (!err && err2 < 0)
		err = errno;
	if (err) {
		fprintf(stderr, "%s: %s\n", pidfile, strerror(err));
		goto out;
	}

	/* Attempt to claim the lock */
	err = link(pidfile, lockfile);
	if (err && errno == EEXIST && !lockfile_got_term) {
		/* The lock file exists - but maybe whoever
		 * owned it died without releasing it?
		 * N.B. This is racey since there is no way
		 * to guarantee we unlink what we opened.
		 * But since it only triggers if something
		 * has already gone wrong it isn't too bad...
		 */
		if ((d = open(lockfile, O_RDONLY, 0)) >= 0) {
			pid_t pid;
			if ((err2 = read(d, buf, sizeof(buf)-1)) > 0
			&& ((buf[err2] = '\0'),(pid = atol(buf))) > 1) {
				if (!kill(pid, 0)) {
					err = EBUSY;
				} else {
					unlink(lockfile);
					fprintf(stderr, "Removed stale lock %s owned by pid %d\n", lockfile, pid);
				}
			}
			close(d);
		}
		if (!lockfile_got_term)
			err = link(pidfile, lockfile);
	}

	if (!err && !lockfile_got_term) {
		claimed = 1;
		goto out;
	}

	if (errno == EEXIST && !lockfile_got_term) {
		/* Use stat to compare inodes since an NFS
		 * server can make the link, die before it
		 * says so then return EXISTS when the client
		 * sends a retry for the response-less request.
		 */
		err = stat(lockfile, &st2);
		if (!err && st1.st_ino == st2.st_ino && st1.st_dev == st2.st_dev) {
			claimed = 1;
			goto out;
		}
	}

	if (lockfile_got_term)
		claimed = -1;

out:
	sigaction(SIGQUIT, &oldsa[0], NULL);
	sigaction(SIGTERM, &oldsa[0], NULL);
	sigaction(SIGINT, &oldsa[0], NULL);
	if (unlink(pidfile) < 0)
		fprintf(stderr, "unlink(\"%s\"): %s\n", pidfile, strerror(errno));
	return claimed;
}


static void quit_handler(void *data)
{
	int local_restart;
	int i;

	/* No more changing your mind. This is definitely what we are going to do. */
	local_restart = restart;

	if (option_verbose)
		opbx_verbose("Executing last minute cleanups\n");

	opbx_cdr_engine_term();

	opbx_run_atexits();

	if (option_verbose && (option_console || option_nofork))
		opbx_verbose("CallWeaver %s ending.\n", opbx_active_channels() ? "uncleanly" : "cleanly");
	if (option_debug)
		opbx_log(OPBX_LOG_DEBUG, "CallWeaver ending.\n");

	manager_event(EVENT_FLAG_SYSTEM, "Shutdown", "Shutdown: %s\r\nRestart: %s\r\n", (opbx_active_channels() ? "Uncleanly" : "Cleanly"), (local_restart ? "True" : "False"));

	if (!pthread_equal(lthread, OPBX_PTHREADT_NULL)) {
		pthread_cancel(lthread);
		unlink(opbx_config_OPBX_SOCKET);
	}

	if (option_verbose || option_console || option_nofork)
		opbx_verbose("%s CallWeaver NOW...\n", (local_restart ? "Restarting" : "Halting"));

	if (!option_remote)
		lockfile_release(opbx_config_OPBX_PID);

	close_logger();

	if (!pthread_equal(consolethread, OPBX_PTHREADT_NULL)) {
		pthread_t tid = consolethread;

		option_reconnect = 0;
		usleep(100000);
		pthread_cancel(tid);
		pthread_join(tid, NULL);
	}

	if (local_restart) {
		/* Mark all FD's for closing on exec */
		for (i = getdtablesize() - 1; i > 2; i--)
			fcntl(i, F_SETFD, FD_CLOEXEC);

		execvp(_argv[0], _argv);
		perror("exec");
		_exit(1);
	}

	exit(0);
}


struct shutdown_state {
	pthread_t tid;
	int nice;
	int timeout;
};

static void shutdown_restart(int fd, int doit, int nice, int timeout);

static void *quit_when_idle(void *data)
{
	struct shutdown_state *state = data;
	time_t s, e;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	time(&e);
	while ((state->timeout == -1 || state->timeout > 0)) {
		int n;

		s = e;
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		n = opbx_active_channels();
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		pthread_testcancel();

		if (!n)
			break;

		usleep(100000);
		if (state->timeout != -1) {
			time(&e);
			state->timeout -= (e - s);
			if (state->timeout < 0)
				state->timeout = 0;
		}

		pthread_testcancel();
	}

	/* Last chance... */
	pthread_testcancel();

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	/* If we ran out of time on a nice shutdown we switch
	 * to a not nice shutdown (which will have a default
	 * timeout applied). Otherwise we are good to initiate
	 * the shutdown/restart.
	 */
	if (state->timeout == 0 && state->nice) {
		opbx_log(OPBX_LOG_NOTICE, "Timeout waiting for idle. Initiating immediate %s\n", (restart ? "restart" : "shutdown"));
		pthread_detach(pthread_self());
		state->tid = OPBX_PTHREADT_NULL;
		shutdown_restart(-1, 1, 0, -1);
	} else {
		opbx_log(OPBX_LOG_NOTICE, "Beginning callweaver %s....\n", restart ? "restart" : "shutdown");
		quit_handler(NULL);
	}

	return NULL;
}


static void shutdown_restart(int fd, int doit, int nice, int timeout)
{
	static opbx_mutex_t lock = OPBX_MUTEX_INIT_VALUE;
	static struct shutdown_state state = {
		.tid = OPBX_PTHREADT_NULL,
		.nice = 0,
		.timeout = 0,
	};

	opbx_mutex_lock(&lock);

	if (doit >= 0) {
		if (!pthread_equal(state.tid, OPBX_PTHREADT_NULL)) {
			pthread_cancel(state.tid);
			pthread_join(state.tid, NULL);
			state.tid = OPBX_PTHREADT_NULL;
		}

		if (doit) {
			if (nice < 2) {
				opbx_begin_shutdown((nice ? 0 : 1));

				if (fd >= 0 && !option_console && fd != STDOUT_FILENO)
					opbx_cli(fd, "Blocked new calls\n");
				if (!option_remote)
					opbx_log(OPBX_LOG_NOTICE, "Blocked new calls\n");

				if (nice < 1) {
					if (fd >= 0 && !option_console && fd != STDOUT_FILENO)
						opbx_cli(fd, "Hanging up active calls\n");
					if (!option_remote)
						opbx_log(OPBX_LOG_NOTICE, "Hanging up active calls\n");
				}
			}

			if (fd >= 0 && !option_console && fd != STDOUT_FILENO)
				opbx_cli(fd, "Will %s when idle...\n", (restart ? "restart" : "shutdown"));
			if (!option_remote)
				opbx_log(OPBX_LOG_NOTICE, "Will %s when idle...\n", (restart ? "restart" : "shutdown"));

			state.nice = nice;
			state.timeout = (nice ? (timeout >= 0 ? timeout : -1 ) : 15);
			opbx_pthread_create(&state.tid, &global_attr_default, quit_when_idle, &state);
		} else {
			if (fd >= 0 && !option_console && fd != STDOUT_FILENO)
				opbx_cli(fd, "%s cancelled\n", (restart ? "restart" : "shutdown"));
			if (!option_remote)
				opbx_log(OPBX_LOG_NOTICE, "%s cancelled\n", (restart ? "restart" : "shutdown"));
		}
	} else {
		if (!pthread_equal(state.tid, OPBX_PTHREADT_NULL)) {
			if (state.timeout == -1)
				opbx_cli(fd, "Pending %s when idle%s\n", (restart ? "restart" : "shutdown"), (state.nice < 2 ? " (new calls blocked)" : ""));
			else
				opbx_cli(fd, "Pending %s in less than %ds%s\n", (restart ? "restart" : "shutdown"), state.timeout, (state.nice < 2 ? " (new calls blocked)" : ""));
		} else
			opbx_cli(fd, "No shutdown or restart pending\n");
	}

	opbx_mutex_unlock(&lock);
}


static char shutdown_restart_status_help[] =
"Usage: stop|restart status\n"
"       Shows status of any pending shutdown or restart\n";

static char shutdown_restart_cancel_help[] = 
"Usage: stop|restart cancel\n"
"       Causes CallWeaver to cancel a pending shutdown or restart, and resume normal\n"
"       call operations.\n";

static char shutdown_now_help[] = 
"Usage: stop now\n"
"       Shuts down a running CallWeaver immediately, hanging up all active calls .\n";

static char shutdown_gracefully_help[] = 
"Usage: stop gracefully [timeout]\n"
"       Causes CallWeaver to not accept new calls, and exit when all\n"
"       active calls have terminated normally.\n"
"       If a timeout is given and CallWeaver is unable to stop in this\n"
"       any seconds an immediate stop will be initiated.\n";

static char shutdown_when_convenient_help[] = 
"Usage: stop when convenient [timeout]\n"
"       Causes CallWeaver to perform a shutdown when all active calls have ended.\n"
"       If a timeout is given and CallWeaver is unable to stop in this\n"
"       any seconds an immediate stop will be initiated.\n";

static char restart_now_help[] = 
"Usage: restart now\n"
"       Causes CallWeaver to hangup all calls and exec() itself performing a cold\n"
"       restart.\n";

static char restart_gracefully_help[] = 
"Usage: restart gracefully [timeout]\n"
"       Causes CallWeaver to stop accepting new calls and exec() itself performing a cold\n"
"       restart when all active calls have ended.\n"
"       If a timeout is given and CallWeaver is unable to stop in this\n"
"       any seconds an immediate stop will be initiated.\n";

static char restart_when_convenient_help[] = 
"Usage: restart when convenient [timeout]\n"
"       Causes CallWeaver to perform a cold restart when all active calls have ended.\n"
"       If a timeout is given and CallWeaver is unable to stop in this\n"
"       any seconds an immediate stop will be initiated.\n";

static char bang_help[] =
"Usage: !<command>\n"
"       Executes a given shell command\n";

/* DEPRECATED */
static char abort_halt_help[] = 
"Usage: abort halt\n"
"       Causes CallWeaver to abort a pending shutdown or restart, and resume normal\n"
"       call operations.\n";


static int handle_shutdown_now(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	restart = 0;
	shutdown_restart(fd, 1, 0 /* Not nice */, -1);
	return RESULT_SUCCESS;
}

static int handle_shutdown_gracefully(int fd, int argc, char *argv[])
{
	int timeout = -1;

	if (argc < 2 || argc > 3)
		return RESULT_SHOWUSAGE;

	if (argc == 3)
		timeout = atoi(argv[2]);

	restart = 0;
	shutdown_restart(fd, 1, 1 /* nicely */, timeout);
	return RESULT_SUCCESS;
}

static int handle_shutdown_when_convenient(int fd, int argc, char *argv[])
{
	int timeout = -1;

	if (argc < 3 || argc > 4)
		return RESULT_SHOWUSAGE;

	if (argc == 4)
		timeout = atoi(argv[3]);

	restart = 0;
	shutdown_restart(fd, 1, 2 /* really nicely */, timeout);
	return RESULT_SUCCESS;
}

static int handle_restart_now(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	restart = 1;
	shutdown_restart(fd, 1, 0 /* not nicely */, -1);
	return RESULT_SUCCESS;
}

static int handle_restart_gracefully(int fd, int argc, char *argv[])
{
	int timeout = -1;

	if (argc < 2 || argc > 3)
		return RESULT_SHOWUSAGE;

	if (argc == 3)
		timeout = atoi(argv[2]);

	restart = 1;
	shutdown_restart(fd, 1, 1 /* nicely */, timeout);
	return RESULT_SUCCESS;
}

static int handle_restart_when_convenient(int fd, int argc, char *argv[])
{
	int timeout = -1;

	if (argc < 3 || argc > 4)
		return RESULT_SHOWUSAGE;

	if (argc == 4)
		timeout = atoi(argv[3]);

	restart = 1;
	shutdown_restart(fd, 1, 2 /* really nicely */, timeout);
	return RESULT_SUCCESS;
}

static int handle_shutdown_restart_cancel(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	opbx_cancel_shutdown();
	shutdown_restart(fd, 0, 0, -1);
	return RESULT_SUCCESS;
}

static int handle_shutdown_restart_status(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	shutdown_restart(fd, -1, 0, 0);
	return RESULT_SUCCESS;
}

static int handle_bang(int fd, int argc, char *argv[])
{
	return RESULT_SUCCESS;
}

#define CALLWEAVER_PROMPT "*CLI> "

#define CALLWEAVER_PROMPT2 "%s*CLI> "

static struct opbx_clicmd core_cli[] = {
	{
		.cmda = { "stop", "status", NULL },
		.handler = handle_shutdown_restart_status,
		.summary = "Show status of any pending stop or restart",
		.usage = shutdown_restart_status_help,
	},
	{
		.cmda = { "restart", "status", NULL },
		.handler = handle_shutdown_restart_status,
		.summary = "Show status of any pending stop or restart",
		.usage = shutdown_restart_status_help,
	},
	{
		.cmda = { "stop", "cancel", NULL },
		.handler = handle_shutdown_restart_cancel,
		.summary = "Cancel a pending stop or restart request",
		.usage = shutdown_restart_cancel_help,
	},
	{
		.cmda = { "restart", "cancel", NULL },
		.handler = handle_shutdown_restart_cancel,
		.summary = "Cancel a pending stop or restart request",
		.usage = shutdown_restart_cancel_help,
	},
	{
		.cmda = { "stop", "now", NULL },
		.handler = handle_shutdown_now,
		.summary = "Shut down CallWeaver immediately hanging up any in-progress calls",
		.usage = shutdown_now_help,
	},
	{
		.cmda = { "stop", "gracefully", NULL },
		.handler = handle_shutdown_gracefully,
		.summary = "Block new calls and shut down CallWeaver when current calls have ended",
		.usage = shutdown_gracefully_help,
	},
	{
		.cmda = { "stop", "when","convenient", NULL },
		.handler = handle_shutdown_when_convenient,
		.summary = "Shut down CallWeaver when there are no calls in progress",
		.usage = shutdown_when_convenient_help,
	},
	{
		.cmda = { "restart", "now", NULL },
		.handler = handle_restart_now,
		.summary = "Restart CallWeaver immediately hanging up any in-progress calls",
		.usage = restart_now_help,
	},
	{
		.cmda = { "restart", "gracefully", NULL },
		.handler = handle_restart_gracefully,
		.summary = "Block new calls and restart CallWeaver when current calls have ended",
		.usage = restart_gracefully_help,
	},
	{
		.cmda = { "restart", "when", "convenient", NULL },
		.handler = handle_restart_when_convenient,
		.summary = "Restart CallWeaver when there are no calls in progress",
		.usage = restart_when_convenient_help,
	},
	{
		.cmda = { "!", NULL },
		.handler = handle_bang,
		.summary = "Execute a shell command",
		.usage = bang_help,
	},
#if !defined(LOW_MEMORY)
	{
		.cmda = { "show", "version", "files", NULL },
		.handler = handle_show_version_files,
		.generator = complete_show_version_files,
		.summary = "Show versions of files used to build CallWeaver",
		.usage = show_version_files_help,
	},
#endif /* ! LOW_MEMORY */

	/* DEPRECATED */
	{
		.cmda = { "abort", "halt", NULL },
		.handler = handle_shutdown_restart_cancel,
		.summary = "Cancel a pending stop or restart request",
		.usage = abort_halt_help,
	},
};


static int console_sock;


static char *cli_prompt(void)
{
	static char prompt[200];
	char *pfmt;
	int color_used = 0;
#if 0
	char term_code[20];
#endif

	if ((pfmt = getenv("CALLWEAVER_PROMPT"))) {
		char *t = pfmt, *p = prompt;
		memset(prompt, 0, sizeof(prompt));
		while (*t != '\0' && *p < sizeof(prompt)) {
			if (*t == '%') {
#if 0
				int i;
#endif
				struct timeval tv;
				struct tm tm;
#ifdef linux
				FILE *LOADAVG;
#endif

				t++;
				switch (*t) {
					case 'C': /* color */
						t++;
#if 0
						if (sscanf(t, "%d;%d%n", &fgcolor, &bgcolor, &i) == 2) {
							strncat(p, opbx_term_color_code(term_code, fgcolor, bgcolor, sizeof(term_code)),sizeof(prompt) - strlen(prompt) - 1);
							t += i - 1;
						} else if (sscanf(t, "%d%n", &fgcolor, &i) == 1) {
							strncat(p, opbx_term_color_code(term_code, fgcolor, 0, sizeof(term_code)),sizeof(prompt) - strlen(prompt) - 1);
							t += i - 1;
						}

						/* If the color has been reset correctly, then there's no need to reset it later */
						if ((fgcolor == COLOR_WHITE) && (bgcolor == COLOR_BLACK)) {
							color_used = 0;
						} else {
							color_used = 1;
						}
#endif
						break;
					case 'd': /* date */
						memset(&tm, 0, sizeof(struct tm));
						tv = opbx_tvnow();
						if (localtime_r(&(tv.tv_sec), &tm)) {
							strftime(p, sizeof(prompt) - strlen(prompt), "%Y-%m-%d", &tm);
						}
						break;
					case 'h': /* hostname */
						strncat(p, hostname, sizeof(prompt) - strlen(prompt) - 1);
						break;
					case 'H': /* short hostname */
						strncat(p, shorthostname, sizeof(prompt) - strlen(prompt) - 1);
						break;
#ifdef linux
					case 'l': /* load avg */
						t++;
						if ((LOADAVG = fopen("/proc/loadavg", "r"))) {
							float avg1, avg2, avg3;
							int actproc, totproc, npid, which;
							fscanf(LOADAVG, "%f %f %f %d/%d %d",
								&avg1, &avg2, &avg3, &actproc, &totproc, &npid);
							if (sscanf(t, "%d", &which) == 1) {
								switch (which) {
									case 1:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%.2f", avg1);
										break;
									case 2:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%.2f", avg2);
										break;
									case 3:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%.2f", avg3);
										break;
									case 4:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%d/%d", actproc, totproc);
										break;
									case 5:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%d", npid);
										break;
								}
							}
						}
						break;
#endif
					case 't': /* time */
						memset(&tm, 0, sizeof(struct tm));
						tv = opbx_tvnow();
						if (localtime_r(&(tv.tv_sec), &tm)) {
							strftime(p, sizeof(prompt) - strlen(prompt), "%H:%M:%S", &tm);
						}
						break;
					case '#': /* process console or remote? */
						if (! option_remote) {
							strncat(p, "#", sizeof(prompt) - strlen(prompt) - 1);
						} else {
							strncat(p, ">", sizeof(prompt) - strlen(prompt) - 1);
						}
						break;
					case '%': /* literal % */
						strncat(p, "%", sizeof(prompt) - strlen(prompt) - 1);
						break;
					case '\0': /* % is last character - prevent bug */
						t--;
						break;
				}
				while (*p != '\0') {
					p++;
				}
				t++;
			} else {
				*p = *t;
				p++;
				t++;
			}
		}
		if (color_used) {
			/* Force colors back to normal at end */
#if 0
			opbx_term_color_code(term_code, COLOR_WHITE, COLOR_BLACK, sizeof(term_code));
			if (strlen(term_code) > sizeof(prompt) - strlen(prompt)) {
				strncat(prompt + sizeof(prompt) - strlen(term_code) - 1, term_code, strlen(term_code));
			} else {
				strncat(p, term_code, sizeof(term_code));
			}
#endif
		}
	} else if (remotehostname)
		snprintf(prompt, sizeof(prompt), CALLWEAVER_PROMPT2, remotehostname);
	else
		snprintf(prompt, sizeof(prompt), CALLWEAVER_PROMPT);

	return (prompt);	
}

static char **opbx_rl_strtoarr(char *buf)
{
	char **match_list = NULL, *retstr;
	size_t match_list_len;
	int matches = 0;

	match_list_len = 1;
	while ( (retstr = strsep(&buf, " ")) != NULL) {

		if (!strcmp(retstr, OPBX_CLI_COMPLETE_EOF))
			break;
		if (matches + 1 >= match_list_len) {
			match_list_len <<= 1;
			match_list = realloc(match_list, match_list_len * sizeof(char *));
		}

		match_list[matches++] = strdup(retstr);
	}

	if (!match_list)
		return (char **) NULL;

	if (matches>= match_list_len)
		match_list = realloc(match_list, (match_list_len + 1) * sizeof(char *));

	match_list[matches] = (char *) NULL;

	return match_list;
}

static char *dummy_completer(char *text, int state)
{
    return (char*)NULL;
}

static char **cli_completion(const char *text, int start, int end)
{
    int nummatches = 0;
    char buf[2048];
    char **matches;
    int res;

    matches = (char**)NULL;
    if (option_remote)
    {
        snprintf(buf, sizeof(buf), "_COMMAND NUMMATCHES \"%s\" \"%s\"", (char *)rl_line_buffer, (char *)text);
        fdprint(console_sock, buf);
        res = read(console_sock, buf, sizeof(buf));
        buf[res] = '\0';
        nummatches = atoi(buf);

        if (nummatches > 0)
        {
            char *mbuf;
            int mlen = 0, maxmbuf = 2048;
            // Start with a 2048 byte buffer
            mbuf = malloc(maxmbuf);
            if (!mbuf)
                return (matches);

            snprintf(buf, sizeof(buf),"_COMMAND MATCHESARRAY \"%s\" \"%s\"", (char *)rl_line_buffer, (char *)text);
            fdprint(console_sock, buf);
            res = 0;
            mbuf[0] = '\0';

            while (!strstr(mbuf, OPBX_CLI_COMPLETE_EOF) && res != -1)
            {
                if (mlen + 1024 > maxmbuf)
                {
                    // Every step increment buffer 1024 bytes
                    maxmbuf += 1024;
                    mbuf = realloc(mbuf, maxmbuf);
                    if (!mbuf)
                        return (matches);
                }
                // Only read 1024 bytes at a time
                res = read(console_sock, mbuf + mlen, 1024);
                if (res > 0)
                    mlen += res;
            }
            mbuf[mlen] = '\0';

            matches = opbx_rl_strtoarr(mbuf);
            free(mbuf);
        }

    }
    else
    {
        nummatches = opbx_cli_generatornummatches((char *)rl_line_buffer, (char*)text);

        if (nummatches > 0 )
            matches = opbx_cli_completion_matches((char*)rl_line_buffer, (char*)text);
    }
    return (matches);
}


static void welcome_message(void)
{
#ifndef RELEASE_TARBALL
	static const char msg[] = PACKAGE_STRING " SVN-" SVN_VERSION " http://www.callweaver.org - The True Open Source PBX\n";
#else
	static const char msg[] = PACKAGE_STRING " http://www.callweaver.org - The True Open Source PBX\n";
#endif
	const char *p;

	fputs(msg, stdout);
	for (p = msg; *p != '\n'; p++)
		putc('=', stdout);
	putc('\n', stdout);
}


static void console_cleanup(void *data)
{
	char filename[80];
	char *p;

	rl_callback_handler_remove();
	terminal_write("\r\n", 2);
	fflush(stdout);
	set_title("");

	if ((p = getenv("HOME"))) {
		snprintf(filename, sizeof(filename), "%s/.callweaver_history", getenv("HOME"));
		write_history(filename);
	}

	consolethread = OPBX_PTHREADT_NULL;
}


static void console_handler(char *s)
{
	if (s) {
		while (isspace(*s)) s++;

		if (*s) {
			HIST_ENTRY *last_he;
    
			last_he = previous_history();
			if (!last_he || strcmp(last_he->line, s) != 0)
				add_history(s);

			if (s[0] == '!') {
				if (s[1])
					opbx_safe_system(s+1);
				else
					opbx_safe_system(getenv("SHELL") ? getenv("SHELL") : "/bin/sh");
			} else if (option_remote && (!strcasecmp(s, "quit") || !strcasecmp(s, "exit"))) {
				console_cleanup(NULL);
				exit(0);
			} else {
				if (write(console_sock, s, strlen(s) + 1) < 1) {
					opbx_log(OPBX_LOG_WARNING, "Unable to write: %s\n", strerror(errno));
					pthread_detach(pthread_self());
					pthread_cancel(pthread_self());
				}
			}
		}
	} else if (option_remote) {
		console_cleanup(NULL);
		exit(0);
	} else {
		shutdown(console_sock, SHUT_WR);
		putc('\n', stdout);
	}
}


static int opbx_tryconnect(char *spec)
{
	union {
		struct sockaddr sa;
		struct sockaddr_un sun;
	} u;
	socklen_t salen;
	int s;

	memset(&u, 0, sizeof(u));
	u.sun.sun_family = AF_LOCAL;
	opbx_copy_string(u.sun.sun_path, spec, sizeof(u.sun.sun_path));
	salen = sizeof(u.sun);

	if ((s = socket(u.sa.sa_family, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "Unable to create socket: %s\n", strerror(errno));
	} else if (connect(s, &u.sa, salen)) {
		close(s);
		s = -1;
	} else {
		fcntl(s, F_SETFD, fcntl(s, F_GETFD, 0) | FD_CLOEXEC);
	}

	return s;
}


static void *console(void *data)
{
	char buf[1024];
	char banner[80];
	sigset_t sigs;
	char *spec = data;
	char *clr_eol;
	char *stringp;
	char *version;
	char *p;
	int update_delay;
	int res;
	int pid;

	console_sock = -1;

	terminal_init();
	terminal_set_icon("Callweaver");

	sigemptyset(&sigs);
	sigaddset(&sigs, SIGWINCH);
	pthread_sigmask(SIG_UNBLOCK, &sigs, NULL);

	pthread_cleanup_push(console_cleanup, NULL);

	rl_initialize ();
	rl_editing_mode = 1;
	rl_completion_entry_function = (void *)dummy_completer; /* The typedef varies between platforms */
	rl_attempted_completion_function = (CPPFunction *)cli_completion;

	/* Setup history with 100 entries */
	using_history();
	stifle_history(100);

	clr_eol = rl_get_termcap("ce");

	if ((p = getenv("HOME"))) {
		snprintf(buf, sizeof(buf), "%s/.callweaver_history", p);
		read_history(buf);
	}

	do {
		const int reconnects_per_second = 20;
		int tries;

		welcome_message();

		fprintf(stderr, "Connecting to Callweaver at %s...\n", spec);
		for (tries = 0; console_sock < 0 && tries < 30 * reconnects_per_second; tries++) {
			if ((console_sock = opbx_tryconnect(spec)) < 0) {
				pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
				pthread_testcancel();
				usleep(1000000 / reconnects_per_second);
				pthread_testcancel();
				pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
			}
		}
		if (console_sock < 0) {
			fprintf(stderr, "Failed to connect in 30 seconds. Quitting.\n");
			break;
		}
		fprintf(stderr, "Connect succeeded after %.3f seconds\n", 1.0 / reconnects_per_second * tries);

		/* Read the welcome line that contains hostname, version and pid */
		read(console_sock, banner, sizeof(banner));

		/* Make sure verbose and debug settings are what we want or higher */
		res = snprintf(buf, sizeof(buf), "set verbose atleast %d", option_verbose);
		write(console_sock, buf, (res <= sizeof(buf) ? res : sizeof(buf)) + 1);
		res = snprintf(buf, sizeof(buf), "set debug atleast %d", option_debug);
		write(console_sock, buf, (res <= sizeof(buf) ? res : sizeof(buf)) + 1);

		stringp = banner;
		remotehostname = strsep(&stringp, "/");
		p = strsep(&stringp, "/");
		version = strsep(&stringp, "\n");
		if (!version)
			version = "<Version Unknown>";
		stringp = remotehostname;
		strsep(&stringp, ".");
		pid = (p ? atoi(p) : -1);

		snprintf(buf, sizeof(buf), "%s on %s (pid %d)", version, remotehostname, pid);
		set_title(buf);
		fprintf(stdout, "Connected to %s currently running on %s (pid = %d)\n", version, remotehostname, pid);

		update_delay = -1;

		if (option_console || option_remote)
			rl_callback_handler_install(cli_prompt(), console_handler);

		for (;;) {
			struct pollfd pfd[2];
			int ret;

			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
			pthread_testcancel();

			pfd[0].fd = console_sock;
			pfd[1].fd = fileno(stdin);
			pfd[0].events = pfd[1].events = POLLIN;
			pfd[0].revents = pfd[1].revents = 0;

			ret = poll(pfd, (option_console || option_remote ? 2 : 1), update_delay);

			pthread_testcancel();
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

			if (ret == 0) {
				if (update_delay >= 0) {
					rl_forced_update_display();
					update_delay = -1;
				}
			} else if (ret >= 0) {
				if (pfd[0].revents) {
					if ((ret = read(console_sock, buf, sizeof(buf) - 1)) > 0) {
						if (update_delay < 0 && (option_console || option_remote)) {
							if (clr_eol) {
								terminal_write("\r", 1);
								fputs(clr_eol, stdout);
							} else
								terminal_write("\r\n", 2);
						}

						terminal_write(buf, ret);

						/* If we have clear to end of line we can redisplay the input line
						 * every time the output ends in a new line. Otherwise we want to
						 * wait and see if there's more output coming because we don't
						 * have any way of backing up and replacing the current line.
						 * Of course, if we don't care about input there's no problem...
						 */
						if (option_console || option_remote) {
							if (clr_eol && buf[ret - 1] == '\n') {
								rl_forced_update_display();
								update_delay = -1;
							} else
								update_delay = 100;
						}
						fflush(stdout);
					} else
						break;
				}

				if (pfd[1].revents) {
					if (update_delay >= 0) {
						rl_forced_update_display();
						update_delay = -1;
					}
					rl_callback_read_char();
				}
			} else if (errno != EINTR) {
				perror("poll");
				break;
			}
		}

		rl_callback_handler_remove();
		fflush(stdout);
		fprintf(stderr, "\nDisconnected from CallWeaver server\n");
		set_title("");
		close(console_sock);
		console_sock = -1;
	} while (option_reconnect);

	pthread_cleanup_pop(1);
	return NULL;
}


static int console_oneshot(char *spec, char *cmd)
{
	char buf[1024];
	int s, n;

	if ((s = opbx_tryconnect(spec)) >= 0) {
		/* Dump the connection banner. We don't need it here */
		read(s, buf, sizeof(buf));

		write(s, cmd, strlen(cmd) + 1);
		shutdown(s, SHUT_WR);

		while ((n = read(s, buf, sizeof(buf))) > 0)
			write(STDOUT_FILENO, buf, n);
		close(s);
		n = 0;
	} else {
		fprintf(stderr, "Unable to connect to Callweaver at %s\n", spec);
		n = 1;
	}

	return n;
}


static void boot(void)
{
	if ((option_console || option_nofork) && !option_verbose) 
		opbx_verbose("[ Booting...");

	/* ensure that the random number generators are seeded with a different value every time
	   CallWeaver is started
	*/
	srand((unsigned int) getpid() + (unsigned int) time(NULL));
	srandom((unsigned int) getpid() + (unsigned int) time(NULL));

	if (init_logger()
	|| opbx_crypto_init()
	|| opbx_loader_cli_init()
	|| load_modules(1)
	|| opbx_channels_init()
	|| opbx_cdr_engine_init()
	|| init_manager()
	|| opbx_device_state_engine_init()
	|| opbx_rtp_init()
	|| opbx_udptl_init()
	|| opbx_stun_init()
	|| direngine_list_init()
	|| opbx_image_init()
	|| opbx_file_init()
	|| load_pbx()
	|| opbxdb_init()
	|| init_framer()
	|| load_modules(0)
	|| opbx_enum_init()
	|| opbx_translator_init()) {
	    exit(1);
	}

#ifdef __OPBX_DEBUG_MALLOC
	__opbx_mm_init();
#endif	
	opbx_cli_register_multiple(core_cli, arraysize(core_cli));

	if ((option_console || option_nofork) && !option_verbose)
		opbx_verbose(" ]\n");

	time(&opbx_startuptime);
	if (option_verbose || option_console || option_nofork)
		opbx_verbose("CallWeaver Ready\n");

	fully_booted = 1;
}


/*!
 * write the string to the console, and all attached
 * console clients
 */
void opbx_console_puts(const char *string)
{
	int i;

	for (i = 0; i < arraysize(consoles); i++) {
		if (consoles[i].fd > -1)
			fdprint(consoles[i].p[1], string);
	}
}


static void network_verboser(const char *s, int pos, int replace, int complete)
	/* ARGUSED */
{
	char *t;

	if (replace) {
		t = alloca(strlen(s) + 2);
		sprintf(t, "\r%s", s);
		s = t;
	}
	if (complete)
		opbx_console_puts(s);
}


static void *netconsole(void *vconsole)
{
	struct console *con = vconsole;
	char tmp[512];
	int res;
	struct pollfd fds[2];

	for(;;) {
		fds[0].fd = con->fd;
		fds[0].events = POLLIN;
		fds[0].revents = 0;
		fds[1].fd = con->p[0];
		fds[1].events = POLLIN;
		fds[1].revents = 0;

		res = poll(fds, 2, -1);
		if (res < 0) {
			if (errno != EINTR)
				opbx_log(OPBX_LOG_WARNING, "poll returned < 0: %s\n", strerror(errno));
			sleep(1);
			continue;
		}
		if (fds[0].revents) {
			res = read(con->fd, tmp, sizeof(tmp));
			if (res < 1) {
				break;
			}
			tmp[res] = 0;
			opbx_cli_command(con->fd, tmp);
		}
		if (fds[1].revents) {
			res = read(con->p[0], tmp, sizeof(tmp));
			if (res < 1) {
				opbx_log(OPBX_LOG_ERROR, "read returned %d\n", res);
				break;
			}
			res = write(con->fd, tmp, res);
			if (res < 1)
				break;
		}
	}
	if (option_verbose > 2)
		opbx_verbose(VERBOSE_PREFIX_3 "Remote UNIX connection disconnected\n");
	close(con->fd);
	close(con->p[0]);
	close(con->p[1]);
	con->fd = -1;
	return NULL;
}

static void *listener(void *data)
{
	char buf[80];
	int sock = (int)data;
	int n, s;
	int x;
	struct pollfd fds[1];

	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		fds[0].fd = sock;
		fds[0].events= POLLIN;
		pthread_testcancel();
		n = poll(fds, 1, -1);
		x = errno;
		pthread_testcancel();
		if (n > 0)
			s = accept(sock, NULL, NULL);

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (n <= 0) {
			if (x != EINTR)
				opbx_log(OPBX_LOG_WARNING, "poll returned %d error: %s\n", n, strerror(x));
		} else if (s < 0) {
			if (x != EINTR)
				opbx_log(OPBX_LOG_WARNING, "Accept returned %d: %s\n", s, strerror(x));
		} else {
			for (x = 0; x < arraysize(consoles); x++) {
				if (consoles[x].fd < 0) {
					if (socketpair(AF_LOCAL, SOCK_STREAM, 0, consoles[x].p)) {
						opbx_log(OPBX_LOG_ERROR, "Unable to create pipe: %s\n", strerror(errno));
						consoles[x].fd = -1;
						close(s);
						break;
					}

#ifndef RELEASE_TARBALL	
					n = snprintf(buf, sizeof(buf), "%s/%d/%s\n", hostname, opbx_mainpid,  PACKAGE_STRING " SVN-" SVN_VERSION );
#else
					n = snprintf(buf, sizeof(buf), "%s/%d/%s\n", hostname, opbx_mainpid,  PACKAGE_STRING );
#endif
					write(s, buf, n);

					consoles[x].fd = s;
					fcntl(s, F_SETFD, fcntl(s, F_GETFD, 0) | FD_CLOEXEC);
					fcntl(consoles[x].p[0], F_SETFD, fcntl(consoles[x].p[0], F_GETFD, 0) | FD_CLOEXEC);
					fcntl(consoles[x].p[1], F_SETFD, fcntl(consoles[x].p[1], F_GETFD, 0) | FD_CLOEXEC);
					fcntl(consoles[x].p[1], F_SETFL, fcntl(consoles[x].p[1], F_GETFL) | O_NONBLOCK);
					if (opbx_pthread_create(&consoles[x].t, &global_attr_detached, netconsole, &consoles[x])) {
						opbx_log(OPBX_LOG_ERROR, "Unable to spawn thread to handle connection: %s\n", strerror(errno));
						consoles[x].fd = -1;
						close(s);
					} else if (!fully_booted)
						boot();
					break;
				}
			}
			if (x >= arraysize(consoles)) {
				opbx_log(OPBX_LOG_WARNING, "No more connections allowed\n");
				close(s);
			} else if (consoles[x].fd > -1) {
				if (option_verbose > 2) 
					opbx_verbose(VERBOSE_PREFIX_3 "Remote UNIX connection\n");
			}
		}
	}
	return NULL;
}

static int opbx_makesocket(char *spec)
{
	union {
		struct sockaddr sa;
		struct sockaddr_un sun;
	} u;
	socklen_t salen;
	uid_t uid = -1;
	gid_t gid = -1;
	int sock;

	memset(&u, 0, sizeof(u));
	u.sun.sun_family = AF_LOCAL;
	opbx_copy_string(u.sun.sun_path, spec, sizeof(u.sun.sun_path));
	salen = sizeof(u.sun);

	if ((sock = socket(u.sa.sa_family, SOCK_STREAM, 0)) < 0) {
		opbx_log(OPBX_LOG_WARNING, "Unable to create socket for %s: %s\n", spec, strerror(errno));
		return -1;
	}		
	if (bind(sock, &u.sa, salen)) {
		opbx_log(OPBX_LOG_WARNING, "Unable to bind socket to %s: %s\n", spec, strerror(errno));
		close(sock);
		return -1;
	}
	if (listen(sock, 1024) < 0) {
		opbx_log(OPBX_LOG_WARNING, "Unable to listen on socket %s: %s\n", spec, strerror(errno));
		close(sock);
		return -1;
	}

	fcntl(sock, F_SETFD, fcntl(sock, F_GETFD, 0) | FD_CLOEXEC);

	opbx_pthread_create(&lthread, &global_attr_default, listener, (void *)sock);

	if (u.sa.sa_family == AF_LOCAL) {
		if (!opbx_strlen_zero(opbx_config_OPBX_CTL_OWNER)) {
			struct passwd *pw;
			if ((pw = getpwnam(opbx_config_OPBX_CTL_OWNER)) == NULL)
				opbx_log(OPBX_LOG_WARNING, "Unable to find uid of user %s\n", opbx_config_OPBX_CTL_OWNER);
			else
				uid = pw->pw_uid;
		}
		
		if (!opbx_strlen_zero(opbx_config_OPBX_CTL_GROUP)) {
			struct group *grp;
			if ((grp = getgrnam(opbx_config_OPBX_CTL_GROUP)) == NULL)
				opbx_log(OPBX_LOG_WARNING, "Unable to find gid of group %s\n", opbx_config_OPBX_CTL_GROUP);
			else
				gid = grp->gr_gid;
		}

		if (chown(opbx_config_OPBX_SOCKET, uid, gid) < 0)
			opbx_log(OPBX_LOG_WARNING, "Unable to change ownership of %s: %s\n", spec, strerror(errno));

		if (!opbx_strlen_zero(opbx_config_OPBX_CTL_PERMISSIONS)) {
			mode_t p;
			sscanf(opbx_config_OPBX_CTL_PERMISSIONS, "%o", (int *) &p);
			if ((chmod(spec, p)) < 0)
				opbx_log(OPBX_LOG_WARNING, "Unable to change file permissions of %s: %s\n", spec, strerror(errno));
		}
	}

	return 0;
}


static int show_version(void)
{
	#ifndef RELEASE_TARBALL
	printf( PACKAGE_STRING " SVN-" SVN_VERSION "\n");
	#else
	printf( PACKAGE_STRING "\n");
	#endif

	return 0;
}

static int show_cli_help(void) {
	#ifndef RELEASE_TARBALL
	printf( PACKAGE_STRING " SVN-" SVN_VERSION  "\n");
	#else
	printf( PACKAGE_STRING "\n");
	#endif
	printf("Usage: callweaver [OPTIONS]\n");
	printf("Valid Options:\n");
	printf("   -V              Display version number and exit\n");
	printf("   -C <configfile> Use an alternate configuration file\n");
	printf("   -G <group>      Run as a group other than the caller\n");
	printf("   -U <user>       Run as a user other than the caller\n");
	printf("   -c              Provide console CLI\n");
	printf("   -d              Enable extra debugging\n");
	printf("   -f              Do not fork\n");
	printf("   -g              Dump core in case of a crash\n");
	printf("   -h              This help screen\n");
	printf("   -i              Initialize crypto keys at startup\n");
	printf("   -n              Disable console colorization at startup or console (not remote)\n");
	printf("   -p              Run as pseudo-realtime thread\n");
	printf("   -q              Quiet mode (suppress output)\n");
	printf("   -r              Connect to CallWeaver on this machine\n");
	printf("   -R              Connect to CallWeaver, and attempt to reconnect if disconnected\n");
	printf("   -t              Record soundfiles in /var/tmp and move them where they belong after they are done.\n");
	printf("   -T              Display the time in [Mmm dd hh:mm:ss] format for each line of output to the CLI.\n");
	printf("   -v              Increase verbosity (multiple v's = more verbose)\n");
	printf("   -x <cmd>        Execute command <cmd> (only valid with -r)\n");
	printf("\n");
	return 0;
}

static void opbx_readconfig(void) {
	struct opbx_config *cfg;
	struct opbx_variable *v;
	char *config = opbx_config_OPBX_CONFIG_FILE;

	if (option_overrideconfig == 1) {
		cfg = opbx_config_load(opbx_config_OPBX_CONFIG_FILE);
		if (!cfg)
			opbx_log(OPBX_LOG_WARNING, "Unable to open specified master config file '%s', using built-in defaults\n", opbx_config_OPBX_CONFIG_FILE);
	} else {
		cfg = opbx_config_load(config);
	}

	/* init with buildtime config */

	opbx_copy_string(opbx_config_OPBX_RUN_USER, opbxrunuser_default, sizeof(opbx_config_OPBX_RUN_USER));
	opbx_copy_string(opbx_config_OPBX_RUN_GROUP, opbxrungroup_default, sizeof(opbx_config_OPBX_RUN_GROUP));
	opbx_copy_string(opbx_config_OPBX_CONFIG_DIR, opbxconfdir_default, sizeof(opbx_config_OPBX_CONFIG_DIR));
	opbx_copy_string(opbx_config_OPBX_SPOOL_DIR, opbxspooldir_default, sizeof(opbx_config_OPBX_SPOOL_DIR));
	lt_dlsetsearchpath(opbxmoddir_default);
 	snprintf(opbx_config_OPBX_MONITOR_DIR, sizeof(opbx_config_OPBX_MONITOR_DIR) - 1, "%s/monitor", opbx_config_OPBX_SPOOL_DIR);
	opbx_copy_string(opbx_config_OPBX_VAR_DIR, opbxvardir_default, sizeof(opbx_config_OPBX_VAR_DIR));
	opbx_copy_string(opbx_config_OPBX_LOG_DIR, opbxlogdir_default, sizeof(opbx_config_OPBX_LOG_DIR));
	opbx_copy_string(opbx_config_OPBX_OGI_DIR, opbxogidir_default, sizeof(opbx_config_OPBX_OGI_DIR));
	opbx_copy_string(opbx_config_OPBX_DB, opbxdbfile_default, sizeof(opbx_config_OPBX_DB));
	opbx_copy_string(opbx_config_OPBX_DB_DIR, opbxdbdir_default, sizeof(opbx_config_OPBX_DB_DIR));
	opbx_copy_string(opbx_config_OPBX_KEY_DIR, opbxkeydir_default, sizeof(opbx_config_OPBX_KEY_DIR));
	opbx_copy_string(opbx_config_OPBX_PID, opbxpidfile_default, sizeof(opbx_config_OPBX_PID));
	opbx_copy_string(opbx_config_OPBX_SOCKET, opbxsocketfile_default, sizeof(opbx_config_OPBX_SOCKET));
	opbx_copy_string(opbx_config_OPBX_RUN_DIR, opbxrundir_default, sizeof(opbx_config_OPBX_RUN_DIR));
	opbx_copy_string(opbx_config_OPBX_SOUNDS_DIR, opbxsoundsdir_default, sizeof(opbx_config_OPBX_SOUNDS_DIR));

	/* no callweaver.conf? no problem, use buildtime config! */
	if (!cfg) {
		return;
	}
	v = opbx_variable_browse(cfg, "general");
	while (v) {
		if (!strcasecmp(v->name, "cwrunuser")) {
			opbx_copy_string(opbx_config_OPBX_RUN_USER, v->value, sizeof(opbx_config_OPBX_RUN_USER));
		} else if (!strcasecmp(v->name, "cwrungroup")) {
			opbx_copy_string(opbx_config_OPBX_RUN_GROUP, v->value, sizeof(opbx_config_OPBX_RUN_GROUP));
		}
		v = v->next;
	}
	v = opbx_variable_browse(cfg, "files");
	while (v) {
		if (!strcasecmp(v->name, "cwctlpermissions")) {
			opbx_copy_string(opbx_config_OPBX_CTL_PERMISSIONS, v->value, sizeof(opbx_config_OPBX_CTL_PERMISSIONS));
		} else if (!strcasecmp(v->name, "cwctlowner")) {
			opbx_copy_string(opbx_config_OPBX_CTL_OWNER, v->value, sizeof(opbx_config_OPBX_CTL_OWNER));
		} else if (!strcasecmp(v->name, "cwctlgroup")) {
			opbx_copy_string(opbx_config_OPBX_CTL_GROUP, v->value, sizeof(opbx_config_OPBX_CTL_GROUP));
		} else if (!strcasecmp(v->name, "cwctl")) {
			opbx_copy_string(opbx_config_OPBX_CTL, v->value, sizeof(opbx_config_OPBX_CTL));
		} else if (!strcasecmp(v->name, "cwdb")) {
			opbx_copy_string(opbx_config_OPBX_DB, v->value, sizeof(opbx_config_OPBX_DB));
		}
		v = v->next;
	}
	v = opbx_variable_browse(cfg, "directories");
	while(v) {
		if (!strcasecmp(v->name, "cwetcdir")) {
			opbx_copy_string(opbx_config_OPBX_CONFIG_DIR, v->value, sizeof(opbx_config_OPBX_CONFIG_DIR));
		} else if (!strcasecmp(v->name, "cwspooldir")) {
			opbx_copy_string(opbx_config_OPBX_SPOOL_DIR, v->value, sizeof(opbx_config_OPBX_SPOOL_DIR));
			snprintf(opbx_config_OPBX_MONITOR_DIR, sizeof(opbx_config_OPBX_MONITOR_DIR) - 1, "%s/monitor", v->value);
		} else if (!strcasecmp(v->name, "cwvarlibdir")) {
			opbx_copy_string(opbx_config_OPBX_VAR_DIR, v->value, sizeof(opbx_config_OPBX_VAR_DIR));
		} else if (!strcasecmp(v->name, "cwdbdir")) {
			opbx_copy_string(opbx_config_OPBX_DB_DIR, v->value, sizeof(opbx_config_OPBX_DB_DIR));
		} else if (!strcasecmp(v->name, "cwlogdir")) {
			opbx_copy_string(opbx_config_OPBX_LOG_DIR, v->value, sizeof(opbx_config_OPBX_LOG_DIR));
		} else if (!strcasecmp(v->name, "cwogidir")) {
			opbx_copy_string(opbx_config_OPBX_OGI_DIR, v->value, sizeof(opbx_config_OPBX_OGI_DIR));
		} else if (!strcasecmp(v->name, "cwsoundsdir")) {
			opbx_copy_string(opbx_config_OPBX_SOUNDS_DIR, v->value, sizeof(opbx_config_OPBX_SOUNDS_DIR));
		} else if (!strcasecmp(v->name, "cwrundir")) {
			snprintf(opbx_config_OPBX_PID, sizeof(opbx_config_OPBX_PID), "%s/%s", v->value, "callweaver.pid");
			snprintf(opbx_config_OPBX_SOCKET, sizeof(opbx_config_OPBX_SOCKET), "%s/%s", v->value, opbx_config_OPBX_CTL);
			opbx_copy_string(opbx_config_OPBX_RUN_DIR, v->value, sizeof(opbx_config_OPBX_RUN_DIR));
		} else if (!strcasecmp(v->name, "cwmoddir")) {
			lt_dlsetsearchpath(v->value);
		} else if (!strcasecmp(v->name, "cwkeydir")) { 
			opbx_copy_string(opbx_config_OPBX_KEY_DIR, v->value, sizeof(opbx_config_OPBX_KEY_DIR)); 
		}
		v = v->next;
	}
	v = opbx_variable_browse(cfg, "options");
	while(v) {
		/* verbose level (-v at startup) */
		if (!strcasecmp(v->name, "verbose")) {
			option_verbose = atoi(v->value);
		/* whether or not to force timestamping. (-T at startup) */
		} else if (!strcasecmp(v->name, "timestamp")) {
			option_timestamp = opbx_true(v->value);
		/* whether or not to support #exec in config files */
		} else if (!strcasecmp(v->name, "execincludes")) {
			option_exec_includes = opbx_true(v->value);
		/* debug level (-d at startup) */
		} else if (!strcasecmp(v->name, "debug")) {
			option_debug = 0;
			if (sscanf(v->value, "%d", &option_debug) != 1) {
				option_debug = opbx_true(v->value);
			}
		/* Disable forking (-f at startup) */
		} else if (!strcasecmp(v->name, "nofork")) {
			option_nofork = opbx_true(v->value);
		/* Run quietly (-q at startup ) */
		} else if (!strcasecmp(v->name, "quiet")) {
			option_quiet = opbx_true(v->value);
		/* Run as console (-c at startup, implies nofork) */
		} else if (!strcasecmp(v->name, "console")) {
			option_console = opbx_true(v->value);
		/* Run with highg priority if the O/S permits (-p at startup) */
		} else if (!strcasecmp(v->name, "highpriority")) {
			option_highpriority = opbx_true(v->value);
		/* Initialize RSA auth keys (IAX2) (-i at startup) */
		} else if (!strcasecmp(v->name, "initcrypto")) {
			option_initcrypto = opbx_true(v->value);
		/* Disable ANSI colors for console (-c at startup) */
		} else if (!strcasecmp(v->name, "nocolor")) {
			option_nocolor = opbx_true(v->value);
		/* Disable some usage warnings for picky people :p */
		} else if (!strcasecmp(v->name, "dontwarn")) {
			option_dontwarn = opbx_true(v->value);
		/* Dump core in case of crash (-g) */
		} else if (!strcasecmp(v->name, "dumpcore")) {
			option_dumpcore = opbx_true(v->value);
		/* Cache recorded sound files to another directory during recording */
		} else if (!strcasecmp(v->name, "cache_record_files")) {
			option_cache_record_files = opbx_true(v->value);
		/* Specify cache directory */
		}  else if (!strcasecmp(v->name, "record_cache_dir")) {
			opbx_copy_string(record_cache_dir, v->value, OPBX_CACHE_DIR_LEN);
		/* Build transcode paths via SLINEAR, instead of directly */
		} else if (!strcasecmp(v->name, "transcode_via_sln")) {
			option_transcode_slin = opbx_true(v->value);
		} else if (!strcasecmp(v->name, "maxcalls")) {
			if ((sscanf(v->value, "%d", &option_maxcalls) != 1) || (option_maxcalls < 0)) {
				option_maxcalls = 0;
			}
		} else if (!strcasecmp(v->name, "maxload")) {
			double test[1];

			if (getloadavg(test, 1) == -1) {
				opbx_log(OPBX_LOG_ERROR, "Cannot obtain load average on this system. 'maxload' option disabled.\n");
				option_maxload = 0.0;
			} else if ((sscanf(v->value, "%lf", &option_maxload) != 1) || (option_maxload < 0.0)) {
				option_maxload = 0.0;
			}
		} else if (!strcasecmp(v->name, "systemname")) {
			opbx_copy_string(opbx_config_OPBX_SYSTEM_NAME, v->value, sizeof(opbx_config_OPBX_SYSTEM_NAME));
		}
		else if (!strcasecmp(v->name, "enableunsafeunload"))
		{
			opbx_copy_string(opbx_config_OPBX_ENABLE_UNSAFE_UNLOAD, v->value, sizeof(opbx_config_OPBX_ENABLE_UNSAFE_UNLOAD));
		}
		v = v->next;
	}
	opbx_config_destroy(cfg);
}


int callweaver_main(int argc, char *argv[])
{
	int c;
	struct sigaction sa;
	char * xarg = NULL;
	int x;
	sigset_t sigs;
	int is_child_of_nonroot=0;
	static char *runuser = NULL, *rungroup = NULL;  


	/* init with default */
	opbx_copy_string(opbx_config_OPBX_CONFIG_FILE, opbxconffile_default, sizeof(opbx_config_OPBX_CONFIG_FILE));
	
	/* Remember original args for restart */
	if (argc > sizeof(_argv) / sizeof(_argv[0]) - 1) {
		fprintf(stderr, "Truncating argument size to %d\n", (int)(sizeof(_argv) / sizeof(_argv[0])) - 1);
		argc = sizeof(_argv) / sizeof(_argv[0]) - 1;
	}
	for (x=0;x<argc;x++)
		_argv[x] = argv[x];
	_argv[x] = NULL;

	/* if the progname is rcallweaver consider it a remote console */
	if (argv[0] && (strstr(argv[0], "rcallweaver")) != NULL) {
		option_remote++;
		option_nofork++;
	}

	gethostname(hostname, sizeof(hostname) - 1);
	hostname[sizeof(hostname) - 1] = '\0';
	for (x = 0; hostname[x] && hostname[x] != '.'; x++)
		shorthostname[x] = hostname[x];
	shorthostname[x] = '\0';

	opbx_mainpid = getpid();
	opbx_ulaw_init();
	opbx_alaw_init();
	opbx_utils_init();

	/* When CallWeaver restarts after it has dropped the root privileges,
	 * it can't issue setuid(), setgid(), setgroups() or set_priority() 
	 * */
	if (getenv("CALLWEAVER_ALREADY_NONROOT"))
		is_child_of_nonroot = 1;

	/* Check for options */
	while((c=getopt(argc, argv, "tThfdvVqprRgcinx:U:G:C:L:M:")) != -1) {
		switch(c) {
		case 'd':
			option_debug++;
			break;
		case 'c':
			option_console++;
			option_reconnect++;
			break;
		case 'f':
			option_nofork++;
			break;
		case 'n':
			option_nocolor++;
			break;
		case 'r':
			option_remote++;
			option_nofork++;
			break;
		case 'R':
			option_remote++;
			option_nofork++;
			option_reconnect++;
			break;
		case 'p':
			option_highpriority++;
			break;
		case 'v':
			option_verbose++;
			break;
		case 'M':
			if ((sscanf(optarg, "%d", &option_maxcalls) != 1) || (option_maxcalls < 0))
				option_maxcalls = 0;
			break;
		case 'L':
			if ((sscanf(optarg, "%lf", &option_maxload) != 1) || (option_maxload < 0.0))
				option_maxload = 0.0;
			break;
		case 'q':
			option_quiet++;
			break;
		case 't':
			option_cache_record_files++;
			break;
		case 'T':
			option_timestamp++;
			break;
		case 'x':
			option_exec++;
			xarg = optarg;
			break;
		case 'C':
			opbx_copy_string((char *)opbx_config_OPBX_CONFIG_FILE,optarg,sizeof(opbx_config_OPBX_CONFIG_FILE));
			option_overrideconfig++;
			break;
		case 'i':
			option_initcrypto++;
			break;
		case 'g':
			option_dumpcore++;
			break;
		case 'h':
			show_cli_help();
			exit(0);
		case 'V':
			show_version();
			exit(0);
		case 'U':
			runuser = optarg;
			break;
		case 'G':
			rungroup = optarg;
			break;
		case '?':
			exit(1);
		}
	}

	initstate((getppid() * 65535 + getpid()) % RAND_MAX, random_state, 256);

	opbx_loader_init();

	/* For remote connections, change the name of the remote connection.
	 * We do this for the benefit of init scripts (which need to know if/when
	 * the main callweaver process has died yet). */
	if (option_remote) {
		strcpy(argv[0], "rcallweaver");
		for (x = 1; x < argc; x++) {
			argv[x] = argv[0] + 10;
		}
	}

	if ((option_console || option_nofork) && !option_verbose) 
		opbx_verbose("[ Reading Master Configuration ]");
	opbx_readconfig();

	if (option_dumpcore) {
		struct rlimit l;
		memset(&l, 0, sizeof(l));
		l.rlim_cur = RLIM_INFINITY;
		l.rlim_max = RLIM_INFINITY;
		if (setrlimit(RLIMIT_CORE, &l)) {
			opbx_log(OPBX_LOG_WARNING, "Unable to disable core size resource limit: %s\n", strerror(errno));
		}
	}


	if (!is_child_of_nonroot && opbx_set_priority(option_highpriority)) {
		exit(1);
	}

	if (!runuser)
		runuser = opbx_config_OPBX_RUN_USER;
	if (!rungroup)
		rungroup = opbx_config_OPBX_RUN_GROUP;
	if (!is_child_of_nonroot) {
		struct group *gr;
		struct passwd *pw;

#if defined(__linux__)
		cap_t caps;

		/* There are no standard capabilities we want but there
		 * are Linux specific capabilities we want. So, in the
		 * case of Linux we don't drop capabilities when we change
		 * uid but we fix them up afterwards ourself.
		 */
		if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) == -1) {
			opbx_log(OPBX_LOG_WARNING, "Unable to keep capabilities: %s\n", strerror(errno));
		}
#endif

		gr = getgrnam(rungroup);
		if (!gr) {
			opbx_log(OPBX_LOG_ERROR, "No such group '%s'!\n", rungroup);
			exit(1);
		}
		pw = getpwnam(runuser);
		if (!pw) {
			opbx_log(OPBX_LOG_ERROR, "No such user '%s'!\n", runuser);
			exit(1);
		}
		
		if (gr->gr_gid != getegid() )
		if (initgroups(pw->pw_name, gr->gr_gid) == -1) {
			opbx_log(OPBX_LOG_ERROR, "Unable to initgroups '%s' (%d)\n", pw->pw_name, gr->gr_gid);
			exit(1);
		}

		if (setregid(gr->gr_gid, gr->gr_gid)) {
			opbx_log(OPBX_LOG_ERROR, "Unable to setgid to '%s' (%d)\n", gr->gr_name, gr->gr_gid);
			exit(1);
		}
		if (option_verbose) {
			int ngroups;
			gid_t gid_list[NGROUPS_MAX];
			int i;
			struct group *gr2;

			gr2 = getgrgid(getegid());
			if (gr2) {
				opbx_verbose("Now running as group '%s' (%d)\n", gr2->gr_name, gr2->gr_gid);
			} else {
				opbx_verbose("Now running as group '' (%d)\n", getegid());
			}

			opbx_verbose("Supplementary groups:\n");
			ngroups = getgroups(NGROUPS_MAX, gid_list);
			for (i = 0; i < ngroups; i++) {
				gr2 = getgrgid(gid_list[i]);
				if (gr2) {
					opbx_verbose("   '%s' (%d)\n", gr2->gr_name, gr2->gr_gid);
				} else {
					opbx_verbose("   '' (%d)\n", gid_list[i]);
				}
			}
		}
#ifdef __Darwin__
		if (seteuid(pw->pw_uid)) {
#else
		if (setreuid(pw->pw_uid, pw->pw_uid)) {
#endif
			opbx_log(OPBX_LOG_ERROR, "Unable to setuid to '%s' (%d)\n", pw->pw_name, pw->pw_uid);
			exit(1);
		}
		setenv("CALLWEAVER_ALREADY_NONROOT","yes",1);
		if (option_verbose) {
			struct passwd *pw2;
			pw2 = getpwuid(geteuid());
			if (pw2) {
				opbx_verbose("Now running as user '%s' (%d)\n", pw2->pw_name, pw2->pw_uid);
			} else {
				opbx_verbose("Now running as user '' (%d)\n", getegid());
			}
		}

#if defined(__linux__)
		/* Linux specific capabilities:
		 *     cap_net_admin    allow TOS setting
		 *     cap_sys_nice     allow use of FIFO and round-robin scheduling
		 */
		if ((caps = cap_from_text("= cap_net_admin,cap_sys_nice=ep"))) {
			if (cap_set_proc(caps))
				fprintf(stderr, "Failed to set caps\n");
			cap_free(caps);
		}
#endif
	}

	/* Check if we're root */
	if (!geteuid()) {
#ifdef VERY_SECURE
        opbx_log(OPBX_LOG_ERROR, "Running as root has been disabled\n");
        exit(1);
#else
		opbx_log(OPBX_LOG_ERROR, "Running as root has been enabled\n");
#endif /* VERY_SECURE */
	}



#if defined(__linux__)
	/* after set*id() the dumpable flag is deleted,
	   so we set it again to get core dumps */
	if (option_dumpcore) {
		if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == -1) {
			opbx_log(OPBX_LOG_ERROR, "Unable to set dumpable flag: %s\n", strerror(errno));
		}
	}
#endif

	if ((option_console || option_nofork) && !option_verbose) 
		opbx_verbose("[ Initializing Custom Configuration Options ]");

	opbx_registry_init(&atexit_registry);
	opbx_registry_init(&cdrbe_registry);
	opbx_registry_init(&clicmd_registry);
	opbx_registry_init(&config_engine_registry);
	opbx_registry_init(&format_registry);
	opbx_registry_init(&func_registry);
	opbx_registry_init(&imager_registry);
	opbx_registry_init(&switch_registry);
	opbx_registry_init(&translator_registry);

	/* custom config setup */
	register_config_cli();
	read_config_maps();

	if (option_remote || option_exec) {
		if (option_exec)
			exit(console_oneshot(opbx_config_OPBX_SOCKET, xarg));
		console(opbx_config_OPBX_SOCKET);
		exit(0);
	}

	switch (lockfile_claim(opbx_config_OPBX_PID)) {
		case 0: /* Already running */
			opbx_log(OPBX_LOG_ERROR, "CallWeaver already running.  Use 'callweaver -r' to connect.\n");
			/* Fall through */
		case -1: /* Interrupted before claim */
			exit(1);
	}

	if (!option_console && !option_nofork) {
		pid_t pid = fork();
		if (pid == -1) {
			opbx_log(OPBX_LOG_ERROR, "fork failed: %s\n", strerror(errno));
			exit(1);
		} else if (pid) {
			/* We, the parent, are going to die and our child takes
			 * over all future responsibilities. Update the pid file
			 * accordingly.
			 */
			if (lockfile_rewrite(opbx_config_OPBX_PID, pid))
				opbx_log(OPBX_LOG_WARNING, "Unable to rewrite pid file '%s' after forking: %s\n", opbx_config_OPBX_PID, strerror(errno));
			_exit(0);
		}

		freopen("/dev/null", "r", stdin);
		freopen("/dev/null", "w", stdout);
		freopen("/dev/null", "w", stderr);
		setsid();
	}

	/* Test recursive mutex locking. */
	if (test_for_thread_safety())
		opbx_verbose("Warning! CallWeaver is not thread safe.\n");

	for (x = 0; x < arraysize(consoles); x++)	
		consoles[x].fd = -1;

	unlink(opbx_config_OPBX_SOCKET);

	opbx_makesocket(opbx_config_OPBX_SOCKET);

	opbx_register_verbose(network_verboser);

	sigemptyset(&sigs);
	sigaddset(&sigs, SIGHUP);
	sigaddset(&sigs, SIGTERM);
	sigaddset(&sigs, SIGINT);
	sigaddset(&sigs, SIGWINCH);
	pthread_sigmask(SIG_BLOCK, &sigs, NULL);

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sa.sa_handler = urg_handler;
	sigaction(SIGURG, &sa, NULL);

	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);

	sa.sa_handler = child_handler;
	sa.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, NULL);

	/* Console start up needs core CLI commands in place because
	 * the console will request debug and verbose settings
	 */
	opbx_cli_init();

	if (option_console || option_nofork) {
		if (opbx_pthread_create(&consolethread, &global_attr_default, console, opbx_config_OPBX_SOCKET)) {
			opbx_log(OPBX_LOG_ERROR, "Failed to start console - console is not available\n");
			option_console = 0;
			option_nofork = 1;
		}
	}

	if (!option_console)
		boot();

	sigdelset(&sigs, SIGWINCH);

	for (;;) {
		int sig;

		sigwait(&sigs, &sig);
		switch (sig) {
			case SIGHUP:
				if (option_verbose > 1) 
					printf("Received HUP signal -- Reloading configs\n");
				opbx_module_reload(NULL);
				break;
			case SIGTERM:
			case SIGINT:
				shutdown_restart(-1, 1, 0, -1);
				break;
		}
	}

	return 0;
}
