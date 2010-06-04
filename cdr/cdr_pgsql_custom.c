/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Eris Associates Limited, UK
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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
 * \brief Store CDR records in a PostgreSQL database using a configurable field list.
 *
 * \author Mike Jagdis <mjagdis@eris-associates.co.uk>
 *
 * See also
 * \arg \ref Config_cdr
 * \arg http://www.postgresql.org/
 *
 * \ingroup cdr_drivers
 */

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <libpq-fe.h>

#include "callweaver/channel.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"
#include "callweaver/config.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/cli.h"
#include "callweaver/options.h"
#include "callweaver/app.h"

static const char config_file[] = "cdr_pgsql_custom.conf";

static const char desc[] = "Custom PostgreSQL CDR Backend";
static const char name[] = "cdr_pgsql_custom";

struct cw_dynstr dbpath = CW_DYNSTR_INIT;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static PGconn *db = NULL;

static char *conninfo, *table, *columns_str, *values_str;
static struct cw_dynargs columns = CW_DYNARRAY_INIT;
static struct cw_dynargs values = CW_DYNARRAY_INIT;
struct cw_dynstr evalbuf = CW_DYNSTR_INIT;

static struct cw_channel *chan;


#define I_INSERT	0
#define I_BEGIN		1
#define I_COMMIT	2
#define I_ROLLBACK	3

static struct {
	const char *name;
	const char *text;
	size_t len;
} cmd[] = {
	[I_INSERT] = { "insert", NULL, 0 },
	[I_BEGIN] = { "begin", "begin transaction;", sizeof("begin transaction;") },
	[I_COMMIT] = { "commit", "commit transaction;", sizeof("commit transaction;") },
	[I_ROLLBACK] = { "rollback", "rollback transaction;", sizeof("rollback transaction;") },
};


static void dbclose(void)
{
	if (db) {
		PQfinish(db);
		db = NULL;
	}
}


static int do_prepares(void)
{
	PGresult *pgres;
	int i, res;

	for (i = 0; i < arraysize(cmd); i++) {
		pgres = PQprepare(db, cmd[i].name, cmd[i].text, 0, NULL);
		res = PQresultStatus(pgres);
		PQclear(pgres);
		if (res != PGRES_COMMAND_OK) {
			cw_log(CW_LOG_ERROR, "prepare \"%6.6s...\" failed: %s\n", cmd[i].text, PQresultErrorMessage(pgres));
			return -1;
		}
	}

	return 0;
}


static int dbopen(int force)
{
	struct cw_dynstr sql_cmd = CW_DYNSTR_INIT;
	PGresult *pgres;
	int i, res;

	if (!table)
		return -1;

	if (!force && db) {
		/* we may already be connected */
		if (PQstatus(db) == CONNECTION_OK)
			goto ok;

		if (PQstatus(db) != CONNECTION_BAD) {
			PQreset(db);
			if (PQstatus(db) == CONNECTION_OK)
				goto ok;
		}
	}

	dbclose();

	/* is the database there? */
	db = PQconnectdb(conninfo);
	if (PQstatus(db) != CONNECTION_OK) {
		cw_log(CW_LOG_ERROR, "Failed to connect to database: %s\n", PQerrorMessage(db));
		goto err;
	}

	/* is the table there? */
	cw_dynstr_printf(&sql_cmd, "SELECT COUNT(AcctId) FROM '");
	i = strlen(table);
	cw_dynstr_need(&sql_cmd, 2 * i + 1);
	if (!sql_cmd.error) {
		PQescapeStringConn(db, &sql_cmd.data[sql_cmd.used], table, i, NULL);
		cw_dynstr_printf(&sql_cmd, "';");
	}
	if (sql_cmd.error)
		goto err;

	pgres = PQexec(db, sql_cmd.data);
	res = PQresultStatus(pgres);
	PQclear(pgres);
	if (res != PGRES_COMMAND_OK) {
		cw_log(CW_LOG_ERROR, "Unable to access table \"%s\": %s\n", table, PQresultErrorMessage(pgres));
		goto err;
	}

	if (!cmd[I_INSERT].text || !do_prepares()) {
ok:
		return 0;
	}

err:
	dbclose();
	return -1;
}


static int pgsql_log(struct cw_cdr *submission)
{
	struct cw_cdr *batch, *cdrset, *cdr;
	PGresult *pgres;
	int res = 0;

	pthread_mutex_lock(&lock);

	if (!table)
		goto done;

restart:
	if (dbopen(0))
		goto done;

	pgres = PQexecPrepared(db, "begin", 0, NULL, NULL, NULL, 0);
	res = PQresultStatus(pgres);
	PQclear(pgres);
	if (res != PGRES_COMMAND_OK) {
		usleep(10);
		goto restart;
	}

	batch = submission;
	while ((cdrset = batch)) {
		batch = batch->batch_next;

		while ((cdr = cdrset)) {
			chan->cdr = cdr;

			pbx_substitute_variables(chan, NULL, values_str, &evalbuf);

			if (!evalbuf.error && !cw_separate_app_args(&values, evalbuf.data, ",", '\0', NULL)) {
				cdrset = cdrset->next;

				pgres = PQexecPrepared(db, "insert", values.used, (const char * const *)values.data, NULL, NULL, 0);
				res = PQresultStatus(pgres);
				PQclear(pgres);

				cw_dynstr_reset(&evalbuf);

				if (res == PGRES_COMMAND_OK)
					continue;
			}

			pgres = PQexecPrepared(db, "rollback", 0, NULL, NULL, NULL, 0);
			res = PQresultStatus(pgres);
			PQclear(pgres);
			if (res != PGRES_COMMAND_OK) {
				cw_log(CW_LOG_ERROR, "rollback failed: %s\n", PQresultErrorMessage(pgres));
				goto done;
			}

			usleep(10);
			goto restart;
		}
	}

	pgres = PQexecPrepared(db, "commit", 0, NULL, NULL, NULL, 0);
	res = PQresultStatus(pgres);
	PQclear(pgres);
	if (res != PGRES_COMMAND_OK) {
		pgres = PQexecPrepared(db, "rollback", 0, NULL, NULL, NULL, 0);
		res = PQresultStatus(pgres);
		PQclear(pgres);
		if (res != PGRES_COMMAND_OK)
			dbclose();
		usleep(10);
		goto restart;
	}

done:
	pthread_mutex_unlock(&lock);
	return res;
}


