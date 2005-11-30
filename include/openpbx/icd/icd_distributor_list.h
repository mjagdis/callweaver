/***************************************************************************
          icd_distributor_list.h  -  list of distributor strategies
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
 * ICD_DISTRIBUTOR_LIST
 *
 * The icd_distributor_list module keeps a collection of distributors, and so
 * for simplicity it is an extension of icd_list. In addition, though, it knows
 * how to manipulate distributors. So you could keep code in the distributor
 * list that allows one distributor to steal an agent or customer from another.
 * It also has the ability to assign an agent or customer to a particular
 * distributor, although we will likely use other mechanisms to make this
 * connection most of the time.
 *
 */

#ifndef ICD_LIST_H
#define ICD_LIST_H

#include "openpbx/icd/icd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***** Constructors and Destructors *****/

/* Constructor for a list object. */
    icd_distributor_list *init_icd_distributor_list(int size);

/* Destructor for a list object. */
    icd_status destroy_icd_distributor_list(icd_distributor_list ** listp);

/***** Behaviours *****/

/* Add an element onto the list. */
    icd_status icd_distributor_list__push(icd_distributor_list * that, icd_distributor * dist);

/* Pop the top element off of the list. */
    icd_distributor *icd_distributor_list__pop(icd_distributor_list * that);

/* Print our a copy of the list */
    icd_status icd_distributor_list__dump(icd_distributor_list * that, int fd);

/***** Locking *****/

/* Lock the list */
    icd_status icd_distributor_list__lock(icd_distributor_list * that);

/* Unlock the list */
    icd_status icd_distributor_list__unlock(icd_distributor_list * that);

/**** Listeners ****/

    icd_status icd_distributor_list__add_listener(icd_distributor_list * that, void *listener,
        int (*lstn_fn) (icd_distributor_list * that, void *msg, void *extra), void *extra);

    icd_status icd_distributor_list__remove_listener(icd_distributor_list * that, void *listener);

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

