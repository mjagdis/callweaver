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
 * \brief Routines implementing music on hold
 *
 * \arg See also \ref Config_moh
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/options.h"
#include "callweaver/atexit.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/say.h"
#include "callweaver/musiconhold.h"
#include "callweaver/config.h"
#include "callweaver/utils.h"
#include "callweaver/cli.h"


#define MAX_MOHFILES 512
#define MAX_MOHFILE_LEN 128

static void *app0;
static void *app1;
static void *app2;
static void *app3;
static void *app4;

static const char name0[] = "MusicOnHold";
static const char name1[] = "WaitMusicOnHold";
static const char name2[] = "SetMusicOnHold";
static const char name3[] = "StartMusicOnHold";
static const char name4[] = "StopMusicOnHold";

static const char synopsis0[] = "Play Music On Hold indefinitely";
static const char synopsis1[] = "Wait, playing Music On Hold";
static const char synopsis2[] = "Set default Music On Hold class";
static const char synopsis3[] = "Play Music On Hold";
static const char synopsis4[] = "Stop Playing Music On Hold";

static const char syntax0[] = "MusicOnHold(class)";
static const char syntax1[] = "WaitMusicOnHold(delay)";
static const char syntax2[] = "SetMusicOnHold(class)";
static const char syntax3[] = "StartMusicOnHold(class)";
static const char syntax4[] = "StopMusicOnHold";

static const char descrip0[] =
"Plays hold music specified by class.  If omitted, the default\n"
"music source for the channel will be used. Set the default \n"
"class with the SetMusicOnHold() application.\n"
"Returns -1 on hangup.\n"
"Never returns otherwise.\n";

static const char descrip1[] =
"Plays hold music specified number of seconds.  Returns 0 when\n"
"done, or -1 on hangup.  If no hold music is available, the delay will\n"
"still occur with no sound.\n";

static const char descrip2[] =
"Sets the default class for music on hold for a given channel.  When\n"
"music on hold is activated, this class will be used to select which\n"
"music is played.\n";

static const char descrip3[] =
"Starts playing music on hold, uses default music class for channel.\n"
"Starts playing music specified by class.  If omitted, the default\n"
"music source for the channel will be used.  Always returns 0.\n";

static const char descrip4[] =
"Stops playing music on hold.\n";


struct moh_files_state {
	struct mohclass *class;
	int origwfmt;
	int samples;
	int sample_queue;
	unsigned char pos;
	unsigned char save_pos;
};

#define MOH_CUSTOM		(1 << 0)
#define MOH_RANDOMIZE		(1 << 1)

struct mohclass {
	char name[MAX_MUSICCLASS];
	char dir[256];
	char args[256];
	char mode[80];
	char filearray[MAX_MOHFILES][MAX_MOHFILE_LEN];
	unsigned int flags;
	int total_files;
	int format;
	int pid;		/* PID of custom command */
	pthread_t thread;
	struct mohdata *members;
	/* Source of audio */
	int srcfd;
	struct mohclass *next;
};

struct mohdata {
	int pipe[2];
	int origwfmt;
	struct mohclass *parent;
	struct mohdata *next;
};

static struct mohclass *mohclasses;

OPBX_MUTEX_DEFINE_STATIC(moh_lock);

static void opbx_moh_free_class(struct mohclass *class) 
{
	struct mohdata *members, *mtmp;

	members = class->members;
	while(members) {
		mtmp = members;
		members = members->next;
		free(mtmp);
	}
	free(class);
}


static void moh_files_release(struct opbx_channel *chan, void *data)
{
	struct moh_files_state *state = chan->music_state;

	if (chan && state) {
		if (option_verbose > 2)
			opbx_verbose(VERBOSE_PREFIX_3 "Stopped music on hold on %s\n", chan->name);

		if (state->origwfmt && opbx_set_write_format(chan, state->origwfmt)) {
			opbx_log(OPBX_LOG_WARNING, "Unable to restore channel '%s' to format '%d'\n", chan->name, state->origwfmt);
		}
		state->save_pos = state->pos + 1;
	}
}


