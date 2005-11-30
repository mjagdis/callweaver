/***************************************************************************
            icd_config.h  -  a table of configuration parameters
                             -------------------
    begin                : Sun Jan 11 2004
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
 * The icd_config module holds a set of parameters that can be used to
 * initialize any of the icd structures. Each module will have its own set
 * of key values that it can pull out of an icd_config object. In addition,
 * this module holds an icd_config_registry structure, which allows the
 * dynamic translation of a key setting into any type of void * value.
 *
 * Note that this makes no attempt to be typesafe, but that should be ok.
 * It is hard to imagine multiple threads wanting to alter the same
 * set of configuration parameters. If this assumption turns out to be
 * false, the code will need to be reworked.
 *
 */

#ifndef ICD_CONFIG_H
#define ICD_CONFIG_H
#include "openpbx/icd/icd_types.h"
#include "openpbx/icd/voidhash.h"

#ifdef __cplusplus
extern "C" {
#endif

/***** Init - Destroyer for icd_config *****/

    icd_config *create_icd_config(icd_config_registry * registry, char *name);
    icd_status destroy_icd_config(icd_config ** configp);
    icd_status init_icd_config(icd_config * that, icd_config_registry * registry, char *name);
    icd_status icd_config__clear(icd_config * that);

    void *icd_config__get_value(icd_config * that, char *key);
    icd_status icd_config__set_value(icd_config * that, char *key, char *setting);
    icd_status icd_config__set_raw(icd_config * that, char *key, void *data);
    icd_status icd_config__set_if_new(icd_config * that, char *key, char *setting);
    icd_status icd_config__parse(icd_config * that, char *line, char delim);
    void *icd_config__get_param(icd_config * that, char *name);
    char *icd_config__get_strdup(icd_config * that, char *key, char *default_str);
    icd_status icd_config__strncpy(icd_config * that, char *key, char *target, int maxchars);
    int icd_config__get_int_value(icd_config * that, char *key, int default_int);
    void *icd_config__get_any_value(icd_config * that, char *key, void *default_any);
    icd_config *icd_config__get_subset(icd_config * that, char *begin_key);
    icd_config_registry *icd_config__get_registry(icd_config * that, char *key);

    icd_config_iterator *icd_config__get_key_iterator(icd_config * that);
    int icd_config_iterator__has_more(icd_config_iterator * that);
    char *icd_config_iterator__next(icd_config_iterator * that);
    icd_status destroy_icd_config_iterator(icd_config_iterator ** that);

/***** Init - Destroyer for icd_config_registry *****/

    icd_config_registry *create_icd_config_registry(char *name);
    icd_status destroy_icd_config_registry(icd_config_registry ** regp);
    icd_status init_icd_config_registry(icd_config_registry * that, char *name);
    icd_status icd_config_registry__clear(icd_config_registry * that);

    icd_status icd_config_registry__register(icd_config_registry * that, char *key);
    icd_status icd_config_registry__register_ptr(icd_config_registry * that, char *key, char *keysetting,
        void *value);

    icd_status icd_config_registry__set_validate(icd_config_registry * that, int validate);
    int icd_config_registry__get_validate(icd_config_registry * that);

    icd_config_iterator *icd_config__get_registered_keys_iterator(icd_config_registry * that);

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

