/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#include <icd_common.h>
#include <icd_apr.h>

#define ICD_APR_PREFIX "  ** ICD APR--->"

static int apr_is_init = 0;
static apr_pool_t *ICD_MEMORY_POOL = NULL;

/* auto-init in the event nobody called icd_apr__init()*/
static void sanity_check(void)
{
    if (apr_is_init == 0) {
        icd_apr__init();
        apr_is_init = 1;
    }
}

apr_pool_t *icd_apr__new_subpool(void)
{
    apr_pool_t *newpool;

    sanity_check();
#ifdef ICD_APR_DEBUG
    ast_verbose(ICD_APR_PREFIX "CREATE SUBPOOL\n");
#endif

    if ((apr_pool_create(&newpool, ICD_MEMORY_POOL)) != APR_SUCCESS) {
        ast_verbose("Could not create memory sub-pool\n");
        return NULL;
    }
    return newpool;

}

void icd_apr__destroy_subpool(apr_pool_t * pool)
{
#ifdef ICD_APR_DEBUG
    ast_verbose(ICD_APR_PREFIX "DESTROY SUBPOOL\n");
#endif

    apr_pool_destroy(pool);
}

void icd_apr__clear_subpool(apr_pool_t * pool)
{
#ifdef ICD_APR_DEBUG
    ast_verbose(ICD_APR_PREFIX "CLEAR SUBPOOL\n");
#endif

    apr_pool_clear(pool);
}

void *icd_apr__submalloc(apr_pool_t * pool, size_t size)
{
#ifdef ICD_APR_DEBUG
    ast_verbose(ICD_APR_PREFIX "ALLOCATE %d BYTES FROM SUBPOOL\n", size);
#endif

    return pool != NULL ? apr_palloc(pool, size) : NULL;
}

void *icd_apr__subcalloc(apr_pool_t * pool, size_t size)
{
#ifdef ICD_APR_DEBUG
    ast_verbose(ICD_APR_PREFIX "ALLOCATE %d BLANK BYTES FROM SUBPOOL\n", size);
#endif

    return pool != NULL ? apr_pcalloc(pool, size) : NULL;
}

void *icd_apr__malloc(size_t size)
{
    sanity_check();
#ifdef ICD_APR_DEBUG
    ast_verbose(ICD_APR_PREFIX "ALLOCATE %d BYTES FROM MAIN POOL\n", size);
#endif
    return apr_palloc(ICD_MEMORY_POOL, size);
}

void *icd_apr__calloc(size_t size)
{
    sanity_check();
#ifdef ICD_APR_DEBUG
    ast_verbose(ICD_APR_PREFIX "ALLOCATE %d BLANK BYTES FROM MAIN POOL\n", size);
#endif
    return apr_pcalloc(ICD_MEMORY_POOL, size);
}

void *icd_apr__free(void *obj)
{
    ast_verbose(ICD_APR_PREFIX "I CANT FREE\n");
    return NULL;
}

char *icd_apr__strdup(char *str)
{
    return apr_pstrdup(ICD_MEMORY_POOL, str);
}

char *icd_apr__substrdup(apr_pool_t * pool, char *str)
{
    return apr_pstrdup(pool, str);
}

void icd_apr__destroy(void)
{

    ast_verbose("DESTROYING THE APR\n");

    if (apr_is_init == 1) {
        apr_pool_destroy(ICD_MEMORY_POOL);
        apr_terminate();
        apr_is_init = 0;
    }
}

icd_status icd_apr__init()
{
    /* initilize */
    ast_verbose("Initializing the APR\n");
    if (apr_initialize() != APR_SUCCESS) {
        ast_verbose("Could not initnialize\n");
        icd_apr__destroy();
        return ICD_ERESOURCE;
    }

    /* Create the pool context */
    if (apr_pool_create(&ICD_MEMORY_POOL, NULL) != APR_SUCCESS) {
        ast_verbose("Could not allocate ICD_MEMORY_POOL\n");
        icd_apr__destroy();
        return ICD_ERESOURCE;
    }

    return ICD_SUCCESS;
}

/* For Emacs:
 * Local Variables:
 * indent-tabs-mode:nil
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 expandtab:
 */

