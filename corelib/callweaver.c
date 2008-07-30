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
 * Also see \ref cwCREDITS
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

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sysexits.h>
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
#include "callweaver/time.h"

#include "callweaver/crypto.h"

#include "core/ulaw.h"
#include "core/alaw.h"

#include "libltdl/ltdl.h"

#include <readline/readline.h>
#include <readline/history.h>

/* defines various compile-time defaults */
#include "defaults.h"

#include "console.h"

#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#define PF_LOCAL PF_UNIX
#endif

#define CW_MAX_CONNECTS 128
#define NUM_MSGS 64


#if defined(_POSIX_TIMERS)
#  if defined(_POSIX_MONOTONIC_CLOCK) && defined(__USE_XOPEN2K)
clockid_t global_clock_monotonic = CLOCK_MONOTONIC;
#  else
clockid_t global_clock_monotonic = CLOCK_REALTIME;
#  endif
#endif


char hostname[MAXHOSTNAMELEN];

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
char record_cache_dir[CW_CACHE_DIR_LEN] = cwtmpdir_default;
char debug_filename[CW_FILENAME_MAX] = "";

int cw_mainpid;
struct console {
	int fd;				/*!< File descriptor */
	int p[2];			/*!< Pipe */
	pthread_t t;			/*!< Thread of handler */
};

time_t cw_startuptime;
time_t cw_lastreloadtime;

struct console consoles[CW_MAX_CONNECTS];

char defaultlanguage[MAX_LANGUAGE] = DEFAULT_LANGUAGE;

char cw_config_CW_CONFIG_DIR[CW_CONFIG_MAX_PATH];
char cw_config_CW_CONFIG_FILE[CW_CONFIG_MAX_PATH];
char cw_config_CW_SPOOL_DIR[CW_CONFIG_MAX_PATH];
char cw_config_CW_MONITOR_DIR[CW_CONFIG_MAX_PATH];
char cw_config_CW_VAR_DIR[CW_CONFIG_MAX_PATH];
char cw_config_CW_LOG_DIR[CW_CONFIG_MAX_PATH];
char cw_config_CW_OGI_DIR[CW_CONFIG_MAX_PATH];
char cw_config_CW_DB[CW_CONFIG_MAX_PATH];
char cw_config_CW_DB_DIR[CW_CONFIG_MAX_PATH];
char cw_config_CW_KEY_DIR[CW_CONFIG_MAX_PATH];
char cw_config_CW_PID[CW_CONFIG_MAX_PATH];
char cw_config_CW_SOCKET[CW_CONFIG_MAX_PATH];
char cw_config_CW_RUN_DIR[CW_CONFIG_MAX_PATH];
char cw_config_CW_RUN_USER[CW_CONFIG_MAX_PATH];
char cw_config_CW_RUN_GROUP[CW_CONFIG_MAX_PATH];
char cw_config_CW_CTL_PERMISSIONS[CW_CONFIG_MAX_PATH];
char cw_config_CW_CTL_OWNER[CW_CONFIG_MAX_PATH] = "\0";
char cw_config_CW_CTL_GROUP[CW_CONFIG_MAX_PATH] = "\0";
char cw_config_CW_CTL[CW_CONFIG_MAX_PATH] = "callweaver.ctl";
char cw_config_CW_SYSTEM_NAME[20] = "";
char cw_config_CW_SOUNDS_DIR[CW_CONFIG_MAX_PATH];
char cw_config_CW_ENABLE_UNSAFE_UNLOAD[20] = "";

static char *_argv[256];
static int restart = 0;
static pthread_t consolethread = CW_PTHREADT_NULL;

static char random_state[256];

static pthread_t lthread = CW_PTHREADT_NULL;


static void cw_clock_init(void)
{
	struct timespec ts;

	if (clock_gettime(global_clock_monotonic, &ts))
		global_clock_monotonic = CLOCK_REALTIME;
}


static const char *atexit_registry_obj_name(struct cw_object *obj)
{
	struct cw_atexit *it = container_of(obj, struct cw_atexit, obj);
	return it->name;
}

static int atexit_registry_obj_match(struct cw_object *obj, const void *pattern)
{
	struct cw_atexit *it = container_of(obj, struct cw_atexit, obj);
	return (it->function == pattern);
}

struct cw_registry atexit_registry = {
	.name = "At Exit",
	.obj_name = atexit_registry_obj_name,
	.obj_match = atexit_registry_obj_match,
	.lock = CW_MUTEX_INIT_VALUE,
};

static int cw_run_atexit_one(struct cw_object *obj, void *data)
{
	struct cw_atexit *it = container_of(obj, struct cw_atexit, obj);
	if (option_verbose > 2)
		cw_verbose(VERBOSE_PREFIX_3 "atexit: run \"%s\"\n", it->name);
	/* Get the module now so it's pinned (atexits don't hold counted refs
	 * while registered)
	 */
	cw_module_get(it->obj.module);
	it->function();
	/* We'd prefer not to put the module. If we are running atexits we're
	 * shutting down so there's no need to release modules. However, shutdowns
	 * can be cancelled...
	 */
	cw_module_put(it->obj.module);
	return 0;
}