static int opbx_moh_files_next(struct opbx_channel *chan) 
{
	struct moh_files_state *state = chan->music_state;
	int tries;

	if (state->save_pos) {
		state->pos = state->save_pos - 1;
		state->save_pos = 0;
	} else {
		/* Try 20 times to find something good */
		for (tries=0;tries < 20;tries++) {
			state->samples = 0;
			if (chan->stream) {
				opbx_closestream(chan->stream);
				chan->stream = NULL;
				state->pos++;
			}

			if (opbx_test_flag(state->class, MOH_RANDOMIZE))
				state->pos = rand();

			state->pos %= state->class->total_files;

			/* check to see if this file's format can be opened */
			if (opbx_fileexists(state->class->filearray[state->pos], NULL, NULL))
				break;

		}
	}

	state->pos = state->pos % state->class->total_files;
/* Check it	
	if (opbx_set_write_format(chan, OPBX_FORMAT_SLINEAR)) {
		opbx_log(OPBX_LOG_WARNING, "Unable to set '%s' to linear format (write)\n", chan->name);
		return -1;
	}
*/
	if (!opbx_openstream_full(chan, state->class->filearray[state->pos], chan->language, 1)) {
		opbx_log(OPBX_LOG_WARNING, "Unable to open file '%s': %s\n", state->class->filearray[state->pos], strerror(errno));
		state->pos++;
		return -1;
	}

	if (option_debug)
		opbx_log(OPBX_LOG_DEBUG, "%s Opened file %d '%s'\n", chan->name, state->pos, state->class->filearray[state->pos]);

	if (state->samples)
		opbx_seekstream(chan->stream, state->samples, SEEK_SET);

	return 0;
}


static struct opbx_frame *moh_files_readframe(struct opbx_channel *chan) 
{
	struct opbx_frame *f = NULL;
	
	if (!opbx_test_flag(chan, OPBX_FLAG_ZOMBIE)) {
		if (!(chan->stream && (f = opbx_readframe(chan->stream)))) {
			if (!opbx_moh_files_next(chan))
    				f = opbx_readframe(chan->stream);
               	}
        }

	return f;
}

static int moh_files_generator(struct opbx_channel *chan, void *data, int samples)
{
	struct moh_files_state *state = chan->music_state;
	struct opbx_frame *f = NULL;
	int res = 0;

	state->sample_queue += samples;

	while (state->sample_queue > 0) {
		if ((f = moh_files_readframe(chan))) {
			state->samples += f->samples;
			res = opbx_write(chan, f);
			state->sample_queue -= f->samples;
			opbx_fr_free(f);
			if (res < 0) {
				opbx_log(OPBX_LOG_WARNING, "Unable to write data: %s\n", strerror(errno));
				return -1;
			}
		} else
			return -1;	
	}
	return res;
}


static void *moh_files_alloc(struct opbx_channel *chan, void *params)
{
	struct moh_files_state *state;
	struct mohclass *class = params;
	int allocated = 0;

	if (!chan->music_state && (state = malloc(sizeof(struct moh_files_state)))) {
		chan->music_state = state;
		allocated = 1;
	} else 
		state = chan->music_state;

	if (state) {
		if (allocated || state->class != class) {
			/* initialize */
			memset(state, 0, sizeof(struct moh_files_state));
			state->class = class;
		}

		state->origwfmt = chan->writeformat;
/*
		if (opbx_set_write_format(chan, OPBX_FORMAT_SLINEAR)) {
			opbx_log(OPBX_LOG_WARNING, "Unable to set '%s' to linear format (write)\n", chan->name);
			free(chan->music_state);
			chan->music_state = NULL;
		} else {
			if (option_verbose > 2)
				opbx_verbose(VERBOSE_PREFIX_3 "Started music on hold, class '%s', on %s\n", class->name, chan->name);
		}
*/
		if (option_verbose > 2)
			opbx_verbose(VERBOSE_PREFIX_3 "Started music on hold, class '%s', on %s\n", class->name, chan->name);

	}
	
	return chan->music_state;
}

static struct opbx_generator moh_file_stream = 
{
	alloc: moh_files_alloc,
	release: moh_files_release,
	generate: moh_files_generator,
};

