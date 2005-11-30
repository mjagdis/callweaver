/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#ifndef ICD_GLOBALS_H

#define ICD_GLOBALS_H
#include "openpbx/icd/icd_common.h"
#include <signal.h>
#include <setjmp.h>

/* turn this flag via the cli in icd_command to enable a wack of icd debugging verbose */
extern int icd_debug;

/* turn this flag via the cli in icd_command to enable  verbosity of the icd show and dump cmds */
extern int icd_verbose;

/* extern char *icd_delimiter; */

extern icd_config_registry *app_icd_config_registry;

/* %TC should this not be in here rather than icd_event.h
extern icd_event_factory *event_factory;
*/

jmp_buf env;

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

