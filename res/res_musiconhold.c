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
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/object.h"
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
	char **files;
	unsigned int flags;
	int total_files;
	int total_space;
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
	struct cw_frame f;
	int16_t buf[1280 + CW_FRIENDLY_OFFSET / sizeof(int16_t)];
};

static struct mohclass *mohclasses;

CW_MUTEX_DEFINE_STATIC(moh_lock);

static void cw_moh_free_class(struct mohclass *class) 
{
	struct mohdata *members, *mtmp;
	int i;

	members = class->members;
	while(members) {
		mtmp = members;
		members = members->next;
		free(mtmp);
	}

	for (i = 0; i < class->total_files; i++)
		free(class->files[i]);
	free(class->files);
	free(class);
}


static void moh_files_release(struct cw_channel *chan, void *data)
{
	struct moh_files_state *state = chan->music_state;

	CW_UNUSED(data);

	if (chan && state) {
		if (option_verbose > 2)
			cw_verbose(VERBOSE_PREFIX_3 "Stopped music on hold on %s\n", chan->name);

		if (state->origwfmt && cw_set_write_format(chan, state->origwfmt)) {
			cw_log(CW_LOG_WARNING, "Unable to restore channel '%s' to format '%d'\n", chan->name, state->origwfmt);
		}
		state->save_pos = state->pos + 1;
	}
}


static int cw_moh_files_next(struct cw_channel *chan) 
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
				cw_closestream(chan->stream);
				chan->stream = NULL;
				state->pos++;
			}

			if (cw_test_flag(state->class, MOH_RANDOMIZE))
				state->pos = cw_random();

			state->pos %= state->class->total_files;

			/* check to see if this file's format can be opened */
			if (cw_fileexists(state->class->files[state->pos], NULL, NULL))
				break;

		}
	}

	state->pos = state->pos % state->class->total_files;
/* Check it	
	if (cw_set_write_format(chan, CW_FORMAT_SLINEAR)) {
		cw_log(CW_LOG_WARNING, "Unable to set '%s' to linear format (write)\n", chan->name);
		return -1;
	}
*/
	if (!cw_openstream_full(chan, state->class->files[state->pos], chan->language, 1)) {
		cw_log(CW_LOG_WARNING, "Unable to open file '%s': %s\n", state->class->files[state->pos], strerror(errno));
		state->pos++;
		return -1;
	}

	if (option_debug)
		cw_log(CW_LOG_DEBUG, "%s Opened file %d '%s'\n", chan->name, state->pos, state->class->files[state->pos]);

	if (state->samples)
		cw_seekstream(chan->stream, state->samples, SEEK_SET);

	return 0;
}


static struct cw_frame *moh_files_readframe(struct cw_channel *chan) 
{
	struct cw_frame *f = NULL;
	
	if (!cw_test_flag(chan, CW_FLAG_ZOMBIE)) {
		if (!(chan->stream && (f = cw_readframe(chan->stream)))) {
			if (!cw_moh_files_next(chan))
    				f = cw_readframe(chan->stream);
               	}
        }

	return f;
}

static struct cw_frame *moh_files_generator(struct cw_channel *chan, void *data, int samples)
{
	struct moh_files_state *state = data;
	struct cw_frame *f = NULL;

	state->sample_queue += samples;

	if ((f = moh_files_readframe(chan))) {
		state->samples += f->samples;
		state->sample_queue -= f->samples;
		return f;
	}
	return NULL;
}


static void *moh_files_alloc(struct cw_channel *chan, void *params)
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
		if (cw_set_write_format(chan, CW_FORMAT_SLINEAR)) {
			cw_log(CW_LOG_WARNING, "Unable to set '%s' to linear format (write)\n", chan->name);
			free(chan->music_state);
			chan->music_state = NULL;
		} else {
			if (option_verbose > 2)
				cw_verbose(VERBOSE_PREFIX_3 "Started music on hold, class '%s', on %s\n", class->name, chan->name);
		}
*/
		if (option_verbose > 2)
			cw_verbose(VERBOSE_PREFIX_3 "Started music on hold, class '%s', on %s\n", class->name, chan->name);

	}
	
	return chan->music_state;
}

static struct cw_generator moh_file_stream = 
{
	alloc: moh_files_alloc,
	release: moh_files_release,
	generate: moh_files_generator,
};


