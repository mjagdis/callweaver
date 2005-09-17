/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 2004 - 2005
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 *
 * OpenPBX Call Manager CDR records.
 * 
 */

#include <sys/types.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/channel.h"
#include "openpbx/cdr.h"
#include "openpbx/module.h"
#include "openpbx/logger.h"
#include "openpbx/utils.h"
#include "openpbx/manager.h"
#include "openpbx/config.h"

#define DATE_FORMAT 	"%Y-%m-%d %T"
#define CONF_FILE	"cdr_manager.conf"

static char *desc = "OpenPBX Call Manager CDR Backend";
static char *name = "cdr_manager";

static int enablecdr = 0;

static void loadconfigurationfile(void)
{
	char *cat;
	struct opbx_config *cfg;
	struct opbx_variable *v;
	
	cfg = opbx_config_load(CONF_FILE);
	if (!cfg) {
		/* Standard configuration */
		enablecdr = 0;
		return;
	}
	
	cat = opbx_category_browse(cfg, NULL);
	while (cat) {
		if (!strcasecmp(cat, "general")) {
			v = opbx_variable_browse(cfg, cat);
			while (v) {
				if (!strcasecmp(v->name, "enabled")) {
					enablecdr = opbx_true(v->value);
				}
				
				v = v->next;
			}
		}
	
		/* Next category */
		cat = opbx_category_browse(cfg, cat);
	}
	
	opbx_config_destroy(cfg);
}

static int manager_log(struct opbx_cdr *cdr)
{
	time_t t;
	struct tm timeresult;
	char strStartTime[80] = "";
	char strAnswerTime[80] = "";
	char strEndTime[80] = "";
	
	if (!enablecdr)
		return 0;

	t = cdr->start.tv_sec;
	localtime_r(&t, &timeresult);
	strftime(strStartTime, sizeof(strStartTime), DATE_FORMAT, &timeresult);
	
	if (cdr->answer.tv_sec)	{
    		t = cdr->answer.tv_sec;
    		localtime_r(&t, &timeresult);
		strftime(strAnswerTime, sizeof(strAnswerTime), DATE_FORMAT, &timeresult);
	}

	t = cdr->end.tv_sec;
	localtime_r(&t, &timeresult);
	strftime(strEndTime, sizeof(strEndTime), DATE_FORMAT, &timeresult);

	manager_event(EVENT_FLAG_CALL, "Cdr",
	    "AccountCode: %s\r\n"
	    "Source: %s\r\n"
	    "Destination: %s\r\n"
	    "DestinationContext: %s\r\n"
	    "CallerID: %s\r\n"
	    "Channel: %s\r\n"
	    "DestinationChannel: %s\r\n"
	    "LastApplication: %s\r\n"
	    "LastData: %s\r\n"
	    "StartTime: %s\r\n"
	    "AnswerTime: %s\r\n"
	    "EndTime: %s\r\n"
	    "Duration: %d\r\n"
	    "BillableSeconds: %d\r\n"
	    "Disposition: %s\r\n"
	    "AMAFlags: %s\r\n"
	    "UniqueID: %s\r\n"
	    "UserField: %s\r\n",
	    cdr->accountcode, cdr->src, cdr->dst, cdr->dcontext, cdr->clid, cdr->channel,
	    cdr->dstchannel, cdr->lastapp, cdr->lastdata, strStartTime, strAnswerTime, strEndTime,
	    cdr->duration, cdr->billsec, opbx_cdr_disp2str(cdr->disposition), 
	    opbx_cdr_flags2str(cdr->amaflags), cdr->uniqueid, cdr->userfield);
	    	
	return 0;
}

char *description(void)
{
	return desc;
}

int unload_module(void)
{
	opbx_cdr_unregister(name);
	return 0;
}

int load_module(void)
{
	int res;

	/* Configuration file */
	loadconfigurationfile();
	
	res = opbx_cdr_register(name, desc, manager_log);
	if (res) {
		opbx_log(LOG_ERROR, "Unable to register OpenPBX Call Manager CDR handling\n");
	}
	
	return res;
}

int reload(void)
{
	loadconfigurationfile();
	return 0;
}

int usecount(void)
{
	return 0;
}

char *key()
{
	return OPENPBX_GPL_KEY;
}
