/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#ifndef ICD_DISTRIBUTOR_PRIVATE_H
#define ICD_DISTRIBUTOR_PRIVATE_H
#include <icd_plugable_fn.h>

/*===== Private functions =====*/

icd_status icd_distributor__set_config_params(icd_distributor * that, icd_config * data);
icd_status icd_distributor__create_lists(icd_distributor * that, icd_config * data);
icd_status icd_distributor__correct_list_config(icd_config * data);
icd_status icd_distributor__set_config_params(icd_distributor * that, icd_config * data);
icd_status icd_distributor__create_thread(icd_distributor * that);
void *icd_distributor__run(void *that);

typedef enum {
    ICD_DISTRIBUTOR_STATE_CREATED, ICD_DISTRIBUTOR_STATE_INITIALIZED,
    ICD_DISTRIBUTOR_STATE_CLEARED, ICD_DISTRIBUTOR_STATE_DESTROYED,
    ICD_DISTRIBUTOR_STATE_LAST_STANDARD
} icd_distributor_state;

struct icd_distributor {
    char name[ICD_STRING_LEN];
    icd_member_list *customers;
    icd_member_list *agents;
    icd_plugable_fn *(*get_plugable_fn) (icd_caller * caller);
      icd_status(*link_fn) (icd_distributor *, void *extra);
      icd_status(*dump_fn) (icd_distributor *, int verbosity, int fd, void *extra);
    void *(*run_fn) (void *that);
    void *link_fn_extra;
    void *dump_fn_extra;
    int customer_list_allocated;
    int agent_list_allocated;
    int allocated;
    icd_distributor_state state;
    icd_thread_state thread_state;
    icd_listeners *listeners;
    opbx_mutex_t lock;
    pthread_t thread;
    opbx_cond_t wakeup;
    icd_memory *memory;
    void_hash_table *params;
};

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

