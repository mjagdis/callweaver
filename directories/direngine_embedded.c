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
 * \brief Embedded directoey engine. 
 */

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>


#include "callweaver/directory_engine.h"
#include "callweaver/xml_parser.h"
#include "callweaver/options.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/mpool.h"
#include "callweaver/hashtable.h"
#include "callweaver/hashtable_helper.h"
#include "callweaver/lock.h"
#include "callweaver/utils.h"

/* **************************************************************************
	EMBEDDED DIRECTORY ENGINE
   ************************************************************************** */

#define EDIR_DB_FAMILY  "EDIR"

#define edir_log(lev,fmt,...) if ( option_debug > 4 ) opbx_log(lev,fmt, __VA_ARGS__ )

#define safe_free(val) if (val) {free(val);val=NULL;}

static char             *edir_config_file;      /* Our config file we are parsing to retrieve our values */
static opbx_mutex_t	edir_mutex;             /* To prevent multiple accesses */
static hash_table_t 	edir_domain_hash;       /* Our domain list */
static opbx_mpool_t     *edir_pool;             /* Our memory pool */

directory_entry_t *root_entry;

static int edir_init(char *config_file) {
    // Create it's pool.
    // Initialize hashtable
    // Initialize mutex
    // save config file for future use (reload)

    // parse config file.

    int 	enabled 	= 1,
		pool_flags	= 0,
		pool_ret;

    cw_xml_t	cfg 		= NULL, 
	        xml 		= NULL, 
	        directory	= NULL,
	        settings	= NULL,
		domain		= NULL,
		param		= NULL,
		users		= NULL,
		user		= NULL,
		values 		= NULL,
		attribute	= NULL;

    opbx_log(LOG_DEBUG,"Initializing Embedded directory engine engine\n");

    opbx_mutex_init(&edir_mutex);
    opbx_mutex_init(&edir_mutex);
    hash_init_table( &edir_domain_hash, HASH_STRING_KEYS );

    edir_pool = opbx_mpool_open(pool_flags, 0, NULL, &pool_ret);
    
    if ( !edir_pool ) {
	opbx_log(LOG_DEBUG,"Cannot initialize Embedded directory engine. Memory pool error\n");
	return 0;
    }

    if ( !( cfg = cwxml_parse_file( config_file ) ) ) {
    	opbx_log(LOG_ERROR, "Open of '%s' failed. Leaving default values.\n", config_file);
	goto error;
    }

    // Saving config file for future use.
    edir_config_file = opbx_mpool_strdup(edir_pool, config_file);

    xml=cfg;

    if ( (directory = cwxml_child(xml, "directory")) ) {
	if ((values = cwxml_child(directory, "embedded"))) {

	    for (param = cwxml_child(values, "setting"); param; param = param->next) {
		char *var = (char *) cwxml_attr_soft(param, "name");
		char *val = (char *) cwxml_attr_soft(param, "value");

		opbx_log(LOG_DEBUG,"setting: %s=%s\n", var, val);
/*
TODO
	        if (!strcasecmp(var, "disabled")) {
		    enabled = ( atoi(val)==1) ? 0 : 1 ;
		    if ( !enabled )
			goto error;
		}
*/
	    }

	    for (domain = cwxml_child(values, "domain"); domain; domain = domain->next) {

		char *domain_name = (char *) cwxml_attr_soft(domain, "name");

                if ( !strlen(domain_name) ) {
		    opbx_log(LOG_WARNING,"Cannot evaluate domain with empty name.\n");	    
                    break;
                }

                directory_domain_t *domainentry;
                domainentry = (directory_domain_t *) opbx_mpool_alloc(edir_pool, sizeof(*domainentry), &pool_ret);

                if ( !domainentry) {
                    opbx_log(LOG_DEBUG,"Cannot alloc pool memory for this domain entry.");
                    break;
                }

		opbx_log(LOG_DEBUG,"Parsing domain '%s'\n",domain_name);	    

                // Adding domain to the hashtable.
                opbx_core_hash_insert ( &edir_domain_hash, domain_name, (void *) domainentry );

		char d_context[80];
		char d_subcontext[80];
		char d_language[80];

		for (settings = cwxml_child(domain, "setting"); settings; settings = settings->next) {
		    char *name 	= (char *) cwxml_attr_soft(settings, "name");
		    char *value	= (char *) cwxml_attr_soft(settings, "value");

		    opbx_log(LOG_DEBUG,"setting: %s=%s\n", name, value);

			if      ( !strcmp(name,"context") ) {
                            strncpy( d_context, value, sizeof(d_context));
			}
			else if ( !strcmp(name,"register-context") ) {
                            strncpy( d_subcontext, value, sizeof(d_subcontext));
			}
			else if ( !strcmp(name,"language") ) {
                            strncpy( d_language, value, sizeof(d_language));
			}

		}

                domainentry->name               = opbx_mpool_strdup( edir_pool, domain_name);
                domainentry->context            = opbx_mpool_strdup( edir_pool, d_context);
                domainentry->subscribecontext   = opbx_mpool_strdup( edir_pool, d_subcontext);
                domainentry->language           = opbx_mpool_strdup( edir_pool, d_language);
                hash_init_table( &domainentry->entries, HASH_STRING_KEYS );

                /* Loop through all users */

		if ((users = cwxml_child(domain, "users")))
		for (user = cwxml_child(users, "user"); user; user = user->next) {

		    char *name 	  = (char *) cwxml_attr_soft(user, "name");
		    char *password= (char *) cwxml_attr_soft(user, "password");
		    char *context = (char *) cwxml_attr_soft(user, "context");

                    directory_entry_t *direntry;
                    direntry = (directory_entry_t *) opbx_mpool_alloc(edir_pool, sizeof(*direntry), &pool_ret);

                    if ( !direntry) {
                        opbx_log(LOG_WARNING,"Cannot alloc pool memory for this directory entry.\n");
                        break;
                    }

		    edir_log(LOG_DEBUG,"adding user '%s' (context: %s)\n", name, ( strlen(context) ) ? context : d_context ) ;

                    direntry->user      = opbx_mpool_strdup( edir_pool, name);
                    direntry->domain    = opbx_mpool_strdup( edir_pool, domain_name);
                    direntry->password  = opbx_mpool_strdup( edir_pool, password);
                    direntry->context   = (strlen(context)) ? opbx_mpool_strdup( edir_pool, context) : opbx_mpool_strdup( edir_pool, d_context);
                    direntry->attributes = NULL;

                    directory_entry_attribute_t *attr;

                    if ( strlen(name) ) {
		        for (attribute = cwxml_child(user, "attribute"); attribute; attribute = attribute->next) {


			    char *avar 	  = (char *) cwxml_attr_soft(attribute, "name");
			    char *aval 	  = (char *) cwxml_attr_soft(attribute, "value");

                            attr = (directory_entry_attribute_t *) opbx_mpool_alloc(edir_pool, sizeof(*attr), &pool_ret);
                            if ( !attr) {
                                opbx_log(LOG_WARNING,"Cannot alloc pool memory for this directory entry.\n");
                                break;
                            }

	                    edir_log(LOG_DEBUG," - attribute '%s'='%s'\n", avar, aval);

                            attr->name  = opbx_mpool_strdup( edir_pool, avar);
                            attr->value = opbx_mpool_strdup( edir_pool, aval);

                            attr->next = direntry->attributes;                            
                            direntry->attributes = attr;

                        } // end foreach attribute

                        // We have added the attributes, now. We should load the DB saved ones.

                        char tmp[512];
                        struct opbx_db_entry *db_tree;
                        struct opbx_db_entry *entry;

                        snprintf(tmp, sizeof(tmp), "%s/%s", domain_name, name );

                        db_tree = opbx_db_gettree( EDIR_DB_FAMILY, NULL);
                        for (entry = db_tree; entry; entry = entry->next)
                        {
                            if ( !strncmp(tmp, entry->key, strlen(tmp) ) ) {
                                char *s1,*s2, *s3;
                                if ( (s1 = strchr( entry->key, '/' )) ) {
                                    s1++;
                                    if ( (s2 = strchr( s1, '/' )) ) {
                                        s2++;
                                        // Remove unique identifier.
                                        if ( (s3 = strchr( s2, '/' )) ) {
                                            *s3 = '\0';
                                        }
                                        //edir_log(LOG_DEBUG,"****OK***** %s = %s\n", s2, entry->data);
                                        // Let's store this attribute.
                                        attr = (directory_entry_attribute_t *) opbx_mpool_alloc(edir_pool, sizeof(*attr), &pool_ret);
                                        if ( !attr) {
                                            opbx_log(LOG_WARNING,"Cannot alloc pool memory for this directory entry.\n");
                                            break;
                                        }
                                        attr->name  = opbx_mpool_strdup( edir_pool, s2);
                                        attr->value = opbx_mpool_strdup( edir_pool, entry->data);

                                        attr->next = direntry->attributes;                            
                                        direntry->attributes = attr;

                                    }
                                }
                            }
                        }

                        opbx_db_freetree(db_tree);

                        /* It's time to add the user to the domain hashtable */
                        opbx_core_hash_insert ( &domainentry->entries, name, direntry );

		    } // If strlen(name)
                    else 
                    {
    			opbx_log(LOG_WARNING,"Skipping user with no name.");
                    }

		}
	    }

	}
	else {
	    opbx_log(LOG_ERROR,"Embedded directory engine: settings not found...\n");
	    goto error;
	}
    } 
    else {
	opbx_log(LOG_ERROR,"Embedded directory engine: config stanza not found.\n");
        goto error;
    }

    safe_free(user);
    safe_free(domain);
    safe_free(settings);
    safe_free(xml);

    return 1;

error:
    opbx_mpool_close( &edir_pool );
    if ( enabled == 0) 
	opbx_log(LOG_NOTICE,"Embedded directory engine: disabled.\n");

    return 0;
}

