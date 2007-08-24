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


static void registry_purge(struct opbx_registry *registry)
{
	struct opbx_list *list;

	opbx_mutex_lock(&registry->lock);
	list = registry->list.del;
	registry->list.del = NULL;
	opbx_mutex_unlock(&registry->lock);

	while (list) {
		struct opbx_registry_entry *entry = container_of(list, struct opbx_registry_entry, list);
		list = list->del;
		if (opbx_object_put_obj(entry->obj) && option_verbose > 1)
			opbx_verbose(VERBOSE_PREFIX_2 "Registry %s: purged %s\n", registry->name, registry->obj_name(entry->obj));
		free(entry);
	}

	opbx_mutex_unlock(&registry->lock);
}

struct opbx_registry_entry *opbx_registry_add(struct opbx_registry *registry, struct opbx_object *obj)
{
	struct opbx_registry_entry *entry = malloc(sizeof(*entry));

	if (entry) {
		opbx_list_init(&entry->list);
		entry->obj = opbx_object_get_obj(obj);

		opbx_mutex_lock(&registry->lock);
		if (!registry->list.next) {
			registry->list.next = registry->list.prev = &registry->list;
			atomic_set(&registry->inuse, 0);
		}
		opbx_list_add(&registry->list, &entry->list);
		opbx_mutex_unlock(&registry->lock);

		if (option_verbose > 1)
			opbx_verbose(VERBOSE_PREFIX_2 "Registry %s: registered %s\n", registry->name, registry->obj_name(entry->obj));

		if (registry->onchange)
			registry->onchange();
	} else {
		opbx_log(OPBX_LOG_ERROR, "Out of memory!\n");
	}

	return entry;
}

int opbx_registry_del(struct opbx_registry *registry, struct opbx_registry_entry *entry)
{
	atomic_inc(&registry->inuse);

	opbx_mutex_lock(&registry->lock);
	opbx_list_del(&registry->list, &entry->list);
	opbx_mutex_unlock(&registry->lock);

	if (option_verbose > 1 && entry->obj)
		opbx_verbose(VERBOSE_PREFIX_2 "Registry %s: unregistered %s\n", registry->name, registry->obj_name(entry->obj));

	if (registry->onchange)
		registry->onchange();

	if (atomic_dec_and_test(&registry->inuse))
		registry_purge(registry);

	return 0;
}


int opbx_registry_iterate(struct opbx_registry *registry, int (*func)(struct opbx_object *, void *), void *data)
{
	struct opbx_list *list;
	struct opbx_registry_entry *entry;
	int ret = 0;

	atomic_inc(&registry->inuse);

	opbx_list_for_each(list, &registry->list) {
		entry = container_of(list, struct opbx_registry_entry, list);
		if ((ret = func(entry->obj, data)))
			break;
	}

	if (atomic_dec_and_test(&registry->inuse))
		registry_purge(registry);

	return ret;
}


struct opbx_object *opbx_registry_find(struct opbx_registry *registry, const void *pattern)
{
	struct opbx_object *obj = NULL;

	if (registry->obj_match) {
		struct opbx_list *list;

		atomic_inc(&registry->inuse);

		opbx_list_for_each(list, &registry->list) {
			struct opbx_registry_entry *entry = container_of(list, struct opbx_registry_entry, list);
			if (registry->obj_match(entry->obj, pattern)) {
				obj = opbx_object_dup_obj(entry->obj);
				break;
			}
		}

		if (atomic_dec_and_test(&registry->inuse))
			registry_purge(registry);
	}

	return obj;
}
