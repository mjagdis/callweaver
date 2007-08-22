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
 * \ingroup applications
 *
 *
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

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/options.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/file.h"
#include "callweaver/app.h"
#include "callweaver/chanvars.h"


static const char tdesc[] = "Page Multiple Phones";

static void *page_app;
static const char page_name[] = "Page";
static const char page_synopsis[] = "Pages phones";
static const char page_syntax[] = "Page(Technology/Resource&Technology2/Resource2[, options])";
static const char page_descrip[] =
"  Places outbound calls to the given technology / resource and dumps\n"
"them into a conference bridge as muted participants.  The original\n"
"caller is dumped into the conference as a speaker and the room is\n"
"destroyed when the original caller leaves.  Valid options are:\n"
"        d - full duplex audio\n"
"	 q - quiet, do not play beep to caller\n";

static unsigned int hash_nconference;


enum {
	PAGE_DUPLEX = (1 << 0),
	PAGE_QUIET = (1 << 1),
} page_opt_flags;


struct calloutdata {
	char cidnum[64];
	char cidname[64];
	char tech[64];
	char resource[256];
	char nconferenceopts[64];
	struct opbx_variable *variables;
};

static void *page_thread(void *data)
{
	struct calloutdata *cd = data;
	opbx_pbx_outgoing_app(cd->tech, OPBX_FORMAT_SLINEAR, cd->resource, 30000,
		"NConference", cd->nconferenceopts, NULL, 0, cd->cidnum, cd->cidname, cd->variables, NULL);
	free(cd);
	return NULL;
}

static void launch_page(struct opbx_channel *chan, const char *nconferenceopts, const char *tech, const char *resource)
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
		opbx_copy_string(cd->nconferenceopts, nconferenceopts, sizeof(cd->nconferenceopts));

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

static int page_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct localuser *u;
	char *tech, *resource;
	char nconferenceopts[80];
	unsigned char flags;
	unsigned int confid = rand();
	int res=0;

	if (argc < 1 || argc > 2)
		return opbx_function_syntax(page_syntax);

	LOCAL_USER_ADD(u);

	flags = 0;
	if (argc > 1) {
		for (; *argv[1]; argv[1]++) {
			switch (*argv[1]) {
				case 'd': flags |= PAGE_DUPLEX; break;
				case 'q': flags |= PAGE_QUIET; break;
			}
		}
	}

	snprintf(nconferenceopts, sizeof(nconferenceopts), "%ud/%sq", confid, ((flags & PAGE_DUPLEX) ? "" : "L"));
	while ((tech = strsep(&argv[0], "&"))) {
		if ((resource = strchr(tech, '/'))) {
			*resource++ = '\0';
			launch_page(chan, nconferenceopts, tech, resource);
		} else {
			opbx_log(LOG_WARNING, "Incomplete destination '%s' supplied.\n", tech);
		}
	}
	if (!(flags & PAGE_QUIET)) {
		res = opbx_streamfile(chan, "beep", chan->language);
		if (!res)
			res = opbx_waitstream(chan, "");
	}
	if (!res) {
		snprintf(nconferenceopts, sizeof(nconferenceopts), "%ud/%sq", confid, ((flags & PAGE_DUPLEX) ? "" : "T"));
		opbx_function_exec_str(chan, hash_nconference, "NConference", nconferenceopts, NULL, 0);
	}

	LOCAL_USER_REMOVE(u);

	return -1;
}

static int unload_module(void)
{
	int res = 0;

	res |=  opbx_unregister_function(page_app);
	return res;
}

static int load_module(void)
{
	hash_nconference = opbx_hash_app_name("NConference");

	page_app = opbx_register_function(page_name, page_exec, page_synopsis, page_syntax, page_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
