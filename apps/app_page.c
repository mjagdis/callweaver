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
	struct cw_variable *variables;
};

static void *page_thread(void *data)
{
	struct calloutdata *cd = data;
	cw_pbx_outgoing_app(cd->tech, CW_FORMAT_SLINEAR, cd->resource, 30000,
		"NConference", cd->nconferenceopts, NULL, 0, cd->cidnum, cd->cidname, cd->variables, NULL);
	free(cd);
	return NULL;
}

static void launch_page(struct cw_channel *chan, const char *nconferenceopts, const char *tech, const char *resource)
{
	struct calloutdata *cd;
	const char *varname;
	struct cw_variable *lastvar = NULL;
	struct cw_var_t *varptr;
	pthread_t t;

	cd = malloc(sizeof(struct calloutdata));
	if (cd) {
		memset(cd, 0, sizeof(struct calloutdata));
		cw_copy_string(cd->cidnum, chan->cid.cid_num ? chan->cid.cid_num : "", sizeof(cd->cidnum));
		cw_copy_string(cd->cidname, chan->cid.cid_name ? chan->cid.cid_name : "", sizeof(cd->cidname));
		cw_copy_string(cd->tech, tech, sizeof(cd->tech));
		cw_copy_string(cd->resource, resource, sizeof(cd->resource));
		cw_copy_string(cd->nconferenceopts, nconferenceopts, sizeof(cd->nconferenceopts));

		CW_LIST_TRAVERSE(&chan->varshead, varptr, entries) {
			if (!(varname = cw_var_full_name(varptr)))
				continue;
			if (varname[0] == '_') {
				struct cw_variable *newvar = NULL;

				if (varname[1] == '_') {
					newvar = cw_variable_new(varname, cw_var_value(varptr));
				} else {
					newvar = cw_variable_new(&varname[1], cw_var_value(varptr));
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

		if (cw_pthread_create(&t, &global_attr_detached, page_thread, cd)) {
			cw_log(CW_LOG_WARNING, "Unable to create paging thread: %s\n", strerror(errno));
			free(cd);
		}
	}
}

static int page_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
	struct localuser *u;
	char *tech, *resource;
	char nconferenceopts[80];
	unsigned char flags;
	unsigned int confid = rand();
	int res=0;

	if (argc < 1 || argc > 2)
		return cw_function_syntax(page_syntax);

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
			cw_log(CW_LOG_WARNING, "Incomplete destination '%s' supplied.\n", tech);
		}
	}
	if (!(flags & PAGE_QUIET)) {
		res = cw_streamfile(chan, "beep", chan->language);
		if (!res)
			res = cw_waitstream(chan, "");
	}
	if (!res) {
		snprintf(nconferenceopts, sizeof(nconferenceopts), "%ud/%sq", confid, ((flags & PAGE_DUPLEX) ? "" : "T"));
		cw_function_exec_str(chan, hash_nconference, "NConference", nconferenceopts, NULL, 0);
	}

	LOCAL_USER_REMOVE(u);

	return -1;
}

static int unload_module(void)
{
	int res = 0;

	res |=  cw_unregister_function(page_app);
	return res;
}

static int load_module(void)
{
	hash_nconference = cw_hash_app_name("NConference");

	page_app = cw_register_function(page_name, page_exec, page_synopsis, page_syntax, page_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
