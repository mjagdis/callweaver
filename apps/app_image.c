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
 * App to transmit an image
 * 
 */
 
#include <string.h>
#include <stdlib.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/lock.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/translate.h"
#include "openpbx/image.h"

static char *tdesc = "Image Transmission Application";

static char *app = "SendImage";

static char *synopsis = "Send an image file";

static char *descrip = 
"  SendImage(filename): Sends an image on a channel. If the channel\n"
"does not support  image transport, and there exists  a  step  with\n"
"priority n + 101, then  execution  will  continue  at  that  step.\n"
"Otherwise,  execution  will continue at  the  next priority level.\n"
"SendImage only  returns  0 if  the  image was sent correctly or if\n"
"the channel does not support image transport, and -1 otherwise.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int sendimage_exec(struct opbx_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	if (!data || !strlen((char *)data)) {
		opbx_log(LOG_WARNING, "SendImage requires an argument (filename)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	if (!opbx_supports_images(chan)) {
		/* Does not support transport */
		if (opbx_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num))
			chan->priority += 100;
		return 0;
	}
	res = opbx_send_image(chan, data);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return opbx_unregister_application(app);
}

int load_module(void)
{
	return opbx_register_application(app, sendimage_exec, synopsis, descrip);
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


