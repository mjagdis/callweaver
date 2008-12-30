/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Joshua Colp
 *
 * Joshua Colp <jcolp@asterlink.com>
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
 * \brief Directed Call Pickup Support
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"
#include "callweaver/app.h"

static const char tdesc[] = "Directed Call Pickup Application";

static void *pickup_app;
static const char pickup_name[] = "Pickup";
static const char pickup_synopsis[] = "Directed Call Pickup application.";
static const char pickup_syntax[] = "Pickup(extension[@context])";
static const char pickup_descrip[] =
"Steals any calls to a specified extension that are in a ringing state and bridges them to the current channel. Context is an optional argument.\n";


static int pickup_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	char workspace[256] = "";
	struct localuser *u = NULL;
	struct cw_channel *origin = NULL, *target = NULL;
	char *tmp = NULL, *exten = NULL, *context = NULL;
	int res = 0;

	if (argc != 1)
		return cw_function_syntax(pickup_syntax);

	LOCAL_USER_ADD(u);
	
	/* Get the extension and context if present */
	exten = argv[0];
	context = strchr(argv[0], '@');
	if (context) {
		*context = '\0';
		context++;
	}

	/* Find a channel to pickup */
	origin = cw_get_channel_by_exten_locked(exten, context);
	if (origin) {
		cw_cdr_getvar(origin->cdr, "dstchannel", &tmp, workspace,
			       sizeof(workspace), 0);
		if (tmp) {
			/* We have a possible channel... now we need to find it! */
			target = cw_get_channel_by_name_locked(tmp);
		} else {
			cw_log(CW_LOG_DEBUG, "No target channel found.\n");
			res = -1;
		}
		cw_channel_unlock(origin);
		cw_object_put(origin);
	} else {
		cw_log(CW_LOG_DEBUG, "No originating channel found.\n");
	}
	
	if (res)
		goto out;

	if (target && (!target->pbx) && ((target->_state == CW_STATE_RINGING) || (target->_state == CW_STATE_RING))) {
		cw_log(CW_LOG_DEBUG, "Call pickup on chan '%s' by '%s'\n", target->name,
			chan->name);
		res = cw_answer(chan);
		if (res) {
			cw_log(CW_LOG_WARNING, "Unable to answer '%s'\n", chan->name);
			res = -1;
			goto out;
		}
		res = cw_queue_control(chan, CW_CONTROL_ANSWER);
		if (res) {
			cw_log(CW_LOG_WARNING, "Unable to queue answer on '%s'\n",
				chan->name);
			res = -1;
			goto out;
		}
		res = cw_channel_masquerade(target, chan);
		if (res) {
			cw_log(CW_LOG_WARNING, "Unable to masquerade '%s' into '%s'\n", chan->name, target->name);
			res = -1;
			goto out;
		}
	} else {
		cw_log(CW_LOG_DEBUG, "No call pickup possible...\n");
		res = -1;
	}
	
 out:
	if (target) {
		cw_channel_unlock(target);
		cw_object_put(target);
	}

	LOCAL_USER_REMOVE(u);

	return res;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(pickup_app);
	return res;
}

static int load_module(void)
{
	pickup_app = cw_register_function(pickup_name, pickup_exec, pickup_synopsis, pickup_syntax, pickup_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
