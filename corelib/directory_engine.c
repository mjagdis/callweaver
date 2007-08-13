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

/*! \file
 * \brief Directory engine core.
 */

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

//#include "headers_common.h"
#include "callweaver/directory_engine.h"
#include "callweaver/xml_parser.h"
#include "callweaver/mpool.h"
#include "callweaver/hashtable.h"
#include "callweaver/hashtable_helper.h"
#include "callweaver/lock.h"
#include "callweaver/utils.h"


#define safe_free(val) if (val) {free(val);val=NULL;}

/* **************************************************************************
	HASHTABLE FUNCTIONS FOR DIRECTORY ENGINES
   ************************************************************************** */

hash_table_t 	direngine_hash;
opbx_mutex_t	direngine_mutex;

int sofia_direngine_list_init(void) {
    hash_init_table( &direngine_hash, HASH_STRING_KEYS );
    opbx_mutex_init( &direngine_mutex);
    return 1;
}

int sofia_direngine_list_destroy(void) {
    // TODO loop through all engines and shut them off.

    hash_delete_table( &direngine_hash );
    opbx_mutex_destroy( &direngine_mutex);
    return 1;
}

int sofia_direngine_engine_add( sofia_direngine_t *de, char *conf ) {
    int new;
    hash_entry_t *x;
    
    opbx_log(LOG_DEBUG,"Adding directory engine '%s' to hashtable\n", de->name);

    if (!conf) {
	opbx_log(LOG_ERROR,"Adding directory engine '%s' not possible: no conf.\n", de->name);    
        return 0;
    }
    if (!de) {
    	opbx_log(LOG_ERROR,"Adding directory engine '%s' not possible: no engine.\n", de->name);    
        return 0;
    }
    
    x = hash_create_entry(&direngine_hash, de->name, &new);
    
    if (!new) {
    	opbx_log(LOG_ERROR,"Adding directory engine '%s' not possible: not new.\n", de->name);    
	return 0;
    } else {
        if ( de->init ) {
            if ( de->init(conf) ) {
	        hash_set_value(x, de);
	        return 1;
            } 
            else {
    		opbx_log(LOG_ERROR,"Adding directory engine '%s' not possible: initialization failed.\n", de->name);    
                return 0;
	    }
        }
    }

    return 0;
}

int sofia_direngine_engine_release( char *name ) {
    hash_entry_t *entry;
    sofia_direngine_t *de;

    entry = hash_find_entry( &direngine_hash, name );
    
    if ( entry == NULL )
    {
        opbx_log(LOG_ERROR,"Hashtable can't find directory engine '%s' marked for deletion.\n", name);
        return 0;
    }

    opbx_log(LOG_NOTICE,"Hashtable releasing directory engine '%s'\n", name);

    de = hash_get_value(entry);

    if (de && de->release)
        de->release();

    hash_delete_entry(entry);

    return 1;
}

/* ************************************************************************* */

sofia_directory_domain_t *sofia_direngine_domain_search(  char *domain ) {
    int totengines = 0;
    hash_table_t *hash;
    hash_search_t search;
    hash_entry_t *entry;
    char *key;
    sofia_direngine_t *value;
    sofia_directory_domain_t *item = NULL;

//    opbx_mutex_lock(&direngine_mutex);

    hash = &direngine_hash;

    for (entry = hash_first_entry(hash, &search);
         entry;
         entry = hash_next_entry(&search))
    {
        key = (char *) hash_get_key(hash, entry);
        value = (sofia_direngine_t *) hash_get_value(entry);
	if (value) {
	    totengines++;
	    opbx_log(LOG_DEBUG,"Directory searching... (module %s)\n", value->name);
	    item = value->search_domain( domain );
	    if ( item ) {
		opbx_log(LOG_DEBUG," FOUND DOMAIN '%s'\n", item->name );
		goto done;
	    }
	}
    }
done:
//    opbx_mutex_unlock(&direngine_mutex);

    if ( !totengines )
	opbx_log(LOG_ERROR,"Didnt' perform a search as no engines are avalaible.\n");

    return item;
}

/* ************************************************************************* */

sofia_directory_entry_t *sofia_direngine_user_search( char *domain, char *user ) {
    int totengines = 0;
    hash_table_t *hash;
    hash_search_t search;
    hash_entry_t *entry;
    char *key;
    sofia_direngine_t *value;
    sofia_directory_entry_t *item = NULL;

//    opbx_mutex_lock(&direngine_mutex);

    hash = &direngine_hash;

    for (entry = hash_first_entry(hash, &search);
         entry;
         entry = hash_next_entry(&search))
    {
        key = (char *) hash_get_key(hash, entry);
        value = (sofia_direngine_t *) hash_get_value(entry);
	if (value) {
	    totengines++;
	    opbx_log(LOG_DEBUG,"Directory searching... (module %s)\n", value->name);
	    item = value->search_user( user, domain );
	    if ( item ) {
		opbx_log(LOG_DEBUG," FOUND '%s@%s'\n", item->user, item->domain );
		goto done;
	    }
	}
    }
done:
//    opbx_mutex_unlock(&direngine_mutex);

    if ( !totengines )
	opbx_log(LOG_ERROR,"Didnt' perform a search as no engines are avalaible.\n");

    return item;
}

