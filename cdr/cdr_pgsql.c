/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * cdr_pgsql.c <PostgreSQL module for CDR logging>
 * Copyright (C) 2005 Business Technology Group (http://www.btg.co.nz)
 *   Danel Swarbrick <daniel@btg.co.nz>
 *
 * Based in part on original by Matthew D. Hardeman <mhardemn@papersoft.com>
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
 * \brief PostgreSQL CDR logger
 *
 * \author Daniel Swarbrick <daniel@btg.co.nz>
 *
 * See also
 * \arg \ref Config_cdr
 * \arg http://www.postgresql.org/
 */

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <sys/types.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"
#include "callweaver/config.h"
#include "callweaver/pbx.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"

#define DATE_FORMAT "%Y-%m-%d %T"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <libpq-fe.h>

static const char desc[] = "PostgreSQL CDR Backend";
static const char name[] = "pgsql";

OPBX_MUTEX_DEFINE_STATIC(pgsql_lock);
#define CDR_PGSQL_CONF "cdr_pgsql.conf"
static char conninfo[512];
static char table[128];
static PGconn *conn = NULL;

static int parse_config(void);
static int pgsql_reconnect(void);

static int parse_config(void)
{
	struct opbx_config *config;
	char *s;

	config = opbx_config_load(CDR_PGSQL_CONF);

	if (config) {

		/* get the PostgreSQL DSN */
		s = opbx_variable_retrieve(config, "global", "dsn");
		if (s == NULL) {
			opbx_log(OPBX_LOG_WARNING, "cdr_pgsql: No DSN found, using 'dbname=callweaver user=callweaver'.\n");
			strncpy(conninfo, "dbname=callweaver user=callweaver", sizeof(conninfo));
		} else {
			strncpy(conninfo, s, sizeof(conninfo));
		}
		
		/* get the CDR table name */
		s = opbx_variable_retrieve(config, "global", "table");
		if (s == NULL) {
			opbx_log(OPBX_LOG_WARNING, "No database table found, assuming 'cdr'.\n");
			strncpy(table, "cdr", sizeof(table));
		} else {
			strncpy(table, s, sizeof(table));
		}

	} else {
		opbx_log(OPBX_LOG_WARNING, "Config file (%s) not found.\n", CDR_PGSQL_CONF);
	}
	opbx_config_destroy(config);

	return 1;
}

static int pgsql_reconnect(void)
{
	if (conn != NULL) {
		/* we may already be connected */
		if (PQstatus(conn) == CONNECTION_OK) {
			return 1;
		} else {
			opbx_log(OPBX_LOG_NOTICE, "Existing database connection broken. Trying to reset.\n");

			/* try to reset the connection */
			if (PQstatus(conn) != CONNECTION_BAD)
				PQreset(conn);

			/* check the connection status again */
			if (PQstatus(conn) == CONNECTION_OK) {
				opbx_log(OPBX_LOG_NOTICE, "Existing database connection reset ok.\n");
				return 1;
			} else {
				/* still no luck, tear down the connection and we'll make a new connection */
				opbx_log(OPBX_LOG_NOTICE, "Unable to reset existing database connection.\n");
				PQfinish(conn);
			}
		}
	}

	conn = PQconnectdb(conninfo);

	if (PQstatus(conn) == CONNECTION_OK) {
		opbx_log(OPBX_LOG_NOTICE, "Successfully connected to PostgreSQL database.\n");
		return 1;
	} else {
		opbx_log(OPBX_LOG_WARNING, "Couldn't establish DB connection. Check debug.\n");
		opbx_log(OPBX_LOG_ERROR, "Reason %s\n", PQerrorMessage(conn));
	}		

	return -1;
}

static int pgsql_log(struct opbx_cdr *cdr)
{
	PGresult *res;
	struct tm tm;
	char sql[2048] = "";
	char timestr[128];
	char *clid=NULL, *dcontext=NULL, *channel=NULL, *dstchannel=NULL, *lastapp=NULL, *lastdata=NULL;
	char *uniqueid=NULL, *userfield=NULL;

	localtime_r(&cdr->start.tv_sec, &tm);
	strftime(timestr, sizeof(timestr), DATE_FORMAT, &tm);

	/* maximum space needed would be if all characters needed to be escaped, plus a trailing NULL */
	clid = alloca(strlen(cdr->clid) * 2 + 1);
	PQescapeString(clid, cdr->clid, strlen(cdr->clid));
	dcontext = alloca(strlen(cdr->dcontext) * 2 + 1);
	PQescapeString(dcontext, cdr->dcontext, strlen(cdr->dcontext));
	channel = alloca(strlen(cdr->channel) * 2 + 1);
	PQescapeString(channel, cdr->channel, strlen(cdr->channel));
	dstchannel = alloca(strlen(cdr->dstchannel) * 2 + 1);
	PQescapeString(dstchannel, cdr->dstchannel, strlen(cdr->dstchannel));
	lastapp = alloca(strlen(cdr->lastapp) * 2 + 1);
	PQescapeString(lastapp, cdr->lastapp, strlen(cdr->lastapp));
	lastdata = alloca(strlen(cdr->lastdata) * 2 + 1);
	PQescapeString(lastdata, cdr->lastdata, strlen(cdr->lastdata));
	uniqueid = alloca(strlen(cdr->uniqueid) * 2 + 1);
	PQescapeString(uniqueid, cdr->uniqueid, strlen(cdr->uniqueid));
	userfield = alloca(strlen(cdr->userfield) * 2 + 1);
	PQescapeString(userfield, cdr->userfield, strlen(cdr->userfield));

	opbx_log(OPBX_LOG_DEBUG,"Inserting a CDR record.\n");

	snprintf(sql, sizeof(sql), "INSERT INTO %s (calldate,clid,src,dst,dcontext,channel,dstchannel,"
		"lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode,uniqueid,userfield) VALUES"
		" ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%d,%d,'%s',%d,'%s','%s','%s')",
		table, timestr, clid, cdr->src, cdr->dst, dcontext,channel, dstchannel, lastapp, lastdata,
		cdr->duration, cdr->billsec, opbx_cdr_disp2str(cdr->disposition), cdr->amaflags, cdr->accountcode, uniqueid, userfield);

	opbx_log(OPBX_LOG_DEBUG, "SQL command executed:  %s\n", sql);

	/* check if database connection is still good */
	if (!pgsql_reconnect()) {
		opbx_log(OPBX_LOG_ERROR, "Unable to reconnect to database server. Some calls will not be logged!\n");
		return -1;
	}

	opbx_mutex_lock(&pgsql_lock);
	res = PQexec(conn, sql);

	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		opbx_log(OPBX_LOG_ERROR, "Failed to insert call detail record into database!\n");
		opbx_log(OPBX_LOG_ERROR, "Reason: %s\n", PQresultErrorMessage(res));
		PQclear(res);
		opbx_mutex_unlock(&pgsql_lock);
		return -1;
	}

	PQclear(res);
	opbx_mutex_unlock(&pgsql_lock);
	return 0;
}


static struct opbx_cdrbe cdrbe = {
	.name = name,
	.description = desc,
	.handler = pgsql_log,
};


static int load_module(void)
{
	int res = 0;

	opbx_cdrbe_register(&cdrbe);

	parse_config();
	
	pgsql_reconnect();

	return res;
}

static int unload_module(void)
{
	opbx_cdrbe_unregister(&cdrbe);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