static int edir_release(void) {

    hash_delete_table( &edir_domain_hash );
    opbx_mpool_close(&edir_pool);       // All memory used is released.

    opbx_mutex_destroy(&edir_mutex);    // Ok. Release the mutex. Directory is no more avalaible.

    return 0;
}

static int edir_reload(void) {
    opbx_log(LOG_ERROR,"edir_reload: Not implemented\n");
    //TODO
    return 0;
}

static directory_domain_t *edir_search_domain( char *domain ) {
    int found;

    edir_log(LOG_DEBUG,"edir_search: domain %s\n", domain);

    if ( !domain ) 
        return NULL;

    directory_domain_t *domain_data = NULL;

    found = opbx_core_hash_get ( &edir_domain_hash, domain, (void *) &domain_data );
    
    if ( found && domain_data ) {
        edir_log(LOG_DEBUG,"domain '%s' found\n", domain_data->name );
        return domain_data;
    }

    return NULL;
}

static directory_entry_t *edir_search_user( char *user, char *domain ) {
    edir_log(LOG_DEBUG,"edir_search_user: domain %s - user %s\n", domain, user);

    int found;
    directory_entry_t *res = NULL;

    if ( !domain ) 
        return NULL;

    directory_domain_t *domain_data = NULL;

    found = opbx_core_hash_get ( &edir_domain_hash, domain, (void *) &domain_data );
    
    if ( found && domain_data ) {
        edir_log(LOG_DEBUG,"edir_search_user: domain '%s' found\n", domain_data->name );

        if (user && strlen(user)) {
            hash_table_t *dh = &domain_data->entries;
            directory_entry_t *entry = NULL;

            found = opbx_core_hash_get ( dh, user, (void *) &entry );

            if ( found && entry ) {
                edir_log(LOG_DEBUG,"edir_search_user: found (%s, %s, %s)\n", entry->user, entry->domain, entry->context);

                res = malloc( sizeof(directory_entry_t) );
                if (!res)
                    return NULL;

                memcpy(res,entry,sizeof(directory_entry_t) );
                res->attributes = NULL;

                // Now loop throu the attributes
                directory_entry_attribute_t *attr, *tmp_attr;

                attr = entry->attributes;
                while ( attr ) {
                    if ( (tmp_attr=malloc(sizeof(directory_entry_attribute_t))) ) {
                        tmp_attr->name =strdup( attr->name );
                        tmp_attr->value=strdup( attr->value );
                        tmp_attr->next = res->attributes;
                        res->attributes = tmp_attr;
                        attr=attr->next;    
                    }
    
                }
                
                return res;
            }
        }
        found = 0;
    }

    return NULL;
}

