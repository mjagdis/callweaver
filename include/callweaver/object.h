/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Eris Associates Ltd, UK
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
 * \brief Object API 
 */

#ifndef _CALLWEAVER_OBJECT_H
#define _CALLWEAVER_OBJECT_H

#include <stddef.h>

#include "callweaver/lock.h"
#include "callweaver/atomic.h"
#include "callweaver/module.h"


struct opbx_object {
	int pad;
	atomic_t refs;
	struct module *module;
	void (*release)(struct opbx_object *);
};


/*! \brief Initialise an object and take a reference to the given module (which may be NULL)
 *
 * \param obj		the object to be initialised
 * \param module	the module that owns it
 *
 * \return nothing
 */
static inline void opbx_object_init_obj(struct opbx_object *obj, struct module *module)
{
	atomic_set(&obj->refs, 1);
	obj->module = opbx_module_get(module);
}


/*! \brief Get a reference to a object
 *
 * Atomically increments the reference counter for the
 * given object.
 *
 * \param obj	the object being referenced
 *
 * \return the given object with the reference counter
 * incremented
 */
static inline struct opbx_object *opbx_object_get_obj(struct opbx_object *obj)
{
	atomic_inc(&obj->refs);
	return obj;
}


/*! \brief Release a reference to a object
 *
 * Atomically decrements the reference counter for the
 * given object. If the counter becomes zero the release
 * function (if any) of the object is called and the reference
 * made by the object to the module that registered
 * it is released.
 *
 * \param obj		the object whose reference is
 * 			being released
 *
 * \return 1 if the reference counter became zero, 0 otherwise
 */
static inline int opbx_object_put_obj(struct opbx_object *obj)
{
	if (atomic_dec_and_test(&obj->refs)) {
		if (obj->release)
			obj->release(obj);
		opbx_module_put(obj->module);
		return 1;
	}
	return 0;
}


static inline int opbx_object_refs_obj(struct opbx_object *obj)
{
	return atomic_read(&obj->refs);
}


/*! \brief Initialise a reference counted struct and take
 * a reference to the given module (which may be NULL)
 *
 * \param ptr		the ref counted struct to initialise
 * \param module	the module that owns it
 *
 * \return a pointer to the ref counted struct
 */
#define opbx_object_init(ptr, mod) ({ \
	const typeof(ptr) __ptr = (ptr); \
	if (__ptr) \
		opbx_object_init_obj(&__ptr->obj,(mod)); \
	__ptr; \
})


/*! \brief Get a pointer to a struct that is ref counted
 * (i.e. owns (contains) an opbx_object struct)
 *
 * Atomically increments the reference counter for the
 * given struct.
 *
 * \param ptr	a pointer to the struct we want a new reference to
 *
 * \return a pointer to the ref counted struct
 */
#define opbx_object_get(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	if (__ptr) \
		opbx_object_get_obj(&__ptr->obj); \
	__ptr; \
})


/*! \brief Release a pointer to a ref counted struct
 *
 * Atomically decrements the reference counter for the
 * given struct. If the counter becomes zero the reference
 * made by the struct to the module that owns it (see
 * opbx_object_init) is also released.
 *
 * \param ptr	a pointer to the ref counted struct we are releasing a reference to
 *
 * \return 1 if the reference counter became zero, 0 otherwise
 */
#define opbx_object_put(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	int r = 0; \
	if (__ptr) \
		r = opbx_object_put_obj(&__ptr->obj); \
	r; \
})


#define opbx_object_refs(ptr) opbx_object_refs_obj(&(ptr)->obj)


#endif /* _CALLWEAVER_OBJECT_H */
