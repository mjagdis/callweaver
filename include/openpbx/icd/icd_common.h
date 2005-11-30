/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#ifndef ICD_COMMON_H

#define ICD_COMMON_H

/* Standard Includes */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>

/* Openpbx Includes */
#include "openpbx.h"

#include "openpbx/file.h"
#include "openpbx/say.h"
#include "openpbx/logger.h"
#include "openpbx/utils.h"
#include "openpbx/options.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/musiconhold.h"
#include "openpbx/cli.h"
#include "openpbx/config.h"
#include "openpbx/manager.h"
#include "openpbx/features.h"

/* ICD Includes */
#include "openpbx/icd/icd_fieldset.h"
#include "openpbx/icd/icd_listeners.h"
#include "openpbx/icd/icd_event.h"
#include "openpbx/icd/voidhash.h"
#include "openpbx/icd/icd_config.h"
#include "openpbx/icd/icd_types.h"
#include "openpbx/icd/icd_globals.h"
#include "openpbx/icd/icd_plugable_fn.h"
#include "openpbx/icd/icd_jabber.h"

/* 
   Support for pre/post 1.0 rendition of opbx_set_(read/write)_format.
   Add the CFLAG -DAST_POST_10 in make.conf to get the 3 arg version *default*
   or comment it to get the 2 arg version.
   This is obsolete as of 06/01/2004 do we nuke this macros 
*/

#ifdef AST_POST_10
#define icd_set_read_format(chan,fmt) opbx_set_read_format(chan,fmt,0);
#define icd_set_write_format(chan,fmt) opbx_set_write_format(chan,fmt,0);
#else
#define icd_set_read_format(chan,fmt) opbx_set_read_format(chan,fmt);
#define icd_set_write_format(chan,fmt) opbx_set_write_format(chan,fmt);
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

