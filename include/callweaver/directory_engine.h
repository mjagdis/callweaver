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
 * \brief Directory engine core
 */

#ifndef _DIRENGINE_H
#define _DIRENGINE_H

#include <assert.h>
#include <strings.h>

#include "callweaver/hashtable.h"
#include "callweaver/hashtable_helper.h"

/*
    Directory structures. 
    Hopefully those things will end up in the core in the future. 
*/

typedef struct directory_entry_attribute_s directory_entry_attribute_t;

struct directory_entry_attribute_s {
    char				*name;
    char				*value;
    directory_entry_attribute_t 	*next;    
};

struct directory_entry_s {
    char 				*user;
    char 				*domain;
    char 				*password;
    char 				*context;
/*
    char 				*accountno;
    char 				*callerid;
    char 				*language;
    char 				*mailbox;
    int  				rtptimeout;
    int  				holdtimeout;
    char 				*subscribecontext;
    cw_group_t                        callgroup;
    cw_group_t                        pickupgroup;
*/

    directory_entry_attribute_t 	*attributes;    // linked list of attributes
    void 				*next;          // Next entry if there are more than one.
};

typedef struct directory_entry_s directory_entry_t;


struct directory_domain_s {
    char 				*name;
    char 				*context;
    char 				*language;
    char 				*subscribecontext;
    hash_table_t 	                entries;
};

typedef struct directory_domain_s directory_domain_t;


/* *************************************************************************
                             Engine structures
   ************************************************************************* */

typedef int (*directory_init_function_t)(char *);
typedef int (*directory_release_function_t)(void);
typedef int (*directory_reload_function_t)(void);

typedef directory_domain_t *(*directory_search_domain_function_t)( char *);
typedef directory_entry_t  *(*directory_search_user_function_t)( char *, char *);

typedef int (*directory_user_add_attribute_function_t)( char *, char *, char *, char *, int );
typedef int (*directory_user_del_attribute_function_t)( char *, char *, char *, char *, int );

struct direngine_s {
    char *name;
    directory_init_function_t             init;
    directory_release_function_t          release;
    directory_reload_function_t           reload;

    directory_search_domain_function_t    search_domain;
    directory_search_user_function_t      search_user;

    directory_user_add_attribute_function_t attribute_add;
    directory_user_del_attribute_function_t attribute_delete;
};

typedef struct direngine_s direngine_t ;

/* *************************************************************************
                             access functions
   ************************************************************************* */


int direngine_list_init(void);
int direngine_list_destroy(void);

int direngine_engine_add( direngine_t *de, char *conf );
int direngine_engine_release( char *name );

directory_domain_t *direngine_domain_search    ( char *domain );
void direngine_release_domain_result(directory_domain_t *item);

directory_entry_t  *direngine_user_search      ( char *domain, char *user );
void direngine_release_user_result(directory_entry_t *item);

directory_entry_attribute_t  *direngine_attribute_search ( char *domain, char *user, char *attrname );
void direngine_release_attr_result(directory_entry_attribute_t *attr);

//-------------------------------

int __direngine_user_add_attribute( char *domain, char *user, char *name, char *value, int persistant );
#define direngine_user_add_attribute(d,u,n,v) __direngine_user_add_attribute(d,u,n,v,0)
#define direngine_user_add_attribute_persistant(d,u,n,v) __direngine_user_add_attribute(d,u,n,v,1)

int direngine_user_del_attribute( char *domain, char *user, char *name, char *value, int partial_compare );

char *direngine_attribute_search_from_entry ( directory_entry_t *entry, char *name );

extern direngine_t directory_embedded_engine;
extern direngine_t directory_sqlite_engine;

#endif //_SOFIA_DIRENGINE_H
