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

//#include <openpbx/astmm.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include <openpbx/lock.h>
#include <openpbx/channel.h>
#include <openpbx/config.h>
#include <openpbx/logger.h>
#include <openpbx/module.h>
#include <openpbx/pbx.h>
#include <openpbx/options.h>
#include <openpbx/cli.h>
#include <openpbx/causes.h>
#include <openpbx/version.h>

#include "chan_visdn.h"
#include "disconnect.h"

static int visdn_exec_disconnect(struct opbx_channel *chan, void *data)
{
	opbx_indicate(chan, OPBX_CONTROL_DISCONNECT);

	return 0;
}

static char *visdn_disconnect_descr =
"  Disconnect():\n";

void visdn_disconnect_register(void)
{
	opbx_register_application(
		"Disconnect",
		visdn_exec_disconnect,
		"Send a Disconnect frame",
		visdn_disconnect_descr);
}

void visdn_disconnect_unregister(void)
{
	opbx_unregister_application("Disconnect");
}
