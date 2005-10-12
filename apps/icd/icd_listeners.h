/***************************************************************************
                    icd_listeners.h  -  collection of listeners
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
 * The icd_listeners module provides a collection of listeners. Each module
 * in ICD provides an interface for adding listeners to each instance. As
 * events occur on the instance, it sends the events through its collection
 * of listeners, which have the opportunity to veto the event if desired.
 */

#ifndef ICD_LISTENERS_H
#define ICD_LISTENERS_H

#include <icd_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/***** Constructors and Destructors *****/

/* Constructor for a listeners object. Parent is the object generating
   events, the one that will hold this collection of listeners. */
    icd_listeners *create_icd_listeners(void);

/* Remove the listener and clear its pointer. */
    icd_status destroy_icd_listeners(icd_listeners ** listenersp);

/* Initialize the icd_listeners structure. */
    icd_status init_icd_listeners(icd_listeners * that);

/* Clear out the icd_listeners structure. Reinitialize before using again. */
    icd_status icd_listeners__clear(icd_listeners * that);

/***** Actions *****/

/* Add a listener to the collection. */
    icd_status icd_listeners__add(icd_listeners * that, void *listener, int (*lstn_fn) (void *listener,
            icd_event * event, void *extra), void *extra);

/* Remove the listener from this collection. */
    icd_status icd_listeners__remove(icd_listeners * that, void *listener);

/* Notify all listeners that an event has occured. */
    int icd_listeners__notify(icd_listeners * that, icd_event * event);

/* Print the contents of the listener collection. */
    icd_status icd_listeners__dump(icd_listeners * that, int fd);

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

