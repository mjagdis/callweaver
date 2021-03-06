/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007-2008, Eris Associates Ltd, UK
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


/* If you need to trace the lifecycle of a reference counted object
 * define OBJECT_TRACING below and the initialization of the object
 * to be traced to used cw_object_init[_obj]_traced(...) instead of
 * cw_object_init[_obj](...).
 * Do NOT attempt to use this to trace manager messages as the trace
 * is logged and the creates manager messages which would then be
 * traced so generating recursive, traced log messages ad inifinitum.
 */
#undef OBJECT_TRACING


#ifdef OBJECT_TRACING
#  define OBJECT_TRACING_FIELD			int trace;
#  define OBJECT_TRACING_PARAMS			const char *file, int line, const char *function,
#  define OBJECT_TRACING_ARGS			__FILE__, __LINE__, __PRETTY_FUNCTION__,
#  define OBJECT_TRACING_PRINT(fmt, ...)	if (obj->trace) cw_log_internal(file, line, function, CW_LOG_DEBUG, (fmt), ## __VA_ARGS__)
#  define cw_object_init_obj(obj, module, refs) do { \
		struct cw_object *__obj = (obj); \
		__obj->trace = 0; \
		cw_object_init_obj_internal(OBJECT_TRACING_ARGS __obj, (module), (refs)); \
	} while (0)
#  define cw_object_init_obj_traced(obj, module, refs) do { \
		struct cw_object *__obj = (obj); \
		__obj->trace = 1; \
		cw_object_init_obj_internal(OBJECT_TRACING_ARGS __obj, (module), (refs)); \
	} while (0)
#  define cw_object_get_obj(obj)		cw_object_get_obj_internal(OBJECT_TRACING_ARGS (obj))
#  define cw_object_dup_obj(obj)		cw_object_dup_obj_internal(OBJECT_TRACING_ARGS (obj))
#  define cw_object_put_obj(obj)		cw_object_put_obj_internal(OBJECT_TRACING_ARGS (obj))
#else
#  define OBJECT_TRACING_FIELD
#  define OBJECT_TRACING_PARAMS
#  define OBJECT_TRACING_ARGS
#  define OBJECT_TRACING_PRINT(fmt, ...)
#  define cw_object_init_obj(obj, module, refs) \
	cw_object_init_obj_internal((obj), (module), (refs))
#  define cw_object_init_obj_traced(obj, module, refs) \
	cw_object_init_obj_internal((obj), (module), (refs))
#  define cw_object_get_obj(obj)		cw_object_get_obj_internal((obj))
#  define cw_object_dup_obj(obj)		cw_object_dup_obj_internal((obj))
#  define cw_object_put_obj(obj)		cw_object_put_obj_internal((obj))
#endif


struct cw_object;
struct cw_registry_entry;


struct cw_module;
struct modinfo;

struct cw_object {
	atomic_t refs;
	OBJECT_TRACING_FIELD
	struct cw_module *module;
	void (*release)(struct cw_object *);
};


struct cw_module {
	struct cw_object obj;
	struct modinfo *modinfo;
	void *lib;
	struct cw_registry_entry *reg_entry;
	char name[0];
};


#define CW_OBJECT_CURRENT_MODULE (get_modinfo()->self)


static inline void cw_object_init_obj_internal(OBJECT_TRACING_PARAMS struct cw_object *obj, struct cw_module *module, int refs);
static inline struct cw_object *cw_object_get_obj_internal(OBJECT_TRACING_PARAMS struct cw_object *obj);
static inline struct cw_object *cw_object_dup_obj_internal(OBJECT_TRACING_PARAMS struct cw_object *obj);
static inline int cw_object_put_obj_internal(OBJECT_TRACING_PARAMS struct cw_object *obj);
static inline int cw_object_refs_obj(struct cw_object *obj);


/*! \brief Initialise a reference counted struct noting the module that owns it and setting an initial count of references
 *
 * \param ptr		the ref counted struct to initialise
 * \param module	the module that owns it (NULL if this is owned by the core or is dynmically allocated)
 * \param refs          the count of references already in existence, -1 if there are none.
 *                      N.B. If references already exist the module should be a counted reference to the
 *                      module rather than just a pointer, i.e. instead of get_modinfo()->self you should
 *                      use cw_object_get(get_modinfo()->self)
 *
 * \return a pointer to the given object. Note that this is only a counted reference if its existence
 *         was allowed for in the value of refs passed as the third argument
 */
#define cw_object_init(ptr, mod, refs) ({ \
	typeof(ptr) __ptr = (ptr); \
	if (__ptr) \
		cw_object_init_obj(&__ptr->obj, (mod), (refs)); \
	__ptr; \
})
#ifdef OBJECT_TRACING
#  define cw_object_init_traced(ptr, mod, refs) ({ \
	typeof(ptr) __ptr = (ptr); \
	if (__ptr) \
		cw_object_init_obj_traced(&__ptr->obj, (mod), (refs)); \
	__ptr; \
})
#else
#  define cw_object_init_traced(ptr, mod, refs) cw_object_init((ptr), (mod), (refs))
#endif


/*! \brief Destroy the object data for a reference counted struct
 *
 * \param ptr		the ref counted struct whose object data is to be destroyed
 *
 * \return a pointer to the given object.
 */
#define cw_object_destroy(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	if (__ptr) \
		atomic_destroy(&__ptr->obj.refs); \
	__ptr; \
})