static int moh_realloc(char ***ptr, size_t *size, size_t el_size)
{
	void *n_ptr;
	int n_size = *size + 16;

	if ((n_ptr = realloc(*ptr, el_size * n_size))) {
		*ptr = n_ptr;
		*size = n_size;
		return 0;
	}

	cw_log(CW_LOG_ERROR, "Out of memory!\n");
	return -1;
}


static int spawn_custom_command(struct mohclass *class)
{
	char xargs[sizeof(class->args)];
	char **argv = NULL;
	char *argptr;
	DIR *dir = NULL;
	struct dirent *de;
	int fds[2];
	size_t argv_size = 0;
	size_t argc = 0;

	if (strcasecmp(class->dir, "nodir")) {
		dir = opendir(class->dir);
		if (!dir) {
			cw_log(CW_LOG_WARNING, "%s is not a valid directory\n", class->dir);
			return -1;
		}
	}

	/* Format arguments for argv vector */
	strncpy(xargs, class->args, sizeof(xargs) - 1);
	argptr = xargs;
	while (argptr && !cw_strlen_zero(argptr)) {
		if (argc == argv_size && moh_realloc(&argv, &argv_size, sizeof(argv[0])))
			break;

		argv[argc++] = argptr;
		argptr = strchr(argptr, ' ');
		if (argptr) {
			*argptr = '\0';
			argptr++;
		}
	}

	if (dir) {
		while ((de = readdir(dir))) {
			if (de->d_name[0] != '.') {
				if (argc == argv_size && moh_realloc(&argv, &argv_size, sizeof(argv[0])))
					break;

				if ((argv[argc] = strdup(de->d_name)))
					argc++;
				else {
					cw_log(CW_LOG_ERROR, "Out of memory!\n");
					break;
				}
			}
		}

		closedir(dir);
	}

	if (argc == argv_size && moh_realloc(&argv, &argv_size, sizeof(argv[0])))
		argc--;
	argv[argc] = NULL;

	if (pipe(fds)) {	
		cw_log(CW_LOG_WARNING, "Pipe failed\n");
		return -1;
	}
#if 0
	printf("%d args total\n", argc);
	{
		int x;
		for (x=0;argv[x];x++)
			printf("arg%d: %s\n", x, argv[x]);
	}
#endif	
#if defined(HAVE_WORKING_FORK)
	class->pid = fork();
#else
	class->pid = vfork();
#endif
	if (class->pid < 0) {
		close(fds[0]);
		close(fds[1]);
		cw_log(CW_LOG_WARNING, "Fork failed: %s\n", strerror(errno));
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
		cw_log(CW_LOG_WARNING, "Exec failed: %s\n", strerror(errno));
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

	if (class->pid) {
		if (option_debug)
			cw_log(CW_LOG_DEBUG, "killing %d!\n", class->pid);

		kill(class->pid, SIGKILL);
		if (class->srcfd >= 0)
			close(class->srcfd);
	}

	cw_moh_free_class(class);
}

static void *monitor_custom_command(void *data)
{
#define	MOH_MS_INTERVAL		100

	short sbuf[8192];
	struct mohclass *class = data;
	struct mohdata *moh;
	struct timeval tv, tv_tmp;
	long delta;
	int res, res2;
	const int len = cw_codec_get_len(class->format, 8 * MOH_MS_INTERVAL);

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	pthread_cleanup_push(monitor_custom_command_cleanup, class);

	for(;/* ever */;) {
		/* Spawn custom command if it's not there */
		if (class->srcfd < 0) {
			if ((class->srcfd = spawn_custom_command(class)) < 0) {
				cw_log(CW_LOG_WARNING, "Unable to spawn custom command\n");
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
		tv_tmp = cw_tvnow();
		if (cw_tvzero(tv))
			tv = tv_tmp;
		delta = cw_tvdiff_ms(tv_tmp, tv);
		if (delta < MOH_MS_INTERVAL) {	/* too early */
			tv = cw_tvadd(tv, cw_samp2tv(MOH_MS_INTERVAL, 1000));	/* next deadline */
			usleep(1000 * (MOH_MS_INTERVAL - delta));
		} else {
			cw_log(CW_LOG_NOTICE, "Request to schedule in the past?!?!\n");
			tv = tv_tmp;
		}

		if (!class->members) {
			pthread_testcancel();
			continue;
		}

		/* Read audio */
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
				cw_log(CW_LOG_DEBUG, "Read %d bytes of audio while expecting %d: %s\n", res2, len, strerror(errno));
			continue;
		}

		cw_mutex_lock(&moh_lock);
		for (moh = class->members; moh; moh = moh->next) {
			/* Write data */
			if ((res = write(moh->pipe[1], sbuf, res2)) != res2)  {
				if (res == -1) {
					cw_log(CW_LOG_WARNING, "Failed to write to pipe (%d): %s\n", moh->pipe[1], strerror(errno));
				} else if (option_debug) {
					cw_log(CW_LOG_DEBUG, "Only wrote %d of %d bytes to pipe %d\n", res, res2, moh->pipe[1]);
				}
			}
			if (option_debug > 8) {
				cw_log(CW_LOG_DEBUG, "Wrote %d bytes to pipe with handle %d\n", res, moh->pipe[1]);
			}
		}
		cw_mutex_unlock(&moh_lock);
	}

	pthread_cleanup_pop(1);
	return NULL;
}

static int moh0_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(result);

	if (cw_moh_start(chan, argv[0])) {
		cw_log(CW_LOG_WARNING, "Unable to start music on hold (class '%s') on channel %s\n", argv[0], chan->name);
		return -1;
	}
	while (!cw_safe_sleep(chan, 10000));
	cw_moh_stop(chan);
	return -1;
}

static int moh1_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	int res;

	CW_UNUSED(result);

	if (argc != 1 || !(res = atoi(argv[0])))
		return cw_function_syntax("WaitMusicOnHold(seconds)");

	if (cw_moh_start(chan, NULL)) {
		cw_log(CW_LOG_WARNING, "Unable to start music on hold for %d seconds on channel %s\n", res, chan->name);
		return -1;
	}
	res = cw_safe_sleep(chan, res * 1000);
	cw_moh_stop(chan);
	return res;
}

