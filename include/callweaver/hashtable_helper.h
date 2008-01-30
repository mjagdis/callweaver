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

#ifndef _CALLWEAVER_HASHTABLE_HELPER_H
#define _CALLWEAVER_HASHTABLE_HELPER_H

#include "callweaver/hashtable.h"


int cw_core_hash_insert ( hash_table_t *hash, char *key, void *data );
int cw_core_hash_get ( hash_table_t *hash, char *key, void **data );
int cw_core_hash_delete (hash_table_t *hash, char *key, int mustfree );



#endif // _CALLWEAVER_HASHTABLE_HELPER_H

