/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Connect to MySQL
 * 
 * Copyright (C) 2004, Constantine Filin and Christos Ricudis
 *
 * Christos Ricudis <ricudis@itc.auth.gr>
 * Constantine Filin <cf@intermedia.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>

#include <mysql/mysql.h>

#include "callweaver.h"

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/linkedlists.h"
#include "callweaver/chanvars.h"
#include "callweaver/lock.h"


#define OPBX_MODULE "app_addon_sql_mysql"

#define EXTRA_LOG 0

static void *app;
static const char name[] = "MYSQL";
static const char synopsis[] = "Do several mySQLy things";
static const char syntax[] = "MYSQL()";
static const char descrip[] = 
"Do several mySQLy things\n"
"Syntax:\n"
"  MYSQL(Connect connid dhhost dbuser dbpass dbname)\n"
"    Connects to a database.  Arguments contain standard MySQL parameters\n"
"    passed to function mysql_real_connect.  Connection identifer returned\n"
"    in ${var}\n"
"  MYSQL(Query resultid ${connid} query-string)\n"
"    Executes standard MySQL query contained in query-string using established\n"
"    connection identified by ${connection_identifier}. Result of query is\n"
"    is stored in ${var}.\n"
"  MYSQL(Fetch fetchid ${resultid} var1 var2 ... varN)\n"
"    Fetches a single row from a result set contained in ${result_identifier}.\n"
"    Assigns returned fields to ${var1} ... ${varn}.  ${fetchid} is set TRUE\n"
"    if additional rows exist in result set.\n"
"  MYSQL(Clear ${resultid})\n"
"    Frees memory and datastructures associated with result set.\n" 
"  MYSQL(Disconnect ${connid})\n"
"    Disconnects from named connection to MySQL.\n"
"  On exit, always returns 0. Sets MYSQL_STATUS to 0 on success and -1 on error.\n";

/*	
EXAMPLES OF USE : 

exten => s,2,MYSQL(Connect connid localhost callweaver mypass credit)
exten => s,3,MYSQL(Query resultid ${connid} SELECT username,credit FROM credit WHERE callerid=${CALLERIDNUM})
exten => s,4,MYSQL(Fetch fetchid ${resultid} datavar1 datavar2)
exten => s,5,GotoIf(${fetchid}?6:8)
exten => s,6,Festival("User ${datavar1} currently has credit balance of ${datavar2} dollars.")	
exten => s,7,Goto(s,4)
exten => s,8,MYSQL(Clear ${resultid})
exten => s,9,MYSQL(Disconnect ${connid})
*/

OPBX_MUTEX_DEFINE_STATIC(_mysql_mutex);

#define OPBX_MYSQL_ID_DUMMY   0
#define OPBX_MYSQL_ID_CONNID  1
#define OPBX_MYSQL_ID_RESID   2
#define OPBX_MYSQL_ID_FETCHID 3

struct opbx_MYSQL_id {
	int identifier_type; /* 0=dummy, 1=connid, 2=resultid */
	int identifier;
	void *data;
	OPBX_LIST_ENTRY(opbx_MYSQL_id) entries;
} *opbx_MYSQL_id;

OPBX_LIST_HEAD(MYSQLidshead,opbx_MYSQL_id) _mysql_ids_head;

/* helpful procs */
static void *find_identifier(int identifier,int identifier_type) {
	struct MYSQLidshead *headp;
	struct opbx_MYSQL_id *i;
	void *res=NULL;
	int found=0;
	
	headp=&_mysql_ids_head;
	
	if (OPBX_LIST_LOCK(headp)) {
		opbx_log(OPBX_LOG_WARNING,"Unable to lock identifiers list\n");
	} else {
		OPBX_LIST_TRAVERSE(headp,i,entries) {
			if ((i->identifier==identifier) && (i->identifier_type==identifier_type)) {
				found=1;
				res=i->data;
				break;
			}
		}
		if (!found) {
			opbx_log(OPBX_LOG_WARNING,"Identifier %d, identifier_type %d not found in identifier list\n",identifier,identifier_type);
		}
		OPBX_LIST_UNLOCK(headp);
	}
	
	return res;
}

static int add_identifier(int identifier_type,void *data) {
	struct opbx_MYSQL_id *i,*j;
	struct MYSQLidshead *headp;
	int maxidentifier=0;
	
	headp=&_mysql_ids_head;
	i=NULL;
	j=NULL;
	
	if (OPBX_LIST_LOCK(headp)) {
		opbx_log(OPBX_LOG_WARNING,"Unable to lock identifiers list\n");
		return(-1);
	} else {
 		i=malloc(sizeof(struct opbx_MYSQL_id));
		OPBX_LIST_TRAVERSE(headp,j,entries) {
			if (j->identifier>maxidentifier) {
				maxidentifier=j->identifier;
			}
		}
		i->identifier=maxidentifier+1;
		i->identifier_type=identifier_type;
		i->data=data;
		OPBX_LIST_INSERT_HEAD(headp,i,entries);
		OPBX_LIST_UNLOCK(headp);
	}
	return i->identifier;
}

