/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Changes Copyright (c) 2004 - 2005 Todd Freeman <freeman@andrews.edu>
 * 
 * 95% based on HasNewVoicemail by:
 * 
 * Copyright (c) 2003 Tilghman Lesher.  All rights reserved.
 * 
 * Tilghman Lesher <asterisk-hasnewvoicemail-app@the-tilghman.com>
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
 * \brief HasVoicemail application
 *
 */
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"
#include "callweaver/utils.h"

static void *vmcount_function;
static const char vmcount_func_name[] = "VMCOUNT";
static const char vmcount_func_synopsis[] = "Counts the voicemail in a specified mailbox";
static const char vmcount_func_syntax[] = "VMCOUNT(vmbox[@context][, folder])";
static const char vmcount_func_desc[] =
	"  context - defaults to \"default\"\n"
	"  folder  - defaults to \"INBOX\"\n";

static const char tdesc[] = "Indicator for whether a voice mailbox has messages in a given folder.";

static int hasvoicemail_internal(const char *context, const char *box, const char *folder)
{
	struct cw_dynstr vmpath = CW_DYNSTR_INIT;
	DIR *vmdir;
	struct dirent *vment;
	int count=0;

	cw_dynstr_printf(&vmpath, "%s/voicemail/%s/%s/%s", cw_config[CW_SPOOL_DIR], context, box, folder);
	if ((vmdir = opendir(vmpath.data))) {
		/* No matter what the format of VM, there will always be a .txt file for each message. */
		while ((vment = readdir(vmdir))) {
			if (!strncmp(vment->d_name + 7, ".txt", 4)) {
				count++;
				break;
			}
		}
		closedir(vmdir);
	}
	cw_dynstr_free(&vmpath);
	return count;
}

static int acf_vmcount_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct localuser *u;
	const char *context;
	char *p;

	if (argc < 1 || argc > 2 || !argv[0][0])
		return cw_function_syntax(vmcount_func_syntax);

	if (!result)
		return 0;

	LOCAL_USER_ADD(u);

	if ((p = strchr(argv[0], '@'))) {
		*p = '\0';
		context = p + 1;
	} else
		context = "default";

	cw_dynstr_printf(result, "%d", hasvoicemail_internal(context, argv[0], (argc > 1 && argv[1][0] ? argv[1] : "INBOX")));

	LOCAL_USER_REMOVE(u);
	return 0;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(vmcount_function);
	return res;
}

static int load_module(void)
{
	vmcount_function = cw_register_function(vmcount_func_name, acf_vmcount_exec, vmcount_func_synopsis, vmcount_func_syntax, vmcount_func_desc);
	return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