static void cw_run_atexits(void)
{
	cw_registry_iterate(&atexit_registry, cw_run_atexit_one, NULL);
}


/*! NULL handler so we can collect the child exit status */
static void null_sig_handler(int signal)
{
}

CW_MUTEX_DEFINE_STATIC(safe_system_lock);
static unsigned int safe_system_level = 0;
static struct sigaction safe_system_prev_handler;

int cw_safe_system(const char *s)
{
    struct sigaction sa;
    pid_t pid;
    int x;
    int res;
    struct rusage rusage;
    int status;
    unsigned int level;

    /* keep track of how many cw_safe_system() functions
       are running at this moment
    */
    cw_mutex_lock(&safe_system_lock);
    level = safe_system_level++;

    /* only replace the handler if it has not already been done */
    if (level == 0) {
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_NOCLDSTOP;
        sa.sa_handler = null_sig_handler;
        sigaction(SIGCHLD, &sa, &safe_system_prev_handler);
    }

    cw_mutex_unlock(&safe_system_lock);

#if defined(HAVE_WORKING_FORK)
    pid = fork();
#else
    pid = vfork();
#endif

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
        cw_log(CW_LOG_WARNING, "Fork failed: %s\n", strerror(errno));
        res = -1;
    }

    cw_mutex_lock(&safe_system_lock);
    level = --safe_system_level;

    /* only restore the handler if we are the last one */
    if (level == 0)
        sigaction(SIGCHLD, &safe_system_prev_handler, NULL);

    cw_mutex_unlock(&safe_system_lock);

    return res;
}


/*! Urgent handler
 Called by soft_hangup to interrupt the poll, read, or other
 system call.  We don't actually need to do anything though.  
 Remember: Cannot EVER cw_log from within a signal handler 
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
				if (pid == getpid()) {
					/* Hang on, we alerady own the lock. We must have restarted */
					close(d);
					claimed = 1;
					goto out;
				} else if (!kill(pid, 0)) {
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

	/* No more changing your mind. This is definitely what we are going to do. */
	local_restart = restart;

	if (option_verbose)
		cw_verbose("Executing last minute cleanups\n");

	cw_cdr_engine_term();

	cw_run_atexits();

	if (option_verbose && (option_console || option_nofork))
		cw_verbose("CallWeaver %s ending.\n", cw_active_channels() ? "uncleanly" : "cleanly");
	if (option_debug)
		cw_log(CW_LOG_DEBUG, "CallWeaver ending.\n");

	manager_event(EVENT_FLAG_SYSTEM, "Shutdown", "Shutdown: %s\r\nRestart: %s\r\n", (cw_active_channels() ? "Uncleanly" : "Cleanly"), (local_restart ? "True" : "False"));

	if (!pthread_equal(lthread, CW_PTHREADT_NULL)) {
		pthread_cancel(lthread);
		unlink(cw_config_CW_SOCKET);
	}

	if (option_verbose || option_console || option_nofork)
		cw_verbose("%s CallWeaver NOW...\n", (local_restart ? "Restarting" : "Halting"));

	close_logger();

	if (!pthread_equal(consolethread, CW_PTHREADT_NULL)) {
		pthread_t tid = consolethread;

		option_reconnect = 0;
		usleep(100000);
		pthread_cancel(tid);
		pthread_join(tid, NULL);
	}

	if (local_restart)
		exit(EX_TEMPFAIL);

	exit(EX_OK);
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
		n = cw_active_channels();
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
		cw_log(CW_LOG_NOTICE, "Timeout waiting for idle. Initiating immediate %s\n", (restart ? "restart" : "shutdown"));
		pthread_detach(pthread_self());
		state->tid = CW_PTHREADT_NULL;
		shutdown_restart(-1, 1, 0, -1);
	} else {
		cw_log(CW_LOG_NOTICE, "Beginning callweaver %s....\n", restart ? "restart" : "shutdown");
		quit_handler(NULL);
	}

	return NULL;
}


