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
 * \brief App to transmit a text message
 * 
 */
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
#include "callweaver/module.h"
#include "callweaver/translate.h"
#include "callweaver/image.h"
#include "callweaver/options.h"

static const char tdesc[] = "Send Text Applications";

static void *sendtext_app;
static const char sendtext_name[] = "SendText";
static const char sendtext_synopsis[] = "Send a Text Message";
static const char sendtext_syntax[] = "SendText(text)";
static const char sendtext_descrip[] = 
"Sends text to current channel (callee).\n"
"Otherwise, execution will continue at the next priority level.\n"
"Result of transmission will be stored in the SENDTEXTSTATUS\n"
"channel variable:\n"
"      SUCCESS      Transmission succeeded\n"
"      FAILURE      Transmission failed\n"
"      UNSUPPORTED  Text transmission not supported by channel\n"
"\n"
"At this moment, text is supposed to be 7 bit ASCII in most channels.\n";


static int sendtext_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct localuser *u;
	int res;

	CW_UNUSED(result);

	if (argc == 0)
		return cw_function_syntax(sendtext_syntax);

	LOCAL_USER_ADD(u);

	cw_channel_lock(chan);
	if (!chan->tech->send_text) {
		cw_channel_unlock(chan);
		/* Does not support transport */
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	cw_channel_unlock(chan);

	res = 0;
	for (; !res && argc; argv++, argc--)
		res = cw_sendtext(chan, argv[0]);

	pbx_builtin_setvar_helper(chan, "SENDTEXTSTATUS", (res ? "FAILURE" : "SUCCESS"));

	LOCAL_USER_REMOVE(u);
	return 0;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(sendtext_app);
	return res;
}

static int load_module(void)
{
	sendtext_app = cw_register_function(sendtext_name, sendtext_exec, sendtext_synopsis, sendtext_syntax, sendtext_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
