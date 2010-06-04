/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Created by Olle E. Johansson, Edvina.net 
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
 * \brief URI encoding / decoding
 * 
 * \note For now this code only supports 8 bit characters, not unicode,
         which we ultimately will need to support.
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"
#include "callweaver/module.h"


static void *urldecode_function;
static const char urldecode_func_name[] = "URIDECODE";
static const char urldecode_func_synopsis[] = "Decodes an URI-encoded string.";
static const char urldecode_func_syntax[] = "URIDECODE(data)";
static const char urldecode_func_desc[] = "";

static void *urlencode_function;
static const char urlencode_func_name[] = "URIENCODE";
static const char urlencode_func_synopsis[] = "Encodes a string to URI-safe encoding.";
static const char urlencode_func_syntax[] = "URIENCODE(data)";
static const char urlencode_func_desc[] = "";


/*! \brief builtin_function_uriencode: Encode URL according to RFC 2396 */
static int builtin_function_uriencode(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(chan);

	if (argc != 1 || !argv[0][0])
		return cw_function_syntax(urlencode_func_syntax);

	if (result)
		cw_uri_encode(argv[0], result, 1);

	return 0;
}

/*!\brief builtin_function_uridecode: Decode URI according to RFC 2396 */
static int builtin_function_uridecode(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(chan);

	if (argc != 1 || !argv[0][0])
		return cw_function_syntax(urldecode_func_syntax);

	if (result) {
		int mark = result->used;

		cw_dynstr_printf(result, "%s", argv[0]);
		if (!result->error) {
			cw_uri_decode(&result->data[mark]);
			cw_dynstr_truncate(result, mark + strlen(&result->data[mark]));
		}
	}

	return 0;
}


static const char tdesc[] = "URI encode/decode functions";

static int unload_module(void)
{
	int res = 0;

        res |= cw_unregister_function(urldecode_function);
	res |= cw_unregister_function(urlencode_function);
	return res;
}

static int load_module(void)
{
        urldecode_function = cw_register_function(urldecode_func_name, builtin_function_uridecode, urldecode_func_synopsis, urldecode_func_syntax, urldecode_func_desc);
	urlencode_function = cw_register_function(urlencode_func_name, builtin_function_uriencode, urlencode_func_synopsis, urlencode_func_syntax, urlencode_func_desc);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
