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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

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

static void *hasvoicemail_app;
static const char hasvoicemail_name[] = "HasVoicemail";
static const char hasvoicemail_synopsis[] = "Conditionally branches to priority + 101";
static const char hasvoicemail_syntax[] = "HasVoicemail(vmbox[/folder][@context][, varname])";
static const char hasvoicemail_descrip[] =
"Branches to priority + 101, if there is voicemail in folder indicated."
"Optionally sets <varname> to the number of messages in that folder."
"Assumes folder of INBOX if not specified.\n";

static void *hasnewvoicemail_app;
static const char hasnewvoicemail_name[] = "HasNewVoicemail";
static const char hasnewvoicemail_synopsis[] = "Conditionally branches to priority + 101";
static const char hasnewvoicemail_syntax[] = "HasNewVoicemail(vmbox[/folder][@context][, varname])";
static const char hasnewvoicemail_descrip[] =
"Branches to priority + 101, if there is voicemail in folder 'folder' or INBOX.\n"
"if folder is not specified. Optionally sets <varname> to the number of messages\n" 
"in that folder.\n";


static int hasvoicemail_internal(char *context, char *box, char *folder)
{
	char vmpath[256];
	DIR *vmdir;
	struct dirent *vment;
	int count=0;

	snprintf(vmpath,sizeof(vmpath), "%s/voicemail/%s/%s/%s", (char *)opbx_config_OPBX_SPOOL_DIR, context, box, folder);
	if ((vmdir = opendir(vmpath))) {
		/* No matter what the format of VM, there will always be a .txt file for each message. */
		while ((vment = readdir(vmdir))) {
			if (!strncmp(vment->d_name + 7, ".txt", 4)) {
				count++;
				break;
			}
		}
		closedir(vmdir);
	}
	return count;
}

static int hasvoicemail_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	static int dep_warning = 0;
	struct localuser *u;
	char *vmbox, *context = "default";
	char *vmfolder;
	int vmcount = 0;

	if (!dep_warning) {
		opbx_log(OPBX_LOG_WARNING, "The applications HasVoicemail and HasNewVoicemail have been deprecated.  Please use the VMCOUNT() function instead.\n");
		dep_warning = 1;
	}
	
	if (argc != 2)
		return opbx_function_syntax(hasvoicemail_syntax);

	LOCAL_USER_ADD(u);

	if ((vmbox = strsep(&argv[0], "@")))
		if (!opbx_strlen_zero(argv[0]))
			context = argv[0];
	if (!vmbox)
		vmbox = argv[0];

	vmfolder = strchr(vmbox, '/');
	if (vmfolder) {
		*vmfolder = '\0';
		vmfolder++;
	} else {
		vmfolder = "INBOX";
	}

	vmcount = hasvoicemail_internal(context, vmbox, vmfolder);
	/* Set the count in the channel variable */
	if (argv[1]) {
		char tmp[12];
		snprintf(tmp, sizeof(tmp), "%d", vmcount);
		pbx_builtin_setvar_helper(chan, argv[1], tmp);
	}

	if (vmcount > 0) {
		/* Branch to the next extension */
		if (!opbx_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) 
			opbx_log(OPBX_LOG_WARNING, "VM box %s@%s has new voicemail, but extension %s, priority %d doesn't exist\n", vmbox, context, chan->exten, chan->priority + 101);
	}

	LOCAL_USER_REMOVE(u);
	return 0;
}

static int acf_vmcount_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct localuser *u;
	char *context;

	if (argc < 1 || argc > 2 || !argv[0][0])
		return opbx_function_syntax(vmcount_func_syntax);

	if (!result)
		return 0;

	LOCAL_USER_ADD(u);

	if ((context = strchr(argv[0], '@')))
		*(context++) = '\0';
	else
		context = "default";

	snprintf(result, result_max, "%d", hasvoicemail_internal(context, argv[0], (argc > 1 && argv[1][0] ? argv[1] : "INBOX")));

	LOCAL_USER_REMOVE(u);
	return 0;
}


static int unload_module(void)
{
	int res = 0;

	res |= opbx_unregister_function(vmcount_function);
	res |= opbx_unregister_function(hasvoicemail_app);
	res |= opbx_unregister_function(hasnewvoicemail_app);
	return res;
}

static int load_module(void)
{
	vmcount_function = opbx_register_function(vmcount_func_name, acf_vmcount_exec, vmcount_func_synopsis, vmcount_func_syntax, vmcount_func_desc);
	hasvoicemail_app = opbx_register_function(hasvoicemail_name, hasvoicemail_exec, hasvoicemail_synopsis, hasvoicemail_syntax, hasvoicemail_descrip);
	hasnewvoicemail_app = opbx_register_function(hasnewvoicemail_name, hasvoicemail_exec, hasnewvoicemail_synopsis, hasnewvoicemail_syntax, hasnewvoicemail_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
