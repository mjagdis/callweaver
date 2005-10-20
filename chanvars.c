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
 * Channel Variables
 * 
 */

#include <stdlib.h>
#include <string.h>

#include "include/openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL$", "$Revision$")

#include "openpbx/chanvars.h"
#include "openpbx/logger.h"
#include "openpbx/strings.h"

struct opbx_var_t *opbx_var_assign(const char *name, const char *value)
{
	int i;
	struct opbx_var_t *var;
	
	var = calloc(sizeof(struct opbx_var_t) + strlen(name) + 1 + strlen(value) + 1, sizeof(char));

	if (var == NULL) {
		opbx_log(LOG_WARNING, "Out of memory\n");
		return NULL;
	}

	i = strlen(name) + 1;
	opbx_copy_string(var->name, name, i);
	var->value = var->name + i;
	opbx_copy_string(var->value, value, strlen(value) + 1);
	
	return var;
}	
	
void opbx_var_delete(struct opbx_var_t *var)
{
	if (var)
		free(var);
}

char *opbx_var_name(struct opbx_var_t *var)
{
	char *name;

	if (var == NULL)
		return NULL;
	if (var->name == NULL)
		return NULL;
	/* Return the name without the initial underscores */
	if (var->name[0] == '_') {
		if (var->name[1] == '_')
			name = (char*)&(var->name[2]);
		else
			name = (char*)&(var->name[1]);
	} else
		name = var->name;
	return name;
}

char *opbx_var_full_name(struct opbx_var_t *var)
{
	return (var ? var->name : NULL);
}

char *opbx_var_value(struct opbx_var_t *var)
{
	return (var ? var->value : NULL);
}