static int spawn_custom_command(struct mohclass *class)
{
	int fds[2];
	int files = 0;
	char fns[MAX_MOHFILES][MAX_MOHFILE_LEN];
	char *argv[MAX_MOHFILES + 50];
	char xargs[256];
	char *argptr;
	int argc = 0;
	DIR *dir = NULL;
	struct dirent *de;

	
	if (!strcasecmp(class->dir, "nodir")) {
		files = 1;
	} else {
		dir = opendir(class->dir);
		if (!dir) {
			opbx_log(OPBX_LOG_WARNING, "%s is not a valid directory\n", class->dir);
			return -1;
		}
	}

	/* Format arguments for argv vector */
	strncpy(xargs, class->args, sizeof(xargs) - 1);
	argptr = xargs;
	while (argptr && !opbx_strlen_zero(argptr)) {
		argv[argc++] = argptr;
		argptr = strchr(argptr, ' ');
		if (argptr) {
			*argptr = '\0';
			argptr++;
		}
	}

	if (dir) {
		while ((de = readdir(dir)) && (files < MAX_MOHFILES)) {
			if (de->d_name[0] != '.') {
				strncpy(fns[files], de->d_name, sizeof(fns[files]) - 1);
				argv[argc++] = fns[files];
				files++;
			}
		}
	}
	argv[argc] = NULL;
	if (dir) {
		closedir(dir);
	}
	if (!files) {
		opbx_log(OPBX_LOG_WARNING, "Found no files in '%s'\n", class->dir);
		return -1;
	}
	if (pipe(fds)) {	
		opbx_log(OPBX_LOG_WARNING, "Pipe failed\n");
		return -1;
	}
#if 0
	printf("%d files total, %d args total\n", files, argc);
	{
		int x;
		for (x=0;argv[x];x++)
			printf("arg%d: %s\n", x, argv[x]);
	}
#endif	
	class->pid = fork();
	if (class->pid < 0) {
		close(fds[0]);
		close(fds[1]);
		opbx_log(OPBX_LOG_WARNING, "Fork failed: %s\n", strerror(errno));
		return -1;
	}
	if (!class->pid) {
		/* Child */
		int x;
		close(fds[0]);
		/* Stdout goes to pipe */
		dup2(fds[1], STDOUT_FILENO);
		/* Close unused file descriptors */
		for (x = 3; x < 8192; x++)
			close(x);
		chdir(class->dir);
		execv(argv[0], argv);
		opbx_log(OPBX_LOG_WARNING, "Exec failed: %s\n", strerror(errno));
		close(fds[1]);
		exit(1);
	} else {
		/* Parent */
		close(fds[1]);
	}
	return fds[0];
}

static void monitor_custom_command_cleanup(void *data)
{
	struct mohclass *class = data;
	struct mohdata *moh;

	if (class->pid) {
		if (option_debug)
			opbx_log(OPBX_LOG_DEBUG, "killing %d!\n", class->pid);

		kill(class->pid, SIGKILL);
		if (class->srcfd >= 0)
			close(class->srcfd);
	}

	opbx_moh_free_class(class);
}

static void *monitor_custom_command(void *data)
{
#define	MOH_MS_INTERVAL		100

	struct mohclass *class = data;
	struct mohdata *moh;
	short sbuf[8192];
	int res, res2;
	int len;
	struct timeval tv, tv_tmp;
	long delta;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	pthread_cleanup_push(monitor_custom_command_cleanup, class);

	for(;/* ever */;) {
		/* Spawn custom command if it's not there */
		if (class->srcfd < 0) {
			if ((class->srcfd = spawn_custom_command(class)) < 0) {
				opbx_log(OPBX_LOG_WARNING, "Unable to spawn custom command\n");
				/* Try again later */
				if (!class->members) {
					pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
					pthread_testcancel();
				}
				sleep(60);
				if (!class->members)
					pthread_testcancel();
				pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
				continue;
			}
		}

		/* Reliable sleep */
		if (!class->members) {
			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
			pthread_testcancel();
		}
		tv_tmp = opbx_tvnow();
		if (opbx_tvzero(tv))
			tv = tv_tmp;
		delta = opbx_tvdiff_ms(tv_tmp, tv);
		if (delta < MOH_MS_INTERVAL) {	/* too early */
			tv = opbx_tvadd(tv, opbx_samp2tv(MOH_MS_INTERVAL, 1000));	/* next deadline */
			usleep(1000 * (MOH_MS_INTERVAL - delta));
		} else {
			opbx_log(OPBX_LOG_NOTICE, "Request to schedule in the past?!?!\n");
			tv = tv_tmp;
		}

		if (!class->members) {
			pthread_testcancel();
			continue;
		}

		/* Read audio */
		res = 8 * MOH_MS_INTERVAL;	/* 8 samples per millisecond */
		len = opbx_codec_get_len(class->format, res);

		res2 = read(class->srcfd, sbuf, len);
		if (!class->members)
			pthread_testcancel();
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		if (res2 != len) {
			if (!res2) {
				close(class->srcfd);
				class->srcfd = -1;
				if (class->pid) {
					kill(class->pid, SIGKILL);
					class->pid = 0;
				}
			} else if (option_debug)
				opbx_log(OPBX_LOG_DEBUG, "Read %d bytes of audio while expecting %d: %s\n", res2, len, strerror(errno));
			continue;
		}

		opbx_mutex_lock(&moh_lock);
		for (moh = class->members; moh; moh = moh->next) {
			/* Write data */
			if ((res = write(moh->pipe[1], sbuf, res2)) != res2)  {
				if (res == -1) {
					opbx_log(OPBX_LOG_WARNING, "Failed to write to pipe (%d): %s\n", moh->pipe[1], strerror(errno));
				} else if (option_debug) {
					opbx_log(OPBX_LOG_DEBUG, "Only wrote %d of %d bytes to pipe %d\n", res, res2, moh->pipe[1]);
				}
			}
			if (option_debug > 8) {
				opbx_log(OPBX_LOG_DEBUG, "Wrote %d bytes to pipe with handle %d\n", res, moh->pipe[1]);
			}
		}
		opbx_mutex_unlock(&moh_lock);
	}

	pthread_cleanup_pop(1);
	return NULL;
}

