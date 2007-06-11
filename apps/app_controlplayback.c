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
 * \brief Trivial application to control playback of a sound file
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif
 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/app.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/utils.h"

static const char *tdesc = "Control Playback Application";

static void *controlplayback_app;
static const char *controlplayback_name = "ControlPlayback";
static const char *controlplayback_synopsis = "Play a file with fast forward and rewind";
static const char *controlplayback_syntax = "ControlPlayback(filename[, skipms[, ffchar[, rewchar[, stopchar[, pausechar[, restartchar]]]]]])";
static const char *controlplayback_descrip = 
"  Plays  back  a  given  filename (do not put extension). Options may also\n"
"  be included following a pipe symbol.  You can use * and # to rewind and\n"
"  fast forward the playback specified. If 'stopchar' is added the file will\n"
"  terminate playback when 'stopchar' is pressed. If 'restartchar' is added, the file\n"
"  will restart when 'restartchar' is pressed. Returns -1 if the channel\n"
"  was hung up. if the file does not exist jumps to n+101 if it present.\n\n"
"  Example:  exten => 1234,1, ControlPlayback(file, 4000, *, #, 1, 0, 5)\n\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int is_on_phonepad(char key)
{
	return key == 35 || key == 42 || (key >= 48 && key <= 57);
}

static int controlplayback_exec(struct opbx_channel *chan, int argc, char **argv)
{
	int res = 0;
	int skipms = 0;
	struct localuser *u;
	int i;

	if (argc < 1 || argc > 7) {
		opbx_log(LOG_ERROR, "Syntax: %s\n", controlplayback_syntax);
		return -1;
	}

	LOCAL_USER_ADD(u);
	
	skipms = (argc > 1 && argv[1] ? atoi(argv[1]) : 3000);
	if (!skipms)
		skipms = 3000;

	for (i = 2; i < 6; i++) {
		if (i >= argc || !argv[i] || !is_on_phonepad(argv[i][0]))
			argv[i] = NULL;
	}
	if (!argv[2]) argv[2] = "#";
	if (!argv[3]) argv[2] = "*";

	res = opbx_control_streamfile(chan, argv[0], argv[2], argv[3], argv[4], argv[5], argv[6], skipms);

	/* If we stopped on one of our stop keys, return 0  */
	if (argv[4] && strchr(argv[4], res)) 
		res = 0;

	if (res < 0) {
		if (opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101))
			res = 0;
	}

	LOCAL_USER_REMOVE(u);

	return res;
}

int unload_module(void)
{
	int res = 0;

	res |= opbx_unregister_application(controlplayback_app);
	STANDARD_HANGUP_LOCALUSERS;
	return res;
}

int load_module(void)
{
	controlplayback_app = opbx_register_application(controlplayback_name, controlplayback_exec, controlplayback_synopsis, controlplayback_syntax, controlplayback_descrip);
	return 0;
}

char *description(void)
{
	return (char *) tdesc;
}

int usecount(void)
{
	int res;

	STANDARD_USECOUNT(res);
	return res;
}


