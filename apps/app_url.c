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
 * App to transmit a URL
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
#include "openpbx/options.h"

static char *tdesc = "Send URL Applications";

static char *app = "SendURL";

static char *synopsis = "Send a URL";

static char *descrip = 
"  SendURL(URL[|option]): Requests client go to URL (IAX2) or sends the \n"
"URL to the client (other channels).\n"
"Result is returned in the SENDURLSTATUS channel variable:\n"
"    SUCCESS       URL successfully sent to client\n"
"    FAILURE       Failed to send URL\n"
"    NOLOAD        Clien failed to load URL (wait enabled)\n"
"    UNSUPPORTED   Channel does not support URL transport\n"
"\n"
"If the option 'wait' is specified, execution will wait for an\n"
"acknowledgement that the URL has been loaded before continuing\n"
"and will return -1 if the peer is unable to load the URL\n"
"\n"
"Old behaviour (deprecated): \n"
" If the client does not support OpenPBX \"html\" transport, \n"
" and there exists a step with priority n + 101, then execution will\n"
" continue at that step.\n"
" Otherwise, execution will continue at the next priority level.\n"
" SendURL only returns 0 if the URL was sent correctly  or if\n"
" the channel does not support HTML transport, and -1 otherwise.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int sendurl_exec(struct opbx_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char tmp[256];
	char *options;
	int local_option_wait=0;
	int local_option_jump = 0;
	struct opbx_frame *f;
	char *stringp=NULL;
	char *status = "FAILURE";

	if (!data || !strlen((char *)data)) {
		opbx_log(LOG_WARNING, "SendURL requires an argument (URL)\n");
		pbx_builtin_setvar_helper(chan, "SENDURLSTATUS", status);
		return -1;
	}
	strncpy(tmp, (char *)data, sizeof(tmp)-1);
	stringp=tmp;
	strsep(&stringp, "|");
	options = strsep(&stringp, "|");
	if (options && !strcasecmp(options, "wait"))
		local_option_wait = 1;
	if (options && !strcasecmp(options, "j"))
		local_option_jump = 1;
	LOCAL_USER_ADD(u);
	if (!opbx_channel_supports_html(chan)) {
		/* Does not support transport */
		if (local_option_jump || option_priority_jumping)
			 opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		pbx_builtin_setvar_helper(chan, "SENDURLSTATUS", "UNSUPPORTED");
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	res = opbx_channel_sendurl(chan, tmp);
	if (res == -1) {
		pbx_builtin_setvar_helper(chan, "SENDURLSTATUS", "FAILURE");
		LOCAL_USER_REMOVE(u);
		return res;
	}
	status = "SUCCESS";
	if (local_option_wait) {
		for(;;) {
			/* Wait for an event */
			res = opbx_waitfor(chan, -1);
			if (res < 0) 
				break;
			f = opbx_read(chan);
			if (!f) {
				res = -1;
				status = "FAILURE";
				break;
			}
			if (f->frametype == OPBX_FRAME_HTML) {
				switch(f->subclass) {
				case OPBX_HTML_LDCOMPLETE:
					res = 0;
					opbx_frfree(f);
					status = "NOLOAD";
					goto out;
					break;
				case OPBX_HTML_NOSUPPORT:
					/* Does not support transport */
					status ="UNSUPPORTED";
					if (local_option_jump || option_priority_jumping)
			 			opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
					res = 0;
					goto out;
					break;
				default:
					opbx_log(LOG_WARNING, "Don't know what to do with HTML subclass %d\n", f->subclass);
				};
			}
			opbx_frfree(f);
		}
	} 
out:	
	pbx_builtin_setvar_helper(chan, "SENDURLSTATUS", status);
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
	return opbx_register_application(app, sendurl_exec, synopsis, descrip);
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


