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
 * \brief Switch registry API 
 */

#ifndef _CALLWEAVER_SWITCH_H
#define _CALLWEAVER_SWITCH_H

#include "callweaver/atomic.h"
#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/module.h"
#include "callweaver/channel.h"


/*! Data structure associated with an callweaver switch */
struct opbx_switch
{
	struct opbx_object obj;
	struct opbx_registry_entry *reg_entry;
	const char *name;				
	const char *description;		
	int (*exists)(struct opbx_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data);
	int (*canmatch)(struct opbx_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data);
	int (*exec)(struct opbx_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data);
	int (*matchmore)(struct opbx_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data);
};


extern struct opbx_registry switch_registry;


#define opbx_switch_register(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	/* We know 0 refs means not initialized because we know how objs work \
	 * internally and we know that registration only happens while the \
	 * module lock is held. \
	 */ \
	if (!opbx_object_refs(__ptr)) \
		opbx_object_init_obj(&__ptr->obj, get_modinfo()->self, -1); \
	__ptr->reg_entry = opbx_registry_add(&switch_registry, &__ptr->obj); \
	0; \
})
#define opbx_switch_unregister(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	if (__ptr->reg_entry) \
		opbx_registry_del(&switch_registry, __ptr->reg_entry); \
	0; \
})


#endif /* _CALLWEAVER_SWITCH_H */
