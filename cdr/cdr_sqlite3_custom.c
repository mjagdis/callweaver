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
 * \brief Store CDR records in a SQLite database using a configurable field list.
 *
 * \author Mike Jagdis <mjagdis@eris-associates.co.uk>
 *
 * See also
 * \arg \ref Config_cdr
 * \arg http://www.sqlite.org/
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
#include <sqlite3.h>

#include "callweaver/channel.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"
#include "callweaver/config.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/cli.h"
#include "callweaver/options.h"

static const char config_file[] = "cdr_sqlite3_custom.conf";

static const char desc[] = "Customizable SQLite3 CDR Backend";
static const char name[] = "cdr_sqlite3_custom";

struct cw_dynstr dbpath = CW_DYNSTR_INIT;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static sqlite3 *db = NULL;

static char *table, *columns_str, *values_str;
static struct cw_dynargs columns = CW_DYNARRAY_INIT;
static struct cw_dynargs values = CW_DYNARRAY_INIT;
static struct cw_dynstr evalbuf = CW_DYNSTR_INIT;

static struct cw_channel *chan;


#define I_INSERT	0
#define I_BEGIN		1
#define I_COMMIT	2
#define I_ROLLBACK	3

const char *cmd[] = {
	[I_INSERT] = NULL,
	[I_BEGIN] = "begin transaction",
	[I_COMMIT] = "commit transaction",
	[I_ROLLBACK] = "rollback transaction",
};

static sqlite3_stmt *sql[arraysize(cmd)];


static void dbclose(void)
{
	int i;

	for (i = 0; i < arraysize(sql); i++) {
		if (sql[i]) {
			sqlite3_finalize(sql[i]);
			sql[i] = NULL;
		}
	}

	if (db) {
		sqlite3_close(db);
		db = NULL;
	}
}


static int dbopen(int force)
{
	static dev_t dev = 0;
	static ino_t ino = 0;
	struct stat st;
	char *sql_cmd;
	int i, res;

	if (!table)
		return -1;

	if (!force && !(res = stat(dbpath.data, &st)) && st.st_dev == dev && st.st_ino == ino)
		return 0;

	dbclose();

	/* is the database there? */
	res = sqlite3_open(dbpath.data, &db);
	if (!db) {
		cw_log(CW_LOG_ERROR, "Out of memory!\n");
		goto err;
	}
	if (res != SQLITE_OK) {
		cw_log(CW_LOG_ERROR, "%s\n", sqlite3_errmsg(db));
		goto err;
	}

	/* is the table there? */
	sql_cmd = sqlite3_mprintf("SELECT COUNT(AcctId) FROM %q;", table);
	res = sqlite3_exec(db, sql_cmd, NULL, NULL, NULL);
	sqlite3_free(sql_cmd);
	if (res != SQLITE_OK) {
		cw_log(CW_LOG_ERROR, "Unable to access table '%s'.\n", table);
		goto err;
	}

	for (i = 0; i < arraysize(sql); i++) {
		if ((res = sqlite3_prepare_v2(db, cmd[i], -1, &sql[i], NULL)) != SQLITE_OK) {
			cw_log(CW_LOG_ERROR, "Error: %s\n", sqlite3_errmsg(db));
			goto err;
		}
	}

	return 0;

err:
	dbclose();
	return -1;
}


