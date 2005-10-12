/***************************************************************************
                 icd_fieldset.h  -  a set of extension fields
                             -------------------
    begin                : Tue Jan 27 2004
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
 * The icd_fieldset module holds a set of parameters that can be used for any
 * purpose within an ICD module. Most of the structures in ICD hold a
 * icd_fieldset pointer.
 *
 * Note that this makes no attempt to be typesafe. That may prove to be
 * problem eventually, although it may just work out.
 *
 */

#ifndef ICD_FIELDSET_H
#define ICD_FIELDSET_H
#include <icd_types.h>
#include <voidhash.h>

#ifdef __cplusplus
extern "C" {
#endif

/***** Init - Destroyer for icd_fieldset *****/

    icd_fieldset *create_icd_fieldset(char *name);
    icd_status destroy_icd_fieldset(icd_fieldset ** fieldsetp);
    icd_status init_icd_fieldset(icd_fieldset * that, char *name);
    icd_status icd_fieldset__clear(icd_fieldset * that);

/***** Actions *****/

    void *icd_fieldset__get_value(icd_fieldset * that, char *key);
    icd_status icd_fieldset__set_value(icd_fieldset * that, char *key, void *setting);
    icd_status icd_fieldset__set_if_new(icd_fieldset * that, char *key, void *setting);
    char *icd_fieldset__get_strdup(icd_fieldset * that, char *key, char *default_str);
    icd_status icd_fieldset__strncpy(icd_fieldset * that, char *key, char *target, int maxchars);
    int icd_fieldset__get_int_value(icd_fieldset * that, char *key, int default_int);
    void *icd_fieldset__get_any_value(icd_fieldset * that, char *key, void *default_any);
    icd_fieldset *icd_fieldset__get_subset(icd_fieldset * that, char *begin_key);
    icd_status icd_fieldset__remove_key(icd_fieldset * that, char *key);
    icd_status icd_fieldset__remove_value(icd_fieldset * that, void *value);
    icd_status icd_fieldset__remove_all(icd_fieldset * that);
    icd_status icd_fieldset__parse(icd_fieldset * that, char *line, char delim);

    icd_fieldset_iterator *icd_fieldset__get_key_iterator(icd_fieldset * that);
    int icd_fieldset_iterator__has_more(icd_fieldset_iterator * that);
    char *icd_fieldset_iterator__next(icd_fieldset_iterator * that);
    icd_status destroy_icd_fieldset_iterator(icd_fieldset_iterator ** that);

/***** Shared helper functions *****/

    char *correct_null_str(char *str);

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

