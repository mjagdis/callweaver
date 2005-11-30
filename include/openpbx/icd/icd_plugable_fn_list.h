/***************************************************************************
                        icd_plugable_fn_list.h  -  list of lists
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
 * The icd_plugable_fn_list module provides a list of function ptrs. 
 * Each element stored in this list is a function ptrs. It allows access to the contained function ptrs
 * by name as well as through the mechanisms of icd_list. It also has its own
 * typesafe iterator that returns icd_plugable_fn structures.
 *
 *
 */

#ifndef ICD_PLUGABLE_FN_LIST_H
#define ICD_PLUGABLE_FN_LIST_H

#include "openpbx/icd/icd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***** Constructors and Destructors *****/

/* Constructor for a list of plugable_fns. Name is optional, use NULL if none. */
    icd_plugable_fn_list *create_icd_plugable_fn_list(char *name, icd_config * data);

/* Destructor for a list of  plugable_fns. Only use this for objects created with
 * create_icd_plugable_fn_list.
 */
    icd_status destroy_icd_plugable_fn_list(icd_plugable_fn_list ** listp);

/* Initializer for an already created  plugable_fns object. */
    icd_status init_icd_plugable_fn_list(icd_plugable_fn_list * that, char *name, icd_config * data);

/* Clear the list so it needs to be reinitialized to be reused. */
    icd_status icd_plugable_fn_list__clear(icd_plugable_fn_list * that);

/***** Behaviours *****/

/* Adds a  plugable_fns to the list, returns success or failure. */
    icd_status icd_plugable_fn_list__add_fns(icd_plugable_fn_list * that, icd_plugable_fn * new_fns);

/* Retrieves a  plugable_fns from the list when given a name, usually dist name */
    icd_plugable_fn *icd_plugable_fn_list__fetch_fns(icd_plugable_fn_list * that, char *id);

/* Removes a  plugable_fns from the list when given a name, usually dist name returns success or failure. */
    icd_status icd_plugable_fn_list__remove_fns(icd_plugable_fn_list * that, char *id);

/* Removes a plugable from the list when given the object itself, returns success or failure. */
    icd_status icd_plugable_fn_list__remove_fns_by_element(icd_plugable_fn_list * that, icd_list * target);

    icd_status icd_plugable_fn_remove_all_plugable_fns(icd_plugable_fn_list * that);

/* Prints the contents of the metalist to the given file descriptor. */
    icd_status icd_plugable_fn_list__dump(icd_plugable_fn_list * that, int fd);

/* Getter and Setters */
    int icd_plugable_fn_list_count(icd_plugable_fn_list * that);

/**** Iterator functions ****/

/* Returns the next element from the list. */
    icd_plugable_fn *icd_plugable_fn_list_iterator__next(icd_list_iterator * that);

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

