/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * cdr_pgsql_custom.c <PostgreSQL module for CDR logging with custom columns>
 * 
 * Copyright (C) 2005 Business Technology Group (http://www.btg.co.nz)
 *  Danel Swarbrick <daniel@btg.co.nz>
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
 * \brief Custom PostgreSQL CDR logger
 *
 * \author Mikael Bjerkeland <mikael@bjerkeland.com>
 * based on original code by Daniel Swarbrick <daniel@btg.co.nz>
 *
 * See also
 * \arg \ref Config_cdr
 * \arg http://www.postgresql.org/
 */

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

static const char desc[] = "Custom PostgreSQL CDR Backend";
static const char name[] = "pgsql_custom";

CW_MUTEX_DEFINE_STATIC(pgsql_lock);
#define CDR_PGSQL_CONF "cdr_pgsql_custom.conf"
static char conninfo[512];
static char table[128];
static char columns[1024];
static char values[1024];

static PGconn *conn = NULL;

static int parse_config(void);
static int pgsql_reconnect(void);

static int parse_config(void)
{
	struct cw_config *config;
	char *s;

	config = cw_config_load(CDR_PGSQL_CONF);

	if (config) {

		/* get the PostgreSQL DSN */
		s = cw_variable_retrieve(config, "global", "dsn");
		if (s == NULL) {
			cw_log(CW_LOG_WARNING, "cdr_pgsql_custom: No DSN found, using 'dbname=callweaver user=callweaver'.\n");
			strncpy(conninfo, "dbname=callweaver user=callweaver", sizeof(conninfo));
		} else {
			strncpy(conninfo, s, sizeof(conninfo));
		}
		
		/* get the CDR table name */
		s = cw_variable_retrieve(config, "global", "table");
		if (s == NULL) {
			cw_log(CW_LOG_WARNING, "No database table found, assuming 'cdr'.\n");
			strncpy(table, "cdr", sizeof(table));
		} else {
			strncpy(table, s, sizeof(table));
		}

		/* get the CDR columns */
                s = cw_variable_retrieve(config, "master", "columns");
                if (s == NULL) {
                        cw_log(CW_LOG_WARNING, "Column names not specified. Module not loaded.\n");
			return -1;
                } else {
                        strncpy(columns, s, sizeof(columns));
                }

		/* get the CDR column values */
                s = cw_variable_retrieve(config, "master", "values");
                if (s == NULL) {
                        cw_log(CW_LOG_WARNING, "Values not specified. Module not loaded.\n");
			return -1;
                } else {
                        strncpy(values, s, sizeof(values));
                }
		
		if (columns != NULL && values != NULL)
			cw_log(CW_LOG_NOTICE, "Using column layout: %s.\n", columns);


	} else {
		cw_log(CW_LOG_WARNING, "Config file (%s) not found.\n", CDR_PGSQL_CONF);
	}
	cw_config_destroy(config);

	return 1;
}

static int pgsql_reconnect(void)
{
	if (conn != NULL) {
		/* we may already be connected */
		if (PQstatus(conn) == CONNECTION_OK) {
			return 1;
		} else {
			cw_log(CW_LOG_NOTICE, "Existing database connection broken. Trying to reset.\n");

			/* try to reset the connection */
			if (PQstatus(conn) != CONNECTION_BAD)
				PQreset(conn);

			/* check the connection status again */
			if (PQstatus(conn) == CONNECTION_OK) {
				cw_log(CW_LOG_NOTICE, "Existing database connection reset ok.\n");
				return 1;
			} else {
				/* still no luck, tear down the connection and we'll make a new connection */
				cw_log(CW_LOG_NOTICE, "Unable to reset existing database connection.\n");
				PQfinish(conn);
			}
		}
	}

	conn = PQconnectdb(conninfo);

	if (PQstatus(conn) == CONNECTION_OK) {
		cw_log(CW_LOG_NOTICE, "Successfully connected to PostgreSQL database.\n");
		return 1;
	} else {
		cw_log(CW_LOG_WARNING, "Couldn't establish DB connection. Check debug.\n");
		cw_log(CW_LOG_ERROR, "Reason %s\n", PQerrorMessage(conn));
	}		

	return -1;
}

static int pgsql_log(struct cw_cdr *batch)
{
	cw_dynstr_t sql_tmp_cmd = CW_DYNSTR_INIT;
	cw_dynstr_t cmd_ds = CW_DYNSTR_INIT;
	struct cw_channel *chan;
	PGresult *res;
	struct cw_cdr *cdrset, *cdr;
	int ret = -1;

	if ((chan = cw_channel_alloc(0, NULL))) {
		cw_mutex_lock(&pgsql_lock);

		while ((cdrset = batch)) {
			batch = batch->batch_next;

			while ((cdr = cdrset)) {
				if (!sql_tmp_cmd.used)
					cw_dynstr_printf(&sql_tmp_cmd, "INSERT INTO %s (%s) VALUES (%s)", table, columns, values);

				if (!sql_tmp_cmd.error) {
					chan->cdr = cdr;
					pbx_substitute_variables(chan, NULL, sql_tmp_cmd.data, &cmd_ds);

					if (!cmd_ds.error) {
						cdrset = cdrset->next;

						cw_log(CW_LOG_DEBUG, "SQL command executed:  %s\n", cmd_ds.data);

						/* check if database connection is still good */
						if (!pgsql_reconnect()) {
							cw_log(CW_LOG_ERROR, "Unable to reconnect to database server. Some calls will not be logged!\n");
							goto out;
						}

						res = PQexec(conn, cmd_ds.data);

						if (PQresultStatus(res) != PGRES_COMMAND_OK) {
							cw_log(CW_LOG_ERROR, "Failed to insert call detail record into database!\n");
							cw_log(CW_LOG_ERROR, "Reason: %s\n", PQresultErrorMessage(res));
							PQclear(res);
							goto out;
						}

						PQclear(res);

						cw_dynstr_reset(&cmd_ds);
						continue;
					}
				}

				cw_dynstr_free(&cmd_ds);
				cw_dynstr_free(&sql_tmp_cmd);
				cw_log(CW_LOG_ERROR, "Out of memory!\n");
				sleep(1);
			}
		}

out:
		cw_mutex_unlock(&pgsql_lock);

		cw_dynstr_free(&cmd_ds);
		cw_dynstr_free(&sql_tmp_cmd);
		cw_channel_free(chan);
		ret = 0;
	}

	return ret;
}


static struct cw_cdrbe cdrbe = {
	.name = name,
	.description = desc,
	.handler = pgsql_log,
};


static int unload_module(void)
{ 
	cw_cdrbe_unregister(&cdrbe);
	return 0;
}

static int load_module(void)
{
	cw_cdrbe_register(&cdrbe);

	parse_config();
	
	pgsql_reconnect();

	return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
