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
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/callerid.h"
#include "callweaver/phone_no_utils.h"

static const char callerid_func_syntax[] = "CALLERID([datatype, value])";


static int callerid_rw(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	if (argc > 2)
		return cw_function_syntax(callerid_func_syntax);

	if (argc > 1) {
		if (!strcasecmp("all", argv[0])) {
			char name[256];
			char num[256];
			if (!cw_callerid_split(argv[1], name, sizeof(name), num, sizeof(num)))
				cw_set_callerid(chan, num, name, num);	
		} else if (!strcasecmp("name", argv[0])) {
			cw_set_callerid(chan, NULL, argv[1], NULL);
		} else if (!strcasecmp("num", argv[0]) || !strcasecmp("number", argv[0])) {
			cw_set_callerid(chan, argv[1], NULL, NULL);
		} else if (!strcasecmp("ani", argv[0])) {
			cw_set_callerid(chan, NULL, NULL, argv[1]);
		} else if (!strcasecmp("dnid", argv[0])) {
			/* FIXME: do we need to lock chan here? */
			free(chan->cid.cid_dnid);
			chan->cid.cid_dnid = cw_strlen_zero(argv[1]) ? NULL : strdup(argv[1]);
		} else if (!strcasecmp("rdnis", argv[0])) {
			/* FIXME: do we need to lock chan here? */
			free(chan->cid.cid_rdnis);
			chan->cid.cid_rdnis = cw_strlen_zero(argv[1]) ? NULL : strdup(argv[1]);
		} else {
			cw_log(CW_LOG_ERROR, "Unknown callerid data type '%s'\n", argv[0]);
			return -1;
		}
	}

	if (result) {
		if (argc == 0) {
			if (chan->cid.cid_num) {
				if (chan->cid.cid_name)
					cw_dynstr_printf(result, "\"%s\" <%s>", chan->cid.cid_name, chan->cid.cid_num);
				else
					cw_dynstr_printf(result, "%s", chan->cid.cid_num);
			} else if (chan->cid.cid_name)
				cw_dynstr_printf(result, "%s", chan->cid.cid_name);
		} else if (!strcasecmp("all", argv[0])) {
			cw_dynstr_printf(result, "\"%s\" <%s>", chan->cid.cid_name ? chan->cid.cid_name : "", chan->cid.cid_num ? chan->cid.cid_num : "");
		} else {
			char *p = NULL;

			if (!strcasecmp("name", argv[0])) {
				p = chan->cid.cid_name;
			} else if (!strcasecmp("num", argv[0]) || !strcasecmp("number", argv[0])) {
				p = chan->cid.cid_num;
			} else if (!strcasecmp("ani", argv[0])) {
				p = chan->cid.cid_ani;
			} else if (!strcasecmp("dnid", argv[0])) {
				p = chan->cid.cid_dnid;
			} else if (!strcasecmp("rdnis", argv[0])) {
				p = chan->cid.cid_rdnis;
			} else {
				int n;

				if (!strcasecmp("pres", argv[0])) {
					n = chan->cid.cid_pres;
				} else if (!strcasecmp("ani2", argv[0])) {
					n = chan->cid.cid_ani2;
				} else if (!strcasecmp("ton", argv[0])) {
					n = chan->cid.cid_ton;
				} else if (!strcasecmp("tns", argv[0])) {
					n = chan->cid.cid_tns;
				} else {
					cw_log(CW_LOG_ERROR, "Unknown callerid data type '%s'\n", argv[0]);
					return -1;
				}

				cw_dynstr_printf(result, "%d", n);
			}

			if (p)
				cw_dynstr_printf(result, "%s", p);
		}
	}

	return 0;
}


#define MKSTR(x)	#x

#define CALLERID_DEPRECATED(lc, func) \
	static int callerid_ ## lc (struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result) \
	{ \
		static int deprecated = 1; \
		char *av[] = { (char *)MKSTR(lc), (argc ? argv[1] : NULL), NULL }; \
	\
		if (deprecated) { \
			cw_log(CW_LOG_WARNING, func " is deprecated. Use CALLERID(" MKSTR(lc) ") instead.\n"); \
			deprecated = 0; \
		} \
	\
		return callerid_rw(chan, arraysize(av) - 1, av, result); \
	}

CALLERID_DEPRECATED(ani,   "CALLERANI")
CALLERID_DEPRECATED(name,  "CALLERIDNAME")
CALLERID_DEPRECATED(num,   "CALLERIDNUM")
CALLERID_DEPRECATED(dnid,  "DNID")
CALLERID_DEPRECATED(rdnis, "RDNIS")
CALLERID_DEPRECATED(pres,  "CALLINGPRES")
CALLERID_DEPRECATED(ani2,  "CALLINGANI2")
CALLERID_DEPRECATED(ton,   "CALLINGTON")
CALLERID_DEPRECATED(tns,   "CALLINGTNS")