static int moh0_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	if (opbx_moh_start(chan, argv[0])) {
		opbx_log(OPBX_LOG_WARNING, "Unable to start music on hold (class '%s') on channel %s\n", argv[0], chan->name);
		return -1;
	}
	while (!opbx_safe_sleep(chan, 10000));
	opbx_moh_stop(chan);
	return -1;
}

static int moh1_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	int res;
	if (argc != 1 || !(res = atoi(argv[0])))
		return opbx_function_syntax("WaitMusicOnHold(seconds)");

	if (opbx_moh_start(chan, NULL)) {
		opbx_log(OPBX_LOG_WARNING, "Unable to start music on hold for %d seconds on channel %s\n", res, chan->name);
		return -1;
	}
	res = opbx_safe_sleep(chan, res * 1000);
	opbx_moh_stop(chan);
	return res;
}

static int moh2_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	if (argc != 1 || !argv[0][0])
		return opbx_function_syntax("SetMusicOnHold(class)");

	strncpy(chan->musicclass, argv[0], sizeof(chan->musicclass) - 1);
	return 0;
}

static int moh3_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	char *class = (argc > 1 && argv[0][0] ? argv[0] : "default");

	if (opbx_moh_start(chan, class))
		opbx_log(OPBX_LOG_NOTICE, "Unable to start music on hold class '%s' on channel %s\n", class, chan->name);

	return 0;
}

static int moh4_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	opbx_moh_stop(chan);

	return 0;
}

static struct mohclass *get_mohbyname(char *name)
{
	struct mohclass *moh;
	moh = mohclasses;
	while (moh) {
		if (!strcasecmp(name, moh->name))
			return moh;
		moh = moh->next;
	}
	return NULL;
}

static struct mohdata *mohalloc(struct mohclass *cl)
{
	struct mohdata *moh;
	int flags;
	int res;

	moh = malloc(sizeof(struct mohdata));
	if (!moh) {
		opbx_log(OPBX_LOG_WARNING, "Out of memory\n");
		return NULL;
	}
	memset(moh, 0, sizeof(struct mohdata));
	if (pipe(moh->pipe)) {
		opbx_log(OPBX_LOG_WARNING, "Failed to create pipe: %s\n", strerror(errno));
		free(moh);
		return NULL;
	}
	/* Make entirely non-blocking */
	flags = fcntl(moh->pipe[0], F_GETFL);
	if (flags == -1) {
		opbx_log(OPBX_LOG_WARNING, "Failed to get flags for moh->pipe[0](%d): %s\n", moh->pipe[0], strerror(errno));
		free(moh);
		return NULL;
	}

	res = fcntl(moh->pipe[0], F_SETFL, flags | O_NONBLOCK);
	if (res == -1) {
		opbx_log(OPBX_LOG_WARNING, "Failed to set flags for moh->pipe[0](%d): %s\n", moh->pipe[0], strerror(errno));
		free(moh);
		return NULL;
	}

	flags = fcntl(moh->pipe[1], F_GETFL);
	if (flags == -1) {
		opbx_log(OPBX_LOG_WARNING, "Failed to get flags for moh->pipe[1](%d): %s\n", moh->pipe[1], strerror(errno));
		free(moh);
		return NULL;
	}

	fcntl(moh->pipe[1], F_SETFL, flags | O_NONBLOCK);
	if (res == -1) {
		opbx_log(OPBX_LOG_WARNING, "Failed to set flags for moh->pipe[1](%d): %s\n", moh->pipe[1], strerror(errno));
		free(moh);
		return NULL;
	}

	moh->parent = cl;
	moh->next = cl->members;
	cl->members = moh;
	return moh;
}

