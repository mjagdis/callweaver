/***************************************************************************
              icd_member_list.h  -  list of member objects
                             -------------------
    begin                : Sat Jan 07 2004
    copyright            : (C) 2004 by Bruce Atherton
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
 * The icd_member_list is a typical icd_list with a few extensions that are
 * specific to keeping a list of icd_members.
 *
 * In addition, it keeps track of a few events fired off by individual
 * icd_member elements and amalgamates them into one place.
 *
 */

#ifndef ICD_MEMBER_LIST_H
#define ICD_MEMBER_LIST_H
#include "openpbx/icd/icd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***** Init - Destroyer *****/

/* Create a member list. data is a parsable string of parameters. */
    icd_member_list *create_icd_member_list(char *name, icd_config * data);

/* Destroy a member list, freeing its memory and cleaning up after it. */
    icd_status destroy_icd_member_list(icd_member_list ** listp);

/* Initialize a member list */
    icd_status init_icd_member_list(icd_member_list * that, char *name, icd_config * data);

/* Clear a member list */
    icd_status icd_member_list__clear(icd_member_list * that);

/***** Actions *****/

/* Adds a member to the list, returns success or failure (typesafe wrapper). */
    icd_status icd_member_list__push(icd_member_list * that, icd_member * new_member);

/* Retrieves a member from the list, returns null if there are none (typesafe wrapper). */
    icd_member *icd_member_list__pop(icd_member_list * that);

/* Pushback to the top of the list a formerly popped node. */
    icd_status icd_member_list__pushback(icd_member_list * that, icd_member * new_member);

/* Returns 1 if there are calls in the list, 0 otherwise. */
    int icd_member_list__has_members(icd_member_list * that);

/* Returns the numerical position of the member from the head of the list (0 based) */
    int icd_member_list__member_position(icd_member_list * that, icd_member * target);

/* Returns the member with the given queue, NULL if none. List is unchanged. */
    icd_member *icd_member_list__get_for_queue(icd_member_list * that, icd_queue * queue);

/* Returns the member with the given distributor, NULL if none. List is unchanged. */
    icd_member *icd_member_list__get_for_distributor(icd_member_list * that, icd_distributor * dist);

/* Returns the member with the given caller, NULL if none. List is unchanged. */
    icd_member *icd_member_list__get_for_caller(icd_member_list * that, icd_caller * caller);

/* Prints the contents of the member structures to the given file descriptor. */
    icd_status icd_member_list__dump(icd_member_list * that, int verbosity, int fd);

/* Removes a member from the list when given an id, returns success or failure. */
    icd_status icd_member_list__remove_member(icd_member_list * that, char *id);

/* Removes a member from the list when given the object itself, returns success or failure. */
    icd_status icd_member_list__remove_member_by_element(icd_member_list * that, icd_member * target);

/***** Getters and Setters *****/

/* Set the list name */
    icd_status icd_member_list__set_name(icd_member_list * that, char *name);

/* Get the list name */
    char *icd_member_list__get_name(icd_member_list * that);

/***** Locking *****/

/* Lock the entire member list. */
    icd_status icd_member_list__lock(icd_member_list * that);

/* Unlock the entire member list */
    icd_status icd_member_list__unlock(icd_member_list * that);

/**** Listener functions ****/

    icd_status icd_member_list__add_listener(icd_member_list * that, void *listener, int (*lstn_fn) (void *listener,
            icd_event * event, void *extra), void *extra);

    icd_status icd_member_list__remove_listener(icd_member_list * that, void *listener);

/***** Predefined Behaviours *****/

/* Standard member list dump function */
    icd_status icd_member_list__standard_dump(icd_list * that, int verbosity, int fd, void *extra);

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

