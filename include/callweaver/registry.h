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
 * \brief Registry API 
 */

#ifndef _CALLWEAVER_REGISTRY_H
#define _CALLWEAVER_REGISTRY_H

#include "callweaver/lock.h"
#include "callweaver/atomic.h"


struct opbx_list {
	struct opbx_list *next, *prev, *del;
};

#define LIST_INIT(name) { &(name), &(name), NULL }

static inline void opbx_list_init(struct opbx_list *list) {
	list->next = list->prev = NULL;
	list->del = NULL;
}

static inline void __opbx_list_add(struct opbx_list *prev, struct opbx_list *entry, struct opbx_list *next)
{
	next->prev = entry;
	entry->next = next;
	entry->prev = prev;
	prev->next = entry;
}

static inline void opbx_list_add(struct opbx_list *head, struct opbx_list *entry)
{
	__opbx_list_add(head, entry, head->next);
}

static inline void __opbx_list_del(struct opbx_list *prev, struct opbx_list *next)
{
	next->prev = prev;
	prev->next = next;
}

static inline void opbx_list_del(struct opbx_list *head, struct opbx_list *entry)
{
	if (!entry->del) {
		__opbx_list_del(entry->prev, entry->next);
		entry->del = head->del;
		head->del = entry;
	}
}

#define opbx_list_for_each(var, head) \
	for (var = (head)->next; var != (head); var = var->next)


struct opbx_object;


struct opbx_registry_entry {
	struct opbx_list list;
	struct opbx_object *obj;
};

struct opbx_registry {
	opbx_mutex_t lock;
	atomic_t inuse;
	struct opbx_list list;
	int (*obj_cmp)(struct opbx_object *a, struct opbx_object *b);
	int (*obj_match)(struct opbx_object *obj, const void *pattern);
	char *name;
	const char *(*obj_name)(struct opbx_object *obj);
	void (*onchange)(void);
};


extern struct opbx_registry_entry *opbx_registry_add(struct opbx_registry *registry, struct opbx_object *obj);
extern int opbx_registry_del(struct opbx_registry *registry, struct opbx_registry_entry *entry);
extern int opbx_registry_iterate(struct opbx_registry *registry, int (*func)(struct opbx_object *, void *), void *data);
extern struct opbx_object *opbx_registry_find(struct opbx_registry *registry, const void *pattern);


#endif /* _CALLWEAVER_REGISTRY_H */