static void moh_release(struct opbx_channel *chan, void *data)
{
	struct mohdata *moh = data, **next;

	opbx_mutex_lock(&moh_lock);

	for (next = &moh->parent->members; *next; next = &(*next)->next) {
		if (*next == moh) {
			*next = moh->next;
			break;
		}
	}

	if (chan && moh->origwfmt && opbx_set_write_format(chan, moh->origwfmt)) 
		opbx_log(OPBX_LOG_WARNING, "Unable to restore channel '%s' to format %s\n", chan->name, opbx_getformatname(moh->origwfmt));

	opbx_mutex_unlock(&moh_lock);

	opbx_log(OPBX_LOG_NOTICE, "Attempting to close pipe FDs %d and %d\n", moh->pipe[0], moh->pipe[1]);
	close(moh->pipe[0]);
	close(moh->pipe[1]);
	free(moh);

	if (chan && option_verbose > 2)
		opbx_verbose(VERBOSE_PREFIX_3 "Stopped music on hold on %s\n", chan->name);
}

static void *moh_alloc(struct opbx_channel *chan, void *params)
{
	struct mohdata *res;
	struct mohclass *class = params;

	res = mohalloc(class);
	if (res) {
		res->origwfmt = chan->writeformat;
		if (opbx_set_write_format(chan, class->format)) {
			opbx_log(OPBX_LOG_WARNING, "Unable to set channel '%s' to format '%s'\n", chan->name, opbx_codec2str(class->format));
			moh_release(NULL, res);
			res = NULL;
		}
		if (option_verbose > 2)
			opbx_verbose(VERBOSE_PREFIX_3 "Started music on hold, class '%s', on channel '%s'\n", class->name, chan->name);
	}
	return res;
}

static int moh_generate(struct opbx_channel *chan, void *data, int samples)
{
	struct opbx_frame f;
	struct mohdata *moh = data;
	short buf[1280 + OPBX_FRIENDLY_OFFSET / 2];
	int len, res;

	if (!moh->parent->pid)
		return -1;

	len = opbx_codec_get_len(moh->parent->format, samples);

	if (len > sizeof(buf) - OPBX_FRIENDLY_OFFSET) {
		opbx_log(OPBX_LOG_WARNING, "Only doing %d of %d requested bytes on %s\n", (int)sizeof(buf), len, chan->name);
		len = sizeof(buf) - OPBX_FRIENDLY_OFFSET;
	}
	res = read(moh->pipe[0], buf + OPBX_FRIENDLY_OFFSET/2, len);
#if 0
	if (res != len) {
		opbx_log(OPBX_LOG_WARNING, "Read only %d of %d bytes: %s\n", res, len, strerror(errno));
	}
#endif
	if (res > 0) {
		opbx_fr_init_ex(&f, OPBX_FRAME_VOICE, moh->parent->format, NULL);
		f.datalen = res;
		f.data = buf + OPBX_FRIENDLY_OFFSET/2;
		f.offset = OPBX_FRIENDLY_OFFSET;
		f.samples = opbx_codec_get_samples(&f);
		res = 0;

		if (opbx_write(chan, &f) < 0) {
			opbx_log(OPBX_LOG_WARNING, "Failed to write frame to '%s': %s\n", chan->name, strerror(errno));
			res = -1;
		}
	} else if (res < 0) {
		/* This can happen either because the custom command has only just
		 * been started and is not yet providing data OR because it is
		 * unable to provide data fast enough on occasion OR because the
		 * monitor_custom_command thread is unable to pass data from the
		 * custom command pipe to the generator pipes quickly enough on
		 * occasion.
		 * The first _always_ happens. The last occasionally happens even
		 * on a reasonably fast dual cored AMD64 with a single call in MOH.
		 */
		if (errno == EAGAIN)
			res = 0;
	} else {
		res = -1;
	}

	return res;
}

static struct opbx_generator mohgen = 
{
	alloc: moh_alloc,
	release: moh_release,
	generate: moh_generate,
};

static int moh_scan_files(struct mohclass *class) {

	DIR *files_DIR;
	struct dirent *files_dirent;
	char path[512];
	char filepath[MAX_MOHFILE_LEN];
	char *ext;
	struct stat statbuf;
	int dirnamelen;
	int i;
	
	files_DIR = opendir(class->dir);
	if (!files_DIR) {
		opbx_log(OPBX_LOG_WARNING, "Cannot open dir %s or dir does not exist\n", class->dir);
		return -1;
	}

	class->total_files = 0;
	dirnamelen = strlen(class->dir) + 2;
	getcwd(path, 512);
	chdir(class->dir);
	memset(class->filearray, 0, MAX_MOHFILES*MAX_MOHFILE_LEN);
	while ((files_dirent = readdir(files_DIR))) {
		if ((files_dirent->d_name[0] == '.') || ((strlen(files_dirent->d_name) + dirnamelen) >= MAX_MOHFILE_LEN))
			continue;

		snprintf(filepath, MAX_MOHFILE_LEN, "%s/%s", class->dir, files_dirent->d_name);

		if (stat(filepath, &statbuf))
			continue;

		if (!S_ISREG(statbuf.st_mode))
			continue;

		if ((ext = strrchr(filepath, '.'))) {
			*ext = '\0';
			ext++;
		}

		/* if the file is present in multiple formats, ensure we only put it into the list once */
		for (i = 0; i < class->total_files; i++)
			if (!strcmp(filepath, class->filearray[i]))
				break;

		if (i == class->total_files)
			strcpy(class->filearray[class->total_files++], filepath);
	}

	closedir(files_DIR);
	chdir(path);
	return class->total_files;
}

