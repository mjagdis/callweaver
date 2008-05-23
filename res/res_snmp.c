/*
 * Copyright (C) 2006 Voop as
 * Thorsten Lockert <tholo@voop.as>
 *
 * Ported to CallWeaver by Roy Sigurd Karlsbakk <roy@karlsbakk.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/channel.h"
#include "callweaver/module.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"

#include "snmp/agent.h"


static const char tdesc[] = "SNMP [Sub]Agent for Callweaver";

static pthread_t thread = CW_PTHREADT_NULL;

int res_snmp_agentx_subagent;
int res_snmp_dont_stop;
int res_snmp_enabled;

/*
static char *app_1 = "URLFetch";
static char *synopsis_1 = "Fetch Data from a URL";
static char *desc_1 = "  URLFetch(<url>)\n"
"load a url that returns cw_config and set according chanvars\n"
;
*/

static int load_config(void)
{
	struct cw_variable *var;
	struct cw_config *cfg;
	char *cat;

	res_snmp_enabled = 0;
	res_snmp_agentx_subagent = 1;
	cfg = cw_config_load("res_snmp.conf");
	if (!cfg) {
		cw_log(CW_LOG_WARNING, "Could not load res_snmp.conf\n");
		return 0;
	}
	cat = cw_category_browse(cfg, NULL);
	while (cat) {
		var = cw_variable_browse(cfg, cat);

		if (strcasecmp(cat, "general") == 0) {
			while (var) {
				if (strcasecmp(var->name, "subagent") == 0) {
					if (cw_true(var->value))
						res_snmp_agentx_subagent = 1;
					else if (cw_false(var->value))
						res_snmp_agentx_subagent = 0;
					else {
						cw_log(CW_LOG_ERROR, "Value '%s' does not evaluate to true or false.\n", var->value);
						cw_config_destroy(cfg);
						return 1;
					}
				} else if (strcasecmp(var->name, "enabled") == 0) {
					res_snmp_enabled = cw_true(var->value);
				} else {
					cw_log(CW_LOG_ERROR, "Unrecognized variable '%s' in category '%s'\n", var->name, cat);
					cw_config_destroy(cfg);
					return 1;
				}
				var = var->next;
			}
		} else {
			cw_log(CW_LOG_ERROR, "Unrecognized category '%s'\n", cat);
			cw_config_destroy(cfg);
			return 1;
		}

		cat = cw_category_browse(cfg, cat);
	}
	cw_config_destroy(cfg);
	return 1;
}

static int load_module(void)
{
	if (!load_config())
		return -1;

	res_snmp_dont_stop = 1;
	if (res_snmp_enabled)
		return cw_pthread_create(&thread, &global_attr_default, agent_thread, NULL);
	return 0;
}

static int unload_module(void)
{
	res_snmp_dont_stop = 0;
	return (!pthread_equal(thread, CW_PTHREADT_NULL) ? pthread_join(thread, NULL) : 0);
}

static int reload_module(void)
{
	res_snmp_dont_stop = 0;
	if (!pthread_equal(thread, CW_PTHREADT_NULL))
		pthread_join(thread, NULL);
	thread = CW_PTHREADT_NULL;
	load_config();

	res_snmp_dont_stop = 1;
	if (res_snmp_enabled)
		return cw_pthread_create(&thread, &global_attr_default, agent_thread, NULL);
	return 0;
}

MODULE_INFO(load_module, reload_module, unload_module, NULL, tdesc)
