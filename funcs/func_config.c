/* comments and license {{{
 * vim:ts=4:sw=4:smartindent:cindent:autoindent:foldmethod=marker
 *
 * openpbx -- an open source telephony toolkit.
 *
 * copyright (c) 1999 - 2005, digium, inc.
 *
 * roy sigurd karlsbakk <roy@karlsbakk.net>
 *
 * see http://www.callweaver.org for more information about
 * the openpbx project. please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and irc
 * channels for your use.
 *
 * this program is free software, distributed under the terms of
 * the gnu general public license version 2. see the license file
 * at the top of the source tree.
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

CALLWEAVER_FILE_VERSION("$headurl: svn+ssh://svn@svn.openpbx.org/openpbx/trunk/funcs/func_config.c $", "$revision: 2183 $")

#include "callweaver/module.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/utils.h"
/* }}} */

/* function_config_read() {{{ */
static char *function_config_read(struct opbx_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
/* These doesn't seem to be available outside openpbx.c
	if (strcasecmp(data, "opbxrunuser") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_RUN_USER, len);
	} else if (strcasecmp(data, "opbxrungroup") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_RUN_GROUP, len);
	} else if (strcasecmp(data, "opbxmoddir") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_MOD_DIR, len);
	} else
*/
	if (strcasecmp(data, "opbxctlpermissions") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_CTL_PERMISSIONS, len);
	} else if (strcasecmp(data, "opbxctlowner") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_CTL_OWNER, len);
	} else if (strcasecmp(data, "opbxctlgroup") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_CTL_GROUP, len);
	} else if (strcasecmp(data, "opbxctl") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_CTL, len);
	} else if (strcasecmp(data, "opbxdb") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_DB, len);
	} else if (strcasecmp(data, "opbxetcdir") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_CONFIG_DIR, len);
	} else if (strcasecmp(data, "opbxconfigdir") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_CONFIG_DIR, len);; /* opbxetcdir alias */
	} else if (strcasecmp(data, "opbxspooldir") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_SPOOL_DIR, len);
	} else if (strcasecmp(data, "opbxvarlibdir") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_VAR_DIR, len);
	} else if (strcasecmp(data, "opbxvardir") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_VAR_DIR, len);; /* opbxvarlibdir alias */
	} else if (strcasecmp(data, "opbxdbdir") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_DB_DIR, len);
	} else if (strcasecmp(data, "opbxlogdir") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_LOG_DIR, len);
	} else if (strcasecmp(data, "opbxogidir") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_OGI_DIR, len);
	} else if (strcasecmp(data, "opbxsoundsdir") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_SOUNDS_DIR, len);
	} else if (strcasecmp(data, "opbxrundir") == 0) {
		opbx_copy_string(buf, opbx_config_OPBX_RUN_DIR, len);
	} else {
		opbx_log(LOG_WARNING, "Config setting '%s' not known.\n", data);
	}

	return buf;
}
/* function_config_read() }}} */

/* function_config_write() {{{ */
static void function_config_write(struct opbx_channel *chan, char *cmd, char *data, const char *value) 
{
	opbx_log(LOG_WARNING, "This function cannot be used to change the CallWeaver config. Modify openpbx.conf manually and restart.\n");
}
/* function_config_write() }}} */

/* globals {{{ */
static struct opbx_custom_function config_function = {
	.name = "CONFIG",
	.synopsis = "Read configuration values set in openpbx.conf",
	.syntax = "CONFIG(<name>)",
	.desc = "This function will read configuration values set in openpbx.conf.\n"
			"Possible values include opbxctlpermissions, opbxctlowner, opbxctlgroup,\n"
			"opbxctl, opbxdb, opbxetcdir, opbxconfigdir, opbxspooldir, opbxvarlibdir,\n"
			"opbxvardir, opbxdbdir, opbxlogdir, opbxogidir, opbxsoundsdir, and opbxrundir\n",
	.read = function_config_read,
	.write = function_config_write,
};

static char *tdesc = "CONFIG function";
/* globals }}} */

/* unload_module() {{{ */
int unload_module(void)
{
        return opbx_custom_function_unregister(&config_function);
}
/* }}} */

/* load_module() {{{ */
int load_module(void)
{
        return opbx_custom_function_register(&config_function);
}
/* }}} */

/* description() {{{ */
char *description(void)
{
	return tdesc;
}
/* }}} */

/* usecount() {{{ */
int usecount(void)
{
	return 0;
}
/* }}} */

/* tail {{{

local variables:
mode: c
c-file-style: "linux"
indent-tabs-mode: nil
end:

function_config_read() }}} */