static void shutdown_restart(int fd, int doit, int nice, int timeout)
{
	static cw_mutex_t lock = CW_MUTEX_INIT_VALUE;
	static struct shutdown_state state = {
		.tid = CW_PTHREADT_NULL,
		.nice = 0,
		.timeout = 0,
	};

	cw_mutex_lock(&lock);

	if (doit >= 0) {
		if (!pthread_equal(state.tid, CW_PTHREADT_NULL)) {
			pthread_cancel(state.tid);
			pthread_join(state.tid, NULL);
			state.tid = CW_PTHREADT_NULL;
		}

		if (doit) {
			if (nice < 2) {
				cw_begin_shutdown((nice ? 0 : 1));

				if (fd >= 0 && !option_console && fd != STDOUT_FILENO)
					cw_cli(fd, "Blocked new calls\n");
				if (!option_remote)
					cw_log(CW_LOG_NOTICE, "Blocked new calls\n");

				if (nice < 1) {
					if (fd >= 0 && !option_console && fd != STDOUT_FILENO)
						cw_cli(fd, "Hanging up active calls\n");
					if (!option_remote)
						cw_log(CW_LOG_NOTICE, "Hanging up active calls\n");
				}
			}

			if (fd >= 0 && !option_console && fd != STDOUT_FILENO)
				cw_cli(fd, "Will %s when idle...\n", (restart ? "restart" : "shutdown"));
			if (!option_remote)
				cw_log(CW_LOG_NOTICE, "Will %s when idle...\n", (restart ? "restart" : "shutdown"));

			state.nice = nice;
			state.timeout = (nice ? (timeout >= 0 ? timeout : -1 ) : 15);
			cw_pthread_create(&state.tid, &global_attr_default, quit_when_idle, &state);
		} else {
			if (fd >= 0 && !option_console && fd != STDOUT_FILENO)
				cw_cli(fd, "%s cancelled\n", (restart ? "restart" : "shutdown"));
			if (!option_remote)
				cw_log(CW_LOG_NOTICE, "%s cancelled\n", (restart ? "restart" : "shutdown"));
		}
	} else {
		if (!pthread_equal(state.tid, CW_PTHREADT_NULL)) {
			if (state.timeout == -1)
				cw_cli(fd, "Pending %s when idle%s\n", (restart ? "restart" : "shutdown"), (state.nice < 2 ? " (new calls blocked)" : ""));
			else
				cw_cli(fd, "Pending %s in less than %ds%s\n", (restart ? "restart" : "shutdown"), state.timeout, (state.nice < 2 ? " (new calls blocked)" : ""));
		} else
			cw_cli(fd, "No shutdown or restart pending\n");
	}

	cw_mutex_unlock(&lock);
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
"       many seconds an immediate stop will be initiated.\n";

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
	cw_cancel_shutdown();
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

static struct cw_clicmd core_cli[] = {
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

	/* DEPRECATED */
	{
		.cmda = { "abort", "halt", NULL },
		.handler = handle_shutdown_restart_cancel,
		.summary = "Cancel a pending stop or restart request",
		.usage = abort_halt_help,
	},
};


static void boot(void)
{
	struct timespec ts;

	if ((option_console || option_nofork) && !option_verbose) 
		cw_verbose("[ Booting...");

	/* Ensure that the random number generators are seeded with a different value every time
	 * CallWeaver is started
	 */
	cw_clock_gettime(CLOCK_REALTIME, &ts);
	srand((unsigned int) getpid() + ts.tv_sec + ts.tv_nsec);
	srandom((unsigned int) getpid() + ts.tv_sec + ts.tv_nsec);

	if (cw_crypto_init()
	|| cw_loader_cli_init()
	|| load_modules(1)
	|| cw_channels_init()
	|| cw_cdr_engine_init()
	|| cw_device_state_engine_init()
	|| cw_rtp_init()
	|| cw_udptl_init()
	|| cw_stun_init()
	|| direngine_list_init()
	|| cw_image_init()
	|| cw_file_init()
	|| load_pbx()
	|| cwdb_init()
	|| init_framer()
	|| load_modules(0)
	|| cw_enum_init()
	|| cw_translator_init()
	|| init_features()) {
	    exit(EX_USAGE);
	}

#ifdef __CW_DEBUG_MALLOC
	__cw_mm_init();
#endif	
	cw_cli_register_multiple(core_cli, arraysize(core_cli));

	if ((option_console || option_nofork) && !option_verbose)
		cw_verbose(" ]\n");

	time(&cw_startuptime);
	if (option_verbose || option_console || option_nofork)
		cw_verbose("CallWeaver Ready\n");

	fully_booted = 1;
}


/*!
 * write the string to the console, and all attached
 * console clients
 */
void cw_console_puts(const char *string)
{
	int i;

	for (i = 0; i < arraysize(consoles); i++) {
		if (consoles[i].fd > -1)
			write(consoles[i].p[1], string, strlen(string));
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
		cw_console_puts(s);
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
				cw_log(CW_LOG_WARNING, "poll returned < 0: %s\n", strerror(errno));
			sleep(1);
			continue;
		}
		if (fds[0].revents) {
			res = read(con->fd, tmp, sizeof(tmp));
			if (res < 1) {
				break;
			}
			tmp[res] = 0;
			cw_cli_command(con->fd, tmp);
		}
		if (fds[1].revents) {
			res = read(con->p[0], tmp, sizeof(tmp));
			if (res < 1) {
				cw_log(CW_LOG_ERROR, "read returned %d\n", res);
				break;
			}
			res = write(con->fd, tmp, res);
			if (res < 1)
				break;
		}
	}
	if (option_verbose > 2)
		cw_verbose(VERBOSE_PREFIX_3 "Remote UNIX connection disconnected\n");
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
				cw_log(CW_LOG_WARNING, "poll returned %d error: %s\n", n, strerror(x));
		} else if (s < 0) {
			if (x != EINTR)
				cw_log(CW_LOG_WARNING, "Accept returned %d: %s\n", s, strerror(x));
		} else {
			for (x = 0; x < arraysize(consoles); x++) {
				if (consoles[x].fd < 0) {
					if (socketpair(AF_LOCAL, SOCK_STREAM, 0, consoles[x].p)) {
						cw_log(CW_LOG_ERROR, "Unable to create pipe: %s\n", strerror(errno));
						consoles[x].fd = -1;
						close(s);
						break;
					}

					n = snprintf(buf, sizeof(buf), "%s/%d/%s\n", hostname, cw_mainpid,  cw_version_string );
					write(s, buf, n);

					consoles[x].fd = s;
					fcntl(s, F_SETFD, fcntl(s, F_GETFD, 0) | FD_CLOEXEC);
					fcntl(consoles[x].p[0], F_SETFD, fcntl(consoles[x].p[0], F_GETFD, 0) | FD_CLOEXEC);
					fcntl(consoles[x].p[1], F_SETFD, fcntl(consoles[x].p[1], F_GETFD, 0) | FD_CLOEXEC);
					fcntl(consoles[x].p[1], F_SETFL, fcntl(consoles[x].p[1], F_GETFL) | O_NONBLOCK);
					if (cw_pthread_create(&consoles[x].t, &global_attr_detached, netconsole, &consoles[x])) {
						cw_log(CW_LOG_ERROR, "Unable to spawn thread to handle connection: %s\n", strerror(errno));
						consoles[x].fd = -1;
						close(s);
					} else if (!fully_booted)
						boot();
					break;
				}
			}
			if (x >= arraysize(consoles)) {
				cw_log(CW_LOG_WARNING, "No more connections allowed\n");
				close(s);
			} else if (consoles[x].fd > -1) {
				if (option_verbose > 2) 
					cw_verbose(VERBOSE_PREFIX_3 "Remote UNIX connection\n");
			}
		}
	}
	return NULL;
}