int edir_user_add_attribute( char *domain, char *user, char *name, char *val, int persistant) {

    // key is $domain/$user/$varname/$random[4]

    int found, pool_ret;
    directory_domain_t *domain_data = NULL;

    if ( !domain ) 
        return 0;

    if ( !user ) 
        return 0;

    found = opbx_core_hash_get ( &edir_domain_hash, domain, (void *) &domain_data );

    if ( found && domain_data ) {
        edir_log(LOG_DEBUG,"edir_user_add_attribute: domain '%s' found\n", domain_data->name );

        if (user && strlen(user)) {
            hash_table_t *dh = &domain_data->entries;
            directory_entry_t *entry = NULL;

            found = opbx_core_hash_get ( dh, user, (void *) &entry );

            if ( found && entry ) {
                edir_log(LOG_DEBUG,"edir_user_add_attribute: found (%s, %s, %s)\n", entry->user, entry->domain, entry->context);

                if (name && val) {
                    directory_entry_attribute_t *attr;

                    attr = opbx_mpool_alloc( edir_pool, sizeof(directory_entry_attribute_t), &pool_ret );
                    if (attr) {
                        attr->name = opbx_mpool_strdup( edir_pool, name );
                        attr->value= opbx_mpool_strdup( edir_pool, val );
                        attr->next = entry->attributes;
                        entry->attributes = attr;

                        // Now we have added it to the memory structures. 
                        // It's time to save it to the persistent storage.
                        if ( persistant ) {
                            char tmp[512];
                            snprintf( tmp,sizeof(tmp),"%s/%s/%s/%04X",domain,user,name, (unsigned int) opbx_random() );
                            opbx_db_put( EDIR_DB_FAMILY, tmp, val);
                            edir_log(LOG_DEBUG,"Adding %s \n", tmp);
                        }

                        return 1;
                    }

                }

            }
        }

    }

    return 0;
}

