/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Digium, Inc.
 * Copyright (C) 2005, Olle E. Johansson, Edvina.net
 * Copyright (C) 2005, Russell Bryant <russelb@clemson.edu> 
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
 * \brief MD5 digest related dialplan functions
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"


static void *md5_function;
static const char md5_func_name[] = "MD5";
static const char md5_func_synopsis[] = "Computes an MD5 digest";
static const char md5_func_syntax[] = "MD5(data)";
static const char md5_func_desc[] = "";

static void *checkmd5_function;
static const char checkmd5_func_name[] = "CHECK_MD5";
static const char checkmd5_func_synopsis[] = "Checks an MD5 digest";
static const char checkmd5_func_syntax[] = "CHECK_MD5(digest, data)";
static const char checkmd5_func_desc[] = "Returns 1 on a match, 0 otherwise\n";


static int builtin_function_md5(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	char md5[33];

	if (argc != 1 || !argv[0][0])
		return cw_function_syntax(md5_func_syntax);

	if (buf) {
		cw_md5_hash(md5, argv[0]);
		cw_copy_string(buf, md5, len);
	}

	return 0;
}

static int builtin_function_checkmd5(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	char newmd5[33];

	if (argc != 2 || !argv[0][0] || !argv[1][0])
		return cw_function_syntax(checkmd5_func_syntax);

	if (buf) {
		cw_md5_hash(newmd5, argv[1]);
		cw_copy_string(buf, (strcasecmp(newmd5, argv[0]) ? "0" : "1"), len);
	}

	return 0;
}


static const char tdesc[] = "MD5 functions";

static int unload_module(void)
{
        int res = 0;

	res |= cw_unregister_function(md5_function);
	res |= cw_unregister_function(checkmd5_function);

        return res;
}

static int load_module(void)
{
        md5_function = cw_register_function(md5_func_name, builtin_function_md5, md5_func_synopsis, md5_func_syntax, md5_func_desc);
        checkmd5_function = cw_register_function(checkmd5_func_name, builtin_function_checkmd5, checkmd5_func_synopsis, checkmd5_func_syntax, checkmd5_func_desc);

        return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