static int cw_makesocket(char *spec)
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
	cw_copy_string(u.sun.sun_path, spec, sizeof(u.sun.sun_path));
	salen = sizeof(u.sun);

	if ((sock = socket(u.sa.sa_family, SOCK_STREAM, 0)) < 0) {
		cw_log(CW_LOG_WARNING, "Unable to create socket for %s: %s\n", spec, strerror(errno));
		return -1;
	}		
	if (bind(sock, &u.sa, salen)) {
		cw_log(CW_LOG_WARNING, "Unable to bind socket to %s: %s\n", spec, strerror(errno));
		close(sock);
		return -1;
	}
	if (listen(sock, 1024) < 0) {
		cw_log(CW_LOG_WARNING, "Unable to listen on socket %s: %s\n", spec, strerror(errno));
		close(sock);
		return -1;
	}

	fcntl(sock, F_SETFD, fcntl(sock, F_GETFD, 0) | FD_CLOEXEC);

	cw_pthread_create(&lthread, &global_attr_default, listener, (void *)sock);

	if (u.sa.sa_family == AF_LOCAL) {
		if (!cw_strlen_zero(cw_config_CW_CTL_OWNER)) {
			struct passwd *pw;
			if ((pw = getpwnam(cw_config_CW_CTL_OWNER)) == NULL)
				cw_log(CW_LOG_WARNING, "Unable to find uid of user %s\n", cw_config_CW_CTL_OWNER);
			else
				uid = pw->pw_uid;
		}
		
		if (!cw_strlen_zero(cw_config_CW_CTL_GROUP)) {
			struct group *grp;
			if ((grp = getgrnam(cw_config_CW_CTL_GROUP)) == NULL)
				cw_log(CW_LOG_WARNING, "Unable to find gid of group %s\n", cw_config_CW_CTL_GROUP);
			else
				gid = grp->gr_gid;
		}

		if (chown(cw_config_CW_SOCKET, uid, gid) < 0)
			cw_log(CW_LOG_WARNING, "Unable to change ownership of %s: %s\n", spec, strerror(errno));

		if (!cw_strlen_zero(cw_config_CW_CTL_PERMISSIONS)) {
			mode_t p;
			sscanf(cw_config_CW_CTL_PERMISSIONS, "%o", (int *) &p);
			if ((chmod(spec, p)) < 0)
				cw_log(CW_LOG_WARNING, "Unable to change file permissions of %s: %s\n", spec, strerror(errno));
		}
	}

	return 0;
}


static int show_version(void)
{
	puts(cw_version_string);
	return 0;
}

static int show_cli_help(void) {
	puts(cw_version_string);
	puts("\n"
	"Usage: callweaver [OPTIONS]\n"
	"Valid Options:\n"
	"   -V              Display version number and exit\n"
	"   -C <configfile> Use an alternate configuration file\n"
	"   -G <group>      Run as a group other than the caller\n"
	"   -U <user>       Run as a user other than the caller\n"
	"   -c              Provide console CLI\n"
	"   -d              Enable extra debugging\n"
	"   -f              Do not fork\n"
	"   -g              Dump core in case of a crash\n"
	"   -h              This help screen\n"
	"   -i              Initialize crypto keys at startup\n"
	"   -n              Disable console colorization at startup or console (not remote)\n"
	"   -p              Run with elevated priority\n"
	"   -q              Quiet mode (suppress output)\n"
	"   -r              Connect to CallWeaver on this machine\n"
	"   -R              Connect to CallWeaver, and attempt to reconnect if disconnected\n"
	"   -t              Record soundfiles in /var/tmp and move them where they belong after they are done.\n"
	"   -T              Display the time in [Mmm dd hh:mm:ss] format for each line of output to the CLI.\n"
	"   -v              Increase verbosity (multiple v's = more verbose)\n"
	"   -x <cmd>        Execute command <cmd> (only valid with -r)");
	return 0;
}

