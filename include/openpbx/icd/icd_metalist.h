/***************************************************************************
                        icd_metalist.h  -  list of lists
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
 * The icd_metalist module provides a list of lists. Each element stored in
 * this list is itself a list. It allows access to the contained lists by
 * name as well as through the mechanisms of icd_list. It also has its own
 * typesafe iterator that returns icd_list structures.
 *
 * The icd_metalist is castable to an icd_list (since it is an extension
 * of it) so the API of icd_list is usable on icd_metalist. See that
 * section for more of the actions that are possible with icd_metalist
 * via a cast.
 *
 * Because an icd_metalist is itself an icd_list (which is what icd_metalist
 * contains), you could create arbitrarily deep trees by keeping icd_metalists
 * in another icd_metalist.
 *
 * Internally, icd_metalist sets its insertion point function to icd_list's
 * icd_list__insert_ordered() function, passing the icd_list__cmp_name_order()
 * comparison function to it to use to determine the sort order. This shows an
 * example of how you can keep list elements in an arbitrary sort order.
 *
 */

#ifndef ICD_METALIST_H
#define ICD_METALIST_H

#include <icd_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/***** Constructors and Destructors *****/

/* Constructor for a list of lists. Name is optional, use NULL if none. */
    icd_metalist *create_icd_metalist(icd_config * data);

/* Destructor for a list of lists. Only use this for objects created with
 * create_icd_metalist.
 */
    icd_status destroy_icd_metalist(icd_metalist ** listp);

/* Initializer for an already created metalist object. */
    icd_status init_icd_metalist(icd_metalist * that, icd_config * data);

/* Clear the metalist so it needs to be reinitialized to be reused. */
    icd_status icd_metalist__clear(icd_metalist * that);

/***** Behaviours *****/

/* Adds a list to the metalist, returns success or failure. */
    icd_status icd_metalist__add_list(icd_metalist * that, icd_list * new_list);

/* Retrieves a list from the metalist when given a name. */
    icd_list *icd_metalist__fetch_list(icd_metalist * that, char *name);

/* Removes a list from the metalist when given a name, returns success or failure. */
    icd_status icd_metalist__remove_list(icd_metalist * that, char *name);

/* Removes a list from the metalist when given the object itself, returns success or failure. */
    icd_status icd_metalist__remove_list_by_element(icd_metalist * that, icd_list * target);

/* Prints the contents of the metalist to the given file descriptor. */
    icd_status icd_metalist__dump(icd_metalist * that, int fd);

/**** Iterator functions ****/

/* Returns the next element from the list. */
    icd_list *icd_metalist_iterator__next(icd_list_iterator * that);

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

