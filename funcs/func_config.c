/* comments and license
 * vim:ts=4:sw=4:smartindent:cindent:autoindent:foldmethod=marker
 *
 * CallWeaver -- an open source telephony toolkit.
 *
 * Copyright (c) 1999 - 2005, digium, inc.
 *
 * Roy Sigurd Karlsbakk <roy@karlsbakk.net>
 *
 * See http://www.callweaver.org for more information about
 * the callweaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and irc
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the gnu general public license version 2. See the license file
 * at the top of the source tree.
 *
 *  */

/* includes and so on  */
/*! \roy
 *
 * \brief functions for reading global configuration data
 * 
 */
#include <stdio.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/utils.h"
/*  */


/* function_config_read()  */
static int function_config_rw(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	int i;

	CW_UNUSED(chan);
	CW_UNUSED(argc);

	if (result) {
		i = cw_config_name2key(argv[0]);
		if (i != CW_CONFIG_UNKNOWN && i != CW_CONFIG_DEPRECATED) {
			cw_dynstr_printf(result, "%s", cw_config[i]);
			return 0;
		}
		cw_log(CW_LOG_ERROR, "Config setting '%s' not known.\n", argv[0]);
		return -1;
	}

	cw_log(CW_LOG_ERROR, "This function currently cannot be used to change the CallWeaver config. Modify callweaver.conf manually and restart.\n");
	return -1;
}
/* function_config_read()  */


static int function_config_systemname(struct cw_channel *chan, int argc, char **argv, cw_dynstr_t *result)
{
	static int deprecated = 1;
	char *av[] = { (char *)"systemname", NULL };

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (deprecated) {
		cw_log(CW_LOG_WARNING, "SYSTEMNAME is deprecated. Use CONFIG(systemname) instead.\n");
		deprecated = 0;
	}

	return function_config_rw(chan, arraysize(av) - 1, av, result);
}


static struct cw_func func_list[] =
{
	{
		.name = "CONFIG",
		.handler = function_config_rw,
		.synopsis = "Read configuration values set in callweaver.conf",
		.syntax = "CONFIG(name)",
		.description = "This function will read configuration values set in callweaver.conf.\n"
			"Possible values include cwctlpermissions, cwctlowner, cwctlgroup,\n"
			"cwctl, cwdb, cwetcdir, cwconfigdir, cwspooldir, cwvarlibdir,\n"
			"cwvardir, cwdbdir, cwlogdir, cwogidir, cwsoundsdir, and cwrundir\n",
	},

	/* DEPRECATED */
	{
		.name = "SYSTEMNAME",
		.handler = function_config_systemname,
		.synopsis = "Return the system name set in callweaver.conf",
		.syntax = "SYSTEMNAME()",
		.description = "Return the system name set in callweaver.conf.\n",
	},
};


/* globals  */
static const char tdesc[] = "CONFIG function";
/* globals  */


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


/* tail

local variables:
mode: c
c-file-style: "linux"
indent-tabs-mode: nil
end:

function_config_read()  */
