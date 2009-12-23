/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2004 - 2005, Holger Schurig
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


static const char sql_insert[] =
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


pthread_mutex_t sqlite3_lock = PTHREAD_MUTEX_INITIALIZER;

static struct sqlite3 *db = NULL;
static sqlite3_stmt *sql;


static int sqlite_log(struct cw_cdr *batch)
{
	char startstr[80], answerstr[80], endstr[80];
	struct tm tm;
	time_t t;
	struct cw_cdr *cdrset, *cdr;
	int startlen, answerlen, endlen;
	int res;

	if (!db)
		return;

	pthread_mutex_lock(&sqlite3_lock);

	while ((cdrset = batch)) {
		batch = batch->batch_next;

		while ((cdr = cdrset)) {
			cdrset = cdrset->next;

			t = cdr->start.tv_sec;
			localtime_r(&t, &tm);
			startlen = strftime(startstr, sizeof(startstr), DATE_FORMAT, &tm);

			t = cdr->answer.tv_sec;
			localtime_r(&t, &tm);
			answerlen = strftime(answerstr, sizeof(answerstr), DATE_FORMAT, &tm);

			t = cdr->end.tv_sec;
			localtime_r(&t, &tm);
			endlen = strftime(endstr, sizeof(endstr), DATE_FORMAT, &tm);

			sqlite3_reset(sql);
			sqlite3_bind_text(sql, 1, cdr->clid, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql, 2, cdr->src, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql, 3, cdr->dst, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql, 4, cdr->dcontext, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql, 5, cdr->channel, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql, 6, cdr->dstchannel, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql, 7, cdr->lastapp, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql, 8, cdr->lastdata, -1, SQLITE_STATIC);
			sqlite3_bind_text(sql, 9, startstr, startlen, SQLITE_STATIC);
			sqlite3_bind_text(sql, 10, answerstr, answerlen, SQLITE_STATIC);
			sqlite3_bind_text(sql, 11, endstr, endlen, SQLITE_STATIC);
			sqlite3_bind_int(sql, 12, cdr->duration);
			sqlite3_bind_int(sql, 13, cdr->billsec);
			sqlite3_bind_int(sql, 14, cdr->disposition);
			sqlite3_bind_int(sql, 15, cdr->amaflags);
			sqlite3_bind_text(sql, 16, cdr->accountcode, -1, SQLITE_STATIC);
#if LOG_UNIQUEID
			sqlite3_bind_text(sql, 17, cdr->uniqueid, -1, SQLITE_STATIC);
#endif
#if LOG_USERFIELD
			sqlite3_bind_text(sql, 18, cdr->userfield, -1, SQLITE_STATIC);
#endif

			while ((res = sqlite3_step(sql)) == SQLITE_BUSY || res == SQLITE_LOCKED)
				usleep(10);

			if (res != SQLITE_DONE)
				cw_log(CW_LOG_ERROR, "sqlite3_step failed with error code %d\n", res);
		}
	}

	pthread_mutex_unlock(&sqlite3_lock);
	return 0;
}


static struct cw_cdrbe cdrbe = {
	.name = name,
	.description = desc,
	.handler = sqlite_log,
};


static void release(void)
{
	if (sql)
		sqlite3_finalize(sql);

	if (db)
		sqlite3_close(db);
}


static int unload_module(void)
{
	cw_cdrbe_unregister(&cdrbe);
	return 0;
}


static int reconfig_module(void)
{
	char fn[PATH_MAX];
	char *zErr;
	const char *tail;
	int res;

	pthread_mutex_lock(&sqlite3_lock);

	if (sql)
		sqlite3_finalize(sql);
	if (db)
		sqlite3_close(db);

	snprintf(fn, sizeof(fn), "%s/cdr.db", cw_config_CW_LOG_DIR);

	res = sqlite3_open(fn, &db);
	if (!db) {
		cw_log(CW_LOG_ERROR, "Out of memory!\n");
		return -1;
	} else if (res != SQLITE_OK) {
		cw_log(CW_LOG_ERROR, "%s\n", sqlite3_errmsg(db));
		goto err;
	}

	zErr = NULL;

	/* is the table there? */
	if (sqlite3_exec(db, "SELECT COUNT(AcctId) FROM cdr;", NULL, NULL, NULL) != SQLITE_OK) {
		if (sqlite3_exec(db, sql_create_table, NULL, NULL, &zErr) != SQLITE_OK) {
			cw_log(CW_LOG_ERROR, "cdr_sqlite: Unable to create table 'cdr': %s\n", zErr);
			sqlite3_free(zErr);
			goto err;
		}

		/* TODO: here we should probably create an index */
	}

	if ((res = sqlite3_prepare_v2(db, sql_insert, sizeof(sql_insert), &sql, &tail)) != SQLITE_OK) {
		cw_log(CW_LOG_ERROR, "sqlite3_prepare_v2 failed with error code %d\n", res);
		goto err;
	}

	pthread_mutex_unlock(&sqlite3_lock);
	return 0;

err:
	sqlite3_close(db);
	db = NULL;
	pthread_mutex_unlock(&sqlite3_lock);
	return -1;
}


static int load_module(void)
{
	if (!reconfig_module()) {
		cw_cdrbe_register(&cdrbe);
		return 0;
	}

	return -1;
}


MODULE_INFO(load_module, reconfig_module, unload_module, release, desc)
