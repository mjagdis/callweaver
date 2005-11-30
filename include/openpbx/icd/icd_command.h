/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#ifndef ICD_COMMAND_H

#define ICD_COMMAND_H
#include <icd_common.h>

void create_command_hash(void);
void destroy_command_hash(void);
int icd_command_register(char *name, int (*func) (int, int, char **), char *short_help, char *syntax_help,
    char *long_help);
void *icd_command_pointer(char *name);
int icd_command_cli(int fd, int argc, char **argv);

/* all our commands */
int icd_command_help(int fd, int argc, char **argv);
int icd_command_bad(int fd, int argc, char **argv);
int icd_command_verbose(int fd, int argc, char **argv);
int icd_command_debug(int fd, int argc, char **argv);
int icd_command_show(int fd, int argc, char **argv);
int icd_command_dump(int fd, int argc, char **argv);
int icd_command_list(int fd, int argc, char **argv);
int icd_command_load(int fd, int argc, char **argv);

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

