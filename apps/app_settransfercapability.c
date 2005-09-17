/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Frank Sautter, levigo holding gmbh, www.levigo.de
 *
 * Frank Sautter - openpbx+at+sautter+dot+com 
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
 * App to set the ISDN Transfer Capability
 * 
 */
 
#include <string.h>
#include <stdlib.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/options.h"
#include "openpbx/transcap.h"


static char *app = "SetTransferCapability";

static char *synopsis = "Set ISDN Transfer Capability";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static struct {	int val; char *name; } transcaps[] = {
	{ OPBX_TRANS_CAP_SPEECH,				"SPEECH" },
	{ OPBX_TRANS_CAP_DIGITAL,			"DIGITAL" },
	{ OPBX_TRANS_CAP_RESTRICTED_DIGITAL,	"RESTRICTED_DIGITAL" },
	{ OPBX_TRANS_CAP_3_1K_AUDIO,			"3K1AUDIO" },
	{ OPBX_TRANS_CAP_DIGITAL_W_TONES,	"DIGITAL_W_TONES" },
	{ OPBX_TRANS_CAP_VIDEO,				"VIDEO" },
};

static char *descrip = 
"  SetTransferCapability(transfercapability): Set the ISDN Transfer \n"
"Capability of a call to a new value.\n"
"Always returns 0.  Valid Transfer Capabilities are:\n"
"\n"
"  SPEECH             : 0x00 - Speech (default, voice calls)\n"
"  DIGITAL            : 0x08 - Unrestricted digital information (data calls)\n"
"  RESTRICTED_DIGITAL : 0x09 - Restricted digital information\n"
"  3K1AUDIO           : 0x10 - 3.1kHz Audio (fax calls)\n"
"  DIGITAL_W_TONES    : 0x11 - Unrestricted digital information with tones/announcements\n"
"  VIDEO              : 0x18 - Video:\n"
"\n"
;

static int settransfercapability_exec(struct opbx_channel *chan, void *data)
{
	char tmp[256] = "";
	struct localuser *u;
	int x;
	char *opts;
	int transfercapability = -1;
	
	if (data)
		opbx_copy_string(tmp, (char *)data, sizeof(tmp));
	opts = strchr(tmp, '|');
	if (opts)
		*opts = '\0';
	for (x=0;x<sizeof(transcaps) / sizeof(transcaps[0]);x++) {
		if (!strcasecmp(transcaps[x].name, tmp)) {
			transfercapability = transcaps[x].val;
			break;
		}
	}
	if (transfercapability < 0) {
		opbx_log(LOG_WARNING, "'%s' is not a valid transfer capability (see 'show application SetTransferCapability')\n", tmp);
		return 0;
	} else {
		LOCAL_USER_ADD(u);
		chan->transfercapability = (unsigned short)transfercapability;
		LOCAL_USER_REMOVE(u);
		if (option_verbose > 2)
			opbx_verbose(VERBOSE_PREFIX_3 "Setting transfer capability to: 0x%.2x - %s.\n", transfercapability, tmp);			
		return 0;
	}
}


int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return opbx_unregister_application(app);
}

int load_module(void)
{
	return opbx_register_application(app, settransfercapability_exec, synopsis, descrip);
}

char *description(void)
{
	return synopsis;
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