static int moh_register(struct mohclass *moh)
{
	opbx_mutex_lock(&moh_lock);
	if (get_mohbyname(moh->name)) {
		opbx_log(OPBX_LOG_WARNING, "Music on Hold class '%s' already exists\n", moh->name);
		free(moh);	
		opbx_mutex_unlock(&moh_lock);
		return -1;
	}
	opbx_mutex_unlock(&moh_lock);

	if (!strcasecmp(moh->mode, "files")) {
		if (!moh_scan_files(moh)) {
			opbx_moh_free_class(moh);
			return -1;
		}
		if (strchr(moh->args, 'r'))
			opbx_set_flag(moh, MOH_RANDOMIZE);
	} else if (!strcasecmp(moh->mode, "custom")) {
		
		opbx_set_flag(moh, MOH_CUSTOM);
		
		moh->srcfd = -1;
		if (opbx_pthread_create(&moh->thread, NULL, monitor_custom_command, moh)) {
			opbx_log(OPBX_LOG_WARNING, "Unable to create moh...\n");
			opbx_moh_free_class(moh);
			return -1;
		}
	} else {
		opbx_log(OPBX_LOG_WARNING, "Don't know how to do a mode '%s' music on hold\n", moh->mode);
		opbx_moh_free_class(moh);
		return -1;
	}
	opbx_mutex_lock(&moh_lock);
	moh->next = mohclasses;
	mohclasses = moh;
	opbx_mutex_unlock(&moh_lock);
	return 0;
}

static void local_opbx_moh_cleanup(struct opbx_channel *chan)
{
	if (chan->music_state) {
		free(chan->music_state);
		chan->music_state = NULL;
	}
}

static int local_opbx_moh_start(struct opbx_channel *chan, char *class)
{
	struct mohclass *mohclass;

	if (!class || opbx_strlen_zero(class))
		class = chan->musicclass;
	if (!class || opbx_strlen_zero(class))
		class = "default";
	opbx_mutex_lock(&moh_lock);
	mohclass = get_mohbyname(class);
	opbx_mutex_unlock(&moh_lock);

	if (!mohclass) {
		opbx_log(OPBX_LOG_WARNING, "No class: %s\n", (char *)class);
		return -1;
	}

	/* Stop any generators that might be running */
	opbx_generator_deactivate(chan);

	opbx_set_flag(chan, OPBX_FLAG_MOH);
	if (mohclass->total_files) {
		return opbx_generator_activate(chan, &moh_file_stream, mohclass);
	} else
		return opbx_generator_activate(chan, &mohgen, mohclass);
}

static void local_opbx_moh_stop(struct opbx_channel *chan)
{
	opbx_clear_flag(chan, OPBX_FLAG_MOH);
	opbx_generator_deactivate(chan);

	if (chan->music_state) {
		if (chan->stream) {
			opbx_closestream(chan->stream);
			chan->stream = NULL;
		}
	}
}

static struct mohclass *moh_class_malloc(void)
{
	struct mohclass *class;

	class = malloc(sizeof(struct mohclass));

	if (!class)
		return NULL;

	memset(class, 0, sizeof(struct mohclass));

	class->format = OPBX_FORMAT_SLINEAR;

	return class;
}