static struct cw_func func_list[] =
{
	{
		.name = "CALLERID",
		.handler = callerid_rw,
		.synopsis = "Gets or sets Caller*ID data on the channel.",
		.syntax = callerid_func_syntax,
		.description =
			"Gets or sets Caller*ID data on the channel.  The allowable datatypes\n"
			"are \"all\", \"name\", \"num\", \"ANI\", \"DNID\", \"RDNIS\".\n"
			"If no datatype is given CALLERID attempts to produce the best name\n"
			"and number it can but without using empty quotes.\n",
	},

	/* DEPRECATED */
	{
		.name = "CALLERIDANI",
		.handler = callerid_ani,
		.synopsis = "Gets or sets ANI on the channel.",
		.syntax = "CALLERIDANI([value])",
		.description =
			"Gets or sets ANI on the channel.\n"
			"This function is deprecated. Use CALLERID(\"ani\"[, value]) instead.\n",
	},
	{
		.name = "CALLERIDNUM",
		.handler = callerid_num,
		.synopsis = "Gets or sets Caller*ID number on the channel.",
		.syntax = "CALLERIDNUM([value])",
		.description =
			"Gets or sets Caller*ID number on the channel.\n"
			"This function is deprecated. Use CALLERID(\"num\"[, value]) instead.\n",
	},
	{
		.name = "CALLERIDNAME",
		.handler = callerid_name,
		.synopsis = "Gets or sets Caller*ID name on the channel.",
		.syntax = "CALLERIDNAME([value])",
		.description =
			"Gets or sets Caller*ID name on the channel.\n"
			"This function is deprecated. Use CALLERID(\"name\"[, value]) instead.\n",
	},
	{
		.name = "DNID",
		.handler = callerid_dnid,
		.synopsis = "Gets or sets DNID on the channel.",
		.syntax = "DNID([value])",
		.description =
			"Gets or sets DNID on the channel.\n"
			"This function is deprecated. Use CALLERID(\"dnid\"[, value]) instead.\n",
	},
	{
		.name = "RDNIS",
		.handler = callerid_rdnis,
		.synopsis = "Gets or sets RDNIS on the channel.",
		.syntax = "RDNIS([value])",
		.description =
			"Gets or sets RDNIS on the channel.\n"
			"This function is deprecated. Use CALLERID(\"rdnis\"[, value]) instead.\n",
	},
	{
		.name = "CALLINGPRES",
		.handler = callerid_pres,
		.synopsis = "Gets or sets Caller*ID presentation/screening on the channel.",
		.syntax = "CALLINGPRES([value])",
		.description =
			"Gets or sets Caller*ID presentation/screening on the channel.\n"
			"This function is deprecated. Use CALLERID(\"pres\"[, value]) instead.\n",
	},
	{
		.name = "CALLINGANI2",
		.handler = callerid_ani2,
		.synopsis = "Gets or sets Caller*ID ANI 2 (info digits) on the channel.",
		.syntax = "CALLINGANI2([value])",
		.description =
			"Gets or sets Caller*ID ANI 2 (info digits) on the channel.\n"
			"This function is deprecated. Use CALLERID(\"ani2\"[, value]) instead.\n",
	},
	{
		.name = "CALLINGTON",
		.handler = callerid_ton,
		.synopsis = "Gets or sets Caller*ID Type-Of-Number on the channel.",
		.syntax = "CALLINGTON([value])",
		.description =
			"Gets or sets Caller*ID Type-Of-Number on the channel.\n"
			"This function is deprecated. Use CALLERID(\"ton\"[, value]) instead.\n",
	},
	{
		.name = "CALLINGTNS",
		.handler = callerid_tns,
		.synopsis = "Gets or sets Caller*ID Transit-Network-Select on the channel.",
		.syntax = "CALLINGTNS([value])",
		.description =
			"Gets or sets Caller*ID Transit-Network-Select on the channel.\n"
			"This function is deprecated. Use CALLERID(\"tns\"[, value]) instead.\n",
	},
};


static const char tdesc[] = "Caller ID related dialplan function";

static int unload_module(void)
{
	int i, res = 0;

	for (i = 0;  i < arraysize(func_list);  i++)
		cw_function_unregister(&func_list[i]);

	return res;
}

static int load_module(void)
{
	int i;

	for (i = 0;  i < arraysize(func_list);  i++)
		cw_function_register(&func_list[i]);

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
