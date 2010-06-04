/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Frank Sautter, levigo holding gmbh, www.levigo.de
 *
 * Frank Sautter - callweaver+at+sautter+dot+com 
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
 * \brief App to set the ISDN Transfer Capability
 * 
 */
#include <stdio.h> 
#include <string.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/options.h"
#include "callweaver/transcap.h"


static void *settransfercapability_app;
static const char settransfercapability_name[] = "SetTransferCapability";
static const char settransfercapability_synopsis[] = "Set ISDN Transfer Capability";
static const char settransfercapability_syntax[] = "SetTransferCapability(transfercapability)";
static const char settransfercapability_descrip[] = 
"Set the ISDN Transfer Capability of a call to a new value.\n"
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


static struct {
	int val;
	const char *name;
} transcaps[] = {
	{ CW_TRANS_CAP_SPEECH,				"SPEECH" },
	{ CW_TRANS_CAP_DIGITAL,			"DIGITAL" },
	{ CW_TRANS_CAP_RESTRICTED_DIGITAL,	"RESTRICTED_DIGITAL" },
	{ CW_TRANS_CAP_3_1K_AUDIO,			"3K1AUDIO" },
	{ CW_TRANS_CAP_DIGITAL_W_TONES,	"DIGITAL_W_TONES" },
	{ CW_TRANS_CAP_VIDEO,				"VIDEO" },
};

static int settransfercapability_exec(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	struct localuser *u;
	int x;
	int transfercapability = -1;

	CW_UNUSED(result);

	if (argc != 1)
		return cw_function_syntax(settransfercapability_syntax);

	LOCAL_USER_ADD(u);

	for (x = 0; x < (sizeof(transcaps) / sizeof(transcaps[0])); x++) {
		if (!strcasecmp(transcaps[x].name, argv[0])) {
			transfercapability = transcaps[x].val;
			break;
		}
	}
	if (transfercapability < 0) {
		cw_log(CW_LOG_WARNING, "'%s' is not a valid transfer capability (see 'show application SetTransferCapability')\n", argv[0]);
		LOCAL_USER_REMOVE(u);
		return 0;
	}
		
	chan->transfercapability = (unsigned short)transfercapability;
	
	if (option_verbose > 2)
		cw_verbose(VERBOSE_PREFIX_3 "Setting transfer capability to: 0x%.2x - %s.\n", transfercapability, argv[0]);

	LOCAL_USER_REMOVE(u);

	return 0;
}


static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(settransfercapability_app);
	return res;
}

static int load_module(void)
{
	settransfercapability_app = cw_register_function(settransfercapability_name, settransfercapability_exec, settransfercapability_synopsis, settransfercapability_syntax, settransfercapability_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, settransfercapability_synopsis)