static int del_identifier(int identifier,int identifier_type) {
	struct opbx_MYSQL_id *i;
	struct MYSQLidshead *headp;
	int found=0;
	
        headp=&_mysql_ids_head;
        
        if (OPBX_LIST_LOCK(headp)) {
		opbx_log(OPBX_LOG_WARNING,"Unable to lock identifiers list\n");
	} else {
		OPBX_LIST_TRAVERSE(headp,i,entries) {
			if ((i->identifier==identifier) && 
			    (i->identifier_type==identifier_type)) {
				OPBX_LIST_REMOVE(headp,i,entries);
				free(i);
				found=1;
				break;
			}
		}
		OPBX_LIST_UNLOCK(headp);
	}
	                
	if (found==0) {
		opbx_log(OPBX_LOG_WARNING,"Could not find identifier %d, identifier_type %d in list to delete\n",identifier,identifier_type);
		return(-1);
	} else {
		return(0);
	}
}

static int set_callweaver_int(struct opbx_channel *chan, char *varname, int id) {
	if( id>=0 ) {
		char s[100] = "";
		snprintf(s, sizeof(s)-1, "%d", id);
#if EXTRA_LOG
		opbx_log(OPBX_LOG_WARNING,"MYSQL: setting var '%s' to value '%s'\n",varname,s);
#endif
		pbx_builtin_setvar_helper(chan,varname,s);
	}
	return id;
}

static int add_identifier_and_set_callweaver_int(struct opbx_channel *chan, char *varname, int identifier_type, void *data) {
	return set_callweaver_int(chan,varname,add_identifier(identifier_type,data));
}

static int safe_scan_int( char** data, char* delim, int def ) {
	char* end;
	int res = def;
	char* s = strsep(data,delim);
	if( s ) {
		res = strtol(s,&end,10);
		if (*end) res = def;  /* not an integer */
	}
	return res;
}

/* MYSQL operations */
static int aMYSQL_connect(struct opbx_channel *chan, char *data) {
	
	MYSQL *mysql;

	char *connid_var;
	char *dbhost;
	char *dbuser;
	char *dbpass;
	char *dbname;
	 
	strsep(&data," "); // eat the first token, we already know it :P 

	connid_var=strsep(&data," ");
	dbhost=strsep(&data," ");
	dbuser=strsep(&data," ");
	dbpass=strsep(&data," ");
	dbname=strsep(&data,"\n");
	
	if( connid_var && dbhost && dbuser && dbpass && dbname ) {
		mysql = mysql_init(NULL);
		if (mysql) {
			if (mysql_real_connect(mysql,dbhost,dbuser,dbpass,dbname,0,NULL,0)) {
				add_identifier_and_set_callweaver_int(chan,connid_var,OPBX_MYSQL_ID_CONNID,mysql);
				return 0;
			}
			else {
				opbx_log(OPBX_LOG_WARNING,"mysql_real_connect(mysql,%s,%s,dbpass,%s,...) failed\n",dbhost,dbuser,dbname);
			}
		}
		else {
			opbx_log(OPBX_LOG_WARNING,"myslq_init returned NULL\n");
		}
 	}
	else {
		opbx_log(OPBX_LOG_WARNING,"MYSQL(connect is missing some arguments\n");
	}

	return -1;
}

static int aMYSQL_query(struct opbx_channel *chan, char *data) {
	
	MYSQL       *mysql;
	MYSQL_RES   *mysqlres;

	char *resultid_var;
	int connid;
	char *querystring;

	strsep(&data," "); // eat the first token, we already know it :P 

	resultid_var = strsep(&data," ");
	connid       = safe_scan_int(&data," ",-1);
	querystring  = strsep(&data,"\n");

	if (resultid_var && (connid>=0) && querystring) {
		if ((mysql=find_identifier(connid,OPBX_MYSQL_ID_CONNID))!=NULL) {
			mysql_query(mysql,querystring);
			if ((mysqlres=mysql_use_result(mysql))!=NULL) {
				add_identifier_and_set_callweaver_int(chan,resultid_var,OPBX_MYSQL_ID_RESID,mysqlres);
				return 0;
			}
			else if( mysql_field_count(mysql)==0 ) {
				return 0;  // See http://dev.mysql.com/doc/mysql/en/mysql_field_count.html
			}
			else {
				opbx_log(OPBX_LOG_WARNING,"aMYSQL_query: mysql_store_result() failed on query %s\n",querystring);
			}
		}
		else {
			opbx_log(OPBX_LOG_WARNING,"aMYSQL_query: Invalid connection identifier %d passed in aMYSQL_query\n",connid);
		}
	}
	else {
		opbx_log(OPBX_LOG_WARNING,"aMYSQL_query: missing some arguments\n");
	}
	
	return -1;
}


