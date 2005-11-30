/***************************************************************************
                  icd_list_private.h  - generic thread-safe list
                             -------------------
    begin                : Mon Dec 15 2003
    copyright            : (C) 2003 by Bruce Atherton
    email                : bruce at callenish dot com
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

/*
 * ICD_LIST Private Parts
 *
 * If you are not directly inheriting from icd_list, you should pay no
 * attention to the man behind the curtain. Nothing to see here. Return
 * to your homes.
 *
 * Still here? That must mean you need details about the internals of the
 * icd_list structure. Be careful, as you will now have to keep up with
 * any changes to the internal structure. You've been warned.
 *
 */

#ifndef ICD_LIST_PRIVATE_H
#define ICD_LIST_PRIVATE_H

#include "openpbx/lock.h"
#include "openpbx/icd/icd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

    typedef enum {
        ICD_LIST_STATE_CREATED, ICD_LIST_STATE_INITIALIZED,
        ICD_LIST_STATE_CLEARED, ICD_LIST_STATE_DESTROYED,
        ICD_LIST_STATE_LAST_STANDARD
    } icd_list_state;

    extern char *icd_list_state_strings[];

    typedef enum {
        ICD_NODE_STATE_ALLOCATED, ICD_NODE_STATE_FREE, ICD_NODE_STATE_USED,
        ICD_NODE_STATE_LAST_STANDARD
    } icd_node_state;

    typedef enum {
        ICD_LIST_ITERTYPE_PAYLOAD, ICD_LIST_ITERTYPE_NODE, ICD_LIST_ITERTYPE_LAST_STANDARD
    } icd_iterator_type;

    struct icd_list_node {
        icd_list_node *next;
        void *payload;
        icd_node_state state;
        unsigned int flags;
    };

    struct icd_list_iterator {
        icd_list *parent;
        icd_list_node *prev;
        icd_list_node *curr;
        icd_list_node *next;
        icd_iterator_type type;
    };

    struct icd_list {
        char *name;
        icd_list_node *head;
        icd_list_node *tail;
        icd_list_node *cache;
        icd_list_node *first_free;
        icd_list_category category;
        int count;
        int size;
        icd_list_state state;
        unsigned int flags;
        icd_memory *memory;
        int created_as_object;
        int allocated;
        int (*key_fn) (void *key, void *payload);
        icd_list_node *(*ins_fn) (icd_list * that, void *new_elem, void *extra);
        int (*add_fn) (icd_event * that, void *extra);
        int (*del_fn) (icd_event * that, void *extra);
        int (*clr_fn) (icd_event * that, void *extra);
        int (*dstry_fn) (icd_event * that, void *extra);
          icd_status(*dump_fn) (icd_list * that, int verbosity, int fd, void *extra);
        void *ins_fn_extra;
        void *add_fn_extra;
        void *del_fn_extra;
        void *clr_fn_extra;
        void *dstry_fn_extra;
        void *dump_fn_extra;
        icd_listeners *listeners;
        opbx_mutex_t lock;
    };

/* Methods available for use of subtypes */

/* Retrieves the first node whose payload the match_fn returns true for */
    icd_list_node *icd_list__fetch_node(icd_list * that, void *key, int (*match_fn) (void *key, void *payload));

/* Fetches the first payload that a callback function returns true for. */
    void *icd_list__fetch(icd_list * that, void *key, int (*match_fn) (void *key, void *payload));

/* Removes the first node that a callback function returns true for */
    icd_status icd_list__drop_node(icd_list * that, void *key, int (*match_fn) (void *key, void *payload));

/* Gets an unused node out of the list's cache */
    icd_list_node *icd_list__get_node(icd_list * list);

#ifdef __cplusplus
}
#endif
#endif

/* For Emacs:
 * Local Variables:
 * indent-tabs-mode:nil
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 expandtab:
 */

