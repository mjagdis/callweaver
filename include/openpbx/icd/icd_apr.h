/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#ifndef ICD_APR_H
#define ICD_APR_H
#include <apr_strings.h>
#include <apr_pools.h>
#include "openpbx/icd/icd_types.h"

void *icd_apr__malloc(size_t size);
void *icd_apr__calloc(size_t size);
void *icd_apr__submalloc(apr_pool_t * pool, size_t size);
void *icd_apr__subcalloc(apr_pool_t * pool, size_t size);
void *icd_apr__free(void *obj);
void icd_apr__destroy(void);
icd_status icd_apr__init(void);
apr_pool_t *icd_apr__new_subpool(void);
void icd_apr__destroy_subpool(apr_pool_t * pool);
void icd_apr__clear_subpool(apr_pool_t * pool);
char *icd_apr__strdup(char *str);
char *icd_apr__substrdup(apr_pool_t * pool, char *str);
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