static struct cw_cdrbe cdrbe = {
	.name = name,
	.description = desc,
	.handler = pgsql_log,
};


static void release(void)
{
	dbclose();

	if (cmd[I_INSERT].text)
		free((char *)cmd[I_INSERT].text);

	if (values_str)
		free(values_str);
	cw_dynargs_free(&values);

	if (columns_str)
		free(columns_str);
	cw_dynargs_free(&columns);

	if (table)
		free(table);

	if (conninfo)
		free(conninfo);

	cw_dynstr_free(&evalbuf);
	cw_dynstr_free(&dbpath);

	if (chan)
		cw_channel_free(chan);
}


static int unload_module(void)
{
	cw_cdrbe_unregister(&cdrbe);
	return 0;
}


static int reload_module(void)
{
	struct cw_config *cfg;
	char *tmp;
	int res = -1;

	pthread_mutex_lock(&lock);

	dbclose();

	if (conninfo) {
		free(conninfo);
		conninfo = NULL;
	}

	if (values_str) {
		free(values_str);
		values_str = NULL;
	}
	cw_dynargs_reset(&values);

	if (columns_str) {
		free(columns_str);
		columns_str = NULL;
	}
	cw_dynargs_reset(&columns);

	if (table) {
		free(table);
		table = NULL;
	}

	if ((cfg = cw_config_load(config_file))) {
		res = 0;

		if ((tmp = cw_variable_retrieve(cfg, "global", "dsn")))
			conninfo = strdup(tmp);
		else {
			cw_log(CW_LOG_WARNING, "No DSN found. Using \"dbname=callweaver user=callweaver\".\n");
			conninfo = strdup("dbname=callweaver user=callweaver");
		}

		if (cw_variable_browse(cfg, "master")) {
			/* Previous cdr_pgsql_custom had the table in "global" but cdr_sqlite3_custom
			 * has it in "master". For the sake of compatibility with expectations we
			 * look in both places here.
			 */
			if ((tmp = cw_variable_retrieve(cfg, "global", "table"))
			|| (tmp = cw_variable_retrieve(cfg, "master", "table")))
				table = strdup(tmp);
			else {
				cw_log(CW_LOG_WARNING, "Table name not specified. Assuming cdr.\n");
				table = strdup("cdr");
			}

			if ((tmp = cw_variable_retrieve(cfg, "master", "columns"))) {
				if (!(columns_str = strdup(tmp)) || cw_separate_app_args(&columns, columns_str, ",", '\0', NULL)) {
					cw_log(CW_LOG_ERROR, "Out of memory!\n");
					res = -1;
				}
			} else {
				cw_log(CW_LOG_ERROR, "Column names not specified.\n");
				res = -1;
			}

			if ((tmp = cw_variable_retrieve(cfg, "master", "values"))) {
				if (!(values_str = strdup(tmp))) {
					cw_log(CW_LOG_ERROR, "Out of memory!\n");
					res = -1;
				}
			} else {
				cw_log(CW_LOG_ERROR, "Values not specified.\n");
				res = -1;
			}
		}

		cw_config_destroy(cfg);

		if (!res && !(res = dbopen(1))) {
			struct cw_dynstr ds = CW_DYNSTR_INIT;
			int i, l;

			res = -1;

			cw_dynstr_printf(&ds, "INSERT INTO ");

			l = strlen(table);
			cw_dynstr_need(&ds, 2 * l + 1);
			if (!ds.error) {
				PQescapeStringConn(db, &ds.data[ds.used], table, l, NULL);
				cw_dynstr_printf(&ds, " (");

				for (i = 0; !ds.error && i < columns.used; i++) {
					l = strlen(columns.data[i]);
					cw_dynstr_need(&ds, 2 + 2 * l + 1);
					if (!ds.error) {
						if (i == 0)
							cw_dynstr_printf(&ds, ", ");
						PQescapeStringConn(db, &ds.data[ds.used], columns.data[i], l, NULL);
					}
				}

				cw_dynstr_printf(&ds, ") VALUES (?");

				for (i = 1; i < columns.used; i++)
					cw_dynstr_printf(&ds, ", ?");

				cw_dynstr_printf(&ds, ")");

				if (!ds.error) {
					cmd[I_INSERT].len = ds.used;
					cmd[I_INSERT].text = cw_dynstr_steal(&ds);
					res = do_prepares();
				}
			}
		}
	} else
		cw_log(CW_LOG_WARNING, "Failed to load configuration file \"%s\"\n", config_file);

	if (res && table) {
		free(table);
		table = NULL;
	}

	pthread_mutex_unlock(&lock);

	return res;
}


static int load_module(void)
{
	int res = -1;

	cw_dynstr_printf(&dbpath, "%s/master.db", cw_config[CW_LOG_DIR]);

	if (!dbpath.error && !reload_module() && (chan = cw_channel_alloc(0, NULL))) {
		cw_cdrbe_register(&cdrbe);
		res = 0;
	}

	return res;
}


MODULE_INFO(load_module, reload_module, unload_module, release, desc)
