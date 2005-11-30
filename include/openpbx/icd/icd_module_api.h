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
#include "openpbx/icd/icd_globals.h"
#include "openpbx/icd/icd_common.h"
#include "openpbx/icd/icd_event.h"
#include "openpbx/icd/icd_list.h"
#include "openpbx/icd/icd_bridge.h"
#include "openpbx/icd/icd_queue.h"
#include "openpbx/icd/icd_distributor.h"
#include "openpbx/icd/icd_distributor_private.h"
#include "openpbx/icd/icd_member.h"
#include "openpbx/icd/icd_member_list.h"
#include "openpbx/icd/icd_caller.h"
#include "openpbx/icd/icd_caller_list.h"
#include "openpbx/icd/icd_agent.h"
#include "openpbx/icd/icd_customer.h"
#include "openpbx/icd/icd_command.h"
#include "openpbx/icd/icd_plugable_fn.h>"
#include "openpbx/icd/icd_plugable_fn_list.h"
#include "openpbx/icd/icd_fieldset.h"

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

