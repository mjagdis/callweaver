/*
 * CallWeaver.org -- An open source telephony toolkit.
 *
 * Copyright (C) 2004 - 2005
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver.org project. Please do not directly contact
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
 * \brief CallWeaver.org Call Manager CDR records.
 * 
 * See also
 * \arg \ref OpbxCDR
 * \arg \ref OpbxOMI
 * \arg \ref Config_omi
 * \ingroup cdr_drivers
 */
#include <stdio.h>
#include <sys/types.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/cdr.h"
#include "callweaver/module.h"
#include "callweaver/logger.h"
#include "callweaver/utils.h"
#include "callweaver/manager.h"
#include "callweaver/config.h"

#define DATE_FORMAT 	"%Y-%m-%d %T"
#define CONF_FILE	"cdr_manager.conf"

static const char desc[] = "CallWeaver.org Call Manager CDR Backend";
static const char name[] = "cdr_manager";

static int enablecdr = 0;

static void loadconfigurationfile(void)
{
	char *cat;
	struct cw_config *cfg;
	struct cw_variable *v;
	
	cfg = cw_config_load(CONF_FILE);
	if (!cfg) {
		/* Standard configuration */
		enablecdr = 0;
		return;
	}
	
	cat = cw_category_browse(cfg, NULL);
	while (cat) {
		if (!strcasecmp(cat, "general")) {
			v = cw_variable_browse(cfg, cat);
			while (v) {
				if (!strcasecmp(v->name, "enabled")) {
					enablecdr = cw_true(v->value);
				}
				
				v = v->next;
			}
		}
	
		/* Next category */
		cat = cw_category_browse(cfg, cat);
	}
	
	cw_config_destroy(cfg);
}

static int manager_log(struct cw_cdr *batch)
{
	char strStartTime[80] = "";
	char strAnswerTime[80] = "";
	char strEndTime[80] = "";
	struct tm timeresult;
	time_t t;
	struct cw_cdr *cdrset, *cdr;
	
	if (!enablecdr)
		return 0;

	while ((cdrset = batch)) {
		batch = batch->batch_next;

		while ((cdr = cdrset)) {
			cdrset = cdrset->next;

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

			cw_manager_event(CW_EVENT_FLAG_CALL, "Cdr",
			    18,
			    cw_msg_tuple("AccountCode",        "%s", cdr->accountcode),
			    cw_msg_tuple("Source",             "%s", cdr->src),
			    cw_msg_tuple("Destination",        "%s", cdr->dst),
			    cw_msg_tuple("DestinationContext", "%s", cdr->dcontext),
			    cw_msg_tuple("CallerID",           "%s", cdr->clid),
			    cw_msg_tuple("Channel",            "%s", cdr->channel),
			    cw_msg_tuple("DestinationChannel", "%s", cdr->dstchannel),
			    cw_msg_tuple("LastApplication",    "%s", cdr->lastapp),
			    cw_msg_tuple("LastData",           "%s", cdr->lastdata),
			    cw_msg_tuple("StartTime",          "%s", strStartTime),
			    cw_msg_tuple("AnswerTime",         "%s", strAnswerTime),
			    cw_msg_tuple("EndTime",            "%s", strEndTime),
			    cw_msg_tuple("Duration",           "%d", cdr->duration),
			    cw_msg_tuple("BillableSeconds",    "%d", cdr->billsec),
			    cw_msg_tuple("Disposition",        "%s", cw_cdr_disp2str(cdr->disposition)),
			    cw_msg_tuple("AMAFlags",           "%s", cw_cdr_flags2str(cdr->amaflags)),
			    cw_msg_tuple("UniqueID",           "%s", cdr->uniqueid),
			    cw_msg_tuple("UserField",          "%s", cdr->userfield)
			);
		}
	}

	return 0;
}


static struct cw_cdrbe cdrbe = {
	.name = name,
	.description = desc,
	.handler = manager_log,
};


static int unload_module(void)
{
	cw_cdrbe_unregister(&cdrbe);
	return 0;
}

static int load_module(void)
{
	int res = 0;

	cw_cdrbe_register(&cdrbe);

	/* Configuration file */
	loadconfigurationfile();

	return res;
}

static int reload_module(void)
{
	loadconfigurationfile();
	return 0;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, desc)
