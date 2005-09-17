/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 *
 * Silly application to play an MP3 file -- uses mpg123
 * 
 */
 
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/lock.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/frame.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/translate.h"

#define LOCAL_MPG_123 "/usr/local/bin/mpg123"
#define MPG_123 "/usr/bin/mpg123"

static char *tdesc = "Silly MP3 Application";

static char *app = "MP3Player";

static char *synopsis = "Play an MP3 file or stream";

static char *descrip = 
"  MP3Player(location) Executes mpg123 to play the given location\n"
"which typically would be a filename o a URL. User can exit by pressing any key\n."
"Returns  -1  on hangup or 0 otherwise."; 

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int mp3play(char *filename, int fd)
{
	int res;
	int x;
	res = fork();
	if (res < 0) 
		opbx_log(LOG_WARNING, "Fork failed\n");
	if (res)
		return res;
	dup2(fd, STDOUT_FILENO);
	for (x=0;x<256;x++) {
		if (x != STDOUT_FILENO)
			close(x);
	}
	/* Execute mpg123, but buffer if it's a net connection */
	if (!strncasecmp(filename, "http://", 7)) {
		/* Most commonly installed in /usr/local/bin */
	    execl(LOCAL_MPG_123, "mpg123", "-q", "-s", "-b", "1024", "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
		/* But many places has it in /usr/bin */
	    execl(MPG_123, "mpg123", "-q", "-s", "-b", "1024","-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
		/* As a last-ditch effort, try to use PATH */
	    execlp("mpg123", "mpg123", "-q", "-s", "-b", "1024",  "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
	}
	else {
		/* Most commonly installed in /usr/local/bin */
	    execl(MPG_123, "mpg123", "-q", "-s", "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
		/* But many places has it in /usr/bin */
	    execl(LOCAL_MPG_123, "mpg123", "-q", "-s", "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
		/* As a last-ditch effort, try to use PATH */
	    execlp("mpg123", "mpg123", "-q", "-s", "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
	}
	opbx_log(LOG_WARNING, "Execute of mpg123 failed\n");
	return -1;
}

static int timed_read(int fd, void *data, int datalen, int timeout)
{
	int res;
	struct pollfd fds[1];
	fds[0].fd = fd;
	fds[0].events = POLLIN;
	res = poll(fds, 1, timeout);
	if (res < 1) {
		opbx_log(LOG_NOTICE, "Poll timed out/errored out with %d\n", res);
		return -1;
	}
	return read(fd, data, datalen);
	
}

static int mp3_exec(struct opbx_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	int fds[2];
	int ms = -1;
	int pid = -1;
	int owriteformat;
	int timeout = 2000;
	struct timeval next;
	struct opbx_frame *f;
	struct myframe {
		struct opbx_frame f;
		char offset[OPBX_FRIENDLY_OFFSET];
		short frdata[160];
	} myf;
	if (!data) {
		opbx_log(LOG_WARNING, "MP3 Playback requires an argument (filename)\n");
		return -1;
	}
	if (pipe(fds)) {
		opbx_log(LOG_WARNING, "Unable to create pipe\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	opbx_stopstream(chan);

	owriteformat = chan->writeformat;
	res = opbx_set_write_format(chan, OPBX_FORMAT_SLINEAR);
	if (res < 0) {
		opbx_log(LOG_WARNING, "Unable to set write format to signed linear\n");
		return -1;
	}
	
	res = mp3play((char *)data, fds[1]);
	if (!strncasecmp((char *)data, "http://", 7)) {
		timeout = 10000;
	}
	/* Wait 1000 ms first */
	next = opbx_tvnow();
	next.tv_sec += 1;
	if (res >= 0) {
		pid = res;
		/* Order is important -- there's almost always going to be mp3...  we want to prioritize the
		   user */
		for (;;) {
			ms = opbx_tvdiff_ms(next, opbx_tvnow());
			if (ms <= 0) {
				res = timed_read(fds[0], myf.frdata, sizeof(myf.frdata), timeout);
				if (res > 0) {
					myf.f.frametype = OPBX_FRAME_VOICE;
					myf.f.subclass = OPBX_FORMAT_SLINEAR;
					myf.f.datalen = res;
					myf.f.samples = res / 2;
					myf.f.mallocd = 0;
					myf.f.offset = OPBX_FRIENDLY_OFFSET;
					myf.f.src = __PRETTY_FUNCTION__;
					myf.f.delivery.tv_sec = 0;
					myf.f.delivery.tv_usec = 0;
					myf.f.data = myf.frdata;
					if (opbx_write(chan, &myf.f) < 0) {
						res = -1;
						break;
					}
				} else {
					opbx_log(LOG_DEBUG, "No more mp3\n");
					res = 0;
					break;
				}
				next = opbx_tvadd(next, opbx_samp2tv(myf.f.samples, 8000));
			} else {
				ms = opbx_waitfor(chan, ms);
				if (ms < 0) {
					opbx_log(LOG_DEBUG, "Hangup detected\n");
					res = -1;
					break;
				}
				if (ms) {
					f = opbx_read(chan);
					if (!f) {
						opbx_log(LOG_DEBUG, "Null frame == hangup() detected\n");
						res = -1;
						break;
					}
					if (f->frametype == OPBX_FRAME_DTMF) {
						opbx_log(LOG_DEBUG, "User pressed a key\n");
						opbx_frfree(f);
						res = 0;
						break;
					}
					opbx_frfree(f);
				} 
			}
		}
	}
	close(fds[0]);
	close(fds[1]);
	LOCAL_USER_REMOVE(u);
	if (pid > -1)
		kill(pid, SIGKILL);
	if (!res && owriteformat)
		opbx_set_write_format(chan, owriteformat);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return opbx_unregister_application(app);
}

int load_module(void)
{
	return opbx_register_application(app, mp3_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}


