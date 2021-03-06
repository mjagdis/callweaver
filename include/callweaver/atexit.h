/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Eris Associates Ltd.
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
 * \brief Clean up on exit function handling
 */

#ifndef _CALLWEAVER_ATEXIT_H
#define _CALLWEAVER_ATEXIT_H

#include "callweaver/object.h"
#include "callweaver/registry.h"


/*! \brief structure associated with registering an exit handler */
struct cw_atexit {
	struct cw_object obj;
	struct cw_registry_entry *reg_entry;
	void (*function)(void);
	const char *name;
};


extern CW_API_PUBLIC struct cw_registry atexit_registry;


#define cw_atexit_register(ptr) ({ \
	const typeof(ptr) __atptr = (ptr); \
	/* We know 0 refs means not initialized because we know how objs work \
	 * internally and we know that registration only happens while the \
	 * module lock is held. \
	 */ \
	if (!cw_object_refs(__atptr)) { \
		cw_object_init_obj(&__atptr->obj, NULL, 0); \
		/* atexits don't pin the module when registered, but they do pin it \
		 * just before being run or unregistered so the normal puts only \
		 * release the module once we're done. \
		 */ \
		__atptr->obj.module = get_modinfo()->self; \
	} \
	__atptr->reg_entry = cw_registry_add(&atexit_registry, 0, &__atptr->obj); \
	0; \
})
#define cw_atexit_unregister(ptr) ({ \
	const typeof(ptr) __atptr = (ptr); \
	if (__atptr->reg_entry) { \
		cw_object_get(__atptr->obj.module); \
		cw_registry_del(&atexit_registry, __atptr->reg_entry); \
	} \
	0; \
})


#endif /* _CALLWEAVER_ATEXIT_H */
