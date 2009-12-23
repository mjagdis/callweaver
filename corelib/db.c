/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * SQLite DB Functionality
 * 
 * Copyright (C) 2004, Anthony Minessale II
 *
 * Anthony Minessale II <anthmct@cylynx.com>
 *
 * Original Function Prototypes
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 *
 * \brief SQLite Management
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/file.h"
#include "callweaver/app.h"
#include "callweaver/dsp.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/cli.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/manager.h"
#include "sqlite3.h"

#define SQL_MAX_RETRIES 5
#define SQL_RETRY_USEC  500000

CW_MUTEX_DEFINE_STATIC(dblock);

static const char *create_odb_sql =
"create table odb (\n"
"						 family varchar(255),\n"
"						 keys varchar(255) not null,\n"
"						 value varchar(255) not null\n"
"						 );\n\n"
"CREATE INDEX odb_index_1 ON odb(family);\n"
"CREATE INDEX odb_index_2 ON odb(keys);\n"
"CREATE INDEX odb_index_3 ON odb(value);\n";

static int loaded = 0;

static struct {
	const char *dbdir;
	const char *dbfile;
	const char *dbname;
	const char *tablename;
} globals;

struct cw_db_data {
	char *data;
	int datalen;
	int rownum;
};

static int dbinit(void);
static void sqlite_pick_path(const char *dbname, char *buf, size_t size);
static sqlite3 *sqlite_open_db(const char *filename);
static void sqlite_check_table_exists(const char *dbfile, const char *test_sql, const char *create_sql);
static int get_callback(void *pArg, int argc, char **argv, char **columnNames);
static int tree_callback(void *pArg, int argc, char **argv, char **columnNames);
static int show_callback(void *pArg, int argc, char **argv, char **columnNames);
static int database_show(struct cw_dynstr **ds_p, int argc, char *argv[]);
static int database_put(struct cw_dynstr **ds_p, int argc, char *argv[]);
static int database_get(struct cw_dynstr **ds_p, int argc, char *argv[]);
static int database_del(struct cw_dynstr **ds_p, int argc, char *argv[]);
static int database_deltree(struct cw_dynstr **ds_p, int argc, char *argv[]);


static int sanity_check(void)
{
	if (!loaded) {
		cw_log(CW_LOG_ERROR, "NICE RACE CONDITION WE HAVE HERE! PUTTING THE CART BEFORE THE HORSE EH? >=0\n");
		dbinit();
	}
	return 0;
}

static int dbinit(void)
{
	char *sql;

	cw_mutex_lock(&dblock);
 	globals.dbdir = cw_config_CW_DB_DIR;
	globals.dbfile = cw_config_CW_DB;
	globals.tablename = "odb";
	
	
	if ((sql = sqlite3_mprintf("select count(*) from %q limit 1", globals.tablename))) {
		sqlite_check_table_exists(globals.dbfile, sql, create_odb_sql);
		sqlite3_free(sql);
		sql = NULL;
		loaded = 1;
	}

	cw_mutex_unlock(&dblock);
	return loaded ? 0 : -1;
}


static void sqlite_pick_path(const char *dbname, char *buf, size_t size)
{

	memset(buf, 0, size);
	if (strchr(dbname, '/')) {
		strncpy(buf, dbname, size);
	} else {
		snprintf(buf, size, "%s/%s", globals.dbdir, dbname);
	}
}

