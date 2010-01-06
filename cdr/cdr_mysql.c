/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * MySQL CDR logger 
 * 
 * James Sharp <jsharp@psychoses.org>
 *
 * Modified August 2003
 * Tilghman Lesher <asterisk__cdr__cdr_mysql__200308@the-tilghman.com>
 *
 * Modified August 6, 2005
 * Joseph Benden <joe@thrallingpenguin.com>
 * Added mysql connection timeout parameter
 * Added an automatic reconnect as to not lose a cdr record
 * Cleaned up the original code to match the coding guidelines
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 */

#include <callweaver.h>

#include <sys/types.h>
#include <callweaver/config.h>
#include <callweaver/options.h>
#include <callweaver/channel.h>
#include <callweaver/cdr.h>
#include <callweaver/module.h>
#include <callweaver/logger.h>
#include <callweaver/cli.h>

#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <mysql/mysql.h>
#include <mysql/errmsg.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define CW_MODULE "cdr_mysql"

#define DATE_FORMAT "%Y-%m-%d %T"

static const char desc[] = "MySQL CDR Backend";
static const char name[] = "mysql";
static const char config[] = "cdr_mysql.conf";
static char *dbserver = NULL, *dbname = NULL, *dbuser = NULL, *password = NULL, *dbsock = NULL, *dbtable = NULL;
static int dbserver_alloc = 0, dbname_alloc = 0, dbuser_alloc = 0, password_alloc = 0, dbsock_alloc = 0, dbtable_alloc = 0;
static int dbport = 0;
static int connected = 0;
static time_t connect_time = 0;
static int records = 0;
static int totalrecords = 0;
static int userfield = 0;
static unsigned int timeout = 0;

CW_MUTEX_DEFINE_STATIC(mysql_lock);

static MYSQL mysql;

static const char cdr_mysql_status_help[] =
"Usage: cdr mysql status\n"
"       Shows current connection status for cdr_mysql\n";

static int handle_cdr_mysql_status(struct cw_dynstr **ds_p, int argc, char *argv[])
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (connected) {
		char status[256], status2[100] = "";
		int interval = time(NULL) - connect_time;
		if (dbport)
			snprintf(status, 255, "Connected to %s@%s, port %d", dbname, dbserver, dbport);
		else if (dbsock)
			snprintf(status, 255, "Connected to %s on socket file %s", dbname, dbsock);
		else
			snprintf(status, 255, "Connected to %s@%s", dbname, dbserver);

		if (dbuser && *dbuser)
			snprintf(status2, 99, " with username %s", dbuser);
		if (dbtable && *dbtable)
			snprintf(status2, 99, " using table %s", dbtable);
		if (interval > 31536000) {
			cw_dynstr_printf(ds_p, "%s%s for %d years, %d days, %d hours, %d minutes, %d seconds.\n", status, status2, interval / 31536000, (interval % 31536000) / 86400, (interval % 86400) / 3600, (interval % 3600) / 60, interval % 60);
		} else if (interval > 86400) {
			cw_dynstr_printf(ds_p, "%s%s for %d days, %d hours, %d minutes, %d seconds.\n", status, status2, interval / 86400, (interval % 86400) / 3600, (interval % 3600) / 60, interval % 60);
		} else if (interval > 3600) {
			cw_dynstr_printf(ds_p, "%s%s for %d hours, %d minutes, %d seconds.\n", status, status2, interval / 3600, (interval % 3600) / 60, interval % 60);
		} else if (interval > 60) {
			cw_dynstr_printf(ds_p, "%s%s for %d minutes, %d seconds.\n", status, status2, interval / 60, interval % 60);
		} else {
			cw_dynstr_printf(ds_p, "%s%s for %d seconds.\n", status, status2, interval);
		}
		if (records == totalrecords)
			cw_dynstr_printf(ds_p, "  Wrote %d records since last restart.\n", totalrecords);
		else
			cw_dynstr_printf(ds_p, "  Wrote %d records since last restart and %d records since last reconnect.\n", totalrecords, records);
		return RESULT_SUCCESS;
	} else {
		cw_dynstr_printf(ds_p, "Not currently connected to a MySQL server.\n");
		return RESULT_FAILURE;
	}
}

static struct cw_clicmd cdr_mysql_status_cli = {
	.cmda = { "cdr", "mysql", "status", NULL },
	.handler = handle_cdr_mysql_status,
	.summary = "Show connection status of cdr_mysql",
	.usage = cdr_mysql_status_help,
};

