/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Eris Associates Limited, UK
 * Copyright (C) 2004 - 2005, Holger Schurig
 *
 * Authors:
 *     Mike Jagdis <mjagdis@eris-associates.co.uk>
 *     Holger Schurig <hs4233@mail.mn-solutions.de>
 *
 *
 * Ideas taken from other cdr_*.c files
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
 * \brief Store CDR records in a SQLite database.
 * 
 * \author Mike Jagdis <mjagdis@eris-associates.co.uk>
 * \author Holger Schurig <hs4233@mail.mn-solutions.de>
 *
 * See also
 * \arg \ref Config_cdr
 * \arg http://www.sqlite.org/
 * 
 * Creates the database and table on-the-fly
 * \ingroup cdr_drivers
 */

/*** MODULEINFO
	<depend>sqlite</depend>
 ***/

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "callweaver/channel.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include <sqlite3.h>

#define LOG_UNIQUEID	0
#define LOG_USERFIELD	0

/* When you change the DATE_FORMAT, be sure to change the CHAR(19) below to something else */
#define DATE_FORMAT "%Y-%m-%d %T"

static const char desc[] = "SQLite CDR Backend";
static const char name[] = "sqlite";


/*! \brief SQL table format */
static const char sql_create_table[] = "CREATE TABLE cdr ("
"	AcctId		INTEGER PRIMARY KEY,"
"	clid		VARCHAR(80),"
"	src		VARCHAR(80),"
"	dst		VARCHAR(80),"
"	dcontext	VARCHAR(80),"
"	channel		VARCHAR(80),"
"	dstchannel	VARCHAR(80),"
"	lastapp		VARCHAR(80),"
"	lastdata	VARCHAR(80),"
"	start		CHAR(19),"
"	answer		CHAR(19),"
"	end		CHAR(19),"
"	duration	INTEGER,"
"	billsec		INTEGER,"
"	disposition	INTEGER,"
"	amaflags	INTEGER,"
"	accountcode	VARCHAR(20)"
#if LOG_UNIQUEID
"	,uniqueid	VARCHAR(32)"
#endif
#if LOG_USERFIELD
"	,userfield	VARCHAR(255)"
#endif
");";


static const char sql_insert_cmd[] =
	"INSERT INTO cdr ("
		"clid, src, dst, dcontext,"
		"channel, dstchannel, lastapp, lastdata, "
		"start, answer, end,"
		"duration, billsec, disposition, amaflags, "
		"accountcode"
#if LOG_UNIQUEID
		",uniqueid"
#endif
#if LOG_USERFIELD
		",userfield"
#endif
	") VALUES ("
		"?, ?, ?, ?,"
		"?, ?, ?, ?,"
		"?, ?, ?,"
		"?, ?, ?, ?, "
		"?"
#if LOG_UNIQUEID
		",?"
#endif
#if LOG_USERFIELD
		",?"
#endif
	");";


static char dbpath[PATH_MAX];

static pthread_mutex_t sqlite3_lock = PTHREAD_MUTEX_INITIALIZER;

static struct sqlite3 *db = NULL;

#define I_INSERT	0
#define I_BEGIN		1
#define I_COMMIT	2
#define I_ROLLBACK	3
static sqlite3_stmt *sql[I_ROLLBACK + 1];
static struct {
	const char *text;
	size_t len;
} cmd[] = {
	[I_INSERT] = { sql_insert_cmd, sizeof(sql_insert_cmd) },
	[I_BEGIN] = { "begin transaction;", sizeof("begin transaction;") },
	[I_COMMIT] = { "commit transaction;", sizeof("commit transaction;") },
	[I_ROLLBACK] = { "rollback transaction;", sizeof("rollback transaction;") },
};


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
	static dev_t dev;
	static ino_t ino;
	struct stat st;
	char *zErr;
	const char *tail;
	int i, res;

	if (!force && !(res = stat(dbpath, &st)) && st.st_dev == dev && st.st_ino == ino)
		return 0;

	dbclose();

	res = sqlite3_open(dbpath, &db);
	if (!db) {
		cw_log(CW_LOG_ERROR, "Out of memory!\n");
		goto err;
	}
	if (res != SQLITE_OK) {
		cw_log(CW_LOG_ERROR, "%s\n", sqlite3_errmsg(db));
		goto err;
	}

	/* is the table there? */
	zErr = NULL;
	if (sqlite3_exec(db, "SELECT COUNT(AcctId) FROM cdr;", NULL, NULL, NULL) != SQLITE_OK) {
		if (sqlite3_exec(db, sql_create_table, NULL, NULL, &zErr) != SQLITE_OK) {
			cw_log(CW_LOG_ERROR, "cdr_sqlite: Unable to create table 'cdr': %s\n", zErr);
			sqlite3_free(zErr);
			goto err;
		}

		/* TODO: here we should probably create an index */
	}

	for (i = 0; i < arraysize(sql); i++) {
		if ((res = sqlite3_prepare_v2(db, cmd[i].text, cmd[i].len, &sql[i], &tail)) != SQLITE_OK) {
			cw_log(CW_LOG_ERROR, "sqlite3_prepare_v2 \"%6.6s...\" failed with error code %d\n", cmd[i].text, res);
			goto err;
		}
	}

	return 0;

