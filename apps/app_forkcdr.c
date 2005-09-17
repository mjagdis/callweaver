/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Anthony Minessale anthmct@yahoo.com
 * Development of this app Sponsered/Funded  by TAAN Softworks Corp
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
 * Fork CDR application
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/cdr.h"
#include "openpbx/module.h"

static char *tdesc = "Fork The CDR into 2 separate entities.";
static char *app = "ForkCDR";
static char *synopsis = 
"Forks the Call Data Record";
static char *descrip = 
"  ForkCDR([options]):  Causes the Call Data Record to fork an additional\n"
	"cdr record starting from the time of the fork call\n"
"If the option 'v' is passed all cdr variables will be passed along also.\n"
"";


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static void ast_cdr_fork(struct ast_channel *chan) 
{
	struct ast_cdr *cdr;
	struct ast_cdr *newcdr;
	if (!chan || !(cdr = chan->cdr))
		return;
	while (cdr->next)
		cdr = cdr->next;
	if (!(newcdr = ast_cdr_dup(cdr)))
		return;
	ast_cdr_append(cdr, newcdr);
	ast_cdr_reset(newcdr, AST_CDR_FLAG_KEEP_VARS);
	if (!ast_test_flag(cdr, AST_CDR_FLAG_KEEP_VARS))
		ast_cdr_free_vars(cdr, 0);
	ast_set_flag(cdr, AST_CDR_FLAG_CHILD | AST_CDR_FLAG_LOCKED);
}

static int forkcdr_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	LOCAL_USER_ADD(u);
	if (data && !ast_strlen_zero(data))
		ast_set2_flag(chan->cdr, strchr((char *)data, 'v'), AST_CDR_FLAG_KEEP_VARS);
	
	ast_cdr_fork(chan);

	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, forkcdr_exec, synopsis, descrip);
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

char *key()
{
	return OPENPBX_GPL_KEY;
}