/* ************************************************************************* */

void sofia_direngine_release_domain_result(sofia_directory_domain_t *item) {
    safe_free(item);
}

void sofia_direngine_release_attr_result(sofia_directory_entry_attribute_t *attr) {
    sofia_directory_entry_attribute_t *tmp;

    while (attr) {
        tmp=attr;
        attr=attr->next;
        safe_free(tmp);
    }

}

/* ************************************************************************* */

void sofia_direngine_release_user_result(sofia_directory_entry_t *item) {
    sofia_directory_entry_attribute_t *attr, *tmp_attr;

    attr = item->attributes;

    while ( attr ) {
        tmp_attr = attr;
        attr = attr->next;
        safe_free(tmp_attr);
    }

    safe_free(item);
}

/* ************************************************************************* */

sofia_directory_entry_attribute_t  *sofia_direngine_attribute_search ( char *domain, char *user, char *attrname ) {

    sofia_directory_entry_t *item = NULL;
    sofia_directory_entry_attribute_t *ret = NULL, *tmp = NULL;

    // first, let's find our directory entry
    item = sofia_direngine_user_search( domain, user );

    // if it's not found, return NULL
    if ( !item ) {
        return NULL;
    }

    // Otherwise, let's manage the list
    sofia_directory_entry_attribute_t *attr;

    attr = item->attributes;

    while ( attr ) {
        if ( !strcmp(attr->name, attrname) ) {
//TODO should handle more.
            tmp = malloc( sizeof(sofia_directory_entry_attribute_t) );
            if ( tmp ) {
                tmp->name =strdup( attr->name );
                tmp->value=strdup( attr->value );
                tmp->next = ret;
                ret = tmp;
            }
        }
        attr=attr->next;
    }

    sofia_direngine_release_user_result( item );
    
    return ret;
}

/* ************************************************************************* */

int __sofia_direngine_user_add_attribute( char *domain, char *user, char *name, char *value, int persistant ) {
    int totengines = 0;
    hash_table_t *hash;
    hash_search_t search;
    hash_entry_t *entry;
    char *key;
    sofia_direngine_t *engine;
    sofia_directory_entry_t *item = NULL;

//    opbx_mutex_lock(&direngine_mutex);

    hash = &direngine_hash;

    for (entry = hash_first_entry(hash, &search);
         entry;
         entry = hash_next_entry(&search))
    {
        key = (char *) hash_get_key(hash, entry);
        engine = (sofia_direngine_t *) hash_get_value(entry);
	if (engine) {
	    totengines++;
	    opbx_log(LOG_DEBUG,"Directory searching... (module %s)\n", engine->name);
	    item = engine->search_user( user, domain );
	    if ( item ) {
		opbx_log(LOG_DEBUG," FOUND '%s@%s'\n", item->user, item->domain );
                int tot = engine->attribute_add(domain, user, name, value, persistant);
                sofia_direngine_release_user_result(item);
		return tot;
	    }
	}
    }

//    opbx_mutex_unlock(&direngine_mutex);

    if ( !totengines )
	opbx_log(LOG_ERROR,"Didnt' perform a search as no engines are avalaible.\n");

    return 0;
}

/* ************************************************************************* */

int sofia_direngine_user_del_attribute( char *domain, char *user, char *name, char *value, int partial_compare ) {

    int totengines = 0;
    hash_table_t *hash;
    hash_search_t search;
    hash_entry_t *entry;
    char *key;
    sofia_direngine_t *engine;
    sofia_directory_entry_t *item = NULL;

//    opbx_mutex_lock(&direngine_mutex);

    hash = &direngine_hash;

    for (entry = hash_first_entry(hash, &search);
         entry;
         entry = hash_next_entry(&search))
    {
        key = (char *) hash_get_key(hash, entry);
        engine = (sofia_direngine_t *) hash_get_value(entry);
	if (engine) {
	    totengines++;
	    opbx_log(LOG_DEBUG,"Directory searching... (module %s)\n", engine->name);
	    item = engine->search_user( user, domain );
	    if ( item ) {
		opbx_log(LOG_DEBUG," FOUND '%s@%s'\n", item->user, item->domain );
                int tot = engine->attribute_delete(domain, user, name, value, partial_compare);
                sofia_direngine_release_user_result(item);
		return tot;
	    }
	}
    }

//    opbx_mutex_unlock(&direngine_mutex);

    if ( !totengines )
	opbx_log(LOG_ERROR,"Didnt' perform a search as no engines are avalaible.\n");

    return 0;
}

/* ************************************************************************* */

char *sofia_direngine_attribute_search_from_entry ( sofia_directory_entry_t *entry, char *name ) {
    return 0;
}