static void cw_readconfig(void) {
	struct cw_config *cfg;
	struct cw_variable *v;
	char *config = cw_config_CW_CONFIG_FILE;

	if (option_overrideconfig == 1) {
		cfg = cw_config_load(cw_config_CW_CONFIG_FILE);
		if (!cfg)
			cw_log(CW_LOG_WARNING, "Unable to open specified master config file '%s', using built-in defaults\n", cw_config_CW_CONFIG_FILE);
	} else {
		cfg = cw_config_load(config);
	}

	/* init with buildtime config */

	cw_copy_string(cw_config_CW_RUN_USER, cwrunuser_default, sizeof(cw_config_CW_RUN_USER));
	cw_copy_string(cw_config_CW_RUN_GROUP, cwrungroup_default, sizeof(cw_config_CW_RUN_GROUP));
	cw_copy_string(cw_config_CW_CONFIG_DIR, cwconfdir_default, sizeof(cw_config_CW_CONFIG_DIR));
	cw_copy_string(cw_config_CW_SPOOL_DIR, cwspooldir_default, sizeof(cw_config_CW_SPOOL_DIR));
	lt_dlsetsearchpath(cwmoddir_default);
 	snprintf(cw_config_CW_MONITOR_DIR, sizeof(cw_config_CW_MONITOR_DIR) - 1, "%s/monitor", cw_config_CW_SPOOL_DIR);
	cw_copy_string(cw_config_CW_VAR_DIR, cwvardir_default, sizeof(cw_config_CW_VAR_DIR));
	cw_copy_string(cw_config_CW_LOG_DIR, cwlogdir_default, sizeof(cw_config_CW_LOG_DIR));
	cw_copy_string(cw_config_CW_OGI_DIR, cwogidir_default, sizeof(cw_config_CW_OGI_DIR));
	cw_copy_string(cw_config_CW_DB, cwdbfile_default, sizeof(cw_config_CW_DB));
	cw_copy_string(cw_config_CW_DB_DIR, cwdbdir_default, sizeof(cw_config_CW_DB_DIR));
	cw_copy_string(cw_config_CW_KEY_DIR, cwkeydir_default, sizeof(cw_config_CW_KEY_DIR));
	cw_copy_string(cw_config_CW_PID, cwpidfile_default, sizeof(cw_config_CW_PID));
	cw_copy_string(cw_config_CW_SOCKET, cwsocketfile_default, sizeof(cw_config_CW_SOCKET));
	cw_copy_string(cw_config_CW_RUN_DIR, cwrundir_default, sizeof(cw_config_CW_RUN_DIR));
	cw_copy_string(cw_config_CW_SOUNDS_DIR, cwsoundsdir_default, sizeof(cw_config_CW_SOUNDS_DIR));

	/* no callweaver.conf? no problem, use buildtime config! */
	if (!cfg) {
		return;
	}
	v = cw_variable_browse(cfg, "general");
	while (v) {
		if (!strcasecmp(v->name, "cwrunuser")) {
			cw_copy_string(cw_config_CW_RUN_USER, v->value, sizeof(cw_config_CW_RUN_USER));
		} else if (!strcasecmp(v->name, "cwrungroup")) {
			cw_copy_string(cw_config_CW_RUN_GROUP, v->value, sizeof(cw_config_CW_RUN_GROUP));
		}
		v = v->next;
	}
	v = cw_variable_browse(cfg, "files");
	while (v) {
		if (!strcasecmp(v->name, "cwctlpermissions")) {
			cw_copy_string(cw_config_CW_CTL_PERMISSIONS, v->value, sizeof(cw_config_CW_CTL_PERMISSIONS));
		} else if (!strcasecmp(v->name, "cwctlowner")) {
			cw_copy_string(cw_config_CW_CTL_OWNER, v->value, sizeof(cw_config_CW_CTL_OWNER));
		} else if (!strcasecmp(v->name, "cwctlgroup")) {
			cw_copy_string(cw_config_CW_CTL_GROUP, v->value, sizeof(cw_config_CW_CTL_GROUP));
		} else if (!strcasecmp(v->name, "cwctl")) {
			cw_copy_string(cw_config_CW_CTL, v->value, sizeof(cw_config_CW_CTL));
		} else if (!strcasecmp(v->name, "cwdb")) {
			cw_copy_string(cw_config_CW_DB, v->value, sizeof(cw_config_CW_DB));
		}
		v = v->next;
	}
	v = cw_variable_browse(cfg, "directories");
	while(v) {
		if (!strcasecmp(v->name, "cwetcdir")) {
			cw_copy_string(cw_config_CW_CONFIG_DIR, v->value, sizeof(cw_config_CW_CONFIG_DIR));
		} else if (!strcasecmp(v->name, "cwspooldir")) {
			cw_copy_string(cw_config_CW_SPOOL_DIR, v->value, sizeof(cw_config_CW_SPOOL_DIR));
			snprintf(cw_config_CW_MONITOR_DIR, sizeof(cw_config_CW_MONITOR_DIR) - 1, "%s/monitor", v->value);
		} else if (!strcasecmp(v->name, "cwvarlibdir")) {
			cw_copy_string(cw_config_CW_VAR_DIR, v->value, sizeof(cw_config_CW_VAR_DIR));
		} else if (!strcasecmp(v->name, "cwdbdir")) {
			cw_copy_string(cw_config_CW_DB_DIR, v->value, sizeof(cw_config_CW_DB_DIR));
		} else if (!strcasecmp(v->name, "cwlogdir")) {
			cw_copy_string(cw_config_CW_LOG_DIR, v->value, sizeof(cw_config_CW_LOG_DIR));
		} else if (!strcasecmp(v->name, "cwogidir")) {
			cw_copy_string(cw_config_CW_OGI_DIR, v->value, sizeof(cw_config_CW_OGI_DIR));
		} else if (!strcasecmp(v->name, "cwsoundsdir")) {
			cw_copy_string(cw_config_CW_SOUNDS_DIR, v->value, sizeof(cw_config_CW_SOUNDS_DIR));
		} else if (!strcasecmp(v->name, "cwrundir")) {
			snprintf(cw_config_CW_PID, sizeof(cw_config_CW_PID), "%s/%s", v->value, "callweaver.pid");
			snprintf(cw_config_CW_SOCKET, sizeof(cw_config_CW_SOCKET), "%s/%s", v->value, cw_config_CW_CTL);
			cw_copy_string(cw_config_CW_RUN_DIR, v->value, sizeof(cw_config_CW_RUN_DIR));
		} else if (!strcasecmp(v->name, "cwmoddir")) {
			lt_dlsetsearchpath(v->value);
		} else if (!strcasecmp(v->name, "cwkeydir")) { 
			cw_copy_string(cw_config_CW_KEY_DIR, v->value, sizeof(cw_config_CW_KEY_DIR)); 
		}
		v = v->next;
	}
	v = cw_variable_browse(cfg, "options");
	while(v) {
		/* verbose level (-v at startup) */
		if (!strcasecmp(v->name, "verbose")) {
			option_verbose = atoi(v->value);
		/* whether or not to force timestamping. (-T at startup) */
		} else if (!strcasecmp(v->name, "timestamp")) {
			option_timestamp = cw_true(v->value);
		/* whether or not to support #exec in config files */
		} else if (!strcasecmp(v->name, "execincludes")) {
			option_exec_includes = cw_true(v->value);
		/* debug level (-d at startup) */
		} else if (!strcasecmp(v->name, "debug")) {
			option_debug = 0;
			if (sscanf(v->value, "%d", &option_debug) != 1) {
				option_debug = cw_true(v->value);
			}
		/* Disable forking (-f at startup) */
		} else if (!strcasecmp(v->name, "nofork")) {
			option_nofork = cw_true(v->value);
		/* Run quietly (-q at startup ) */
		} else if (!strcasecmp(v->name, "quiet")) {
			option_quiet = cw_true(v->value);
		/* Run as console (-c at startup, implies nofork) */
		} else if (!strcasecmp(v->name, "console")) {
			option_console = cw_true(v->value);
		/* Run with highg priority if the O/S permits (-p at startup) */
		} else if (!strcasecmp(v->name, "highpriority")) {
			option_highpriority = cw_true(v->value);
		/* Initialize RSA auth keys (IAX2) (-i at startup) */
		} else if (!strcasecmp(v->name, "initcrypto")) {
			option_initcrypto = cw_true(v->value);
		/* Disable ANSI colors for console (-c at startup) */
		} else if (!strcasecmp(v->name, "nocolor")) {
			option_nocolor = cw_true(v->value);
		/* Disable some usage warnings for picky people :p */
		} else if (!strcasecmp(v->name, "dontwarn")) {
			option_dontwarn = cw_true(v->value);
		/* Dump core in case of crash (-g) */
		} else if (!strcasecmp(v->name, "dumpcore")) {
			option_dumpcore = cw_true(v->value);
		/* Cache recorded sound files to another directory during recording */
		} else if (!strcasecmp(v->name, "cache_record_files")) {
			option_cache_record_files = cw_true(v->value);
		/* Specify cache directory */
		}  else if (!strcasecmp(v->name, "record_cache_dir")) {
			cw_copy_string(record_cache_dir, v->value, CW_CACHE_DIR_LEN);
		/* Build transcode paths via SLINEAR, instead of directly */
		} else if (!strcasecmp(v->name, "transcode_via_sln")) {
			option_transcode_slin = cw_true(v->value);
		} else if (!strcasecmp(v->name, "maxcalls")) {
			if ((sscanf(v->value, "%d", &option_maxcalls) != 1) || (option_maxcalls < 0)) {
				option_maxcalls = 0;
			}
		} else if (!strcasecmp(v->name, "maxload")) {
			double test[1];

			if (getloadavg(test, 1) == -1) {
				cw_log(CW_LOG_ERROR, "Cannot obtain load average on this system. 'maxload' option disabled.\n");
				option_maxload = 0.0;
			} else if ((sscanf(v->value, "%lf", &option_maxload) != 1) || (option_maxload < 0.0)) {
				option_maxload = 0.0;
			}
		} else if (!strcasecmp(v->name, "systemname")) {
			cw_copy_string(cw_config_CW_SYSTEM_NAME, v->value, sizeof(cw_config_CW_SYSTEM_NAME));
		}
		else if (!strcasecmp(v->name, "enableunsafeunload"))
		{
			cw_copy_string(cw_config_CW_ENABLE_UNSAFE_UNLOAD, v->value, sizeof(cw_config_CW_ENABLE_UNSAFE_UNLOAD));
		}
		v = v->next;
	}
	cw_config_destroy(cfg);
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
	pid_t pid;


	/* init with default */
	cw_copy_string(cw_config_CW_CONFIG_FILE, cwconffile_default, sizeof(cw_config_CW_CONFIG_FILE));
	
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

	cw_mainpid = getpid();
	cw_ulaw_init();
	cw_alaw_init();
	cw_utils_init();

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
			cw_copy_string((char *)cw_config_CW_CONFIG_FILE,optarg,sizeof(cw_config_CW_CONFIG_FILE));
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
			exit(EX_OK);
		case 'V':
			show_version();
			exit(EX_OK);
		case 'U':
			runuser = optarg;
			break;
		case 'G':
			rungroup = optarg;
			break;
		case '?':
			exit(EX_USAGE);
		}
	}

	initstate((getppid() * 65535 + getpid()) % RAND_MAX, random_state, 256);

	cw_loader_init();

	if ((option_console || option_nofork) && !option_verbose) 
		cw_verbose("[ Reading Master Configuration ]");
	cw_readconfig();

	if (option_remote || option_exec) {
		/* For remote connections, change the name of the remote connection.
		 * We do this for the benefit of init scripts (which need to know if/when
		 * the main callweaver process has died yet).
		 */
		strcpy(argv[0], "rcallweaver");
		for (x = 1; x < argc; x++) {
			argv[x] = argv[0] + 10;
		}

		if (option_exec)
			exit(console_oneshot(cw_config_CW_SOCKET, xarg));

		console(cw_config_CW_SOCKET);
		exit(EX_OK);
	}

	switch (lockfile_claim(cw_config_CW_PID)) {
		case 0: /* Already running */
			fprintf(stderr, "CallWeaver already running. Use \"callweaver -r\" to connect.\n");
			/* Fall through */
		case -1: /* Interrupted before claim */
			exit(1);
	}

	if (!option_console && !option_nofork) {
		if ((pid = fork()) == -1) {
			perror("fork");
			exit(EX_OSERR);
		} else if (pid) {
			/* We, the parent, are going to die and our child takes
			 * over all future responsibilities. Update the pid file
			 * accordingly.
			 */
			if (lockfile_rewrite(cw_config_CW_PID, pid))
				fprintf(stderr, "Unable to rewrite pid file '%s' after forking: %s\n", cw_config_CW_PID, strerror(errno));
			_exit(0);
		}

		freopen("/dev/null", "r", stdin);
		freopen("/dev/null", "w", stdout);
		freopen("/dev/null", "w", stderr);
		setsid();
	}

	do {
		int status;

		switch ((pid = fork())) {
			case -1:
				perror("fork");
				sleep(1);
				break;

			case 0:
				break;

			default:
				while (!waitpid(pid, &status, 0) && (!WIFEXITED(status) || !WIFSIGNALED(status)));

				if (WIFEXITED(status) && WEXITSTATUS(status) == EX_TEMPFAIL) {
					/* Mark all FD's for closing on exec */
					for (x = getdtablesize() - 1; x >= (option_console || option_nofork ? 3 : 0); x--)
						fcntl(x, F_SETFD, FD_CLOEXEC);
					execvp(_argv[0], _argv);
					perror("exec");
					_exit(EX_OSERR);
				}

				lockfile_release(cw_config_CW_PID);
				exit(status);
				break;
		}
	} while (pid != 0);

	if (option_dumpcore) {
		struct rlimit l;
		memset(&l, 0, sizeof(l));
		l.rlim_cur = RLIM_INFINITY;
		l.rlim_max = RLIM_INFINITY;
		if (setrlimit(RLIMIT_CORE, &l)) {
			cw_log(CW_LOG_WARNING, "Unable to disable core size resource limit: %s\n", strerror(errno));
		}
	}


	if (!is_child_of_nonroot && option_highpriority) {
		if (setpriority(PRIO_PROCESS, 0, -10) == -1)
			cw_log(CW_LOG_WARNING, "Unable to set high priority\n");
	}

	if (!runuser)
		runuser = cw_config_CW_RUN_USER;
	if (!rungroup)
		rungroup = cw_config_CW_RUN_GROUP;
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
			cw_log(CW_LOG_WARNING, "Unable to keep capabilities: %s\n", strerror(errno));
		}
