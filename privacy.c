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
 * Privacy Routines
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/channel.h"
#include "openpbx/file.h"
#include "openpbx/app.h"
#include "openpbx/dsp.h"
#include "openpbx/logger.h"
#include "openpbx/options.h"
#include "openpbx/astdb.h"
#include "openpbx/callerid.h"
#include "openpbx/privacy.h"
#include "openpbx/utils.h"
#include "openpbx/lock.h"

int ast_privacy_check(char *dest, char *cid)
{
	char tmp[256] = "";
	char *trimcid = "";
	char *n, *l;
	int res;
	char key[256], result[256];
	if (cid)
		ast_copy_string(tmp, cid, sizeof(tmp));
	ast_callerid_parse(tmp, &n, &l);
	if (l) {
		ast_shrink_phone_number(l);
		trimcid = l;
	}
	snprintf(key, sizeof(key), "%s/%s", dest, trimcid);
	res = ast_db_get("privacy", key, result, sizeof(result));
	if (!res) {
		if (!strcasecmp(result, "allow"))
			return AST_PRIVACY_ALLOW;
		if (!strcasecmp(result, "deny"))
			return AST_PRIVACY_DENY;
		if (!strcasecmp(result, "kill"))
			return AST_PRIVACY_KILL;
		if (!strcasecmp(result, "torture"))
			return AST_PRIVACY_TORTURE;
	}
	return AST_PRIVACY_UNKNOWN;
}

int ast_privacy_reset(char *dest)
{
	if (!dest)
		return -1;
	return ast_db_deltree("privacy", dest);
}

int ast_privacy_set(char *dest, char *cid, int status)
{
	char tmp[256] = "";
	char *trimcid = "";
	char *n, *l;
	int res;
	char key[256];
	if (cid)
		ast_copy_string(tmp, cid, sizeof(tmp));
	ast_callerid_parse(tmp, &n, &l);
	if (l) {
		ast_shrink_phone_number(l);
		trimcid = l;
	}
	if (ast_strlen_zero(trimcid)) {
		/* Don't store anything for empty Caller*ID */
		return 0;
	}
	snprintf(key, sizeof(key), "%s/%s", dest, trimcid);
	if (status == AST_PRIVACY_UNKNOWN) 
		res = ast_db_del("privacy", key);
	else if (status == AST_PRIVACY_ALLOW)
		res = ast_db_put("privacy", key, "allow");
	else if (status == AST_PRIVACY_DENY)
		res = ast_db_put("privacy", key, "deny");
	else if (status == AST_PRIVACY_KILL)
		res = ast_db_put("privacy", key, "kill");
	else if (status == AST_PRIVACY_TORTURE)
		res = ast_db_put("privacy", key, "torture");
	else
		res = -1;
	return res;
}
