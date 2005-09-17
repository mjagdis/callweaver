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
 * Silly application to play an NBScat file -- uses nbscat8k
 * 
 */
 
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/socket.h>

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

#define LOCAL_NBSCAT "/usr/local/bin/nbscat8k"
#define NBSCAT "/usr/bin/nbscat8k"

#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#endif

static char *tdesc = "Silly NBS Stream Application";

static char *app = "NBScat";

static char *synopsis = "Play an NBS local stream";

static char *descrip = 
"  NBScat: Executes nbscat to listen to the local NBS stream.\n"
"Returns  -1  on\n hangup or 0 otherwise. User can exit by \n"
"pressing any key\n.";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int NBScatplay(int fd)
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
	/* Most commonly installed in /usr/local/bin */
	execl(NBSCAT, "nbscat8k", "-d", (char *)NULL);
	execl(LOCAL_NBSCAT, "nbscat8k", "-d", (char *)NULL);
	opbx_log(LOG_WARNING, "Execute of nbscat8k failed\n");
	return -1;
}

static int timed_read(int fd, void *data, int datalen)
{
	int res;
	struct pollfd fds[1];
	fds[0].fd = fd;
	fds[0].events = POLLIN;
	res = poll(fds, 1, 2000);
	if (res < 1) {
		opbx_log(LOG_NOTICE, "Selected timed out/errored out with %d\n", res);
		return -1;
	}
	return read(fd, data, datalen);
	
}

static int NBScat_exec(struct opbx_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	int fds[2];
	int ms = -1;
	int pid = -1;
	int owriteformat;
	struct timeval next;
	struct opbx_frame *f;
	struct myframe {
		struct opbx_frame f;
		char offset[OPBX_FRIENDLY_OFFSET];
		short frdata[160];
	} myf;
	if (socketpair(AF_LOCAL, SOCK_STREAM, 0, fds)) {
		opbx_log(LOG_WARNING, "Unable to create socketpair\n");
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
	
	res = NBScatplay(fds[1]);
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
				res = timed_read(fds[0], myf.frdata, sizeof(myf.frdata));
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
	return opbx_register_application(app, NBScat_exec, synopsis, descrip);
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


