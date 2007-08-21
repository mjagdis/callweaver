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
 * \brief App to flash a zap trunk
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h> 
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include ZAPTEL_H

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/image.h"
#include "callweaver/options.h"

static const char tdesc[] = "Flash zap trunk application";

static void *flash_app;
static char flash_name[] = "Flash";
static char flash_synopsis[] = "Flashes a Zap Trunk";
static char flash_syntax[] = "Flash()";
static char flash_descrip[] =
"Sends a flash on a zap trunk.  This is only a hack for\n"
"people who want to perform transfers and such via OGI and is generally\n"
"quite useless otherwise.  Returns 0 on success or -1 if this is not\n"
"a zap trunk\n";


static inline int zt_wait_event(int fd)
{
	/* Avoid the silly zt_waitevent which ignores a bunch of events */
	int i,j=0;
	i = ZT_IOMUX_SIGEVENT;
	if (ioctl(fd, ZT_IOMUX, &i) == -1) return -1;
	if (ioctl(fd, ZT_GETEVENT, &j) == -1) return -1;
	return j;
}

static int flash_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct zt_params ztp;
	struct localuser *u;
	int res = -1;
	int x;

	LOCAL_USER_ADD(u);
	if (!strcasecmp(chan->type, "Zap")) {
		memset(&ztp, 0, sizeof(ztp));
		res = ioctl(chan->fds[0], ZT_GET_PARAMS, &ztp);
		if (!res) {
			if (ztp.sigtype & __ZT_SIG_FXS) {
				x = ZT_FLASH;
				res = ioctl(chan->fds[0], ZT_HOOK, &x);
				if (!res || (errno == EINPROGRESS)) {
					if (res) {
						/* Wait for the event to finish */
						zt_wait_event(chan->fds[0]);
					}
					res = opbx_safe_sleep(chan, 1000);
					if (option_verbose > 2)
						opbx_verbose(VERBOSE_PREFIX_3 "Flashed channel %s\n", chan->name);
				} else
					opbx_log(LOG_WARNING, "Unable to flash channel %s: %s\n", chan->name, strerror(errno));
			} else
				opbx_log(LOG_WARNING, "%s is not an FXO Channel\n", chan->name);
		} else
			opbx_log(LOG_WARNING, "Unable to get parameters of %s: %s\n", chan->name, strerror(errno));
	} else
		opbx_log(LOG_WARNING, "%s is not a Zap channel\n", chan->name);
	LOCAL_USER_REMOVE(u);
	return res;
}

static int unload_module(void)
{
	int res = 0;

	res |= opbx_unregister_function(flash_app);
	return res;
}

static int load_module(void)
{
	flash_app = opbx_register_function(flash_name, flash_exec, flash_synopsis, flash_syntax, flash_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
