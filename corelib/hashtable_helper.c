/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - Navynet SRL
 *
 * Massimo Cetra <devel@navynet.it>
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

/*
 * Hash table engine helper.
 */

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "callweaver.h"

#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/hashtable.h"
#include "callweaver/hashtable_helper.h"


#define hash_log(lev,fmt,...) if ( option_debug > 5 ) opbx_log(lev,fmt, __VA_ARGS__ )

/* ***************************************************************************
   ***************************************************************************
   ************************************************************************ */

int opbx_core_hash_insert ( hash_table_t *hash, char *key, void *data ) {
    int new;

    hash_entry_t *x;

    if ( !strlen(key) ) {
        opbx_log(LOG_WARNING,"Cannot add entry with empty name to hashtable.\n");
        return 0;
    }

    hash_log(LOG_DEBUG,"Hash adding Hash Entry '%s' to table.\n",key);

    x = hash_create_entry(hash, key, &new);

    if (!new) 
	return 0;

    hash_set_value(x, data);

    return 0;
}


int opbx_core_hash_get ( hash_table_t *hash, char *key, void **data ) {
    hash_entry_t *entry;

    hash_log(LOG_DEBUG, "Hash searching Key '%s'\n", key);

    entry = hash_find_entry( hash, key );

    if ( (entry == NULL) )
    {
        hash_log(LOG_DEBUG,"Can't find hash identified by key '%s'\n", key);
	*data = NULL;
        return 0;
    } else {
        hash_log(LOG_DEBUG,"Hashtable: found key '%s'\n", key);
	*data = hash_get_value(entry);
	return 1;
    }

    return 1;
}

int opbx_core_hash_delete (hash_table_t *hash, char *key, int mustfree ) {
    hash_entry_t *entry;

    if ( !strlen(key) ) {
	opbx_log(LOG_WARNING, "Cannot delete hash with empty key from hashtable.\n");
        return 0;
    }

    hash_log(LOG_DEBUG, "Destroying hash with key '%s' from hashtable.\n", key);

    if ((entry = hash_find_entry(hash, (const void *) key)) == NULL)
    {
        opbx_log(LOG_ERROR,"Hash can't find key '%s'\n", key);
        return 0;
    }

    if ( mustfree ) {
        void *data;
	data = hash_get_value(entry);
        if (data)
            free(data);        
    }

    hash_delete_entry(entry);

    return 1;
}