#endif

		gr = getgrnam(rungroup);
		if (!gr) {
			cw_log(CW_LOG_ERROR, "No such group '%s'!\n", rungroup);
			exit(EX_NOUSER);
		}
		pw = getpwnam(runuser);
		if (!pw) {
			cw_log(CW_LOG_ERROR, "No such user '%s'!\n", runuser);
			exit(EX_NOUSER);
		}
		
		if (gr->gr_gid != getegid() )
		if (initgroups(pw->pw_name, gr->gr_gid) == -1) {
			cw_log(CW_LOG_ERROR, "Unable to initgroups '%s' (%d)\n", pw->pw_name, gr->gr_gid);
			exit(EX_OSERR);
		}

		if (setregid(gr->gr_gid, gr->gr_gid)) {
			cw_log(CW_LOG_ERROR, "Unable to setgid to '%s' (%d)\n", gr->gr_name, gr->gr_gid);
			exit(EX_OSERR);
		}
		if (option_verbose) {
			int ngroups;
			gid_t gid_list[NGROUPS_MAX];
			int i;
			struct group *gr2;

			gr2 = getgrgid(getegid());
			if (gr2) {
				cw_verbose("Now running as group '%s' (%d)\n", gr2->gr_name, gr2->gr_gid);
			} else {
				cw_verbose("Now running as group '' (%d)\n", getegid());
			}

			cw_verbose("Supplementary groups:\n");
			ngroups = getgroups(NGROUPS_MAX, gid_list);
			for (i = 0; i < ngroups; i++) {
				gr2 = getgrgid(gid_list[i]);
				if (gr2) {
					cw_verbose("   '%s' (%d)\n", gr2->gr_name, gr2->gr_gid);
				} else {
					cw_verbose("   '' (%d)\n", gid_list[i]);
				}
			}
		}