static int moh2_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(result);

	if (argc != 1 || !argv[0][0])
		return cw_function_syntax("SetMusicOnHold(class)");

	strncpy(chan->musicclass, argv[0], sizeof(chan->musicclass) - 1);
	return 0;
}

static int moh3_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	const char *class = (argc > 1 && argv[0][0] ? argv[0] : "default");

	CW_UNUSED(result);

	if (cw_moh_start(chan, class))
		cw_log(CW_LOG_NOTICE, "Unable to start music on hold class '%s' on channel %s\n", class, chan->name);

	return 0;
}

static int moh4_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);
	CW_UNUSED(result);

	cw_moh_stop(chan);

	return 0;
}

static struct mohclass *get_mohbyname(const char *name)
{
	struct mohclass *moh;

	for (moh = mohclasses; moh; moh = moh->next) {
		if (!strcasecmp(name, moh->name))
			return moh;
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
		cw_log(CW_LOG_WARNING, "Out of memory\n");
		return NULL;
	}
	memset(moh, 0, sizeof(struct mohdata));
	if (pipe(moh->pipe)) {
		cw_log(CW_LOG_WARNING, "Failed to create pipe: %s\n", strerror(errno));
		free(moh);
		return NULL;
	}
	/* Make entirely non-blocking */
	flags = fcntl(moh->pipe[0], F_GETFL);
	if (flags == -1) {
		cw_log(CW_LOG_WARNING, "Failed to get flags for moh->pipe[0](%d): %s\n", moh->pipe[0], strerror(errno));
		free(moh);
		return NULL;
	}

	res = fcntl(moh->pipe[0], F_SETFL, flags | O_NONBLOCK);
	if (res == -1) {
		cw_log(CW_LOG_WARNING, "Failed to set flags for moh->pipe[0](%d): %s\n", moh->pipe[0], strerror(errno));
		free(moh);
		return NULL;
	}

	flags = fcntl(moh->pipe[1], F_GETFL);
	if (flags == -1) {
		cw_log(CW_LOG_WARNING, "Failed to get flags for moh->pipe[1](%d): %s\n", moh->pipe[1], strerror(errno));
		free(moh);
		return NULL;
	}

	fcntl(moh->pipe[1], F_SETFL, flags | O_NONBLOCK);
	if (res == -1) {
		cw_log(CW_LOG_WARNING, "Failed to set flags for moh->pipe[1](%d): %s\n", moh->pipe[1], strerror(errno));
		free(moh);
		return NULL;
	}

	moh->parent = cl;
	moh->next = cl->members;
	cl->members = moh;
	return moh;
}

