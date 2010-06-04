/*
 * ICD - Intelligent Call Distributor 
 *
 * Copyright (C) 2003, 2004, 2005
 *
 * Written by Anthony Minessale II <anthmct at yahoo dot com>
 * Written by Bruce Atherton <bruce at callenish dot com>
 * Additions, Changes and Support by Tim R. Clark <tclark at shaw dot ca>
 * Changed to adopt to jabber interaction and adjusted for CallWeaver.org by
 * Halo Kwadrat Sp. z o.o., Piotr Figurny and Michal Bielicki
 * 
 * This application is a part of:
 * 
 * CallWeaver -- An open source telephony toolkit.
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#ifndef ICD_COMMAND_H
#define ICD_COMMAND_H

#include "callweaver/dynstr.h"
#include "callweaver/icd/icd_common.h"

void create_command_hash(void);
void destroy_command_hash(void);
int icd_command_register(const char *name, int (*func) (struct cw_dynstr *, int, char **), const char *short_help, const char *syntax_help, const char *long_help);
int icd_command_cli(struct cw_dynstr *ds_p, int argc, char **argv);

/* all our commands */
int icd_command_help(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_bad(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_verbose(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_debug(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_show(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_dump(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_list(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_load(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_transfer(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_ack(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_login(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_logout(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_hang_up(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_hangup_channel(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_playback_channel(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_record(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_join_queue(struct cw_dynstr *ds_p, int argc, char **argv);
int icd_command_control_playback(struct cw_dynstr *ds_p, int argc, char **argv);
void icd_manager_send_message( const char *format, ...);


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