int edir_user_del_attribute( char *domain, char *user, char *name, char *value, int partial_compare) {

    /* 
        It works this way:
        If we have NO value, all entries of attribute 'name' are wiped out
        If we have a value, then only that particular entry is wiped out.
    */

    // key is $domain/$user/$varname/$random[4]

    int found;
    directory_domain_t *domain_data = NULL;

    if ( !domain ) 
        return 0;

    if ( !user ) 
        return 0;

    if ( !name ) 
        return 0;

    found = opbx_core_hash_get ( &edir_domain_hash, domain, (void *) &domain_data );

    if ( found && domain_data ) {
        edir_log(LOG_DEBUG,"edir_user_del_attribute: domain '%s' found\n", domain_data->name );

        if (user && strlen(user)) {
            hash_table_t *dh = &domain_data->entries;
            directory_entry_t *entry = NULL;

            found = opbx_core_hash_get ( dh, user, (void *) &entry );

            if ( found && entry ) {
                edir_log(LOG_DEBUG,"edir_user_del_attribute: found (%s, %s, %s)\n", entry->user, entry->domain, entry->context);

                if ( name ) {
                    directory_entry_attribute_t *attr ,*prev, *tobedel;
                    prev = NULL;
                    attr = entry->attributes;

                    while ( attr ) {
                        found = 0;
                        tobedel = NULL;
                        if ( !strcmp( attr->name, name ) ) {

                            if ( !partial_compare && value && strcmp( attr->value, value ) ) {
                                goto next;
                            }
                            if ( partial_compare && value && strncmp( attr->value, value, strlen(value) ) ) {
                                goto next;
                            }
                            // Here we are matching both name and value, if exists
                            // So remove the attribute from the linked list
                            if ( prev )
                                prev->next = attr->next;
                            else
                                entry->attributes = attr->next;

                            found = 1;
                            tobedel = attr;
                    
                        }
next:       
                        if ( found ) {

                            edir_log(LOG_DEBUG,"* deleting attribute %s => %s \n", attr->name, attr->value);
                            // Now let's remove the item from the DB
                            char tmp[128];
                            snprintf(tmp,sizeof(tmp),"%s/%s/%s/", domain, user, name);

                            opbx_db_deltree_with_value(EDIR_DB_FAMILY, tmp, value);

                            attr=attr->next;
                            opbx_mpool_free( edir_pool, tobedel, sizeof(directory_entry_attribute_t) );
                        }
                        else {
                            edir_log(LOG_DEBUG,"* skipping attribute %s => %s (not matching with %s)\n", attr->name, attr->value, value);
                            prev=attr;
                            attr=attr->next;
                        }
                    }


                }
            }

        }
    }

    return 0;
}