static void moh_release(struct cw_channel *chan, void *data)
{
	struct mohdata *moh = data, **next;

	cw_mutex_lock(&moh_lock);

	for (next = &moh->parent->members; *next; next = &(*next)->next) {
		if (*next == moh) {
			*next = moh->next;
			break;
		}
	}

	if (chan && moh->origwfmt && cw_set_write_format(chan, moh->origwfmt)) 
		cw_log(CW_LOG_WARNING, "Unable to restore channel '%s' to format %s\n", chan->name, cw_getformatname(moh->origwfmt));

	cw_mutex_unlock(&moh_lock);

	close(moh->pipe[0]);
	close(moh->pipe[1]);
	free(moh);

	if (chan && option_verbose > 2)
		cw_verbose(VERBOSE_PREFIX_3 "Stopped music on hold on %s\n", chan->name);
}

static void *moh_alloc(struct cw_channel *chan, void *params)
{
	struct mohdata *res;
	struct mohclass *class = params;

	res = mohalloc(class);
	if (res) {
		res->origwfmt = chan->writeformat;
		if (cw_set_write_format(chan, class->format)) {
			cw_log(CW_LOG_WARNING, "Unable to set channel '%s' to format '%s'\n", chan->name, cw_codec2str(class->format));
			moh_release(NULL, res);
			res = NULL;
		}
		if (option_verbose > 2)
			cw_verbose(VERBOSE_PREFIX_3 "Started music on hold, class '%s', on channel '%s'\n", class->name, chan->name);
	}
	return res;
}

static struct cw_frame *moh_generate(struct cw_channel *chan, void *data, int samples)
{
	struct mohdata *moh = data;
	int len, res;

	CW_UNUSED(chan);

	if (!moh->parent->pid)
		return NULL;

	len = cw_codec_get_len(moh->parent->format, samples);

	if (len > sizeof(moh->buf) - CW_FRIENDLY_OFFSET)
		len = sizeof(moh->buf) - CW_FRIENDLY_OFFSET;

	res = read(moh->pipe[0], moh->buf + CW_FRIENDLY_OFFSET / sizeof(moh->buf[0]), len);
#if 0
	if (res != len) {
		cw_log(CW_LOG_WARNING, "Read only %d of %d bytes: %s\n", res, len, strerror(errno));
	}
#endif
	cw_fr_init_ex(&moh->f, CW_FRAME_VOICE, moh->parent->format);
	if (res > 0) {
		moh->f.datalen = res;
		moh->f.data = moh->buf + CW_FRIENDLY_OFFSET / sizeof(moh->buf[0]);
		moh->f.offset = CW_FRIENDLY_OFFSET;
		moh->f.samples = cw_codec_get_samples(&moh->f);
		return &moh->f;
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
			return &moh->f;
	}

	return NULL;
}

static struct cw_generator mohgen = 
{
	alloc: moh_alloc,
	release: moh_release,
	generate: moh_generate,
};

static int moh_scan_files(struct mohclass *class)
{
	struct stat statbuf;
	DIR *files_DIR;
	struct dirent *files_dirent;
	char *ext;
	int dirnamelen;
	int i;

	if (!(files_DIR = opendir(class->dir))) {
		cw_log(CW_LOG_WARNING, "Cannot open dir %s or dir does not exist\n", class->dir);
		return -1;
	}

	dirnamelen = strlen(class->dir) + 2;

	while ((files_dirent = readdir(files_DIR))) {
		if ((files_dirent->d_name[0] == '.'))
			continue;

		if (class->total_files == class->total_space) {
			char **n_files;
			int n_space = class->total_space + 16;

			if (!(n_files = realloc(class->files, sizeof(class->files[0]) * n_space))) {
				cw_log(CW_LOG_WARNING, "Out of memory for Music On Hold class %s - only %d files scanned\n", class->name, class->total_files);
				break;
			}

			class->files = n_files;
			class->total_space = n_space;
		}

		if (!(class->files[class->total_files] = malloc(dirnamelen + strlen(files_dirent->d_name)))) {
			cw_log(CW_LOG_WARNING, "Out of memory for Music On Hold class %s - only %d files scanned\n", class->name, class->total_files);
			break;
		}

		sprintf(class->files[class->total_files], "%s/%s", class->dir, files_dirent->d_name);

		if (stat(class->files[class->total_files], &statbuf))
			goto discard;

		if (!S_ISREG(statbuf.st_mode))
			goto discard;

		if (!(ext = strrchr(class->files[class->total_files] + dirnamelen + 1, '.')))
			goto discard;

		*ext = '\0';

		/* if the file is present in multiple formats, ensure we only put it into the list once */
		for (i = 0; i < class->total_files; i++)
			if (!strcmp(class->files[class->total_files], class->files[i]))
				break;

		if (i == class->total_files) {
			class->total_files++;
			continue;
		}

discard:
		free(class->files[class->total_files]);
	}

	closedir(files_DIR);

	return class->total_files;
}

