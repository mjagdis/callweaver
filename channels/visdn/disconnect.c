/*
 * vISDN channel driver for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifdef HAVE_CONFIG_H
 #include "confdefs.h"
#endif

//#include <callweaver/astmm.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include <callweaver/lock.h>
#include <callweaver/channel.h>
#include <callweaver/config.h>
#include <callweaver/logger.h>
#include <callweaver/module.h>
#include <callweaver/pbx.h>
#include <callweaver/options.h>
#include <callweaver/cli.h>
#include <callweaver/causes.h>

#include "chan_visdn.h"
#include "disconnect.h"

static int visdn_exec_disconnect(struct cw_channel *chan, int argc, char **argv)
{
	cw_indicate(chan, CW_CONTROL_DISCONNECT);

	return 0;
}

static char *visdn_disconnect_descr =
"  Disconnect():\n";

void visdn_disconnect_register(void)
{
	cw_register_application(
		"Disconnect",
		visdn_exec_disconnect,
		"Send a Disconnect frame",
		"Disconnect()",
		visdn_disconnect_descr);
}

void visdn_disconnect_unregister(void)
{
	cw_unregister_application("Disconnect");
}