err:
	dbclose();
	return -1;
}


static int sqlite_log(struct cw_cdr *submission)
{
	char startstr[80], answerstr[80], endstr[80];
	struct tm tm;
	struct cw_cdr *batch, *cdrset, *cdr;
	int startlen, answerlen, endlen;
	int res;

	pthread_mutex_lock(&sqlite3_lock);

restart:
	if (dbopen(0))
		goto done;

	sqlite3_reset(sql[I_BEGIN]);
	if ((res = sqlite3_step(sql[I_BEGIN])) == SQLITE_BUSY || res == SQLITE_LOCKED) {
		usleep(10);
		goto restart;
	}

	if (res != SQLITE_DONE)
		cw_log(CW_LOG_ERROR, "begin transaction failed with error code %d\n", res);

	batch = submission;
	while ((cdrset = batch)) {
		batch = batch->batch_next;

		while ((cdr = cdrset)) {
			cdrset = cdrset->next;

			localtime_r(&cdr->start.tv_sec, &tm);
			startlen = strftime(startstr, sizeof(startstr), DATE_FORMAT, &tm);

			localtime_r(&cdr->answer.tv_sec, &tm);
			answerlen = strftime(answerstr, sizeof(answerstr), DATE_FORMAT, &tm);

			localtime_r(&cdr->end.tv_sec, &tm);
			endlen = strftime(endstr, sizeof(endstr), DATE_FORMAT, &tm);

			sqlite3_reset(sql[I_INSERT]);
			sqlite3_bind_text(sql[I_INSERT], 1, cdr->clid, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql[I_INSERT], 2, cdr->src, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql[I_INSERT], 3, cdr->dst, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql[I_INSERT], 4, cdr->dcontext, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql[I_INSERT], 5, cdr->channel, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql[I_INSERT], 6, cdr->dstchannel, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql[I_INSERT], 7, cdr->lastapp, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql[I_INSERT], 8, cdr->lastdata, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql[I_INSERT], 9, startstr, startlen, SQLITE_STATIC);
			sqlite3_bind_text(sql[I_INSERT], 10, answerstr, answerlen, SQLITE_STATIC);
			sqlite3_bind_text(sql[I_INSERT], 11, endstr, endlen, SQLITE_STATIC);
			sqlite3_bind_int(sql[I_INSERT], 12, cdr->duration);
			sqlite3_bind_int(sql[I_INSERT], 13, cdr->billsec);
			sqlite3_bind_int(sql[I_INSERT], 14, cdr->disposition);
			sqlite3_bind_int(sql[I_INSERT], 15, cdr->amaflags);
			sqlite3_bind_text(sql[I_INSERT], 16, cdr->accountcode, -1, SQLITE_STATIC);
#if LOG_UNIQUEID
			sqlite3_bind_text(sql[I_INSERT], 17, cdr->uniqueid, -1, SQLITE_STATIC);
#endif
#if LOG_USERFIELD
			sqlite3_bind_text(sql[I_INSERT], 18, cdr->userfield, -1, SQLITE_STATIC);
#endif

			if ((res = sqlite3_step(sql[I_INSERT])) == SQLITE_DONE)
				continue;

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

	if (res != SQLITE_DONE)
		cw_log(CW_LOG_ERROR, "commit transaction failed with error code %d\n", res);

done:
	pthread_mutex_unlock(&sqlite3_lock);
	return 0;
}


static struct cw_cdrbe cdrbe = {
	.name = name,
	.description = desc,
	.handler = sqlite_log,
};


static int unload_module(void)
{
	cw_cdrbe_unregister(&cdrbe);
	return 0;
}


static int reconfig_module(void)
{
	int res;

	pthread_mutex_lock(&sqlite3_lock);

	/* FIXME: this should be coming from a conf file */
	snprintf(dbpath, sizeof(dbpath), "%s/cdr.db", cw_config_CW_LOG_DIR);

	if ((res = dbopen(1)))
		dbclose();

	pthread_mutex_unlock(&sqlite3_lock);

	return res;
}


static int load_module(void)
{
	if (!reconfig_module()) {
		cw_cdrbe_register(&cdrbe);
		return 0;
	}

	return -1;
}


MODULE_INFO(load_module, reconfig_module, unload_module, dbclose, desc)
