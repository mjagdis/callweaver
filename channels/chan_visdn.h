/*
 * vISDN channel driver for OpenPBX
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 * Adapted for OpenPBX by < Michael "cypromis" Bielicki <Michal.Bielicki@Halo2.pl>
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <openpbx/channel.h>
#include <openpbx/channel_pvt.h>

#include <libq931/list.h>

struct visdn_suspended_call
{
	struct list_head node;

	struct oppbx_channel *opbx_chan;
	struct q931_channel *q931_chan;

	char call_identity[10];
	int call_identity_len;

	time_t old_when_to_hangup;
};

struct visdn_chan {
	struct opbx_channel *opbx_chan;
	struct q931_call *q931_call;
	struct visdn_suspended_call *suspended_call;

	int pbx_started;

	char visdn_chanid[30];
	int is_voice;
	int channel_fd;

	char calling_number[21];
	char called_number[21];
	int sending_complete;

//	echo_can_state_t *ec;
};