static sqlite3 *sqlite_open_db(const char *filename)
{
	sqlite3 *db;
	char path[1024];
	
	sqlite_pick_path(filename, path, sizeof(path));
	if (sqlite3_open(path, &db)) {
		cw_log(CW_LOG_WARNING, "SQL ERR [%s]\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		db=NULL;
	}
	return db;
}


static void sqlite_check_table_exists(const char *dbfile, const char *test_sql, const char *create_sql)
{
	sqlite3 *db;
	char *errmsg;


	if ((db = sqlite_open_db(dbfile))) {
		if (test_sql) {
			sqlite3_exec(
						 db,
						 test_sql,
						 NULL,
						 NULL,
						 &errmsg
						 );

			if (errmsg) {
				cw_log(CW_LOG_WARNING,"SQL ERR [%s]\n[%s]\nAuto Repairing!\n",errmsg,test_sql);
				sqlite3_free(errmsg);
				errmsg = NULL;
				sqlite3_exec(
							 db,
							 create_sql,
							 NULL,
							 NULL,
							 &errmsg
							 );
				if (errmsg) {
					cw_log(CW_LOG_WARNING,"SQL ERR [%s]\n[%s]\n",errmsg,create_sql);
					sqlite3_free(errmsg);
					errmsg = NULL;
				}
			}
			sqlite3_close(db);
		}
	}

}

int cw_db_put(const char *family, const char *keys, const char *value)
{
	char *sql;
	char *zErr = 0;
	int res = 0;
	sqlite3 *db;

	sanity_check();
	if (!(db = sqlite_open_db(globals.dbfile))) {
		return -1;
	}


	if (!family || cw_strlen_zero(family)) {
		family = "_undef_";
	}

	cw_db_del(family, keys);

	if ((sql = sqlite3_mprintf("insert into %q values('%q','%q','%q')", globals.tablename, family, keys, value))) {
		cw_log(CW_LOG_DEBUG, "SQL [%s]\n", sql);

		res = sqlite3_exec(db,
						   sql,
						   NULL,
						   NULL,
						   &zErr
						   );

		if (zErr) {
			cw_log(CW_LOG_ERROR, "SQL ERR [%s] [%s]\n", sql, zErr);
			res = -1;
			sqlite3_free(zErr);
		} else {
			res = 0;
		}
	} else {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		res = -1;	/* Return an error */
	}

	if (sql) {
		sqlite3_free(sql);
		sql = NULL;
	}
	sqlite3_close(db);
	return res;
}

static int get_callback(void *pArg, int argc, char **argv, char **columnNames) 
{
	struct cw_db_data *result = pArg;

	cw_copy_string(result->data, argv[0], result->datalen);
	result->rownum++;
	return 0;
}

int cw_db_get(const char *family, const char *keys, char *value, int valuelen)
{

	char *sql;
	char *zErr = 0;
	int res = 0;
	struct cw_db_data result;
	sqlite3 *db;
	int retry=0;

	sanity_check();
	if (!(db = sqlite_open_db(globals.dbfile))) {
		return -1;
	}

	if (!family || cw_strlen_zero(family)) {
		family = "_undef_";
	}

	result.data = value;
	result.datalen = valuelen;
	result.rownum = 0;

retry_1:
	if ((sql = sqlite3_mprintf("select value from %q where family='%q' and keys='%q'", globals.tablename, family, keys))) {
		cw_log(CW_LOG_DEBUG, "SQL [%s]\n", sql);
		res = sqlite3_exec(db,
						   sql,
						   get_callback,
						   &result,
						   &zErr
						   );
		
		if (zErr) {
			sqlite3_free(zErr);
			if (retry >= SQL_MAX_RETRIES) {
				cw_log(CW_LOG_ERROR, "SQL ERR [%s] [%s]\n", sql, zErr);
			} else {
				cw_log(CW_LOG_WARNING, "SQL ERR [%s] (retry %d)\n", zErr, ++retry);
				usleep(SQL_RETRY_USEC);
				goto retry_1;
			}
			res = -1;
		} else {
			if (result.rownum)
				res = 0;
			else
				res = -1;
		}
	} else {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		res = -1;   /* Return an error */
	}

	if (sql) {
		sqlite3_free(sql);
		sql = NULL;
	}

	sqlite3_close(db);
	return res;
}

static int cw_db_del_main(const char *family, const char *keys, int like, const char *value)
{
	char *sql;
	char *zErr = 0;
	int res = 0;
	sqlite3 *db;
	const char *op = "=";
	const char *pct = "";
	int retry=0;

	sanity_check();
	if (!(db = sqlite_open_db(globals.dbfile))) {
		return -1;
	}

	if (!family || cw_strlen_zero(family)) {
		family = "_undef_";
	}

	if (like) {
		op = "like";
		pct = "%";
	}

	if (family && keys && value) {
		sql = sqlite3_mprintf("delete from %q where family %s '%q%s' and keys %s '%q%s' AND value %s '%q%s' ", 
                                        globals.tablename, op, family, pct, op, keys, pct, op, value, pct );
	} else if (family && keys) {
		sql = sqlite3_mprintf("delete from %q where family %s '%q%s' and keys %s '%q%s'", globals.tablename, op, family, pct, op, keys, pct);
	} else if (family) {
		sql = sqlite3_mprintf("delete from %q where family %s '%q%s'", globals.tablename, op, family, pct);
	} else {
		sql = sqlite3_mprintf("delete from %q", globals.tablename);
	}

	if (sql) {
retry_2:
		if (retry)
			cw_log(CW_LOG_DEBUG, "SQL [%s] (retry %d)\n", sql, retry);
		else
			cw_log(CW_LOG_DEBUG, "SQL [%s]\n", sql);
		res = sqlite3_exec(db,
						   sql,
						   NULL,
						   NULL,
						   &zErr
						   );

		if (zErr) {
			sqlite3_free(zErr);
			if (retry >= SQL_MAX_RETRIES) {
				cw_log(CW_LOG_ERROR, "SQL ERR [%s] [%s]\n", sql, zErr);
			} else {
				cw_log(CW_LOG_WARNING, "SQL ERR [%s] (retry %d)\n", zErr, ++retry);
				usleep(SQL_RETRY_USEC);
				goto retry_2;
			}
			res = -1;
		} else {
			if (!sqlite3_changes(db))
				res = -1;
			else
				res = 0;
		}
	} else {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		res = -1;   /* Return an error */
	}

	if (sql) {
		sqlite3_free(sql);
		sql = NULL;
	}

	sqlite3_close(db);
	return res;
}

int cw_db_del(const char *family, const char *keys)
{
	return cw_db_del_main(family, keys, 0, NULL);
}

int cw_db_deltree(const char *family, const char *keytree)
{
	return cw_db_del_main(family, keytree, 1, NULL);
}

int cw_db_deltree_with_value(const char *family, const char *keytree, const char *value)
{
	return cw_db_del_main(family, keytree, 1, value);
}

static int tree_callback(void *pArg, int argc, char **argv, char **columnNames) 
{
	int x = 0;
	char *keys, *values;
	struct cw_db_entry **treeptr = pArg,
		*cur = NULL,
		*ret = NULL;

	for(x=0; x < argc; x++) {
		keys = argv[0];
		values = argv[1];

		cur = malloc(sizeof(struct cw_db_entry) + strlen(keys) + strlen(values) + 2);
		if (cur) {
			cur->next = NULL;
			cur->key = cur->data + strlen(values) + 1;
			strcpy(cur->data, values);
			strcpy(cur->key, keys);
			if (*treeptr) {
				cur->next = *treeptr;
			}
			ret = cur;
		}
	}

	*treeptr = ret;
	return 0;
}

struct cw_db_entry *cw_db_gettree(const char *family, const char *keytree)
{
	char *sql;
	char *zErr = 0;
	int res = 0;
	struct cw_db_entry *tree = NULL;
	sqlite3 *db;
	int retry=0;

	sanity_check();
	if (!(db = sqlite_open_db(globals.dbfile))) {
		return NULL;
	}

	if (!family || cw_strlen_zero(family)) {
		family = "_undef_";
	}

	if (family && keytree && !cw_strlen_zero(keytree)) {
		sql = sqlite3_mprintf("select keys,value from %q where family='%q' and keys like '%q%%'", globals.tablename, family, keytree);
	} else if(family) {
		sql = sqlite3_mprintf("select keys,value from %q where family='%q'", globals.tablename, family);
	} else {
		cw_log(CW_LOG_ERROR, "No parameters supplied.\n");
		return NULL;
	}

	if (sql) {
retry_3:
		if (retry)
			cw_log(CW_LOG_DEBUG, "SQL [%s] (retry %d)\n", sql, retry);
		else
			cw_log(CW_LOG_DEBUG, "SQL [%s]\n", sql);
		res = sqlite3_exec(db,
						   sql,
						   tree_callback,
						   &tree,
						   &zErr
						   );
		
		if (zErr) {
			sqlite3_free(zErr);
			if (retry >= SQL_MAX_RETRIES) {
				cw_log(CW_LOG_ERROR, "SQL ERR [%s] [%s]\n", sql, zErr);
			} else {
				cw_log(CW_LOG_WARNING, "SQL ERR [%s] (retry %d)\n", zErr, ++retry);
				usleep(SQL_RETRY_USEC);
				goto retry_3;
			}
			res = -1;
		} else {
			res = 0;
		}
	} else {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		res = -1;   /* Return an error */
	}

	if (sql) {
		sqlite3_free(sql);
		sql = NULL;
	}

	sqlite3_close(db);
	return tree;

}

void cw_db_freetree(struct cw_db_entry *dbe)
{
	struct cw_db_entry *last;

	while (dbe) {
		last = dbe;
		dbe = dbe->next;
		free(last);
	}
}


static int show_callback(void *pArg, int argc, char **argv, char **columnNames) 
{
	struct cw_dynstr **ds_p = pArg;

	cw_dynstr_printf(ds_p, "/%s/%-50s: %-25s\n", argv[0], argv[1], argv[2]);

	return 0;
}

static int database_show(struct cw_dynstr **ds_p, int argc, char *argv[])
{
	char *prefix, *family;
	char *sql;
	char *zErr = 0;
	int res = 0;
	sqlite3 *db;

	sanity_check();
	if (!(db = sqlite_open_db(globals.dbfile))) {
		return -1;
	}

	if (argc == 4) {
		/* Family and key tree */
		prefix = argv[3];
		family = argv[2];
	} else if (argc == 3) {
		/* Family only */
		family = argv[2];
		prefix = NULL;
	} else if (argc == 2) {
		/* Neither */
		prefix = family = NULL;
	} else {
		return RESULT_SHOWUSAGE;
	}

	if (family && prefix) {
		sql = sqlite3_mprintf("select * from %q where family='%q' and keys='%q'", globals.tablename, family, prefix);
	} else if (family) {
		sql = sqlite3_mprintf("select * from %q where family='%q'", globals.tablename, family);
	} else {
		sql = sqlite3_mprintf("select * from %q", globals.tablename);
	}

	if (sql) {
		cw_log(CW_LOG_DEBUG, "SQL [%s]\n", sql);
		res = sqlite3_exec(db,
						   sql,
						   show_callback,
						   ds_p,
						   &zErr
						   );
		
		if (zErr) {
			cw_log(CW_LOG_ERROR, "SQL ERR [%s] [%s]\n", sql, zErr);
			res = -1;
			sqlite3_free(zErr);
		} else {
			res = 0;
		}
	} else {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		res = -1;   /* Return an error */
	}

	if (sql) {
		sqlite3_free(sql);
		sql = NULL;
	}

	sqlite3_close(db);
	return RESULT_SUCCESS;	
}



static int database_put(struct cw_dynstr **ds_p, int argc, char *argv[])
{
	int res;
	if (argc != 5)
		return RESULT_SHOWUSAGE;
	res = cw_db_put(argv[2], argv[3], argv[4]);
	if (res)  {
		cw_dynstr_printf(ds_p, "Failed to update entry\n");
	} else {
		cw_dynstr_printf(ds_p, "Updated database successfully\n");
	}
	return RESULT_SUCCESS;
}

static int database_get(struct cw_dynstr **ds_p, int argc, char *argv[])
{
	int res;
	char tmp[256];
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	res = cw_db_get(argv[2], argv[3], tmp, sizeof(tmp));
	if (res) {
		cw_dynstr_printf(ds_p, "Database entry not found.\n");
	} else {
		cw_dynstr_printf(ds_p, "Value: %s\n", tmp);
	}
	return RESULT_SUCCESS;
}

static int database_del(struct cw_dynstr **ds_p, int argc, char *argv[])
{
	int res;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	res = cw_db_del(argv[2], argv[3]);
	if (res) {
		cw_dynstr_printf(ds_p, "Database entry does not exist.\n");
	} else {
		cw_dynstr_printf(ds_p, "Database entry removed.\n");
	}
	return RESULT_SUCCESS;
}

static int database_deltree(struct cw_dynstr **ds_p, int argc, char *argv[])
{
	int res;
	if ((argc < 3) || (argc > 4))
		return RESULT_SHOWUSAGE;
	if (argc == 4) {
		res = cw_db_deltree(argv[2], argv[3]);
	} else {
		res = cw_db_deltree(argv[2], NULL);
	}
	if (res) {
		cw_dynstr_printf(ds_p, "Database entries do not exist.\n");
	} else {
		cw_dynstr_printf(ds_p, "Database entries removed.\n");
	}
	return RESULT_SUCCESS;
}


static const char database_show_usage[] =
"Usage: database show [family [keytree]]\n"
"       Shows CallWeaver database contents, optionally restricted\n"
"to a given family, or family and keytree.\n";

static const char database_put_usage[] =
"Usage: database put <family> <key> <value>\n"
"       Adds or updates an entry in the CallWeaver database for\n"
"a given family, key, and value.\n";

static const char database_get_usage[] =
"Usage: database get <family> <key>\n"
"       Retrieves an entry in the CallWeaver database for a given\n"
"family and key.\n";

static const char database_del_usage[] =
"Usage: database del <family> <key>\n"
"       Deletes an entry in the CallWeaver database for a given\n"
"family and key.\n";

static const char database_deltree_usage[] =
"Usage: database deltree <family> [keytree]\n"
"       Deletes a family or specific keytree within a family\n"
"in the CallWeaver database.\n";

struct cw_clicmd cli_database_show = {
	.cmda = { "database", "show", NULL },
	.handler = database_show,
	.summary = "Shows database contents",
	.usage = database_show_usage,
};

struct cw_clicmd cli_database_get = {
	.cmda = { "database", "get", NULL },
	.handler = database_get,
	.summary = "Gets database value",
	.usage = database_get_usage,
};

struct cw_clicmd cli_database_put = {
	.cmda = { "database", "put", NULL },
	.handler = database_put,
	.summary = "Adds/updates database value",
	.usage = database_put_usage,
};

struct cw_clicmd cli_database_del = {
	.cmda = { "database", "del", NULL },
	.handler = database_del,
	.summary = "Removes database key/value",
	.usage = database_del_usage,
};

struct cw_clicmd cli_database_deltree = {
	.cmda = { "database", "deltree", NULL },
	.handler = database_deltree,
	.summary = "Removes database keytree/values",
	.usage = database_deltree_usage,
};

int cwdb_init(void)
{
	int res = dbinit();
	if (res == 0) {
		cw_cli_register(&cli_database_show);
		cw_cli_register(&cli_database_get);
		cw_cli_register(&cli_database_put);
		cw_cli_register(&cli_database_del);
		cw_cli_register(&cli_database_deltree);
	}
	return res;
}

