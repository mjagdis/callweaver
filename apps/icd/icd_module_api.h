/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#ifndef ICD_MODULE_API_H
#define ICD_MODULE_API_H

#include <assert.h>
#include <icd_globals.h>
#include <icd_common.h>
#include <icd_event.h>
#include <icd_list.h>
#include <icd_bridge.h>
#include <icd_queue.h>
#include <icd_distributor.h>
#include <icd_distributor_private.h>
#include <icd_member.h>
#include <icd_member_list.h>
#include <icd_caller.h>
#include <icd_caller_list.h>
#include <icd_agent.h>
#include <icd_customer.h>
#include <icd_command.h>
#include <icd_plugable_fn.h>
#include <icd_plugable_fn_list.h>
#include <icd_fieldset.h>

/* Only Used in the external c file than contains the custom code eg icd_module_mystuff.c */
int icd_module_unload(void);
int icd_module_load(icd_config_registry * registry);

struct icd_loadable_object {
    char filename[ICD_STRING_LEN];
    int (*load_fn) (icd_config_registry * registry);
    int (*unload_fn) (void);
    int (*dist_run) (icd_distributor * that, char *name, icd_config * data);
    /* need to add run for callers */
    void *lib;
    icd_memory *memory;
    int allocated;
};

/* Only used by api_icd.c->load_module(void)  */
icd_status icd_module_load_dynamic_module(icd_config_registry * registry);

/* Only used by api_icd.c->unload_module(void)  */
icd_status icd_module_unload_dynamic_modules(void);

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

