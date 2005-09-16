/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Kevin P. Fleming  <kpfleming@digium.com>
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
 * Builtin dialplan functions
 * 
 */

#include <sys/types.h>
#include <stdlib.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision: 1.5 $")

#include "openpbx/module.h"
#include "openpbx/pbx.h"
#include "pbx_functions.h"

static char *tdesc = "Builtin dialplan functions";

int unload_module(void)
{
	int x;

	for (x = 0; x < (sizeof(builtins) / sizeof(builtins[0])); x++) {
		ast_custom_function_unregister(builtins[x]);
	}

	return 0;
}

int load_module(void)
{
	int x;

	for (x = 0; x < (sizeof(builtins) / sizeof(builtins[0])); x++) {
		ast_custom_function_register(builtins[x]);
	}

	return 0;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	return 0;
}

char *key()
{
	return OPENPBX_GPL_KEY;
}