static int moh_register(struct mohclass *moh)
{
	cw_mutex_lock(&moh_lock);
	if (get_mohbyname(moh->name)) {
		cw_log(CW_LOG_WARNING, "Music on Hold class '%s' already exists\n", moh->name);
		free(moh);	
		cw_mutex_unlock(&moh_lock);
		return -1;
	}
	cw_mutex_unlock(&moh_lock);

	if (!strcasecmp(moh->mode, "files")) {
		if (!moh_scan_files(moh)) {
			cw_moh_free_class(moh);
			return -1;
		}
		if (strchr(moh->args, 'r'))
			cw_set_flag(moh, MOH_RANDOMIZE);
	} else if (!strcasecmp(moh->mode, "custom")) {
		
		cw_set_flag(moh, MOH_CUSTOM);
		
		moh->srcfd = -1;
		if (cw_pthread_create(&moh->thread, &global_attr_default, monitor_custom_command, moh)) {
			cw_log(CW_LOG_WARNING, "Unable to create moh...\n");
			cw_moh_free_class(moh);
			return -1;
		}
	} else {
		cw_log(CW_LOG_WARNING, "Don't know how to do a mode '%s' music on hold\n", moh->mode);
		cw_moh_free_class(moh);
		return -1;
	}
	cw_mutex_lock(&moh_lock);
	moh->next = mohclasses;
	mohclasses = moh;
	cw_mutex_unlock(&moh_lock);
	return 0;
}

static void local_cw_moh_cleanup(struct cw_channel *chan)
{
	free(chan->music_state);
	chan->music_state = NULL;
}

static int local_cw_moh_start(struct cw_channel *chan, const char *class)
{
	struct mohclass *mohclass;

	if (!class || cw_strlen_zero(class))
		class = chan->musicclass;
	if (!class || cw_strlen_zero(class))
		class = "default";
	cw_mutex_lock(&moh_lock);
	mohclass = get_mohbyname(class);
	cw_mutex_unlock(&moh_lock);

	if (!mohclass) {
		cw_log(CW_LOG_WARNING, "No class: %s\n", (char *)class);
		return -1;
	}

	/* Stop any generators that might be running */
	cw_generator_deactivate(&chan->generator);

	cw_set_flag(chan, CW_FLAG_MOH);
	if (mohclass->total_files) {
		return cw_generator_activate(chan, &chan->generator, &moh_file_stream, mohclass);
	} else
		return cw_generator_activate(chan, &chan->generator, &mohgen, mohclass);
}

static void local_cw_moh_stop(struct cw_channel *chan)
{
	cw_clear_flag(chan, CW_FLAG_MOH);
	cw_generator_deactivate(&chan->generator);

	if (chan->music_state)
		cw_stopstream(chan);
}

static struct mohclass *moh_class_malloc(void)
{
	struct mohclass *class;

	if ((class = calloc(1, sizeof(struct mohclass))))
		class->format = CW_FORMAT_SLINEAR;

	return class;
}