static int aMYSQL_fetch(struct opbx_channel *chan, char *data) {
	
	MYSQL_RES *mysqlres;
	MYSQL_ROW mysqlrow;

	char *fetchid_var,*s5,*s6;
	int resultid,numFields,j;
	
	strsep(&data," "); // eat the first token, we already know it :P 

	fetchid_var = strsep(&data," ");
	resultid    = safe_scan_int(&data," ",-1);

	if (fetchid_var && (resultid>=0) ) {
		if ((mysqlres=find_identifier(resultid,OPBX_MYSQL_ID_RESID))!=NULL) {
			/* Grab the next row */
			if ((mysqlrow=mysql_fetch_row(mysqlres))!=NULL) {
				numFields=mysql_num_fields(mysqlres);
				for (j=0;j<numFields;j++) {
					s5=strsep(&data," ");
					if (s5==NULL) {
						opbx_log(OPBX_LOG_WARNING,"opbx_MYSQL_fetch: More fields (%d) than variables (%d)\n",numFields,j);
						break;
					}
					s6=mysqlrow[j];
					pbx_builtin_setvar_helper(chan,s5, s6 ? s6 : "NULL");
				}
#if EXTRA_LOG
				opbx_log(OPBX_LOG_WARNING,"opbx_MYSQL_fetch: numFields=%d\n",numFields);
#endif
				set_callweaver_int(chan,fetchid_var,1); // try more rows
			} else {
#if EXTRA_LOG
				opbx_log(OPBX_LOG_WARNING,"opbx_MYSQL_fetch : EOF\n");
#endif
				set_callweaver_int(chan,fetchid_var,0); // no more rows
			}
			return 0;
		}
		else {
			opbx_log(OPBX_LOG_WARNING,"aMYSQL_fetch: Invalid result identifier %d passed\n",resultid);
		}
	}
	else {
		opbx_log(OPBX_LOG_WARNING,"aMYSQL_fetch: missing some arguments\n");
	}

	return -1;
}

static int aMYSQL_clear(struct opbx_channel *chan, char *data) {

	MYSQL_RES *mysqlres;

	int id;
	strsep(&data," "); // eat the first token, we already know it :P 
	id = safe_scan_int(&data," \n",-1);
	if ((mysqlres=find_identifier(id,OPBX_MYSQL_ID_RESID))==NULL) {
		opbx_log(OPBX_LOG_WARNING,"Invalid result identifier %d passed in aMYSQL_clear\n",id);
	} else {
		mysql_free_result(mysqlres);
		del_identifier(id,OPBX_MYSQL_ID_RESID);
	}

	return 0;
}

static int aMYSQL_disconnect(struct opbx_channel *chan, char *data) {
	
	MYSQL *mysql;
	int id;
	strsep(&data," "); // eat the first token, we already know it :P 

	id = safe_scan_int(&data," \n",-1);
	if ((mysql=find_identifier(id,OPBX_MYSQL_ID_CONNID))==NULL) {
		opbx_log(OPBX_LOG_WARNING,"Invalid connection identifier %d passed in aMYSQL_disconnect\n",id);
	} else {
		mysql_close(mysql);
		del_identifier(id,OPBX_MYSQL_ID_CONNID);
	} 

	return 0;
}

static int MYSQL_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct localuser *u;
	int res;
	char sresult[10];

	if (argc == 0 || !argv[0][0]) {
		opbx_log(OPBX_LOG_WARNING, "APP_MYSQL requires an argument (see manual)\n");
		return -1;
	}

#if EXTRA_LOG
	fprintf(stderr,"MYSQL_exec: arg=%s\n", argv[0]);
#endif

	LOCAL_USER_ADD(u);
	res=0;

	opbx_mutex_lock(&_mysql_mutex);

	if (strncasecmp("connect",argv[0],strlen("connect"))==0) {
		res=aMYSQL_connect(chan,opbx_strdupa(argv[0]));
	} else 	if (strncasecmp("query",argv[0],strlen("query"))==0) {
		res=aMYSQL_query(chan,opbx_strdupa(argv[0]));
	} else 	if (strncasecmp("fetch",argv[0],strlen("fetch"))==0) {
		res=aMYSQL_fetch(chan,opbx_strdupa(argv[0]));
	} else 	if (strncasecmp("clear",argv[0],strlen("clear"))==0) {
		res=aMYSQL_clear(chan,opbx_strdupa(argv[0]));
	} else 	if (strncasecmp("disconnect",argv[0],strlen("disconnect"))==0) {
		res=aMYSQL_disconnect(chan,opbx_strdupa(argv[0]));
	} else {
		opbx_log(OPBX_LOG_WARNING, "Unknown argument to MYSQL application : %s\n", argv[0]);
		res=-1;	
	}
		
	opbx_mutex_unlock(&_mysql_mutex);

	LOCAL_USER_REMOVE(u);
	snprintf(sresult, sizeof(sresult), "%d", res);
	pbx_builtin_setvar_helper(chan, "MYSQL_STATUS", sresult);
	return 0;
}

static int unload_module(void)
{
	int res = 0;

	res |= opbx_unregister_function(app);
	return res;
}

static int load_module(void)
{
	struct MYSQLidshead *headp = &_mysql_ids_head;
	OPBX_LIST_HEAD_INIT(headp);
	app = opbx_register_function(name, MYSQL_exec, synopsis, syntax, descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, "Simple MySQL Interface")
