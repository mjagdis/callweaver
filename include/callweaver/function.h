/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Eris Associates Limited, UK
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
 */

/*! \file
 * \brief CallWeaver function handling
 */

#ifndef _CALLWEAVER_FUNCTION_H
#define _CALLWEAVER_FUNCTION_H

#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/module.h"


struct opbx_channel;


/*! \brief structure associated with registering a function */
struct opbx_func {
	struct opbx_object obj;
	struct opbx_registry_entry func_entry;
	unsigned int hash;
	int (*handler)(struct opbx_channel *chan, int argc, char **argv, char *buf, size_t len);
	const char *name;
	const char *synopsis;
	const char *syntax;
	const char *description;
};


extern struct opbx_registry func_registry;


#define opbx_function_register(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	opbx_object_init_obj(&__ptr->obj, get_modinfo()->self); \
	__ptr->func_entry.obj = &__ptr->obj; \
	opbx_registry_add(&func_registry, &__ptr->func_entry); \
})
#define opbx_function_unregister(ptr)	opbx_registry_del(&func_registry, &(ptr)->func_entry)


/* Backwards compatibility */
#define opbx_register_function(f_name, f_handler, f_synopsis, f_syntax, f_description) ({ \
	static struct opbx_func f = { \
		.name = (f_name), \
		.handler = (f_handler), \
		.synopsis = (f_synopsis), \
		.syntax = (f_syntax), \
		.description = (f_description), \
	}; \
	opbx_function_register(&f); \
	&f; \
})
#define opbx_unregister_function(ptr) ({ \
	opbx_function_unregister((struct opbx_func *)(ptr)); \
	0; \
})


extern void opbx_function_registry_initialize(void);


#endif /* _CALLWEAVER_FUNCTION_H */
