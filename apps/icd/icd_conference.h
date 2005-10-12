/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#include <linux/zaptel.h>
#ifndef ICD_CONFERENCE_H
#define ICD_CONFERENCE_H

struct icd_conference {
    char name[ICD_STRING_LEN];
    char pin[ICD_STRING_LEN];
    int fd;
    int usecount;
    time_t start;
    icd_caller *owner;
    int is_agent_conf;
    struct zt_confinfo ztc;
    icd_memory *memory;
};

int icd_conference__set_global_usage(int value);
int icd_conference__get_global_usage(void);
icd_status icd_conference__clear(icd_caller * that);
icd_conference *icd_conference__new(char *name);
int icd_conference__usecount(icd_conference * conf);
icd_status icd_conference__associate(icd_caller * that, icd_conference * conf, int owner);
icd_status icd_conference__join(icd_caller * that);
icd_status icd_conference__register(char *name, icd_conference * conf);
icd_status icd_conference__deregister(char *name);
icd_conference *icd_conference__locate(char *name);
vh_keylist *icd_conference__list(void);
void icd_conference__init_registry(void);
void icd_conference__destroy_registry(void);
icd_caller *icd_conference__get_owner(icd_conference * conf);
void icd_conference__lock(icd_conference * conf, char *pin);
char *icd_conference__key(icd_conference * conf);

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