direngine_t directory_embedded_engine = {
	/*! the name of the interface */
	"Embedded directory",
        edir_init,
        edir_release,
        edir_reload,
        edir_search_domain,
        edir_search_user,
        edir_user_add_attribute,
        edir_user_del_attribute
};




/*
        This is what should be done to initialize the core, the directory engine and how to add a pluggable engine.
        ------------------------------------

        sofia_direngine_list_init();
        sofia_direngine_engine_add( &directory_embedded_engine, "/etc/callweaver/conf_sofia.xml" ); //TODO

        sofia_direngine_engine_release( directory_embedded_engine.name );
        sofia_direngine_list_destroy();

*/


// THE FOLLOWING CODE IS FOR DEBUGGING PURPOSES ONLY.
// IT IS HERE ONLY TO GIVE SOME HINTS ON HOW TO USE.
// DOCUMENTATION WILL FOLLOW.

/*
sofia_directory_entry_attribute_t *a;
sofia_directory_entry_t  *user = NULL;
sofia_directory_domain_t *domain = NULL;

opbx_log(LOG_DEBUG,"\n\n\n\n");
domain = sofia_direngine_domain_search("navynet.it");
opbx_log(LOG_DEBUG,"------------> %s\n", domain->name);

opbx_log(LOG_DEBUG,"\n\n\n\n");
user = sofia_direngine_user_search("navynet.it","523");
opbx_log(LOG_DEBUG,"------------> %s\n", user->user);
a = user->attributes;
while ( a ) {
    opbx_log(LOG_DEBUG,"       -----> %s = %s\n", a->name, a->value);
    a=a->next;
}

opbx_log(LOG_DEBUG,"\n\n\n\n");
a=sofia_direngine_attribute_search("navynet.it","523","accountno");
if (a) {
    opbx_log(LOG_DEBUG," search-----> %s = %s\n", a->name, a->value);
}
sofia_direngine_release_attr_result(a);


opbx_log(LOG_DEBUG,"\n\n\n\n");
sofia_direngine_user_add_attribute_persistant("navynet.it","523","foo","bar");
sofia_direngine_user_add_attribute("navynet.it","523","foo3","bar3");
sofia_direngine_user_add_attribute("navynet.it","523","foo","bar2");
opbx_log(LOG_WARNING,"---------------------\n");
sofia_direngine_user_del_attribute("navynet.it","523","foo","bar");


opbx_log(LOG_DEBUG,"\n\n\n\n");
user   = sofia_direngine_user_search("navynet.it","523");
opbx_log(LOG_DEBUG,"------------> %s\n", user->user);
a = user->attributes;
while ( a ) {
    opbx_log(LOG_DEBUG,"       -----> %s = %s\n", a->name, a->value);
    a=a->next;
}

opbx_log(LOG_DEBUG,"\n\n\n\n");
a=sofia_direngine_attribute_search("navynet.it","523","foo");
if (a) {
    while (a) {
        opbx_log(LOG_DEBUG," search-----> %s = %s\n", a->name, a->value);
        a=a->next;
    }
}
sofia_direngine_release_attr_result(a);



opbx_log(LOG_DEBUG,"\n\n\n\n");
sofia_direngine_release_user_result(user);
sofia_direngine_release_domain_result(domain);
        return -1;

*/