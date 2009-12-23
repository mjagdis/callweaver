/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Eris Associates Limited, UK
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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
 *
 * Based loosely on app_curl.c code from Asterisk which was:
 *
 * Copyright (C)  2004 - 2006, Tilghman Lesher
 *
 * Tilghman Lesher <curl-20050919@the-tilghman.com>
 * and Brian Wilkins <bwilkins@cfl.rr.com> (Added POST option)
 *
 * app_curl.c is distributed with no restrictions on usage or
 * redistribution.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 *
 * \brief CURL - Fetch a URL
 *
 * \author Mike Jagdis <mjagdis@eris-associates.co.uk>
 *
 * \extref Depends on the CURL library  - http://curl.haxx.se/
 *
 * \ingroup functions
 */

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"
#include "callweaver/curl.h"

static void *curl_function;
static const char curl_func_name[] = "CURL";
static const char curl_func_synopsis[] = "Retrieves the contents of a URL";
static const char curl_func_syntax[] = "CURL(url[, post-data])";
static const char curl_func_desc[] =
	"Fetches the given url, optionally passing post-data as part of the request.\n"
	"If there is post-data the request will be a POST, otherwise a GET\n";


struct curl_rw_args {
	char *buf;
	size_t len;
};


static size_t write_data(void *ptr, size_t size, size_t nmemb, void *data)
{
	struct curl_rw_args *write_args = data;
	int len;

	len = (size * nmemb < write_args->len ? size * nmemb : write_args->len);

	if (write_args->buf) {
		memcpy(write_args->buf, ptr, len);
		write_args->buf[len] = '\0';
		write_args->buf += len;
		write_args->len -= len;
	}
	return len;
}


static int curl_rw(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	struct curl_rw_args write_args = {
		.buf = buf,
		.len = len,
	};
	int ret;

	if (argc < 1 || argc > 2)
		return cw_function_syntax(curl_func_syntax);

	if (chan)
		cw_autoservice_start(chan);

	ret = cw_curl_do(argv[0], argv[1], write_data, &write_args);

	if (chan)
		cw_autoservice_stop(chan);

	return ret;
}


static const char tdesc[] = "HTTP data retrieval function";

static int unload_module(void)
{
        return cw_unregister_function(curl_function);
}

static int load_module(void)
{
        curl_function = cw_register_function(curl_func_name, curl_rw, curl_func_synopsis, curl_func_syntax, curl_func_desc);
	return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)

