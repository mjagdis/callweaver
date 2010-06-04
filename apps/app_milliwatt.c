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
 * \brief Digital Milliwatt Test
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/indications.h"

static const char tdesc[] = "Digital Milliwatt (mu-law) Test Application";

static void *milliwatt_app;
static const char milliwatt_name[] = "Milliwatt";
static const char milliwatt_synopsis[] = "Generate a Constant 1004Hz tone at 0dbm (mu-law)";
static const char milliwatt_syntax[] = "Milliwatt()";
static const char milliwatt_descrip[] = 
"Generate a Constant 1004Hz tone at 0dbm (mu-law)\n";


static int milliwatt_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	static char deprecated = 0;
	struct localuser *u;

	CW_UNUSED(argc);
	CW_UNUSED(argv);
	CW_UNUSED(result);

	if (!deprecated) {
		deprecated = 1;
		cw_log(CW_LOG_WARNING, "Milliwatt is deprecated. Use either Playtones(1004/0) or Playback(...) (to avoid transcoding)\n");
	}

	LOCAL_USER_ADD(u);

	if (chan->_state != CW_STATE_UP)
		cw_answer(chan);

	if (!cw_playtones_start(chan, 0, "1004/0", 0))
		while (!cw_safe_sleep(chan, 10000));

	cw_playtones_stop(chan);

	LOCAL_USER_REMOVE(u);
	return -1;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(milliwatt_app);
	return res;
}

static int load_module(void)
{
	milliwatt_app = cw_register_function(milliwatt_name, milliwatt_exec, milliwatt_synopsis, milliwatt_syntax, milliwatt_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
