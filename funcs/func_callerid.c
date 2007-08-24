/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 * \brief Caller ID related dialplan functions
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/app.h"
#include "callweaver/options.h"
#include "callweaver/callerid.h"
#include "callweaver/phone_no_utils.h"

static void *callerid_function;
static const char callerid_func_name[] = "CALLERID";
static const char callerid_func_synopsis[] = "Gets or sets Caller*ID data on the channel.";
static const char callerid_func_syntax[] = "CALLERID(datatype[, value])";
static const char callerid_func_desc[] =
	"Gets or sets Caller*ID data on the channel.  The allowable datatypes\n"
	"are \"all\", \"name\", \"num\", \"ANI\", \"DNID\", \"RDNIS\".\n";


static int callerid_rw(struct opbx_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	char *p;

	if (argc < 1 || argc > 2)
		return opbx_function_syntax(callerid_func_syntax);

	if (argc > 1) {
		if (!strncasecmp("all", argv[0], 3)) {
			char name[256];
			char num[256];
			if (!opbx_callerid_split(argv[1], name, sizeof(name), num, sizeof(num)))
				opbx_set_callerid(chan, num, name, num);	
        	} else if (!strncasecmp("name", argv[0], 4)) {
                	opbx_set_callerid(chan, NULL, argv[1], NULL);
        	} else if (!strncasecmp("num", argv[0], 3) || !strncasecmp("number", argv[0], 6)) {
                	opbx_set_callerid(chan, argv[1], NULL, NULL);
        	} else if (!strncasecmp("ani", argv[0], 3)) {
                	opbx_set_callerid(chan, NULL, NULL, argv[1]);
        	} else if (!strncasecmp("dnid", argv[0], 4)) {
                	/* do we need to lock chan here? */
                	if (chan->cid.cid_dnid)
                        	free(chan->cid.cid_dnid);
                	chan->cid.cid_dnid = opbx_strlen_zero(argv[1]) ? NULL : strdup(argv[1]);
        	} else if (!strncasecmp("rdnis", argv[0], 5)) {
                	/* do we need to lock chan here? */
                	if (chan->cid.cid_rdnis)
                        	free(chan->cid.cid_rdnis);
                	chan->cid.cid_rdnis = opbx_strlen_zero(argv[1]) ? NULL : strdup(argv[1]);
        	} else {
                	opbx_log(OPBX_LOG_ERROR, "Unknown callerid data type '%s'\n", argv[0]);
			return -1;
        	}
	}

	if (buf) {
		if (!strncasecmp("all", argv[0], 3)) {
			snprintf(buf, len, "\"%s\" <%s>", chan->cid.cid_name ? chan->cid.cid_name : "", chan->cid.cid_num ? chan->cid.cid_num : "");	
		} else {
			if (!strncasecmp("name", argv[0], 4)) {
				p = chan->cid.cid_name;
			} else if (!strncasecmp("num", argv[0], 3) || !strncasecmp("number", argv[0], 6)) {
				p = chan->cid.cid_num;
			} else if (!strncasecmp("ani", argv[0], 3)) {
				p = chan->cid.cid_ani;
			} else if (!strncasecmp("dnid", argv[0], 4)) {
				p = chan->cid.cid_dnid;
			} else if (!strncasecmp("rdnis", argv[0], 5)) {
				p = chan->cid.cid_rdnis;
			} else {
				opbx_log(OPBX_LOG_ERROR, "Unknown callerid data type '%s'\n", argv[0]);
				return -1;
			}

			if (p)
				opbx_copy_string(buf, p, len);
		}
	}

	return 0;
}


static const char tdesc[] = "Caller ID related dialplan function";

static int unload_module(void)
{
        return opbx_unregister_function(callerid_function);
}

static int load_module(void)
{
        callerid_function = opbx_register_function(callerid_func_name, callerid_rw, callerid_func_synopsis, callerid_func_syntax, callerid_func_desc);
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
