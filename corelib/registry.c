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
 *
 * \brief Registry API
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/options.h"
#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"


static void registry_purge(struct cw_registry *registry)
{
	struct cw_list *list;

	cw_mutex_lock(&registry->lock);
	list = registry->del;
	registry->del = NULL;
	cw_mutex_unlock(&registry->lock);

	while (list) {
		struct cw_registry_entry *entry = container_of(list, struct cw_registry_entry, list);
		list = list->del;
		if (option_verbose > 1)
			cw_verbose(VERBOSE_PREFIX_2 "Registry %s: purged %s\n", registry->name, entry->obj->type->name(entry->obj));
		cw_object_put_obj(entry->obj);
		free(entry);
	}
}

struct cw_registry_entry *cw_registry_add(struct cw_registry *registry, unsigned int hash, struct cw_object *obj)
{
	struct cw_registry_entry *entry = malloc(sizeof(*entry));

	if (entry) {
		cw_list_init(&entry->list);
		entry->obj = cw_object_get_obj(obj);
		entry->hash = hash;

		cw_mutex_lock(&registry->lock);
		cw_list_add(&registry->list[hash % registry->size], &entry->list);
		cw_mutex_unlock(&registry->lock);

		if (option_verbose > 1)
			cw_verbose(VERBOSE_PREFIX_2 "Registry %s: registered %s\n", registry->name, entry->obj->type->name(entry->obj));

		if (registry->onchange)
			registry->onchange();
	} else {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
	}

	return entry;
}

int cw_registry_del(struct cw_registry *registry, struct cw_registry_entry *entry)
{
	atomic_inc(&registry->inuse);

	cw_mutex_lock(&registry->lock);
	cw_list_del(&registry->list, &entry->list);
	cw_mutex_unlock(&registry->lock);

	if (option_verbose > 1 && entry->obj)
		cw_verbose(VERBOSE_PREFIX_2 "Registry %s: unregistered %s\n", registry->name, entry->obj->type->name(entry->obj));

	if (registry->onchange)
		registry->onchange();

	if (atomic_dec_and_test(&registry->inuse))
		registry_purge(registry);

	return 0;
}


int cw_registry_iterate(struct cw_registry *registry, int (*func)(struct cw_object *, void *), void *data)
{
	struct cw_list *list;
	struct cw_registry_entry *entry;
	int i, ret = 0;

	atomic_inc(&registry->inuse);

	for (i = 0; i < registry->size; i++) {
		cw_list_for_each(list, &registry->list[i]) {
			entry = container_of(list, struct cw_registry_entry, list);
			if ((ret = func(entry->obj, data)))
				break;
		}
	}

	if (atomic_dec_and_test(&registry->inuse))
		registry_purge(registry);

	return ret;
}


struct cw_object *cw_registry_find(struct cw_registry *registry, int have_hash, unsigned int hash, const void *pattern)
{
	struct cw_object *obj = NULL;
	struct cw_list *list;
	int i;

	atomic_inc(&registry->inuse);

	i = (have_hash ? hash % registry->size : 0);
	do {
		cw_list_for_each(list, &registry->list[i]) {
			struct cw_registry_entry *entry = container_of(list, struct cw_registry_entry, list);
			if ((!have_hash || entry->hash == hash) && registry->match && registry->match(entry->obj, pattern)) {
				obj = cw_object_dup_obj(entry->obj);
				i = registry->size;
				break;
			}
		}
		i++;
	} while (!have_hash && i < registry->size);

	if (atomic_dec_and_test(&registry->inuse))
		registry_purge(registry);

	return obj;
}


int cw_registry_init(struct cw_registry *registry, size_t estsize)
{
	int i;

	if ((registry->list = malloc(sizeof(*registry->list) * estsize))) {
		registry->size = estsize;
		registry->del = NULL;
		cw_mutex_init(&registry->lock);
		atomic_set(&registry->inuse, 0);
		for (i = 0; i < estsize; i++)
			cw_list_init(&registry->list[i]);
		return 0;
	}

	cw_log(CW_LOG_ERROR, "Out of memory");
	return -1;
}
