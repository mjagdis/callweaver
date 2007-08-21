/* comments and license {{{
 * vim:ts=4:sw=4:smartindent:cindent:autoindent:foldmethod=marker
 *
 * CallWeaver -- an open source telephony toolkit.
 *
 * Copyright (c) 1999 - 2005, digium, inc.
 *
 * Roy Sigurd Karlsbakk <roy@karlsbakk.net>
 *
 * See http://www.callweaver.org for more information about
 * the callweaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and irc
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the gnu general public license version 2. See the license file
 * at the top of the source tree.
 *
 * }}} */

/* includes and so on {{{ */
/*! \roy
 *
 * \brief functions for reading global configuration data
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/utils.h"
/* }}} */

/* globals {{{ */
static void *config_function;
static const char config_func_name[] = "CONFIG";
static const char config_func_synopsis[] = "Read configuration values set in callweaver.conf";
static const char config_func_syntax[] = "CONFIG(name)";
static const char config_func_desc[] = "This function will read configuration values set in callweaver.conf.\n"
			"Possible values include cwctlpermissions, cwctlowner, cwctlgroup,\n"
			"cwctl, cwdb, cwetcdir, cwconfigdir, cwspooldir, cwvarlibdir,\n"
			"cwvardir, cwdbdir, cwlogdir, cwogidir, cwsoundsdir, and cwrundir\n";
/* }}} */

/* function_config_read() {{{ */
static int function_config_rw(struct opbx_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	static struct {
		char *key, *value;
	} keytab[] = {
#if 0
		/* These doesn't seem to be available outside callweaver.c */
		{ "cwrunuser", opbx_config_OPBX_RUN_USER },
		{ "cwrungroup", opbx_config_OPBX_RUN_GROUP },
		{ "cwmoddir", opbx_config_OPBX_MOD_DIR },
#endif
		{ "cwctlpermissions", opbx_config_OPBX_CTL_PERMISSIONS },
		{ "cwctlowner", opbx_config_OPBX_CTL_OWNER },
		{ "cwctlgroup", opbx_config_OPBX_CTL_GROUP },
		{ "cwctl", opbx_config_OPBX_CTL },
		{ "cwdb", opbx_config_OPBX_DB },
		{ "cwetcdir", opbx_config_OPBX_CONFIG_DIR },
		{ "cwconfigdir", opbx_config_OPBX_CONFIG_DIR },
		{ "cwspooldir", opbx_config_OPBX_SPOOL_DIR },
		{ "cwvarlibdir", opbx_config_OPBX_VAR_DIR },
		{ "cwvardir", opbx_config_OPBX_VAR_DIR },
		{ "cwdbdir", opbx_config_OPBX_DB_DIR },
		{ "cwlogdir", opbx_config_OPBX_LOG_DIR },
		{ "cwogidir", opbx_config_OPBX_OGI_DIR },
		{ "cwsoundsdir", opbx_config_OPBX_SOUNDS_DIR },
		{ "cwrundir", opbx_config_OPBX_RUN_DIR },
		{ "systemname", opbx_config_OPBX_SYSTEM_NAME },
		{ "enableunsafeunload", opbx_config_OPBX_ENABLE_UNSAFE_UNLOAD },
	};
	int i;

	if (buf) {
		for (i = 0; i < arraysize(keytab); i++) {
			if (!strcasecmp(keytab[i].key, argv[0])) {
				opbx_copy_string(buf, keytab[i].value, len);
				return 0;
			}
		}
		opbx_log(LOG_ERROR, "Config setting '%s' not known.\n", argv[0]);
		return -1;
	}

	opbx_log(LOG_ERROR, "This function currently cannot be used to change the CallWeaver config. Modify callweaver.conf manually and restart.\n");
	return -1;
}
/* function_config_read() }}} */

/* globals {{{ */
static const char tdesc[] = "CONFIG function";
/* globals }}} */

/* unload_module() {{{ */
static int unload_module(void)
{
        return opbx_unregister_function(config_function);
}
/* }}} */

/* load_module() {{{ */
static int load_module(void)
{
        config_function = opbx_register_function(config_func_name, function_config_rw, config_func_synopsis, config_func_syntax, config_func_desc);
	return 0;
}
/* }}} */


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)


/* tail {{{

local variables:
mode: c
c-file-style: "linux"
indent-tabs-mode: nil
end:

function_config_read() }}} */