static int load_moh_classes(void)
{
	struct opbx_config *cfg;
	struct opbx_variable *var;
	struct mohclass *class;	
	char *data;
	char *args;
	char *cat;
	int numclasses = 0;
	static int dep_warning = 0;

	cfg = opbx_config_load("musiconhold.conf");

	if (!cfg)
		return 0;

	cat = opbx_category_browse(cfg, NULL);
	for (; cat; cat = opbx_category_browse(cfg, cat)) {
		if (strcasecmp(cat, "classes") && strcasecmp(cat, "moh_files")) {
			class = moh_class_malloc();
			if (!class) {
				opbx_log(OPBX_LOG_WARNING, "Out of memory!\n");
				break;
			}				
			opbx_copy_string(class->name, cat, sizeof(class->name));	
			var = opbx_variable_browse(cfg, cat);
			while (var) {
				if (!strcasecmp(var->name, "mode"))
					opbx_copy_string(class->mode, var->value, sizeof(class->name)); 
				else if (!strcasecmp(var->name, "directory"))
					opbx_copy_string(class->dir, var->value, sizeof(class->dir));
				else if (!strcasecmp(var->name, "application"))
					opbx_copy_string(class->args, var->value, sizeof(class->args));
				else if (!strcasecmp(var->name, "random"))
					opbx_set2_flag(class, opbx_true(var->value), MOH_RANDOMIZE);
				else if (!strcasecmp(var->name, "format")) {
					class->format = opbx_getformatbyname(var->value);
					if (!class->format) {
						opbx_log(OPBX_LOG_WARNING, "Unknown format '%s' -- defaulting to SLIN\n", var->value);
						class->format = OPBX_FORMAT_SLINEAR;
					}
				}
					var = var->next;
			}

			if (opbx_strlen_zero(class->dir)) {
				if (!strcasecmp(class->mode, "custom")) {
					strcpy(class->dir, "nodir");
				} else {
					opbx_log(OPBX_LOG_WARNING, "A directory must be specified for class '%s'!\n", class->name);
					free(class);
					continue;
				}
			}
			if (opbx_strlen_zero(class->mode)) {
				opbx_log(OPBX_LOG_WARNING, "A mode must be specified for class '%s'!\n", class->name);
				free(class);
				continue;
			}
			if (opbx_strlen_zero(class->args) && !strcasecmp(class->mode, "custom")) {
				opbx_log(OPBX_LOG_WARNING, "An application must be specified for class '%s'!\n", class->name);
				free(class);
				continue;
			}

			moh_register(class);
			numclasses++;
		}
	}
	

	/* Deprecated Old-School Configuration */
	var = opbx_variable_browse(cfg, "classes");
	while (var) {
		if (!dep_warning) {
			opbx_log(OPBX_LOG_WARNING, "The old musiconhold.conf syntax has been deprecated!  Please refer to the sample configuration for information on the new syntax.\n");
			dep_warning = 1;
		}
		data = strchr(var->value, ':');
		if (data) {
			*data++ = '\0';
			args = strchr(data, ',');
			if (args)
				*args++ = '\0';
			if (!(get_mohbyname(var->name))) {
				class = moh_class_malloc();
				if (!class) {
					opbx_log(OPBX_LOG_WARNING, "Out of memory!\n");
					return numclasses;
				}
				
				opbx_copy_string(class->name, var->name, sizeof(class->name));
				opbx_copy_string(class->dir, data, sizeof(class->dir));
				opbx_copy_string(class->mode, var->value, sizeof(class->mode));
				if (args)
					opbx_copy_string(class->args, args, sizeof(class->args));
				
				moh_register(class);
				numclasses++;
			}
		}
		var = var->next;
	}
	var = opbx_variable_browse(cfg, "moh_files");
	while (var) {
		if (!dep_warning) {
			opbx_log(OPBX_LOG_WARNING, "The old musiconhold.conf syntax has been deprecated!  Please refer to the sample configuration for information on the new syntax.\n");
			dep_warning = 1;
		}
		if (!(get_mohbyname(var->name))) {
			args = strchr(var->value, ',');
			if (args)
				*args++ = '\0';
			class = moh_class_malloc();
			if (!class) {
				opbx_log(OPBX_LOG_WARNING, "Out of memory!\n");
				return numclasses;
			}
			
			opbx_copy_string(class->name, var->name, sizeof(class->name));
			opbx_copy_string(class->dir, var->value, sizeof(class->dir));
			strcpy(class->mode, "files");
			if (args)	
				opbx_copy_string(class->args, args, sizeof(class->args));
			
			moh_register(class);
			numclasses++;
		}
		var = var->next;
	}

	opbx_config_destroy(cfg);

	return numclasses;
}

static void moh_on_off(int on)
{
	struct opbx_channel *chan = NULL;

	while ( (chan = opbx_channel_walk_locked(chan)) != NULL) {
		if (opbx_test_flag(chan, OPBX_FLAG_MOH)) {
			if (on)
				local_opbx_moh_start(chan, NULL);
			else
				opbx_generator_deactivate(chan);
		}
		opbx_mutex_unlock(&chan->lock);
	}
}

