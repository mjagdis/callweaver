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

#ifndef _SOFIA_DIRENGINE_H
#define _SOFIA_DIRENGINE_H

#include <assert.h>
#include <strings.h>

#include "callweaver/hashtable.h"
#include "callweaver/hashtable_helper.h"

/*
    Directory structures. 
    Hopefully those things will end up in the core in the future. 
*/

typedef struct sofia_directory_entry_attribute_s sofia_directory_entry_attribute_t;

struct sofia_directory_entry_attribute_s {
    char				*name;
    char				*value;
    sofia_directory_entry_attribute_t 	*next;    
};

struct sofia_directory_entry_s {
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
    opbx_group_t                        callgroup;
    opbx_group_t                        pickupgroup;
*/

    sofia_directory_entry_attribute_t 	*attributes;    // linked list of attributes
    void 				*next;          // Next entry if there are more than one.
};

typedef struct sofia_directory_entry_s sofia_directory_entry_t;


struct sofia_directory_domain_s {
    char 				*name;
    char 				*context;
    char 				*language;
    char 				*subscribecontext;
    hash_table_t 	                entries;
};

typedef struct sofia_directory_domain_s sofia_directory_domain_t;


/* *************************************************************************
                             Engine structures
   ************************************************************************* */

typedef int (*sofia_directory_init_function_t)(char *);
typedef int (*sofia_directory_release_function_t)(void);
typedef int (*sofia_directory_reload_function_t)(void);

typedef sofia_directory_domain_t *(*sofia_directory_search_domain_function_t)( char *);
typedef sofia_directory_entry_t  *(*sofia_directory_search_user_function_t)( char *, char *);

typedef int (*sofia_directory_user_add_attribute_function_t)( char *, char *, char *, char *, int );
typedef int (*sofia_directory_user_del_attribute_function_t)( char *, char *, char *, char *, int );

struct sofia_direngine_s {
    char *name;
    sofia_directory_init_function_t             init;
    sofia_directory_release_function_t          release;
    sofia_directory_reload_function_t           reload;

    sofia_directory_search_domain_function_t    search_domain;
    sofia_directory_search_user_function_t      search_user;

    sofia_directory_user_add_attribute_function_t attribute_add;
    sofia_directory_user_del_attribute_function_t attribute_delete;
};

typedef struct sofia_direngine_s sofia_direngine_t ;

/* *************************************************************************
                             access functions
   ************************************************************************* */


int sofia_direngine_list_init(void);
int sofia_direngine_list_destroy(void);

int sofia_direngine_engine_add( sofia_direngine_t *de, char *conf );
int sofia_direngine_engine_release( char *name );

sofia_directory_domain_t *sofia_direngine_domain_search    ( char *domain );
void sofia_direngine_release_domain_result(sofia_directory_domain_t *item);

sofia_directory_entry_t  *sofia_direngine_user_search      ( char *domain, char *user );
void sofia_direngine_release_user_result(sofia_directory_entry_t *item);

sofia_directory_entry_attribute_t  *sofia_direngine_attribute_search ( char *domain, char *user, char *attrname );
void sofia_direngine_release_attr_result(sofia_directory_entry_attribute_t *attr);

//-------------------------------

int __sofia_direngine_user_add_attribute( char *domain, char *user, char *name, char *value, int persistant );
#define sofia_direngine_user_add_attribute(d,u,n,v) __sofia_direngine_user_add_attribute(d,u,n,v,0)
#define sofia_direngine_user_add_attribute_persistant(d,u,n,v) __sofia_direngine_user_add_attribute(d,u,n,v,1)

int sofia_direngine_user_del_attribute( char *domain, char *user, char *name, char *value, int partial_compare );

char *sofia_direngine_attribute_search_from_entry ( sofia_directory_entry_t *entry, char *name );

extern sofia_direngine_t directory_embedded_engine;
extern sofia_direngine_t directory_sqlite_engine;

#endif //_SOFIA_DIRENGINE_H
