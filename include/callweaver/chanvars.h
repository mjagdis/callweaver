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
 * \brief Channel Variables
 */

#ifndef _CALLWEAVER_CHANVARS_H
#define _CALLWEAVER_CHANVARS_H

#include "callweaver/atomic.h"
#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/callweaver_hash.h"


struct cw_var_t {
	struct cw_object obj;
	unsigned int hash;
	const char *value;
	const char name[0];
};


extern CW_API_PUBLIC struct cw_registry var_registry;


extern CW_API_PUBLIC int cw_var_registry_init(struct cw_registry *reg, int estsize);


static inline __attribute__ ((pure)) unsigned int cw_hash_var_name(const char *name)
{
	return cw_hash_string(name[0] == '_' ? (name[1] == '_' ? &name[2] : &name[1]) : name);
}


extern CW_API_PUBLIC struct cw_var_t *cw_var_new(const char *name, const char *value, int refs);
extern CW_API_PUBLIC int cw_var_assign(struct cw_registry *registry, const char *name, const char *value);


static inline __attribute__ ((pure)) const char *cw_var_name(struct cw_var_t *var)
{
	if (var == NULL)
		return NULL;
	if (var->name == NULL)
		return NULL;
	/* Return the name without the initial underscores */
	return (var->name[0] == '_' ? (var->name[1] == '_' ? &var->name[2] : &var->name[1]) : var->name);
}

static inline __attribute__ ((pure)) const char *cw_var_full_name(struct cw_var_t *var)
{
	return (var ? var->name : NULL);
}


/*!\brief Inherit variables
 * \param dst  Variable registry to copy to
 * \param src  Variable registry to copy from
 *
 * Scans all variables in the source registry, looking for those
 * that should be copied into the destination registry.
 * Variables whose names begin with a single '_' are copied into the
 * destination with the prefix removed.
 * Variables whose names begin with '__' are copied into the destination
 * with their names unchanged.
 *
 * \return 0 if no error
 */
extern CW_API_PUBLIC int cw_var_inherit(struct cw_registry *dst, struct cw_registry *src);

/*!\brief Inherit variables
 * \param src  Variable registry to copy from
 * \param dst  Variable registry to copy to
 *
 * Copies all variables in the source registry into the destination
 * registry.
 *
 * \return 0 if no error
 */
extern CW_API_PUBLIC int cw_var_copy(struct cw_registry *dst, struct cw_registry *src);

#endif /* _CALLWEAVER_CHANVARS_H */