static int load_moh_classes(void)
{
	struct cw_config *cfg;
	struct cw_variable *var;
	struct mohclass *class;	
	char *data;
	char *args;
	char *cat;
	int numclasses = 0;
	static int dep_warning = 0;

	cfg = cw_config_load("musiconhold.conf");

	if (!cfg)
		return 0;

	cat = cw_category_browse(cfg, NULL);
	for (; cat; cat = cw_category_browse(cfg, cat)) {
		if (strcasecmp(cat, "classes") && strcasecmp(cat, "moh_files")) {
			class = moh_class_malloc();
			if (!class) {
				cw_log(CW_LOG_WARNING, "Out of memory\n");
				break;
			}				
			cw_copy_string(class->name, cat, sizeof(class->name));	
			var = cw_variable_browse(cfg, cat);
			while (var) {
				if (!strcasecmp(var->name, "mode"))
					cw_copy_string(class->mode, var->value, sizeof(class->name)); 
				else if (!strcasecmp(var->name, "directory"))
					cw_copy_string(class->dir, var->value, sizeof(class->dir));
				else if (!strcasecmp(var->name, "application"))
					cw_copy_string(class->args, var->value, sizeof(class->args));
				else if (!strcasecmp(var->name, "random"))
					cw_set2_flag(class, cw_true(var->value), MOH_RANDOMIZE);
				else if (!strcasecmp(var->name, "format")) {
					class->format = cw_getformatbyname(var->value);
					if (!class->format) {
						cw_log(CW_LOG_WARNING, "Unknown format '%s' -- defaulting to SLIN\n", var->value);
						class->format = CW_FORMAT_SLINEAR;
					}
				}
					var = var->next;
			}

			if (cw_strlen_zero(class->dir)) {
				if (!strcasecmp(class->mode, "custom")) {
					strcpy(class->dir, "nodir");
				} else {
					cw_log(CW_LOG_WARNING, "A directory must be specified for class '%s'!\n", class->name);
					free(class);
					continue;
				}
			}
			if (cw_strlen_zero(class->mode)) {
				cw_log(CW_LOG_WARNING, "A mode must be specified for class '%s'!\n", class->name);
				free(class);
				continue;
			}
			if (cw_strlen_zero(class->args) && !strcasecmp(class->mode, "custom")) {
				cw_log(CW_LOG_WARNING, "An application must be specified for class '%s'!\n", class->name);
				free(class);
				continue;
			}

			moh_register(class);
			numclasses++;
		}
	}
	

	/* Deprecated Old-School Configuration */
	var = cw_variable_browse(cfg, "classes");
	while (var) {
		if (!dep_warning) {
			cw_log(CW_LOG_WARNING, "The old musiconhold.conf syntax has been deprecated!  Please refer to the sample configuration for information on the new syntax.\n");
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
					cw_log(CW_LOG_WARNING, "Out of memory\n");
					return numclasses;
				}
				
				cw_copy_string(class->name, var->name, sizeof(class->name));
				cw_copy_string(class->dir, data, sizeof(class->dir));
				cw_copy_string(class->mode, var->value, sizeof(class->mode));
				if (args)
					cw_copy_string(class->args, args, sizeof(class->args));
				
				moh_register(class);
				numclasses++;
			}
		}
		var = var->next;
	}
	var = cw_variable_browse(cfg, "moh_files");
	while (var) {
		if (!dep_warning) {
			cw_log(CW_LOG_WARNING, "The old musiconhold.conf syntax has been deprecated!  Please refer to the sample configuration for information on the new syntax.\n");
			dep_warning = 1;
		}
		if (!(get_mohbyname(var->name))) {
			args = strchr(var->value, ',');
			if (args)
				*args++ = '\0';
			class = moh_class_malloc();
			if (!class) {
				cw_log(CW_LOG_WARNING, "Out of memory\n");
				return numclasses;
			}
			
			cw_copy_string(class->name, var->name, sizeof(class->name));
			cw_copy_string(class->dir, var->value, sizeof(class->dir));
			strcpy(class->mode, "files");
			if (args)	
				cw_copy_string(class->args, args, sizeof(class->args));
			
			moh_register(class);
			numclasses++;
		}
		var = var->next;
	}

	cw_config_destroy(cfg);

	return numclasses;
}


static int moh_on_one(struct cw_object *obj, void *data)
{
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);

	CW_UNUSED(data);

	if (cw_test_flag(chan, CW_FLAG_MOH))
		local_cw_moh_start(chan, NULL);

	return 0;
}

static int moh_off_one(struct cw_object *obj, void *data)
{
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);

	CW_UNUSED(data);

	if (cw_test_flag(chan, CW_FLAG_MOH))
		cw_generator_deactivate(&chan->generator);

	return 0;
}