#ifdef __Darwin__
		if (seteuid(pw->pw_uid)) {
#else
		if (setreuid(pw->pw_uid, pw->pw_uid)) {
#endif
			cw_log(CW_LOG_ERROR, "Unable to setuid to '%s' (%d)\n", pw->pw_name, pw->pw_uid);
			exit(EX_OSERR);
		}
		setenv("CALLWEAVER_ALREADY_NONROOT","yes",1);
		if (option_verbose) {
			struct passwd *pw2;
			pw2 = getpwuid(geteuid());
			if (pw2) {
				cw_verbose("Now running as user '%s' (%d)\n", pw2->pw_name, pw2->pw_uid);
			} else {
				cw_verbose("Now running as user '' (%d)\n", getegid());
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
	if (!geteuid())
		cw_log(CW_LOG_ERROR, "Running as root is EXTREMELY dangerous. See the documentation!\n");

#if defined(__linux__)
	/* after set*id() the dumpable flag is deleted,
	   so we set it again to get core dumps */
	if (option_dumpcore) {
		if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == -1) {
			cw_log(CW_LOG_ERROR, "Unable to set dumpable flag: %s\n", strerror(errno));
		}
	}
#endif

	cw_clock_init();

	if ((option_console || option_nofork) && !option_verbose) 
		cw_verbose("[ Initializing Custom Configuration Options ]");

	cw_registry_init(&atexit_registry);
	cw_registry_init(&cdrbe_registry);
	cw_registry_init(&clicmd_registry);
	cw_registry_init(&config_engine_registry);
	cw_registry_init(&format_registry);
	cw_registry_init(&func_registry);
	cw_registry_init(&imager_registry);
	cw_registry_init(&manager_listener_registry);
	cw_registry_init(&manager_session_registry);
	cw_registry_init(&manager_action_registry);
	cw_registry_init(&switch_registry);
	cw_registry_init(&translator_registry);

	/* custom config setup */
	register_config_cli();
	read_config_maps();

	/* Initialize the core services */
	if (init_logger()
	|| init_manager())
	    exit(EX_USAGE);

	/* Test recursive mutex locking. */
	if (test_for_thread_safety())
		cw_verbose("Warning! CallWeaver is not thread safe.\n");

	for (x = 0; x < arraysize(consoles); x++)	
		consoles[x].fd = -1;

	unlink(cw_config_CW_SOCKET);

	cw_makesocket(cw_config_CW_SOCKET);

	cw_register_verbose(network_verboser);

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
	cw_cli_init();

	if (option_console || option_nofork) {
		if (cw_pthread_create(&consolethread, &global_attr_default, console, cw_config_CW_SOCKET)) {
			cw_log(CW_LOG_ERROR, "Failed to start console - console is not available\n");
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
				cw_module_reload(NULL);
				break;
			case SIGTERM:
			case SIGINT:
				shutdown_restart(-1, 1, 0, -1);
				break;
		}
	}

	return 0;
}
