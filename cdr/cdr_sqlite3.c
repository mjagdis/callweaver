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
static struct sqlite3 *db = NULL;

OPBX_MUTEX_DEFINE_STATIC(sqlite3_lock);

/*! \brief SQL table format */
static char sql_create_table[] = "CREATE TABLE cdr ("
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

static int sqlite_log(struct opbx_cdr *cdr)
{
	int res = 0;
	char *zErr = 0;
	struct tm tm;
	time_t t;
	char startstr[80], answerstr[80], endstr[80];
	int count;
	char fn[PATH_MAX];
	char *sql;

	opbx_mutex_lock(&sqlite3_lock);

	/* is the database there? */
	snprintf(fn, sizeof(fn), "%s/cdr.db", opbx_config_OPBX_LOG_DIR);
	sqlite3_open(fn, &db);
	if (!db) {
		opbx_log(OPBX_LOG_ERROR, "cdr_sqlite: %s\n", zErr);
		free(zErr);
		return -1;
	}


	t = cdr->start.tv_sec;
	localtime_r(&t, &tm);
	strftime(startstr, sizeof(startstr), DATE_FORMAT, &tm);

	t = cdr->answer.tv_sec;
	localtime_r(&t, &tm);
	strftime(answerstr, sizeof(answerstr), DATE_FORMAT, &tm);

	t = cdr->end.tv_sec;
	localtime_r(&t, &tm);
	strftime(endstr, sizeof(endstr), DATE_FORMAT, &tm);

	for(count=0; count<5; count++) {
		sql = sqlite3_mprintf(
			"INSERT INTO cdr ("
				"clid,src,dst,dcontext,"
				"channel,dstchannel,lastapp,lastdata, "
				"start,answer,end,"
				"duration,billsec,disposition,amaflags, "
				"accountcode"
#				if LOG_UNIQUEID
				",uniqueid"
#				endif
#				if LOG_USERFIELD
				",userfield"
#				endif
			") VALUES ("
				"'%q', '%q', '%q', '%q', "
				"'%q', '%q', '%q', '%q', "
				"'%q', '%q', '%q', "
				"%d, %d, %d, %d, "
				"'%q'"
#				if LOG_UNIQUEID
				",'%q'"
#				endif
#				if LOG_USERFIELD
				",'%q'"
#				endif
			")",
				cdr->clid, cdr->src, cdr->dst, cdr->dcontext,
				cdr->channel, cdr->dstchannel, cdr->lastapp, cdr->lastdata,
				startstr, answerstr, endstr,
				cdr->duration, cdr->billsec, cdr->disposition, cdr->amaflags,
				cdr->accountcode
#				if LOG_UNIQUEID
				,cdr->uniqueid
#				endif
#				if LOG_USERFIELD
				,cdr->userfield
#				endif
			);
		opbx_log(OPBX_LOG_DEBUG, "CDR SQLITE3 SQL [%s]\n", sql);
		res = sqlite3_exec(db,
						   sql,
						   NULL,
						   NULL,
						   &zErr
						   );

		if (sql) {
		    sqlite3_free(sql);
		    sql = NULL;
		}
		if (res != SQLITE_BUSY && res != SQLITE_LOCKED)
			break;
		usleep(200);
	}

	if (sql) {
		sqlite3_free(sql);
		sql = NULL;
	}
	
	if (zErr) {
		opbx_log(OPBX_LOG_ERROR, "cdr_sqlite: %s\n", zErr);
		free(zErr);
	}

	if (db) sqlite3_close(db);

	opbx_mutex_unlock(&sqlite3_lock);
	return res;
}


static void release(void)
{
	if (db) sqlite3_close(db);
}


static struct opbx_cdrbe cdrbe = {
	.name = name,
	.description = desc,
	.handler = sqlite_log,
};


static int unload_module(void)
{
	opbx_cdrbe_unregister(&cdrbe);
	return 0;
}

static int load_module(void)
{
	char *zErr;
	char fn[PATH_MAX];
	int res;

	/* is the database there? */
	snprintf(fn, sizeof(fn), "%s/cdr.db", opbx_config_OPBX_LOG_DIR);
	sqlite3_open(fn, &db);
	if (!db) {
		opbx_log(OPBX_LOG_ERROR, "cdr_sqlite: %s\n", zErr);
		free(zErr);
		return -1;
	}

	/* is the table there? */
	res = sqlite3_exec(db, "SELECT COUNT(AcctId) FROM cdr;", NULL, NULL, NULL);
	if (res) {
		res = sqlite3_exec(db, sql_create_table, NULL, NULL, &zErr);
		if (res) {
			opbx_log(OPBX_LOG_ERROR, "cdr_sqlite: Unable to create table 'cdr': %s\n", zErr);
			free(zErr);
			goto err;
		}

		/* TODO: here we should probably create an index */
	}
	
	if (db) sqlite3_close(db);

	opbx_cdrbe_register(&cdrbe);

	return 0;

err:
	if (db) sqlite3_close(db);
	return -1;
}


MODULE_INFO(load_module, NULL, unload_module, release, desc)
