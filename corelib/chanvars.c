/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Channel Variables
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/chanvars.h"
#include "callweaver/logger.h"
#include "callweaver/strings.h"
#include "callweaver/callweaver_hash.h"


struct cw_var_t *cw_var_assign(const char *name, const char *value)
{
	struct cw_var_t *var;
	int name_len = strlen(name) + 1;
	int value_len = strlen(value) + 1;

	if ((var = calloc(sizeof(struct cw_var_t) + name_len + value_len, sizeof(char)))) {
		var->hash = cw_hash_var_name(name);
		var->value = var->name + name_len;
		memcpy(var->name, name, name_len);
		memcpy(var->value, value, value_len);
		return var;
	} else {
		cw_log(CW_LOG_WARNING, "Out of memory\n");
		return NULL;
	}
}
	
void cw_var_delete(struct cw_var_t *var)
{
	if (var)
		free(var);
}

char *cw_var_name(struct cw_var_t *var)
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

char *cw_var_full_name(struct cw_var_t *var)
{
	return (var ? var->name : NULL);
}

char *cw_var_value(struct cw_var_t *var)
{
	return (var ? var->value : NULL);
}


// END OF FILE

