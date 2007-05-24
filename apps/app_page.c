/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (c) 2004 - 2005 Digium, Inc.  All rights reserved.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This code is released under the GNU General Public License
 * version 2.0.  See LICENSE for more information.
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 *
 * \brief page() - Paging application
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \ingroup applications
 */

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: https://mikael@svn.callweaver.org/callweaver/trunk/apps/app_disa.c $", "$Revision: 2820 $")

#include "callweaver/options.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/file.h"
#include "callweaver/app.h"
#include "callweaver/chanvars.h"


static const char *tdesc = "Page Multiple Phones";

static const char *app_page= "Page";

static const char *page_synopsis = "Pages phones";

static const char *page_descrip =
"Page(Technology/Resource&Technology2/Resource2[|options])\n"
"  Places outbound calls to the given technology / resource and dumps\n"
"them into a conference bridge as muted participants.  The original\n"
"caller is dumped into the conference as a speaker and the room is\n"
"destroyed when the original caller leaves.  Valid options are:\n"
"        d - full duplex audio\n"
"	 q - quiet, do not play beep to caller\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

enum {
	PAGE_DUPLEX = (1 << 0),
	PAGE_QUIET = (1 << 1),
} page_opt_flags;

OPBX_APP_OPTIONS(page_opts, {
	OPBX_APP_OPTION('d', PAGE_DUPLEX),
	OPBX_APP_OPTION('q', PAGE_QUIET),
});

struct calloutdata {
	char cidnum[64];
	char cidname[64];
	char tech[64];
	char resource[256];
	char meetmeopts[64];
	struct opbx_variable *variables;
};

static void *page_thread(void *data)
{
	struct calloutdata *cd = data;
	opbx_pbx_outgoing_app(cd->tech, OPBX_FORMAT_SLINEAR, cd->resource, 30000,
		"MeetMe", cd->meetmeopts, NULL, 0, cd->cidnum, cd->cidname, cd->variables, NULL);
	free(cd);
	return NULL;
}

static void launch_page(struct opbx_channel *chan, const char *meetmeopts, const char *tech, const char *resource)
{
	struct calloutdata *cd;
	const char *varname;
	struct opbx_variable *lastvar = NULL;
	struct opbx_var_t *varptr;
	pthread_t t;
	pthread_attr_t attr;
	cd = malloc(sizeof(struct calloutdata));
	if (cd) {
		memset(cd, 0, sizeof(struct calloutdata));
		opbx_copy_string(cd->cidnum, chan->cid.cid_num ? chan->cid.cid_num : "", sizeof(cd->cidnum));
		opbx_copy_string(cd->cidname, chan->cid.cid_name ? chan->cid.cid_name : "", sizeof(cd->cidname));
		opbx_copy_string(cd->tech, tech, sizeof(cd->tech));
		opbx_copy_string(cd->resource, resource, sizeof(cd->resource));
		opbx_copy_string(cd->meetmeopts, meetmeopts, sizeof(cd->meetmeopts));

		OPBX_LIST_TRAVERSE(&chan->varshead, varptr, entries) {
			if (!(varname = opbx_var_full_name(varptr)))
				continue;
			if (varname[0] == '_') {
				struct opbx_variable *newvar = NULL;

				if (varname[1] == '_') {
					newvar = opbx_variable_new(varname, opbx_var_value(varptr));
				} else {
					newvar = opbx_variable_new(&varname[1], opbx_var_value(varptr));
				}

				if (newvar) {
					if (lastvar)
						lastvar->next = newvar;
					else
						cd->variables = newvar;
					lastvar = newvar;
				}
			}
		}

		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (opbx_pthread_create(&t, &attr, page_thread, cd)) {
			opbx_log(LOG_WARNING, "Unable to create paging thread: %s\n", strerror(errno));
			free(cd);
		}
	}
}

static int page_exec(struct opbx_channel *chan, void *data)
{
	struct localuser *u;
	char *options;
	char *tech, *resource;
	char meetmeopts[80];
	struct opbx_flags flags = { 0 };
	unsigned int confid = rand();
	struct opbx_app *app;
	char *tmp;
	int res=0;

	if (opbx_strlen_zero(data)) {
		opbx_log(LOG_WARNING, "This application requires at least one argument (destination(s) to page)\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	if (!(app = pbx_findapp("MeetMe"))) {
		opbx_log(LOG_WARNING, "There is no MeetMe application available!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	};

	options = opbx_strdupa(data);
	if (!options) {
		opbx_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	tmp = strsep(&options, "|");
	if (options)
		opbx_app_parse_options(page_opts, &flags, NULL, options);

	snprintf(meetmeopts, sizeof(meetmeopts), "%ud|%sqxdw", confid, opbx_test_flag(&flags, PAGE_DUPLEX) ? "" : "m");
	while ((tech = strsep(&tmp, "&"))) {
		if ((resource = strchr(tech, '/'))) {
			*resource++ = '\0';
			launch_page(chan, meetmeopts, tech, resource);
		} else {
			opbx_log(LOG_WARNING, "Incomplete destination '%s' supplied.\n", tech);
		}
	}
	if (!opbx_test_flag(&flags, PAGE_QUIET)) {
		res = opbx_streamfile(chan, "beep", chan->language);
		if (!res)
			res = opbx_waitstream(chan, "");
	}
	if (!res) {
		snprintf(meetmeopts, sizeof(meetmeopts), "%ud|A%sqxd", confid, opbx_test_flag(&flags, PAGE_DUPLEX) ? "" : "t");
		pbx_exec(chan, app, meetmeopts, 1);
	}

	LOCAL_USER_REMOVE(u);

	return -1;
}

int unload_module(void)
{
	int res;

	res =  opbx_unregister_application(app_page);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	return opbx_register_application(app_page, page_exec, page_synopsis, page_descrip);
}

char *description(void)
{
	return (char *) tdesc;
}

int usecount(void)
{
	int res;

	STANDARD_USECOUNT(res);

	return res;
}
