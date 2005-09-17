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
 * Stream to an icecast server via ICES (see contrib/openpbx-ices.xml)
 * 
 */
 
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>

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

#define ICES "/usr/bin/ices"
#define LOCAL_ICES "/usr/local/bin/ices"

static char *tdesc = "Encode and Stream via icecast and ices";

static char *app = "ICES";

static char *synopsis = "Encode and stream using 'ices'";

static char *descrip = 
"  ICES(config.xml) Streams to an icecast server using ices\n"
"(available separately).  A configuration file must be supplied\n"
"for ices (see examples/openpbx-ices.conf).  Returns  -1  on\n"
"hangup or 0 otherwise.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int icesencode(char *filename, int fd)
{
	int res;
	int x;
	res = fork();
	if (res < 0) 
		opbx_log(LOG_WARNING, "Fork failed\n");
	if (res)
		return res;
	dup2(fd, STDIN_FILENO);
	for (x=STDERR_FILENO + 1;x<256;x++) {
		if ((x != STDIN_FILENO) && (x != STDOUT_FILENO))
			close(x);
	}
	/* Most commonly installed in /usr/local/bin */
	execl(ICES, "ices", filename, (char *)NULL);
	/* But many places has it in /usr/bin */
	execl(LOCAL_ICES, "ices", filename, (char *)NULL);
	/* As a last-ditch effort, try to use PATH */
	execlp("ices", "ices", filename, (char *)NULL);
	opbx_log(LOG_WARNING, "Execute of ices failed\n");
	return -1;
}

static int ices_exec(struct opbx_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	int fds[2];
	int ms = -1;
	int pid = -1;
	int flags;
	int oreadformat;
	struct timeval last;
	struct opbx_frame *f;
	char filename[256]="";
	char *c;
	last.tv_usec = 0;
	last.tv_sec = 0;
	if (!data || !strlen(data)) {
		opbx_log(LOG_WARNING, "ICES requires an argument (configfile.xml)\n");
		return -1;
	}
	if (pipe(fds)) {
		opbx_log(LOG_WARNING, "Unable to create pipe\n");
		return -1;
	}
	flags = fcntl(fds[1], F_GETFL);
	fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);
	
	LOCAL_USER_ADD(u);
	opbx_stopstream(chan);

	if (chan->_state != OPBX_STATE_UP)
		res = opbx_answer(chan);
		
	if (res) {
		close(fds[0]);
		close(fds[1]);
		opbx_log(LOG_WARNING, "Answer failed!\n");
		return -1;
	}

	oreadformat = chan->readformat;
	res = opbx_set_read_format(chan, OPBX_FORMAT_SLINEAR);
	if (res < 0) {
		close(fds[0]);
		close(fds[1]);
		opbx_log(LOG_WARNING, "Unable to set write format to signed linear\n");
		return -1;
	}
	if (((char *)data)[0] == '/')
		strncpy(filename, (char *)data, sizeof(filename) - 1);
	else
		snprintf(filename, sizeof(filename), "%s/%s", (char *)opbx_config_OPBX_CONFIG_DIR, (char *)data);
	/* Placeholder for options */		
	c = strchr(filename, '|');
	if (c)
		*c = '\0';	
	res = icesencode(filename, fds[0]);
	close(fds[0]);
	if (res >= 0) {
		pid = res;
		for (;;) {
			/* Wait for audio, and stream */
			ms = opbx_waitfor(chan, -1);
			if (ms < 0) {
				opbx_log(LOG_DEBUG, "Hangup detected\n");
				res = -1;
				break;
			}
			f = opbx_read(chan);
			if (!f) {
				opbx_log(LOG_DEBUG, "Null frame == hangup() detected\n");
				res = -1;
				break;
			}
			if (f->frametype == OPBX_FRAME_VOICE) {
				res = write(fds[1], f->data, f->datalen);
				if (res < 0) {
					if (errno != EAGAIN) {
						opbx_log(LOG_WARNING, "Write failed to pipe: %s\n", strerror(errno));
						res = -1;
						break;
					}
				}
			}
			opbx_frfree(f);
		}
	}
	close(fds[1]);
	LOCAL_USER_REMOVE(u);
	if (pid > -1)
		kill(pid, SIGKILL);
	if (!res && oreadformat)
		opbx_set_read_format(chan, oreadformat);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return opbx_unregister_application(app);
}

int load_module(void)
{
	return opbx_register_application(app, ices_exec, synopsis, descrip);
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

char *key()
{
	return OPENPBX_GPL_KEY;
}
