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

#include "callweaver/options.h"
#include "callweaver/chanvars.h"
#include "callweaver/logger.h"
#include "callweaver/strings.h"
#include "callweaver/callweaver_hash.h"
#include "callweaver/utils.h"


static const char *var_object_name(struct cw_object *obj)
{
	struct cw_var_t *it = container_of(obj, struct cw_var_t, obj);
	return it->name;
}

static int cw_var_qsort_compare_by_name(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct cw_var_t *var_a = container_of(*objp_a, struct cw_var_t, obj);
	const struct cw_var_t *var_b = container_of(*objp_b, struct cw_var_t, obj);

	return strcmp(
		(var_a->name[0] == '_' ? (var_a->name[1] == '_' ? &var_a->name[2] : &var_a->name[1]) : var_a->name),
		(var_b->name[0] == '_' ? (var_b->name[1] == '_' ? &var_b->name[2] : &var_b->name[1]) : var_b->name)
	);
}

static int var_object_match(struct cw_object *obj, const void *pattern)
{
	struct cw_var_t *it = container_of(obj, struct cw_var_t, obj);
	const char *name = pattern;

	return !strcmp(
		(it->name[0] == '_' ? (it->name[1] == '_' ? &it->name[2] : &it->name[1]) : it->name),
		(name[0] == '_' ? (name[1] == '_' ? &name[2] : &name[1]) : name)
	);
}

const struct cw_object_isa cw_object_isa_var = {
	.name = var_object_name,
};

struct cw_registry var_registry = {
	.name = "Global variables",
	.qsort_compare = cw_var_qsort_compare_by_name,
	.match = var_object_match,
};


int cw_var_registry_init(struct cw_registry *reg, int estsize)
{
	memset(reg, 0, sizeof(*reg));
	reg->name = "Channel variables";
	reg->qsort_compare = cw_var_qsort_compare_by_name;
	reg->match = var_object_match;
	return cw_registry_init(reg, estsize);
}


static void var_release(struct cw_object *obj)
{
	struct cw_var_t *it = container_of(obj, struct cw_var_t, obj);
	free(it);
}


struct cw_var_t *cw_var_new(const char *name, const char *value, int refs)
{
	struct cw_var_t *var;
	int name_len = strlen(name) + 1;
	int value_len = strlen(value) + 1;

	if ((var = malloc(sizeof(struct cw_var_t) + name_len + value_len))) {
		cw_object_init(var, &cw_object_isa_var, NULL, refs);
		var->obj.release = var_release;
		var->value = var->name + name_len;
		memcpy((char *)var->name, name, name_len);
		var->hash = cw_hash_var_name(name);
		memcpy((char *)var->value, value, value_len);
		return var;
	} else {
		cw_log(CW_LOG_WARNING, "Out of memory\n");
		return NULL;
	}
}


int cw_var_assign(struct cw_registry *registry, const char *name, const char *value)
{
	struct cw_var_t *var;

	/* Strictly we should create the var with one reference and
	 * put it after the add. But we know what's happening and how
	 * objects work so we can optimize an atomic op away.
	 */
	if ((var = cw_var_new(name, value, 0))) {
		if (cw_registry_add(registry, var->hash, &var->obj))
			return 0;
		var->obj.release(&var->obj);
	}

	return -1;
}


static int cw_var_inherit_one(struct cw_object *obj, void *data)
{
	struct cw_var_t *var = container_of(obj, struct cw_var_t, obj);
	struct cw_registry *reg = data;
	int err = 0;

	if (var->name[0] == '_') {
		if (var->name[1] == '_') {
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "Copying hard-transferable variable %s.\n", var->name);
			err = !cw_registry_add(reg, var->hash, &var->obj);
		} else {
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "Copying soft-transferable variable %s.\n", &var->name[1]);
			err = cw_var_assign(reg, &var->name[1], var->value);
		}
	} else if (option_debug)
		cw_log(CW_LOG_DEBUG, "Not copying variable %s.\n", cw_var_name(var));

	return err;
}

int cw_var_inherit(struct cw_registry *src, struct cw_registry *dst)
{
	return cw_registry_iterate_rev(src, cw_var_inherit_one, dst);
}


static int cw_var_copy_one(struct cw_object *obj, void *data)
{
	struct cw_var_t *var = container_of(obj, struct cw_var_t, obj);
	struct cw_registry *reg = data;

	return !cw_registry_add(reg, var->hash, &var->obj);
}

int cw_var_copy(struct cw_registry *src, struct cw_registry *dst)
{
	return cw_registry_iterate_rev(src, cw_var_copy_one, dst);
}