static int mysql_log(struct cw_cdr *batch)
{
	char sqlcmd[2048], timestr[128];
	struct tm tm;
	char *clid=NULL, *dcontext=NULL, *channel=NULL, *dstchannel=NULL, *lastapp=NULL, *lastdata=NULL;
	char *userfielddata = NULL;
#ifdef MYSQL_LOGUNIQUEID
	char *uniqueid = NULL;
#endif
	struct cw_cdr *cdrset, *cdr;
	int retries = 5;

	cw_mutex_lock(&mysql_lock);

	while ((cdrset = batch)) {
		batch = batch->batch_next;

		while ((cdr = cdrset)) {
			cdrset = cdrset->next;

			localtime_r(&cdr->start.tv_sec, &tm);
			strftime(timestr, 128, DATE_FORMAT, &tm);

db_reconnect:
			if ((!connected) && (dbserver || dbsock) && dbuser && password && dbname && dbtable ) {
				/* Attempt to connect */
				mysql_init(&mysql);
				/* Add option to quickly timeout the connection */
				if (timeout && mysql_options(&mysql, MYSQL_OPT_CONNECT_TIMEOUT, (char *)&timeout)!=0) {
					cw_log(CW_LOG_ERROR, "cdr_mysql: mysql_options returned (%u) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
				}
				if (mysql_real_connect(&mysql, dbserver, dbuser, password, dbname, dbport, dbsock, 0)) {
					connected = 1;
					connect_time = time(NULL);
					records = 0;
				} else {
					cw_log(CW_LOG_ERROR, "cdr_mysql: cannot connect to database server %s.\n", dbserver);
					connected = 0;
				}
			} else {
				/* Long connection - ping the server */
				int error;
				if ((error = mysql_ping(&mysql))) {
					connected = 0;
					records = 0;
					switch (error) {
						case CR_SERVER_GONE_ERROR:
						case CR_SERVER_LOST:
							cw_log(CW_LOG_ERROR, "cdr_mysql: Server has gone away. Attempting to reconnect.\n");
							break;
						default:
							cw_log(CW_LOG_ERROR, "cdr_mysql: Unknown connection error: (%u) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
					}
					retries--;
					if (retries)
						goto db_reconnect;
					else
						cw_log(CW_LOG_ERROR, "cdr_mysql: Retried to connect fives times, giving up.\n");
				}
			}

			/* Maximum space needed would be if all characters needed to be escaped, plus a trailing NULL */
			/* WARNING: This code previously used mysql_real_escape_string, but the use of said function
			   requires an active connection to a database.  If we are not connected, then this function
			    cannot be used.  This is a problem since we need to store off the SQL statement into our
			   spool file for later restoration.
			   So the question is, what's the best way to handle this?  This works for now.
			*/
			clid = alloca(strlen(cdr->clid) * 2 + 1);
			mysql_escape_string(clid, cdr->clid, strlen(cdr->clid));
			dcontext = alloca(strlen(cdr->dcontext) * 2 + 1);
			mysql_escape_string(dcontext, cdr->dcontext, strlen(cdr->dcontext));
			channel = alloca(strlen(cdr->channel) * 2 + 1);
			mysql_escape_string(channel, cdr->channel, strlen(cdr->channel));
			dstchannel = alloca(strlen(cdr->dstchannel) * 2 + 1);
			mysql_escape_string(dstchannel, cdr->dstchannel, strlen(cdr->dstchannel));
			lastapp = alloca(strlen(cdr->lastapp) * 2 + 1);
			mysql_escape_string(lastapp, cdr->lastapp, strlen(cdr->lastapp));
			lastdata = alloca(strlen(cdr->lastdata) * 2 + 1);
			mysql_escape_string(lastdata, cdr->lastdata, strlen(cdr->lastdata));
#ifdef MYSQL_LOGUNIQUEID
			uniqueid = alloca(strlen(cdr->uniqueid) * 2 + 1);
			mysql_escape_string(uniqueid, cdr->uniqueid, strlen(cdr->uniqueid));
#endif
			if (userfield) {
				userfielddata = alloca(strlen(cdr->userfield) * 2 + 1);
				mysql_escape_string(userfielddata, cdr->userfield, strlen(cdr->userfield));
			}

			cw_log(CW_LOG_DEBUG, "cdr_mysql: inserting a CDR record.\n");

			if (userfield && userfielddata) {
#ifdef MYSQL_LOGUNIQUEID
				sprintf(sqlcmd, "INSERT INTO %s (calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode,uniqueid,userfield) VALUES ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%i,%i,'%s',%i,'%s','%s','%s')", dbtable, timestr, clid, cdr->src, cdr->dst, dcontext, channel, dstchannel, lastapp, lastdata, cdr->duration, cdr->billsec, cw_cdr_disp2str(cdr->disposition), cdr->amaflags, cdr->accountcode, uniqueid, userfielddata);
#else
				sprintf(sqlcmd, "INSERT INTO %s (calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode,userfield) VALUES ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%i,%i,'%s',%i,'%s','%s')", dbtable, timestr, clid, cdr->src, cdr->dst, dcontext,channel, dstchannel, lastapp, lastdata, cdr->duration, cdr->billsec, cw_cdr_disp2str(cdr->disposition), cdr->amaflags, cdr->accountcode, userfielddata);
#endif
			} else {
#ifdef MYSQL_LOGUNIQUEID
				sprintf(sqlcmd, "INSERT INTO %s (calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode,uniqueid) VALUES ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%i,%i,'%s',%i,'%s','%s')", dbtable, timestr, clid, cdr->src, cdr->dst, dcontext,channel, dstchannel, lastapp, lastdata, cdr->duration, cdr->billsec, cw_cdr_disp2str(cdr->disposition), cdr->amaflags, cdr->accountcode, uniqueid);
#else
				sprintf(sqlcmd, "INSERT INTO %s (calldate,clid,src,dst,dcontext,channel,dstchannel,lastapp,lastdata,duration,billsec,disposition,amaflags,accountcode) VALUES ('%s','%s','%s','%s','%s', '%s','%s','%s','%s',%i,%i,'%s',%i,'%s')", dbtable, timestr, clid, cdr->src, cdr->dst, dcontext, channel, dstchannel, lastapp, lastdata, cdr->duration, cdr->billsec, cw_cdr_disp2str(cdr->disposition), cdr->amaflags, cdr->accountcode);
#endif
			}
	
			cw_log(CW_LOG_DEBUG, "cdr_mysql: SQL command as follows: %s\n", sqlcmd);
	
			if (connected) {
				if (mysql_real_query(&mysql, sqlcmd, strlen(sqlcmd))) {
					cw_log(CW_LOG_ERROR, "mysql_cdr: Failed to insert into database: (%u) %s", mysql_errno(&mysql), mysql_error(&mysql));
					mysql_close(&mysql);
					connected = 0;
				} else {
					records++;
					totalrecords++;
				}
			}
		}
	}

	cw_mutex_unlock(&mysql_lock);
	return 0;
}


static void release(void)
{
	if (connected) {
		mysql_close(&mysql);
		connected = 0;
		records = 0;
	}
	if (dbserver && dbserver_alloc) {
		free(dbserver);
		dbserver = NULL;
		dbserver_alloc = 0;
	}
	if (dbname && dbname_alloc) {
		free(dbname);
		dbname = NULL;
		dbname_alloc = 0;
	}
	if (dbuser && dbuser_alloc) {
		free(dbuser);
		dbuser = NULL;
		dbuser_alloc = 0;
	}
	if (dbsock && dbsock_alloc) {
		free(dbsock);
		dbsock = NULL;
		dbsock_alloc = 0;
	}
	if (dbtable && dbtable_alloc) {
		free(dbtable);
		dbtable = NULL;
		dbtable_alloc = 0;
	}
	if (password && password_alloc) {
		free(password);
		password = NULL;
		password_alloc = 0;
	}
	dbport = 0;
}


static struct cw_cdrbe cdrbe = {
	.name = name,
	.description = desc,
	.handler = mysql_log,
};


static int unload_module(void)
{
	cw_cli_unregister(&cdr_mysql_status_cli);
	cw_cdrbe_unregister(&cdrbe);
	return 0;
}

static int load_module(void)
{
	struct cw_config *cfg;
	struct cw_variable *var;
	const char *tmp;

	cfg = cw_config_load(config);
	if (!cfg) {
		cw_log(CW_LOG_WARNING, "Unable to load config for mysql CDR's: %s\n", config);
		goto error;
	}
	
	var = cw_variable_browse(cfg, "global");
	if (!var) {
		/* nothing configured */
		goto error_release_cfg;
	}

	if (!(tmp = cw_variable_retrieve(cfg, "global", "hostname"))) {
		cw_log(CW_LOG_WARNING, "MySQL server hostname not specified.  Assuming localhost\n");
		tmp = "localhost";
	}
	if (!(dbserver = strdup(tmp)))
		goto error_no_mem_for_dbserver;

	if (!(tmp = cw_variable_retrieve(cfg, "global", "dbname"))) {
		cw_log(CW_LOG_WARNING, "MySQL database not specified.  Assuming callweavercdrdb\n");
		tmp = "callweavercdrdb";
	}
	if (!(dbname = strdup(tmp)))
		goto error_no_mem_for_dbname;

	if (!(tmp = cw_variable_retrieve(cfg, "global", "user"))) {
		cw_log(CW_LOG_WARNING, "MySQL database user not specified.  Assuming root\n");
		tmp = "root";
	}
	if (!(dbuser = strdup(tmp)))
		goto error_no_mem_for_dbuser;

	if (!(tmp = cw_variable_retrieve(cfg, "global", "sock"))) {
		cw_log(CW_LOG_WARNING, "MySQL database sock file not specified.  Using default\n");
		tmp = NULL;
	} else if (!(dbsock = strdup(tmp)))
		goto error_no_mem_for_dbsock;

	if (!(tmp = cw_variable_retrieve(cfg, "global", "table"))) {
		cw_log(CW_LOG_NOTICE, "MySQL database table not specified.  Assuming \"cdr\"\n");
		tmp = "cdr";
	}
	if (!(dbtable = strdup(tmp)))
		goto error_no_mem_for_dbtable;

	if (!(tmp = cw_variable_retrieve(cfg, "global", "password"))) {
		cw_log(CW_LOG_WARNING, "MySQL database password not specified.  Assuming blank\n");
		tmp = "";
	}
	if (!(password = strdup(tmp)))
		goto error_no_mem_for_password;

	if ((tmp = cw_variable_retrieve(cfg, "global", "port"))) {
		if (sscanf(tmp, "%d", &dbport) < 1) {
			cw_log(CW_LOG_WARNING, "Invalid MySQL port number.  Using default\n");
			dbport = 0;
		}
	}

	if ((tmp = cw_variable_retrieve(cfg, "global", "timeout"))) {
		if (sscanf(tmp, "%u", &timeout) < 1) {
			cw_log(CW_LOG_WARNING, "Invalid MySQL timeout number.  Using default\n");
			timeout = 0;
		}
	}
	
	if (!(tmp = cw_variable_retrieve(cfg, "global", "userfield"))) {
		if (sscanf(tmp, "%d", &userfield) < 1) {
			cw_log(CW_LOG_WARNING, "Invalid MySQL configurtation file\n");
			userfield = 0;
		}
	}

	cw_config_destroy(cfg);

	cw_log(CW_LOG_DEBUG, "cdr_mysql: got hostname of %s\n", dbserver);
	cw_log(CW_LOG_DEBUG, "cdr_mysql: got port of %d\n", dbport);
	cw_log(CW_LOG_DEBUG, "cdr_mysql: got a timeout of %u\n", timeout);
	if (dbsock)
		cw_log(CW_LOG_DEBUG, "cdr_mysql: got sock file of %s\n", dbsock);
	cw_log(CW_LOG_DEBUG, "cdr_mysql: got user of %s\n", dbuser);
	cw_log(CW_LOG_DEBUG, "cdr_mysql: got dbname of %s\n", dbname);
	cw_log(CW_LOG_DEBUG, "cdr_mysql: got password of %s\n", password);

	mysql_init(&mysql);

	if (timeout && mysql_options(&mysql, MYSQL_OPT_CONNECT_TIMEOUT, (char *)&timeout)!=0) {
		cw_log(CW_LOG_ERROR, "cdr_mysql: mysql_options returned (%u) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
	}

	if (!mysql_real_connect(&mysql, dbserver, dbuser, password, dbname, dbport, dbsock, 0)) {
		cw_log(CW_LOG_ERROR, "Failed to connect to mysql database %s on %s.\n", dbname, dbserver);
		connected = 0;
		records = 0;
	} else {
		cw_log(CW_LOG_DEBUG, "Successfully connected to MySQL database.\n");
		connected = 1;
		records = 0;
		connect_time = time(NULL);
	}

	cw_cdrbe_register(&cdrbe);
	cw_cli_register(&cdr_mysql_status_cli);

	return 0;

error_no_mem_for_password:
	free(dbtable);
error_no_mem_for_dbtable:
	free(dbsock);
error_no_mem_for_dbsock:
	free(dbuser);
error_no_mem_for_dbuser:
	free(dbname);
error_no_mem_for_dbname:
	free(dbserver);
error_no_mem_for_dbserver:
	cw_log(CW_LOG_ERROR, "Out of memory!\n");
error_release_cfg:
	cw_config_destroy(cfg);
error:
	return -1;
}


MODULE_INFO(load_module, NULL, unload_module, release,
	"MySQL CDR Backend"
)