/*! \brief Get a pointer to a struct that is ref counted
 * (i.e. owns (contains) an cw_object struct) using a,
 * possibly uncounted, pointer that may be the first reference
 *
 * Atomically increments the reference counter for the
 * given struct.
 *
 * \param ptr	a pointer to the struct we want a new reference to
 *
 * \return a pointer to the ref counted struct
 */
#define cw_object_get(ptr) ({ \
	typeof(ptr) __ptr = (ptr); \
	if (__ptr) \
		cw_object_get_obj(&__ptr->obj); \
	__ptr; \
})


/*! \brief Get a pointer to a struct that is ref counted
 * (i.e. owns (contains) an cw_object struct) using an
 * existing counted reference
 *
 * Atomically increments the reference counter for the
 * given struct.
 *
 * \param ptr	a pointer to the struct we want a new reference to
 *
 * \return a pointer to the ref counted struct
 */
#define cw_object_dup(ptr) ({ \
	typeof(ptr) __ptr = (ptr); \
	if (__ptr) \
		cw_object_dup_obj(&__ptr->obj); \
	__ptr; \
})


/*! \brief Release a pointer to a ref counted struct
 *
 * Atomically decrements the reference counter for the
 * given struct. If the counter becomes zero the reference
 * made by the struct to the module that owns it (see
 * cw_object_init) is also released.
 *
 * \param ptr	a pointer to the ref counted struct we are releasing a reference to
 *
 * \return 1 if the reference counter became zero, 0 otherwise
 */
#define cw_object_put(ptr) ({ \
	typeof(ptr) __ptr = (ptr); \
	int __r = 0; \
	if (__ptr) \
		__r = cw_object_put_obj(&__ptr->obj); \
	__r; \
})


#define cw_object_refs(ptr) cw_object_refs_obj(&(ptr)->obj)


/*! \brief Initialise an object
 *
 * \param obj		the object to be initialised
 * \param module	the module that owns it
 * \param refs          number of existing references
 *
 * \return nothing
 */
static inline void cw_object_init_obj_internal(OBJECT_TRACING_PARAMS struct cw_object *obj, struct cw_module *module, int refs)
{
	obj->module = module;

	if (refs)
		cw_object_get(module);

	atomic_set(&obj->refs, refs);

	OBJECT_TRACING_PRINT("object 0x%p init refs=%d\n", obj, refs);
}


/*! \brief Get a counted reference to an object using a, possibly uncounted, reference that may be the first use of the object
 *
 * Atomically increments the reference counter for the given object.
 * If there was no previous reference the owning module's reference count is also incremented.
 *
 * \param obj	the object being referenced
 *
 * This should _only_ be called if you are taking what could be the first reference to an object _and_
 * that object's existence is also protected in some other way - maybe there's a lock we have, maybe
 * we're taking the address of something static.
 * Worry about the following:
 *     thread A: call cw_object_get(x) [context switch]
 *     thread B: call cw_object_get(x) [refs = 1]
 *     thread B: call cw_object_put(x) [refs = 0, x released, context switch]
 *     thread A: call cw_object_get(x) [uh oh, is x still valid?]
 *
 * \return the given object with the reference counter incremented
 */
static inline struct cw_object *cw_object_get_obj_internal(OBJECT_TRACING_PARAMS struct cw_object *obj)
{
	int prev;

	prev = atomic_fetch_and_add(&obj->refs, 1);

	OBJECT_TRACING_PRINT("object 0x%p get refs=%d\n", obj, prev + 1);

	if (prev == 0)
		cw_object_get(obj->module);

	return obj;
}


/*! \brief Duplicate a counted reference to a object
 *
 * Atomically increments the reference counter for the given object.
 *
 * \param obj	a counted reference to the object
 *
 * \return a new counted reference to the same object as the given reference
 */
static inline struct cw_object *cw_object_dup_obj_internal(OBJECT_TRACING_PARAMS struct cw_object *obj)
{
	int prev;

	prev = atomic_fetch_and_add(&obj->refs, 1);

	OBJECT_TRACING_PRINT("object 0x%p dup refs=%d\n", obj, prev + 1);

	/* If object tracing is not enabled prev will be set but not used */
	CW_UNUSED(prev);

	return obj;
}


/*! \brief Release a counted reference to a object
 *
 * Atomically decrements the reference counter for the
 * object referenced. If the counter becomes zero the release
 * function (if any) of the object is called and the reference
 * made by the object to the module that registered
 * it is released.
 *
 * \param obj		the counted reference to be released
 *
 * \return 1 if the reference counter became zero, 0 otherwise
 */
static inline int cw_object_put_obj_internal(OBJECT_TRACING_PARAMS struct cw_object *obj)
{
	int prev;

	prev = atomic_fetch_and_sub(&obj->refs, 1);

	if (prev == 0 + 1) {
		struct cw_module *module = obj->module;

		OBJECT_TRACING_PRINT("object 0x%p released\n", obj);

		if (obj->release)
			obj->release(obj);
		cw_object_put(module);
		return 1;
	}

	OBJECT_TRACING_PRINT("object 0x%p put refs=%d\n", obj, prev - 1);
	return 0;
}


static inline int cw_object_refs_obj(struct cw_object *obj)
{
	return atomic_read(&obj->refs);
}


#endif /* _CALLWEAVER_OBJECT_H */
