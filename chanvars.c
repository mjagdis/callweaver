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

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/chanvars.h"
#include "openpbx/logger.h"

struct opbx_var_t *opbx_var_assign(const char *name, const char *value)
{
	int i;
	struct opbx_var_t *var;
	int len;
	
	len = sizeof(struct opbx_var_t);
	
	len += strlen(name) + 1;
	len += strlen(value) + 1;
	
	var = malloc(len);

	if (var == NULL)
	{
		opbx_log(LOG_WARNING, "Out of memory\n");
		return NULL;
	}
	
	memset(var, 0, len);
	i = strlen(name);
	strncpy(var->name, name, i); 
	var->name[i] = '\0';

	var->value = var->name + i + 1;

	i = strlen(value);
	strncpy(var->value, value, i);
	var->value[i] = '\0';
	
	return var;
}	
	
void opbx_var_delete(struct opbx_var_t *var)
{
	if (var == NULL) return;
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
	if ((strlen(var->name) > 0) && (var->name[0] == '_')) {
		if ((strlen(var->name) > 1) && (var->name[1] == '_'))
			name = (char*)&(var->name[2]);
		else
			name = (char*)&(var->name[1]);
	} else
		name = var->name;
	return name;
}

char *opbx_var_full_name(struct opbx_var_t *var)
{
	return (var != NULL ? var->name : NULL);
}

char *opbx_var_value(struct opbx_var_t *var)
{
	return (var != NULL ? var->value : NULL);
}

	
