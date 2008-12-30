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


struct cw_list {
	struct cw_list *next, *prev, *del;
};

#define LIST_INIT(name) { &(name), &(name), NULL }

static inline void cw_list_init(struct cw_list *list) {
	list->next = list->prev = list;
	list->del = NULL;
}

static inline void __cw_list_add(struct cw_list *prev, struct cw_list *entry, struct cw_list *next)
{
	next->prev = entry;
	entry->next = next;
	entry->prev = prev;
	prev->next = entry;
}

static inline void cw_list_add(struct cw_list *head, struct cw_list *entry)
{
	__cw_list_add(head, entry, head->next);
}

static inline void __cw_list_del(struct cw_list *prev, struct cw_list *next)
{
	next->prev = prev;
	prev->next = next;
}

static inline void cw_list_del(struct cw_list *head, struct cw_list *entry)
{
	if (!entry->del) {
		__cw_list_del(entry->prev, entry->next);
		entry->del = head->del;
		head->del = entry;
	}
}

#define cw_list_for_each(var, head) \
	for (var = (head)->next; var != (head); var = var->next)


struct cw_object;


struct cw_registry_entry {
	struct cw_list list;
	struct cw_object *obj;
};

struct cw_registry {
	cw_mutex_t lock;
	atomic_t inuse;
	struct cw_list list;
	char *name;
	void (*onchange)(void);
};


extern struct cw_registry_entry *cw_registry_add(struct cw_registry *registry, struct cw_object *obj);
extern int cw_registry_del(struct cw_registry *registry, struct cw_registry_entry *entry);
extern int cw_registry_iterate(struct cw_registry *registry, int (*func)(struct cw_object *, void *), void *data);
extern struct cw_object *cw_registry_find(struct cw_registry *registry, const void *pattern);
extern void cw_registry_init(struct cw_registry *registry);


#endif /* _CALLWEAVER_REGISTRY_H */