static int moh_reload(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct mohclass *moh;
	int x;

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	/* FIXME: logically this should be after we have the moh_lock so nothing
	 * else can start before we destroy the old classes. But that leads to
	 * a deadlock because we'd be taking mohlock, chan->lock whereas other
	 * paths take chan->lock, mohlock.
	 */
	cw_registry_iterate(&channel_registry, moh_off_one, NULL);

	if (option_verbose > 1)
		cw_verbose(VERBOSE_PREFIX_2 "Destroying musiconhold processes\n");

	cw_mutex_lock(&moh_lock);
	while ((moh = mohclasses)) {
		mohclasses = mohclasses->next;
		if (moh->thread) {
			pthread_t tid = moh->thread;
			pthread_cancel(tid); 
			pthread_join(tid, NULL);
		} else
			cw_moh_free_class(moh);
	}
	cw_mutex_unlock(&moh_lock);

	x = load_moh_classes();
	cw_registry_iterate(&channel_registry, moh_on_one, NULL);

	if (ds_p)
		cw_dynstr_printf(ds_p, "\n%d class%s reloaded.\n", x, x == 1 ? "" : "es");

	return 0;
}

static int cli_files_show(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct mohclass *class;
	int i;

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_mutex_lock(&moh_lock);
	for (class = mohclasses; class; class = class->next) {
		if (!class->total_files)
			continue;

		cw_dynstr_printf(ds_p, "Class: %s\n", class->name);
		for (i = 0; i < class->total_files; i++)
			cw_dynstr_printf(ds_p, "\tFile: %s\n", class->files[i]);
	}
	cw_mutex_unlock(&moh_lock);

	return 0;
}

static int moh_classes_show(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct mohclass *class;

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_mutex_lock(&moh_lock);
	for (class = mohclasses; class; class = class->next) {
		cw_dynstr_tprintf(ds_p, 4,
			cw_fmtval("Class: %s\n", class->name),
			cw_fmtval("\tMode: %s\n", (cw_strlen_zero(class->mode) ? "<none>" : class->mode)),
			cw_fmtval("\tDirectory: %s\n", (cw_strlen_zero(class->dir) ? "<none>" : class->dir)),
			cw_fmtval("\tFormat: %s\n", cw_getformatname(class->format))
		);
		if (cw_test_flag(class, MOH_CUSTOM))
			cw_dynstr_printf(ds_p, "\tApplication: %s\n", cw_strlen_zero(class->args) ? "<none>" : class->args);
	}
	cw_mutex_unlock(&moh_lock);

	return 0;
}

static struct cw_clicmd cli_moh = {
	.cmda = { "moh", "reload"},
	.handler = moh_reload,
	.summary = "Music On Hold",
	.usage = "Music On Hold\n",
};

static struct cw_clicmd cli_moh_classes_show = {
	.cmda = { "moh", "classes", "show"},
	.handler = moh_classes_show,
	.summary = "List MOH classes",
	.usage = "Lists all MOH classes\n",
};

static struct cw_clicmd cli_moh_files_show = {
	.cmda = { "moh", "files", "show"},
	.handler = cli_files_show,
	.summary = "List MOH file-based classes",
	.usage = "Lists all loaded file-based MOH classes and their files\n",
};

static void moh_killall(void)
{
	struct mohclass *class;

	for (class = mohclasses; class; class = class->next)
		if (class->pid)
			kill(class->pid, SIGKILL);
}

static struct cw_atexit moh_atexit = {
	.name = "Music On Hold terminate",
	.function = moh_killall,
};


static int load_module(void)
{
	/* We should never be unloaded */
	cw_object_get(get_modinfo()->self);

	if (!mohgen.is_initialized)
		cw_object_init_obj(&mohgen.obj, CW_OBJECT_CURRENT_MODULE, 0);

	if (!moh_file_stream.is_initialized)
		cw_object_init_obj(&moh_file_stream.obj, CW_OBJECT_CURRENT_MODULE, 0);

	load_moh_classes();

	app0 = cw_register_function(name0, moh0_exec, synopsis0, syntax0, descrip0);
	cw_atexit_register(&moh_atexit);
	cw_cli_register(&cli_moh);
	cw_cli_register(&cli_moh_files_show);
	cw_cli_register(&cli_moh_classes_show);
	app1 = cw_register_function(name1, moh1_exec, synopsis1, syntax1, descrip1);
	app2 = cw_register_function(name2, moh2_exec, synopsis2, syntax2, descrip2);
	app3 = cw_register_function(name3, moh3_exec, synopsis3, syntax3, descrip3);
	app4 = cw_register_function(name4, moh4_exec, synopsis4, syntax4, descrip4);

	cw_install_music_functions(local_cw_moh_start, local_cw_moh_stop, local_cw_moh_cleanup);

	return 0;
}

static int reload_module(void)
{
	moh_reload(NULL, 0, NULL);
	return 0;
}

static int unload_module(void)
{
	return -1;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, "Music On Hold Resource")
