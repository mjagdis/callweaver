/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2004 - 2005, Anthony Minessale II.
 *
 * Anthony Minessale <anthmct@yahoo.com>
 *
 * disclaimed to Digium
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
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
 * \brief Application to dump channel variables
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/options.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/utils.h"

static const char tdesc[] = "Dump Info About The Calling Channel";

static void *dumpchan_app;
static const char dumpchan_name[] = "DumpChan";
static const char dumpchan_synopsis[] = "Dump Info About The Calling Channel";
static const char dumpchan_syntax[] = "DumpChan([min_verbose_level])";
static const char dumpchan_descrip[] = 
"Displays information on channel and listing of all channel\n"
"variables. If min_verbose_level is specified, output is only\n"
"displayed when the verbose level is currently set to that number\n"
"or greater. Always returns 0.\n\n";


static void cw_serialize_showchan(struct cw_channel *c, struct cw_dynstr *ds_p)
{
	long elapsed_seconds=0;
	int hour=0, min=0, sec=0;
	char cgrp[256];
	char pgrp[256];

	if (c->cdr) {
		struct timeval now;

		now = cw_tvnow();
		elapsed_seconds = now.tv_sec - c->cdr->start.tv_sec;
		hour = elapsed_seconds / 3600;
		min = (elapsed_seconds % 3600) / 60;
		sec = elapsed_seconds % 60;
	}

	cw_dynstr_tprintf(ds_p, 22,
		cw_fmtval("Name=               %s\n",        c->name),
		cw_fmtval("Type=               %s\n",        c->type),
		cw_fmtval("UniqueID=           %s\n",        c->uniqueid),
		cw_fmtval("CallerID=           %s\n",        (c->cid.cid_num ? c->cid.cid_num : "(N/A)")),
		cw_fmtval("CallerIDName=       %s\n",        (c->cid.cid_name ? c->cid.cid_name : "(N/A)")),
		cw_fmtval("DNIDDigits=         %s\n",        (c->cid.cid_dnid ? c->cid.cid_dnid : "(N/A)" )),
		cw_fmtval("State=              %s (%d)\n",   cw_state2str(c->_state), c->_state),
		cw_fmtval("Rings=              %d\n",        c->rings),
		cw_fmtval("NativeFormat=       %d\n",        c->nativeformats),
		cw_fmtval("WriteFormat=        %d\n",        c->writeformat),
		cw_fmtval("ReadFormat=         %d\n",        c->readformat),
		cw_fmtval("1stFileDescriptor=  %d\n",        c->fds[0]),
		cw_fmtval("Framesin=           %u %s\n",     c->fin, (cw_test_flag(c, CW_FLAG_DEBUG_IN) ? " (DEBUGGED)" : "")),
		cw_fmtval("Framesout=          %u %s\n",     c->fout, (cw_test_flag(c, CW_FLAG_DEBUG_OUT) ? " (DEBUGGED)" : "")),
		cw_fmtval("TimetoHangup=       %ld\n",       (long)c->whentohangup),
		cw_fmtval("ElapsedTime=        %dh%dm%ds\n", hour, min, sec),
		cw_fmtval("Context=            %s\n",        c->context),
		cw_fmtval("Extension=          %s\n",        c->exten),
		cw_fmtval("Priority=           %d\n",        c->priority),
		cw_fmtval("CallGroup=          %s\n",        cw_print_group(cgrp, sizeof(cgrp), c->callgroup)),
		cw_fmtval("PickupGroup=        %s\n",        cw_print_group(pgrp, sizeof(pgrp), c->pickupgroup)),
		cw_fmtval("Application=        %s\n",        (c->appl ? c->appl : "(N/A)"))
	);

	return;
}

static int dumpchan_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	static const char line[] = "================================================================================";
	struct cw_dynstr ds;
	struct localuser *u;
	int level;

	CW_UNUSED(result);

	level = (argc > 0 ? atoi(argv[0]) : 0);

	if (option_verbose >= level) {
		LOCAL_USER_ADD(u);

		cw_dynstr_init(&ds, 1024, CW_DYNSTR_DEFAULT_CHUNK);
		cw_dynstr_printf(&ds, "\nDumping Info For Channel: %s:\n%s\n", chan->name, line);
		cw_serialize_showchan(chan, &ds);
		pbx_builtin_serialize_variables(chan, &ds);
		cw_dynstr_printf(&ds, "%s\n", line);
		cw_verbose("%s", ds.data);

		LOCAL_USER_REMOVE(u);
		cw_dynstr_free(&ds);
	}

	return 0;
}

static int unload_module(void)
{
	int res = 0;

	res |= cw_unregister_function(dumpchan_app);
	return res;
}

static int load_module(void)
{
	dumpchan_app = cw_register_function(dumpchan_name, dumpchan_exec, dumpchan_synopsis, dumpchan_syntax, dumpchan_descrip);
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
