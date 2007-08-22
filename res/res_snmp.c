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

static pthread_t thread = OPBX_PTHREADT_NULL;

int res_snmp_agentx_subagent;
int res_snmp_dont_stop;
int res_snmp_enabled;

/*
static char *app_1 = "URLFetch";
static char *synopsis_1 = "Fetch Data from a URL";
static char *desc_1 = "  URLFetch(<url>)\n"
"load a url that returns opbx_config and set according chanvars\n"
;
*/

static int load_config(void)
{
	struct opbx_variable *var;
	struct opbx_config *cfg;
	char *cat;

	res_snmp_enabled = 0;
	res_snmp_agentx_subagent = 1;
	cfg = opbx_config_load("res_snmp.conf");
	if (!cfg) {
		opbx_log(LOG_WARNING, "Could not load res_snmp.conf\n");
		return 0;
	}
	cat = opbx_category_browse(cfg, NULL);
	while (cat) {
		var = opbx_variable_browse(cfg, cat);

		if (strcasecmp(cat, "general") == 0) {
			while (var) {
				if (strcasecmp(var->name, "subagent") == 0) {
					if (opbx_true(var->value))
						res_snmp_agentx_subagent = 1;
					else if (opbx_false(var->value))
						res_snmp_agentx_subagent = 0;
					else {
						opbx_log(LOG_ERROR, "Value '%s' does not evaluate to true or false.\n", var->value);
						opbx_config_destroy(cfg);
						return 1;
					}
				} else if (strcasecmp(var->name, "enabled") == 0) {
					res_snmp_enabled = opbx_true(var->value);
				} else {
					opbx_log(LOG_ERROR, "Unrecognized variable '%s' in category '%s'\n", var->name, cat);
					opbx_config_destroy(cfg);
					return 1;
				}
				var = var->next;
			}
		} else {
			opbx_log(LOG_ERROR, "Unrecognized category '%s'\n", cat);
			opbx_config_destroy(cfg);
			return 1;
		}

		cat = opbx_category_browse(cfg, cat);
	}
	opbx_config_destroy(cfg);
	return 1;
}

static int load_module(void)
{
	if (!load_config())
		return -1;

	opbx_verbose(VERBOSE_PREFIX_1 "Loading [Sub]Agent Module\n");

	res_snmp_dont_stop = 1;
	if (res_snmp_enabled)
		return opbx_pthread_create(&thread, NULL, agent_thread, NULL);
	return 0;
}

static int unload_module(void)
{
	opbx_verbose(VERBOSE_PREFIX_1 "Unloading [Sub]Agent Module\n");

	res_snmp_dont_stop = 0;
	return ((thread != OPBX_PTHREADT_NULL) ? pthread_join(thread, NULL) : 0);
}

static int reload_module(void)
{
	opbx_verbose(VERBOSE_PREFIX_1 "Reloading [Sub]Agent Module\n");

	res_snmp_dont_stop = 0;
	if (thread != OPBX_PTHREADT_NULL)
		pthread_join(thread, NULL);
	thread = OPBX_PTHREADT_NULL;
	load_config();

	res_snmp_dont_stop = 1;
	if (res_snmp_enabled)
		return opbx_pthread_create(&thread, NULL, agent_thread, NULL);
	return 0;
}


char *description (void)
{
	return (char *)tdesc;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, tdesc)
