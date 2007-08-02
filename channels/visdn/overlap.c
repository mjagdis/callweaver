/*
 * vISDN channel driver for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifdef HAVE_CONFIG_H
 #include "confdefs.h"
#endif

//#include <openpbx/astmm.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include <openpbx/lock.h>
#include <openpbx/channel.h>
#include <openpbx/config.h>
#include <openpbx/logger.h>
#include <openpbx/module.h>
#include <openpbx/pbx.h>
#include <openpbx/options.h>
#include <openpbx/cli.h>
#include <openpbx/causes.h>

#include "chan_visdn.h"
#include "overlap.h"
#include "huntgroup.h"
#include "util.h"

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int supports_overlapping(
	struct opbx_channel *chan,
	char *called_number)
{
	char hint[32];

	if (!opbx_get_hint(hint, sizeof(hint), NULL, 0, NULL,
			chan->context, called_number))
		return FALSE;

	char *slashpos = strchr(hint, '/');
	if (!slashpos)
		return FALSE;

	*slashpos = '\0';

	if (strcmp(hint, "VISDN"))
		return FALSE;

	char *intf_name = slashpos + 1;
	int overlap_supported;

	if (!strncasecmp(intf_name, "huntgroup:", 10)) {

		char *hg_name = intf_name + 10;

		struct visdn_huntgroup *hg;
		hg = visdn_hg_get_by_name(hg_name);
		if (!hg) {
			opbx_log(LOG_NOTICE,
				"No huntgroup '%s' in hint\n", hg_name);
			return FALSE;
		}

		if (list_empty(&hg->members)) {
			opbx_log(LOG_WARNING,
				"No interfaces in huntgroup '%s'\n",
				hg->name);

			visdn_hg_put(hg);
			return FALSE;
		}

		struct visdn_huntgroup_member *hgm;
		hgm = list_entry(hg->members.next,
				struct visdn_huntgroup_member, node);

		overlap_supported = hgm->intf->current_ic->overlap_sending;

		visdn_hg_put(hg);
	} else {
		struct visdn_intf *intf = visdn_intf_get_by_name(intf_name);
		if (!intf) {
			opbx_log(LOG_WARNING,
				"No interface '%s' in hint\n", intf_name);
			return FALSE;
		}

		overlap_supported = intf->current_ic->overlap_sending;
		visdn_intf_put(intf);
	}

	return overlap_supported;
}

static int new_digit(
	struct opbx_channel *chan,
	char *called_number,
	int called_number_size,
	char digit, int *retval)
{
	opbx_setstate(chan, OPBX_STATE_DIALING);

	if (digit) {
		if(strlen(called_number) >= called_number_size - 1) {
			opbx_log(LOG_NOTICE,
				"Maximum number of digits exceeded\n");

			chan->hangupcause =
				OPBX_CAUSE_INVALID_NUMBER_FORMAT;
			*retval = 0;
			return TRUE;
		}

		called_number[strlen(called_number)] = digit;
	}

	int sending_complete = FALSE;
	if (!strcmp(chan->tech->type, "VISDN")) {
		struct visdn_chan *visdn_chan = to_visdn_chan(chan);

		sending_complete = visdn_chan->sending_complete;
	}

	if (sending_complete) {
		if (opbx_exists_extension(NULL,
				chan->context,
				called_number, 1,
				chan->cid.cid_num)) {

			opbx_indicate(chan,
				OPBX_CONTROL_PROCEEDING);

			chan->priority = 0;
			strncpy(chan->exten, called_number,
					sizeof(chan->exten));

			assert(!chan->cid.cid_dnid);
			chan->cid.cid_dnid = strdup(called_number);

			*retval = 0;
			return TRUE;
#if 0
		} else if (opbx_canmatch_extension(NULL,
				chan->context,
				"pippo", 1,
//				called_number, 1, // FIXME!!!!!!!!!!!!1
				chan->cid.cid_num)) {

			opbx_indicate(chan,
				OPBX_CONTROL_PROCEEDING);

			chan->priority = 0;
//			strncpy(chan->exten, called_number,
			strncpy(chan->exten, "pippo",
					sizeof(chan->exten));

			*retval = 0;
			return TRUE;
#endif
		} else {
			chan->hangupcause = OPBX_CAUSE_INVALID_NUMBER_FORMAT;
			opbx_indicate(chan, OPBX_CONTROL_DISCONNECT);

			pbx_builtin_setvar_helper(chan, "INVALID_EXTEN",
					called_number);

			chan->priority = 0;
			strncpy(chan->exten, "i", sizeof(chan->exten));

			*retval = 0;
			return TRUE;
		}
	} else {
		if (supports_overlapping(chan, called_number)) {
			chan->priority = 0;
			strncpy(chan->exten, called_number,
					sizeof(chan->exten));

			assert(!chan->cid.cid_dnid);
			chan->cid.cid_dnid = strdup(called_number);

			*retval = 0;
			return TRUE;
		} else {
			if (!opbx_canmatch_extension(NULL,
					chan->context,
					called_number, 1,
					chan->cid.cid_num)) {

				chan->hangupcause =
					OPBX_CAUSE_INVALID_NUMBER_FORMAT;
				opbx_indicate(chan, OPBX_CONTROL_DISCONNECT);

				pbx_builtin_setvar_helper(chan, "INVALID_EXTEN",
						called_number);

				chan->priority = 1;
				strncpy(chan->exten, "i", sizeof(chan->exten));

				*retval = 0;
				return TRUE;
			}

			if (!opbx_matchmore_extension(NULL,
				chan->context,
				called_number, 1,
				chan->cid.cid_num)) {

				opbx_setstate(chan, OPBX_STATE_RING);
				opbx_indicate(chan, OPBX_CONTROL_PROCEEDING);

				chan->priority = 0;
				strncpy(chan->exten, called_number,
						sizeof(chan->exten));

				assert(!chan->cid.cid_dnid);
				chan->cid.cid_dnid = strdup(called_number);

				*retval = 0;
				return TRUE;
			}
		}
	}

	return FALSE;
}

static int visdn_exec_overlap_dial(struct opbx_channel *chan, void *data)
{
	struct localuser *u;
	int retval = -1;
	int do_exit = FALSE;
	LOCAL_USER_ADD(u);

	char called_number[32] = "";

	while(opbx_waitfor(chan, -1) > -1 && !do_exit) {
		struct opbx_frame *f;
		f = opbx_read(chan);
		if (!f)
			break;

		if (f->frametype == OPBX_FRAME_DTMF)
			 do_exit = new_digit(chan, called_number,
					 sizeof(called_number),
					 f->subclass, &retval);

		opbx_fr_free(f);
	}

	LOCAL_USER_REMOVE(u);
	return retval;
}

static char *visdn_overlap_dial_descr =
"  vISDNOverlapDial():\n";

void visdn_overlap_register(void)
{
	opbx_register_application(
		"VISDNOverlapDial",
		visdn_exec_overlap_dial,
		"Plays dialtone and waits for digits",
		visdn_overlap_dial_descr);
}

void visdn_overlap_unregister(void)
{
	opbx_unregister_application("VISDNOverlapDial");
}