static int sql3_log(struct cw_cdr *submission)
{
	struct cw_cdr *batch, *cdrset, *cdr;
	int i, res = 0;

	pthread_mutex_lock(&lock);

	if (!table)
		goto done;

restart:
	if (dbopen(0))
		goto done;

	sqlite3_reset(sql[I_BEGIN]);
	if ((res = sqlite3_step(sql[I_BEGIN])) == SQLITE_BUSY || res == SQLITE_LOCKED) {
		usleep(10);
		goto restart;
	}

	if (res != SQLITE_DONE)
		cw_log(CW_LOG_ERROR, "begin transaction failed: %s\n", sqlite3_errmsg(db));

	batch = submission;
	while ((cdrset = batch)) {
		batch = batch->batch_next;

		while ((cdr = cdrset)) {
			chan->cdr = cdr;

			pbx_substitute_variables(chan, NULL, values_str, &evalbuf);

			if (!evalbuf.error && !cw_split_args(&values, evalbuf.data, ",", '\0', NULL)) {
				cdrset = cdrset->next;

				sqlite3_reset(sql[I_INSERT]);

				for (i = 0; i < values.used; i++)
					sqlite3_bind_text(sql[I_INSERT], i + 1, values.data[i], -1, SQLITE_STATIC);

				res = sqlite3_step(sql[I_INSERT]);

				cw_dynargs_reset(&values);
				cw_dynstr_reset(&evalbuf);

				if (res == SQLITE_DONE)
					continue;
			}

			sqlite3_reset(sql[I_ROLLBACK]);
			while (sqlite3_step(sql[I_ROLLBACK]) == SQLITE_BUSY) {
				usleep(10);
				sqlite3_reset(sql[I_ROLLBACK]);
			}

			if (res != SQLITE_LOCKED) {
				cw_log(CW_LOG_ERROR, "insert failed with error code %d\n", res);
				goto done;
			}

			usleep(10);
			goto restart;
		}
	}

	sqlite3_reset(sql[I_COMMIT]);
	while ((res = sqlite3_step(sql[I_COMMIT])) == SQLITE_BUSY) {
		usleep(10);
		sqlite3_reset(sql[I_COMMIT]);
	}

	if (res != SQLITE_DONE) {
		sqlite3_reset(sql[I_ROLLBACK]);
		while (sqlite3_step(sql[I_ROLLBACK]) == SQLITE_BUSY) {
			usleep(10);
			sqlite3_reset(sql[I_ROLLBACK]);
		}
		goto restart;
	}

done:
	pthread_mutex_unlock(&lock);
	return res;
}


static struct cw_cdrbe cdrbe = {
	.name = name,
	.description = desc,
	.handler = sql3_log,
};


static void release(void)
{
	dbclose();

	free((char *)cmd[I_INSERT]);

	free(values_str);
	cw_dynargs_free(&values);

	free(columns_str);
	cw_dynargs_free(&columns);

	free(table);

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

	free(values_str);
	values_str = NULL;

	free(columns_str);
	columns_str = NULL;
	cw_dynargs_reset(&columns);

	free(table);
	table = NULL;

	if ((cfg = cw_config_load(config_file))) {
		res = 0;

		if (cw_variable_browse(cfg, "master")) {
			/* Previous cdr_sqlite3_custom had the table in "master" but cdr_pgsql_custom
			 * has it in "global". For the sake of compatibility with expectations we
			 * look in both places here.
			 */
			if ((tmp = cw_variable_retrieve(cfg, "master", "table"))
			|| (tmp = cw_variable_retrieve(cfg, "global", "table")))
				table = strdup(tmp);
			else {
				cw_log(CW_LOG_WARNING, "Table name not specified. Assuming cdr.\n");
				table = strdup("cdr");
			}

			if ((tmp = cw_variable_retrieve(cfg, "master", "columns"))) {
				if (!(columns_str = strdup(tmp)) || cw_split_args(&columns, columns_str, ",", '\0', NULL)) {
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

		if (!res) {
			struct cw_dynstr ds = CW_DYNSTR_INIT;
			char *s;
			int i;

			s = sqlite3_mprintf("INSERT INTO %q (%q", table, columns.data[0]);
			cw_dynstr_printf(&ds, "%s", s);
			sqlite3_free(s);

			for (i = 1; i < columns.used; i++) {
				s = sqlite3_mprintf(", %q", columns.data[i]);
				cw_dynstr_printf(&ds, "%s", s);
				sqlite3_free(s);
			}

			cw_dynstr_printf(&ds, ") VALUES (?");

			for (i = 1; i < columns.used; i++)
				cw_dynstr_printf(&ds, ", ?");

			cw_dynstr_printf(&ds, ")");

			res = -1;
			if (!ds.error) {
				cmd[I_INSERT] = cw_dynstr_steal(&ds);

				res = dbopen(1);
			}
		}
	} else
		cw_log(CW_LOG_WARNING, "Failed to load configuration file \"%s\"\n", config_file);

	if (res) {
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