static int moh_cli(int fd, int argc, char *argv[]) 
{
	struct mohclass *moh;
	int x;

	/* FIXME: logically this should be after we have the moh_lock so nothing
	 * else can start before we destroy the old classes. But that leads to
	 * a deadlock???
	 */
	moh_on_off(0);

	if (option_verbose > 1)
		opbx_verbose(VERBOSE_PREFIX_2 "Destroying musiconhold processes\n");

	opbx_mutex_lock(&moh_lock);
	while ((moh = mohclasses)) {
		mohclasses = mohclasses->next;
		if (moh->thread) {
			pthread_t tid = moh->thread;
			pthread_cancel(tid); 
			pthread_join(tid, NULL);
		} else
			opbx_moh_free_class(moh);
	}
	opbx_mutex_unlock(&moh_lock);

	x = load_moh_classes();
	moh_on_off(1);
	opbx_cli(fd, "\n%d class%s reloaded.\n", x, x == 1 ? "" : "es");
	return 0;
}

static int cli_files_show(int fd, int argc, char *argv[])
{
	int i;
	struct mohclass *class;

	opbx_mutex_lock(&moh_lock);
	for (class = mohclasses; class; class = class->next) {
		if (!class->total_files)
			continue;

		opbx_cli(fd, "Class: %s\n", class->name);
		for (i = 0; i < class->total_files; i++)
			opbx_cli(fd, "\tFile: %s\n", class->filearray[i]);
	}
	opbx_mutex_unlock(&moh_lock);

	return 0;
}

static int moh_classes_show(int fd, int argc, char *argv[])
{
	struct mohclass *class;

	opbx_mutex_lock(&moh_lock);
	for (class = mohclasses; class; class = class->next) {
		opbx_cli(fd, "Class: %s\n", class->name);
		opbx_cli(fd, "\tMode: %s\n", opbx_strlen_zero(class->mode) ? "<none>" : class->mode);
		opbx_cli(fd, "\tDirectory: %s\n", opbx_strlen_zero(class->dir) ? "<none>" : class->dir);
		if (opbx_test_flag(class, MOH_CUSTOM))
			opbx_cli(fd, "\tApplication: %s\n", opbx_strlen_zero(class->args) ? "<none>" : class->args);
		opbx_cli(fd, "\tFormat: %s\n", opbx_getformatname(class->format));
	}
	opbx_mutex_unlock(&moh_lock);

	return 0;
}

static struct opbx_clicmd cli_moh = {
	.cmda = { "moh", "reload"},
	.handler = moh_cli,
	.summary = "Music On Hold",
	.usage = "Music On Hold",
};

static struct opbx_clicmd cli_moh_classes_show = {
	.cmda = { "moh", "classes", "show"},
	.handler = moh_classes_show,
	.summary = "List MOH classes",
	.usage = "Lists all MOH classes",
};

static struct opbx_clicmd cli_moh_files_show = {
	.cmda = { "moh", "files", "show"},
	.handler = cli_files_show,
	.summary = "List MOH file-based classes",
	.usage = "Lists all loaded file-based MOH classes and their files",
};

static void moh_killall(void)
{
	struct mohclass *class;

	for (class = mohclasses; class; class = class->next)
		kill(class->pid, SIGKILL);
}

static struct opbx_atexit moh_atexit = {
	.name = "Music On Hold terminate",
	.function = moh_killall,
};


static int load_module(void)
{
	/* We should never be unloaded */
	opbx_module_get(get_modinfo()->self);

	app0 = opbx_register_function(name0, moh0_exec, synopsis0, syntax0, descrip0);
	opbx_atexit_register(&moh_atexit);
	opbx_cli_register(&cli_moh);
	opbx_cli_register(&cli_moh_files_show);
	opbx_cli_register(&cli_moh_classes_show);
	app1 = opbx_register_function(name1, moh1_exec, synopsis1, syntax1, descrip1);
	app2 = opbx_register_function(name2, moh2_exec, synopsis2, syntax2, descrip2);
	app3 = opbx_register_function(name3, moh3_exec, synopsis3, syntax3, descrip3);
	app4 = opbx_register_function(name4, moh4_exec, synopsis4, syntax4, descrip4);

	if (!load_moh_classes()) { 	/* No music classes configured, so skip it */
		opbx_log(OPBX_LOG_WARNING, "No music on hold classes configured, disabling music on hold.\n");
	} else {
		opbx_install_music_functions(local_opbx_moh_start, local_opbx_moh_stop, local_opbx_moh_cleanup);
	}

	return 0;
}

static int reload_module(void)
{
	if (load_moh_classes())
		opbx_install_music_functions(local_opbx_moh_start, local_opbx_moh_stop, local_opbx_moh_cleanup);

	return 0;
}

static int unload_module(void)
{
	return -1;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, "Music On Hold Resource")
