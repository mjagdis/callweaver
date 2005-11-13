/*
 * vISDN channel driver for OpenPBX
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 * Adopted for OpenPBX by Michael "cypromis" Bielicki <michal.bielicki@halo2.pl>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <signal.h>
#include <ctype.h>

#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <net/if_arp.h>

#include <linux/rtc.h>

#include <openpbx/lock.h>
#include <openpbx/channel.h>
#include <openpbx/channel_pvt.h>
#include <openpbx/config.h>
#include <openpbx/logger.h>
#include <openpbx/module.h>
#include <openpbx/pbx.h>
#include <openpbx/options.h>
#include <openpbx/utils.h>
#include <openpbx/phone_no_utils.h>
#include <openpbx/indications.h>
#include <openpbx/cli.h>
#include <openpbx/musiconhold.h>

#include <streamport.h>
#include <lapd.h>
#include <libq931/q931.h>

#include <visdn/visdn.h>

#include "chan_visdn.h"
//#include "echo.h"

#include "confdefs.h"

#define FRAME_SIZE 160

#define assert(cond)							\
	do {								\
		if (!(cond)) {						\
			opbx_log(LOG_ERROR,				\
				"assertion (" #cond ") failed\n");	\
			abort();					\
		}							\
	} while(0)

OPBX_MUTEX_DEFINE_STATIC(usecnt_lock);

// static char context[OPBX_MAX_EXTENSION] = "default";

static pthread_t visdn_q931_thread = OPBX_PTHREADT_NULL;

#define VISDN_DESCRIPTION "VISDN Channel For OpenPBX"
#define VISDN_CHAN_TYPE "VISDN"
#define VISDN_CONFIG_FILE "visdn.conf"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

enum poll_info_type
{
	POLL_INFO_TYPE_INTERFACE,
	POLL_INFO_TYPE_DLC,
	POLL_INFO_TYPE_NETLINK,
};

struct poll_info
{
	enum poll_info_type type;
	union
	{
		struct q931_interface *interface;
		struct q931_dlc *dlc;
	};
};

enum visdn_type_of_number
{
	VISDN_TYPE_OF_NUMBER_UNKNOWN,
	VISDN_TYPE_OF_NUMBER_INTERNATIONAL,
	VISDN_TYPE_OF_NUMBER_NATIONAL,
	VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC,
	VISDN_TYPE_OF_NUMBER_SUBSCRIBER,
	VISDN_TYPE_OF_NUMBER_ABBREVIATED,
};

struct visdn_interface
{
	struct list_head ifs_node;

	char name[IFNAMSIZ];

	int configured;
	int open_pending;

	enum q931_interface_network_role network_role;
	enum visdn_type_of_number type_of_number;
	enum visdn_type_of_number local_type_of_number;
	int tones_option;
	char context[OPBX_MAX_EXTENSION];
	char default_inbound_caller_id[128];
	int force_inbound_caller_id;
	int overlap_sending;
	int overlap_receiving;
	char national_prefix[10];
	char international_prefix[10];
	int dlc_autorelease_time;

	int T301;
	int T302;
	int T303;
	int T304;
	int T305;
	int T306;
	int T307;
	int T308;
	int T309;
	int T310;
	int T312;
	int T313;
	int T314;
	int T316;
	int T317;
	int T318;
	int T319;
	int T320;
	int T321;
	int T322;

	struct list_head suspended_calls;

	struct q931_interface *q931_intf;
};

struct visdn_state
{
	opbx_mutex_t lock;

	struct q931_lib *libq931;

	int have_to_exit;

	struct list_head ifs;

	struct pollfd polls[100];
	struct poll_info poll_infos[100];
	int npolls;

	int open_pending;
	int open_pending_nextcheck;

	int usecnt;
	int timer_fd;
	int control_fd;
	int netlink_socket;

	int debug;
	int debug_q931;
	int debug_q921;

	struct visdn_interface default_intf;
} visdn = {
	.usecnt = 0,
	.timer_fd = -1,
	.control_fd = -1,
#ifdef DEBUG_DEFAULTS
	.debug = TRUE,
	.debug_q921 = FALSE,
	.debug_q931 = TRUE,
#else
	.debug = FALSE,
	.debug_q921 = FALSE,
	.debug_q931 = FALSE,
#endif

	.default_intf = {
		.network_role = Q931_INTF_NET_PRIVATE,
		.type_of_number = VISDN_TYPE_OF_NUMBER_UNKNOWN,
		.local_type_of_number = VISDN_TYPE_OF_NUMBER_UNKNOWN,
		.tones_option = TRUE,
		.context = "visdn",
		.default_inbound_caller_id = "",
		.force_inbound_caller_id = FALSE,
		.overlap_sending = TRUE,
		.overlap_receiving = FALSE,
		.national_prefix = "0",
		.international_prefix = "00",
		.dlc_autorelease_time = 10,
		.T307 = 180,
	}
};

#ifdef DEBUG_CODE
#define visdn_debug(format, arg...)			\
	if (visdn.debug)				\
		opbx_log(LOG_NOTICE,			\
			format,				\
			## arg)
#else
#define visdn_debug(format, arg...)		\
	do {} while(0);
#endif

static void visdn_set_socket_debug(int on)
{
	struct visdn_interface *intf;
	list_for_each_entry(intf, &visdn.ifs, ifs_node) {
		if (!intf->q931_intf)
			continue;

		if (intf->q931_intf->role == LAPD_ROLE_NT) {
			setsockopt(intf->q931_intf->master_socket,
				SOL_SOCKET, SO_DEBUG,
				&on, sizeof(on));
		} else {
			setsockopt(intf->q931_intf->dlc.socket,
				SOL_SOCKET, SO_DEBUG,
				&on, sizeof(on));
		}

		struct q931_dlc *dlc;
		list_for_each_entry(dlc, &intf->q931_intf->dlcs, intf_node) {
			setsockopt(dlc->socket,
				SOL_SOCKET, SO_DEBUG,
				&on, sizeof(on));
		}
	}

}

static int do_debug_visdn_generic(int fd, int argc, char *argv[])
{
	opbx_mutex_lock(&visdn.lock);
	visdn.debug = TRUE;
	opbx_mutex_unlock(&visdn.lock);

	opbx_cli(fd, "vISDN debugging enabled\n");

	return 0;
}

static int do_no_debug_visdn_generic(int fd, int argc, char *argv[])
{
	opbx_mutex_lock(&visdn.lock);
	visdn.debug = FALSE;
	opbx_mutex_unlock(&visdn.lock);

	opbx_cli(fd, "vISDN debugging disabled\n");

	return 0;
}

static int do_debug_visdn_q921(int fd, int argc, char *argv[])
{
	// Enable debugging on new DLCs FIXME TODO

	opbx_mutex_lock(&visdn.lock);
	visdn.debug_q921 = TRUE;
	visdn_set_socket_debug(1);
	opbx_mutex_unlock(&visdn.lock);

	opbx_cli(fd, "vISDN q.921 debugging enabled\n");

	return 0;
}

static int do_no_debug_visdn_q921(int fd, int argc, char *argv[])
{
	// Disable debugging on new DLCs FIXME TODO

	opbx_mutex_lock(&visdn.lock);
	visdn.debug_q921 = FALSE;
	visdn_set_socket_debug(0);
	opbx_mutex_unlock(&visdn.lock);

	opbx_cli(fd, "vISDN q.921 debugging disabled\n");

	return 0;
}

static int do_debug_visdn_q931(int fd, int argc, char *argv[])
{
	opbx_mutex_lock(&visdn.lock);
	visdn.debug_q931 = TRUE;
	opbx_mutex_unlock(&visdn.lock);

	opbx_cli(fd, "vISDN q.931 debugging enabled\n");

	return 0;
}

static int do_no_debug_visdn_q931(int fd, int argc, char *argv[])
{
	opbx_mutex_lock(&visdn.lock);
	visdn.debug_q931 = FALSE;
	opbx_mutex_unlock(&visdn.lock);

	opbx_cli(fd, "vISDN q.931 debugging disabled\n");

	return 0;
}

static const char *visdn_interface_network_role_to_string(
	enum q931_interface_network_role value)
{
	switch(value) {
	case Q931_INTF_NET_USER:
		return "User";
	case Q931_INTF_NET_PRIVATE:
		return "Private Network";
	case Q931_INTF_NET_LOCAL:
		return "Local Network";
	case Q931_INTF_NET_TRANSIT:
		return "Transit Network";
	case Q931_INTF_NET_INTERNATIONAL:
		return "International Network";
	}
}

static const char *visdn_type_of_number_to_string(enum visdn_type_of_number type_of_number)
{
	switch(type_of_number) {
	case VISDN_TYPE_OF_NUMBER_UNKNOWN:
		return "unknown";
	case VISDN_TYPE_OF_NUMBER_INTERNATIONAL:
		return "international";
	case VISDN_TYPE_OF_NUMBER_NATIONAL:
		return "national";
	case VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC:
		return "network specific";
	case VISDN_TYPE_OF_NUMBER_SUBSCRIBER:
		return "subscriber";
	case VISDN_TYPE_OF_NUMBER_ABBREVIATED:
		return "private";
	default:
		return "INVALID!";
	}
}

static int do_show_visdn_interfaces(int fd, int argc, char *argv[])
{
	opbx_mutex_lock(&visdn.lock);

	struct visdn_interface *intf;
	list_for_each_entry(intf, &visdn.ifs, ifs_node) {

		opbx_cli(fd, "\n------ Interface %s ---------\n", intf->name);

		opbx_cli(fd, "Role                      : %s\n",
				intf->q931_intf ?
					(intf->q931_intf->role == LAPD_ROLE_NT ?
						"NT" : "TE") :
					"UNUSED!");

		opbx_cli(fd,
			"Network role              : %s\n"
			"Type of number            : %s\n"
			"Local type of number      : %s\n"
			"Tones option              : %s\n"
			"Context                   : %s\n"
			"Default inbound caller ID : %s\n"
			"Force inbound caller ID   : %s\n"
			"Overlap Sending           : %s\n"
			"Overlap Receiving         : %s\n"
			"National prefix           : %s\n"
			"International prefix      : %s\n"
			"Autorelease time          : %d\n",
			visdn_interface_network_role_to_string(
				intf->network_role),
			visdn_type_of_number_to_string(
				intf->type_of_number),
			visdn_type_of_number_to_string(
				intf->local_type_of_number),
			intf->tones_option ? "Yes" : "No",
			intf->context,
			intf->default_inbound_caller_id,
			intf->force_inbound_caller_id ? "Yes" : "No",
			intf->overlap_sending ? "Yes" : "No",
			intf->overlap_receiving ? "Yes" : "No",
			intf->national_prefix,
			intf->international_prefix,
			intf->dlc_autorelease_time);

		if (intf->q931_intf) {
			if (intf->q931_intf->role == LAPD_ROLE_NT) {
				opbx_cli(fd, "DLCs                      : ");

				struct q931_dlc *dlc;
				list_for_each_entry(dlc, &intf->q931_intf->dlcs,
						intf_node) {
					opbx_cli(fd, "%d ", dlc->tei);

				}

				opbx_cli(fd, "\n");

#define TIMER_CONFIG(timer)					\
	opbx_cli(fd, #timer ": %lld%s\n",			\
		intf->q931_intf->timer / 1000000LL,		\
		intf->timer ? " (Non-default)" : "");


				TIMER_CONFIG(T301);
				TIMER_CONFIG(T301);
				TIMER_CONFIG(T302);
				TIMER_CONFIG(T303);
				TIMER_CONFIG(T304);
				TIMER_CONFIG(T305);
				TIMER_CONFIG(T306);
				opbx_cli(fd, "T307: %d\n", intf->T307);
				TIMER_CONFIG(T308);
				TIMER_CONFIG(T309);
				TIMER_CONFIG(T310);
				TIMER_CONFIG(T312);
				TIMER_CONFIG(T314);
				TIMER_CONFIG(T316);
				TIMER_CONFIG(T317);
				TIMER_CONFIG(T320);
				TIMER_CONFIG(T321);
				TIMER_CONFIG(T322);
			} else {
				TIMER_CONFIG(T301);
				TIMER_CONFIG(T302);
				TIMER_CONFIG(T303);
				TIMER_CONFIG(T304);
				TIMER_CONFIG(T305);
				TIMER_CONFIG(T306);
				opbx_cli(fd, "T307: %d\n", intf->T307);
				TIMER_CONFIG(T308);
				TIMER_CONFIG(T309);
				TIMER_CONFIG(T310);
				TIMER_CONFIG(T312);
				TIMER_CONFIG(T313);
				TIMER_CONFIG(T314);
				TIMER_CONFIG(T316);
				TIMER_CONFIG(T317);
				TIMER_CONFIG(T318);
				TIMER_CONFIG(T319);
				TIMER_CONFIG(T320);
				TIMER_CONFIG(T321);
				TIMER_CONFIG(T322);
			}

		} else {
			opbx_cli(fd,
				"T301: %d\n"
				"T302: %d\n"
				"T303: %d\n"
				"T304: %d\n"
				"T305: %d\n"
				"T306: %d\n"
				"T307: %d\n"
				"T308: %d\n"
				"T309: %d\n"
				"T310: %d\n"
				"T312: %d\n"
				"T313: %d\n"
				"T314: %d\n"
				"T316: %d\n"
				"T317: %d\n"
				"T318: %d\n"
				"T319: %d\n"
				"T320: %d\n"
				"T321: %d\n"
				"T322: %d\n",
				intf->T301,
				intf->T302,
				intf->T303,
				intf->T304,
				intf->T305,
				intf->T306,
				intf->T307,
				intf->T308,
				intf->T309,
				intf->T310,
				intf->T312,
				intf->T313,
				intf->T314,
				intf->T316,
				intf->T317,
				intf->T318,
				intf->T319,
				intf->T320,
				intf->T321,
				intf->T322);
		}

		opbx_cli(fd, "Parked calls:\n");
		struct visdn_suspended_call *suspended_call;
		list_for_each_entry(suspended_call, &intf->suspended_calls, node) {

			char sane_str[10];
			char hex_str[20];
			int i;
			for(i=0;
			    i<sizeof(sane_str) &&
				i<suspended_call->call_identity_len;
			    i++) {
				sane_str[i] =
					 isprint(suspended_call->call_identity[i]) ?
						suspended_call->call_identity[i] : '.';

				snprintf(hex_str + (i*2),
					sizeof(hex_str)-(i*2),
					"%02x ",
					suspended_call->call_identity[i]);
			}
			sane_str[i] = '\0';
			hex_str[i*2] = '\0';

			opbx_cli(fd, "    %s (%s)\n",
				sane_str,
				hex_str);
		}
	}

	opbx_mutex_unlock(&visdn.lock);

	return 0;
}

static enum visdn_type_of_number
		visdn_string_to_type_of_number(const char *str)
{
	 enum visdn_type_of_number type_of_number =
				VISDN_TYPE_OF_NUMBER_UNKNOWN;

	if (!strcasecmp(str, "unknown"))
		type_of_number = VISDN_TYPE_OF_NUMBER_UNKNOWN;
	else if (!strcasecmp(str, "international"))
		type_of_number = VISDN_TYPE_OF_NUMBER_INTERNATIONAL;
	else if (!strcasecmp(str, "national"))
		type_of_number = VISDN_TYPE_OF_NUMBER_NATIONAL;
	else if (!strcasecmp(str, "network_specific"))
		type_of_number = VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC;
	else if (!strcasecmp(str, "subscriber"))
		type_of_number = VISDN_TYPE_OF_NUMBER_SUBSCRIBER;
	else if (!strcasecmp(str, "abbreviated"))
		type_of_number = VISDN_TYPE_OF_NUMBER_ABBREVIATED;
	else {
		opbx_log(LOG_ERROR,
			"Unknown type_of_number '%s'\n",
			str);
	}

	return type_of_number;
}

static enum q931_interface_network_role
	visdn_string_to_network_role(const char *str)
{
	enum q931_interface_network_role role = 0;

	if (!strcasecmp(str, "user"))
		role = Q931_INTF_NET_USER;
	else if (!strcasecmp(str, "private"))
		role = Q931_INTF_NET_PRIVATE;
	else if (!strcasecmp(str, "local"))
		role = Q931_INTF_NET_LOCAL;
	else if (!strcasecmp(str, "transit"))
		role = Q931_INTF_NET_TRANSIT;
	else if (!strcasecmp(str, "international"))
		role = Q931_INTF_NET_INTERNATIONAL;
	else {
		opbx_log(LOG_ERROR,
			"Unknown network_role '%s'\n",
			str);
	}

	return role;
}

static int visdn_intf_from_var(
	struct visdn_interface *intf,
	struct opbx_variable *var)
{
	if (!strcasecmp(var->name, "network_role")) {
		intf->network_role =
			visdn_string_to_network_role(var->value);
	} else if (!strcasecmp(var->name, "type_of_number")) {
		intf->type_of_number =
			visdn_string_to_type_of_number(var->value);
	} else if (!strcasecmp(var->name, "local_type_of_number")) {
		intf->local_type_of_number =
			visdn_string_to_type_of_number(var->value);
	} else if (!strcasecmp(var->name, "tones_option")) {
		intf->tones_option = opbx_true(var->value);
	} else if (!strcasecmp(var->name, "context")) {
		strncpy(intf->context, var->value,
			sizeof(intf->context));
	} else if (!strcasecmp(var->name, "default_inbound_caller_id")) {
		strncpy(intf->default_inbound_caller_id, var->value,
			sizeof(intf->default_inbound_caller_id));
	} else if (!strcasecmp(var->name, "force_inbound_caller_id")) {
		intf->force_inbound_caller_id = opbx_true(var->value);
	} else if (!strcasecmp(var->name, "overlap_sending")) {
		intf->overlap_sending = opbx_true(var->value);
	} else if (!strcasecmp(var->name, "overlap_receiving")) {
		intf->overlap_receiving = opbx_true(var->value);
	} else if (!strcasecmp(var->name, "national_prefix")) {
		strncpy(intf->national_prefix, var->value,
			sizeof(intf->national_prefix));
	} else if (!strcasecmp(var->name, "international_prefix")) {
		strncpy(intf->international_prefix, var->value,
			sizeof(intf->international_prefix));
	} else if (!strcasecmp(var->name, "autorelease_dlc")) {
		intf->dlc_autorelease_time = atoi(var->value);
	} else if (!strcasecmp(var->name, "t301")) {
		intf->T301 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t302")) {
		intf->T302 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t303")) {
		intf->T303 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t304")) {
		intf->T304 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t305")) {
		intf->T305 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t306")) {
		intf->T306 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t307")) {
		intf->T307 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t308")) {
		intf->T308 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t309")) {
		intf->T309 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t310")) {
		intf->T310 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t312")) {
		intf->T312 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t313")) {
		intf->T313 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t314")) {
		intf->T314 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t316")) {
		intf->T316 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t317")) {
		intf->T317 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t318")) {
		intf->T318 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t319")) {
		intf->T319 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t320")) {
		intf->T320 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t321")) {
		intf->T321 = atoi(var->value);
	} else if (!strcasecmp(var->name, "t322")) {
		intf->T322 = atoi(var->value);
	} else {
		return -1;
	}

	return 0;
}

static void visdn_copy_interface_config(
	struct visdn_interface *dst,
	const struct visdn_interface *src)
{
	dst->network_role = src->network_role;
	dst->type_of_number = src->type_of_number;
	dst->local_type_of_number = src->local_type_of_number;
	dst->tones_option = src->tones_option;
	strncpy(dst->context, src->context,
		sizeof(dst->context));
	strncpy(dst->default_inbound_caller_id, src->default_inbound_caller_id,
		sizeof(dst->default_inbound_caller_id));
	dst->force_inbound_caller_id = src->force_inbound_caller_id;
	dst->overlap_sending = src->overlap_sending;
	dst->overlap_receiving = src->overlap_receiving;
	strncpy(dst->national_prefix, src->national_prefix,
		sizeof(dst->national_prefix));
	strncpy(dst->international_prefix, src->international_prefix,
		sizeof(dst->international_prefix));
	dst->dlc_autorelease_time = src->dlc_autorelease_time;
	dst->T301 = src->T301;
	dst->T302 = src->T302;
	dst->T303 = src->T303;
	dst->T304 = src->T304;
	dst->T305 = src->T305;
	dst->T306 = src->T306;
	dst->T307 = src->T307;
	dst->T308 = src->T308;
	dst->T309 = src->T309;
	dst->T310 = src->T310;
	dst->T312 = src->T312;
	dst->T313 = src->T313;
	dst->T314 = src->T314;
	dst->T316 = src->T316;
	dst->T317 = src->T317;
	dst->T318 = src->T318;
	dst->T319 = src->T319;
	dst->T320 = src->T320;
	dst->T321 = src->T321;
	dst->T322 = src->T322;
}

static void visdn_reload_config(void)
{
	struct opbx_config *cfg;
	cfg = opbx_load(VISDN_CONFIG_FILE);
	if (!cfg) {
		opbx_log(LOG_WARNING,
			"Unable to load config %s, VISDN disabled\n",
			VISDN_CONFIG_FILE);

		return;
	}

	opbx_mutex_lock(&visdn.lock);

	struct opbx_variable *var;
	var = opbx_variable_browse(cfg, "global");
	while (var) {
		if (visdn_intf_from_var(&visdn.default_intf, var) < 0) {
			opbx_log(LOG_WARNING,
				"Unknown configuration variable %s\n",
				var->name);
		}

		var = var->next;
	}

	struct visdn_interface *intf;
	list_for_each_entry(intf, &visdn.ifs, ifs_node) {
		intf->configured = FALSE;
	}

	const char *cat;
	for (cat = opbx_category_browse(cfg, NULL); cat;
	     cat = opbx_category_browse(cfg, (char *)cat)) {

		if (!strcasecmp(cat, "general") ||
		    !strcasecmp(cat, "global"))
			continue;

		int found = FALSE;
		list_for_each_entry(intf, &visdn.ifs, ifs_node) {
			if (!strcasecmp(intf->name, cat)) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			intf = malloc(sizeof(*intf));

			INIT_LIST_HEAD(&intf->suspended_calls);
			intf->q931_intf = NULL;
			intf->open_pending = FALSE;
			strncpy(intf->name, cat, sizeof(intf->name));
			visdn_copy_interface_config(intf, &visdn.default_intf);

			list_add_tail(&intf->ifs_node, &visdn.ifs);
		}

		intf->configured = TRUE;

		var = opbx_variable_browse(cfg, (char *)cat);
		while (var) {
			if (visdn_intf_from_var(intf, var) < 0) {
				opbx_log(LOG_WARNING,
					"Unknown configuration variable %s\n",
					var->name);
			}

			var = var->next;
		}
	}

	opbx_mutex_unlock(&visdn.lock);

	opbx_destroy(cfg);
}

static int do_visdn_reload(int fd, int argc, char *argv[])
{
	visdn_reload_config();

	return 0;
}

static int do_show_visdn_channels(int fd, int argc, char *argv[])
{
	opbx_mutex_lock(&visdn.lock);

	struct visdn_interface *intf;
	list_for_each_entry(intf, &visdn.ifs, ifs_node) {

		if (!intf->q931_intf)
			continue;

		opbx_cli(fd, "Interface: %s\n", intf->name);

		int j;
		for (j=0; j<intf->q931_intf->n_channels; j++) {
			opbx_cli(fd, "  B%d: %s\n",
				intf->q931_intf->channels[j].id + 1,
				q931_channel_state_to_text(
					intf->q931_intf->channels[j].state));
		}
	}

	opbx_mutex_unlock(&visdn.lock);

	return 0;
}

static int visdn_cli_print_call_list(
	int fd,
	struct q931_interface *filter_intf)
{
	int first_call;

	opbx_mutex_lock(&visdn.lock);

	struct visdn_interface *intf;
	list_for_each_entry(intf, &visdn.ifs, ifs_node) {

		if (!intf->q931_intf)
			continue;

		struct q931_call *call;
		first_call = TRUE;

		list_for_each_entry(call, &intf->q931_intf->calls, calls_node) {

			if (!intf || call->intf == filter_intf) {

				if (first_call) {
					opbx_cli(fd, "Interface: %s\n", intf->q931_intf->name);
					opbx_cli(fd, "  Ref#    Caller       Called       State\n");
					first_call = FALSE;
				}

				opbx_cli(fd, "  %c %5ld %s\n",
					(call->direction ==
						Q931_CALL_DIRECTION_INBOUND)
							? 'I' : 'O',
					call->call_reference,
					q931_call_state_to_text(call->state));

/*				opbx_cli(fd, "  %c %5ld %-12s %-12s %s\n",
					(call->direction == Q931_CALL_DIRECTION_INBOUND)
						? 'I' : 'O',
					call->call_reference,
					call->calling_number,
					call->called_number,
					q931_call_state_to_text(call->state));
*/
			}
		}
	}

	opbx_mutex_unlock(&visdn.lock);

	return RESULT_SUCCESS;
}

static void visdn_cli_print_call(int fd, struct q931_call *call)
{
	opbx_cli(fd, "--------- Call %ld %s (%d refs)\n",
		call->call_reference,
		call->direction == Q931_CALL_DIRECTION_INBOUND ?
			"inbound" : "outbound",
		call->refcnt);

	opbx_cli(fd, "Interface       : %s\n", call->intf->name);

	if (call->dlc)
		opbx_cli(fd, "DLC (TEI)       : %d\n", call->dlc->tei);

	opbx_cli(fd, "State           : %s\n",
		q931_call_state_to_text(call->state));

//	opbx_cli(fd, "Calling Number  : %s\n", call->calling_number);
//	opbx_cli(fd, "Called Number   : %s\n", call->called_number);

//	opbx_cli(fd, "Sending complete: %s\n",
//		call->sending_complete ? "Yes" : "No");

	opbx_cli(fd, "Broadcast seutp : %s\n",
		call->broadcast_setup ? "Yes" : "No");

	opbx_cli(fd, "Tones option    : %s\n",
		call->tones_option ? "Yes" : "No");

	opbx_cli(fd, "Active timers   : ");

	if (call->T301.pending) opbx_cli(fd, "T301 ");
	if (call->T302.pending) opbx_cli(fd, "T302 ");
	if (call->T303.pending) opbx_cli(fd, "T303 ");
	if (call->T304.pending) opbx_cli(fd, "T304 ");
	if (call->T305.pending) opbx_cli(fd, "T305 ");
	if (call->T306.pending) opbx_cli(fd, "T306 ");
	if (call->T308.pending) opbx_cli(fd, "T308 ");
	if (call->T309.pending) opbx_cli(fd, "T309 ");
	if (call->T310.pending) opbx_cli(fd, "T310 ");
	if (call->T312.pending) opbx_cli(fd, "T312 ");
	if (call->T313.pending) opbx_cli(fd, "T313 ");
	if (call->T314.pending) opbx_cli(fd, "T314 ");
	if (call->T316.pending) opbx_cli(fd, "T316 ");
	if (call->T318.pending) opbx_cli(fd, "T318 ");
	if (call->T319.pending) opbx_cli(fd, "T319 ");
	if (call->T320.pending) opbx_cli(fd, "T320 ");
	if (call->T321.pending) opbx_cli(fd, "T321 ");
	if (call->T322.pending) opbx_cli(fd, "T322 ");

	opbx_cli(fd, "\n");

	opbx_cli(fd, "CES:\n");
	struct q931_ces *ces;
	list_for_each_entry(ces, &call->ces, node) {
		opbx_cli(fd, "%d %s %s ",
			ces->dlc->tei,
			q931_ces_state_to_text(ces->state),
			(ces == call->selected_ces ? "presel" :
			  (ces == call->preselected_ces ? "sel" : "")));

		if (ces->T304.pending) opbx_cli(fd, "T304 ");
		if (ces->T308.pending) opbx_cli(fd, "T308 ");
		if (ces->T322.pending) opbx_cli(fd, "T322 ");

		opbx_cli(fd, "\n");
	}

}

static int do_show_visdn_calls(int fd, int argc, char *argv[])
{
	opbx_mutex_lock(&visdn.lock);

	if (argc == 3) {
		visdn_cli_print_call_list(fd, NULL);
	} else if (argc == 4) {
		char *callpos = strchr(argv[3], '/');
		if (callpos) {
			*callpos = '\0';
			callpos++;
		}

		struct visdn_interface *filter_intf = NULL;
		struct visdn_interface *intf;
		list_for_each_entry(intf, &visdn.ifs, ifs_node) {
			if (intf->q931_intf &&
			    !strcasecmp(intf->name, argv[3])) {
				filter_intf = intf;
				break;
			}
		}

		if (!filter_intf) {
			opbx_cli(fd, "Interface not found\n");
			goto err_intf_not_found;
		}

		if (!callpos) {
			visdn_cli_print_call_list(fd, filter_intf->q931_intf);
		} else {
			struct q931_call *call;

			if (callpos[0] == 'i' || callpos[0] == 'I') {
				call = q931_get_call_by_reference(
							filter_intf->q931_intf,
					Q931_CALL_DIRECTION_INBOUND,
					atoi(callpos + 1));
			} else if (callpos[0] == 'o' || callpos[0] == 'O') {
				call = q931_get_call_by_reference(
							filter_intf->q931_intf,
					Q931_CALL_DIRECTION_OUTBOUND,
					atoi(callpos + 1));
			} else {
				opbx_cli(fd, "Invalid call reference\n");
				goto err_unknown_direction;
			}

			if (!call) {
				opbx_cli(fd, "Call %s not found\n", callpos);
				goto err_call_not_found;
			}

			visdn_cli_print_call(fd, call);

			q931_call_put(call);
		}
	}

err_call_not_found:
err_unknown_direction:
err_intf_not_found:

	opbx_mutex_unlock(&visdn.lock);

	return RESULT_SUCCESS;
}

static char debug_visdn_generic_help[] =
	"Usage: debug visdn generic\n"
	"	Debug generic vISDN events\n";

static struct opbx_cli_entry debug_visdn_generic =
{
        { "debug", "visdn", "generic", NULL },
	do_debug_visdn_generic,
	"Enables generic vISDN debugging",
	debug_visdn_generic_help,
	NULL
};

static struct opbx_cli_entry no_debug_visdn_generic =
{
        { "no", "debug", "visdn", "generic", NULL },
	do_no_debug_visdn_generic,
	"Disables generic vISDN debugging",
	NULL,
	NULL
};

static char debug_visdn_q921_help[] =
	"Usage: debug visdn q921 [interface]\n"
	"	Traces q921 traffic\n";

static struct opbx_cli_entry debug_visdn_q921 =
{
        { "debug", "visdn", "q921", NULL },
	do_debug_visdn_q921,
	"Enables q.921 tracing",
	debug_visdn_q921_help,
	NULL
};

static struct opbx_cli_entry no_debug_visdn_q921 =
{
        { "no", "debug", "visdn", "q921", NULL },
	do_no_debug_visdn_q921,
	"Disables q.921 tracing",
	NULL,
	NULL
};

static char debug_visdn_q931_help[] =
	"Usage: debug visdn q931 [interface]\n"
	"	Traces q931 traffic\n";

static struct opbx_cli_entry debug_visdn_q931 =
{
        { "debug", "visdn", "q931", NULL },
	do_debug_visdn_q931,
	"Enables q.931 tracing",
	debug_visdn_q931_help,
	NULL
};

static struct opbx_cli_entry no_debug_visdn_q931 =
{
        { "no", "debug", "visdn", "q931", NULL },
	do_no_debug_visdn_q931,
	"Disables q.931 tracing",
	NULL,
	NULL
};

static char show_visdn_interfaces_help[] =
	"Usage: visdn show interfaces\n"
	"	Displays informations on vISDN interfaces\n";

static struct opbx_cli_entry show_visdn_interfaces =
{
        { "show", "visdn", "interfaces", NULL },
	do_show_visdn_interfaces,
	"Displays vISDN interface information",
	show_visdn_interfaces_help,
	NULL
};

static char visdn_visdn_reload_help[] =
	"Usage: visdn reload\n"
	"	Reloads vISDN config\n";

static struct opbx_cli_entry visdn_reload =
{
        { "visdn", "reload", NULL },
	do_visdn_reload,
	"Reloads vISDN configuration",
	visdn_visdn_reload_help,
	NULL
};

static char visdn_show_visdn_channels_help[] =
	"Usage: visdn show channels\n"
	"	Displays informations on vISDN channels\n";

static struct opbx_cli_entry show_visdn_channels =
{
        { "show", "visdn", "channels", NULL },
	do_show_visdn_channels,
	"Displays vISDN channel information",
	visdn_show_visdn_channels_help,
	NULL
};

static char show_visdn_calls_help[] =
	"Usage: show visdn calls\n"
	"	Lists vISDN calls\n";

static struct opbx_cli_entry show_visdn_calls =
{
        { "show", "visdn", "calls", NULL },
	do_show_visdn_calls,
	"Lists vISDN calls",
	show_visdn_calls_help,
	NULL
};

static int visdn_devicestate(void *data)
{
	int res = OPBX_DEVICE_INVALID;

	// not sure what this should do xxx
	res = OPBX_DEVICE_UNKNOWN;
	return res;
}

static inline struct opbx_channel *callpvt_to_opbxchan(
	struct q931_call *call)
{
	return (struct opbx_channel *)call->pvt;
}

static enum q931_ie_called_party_number_type_of_number
	visdn_type_of_number_to_cdpn(enum visdn_type_of_number type_of_number)
{
	switch(type_of_number) {
	case VISDN_TYPE_OF_NUMBER_UNKNOWN:
		return Q931_IE_CDPN_TON_UNKNOWN;
	case VISDN_TYPE_OF_NUMBER_INTERNATIONAL:
		return Q931_IE_CDPN_TON_INTERNATIONAL;
	case VISDN_TYPE_OF_NUMBER_NATIONAL:
		return Q931_IE_CDPN_TON_NATIONAL;
	case VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC:
		return Q931_IE_CDPN_TON_NETWORK_SPECIFIC;
	case VISDN_TYPE_OF_NUMBER_SUBSCRIBER:
		return Q931_IE_CDPN_TON_SUBSCRIBER;
	case VISDN_TYPE_OF_NUMBER_ABBREVIATED:
		return Q931_IE_CDPN_TON_ABBREVIATED;
	}

	assert(0);
}

static enum q931_ie_calling_party_number_type_of_number
	visdn_type_of_number_to_cgpn(enum visdn_type_of_number type_of_number)
{
	switch(type_of_number) {
	case VISDN_TYPE_OF_NUMBER_UNKNOWN:
		return Q931_IE_CGPN_TON_UNKNOWN;
	case VISDN_TYPE_OF_NUMBER_INTERNATIONAL:
		return Q931_IE_CDPN_TON_INTERNATIONAL;
	case VISDN_TYPE_OF_NUMBER_NATIONAL:
		return Q931_IE_CGPN_TON_NATIONAL;
	case VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC:
		return Q931_IE_CGPN_TON_NETWORK_SPECIFIC;
	case VISDN_TYPE_OF_NUMBER_SUBSCRIBER:
		return Q931_IE_CGPN_TON_SUBSCRIBER;
	case VISDN_TYPE_OF_NUMBER_ABBREVIATED:
		return Q931_IE_CGPN_TON_ABBREVIATED;
	}
}

static int visdn_call(
	struct opbx_channel *opbx_chan,
	char *orig_dest,
	int timeout)
{
	struct visdn_chan *visdn_chan = opbx_chan->pvt->pvt;
	int err;
	char dest[256];

	visdn_chan->pbx_started = TRUE;

	strncpy(dest, orig_dest, sizeof(dest));

	// Parse destination and obtain interface name + number
	const char *intf_name;
	const char *number;
	char *stringp = dest;

	intf_name = strsep(&stringp, "/");
	if (!intf_name) {
		opbx_log(LOG_WARNING,
			"Invalid destination '%s' format (interface/number)\n",
			dest);

		err = -1;
		goto err_invalid_destination;
	}

	number = strsep(&stringp, "/");
	if (!number) {
		opbx_log(LOG_WARNING,
			"Invalid destination '%s' format (interface/number)\n",
			dest);

		err = -1;
		goto err_invalid_format;
	}

	enum q931_ie_bearer_capability_information_transfer_capability bc_itc =
		Q931_IE_BC_ITC_SPEECH;
	enum q931_ie_bearer_capability_user_information_layer_1_protocol bc_l1p =
		Q931_IE_BC_UIL1P_G711_ALAW;

	visdn_chan->is_voice = TRUE;

	const char *options = strsep(&stringp, "/");
	if (options) {
		if (strchr(options, 'D')) {
			bc_itc = Q931_IE_BC_ITC_UNRESTRICTED_DIGITAL;
			bc_l1p = Q931_IE_BC_UIL1P_UNUSED;
			visdn_chan->is_voice = FALSE;
		}
	}

	opbx_mutex_unlock(&opbx_chan->lock);
	opbx_mutex_lock(&visdn.lock);

	int found = FALSE;
	struct visdn_interface *intf;
	list_for_each_entry(intf, &visdn.ifs, ifs_node) {
		if (intf->q931_intf &&
		    !strcmp(intf->name, intf_name)) {
			found = TRUE;
			break;
		}
	}

	if (!found) {
		opbx_log(LOG_WARNING, "Interface %s not found\n", intf_name);
		err = -1;
                goto err_intf_not_found;
	}

	if (!intf->q931_intf) {
		opbx_log(LOG_WARNING, "Interface %s not present\n", intf_name);
		err = -1;
                goto err_intf_not_found;
	}

	struct q931_call *q931_call;
	q931_call = q931_call_alloc_out(intf->q931_intf);
	if (!q931_call) {
		opbx_log(LOG_WARNING, "Cannot allocate outbound call\n");
		err = -1;
		goto err_call_alloc;
	}

	if ((opbx_chan->_state != OPBX_STATE_DOWN) &&
	    (opbx_chan->_state != OPBX_STATE_RESERVED)) {
		opbx_log(LOG_WARNING,
			"visdn_call called on %s,"
			" neither down nor reserved\n",
			opbx_chan->name);

		err = -1;
		goto err_channel_not_down;
	}

	if (option_debug)
		opbx_log(LOG_DEBUG,
			"Calling %s on %s\n",
			dest, opbx_chan->name);

	q931_call->pvt = opbx_chan;

	visdn_chan->q931_call = q931_call_get(q931_call);

	char newname[40];
	snprintf(newname, sizeof(newname), "VISDN/%s/%c%ld",
		q931_call->intf->name,
		q931_call->direction == Q931_CALL_DIRECTION_INBOUND ? 'I' : 'O',
		q931_call->call_reference);

	opbx_change_name(opbx_chan, newname);

	opbx_setstate(opbx_chan, OPBX_STATE_DIALING);

	struct q931_ies ies = Q931_IES_INIT;

	struct q931_ie_bearer_capability *bc =
		q931_ie_bearer_capability_alloc();
	bc->coding_standard = Q931_IE_BC_CS_CCITT;
	bc->information_transfer_capability = bc_itc;
	bc->transfer_mode = Q931_IE_BC_TM_CIRCUIT;
	bc->information_transfer_rate = Q931_IE_BC_ITR_64;
	bc->user_information_layer_1_protocol = bc_l1p;
	bc->user_information_layer_2_protocol = Q931_IE_BC_UIL2P_UNUSED;
	bc->user_information_layer_3_protocol = Q931_IE_BC_UIL3P_UNUSED;
	q931_ies_add_put(&ies, &bc->ie);

	struct q931_ie_called_party_number *cdpn =
		q931_ie_called_party_number_alloc();
	cdpn->type_of_number =
		visdn_type_of_number_to_cdpn(intf->type_of_number);
	cdpn->numbering_plan_identificator = Q931_IE_CDPN_NPI_ISDN_TELEPHONY;
	snprintf(cdpn->number, sizeof(cdpn->number), "%s", number);
	q931_ies_add_put(&ies, &cdpn->ie);

	if (intf->q931_intf->role == LAPD_ROLE_NT &&
	    !intf->overlap_receiving) {
		struct q931_ie_sending_complete *sc =
			q931_ie_sending_complete_alloc();

		q931_ies_add_put(&ies, &sc->ie);
	}

	if (opbx_chan->callerid && strlen(opbx_chan->callerid)) {

		char callerid[255];
		char *name, *number;

		strncpy(callerid, opbx_chan->callerid, sizeof(callerid));
		opbx_callerid_parse(callerid, &name, &number);

		if (number) {
			struct q931_ie_calling_party_number *cgpn =
				q931_ie_calling_party_number_alloc();

			cgpn->type_of_number =
				visdn_type_of_number_to_cgpn(
						intf->local_type_of_number);
			cgpn->numbering_plan_identificator =
				Q931_IE_CGPN_NPI_ISDN_TELEPHONY;
			cgpn->presentation_indicator =
				Q931_IE_CGPN_PI_PRESENTATION_ALLOWED;
			cgpn->screening_indicator =
				Q931_IE_CGPN_SI_USER_PROVIDED_VERIFIED_AND_PASSED;

			strncpy(cgpn->number, number, sizeof(cgpn->number));

			q931_ies_add_put(&ies, &cgpn->ie);
		} else {
			opbx_log(LOG_WARNING,
				"Unable to parse '%s'"
				" into CallerID name & number\n",
				callerid);
		}
	}

	struct q931_ie_high_layer_compatibility *hlc =
		q931_ie_high_layer_compatibility_alloc();

	hlc->coding_standard = Q931_IE_HLC_CS_CCITT;
	hlc->interpretation = Q931_IE_HLC_P_FIRST;
	hlc->presentation_method = Q931_IE_HLC_PM_HIGH_LAYER_PROTOCOL_PROFILE;
	hlc->characteristics_identification = Q931_IE_HLC_CI_TELEPHONY;

	q931_setup_request(q931_call, &ies);

	opbx_mutex_unlock(&visdn.lock);
	opbx_mutex_lock(&opbx_chan->lock);

	q931_call_put(q931_call);

	return 0;

err_channel_not_down:
	q931_call_release_reference(q931_call);
	q931_call_put(q931_call);
err_call_alloc:
err_intf_not_found:
	opbx_mutex_unlock(&visdn.lock);
	opbx_mutex_lock(&opbx_chan->lock);
err_invalid_format:
err_invalid_destination:

	return err;
}

static int visdn_answer(struct opbx_channel *opbx_chan)
{
	struct visdn_chan *visdn_chan = opbx_chan->pvt->pvt;

	visdn_debug("visdn_answer\n");

	opbx_indicate(opbx_chan, -1);

	if (!visdn_chan) {
		opbx_log(LOG_ERROR, "NO VISDN_CHAN!!\n");
		return -1;
	}

	opbx_mutex_unlock(&opbx_chan->lock);
	opbx_mutex_lock(&visdn.lock);
	if (visdn_chan->q931_call->state == U6_CALL_PRESENT ||
	    visdn_chan->q931_call->state == U7_CALL_RECEIVED ||
	    visdn_chan->q931_call->state == U9_INCOMING_CALL_PROCEEDING ||
	    visdn_chan->q931_call->state == U25_OVERLAP_RECEIVING ||
	    visdn_chan->q931_call->state == N2_OVERLAP_SENDING ||
	    visdn_chan->q931_call->state == N3_OUTGOING_CALL_PROCEEDING ||
	    visdn_chan->q931_call->state == N4_CALL_DELIVERED) {
		q931_setup_response(visdn_chan->q931_call, NULL);
	}
	opbx_mutex_unlock(&visdn.lock);
	opbx_mutex_lock(&opbx_chan->lock);

	return 0;
}

static int visdn_bridge(
	struct opbx_channel *c0,
	struct opbx_channel *c1,
	int flags, struct opbx_frame **fo,
	struct opbx_channel **rc)
{
	opbx_log(LOG_WARNING, "visdn_bridge\n");

	return -2;

	/* if need DTMF, cant native bridge (at least not yet...) */
	if (flags & (OPBX_BRIDGE_DTMF_CHANNEL_0 | OPBX_BRIDGE_DTMF_CHANNEL_1))
		return -2;

	struct visdn_chan *visdn_chan1 = c0->pvt->pvt;
	struct visdn_chan *visdn_chan2 = c1->pvt->pvt;

	char path[100], dest1[100], dest2[100];

	snprintf(path, sizeof(path),
		"/sys/class/net/%s/visdn_channel/connected/../B%d",
		visdn_chan1->q931_call->intf->name,
		visdn_chan1->q931_call->channel->id+1);

	memset(dest1, 0, sizeof(dest1));
	if (readlink(path, dest1, sizeof(dest1) - 1) < 0) {
		opbx_log(LOG_ERROR, "readlink(%s): %s\n", path, strerror(errno));
		return -2;
	}

	char *chanid1 = strrchr(dest1, '/');
	if (!chanid1 || !strlen(chanid1 + 1)) {
		opbx_log(LOG_ERROR,
			"Invalid chanid found in symlink %s\n",
			dest1);
		return -2;
	}

	chanid1++;

	snprintf(path, sizeof(path),
		"/sys/class/net/%s/visdn_channel/../B%d",
		visdn_chan2->q931_call->intf->name,
		visdn_chan2->q931_call->channel->id+1);

	memset(dest2, 0, sizeof(dest2));
	if (readlink(path, dest2, sizeof(dest2) - 1) < 0) {
		opbx_log(LOG_ERROR, "readlink(%s): %s\n", path, strerror(errno));
		return -2;
	}

	char *chanid2 = strrchr(dest2, '/');
	if (!chanid2 || !strlen(chanid2 + 1)) {
		opbx_log(LOG_ERROR,
			"Invalid chanid found in symlink %s\n",
			dest2);
		return -2;
	}

	chanid2++;

	visdn_debug("Connecting chan %s to chan %s\n", chanid1, chanid2);

	struct visdn_connect vc;
	snprintf(vc.src_chanid, sizeof(vc.src_chanid), "%s", chanid1);
	snprintf(vc.dst_chanid, sizeof(vc.dst_chanid), "%s", chanid2);
	vc.flags = 0;

	if (ioctl(visdn.control_fd, VISDN_IOC_CONNECT,
	    (caddr_t) &vc) < 0) {
		opbx_log(LOG_ERROR,
			"ioctl(VISDN_CONNECT): %s\n",
			strerror(errno));
		return -2;
	}

	struct opbx_channel *cs[3];
	cs[0] = c0;
	cs[1] = c1;
	cs[2] = NULL;

	for (;;) {
		struct opbx_channel *who;
		int to;
		who = opbx_waitfor_n(cs, 2, &to);
		if (!who) {
			opbx_log(LOG_DEBUG, "Ooh, empty read...\n");
			continue;
		}

		struct opbx_frame *f;
		f = opbx_read(who);
		if (!f) {
			return 0;
		}

		printf("Frame %s\n", who->name);

		opbx_frfree(f);
	}

	return 0;
}

struct opbx_frame *visdn_exception(struct opbx_channel *opbx_chan)
{
	opbx_log(LOG_WARNING, "visdn_exception\n");

	return NULL;
}

#if 0

CMANTUNES: With the new channel generator thread, this is no longer necessary

static int visdn_generator_start(struct opbx_channel *chan);
static int visdn_generator_stop(struct opbx_channel *chan);
#endif

/* We are called with chan->lock'ed */
static int visdn_indicate(struct opbx_channel *opbx_chan, int condition)
{
	struct visdn_chan *visdn_chan = opbx_chan->pvt->pvt;

	if (!visdn_chan) {
		opbx_log(LOG_ERROR, "NO VISDN_CHAN!!\n");
		return 1;
	}

	visdn_debug("visdn_indicate %d\n", condition);

	switch(condition) {
	case OPBX_CONTROL_RING:
	case OPBX_CONTROL_TAKEOFFHOOK:
	case OPBX_CONTROL_FLASH:
	case OPBX_CONTROL_WINK:
	case OPBX_CONTROL_OPTION:
	case OPBX_CONTROL_RADIO_KEY:
	case OPBX_CONTROL_RADIO_UNKEY:
		return 1;
	break;

	case -1:
		opbx_playtones_stop(opbx_chan);
#if 0
		if (!opbx_chan->pbx)
			visdn_generator_stop(opbx_chan);
#endif
		return 0;
	break;

	case OPBX_CONTROL_OFFHOOK: {
		const struct tone_zone_sound *tone;
		tone = opbx_get_indication_tone(opbx_chan->zone, "dial");
		if (tone) {
			opbx_playtones_start(opbx_chan, 0, tone->data, 1);
#if 0
			if (!opbx_chan->pbx)
				visdn_generator_start(opbx_chan);
#endif
		}

		return 0;
	}
	break;

	case OPBX_CONTROL_HANGUP: {
		const struct tone_zone_sound *tone;
		tone = opbx_get_indication_tone(opbx_chan->zone, "congestion");
		if (tone) {
			opbx_playtones_start(opbx_chan, 0, tone->data, 1);
#if 0
			if (!opbx_chan->pbx)
				visdn_generator_start(opbx_chan);
#endif
		}

		return 0;
	}
	break;

	case OPBX_CONTROL_RINGING: {
                struct q931_ies ies = Q931_IES_INIT;

		struct q931_ie_progress_indicator *pi = NULL;

		if (opbx_chan->dialed &&
		   strcmp(opbx_chan->dialed->type, VISDN_CHAN_TYPE)) {

			visdn_debug("Channel is not VISDN, sending"
					" progress indicator\n");

			pi = q931_ie_progress_indicator_alloc();
			pi->coding_standard = Q931_IE_PI_CS_CCITT;
			pi->location = q931_ie_progress_indicator_location(
						visdn_chan->q931_call);
			pi->progress_description =
				Q931_IE_PI_PD_CALL_NOT_END_TO_END;
			q931_ies_add_put(&ies, &pi->ie);
		}

		pi = q931_ie_progress_indicator_alloc();
		pi->coding_standard = Q931_IE_PI_CS_CCITT;
		pi->location = q931_ie_progress_indicator_location(
					visdn_chan->q931_call);
		pi->progress_description = Q931_IE_PI_PD_IN_BAND_INFORMATION;
		q931_ies_add_put(&ies, &pi->ie);

		opbx_setstate(opbx_chan, OPBX_STATE_RINGING);

		opbx_mutex_unlock(&opbx_chan->lock);
		opbx_mutex_lock(&visdn.lock);

		if (visdn_chan->q931_call->state == N1_CALL_INITIATED)
			q931_proceeding_request(visdn_chan->q931_call, NULL);

		q931_alerting_request(visdn_chan->q931_call, &ies);
		opbx_mutex_unlock(&visdn.lock);
		opbx_mutex_lock(&opbx_chan->lock);

		const struct tone_zone_sound *tone;
		tone = opbx_get_indication_tone(opbx_chan->zone, "ring");
		if (tone) {
			opbx_playtones_start(opbx_chan, 0, tone->data, 1);
#if 0
			if (!opbx_chan->pbx)
				visdn_generator_start(opbx_chan);
#endif
		}

		return 0;
	}
	break;

	case OPBX_CONTROL_ANSWER:
		opbx_playtones_stop(opbx_chan);
#if 0
		if (!opbx_chan->pbx);
			visdn_generator_stop(opbx_chan);
#endif
		return 0;
	break;

	case OPBX_CONTROL_BUSY: {
                struct q931_ies ies = Q931_IES_INIT;

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(
							visdn_chan->q931_call);
		cause->value = Q931_IE_C_CV_USER_BUSY;
		q931_ies_add_put(&ies, &cause->ie);

		opbx_mutex_unlock(&opbx_chan->lock);
		opbx_mutex_lock(&visdn.lock);
		q931_disconnect_request(visdn_chan->q931_call, &ies);
		opbx_mutex_unlock(&visdn.lock);
		opbx_mutex_lock(&opbx_chan->lock);

		const struct tone_zone_sound *tone;
		tone = opbx_get_indication_tone(opbx_chan->zone, "busy");
		if (tone)
			opbx_playtones_start(opbx_chan, 0, tone->data, 1);

		return 0;
	}
	break;

	case OPBX_CONTROL_CONGESTION: {
                struct q931_ies ies = Q931_IES_INIT;

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(
							visdn_chan->q931_call);
		cause->value = Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;
		q931_ies_add_put(&ies, &cause->ie);

		opbx_mutex_unlock(&opbx_chan->lock);
		opbx_mutex_lock(&visdn.lock);
		q931_disconnect_request(visdn_chan->q931_call, &ies);
		opbx_mutex_unlock(&visdn.lock);
		opbx_mutex_lock(&opbx_chan->lock);

		const struct tone_zone_sound *tone;
		tone = opbx_get_indication_tone(opbx_chan->zone, "busy");
		if (tone) {
			opbx_playtones_start(opbx_chan, 0, tone->data, 1);
#if 0
			if (!opbx_chan->pbx)
				visdn_generator_start(opbx_chan);
#endif
		}

		return 0;
	}
	break;

	case OPBX_CONTROL_PROGRESS: {
                struct q931_ies ies = Q931_IES_INIT;

		struct q931_ie_progress_indicator *pi =
			q931_ie_progress_indicator_alloc();
		pi->coding_standard = Q931_IE_PI_CS_CCITT;
		pi->location = q931_ie_progress_indicator_location(
					visdn_chan->q931_call);

		if (opbx_chan->dialed &&
		   strcmp(opbx_chan->dialed->type, VISDN_CHAN_TYPE)) {
			pi->progress_description =
				Q931_IE_PI_PD_CALL_NOT_END_TO_END; // FIXME
		} else if (visdn_chan->is_voice) {
			pi->progress_description =
				Q931_IE_PI_PD_IN_BAND_INFORMATION;
		}

		q931_ies_add_put(&ies, &pi->ie);

		opbx_mutex_unlock(&opbx_chan->lock);
		opbx_mutex_lock(&visdn.lock);
		q931_progress_request(visdn_chan->q931_call, &ies);
		opbx_mutex_unlock(&visdn.lock);
		opbx_mutex_lock(&opbx_chan->lock);

		return 0;
	}
	break;

	case OPBX_CONTROL_PROCEEDING:
		opbx_mutex_unlock(&opbx_chan->lock);
		opbx_mutex_lock(&visdn.lock);
		q931_proceeding_request(visdn_chan->q931_call, NULL);
		opbx_mutex_unlock(&visdn.lock);
		opbx_mutex_lock(&opbx_chan->lock);

		return 0;
	break;
	}

	return 1;
}

static int visdn_fixup(
	struct opbx_channel *oldchan,
	struct opbx_channel *newchan)
{
	struct visdn_chan *chan = newchan->pvt->pvt;

	if (chan->opbx_chan != oldchan) {
		opbx_log(LOG_WARNING, "old channel wasn't %p but was %p\n",
				oldchan, chan->opbx_chan);
		return -1;
	}

	chan->opbx_chan = newchan;

	return 0;
}

static int visdn_setoption(
	struct opbx_channel *opbx_chan,
	int option,
	void *data,
	int datalen)
{
	opbx_log(LOG_ERROR, "%s\n", __FUNCTION__);

	return -1;
}

static int visdn_transfer(
	struct opbx_channel *opbx,
	char *dest)
{
	opbx_log(LOG_ERROR, "%s\n", __FUNCTION__);

	return -1;
}

static int visdn_send_digit(struct opbx_channel *opbx_chan, char digit)
{
	struct visdn_chan *visdn_chan = opbx_chan->pvt->pvt;
	struct q931_call *q931_call = visdn_chan->q931_call;
	struct visdn_interface *intf = q931_call->intf->pvt;

	struct q931_ies ies = Q931_IES_INIT;

	struct q931_ie_called_party_number *cdpn =
		q931_ie_called_party_number_alloc();
	cdpn->type_of_number = visdn_type_of_number_to_cdpn(
							intf->type_of_number);
	cdpn->numbering_plan_identificator = Q931_IE_CDPN_NPI_ISDN_TELEPHONY;
	cdpn->number[0] = digit;
	cdpn->number[1] = '\0';
	q931_ies_add_put(&ies, &cdpn->ie);

	opbx_mutex_unlock(&opbx_chan->lock);
	opbx_mutex_lock(&visdn.lock);
	q931_info_request(q931_call, &ies);
	opbx_mutex_unlock(&visdn.lock);
	opbx_mutex_lock(&opbx_chan->lock);

	return 1;
}

static int visdn_sendtext(struct opbx_channel *opbx, char *text)
{
	opbx_log(LOG_WARNING, "%s\n", __FUNCTION__);

	return -1;
}

static void visdn_destroy(struct visdn_chan *visdn_chan)
{
//	if (visdn_chan->ec)
//		echo_can_free(visdn_chan->ec);

	free(visdn_chan);
}

static struct visdn_chan *visdn_alloc()
{
	struct visdn_chan *visdn_chan;

	visdn_chan = malloc(sizeof(*visdn_chan));
	if (!visdn_chan)
		return NULL;

	memset(visdn_chan, 0, sizeof(*visdn_chan));

	visdn_chan->channel_fd = -1;

//	visdn_chan->ec = echo_can_create(256, 0);

	return visdn_chan;
}

static int visdn_hangup(struct opbx_channel *opbx_chan)
{
	visdn_debug("visdn_hangup %s\n", opbx_chan->name);

	struct visdn_chan *visdn_chan = opbx_chan->pvt->pvt;

	if (visdn_chan->q931_call) {
		/*
		After we return from visdn_hangup() the opbx_channel is not
		valid anymore. On the other way, q.931 "hangup" is a long
		process, we can only invoke a DISCONNECT-REQ primitive and
		leave libq931 handle the disconnection process.
		Unfortunately the consequence is that we cannot rely on
		libq931's disconnect_channel to actually disconnect the
		streamport channel. We also cannot generate the disconnection
		tones.

		Asterisk is very lacky in this respect. A better
		implementation would have used a reference counter to allow
		the channel driver to hold a channel until it's not needed
		anymore
		*/

		visdn_chan->q931_call->pvt = NULL;

		if (
		    visdn_chan->q931_call->state != N0_NULL_STATE &&
		    visdn_chan->q931_call->state != N1_CALL_INITIATED &&
		    visdn_chan->q931_call->state != N11_DISCONNECT_REQUEST &&
		    visdn_chan->q931_call->state != N12_DISCONNECT_INDICATION &&
		    visdn_chan->q931_call->state != N15_SUSPEND_REQUEST &&
		    visdn_chan->q931_call->state != N17_RESUME_REQUEST &&
		    visdn_chan->q931_call->state != N19_RELEASE_REQUEST &&
		    visdn_chan->q931_call->state != N22_CALL_ABORT &&
		    visdn_chan->q931_call->state != U0_NULL_STATE &&
		    visdn_chan->q931_call->state != U6_CALL_PRESENT &&
		    visdn_chan->q931_call->state != U11_DISCONNECT_REQUEST &&
		    visdn_chan->q931_call->state != U12_DISCONNECT_INDICATION &&
		    visdn_chan->q931_call->state != U15_SUSPEND_REQUEST &&
		    visdn_chan->q931_call->state != U17_RESUME_REQUEST &&
		    visdn_chan->q931_call->state != U19_RELEASE_REQUEST) {

                        struct q931_ies ies = Q931_IES_INIT;

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(
							visdn_chan->q931_call);
			cause->value = Q931_IE_C_CV_NORMAL_CALL_CLEARING;
			q931_ies_add_put(&ies, &cause->ie);

			opbx_mutex_lock(&visdn.lock);
			q931_disconnect_request(visdn_chan->q931_call, &ies);
			opbx_mutex_unlock(&visdn.lock);
		}

		q931_call_put(visdn_chan->q931_call);
		visdn_chan->q931_call = NULL;
	}

	if (visdn_chan->suspended_call) {
		// We are responsible for the channel
		q931_channel_release(visdn_chan->suspended_call->q931_chan);

		list_del(&visdn_chan->suspended_call->node);
		free(visdn_chan->suspended_call);
		visdn_chan->suspended_call = NULL;
	}

	// Make sure the generator is stopped
#if 0
	if (!opbx_chan->pbx)
		visdn_generator_stop(opbx_chan);
#endif
	if (visdn_chan) {
		if (visdn_chan->channel_fd >= 0) {
			// Disconnect the softport since we cannot rely on
			// libq931 (see above)
			if (ioctl(visdn_chan->channel_fd,
					VISDN_IOC_DISCONNECT, NULL) < 0) {
				opbx_log(LOG_ERROR,
					"ioctl(VISDN_IOC_DISCONNECT): %s\n",
					strerror(errno));
			}

			if (close(visdn_chan->channel_fd) < 0) {
				opbx_log(LOG_ERROR,
					"close(visdn_chan->channel_fd): %s\n",
					strerror(errno));
			}

			visdn_chan->channel_fd = -1;
		}

		visdn_destroy(visdn_chan);

		opbx_chan->pvt->pvt = NULL;
	}

	opbx_setstate(opbx_chan, OPBX_STATE_DOWN);

	visdn_debug("visdn_hangup complete\n");

	return 0;
}

static struct opbx_frame *visdn_read(struct opbx_channel *opbx_chan)
{
	struct visdn_chan *visdn_chan = opbx_chan->pvt->pvt;
	static struct opbx_frame f;
	static char buf[512];

	if (visdn_chan->channel_fd < 0) {
		f.frametype = OPBX_FRAME_NULL;
		f.subclass = 0;
		f.samples = 0;
		f.datalen = 0;
		f.data = NULL;
		f.offset = 0;
		f.src = VISDN_CHAN_TYPE;
		f.mallocd = 0;
		f.delivery.tv_sec = 0;
		f.delivery.tv_usec = 0;

		return &f;
	}

	int nread = read(visdn_chan->channel_fd, buf, 512);
	if (nread < 0) {
		opbx_log(LOG_WARNING, "read error: %s\n", strerror(errno));
		return &f;
	}

/*
	for (i=0; i<nread; i++) {
		buf[i] = linear_to_alaw(
				echo_can_update(visdn_chan->ec,
					alaw_to_linear(txbuf[i]),
					alaw_to_linear(buf[i])));
	}
*/

#if 0
struct timeval tv;
gettimeofday(&tv, NULL);
unsigned long long t = tv.tv_sec * 1000000ULL + tv.tv_usec;
opbx_verbose(VERBOSE_PREFIX_3 "R %.3f %d %d\n", t/1000000.0, visdn_chan->channel_fd, r);*/
#endif

	f.frametype = OPBX_FRAME_VOICE;
	f.subclass = OPBX_FORMAT_ALAW;
	f.samples = nread;
	f.datalen = nread;
	f.data = buf;
	f.offset = 0;
	f.src = VISDN_CHAN_TYPE;
	f.mallocd = 0;
	f.delivery.tv_sec = 0;
	f.delivery.tv_usec = 0;

	return &f;
}

static int visdn_write(struct opbx_channel *opbx_chan, struct opbx_frame *frame)
{
	struct visdn_chan *visdn_chan = opbx_chan->pvt->pvt;

	if (frame->frametype != OPBX_FRAME_VOICE) {
		opbx_log(LOG_WARNING,
			"Don't know what to do with frame type '%d'\n",
			frame->frametype);

		return 0;
	}

	if (frame->subclass != OPBX_FORMAT_ALAW) {
		opbx_log(LOG_WARNING,
			"Cannot handle frames in %d format\n",
			frame->subclass);
		return 0;
	}

	if (visdn_chan->channel_fd < 0) {
//		opbx_log(LOG_WARNING,
//			"Attempting to write on unconnected channel\n");
		return 0;
	}

#if 0
opbx_verbose(VERBOSE_PREFIX_3 "W %d %02x%02x%02x%02x%02x%02x%02x%02x %d\n", visdn_chan->channel_fd,
	*(__u8 *)(frame->data + 0),
	*(__u8 *)(frame->data + 1),
	*(__u8 *)(frame->data + 2),
	*(__u8 *)(frame->data + 3),
	*(__u8 *)(frame->data + 4),
	*(__u8 *)(frame->data + 5),
	*(__u8 *)(frame->data + 6),
	*(__u8 *)(frame->data + 7),
	frame->datalen);
#endif

	write(visdn_chan->channel_fd, frame->data, frame->datalen);

	return 0;
}

static struct opbx_channel *visdn_new(
	struct visdn_chan *visdn_chan,
	int state)
{
	struct opbx_channel *opbx_chan;
	opbx_chan = opbx_channel_alloc(1);
	if (!opbx_chan) {
		opbx_log(LOG_WARNING, "Unable to allocate channel\n");
		return NULL;
	}

        opbx_chan->type = VISDN_CHAN_TYPE;

	opbx_chan->fds[0] = visdn.timer_fd;

	if (state == OPBX_STATE_RING)
		opbx_chan->rings = 1;

	opbx_chan->adsicpe = OPBX_ADSI_UNAVAILABLE;

	opbx_chan->nativeformats = OPBX_FORMAT_ALAW;
	opbx_chan->pvt->rawreadformat = OPBX_FORMAT_ALAW;
	opbx_chan->readformat = OPBX_FORMAT_ALAW;
	opbx_chan->pvt->rawwriteformat = OPBX_FORMAT_ALAW;
	opbx_chan->writeformat = OPBX_FORMAT_ALAW;

//	opbx_chan->language[0] = '\0';
//	opbx_set_flag(opbx_chan, OPBX_FLAG_DIGITAL);

	visdn_chan->opbx_chan = opbx_chan;
	opbx_chan->pvt->pvt = visdn_chan;

	opbx_chan->pvt->call = visdn_call;
	opbx_chan->pvt->hangup = visdn_hangup;
	opbx_chan->pvt->answer = visdn_answer;
	opbx_chan->pvt->read = visdn_read;
	opbx_chan->pvt->write = visdn_write;
	opbx_chan->pvt->bridge = visdn_bridge;
	opbx_chan->pvt->exception = visdn_exception;
	opbx_chan->pvt->indicate = visdn_indicate;
	opbx_chan->pvt->fixup = visdn_fixup;
	opbx_chan->pvt->setoption = visdn_setoption;
	opbx_chan->pvt->send_text = visdn_sendtext;
	opbx_chan->pvt->transfer = visdn_transfer;
	opbx_chan->pvt->send_digit = visdn_send_digit;

	opbx_setstate(opbx_chan, state);

	return opbx_chan;
}

static struct opbx_channel *visdn_request(char *type, int format, void *data)
{
	struct visdn_chan *visdn_chan;
	char *dest = NULL;

	if (!(format & OPBX_FORMAT_ALAW)) {
		opbx_log(LOG_NOTICE,
			"Asked to get a channel of unsupported format '%d'\n",
			format);
		return NULL;
	}

	if (data) {
		dest = opbx_strdupa((char *)data);
	} else {
		opbx_log(LOG_WARNING, "Channel requested with no data\n");
		return NULL;
	}

	visdn_chan = visdn_alloc();
	if (!visdn_chan) {
		opbx_log(LOG_ERROR, "Cannot allocate visdn_chan\n");
		return NULL;
	}

	struct opbx_channel *opbx_chan;
	opbx_chan = visdn_new(visdn_chan, OPBX_STATE_DOWN);

	snprintf(opbx_chan->name, sizeof(opbx_chan->name), "VISDN/null");

	opbx_mutex_lock(&usecnt_lock);
	visdn.usecnt++;
	opbx_mutex_unlock(&usecnt_lock);
	opbx_update_use_count();

	return opbx_chan;
}

// Must be called with visdn.lock acquired
static void refresh_polls_list()
{
	visdn.npolls = 0;

	visdn.polls[visdn.npolls].fd = visdn.netlink_socket;
	visdn.polls[visdn.npolls].events = POLLIN | POLLERR;
	visdn.poll_infos[visdn.npolls].type = POLL_INFO_TYPE_NETLINK;
	visdn.poll_infos[visdn.npolls].interface = NULL;
	(visdn.npolls)++;

	visdn.open_pending = FALSE;

	struct visdn_interface *intf;
	list_for_each_entry(intf, &visdn.ifs, ifs_node) {
		if (intf->open_pending)
			visdn.open_pending = TRUE;
			visdn.open_pending_nextcheck = 0;

		if (!intf->q931_intf)
			continue;

		if (intf->q931_intf->role == LAPD_ROLE_NT) {
			visdn.polls[visdn.npolls].fd = intf->q931_intf->master_socket;
			visdn.polls[visdn.npolls].events = POLLIN | POLLERR;
			visdn.poll_infos[visdn.npolls].type = POLL_INFO_TYPE_INTERFACE;
			visdn.poll_infos[visdn.npolls].interface = intf->q931_intf;
			(visdn.npolls)++;
		} else {
			visdn.polls[visdn.npolls].fd = intf->q931_intf->dlc.socket;
			visdn.polls[visdn.npolls].events = POLLIN | POLLERR;
			visdn.poll_infos[visdn.npolls].type = POLL_INFO_TYPE_DLC;
			visdn.poll_infos[visdn.npolls].dlc = &intf->q931_intf->dlc;
			(visdn.npolls)++;
		}

		struct q931_dlc *dlc;
		list_for_each_entry(dlc,  &intf->q931_intf->dlcs, intf_node) {
			visdn.polls[visdn.npolls].fd = dlc->socket;
			visdn.polls[visdn.npolls].events = POLLIN | POLLERR;
			visdn.poll_infos[visdn.npolls].type = POLL_INFO_TYPE_DLC;
			visdn.poll_infos[visdn.npolls].dlc = dlc;
			(visdn.npolls)++;
		}
	}
}

// Must be called with visdn.lock acquired
static void visdn_accept(
	struct q931_interface *intf,
	int accept_socket)
{
	struct q931_dlc *newdlc;

	newdlc = q931_accept(intf, accept_socket);
	if (!newdlc)
		return;

	visdn_debug("New DLC (TEI=%d) accepted on interface %s\n",
			newdlc->tei,
			intf->name);

	refresh_polls_list();
}

static int visdn_open_interface(
	struct visdn_interface *intf)
{
	assert(!intf->q931_intf);

	intf->open_pending = TRUE;

	intf->q931_intf = q931_open_interface(visdn.libq931, intf->name, 0);
	if (!intf->q931_intf) {
		opbx_log(LOG_WARNING,
			"Cannot open interface %s, skipping\n",
			intf->name);

		return -1;
	}

	intf->q931_intf->pvt = intf;
	intf->q931_intf->network_role = intf->network_role;
	intf->q931_intf->dlc_autorelease_time = intf->dlc_autorelease_time;

	if (intf->T301) intf->q931_intf->T301 = intf->T301 * 1000000LL;
	if (intf->T302) intf->q931_intf->T302 = intf->T302 * 1000000LL;
	if (intf->T303) intf->q931_intf->T303 = intf->T303 * 1000000LL;
	if (intf->T304) intf->q931_intf->T304 = intf->T304 * 1000000LL;
	if (intf->T305) intf->q931_intf->T305 = intf->T305 * 1000000LL;
	if (intf->T306) intf->q931_intf->T306 = intf->T306 * 1000000LL;
	if (intf->T308) intf->q931_intf->T308 = intf->T308 * 1000000LL;
	if (intf->T309) intf->q931_intf->T309 = intf->T309 * 1000000LL;
	if (intf->T310) intf->q931_intf->T310 = intf->T310 * 1000000LL;
	if (intf->T312) intf->q931_intf->T312 = intf->T312 * 1000000LL;
	if (intf->T313) intf->q931_intf->T313 = intf->T313 * 1000000LL;
	if (intf->T314) intf->q931_intf->T314 = intf->T314 * 1000000LL;
	if (intf->T316) intf->q931_intf->T316 = intf->T316 * 1000000LL;
	if (intf->T317) intf->q931_intf->T317 = intf->T317 * 1000000LL;
	if (intf->T318) intf->q931_intf->T318 = intf->T318 * 1000000LL;
	if (intf->T319) intf->q931_intf->T319 = intf->T319 * 1000000LL;
	if (intf->T320) intf->q931_intf->T320 = intf->T320 * 1000000LL;
	if (intf->T321) intf->q931_intf->T321 = intf->T321 * 1000000LL;
	if (intf->T322) intf->q931_intf->T322 = intf->T322 * 1000000LL;

	if (intf->q931_intf->role == LAPD_ROLE_NT) {
		if (listen(intf->q931_intf->master_socket, 100) < 0) {
			opbx_log(LOG_ERROR,
				"cannot listen on master socket: %s\n",
				strerror(errno));

			return -1;
		}
	}

	intf->open_pending = FALSE;

	return 0;
}

// Must be called with visdn.lock acquired
static void visdn_add_interface(const char *name)
{
	int found = FALSE;
	struct visdn_interface *intf;
	list_for_each_entry(intf, &visdn.ifs, ifs_node) {
		if (!strcasecmp(intf->name, name)) {
			found = TRUE;
			break;
		}
	}

	if (!found) {
		intf = malloc(sizeof(*intf));

		INIT_LIST_HEAD(&intf->suspended_calls);
		intf->q931_intf = NULL;
		intf->configured = FALSE;
		intf->open_pending = FALSE;
		strncpy(intf->name, name, sizeof(intf->name));
		visdn_copy_interface_config(intf, &visdn.default_intf);

		list_add_tail(&intf->ifs_node, &visdn.ifs);
	}

	if (!intf->q931_intf) {
		visdn_debug("Opening interface %s\n", name);

		visdn_open_interface(intf);
		refresh_polls_list();
	}
}

// Must be called with visdn.lock acquired
static void visdn_rem_interface(const char *name)
{
	struct visdn_interface *intf;
	list_for_each_entry(intf, &visdn.ifs, ifs_node) {
		if (intf->q931_intf &&
		    !strcmp(intf->name, name)) {
			q931_close_interface(intf->q931_intf);

			intf->q931_intf = NULL;

			refresh_polls_list();

			break;
		}
	}
}

#define MAX_PAYLOAD 1024

// Must be called with visdn.lock acquired
static void visdn_netlink_receive()
{
	struct sockaddr_nl tonl;
	tonl.nl_family = AF_NETLINK;
	tonl.nl_pid = 0;
	tonl.nl_groups = 0;

	struct msghdr skmsg;
	struct sockaddr_nl dest_addr;
	struct cmsghdr cmsg;
	struct iovec iov;

	__u8 data[NLMSG_SPACE(MAX_PAYLOAD)];

	struct nlmsghdr *hdr = (struct nlmsghdr *)data;

	iov.iov_base = data;
	iov.iov_len = sizeof(data);

	skmsg.msg_name = &dest_addr;
	skmsg.msg_namelen = sizeof(dest_addr);
	skmsg.msg_iov = &iov;
	skmsg.msg_iovlen = 1;
	skmsg.msg_control = &cmsg;
	skmsg.msg_controllen = sizeof(cmsg);
	skmsg.msg_flags = 0;

	if(recvmsg(visdn.netlink_socket, &skmsg, 0) < 0) {
		opbx_log(LOG_WARNING, "recvmsg: %s\n", strerror(errno));
		return;
	}

	// Implement multipar messages FIXME FIXME TODO

	if (hdr->nlmsg_type == RTM_NEWLINK) {
		struct ifinfomsg *ifi = NLMSG_DATA(hdr);

		if (ifi->ifi_type == ARPHRD_LAPD) {

			char ifname[IFNAMSIZ] = "";
			int len = hdr->nlmsg_len - NLMSG_LENGTH(sizeof(struct ifinfomsg));

			struct rtattr *rtattr;
			for (rtattr = IFLA_RTA(ifi);
			     RTA_OK(rtattr, len);
			     rtattr = RTA_NEXT(rtattr, len)) {

				if (rtattr->rta_type == IFLA_IFNAME) {
					strncpy(ifname,
						RTA_DATA(rtattr),
						sizeof(ifname));
				}
			}

			if (ifi->ifi_flags & IFF_UP) {
				visdn_debug("Netlink msg: %s UP %s\n",
						ifname,
						(ifi->ifi_flags & IFF_ALLMULTI) ? "NT": "TE");

				visdn_add_interface(ifname);
			} else {
				visdn_debug("Netlink msg: %s DOWN %s\n",
						ifname,
						(ifi->ifi_flags & IFF_ALLMULTI) ? "NT": "TE");

				visdn_rem_interface(ifname);
			}
		}
	}
}

static int visdn_q931_thread_do_poll()
{
	longtime_t usec_to_wait = q931_run_timers(visdn.libq931);
	int msec_to_wait;

	if (usec_to_wait < 0) {
		msec_to_wait = -1;
	} else {
		msec_to_wait = usec_to_wait / 1000 + 1;
	}

	if (visdn.open_pending)
		msec_to_wait = (msec_to_wait > 0 && msec_to_wait < 2001) ?
				msec_to_wait : 2001;

	visdn_debug("select timeout = %d\n", msec_to_wait);

	// Uhm... we should lock, copy polls and unlock before poll()
	if (poll(visdn.polls, visdn.npolls, msec_to_wait) < 0) {
		if (errno == EINTR)
			return TRUE;

		opbx_log(LOG_WARNING, "poll error: %s\n", strerror(errno));
		exit(1);
	}

	opbx_mutex_lock(&visdn.lock);

	if (time(NULL) > visdn.open_pending_nextcheck) {

		struct visdn_interface *intf;
		list_for_each_entry(intf, &visdn.ifs, ifs_node) {

			if (intf->open_pending) {
				visdn_debug("Retry opening interface %s\n",
						intf->name);

				if (visdn_open_interface(intf) < 0)
					visdn.open_pending_nextcheck = time(NULL) + 2;
			}
		}

		refresh_polls_list();
	}

	int i;
	for(i = 0; i < visdn.npolls; i++) {
		if (visdn.poll_infos[i].type == POLL_INFO_TYPE_NETLINK) {
			if (visdn.polls[i].revents &
					(POLLIN | POLLPRI | POLLERR |
					 POLLHUP | POLLNVAL)) {
				visdn_netlink_receive();
				break; // polls list may have been changed
			}
		} else if (visdn.poll_infos[i].type == POLL_INFO_TYPE_INTERFACE) {
			if (visdn.polls[i].revents &
					(POLLIN | POLLPRI | POLLERR |
					 POLLHUP | POLLNVAL)) {
				visdn_accept(
					visdn.poll_infos[i].interface,
					visdn.polls[i].fd);
				break; // polls list may have been changed
			}
		} else if (visdn.poll_infos[i].type == POLL_INFO_TYPE_DLC) {
			if (visdn.polls[i].revents &
					(POLLIN | POLLPRI | POLLERR |
					 POLLHUP | POLLNVAL)) {

				int err;
				err = q931_receive(visdn.poll_infos[i].dlc);

				if (err == Q931_RECEIVE_REFRESH) {
					refresh_polls_list();

					break;
				}
			}
		}
	}

	int active_calls_cnt = 0;
	if (visdn.have_to_exit) {
		active_calls_cnt = 0;

		struct visdn_interface *intf;
		list_for_each_entry(intf, &visdn.ifs, ifs_node) {
			if (intf->q931_intf) {
				struct q931_call *call;
				list_for_each_entry(call,
						&intf->q931_intf->calls,
						calls_node)
					active_calls_cnt++;
			}
		}

		opbx_log(LOG_WARNING,
			"There are still %d active calls, waiting...\n",
			active_calls_cnt);
	}

	opbx_mutex_unlock(&visdn.lock);

	return (!visdn.have_to_exit || active_calls_cnt > 0);
}


static void *visdn_q931_thread_main(void *data)
{
	opbx_mutex_lock(&visdn.lock);

	visdn.npolls = 0;
	refresh_polls_list();

	visdn.have_to_exit = 0;

	opbx_mutex_unlock(&visdn.lock);

	while(visdn_q931_thread_do_poll());

	return NULL;
}

#ifdef DEBUG_CODE
#define FUNC_DEBUG()					\
	if (visdn.debug)				\
		opbx_verbose(VERBOSE_PREFIX_3		\
			"%s\n", __FUNCTION__);
#else
#define FUNC_DEBUG() do {} while(0)
#endif

static void visdn_q931_alerting_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(q931_call);

	if (!opbx_chan)
		return;

	opbx_queue_control(opbx_chan, OPBX_CONTROL_RINGING);
	opbx_setstate(opbx_chan, OPBX_STATE_RINGING);
}

static void visdn_q931_connect_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	q931_setup_complete_request(q931_call, NULL);

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(q931_call);

	if (!opbx_chan)
		return;

	opbx_queue_control(opbx_chan, OPBX_CONTROL_ANSWER);
}

static void visdn_q931_disconnect_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(q931_call);

	if (opbx_chan) {
		opbx_softhangup(opbx_chan, OPBX_SOFTHANGUP_DEV);
	}

	q931_release_request(q931_call, NULL);
}

static void visdn_q931_error_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();
}

static int visdn_handle_called_number(
	struct visdn_chan *visdn_chan,
	const struct q931_ie_called_party_number *ie)
{
	if (strlen(visdn_chan->called_number) + strlen(ie->number) - 1 >
			sizeof(visdn_chan->called_number)) {
		opbx_log(LOG_NOTICE,
			"Called number overflow\n");

		return FALSE;
	}

	if (ie->number[strlen(ie->number) - 1] == '#') {
		visdn_chan->sending_complete = TRUE;
		strncat(visdn_chan->called_number, ie->number,
			strlen(ie->number)-1);
	} else {
		strcat(visdn_chan->called_number, ie->number);
	}

	return TRUE;
}

static void visdn_q931_info_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(q931_call);

	if (!opbx_chan)
		return;

	struct visdn_chan *visdn_chan = opbx_chan->pvt->pvt;
	struct visdn_interface *intf = q931_call->intf->pvt;

	if (q931_call->state != U2_OVERLAP_SENDING &&
	    q931_call->state != N2_OVERLAP_SENDING) {
		opbx_log(LOG_WARNING, "Received info not in overlap sending\n");
		return;
	}

	struct q931_ie_called_party_number *cdpn = NULL;

	int i;
	for(i=0; i<ies->count; i++) {
		if (ies->ies[i]->type->id == Q931_IE_SENDING_COMPLETE) {
			visdn_chan->sending_complete = TRUE;
		} else if (ies->ies[i]->type->id == Q931_IE_CALLED_PARTY_NUMBER) {
			cdpn = container_of(ies->ies[i],
				struct q931_ie_called_party_number, ie);
		}
	}

	if (opbx_chan->pbx) {
		if (!cdpn)
			return;

opbx_log(LOG_WARNING, "Trying to send DTMF FRAME\n");

		for(i=0; cdpn->number[i]; i++) {
			struct opbx_frame f = { OPBX_FRAME_DTMF, cdpn->number[i] };
			opbx_queue_frame(opbx_chan, &f);
		}

		return;
	}

	if (!visdn_handle_called_number(visdn_chan, cdpn)) {
                struct q931_ies ies = Q931_IES_INIT;

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(q931_call);
		cause->value = Q931_IE_C_CV_INVALID_NUMBER_FORMAT;
		q931_ies_add_put(&ies, &cause->ie);

		q931_disconnect_request(q931_call, &ies);

		return;
	}

	opbx_setstate(opbx_chan, OPBX_STATE_DIALING);

	if (visdn_chan->sending_complete) {
		if (opbx_exists_extension(NULL, intf->context,
				visdn_chan->called_number, 1,
				visdn_chan->calling_number)) {

			strncpy(opbx_chan->exten,
				visdn_chan->called_number,
				sizeof(opbx_chan->exten)-1);

			opbx_setstate(opbx_chan, OPBX_STATE_RINGING);

			visdn_chan->pbx_started = TRUE;

			if (opbx_pbx_start(opbx_chan)) {
				opbx_log(LOG_ERROR,
					"Unable to start PBX on %s\n",
					opbx_chan->name);
				opbx_hangup(opbx_chan);

		                struct q931_ies ies = Q931_IES_INIT;

				struct q931_ie_cause *cause = q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location = q931_ie_cause_location_call(q931_call);
				cause->value = Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;
				q931_ies_add_put(&ies, &cause->ie);

				q931_disconnect_request(q931_call, &ies);
			} else {
				q931_proceeding_request(q931_call, NULL);

				opbx_setstate(opbx_chan, OPBX_STATE_RING);
			}
		} else {
	                struct q931_ies ies = Q931_IES_INIT;

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(q931_call);
			cause->value = Q931_IE_C_CV_UNALLOCATED_NUMBER;
			q931_ies_add_put(&ies, &cause->ie);

			q931_disconnect_request(q931_call, &ies);
		}
	} else {
		if (!opbx_canmatch_extension(NULL, intf->context,
				visdn_chan->called_number, 1,
				visdn_chan->calling_number)) {

	                struct q931_ies ies = Q931_IES_INIT;

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(q931_call);
			cause->value = Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION;
			q931_ies_add_put(&ies, &cause->ie);

			q931_disconnect_request(q931_call, &ies);

			return;
		}

		if (opbx_exists_extension(NULL, intf->context,
				visdn_chan->called_number, 1,
				visdn_chan->calling_number)) {

			strncpy(opbx_chan->exten,
				visdn_chan->called_number,
				sizeof(opbx_chan->exten)-1);

			opbx_setstate(opbx_chan, OPBX_STATE_RINGING);

			visdn_chan->pbx_started = TRUE;

			if (opbx_pbx_start(opbx_chan)) {
				opbx_log(LOG_ERROR,
					"Unable to start PBX on %s\n",
					opbx_chan->name);
				opbx_hangup(opbx_chan);

		                struct q931_ies ies = Q931_IES_INIT;

				struct q931_ie_cause *cause = q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location = q931_ie_cause_location_call(q931_call);
				cause->value = Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;
				q931_ies_add_put(&ies, &cause->ie);

				q931_disconnect_request(q931_call, &ies);
			}

			if (!opbx_matchmore_extension(NULL, intf->context,
					visdn_chan->called_number, 1,
					visdn_chan->calling_number)) {

				q931_proceeding_request(q931_call, NULL);
			}
		}
	}
}

static void visdn_q931_more_info_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();
}

static void visdn_q931_notify_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();
}

static void visdn_q931_proceeding_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(q931_call);

	if (!opbx_chan)
		return;

	opbx_queue_control(opbx_chan, OPBX_CONTROL_PROCEEDING);
}

static void visdn_q931_progress_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(q931_call);

	if (!opbx_chan)
		return;

	opbx_queue_control(opbx_chan, OPBX_CONTROL_PROGRESS);
}

static void visdn_q931_reject_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(q931_call);

	if (!opbx_chan)
		return;

	opbx_softhangup(opbx_chan, OPBX_SOFTHANGUP_DEV);
}

static void visdn_q931_release_confirm(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_release_confirm_status status)
{
	FUNC_DEBUG();

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(q931_call);

	if (!opbx_chan)
		return;

	struct visdn_chan *visdn_chan = opbx_chan->pvt->pvt;

	if (visdn_chan->pbx_started)
		opbx_softhangup(opbx_chan, OPBX_SOFTHANGUP_DEV);
	else
		opbx_hangup(opbx_chan);
}

static void visdn_q931_release_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(q931_call);

	if (!opbx_chan)
		return;

	struct visdn_chan *visdn_chan = opbx_chan->pvt->pvt;

	if (visdn_chan->pbx_started)
		opbx_softhangup(opbx_chan, OPBX_SOFTHANGUP_DEV);
	else
		opbx_hangup(opbx_chan);
}

static void visdn_q931_resume_confirm(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_resume_confirm_status status)
{
	FUNC_DEBUG();
}

static void visdn_q931_resume_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	enum q931_ie_cause_value cause;

	if (callpvt_to_opbxchan(q931_call)) {
		opbx_log(LOG_WARNING, "Unexpexted opbx_chan\n");
		cause = Q931_IE_C_CV_RESOURCES_UNAVAILABLE;
		goto err_opbx_chan;
	}

	struct q931_ie_call_identity *ci = NULL;

	int i;
	for (i=0; i<ies->count; i++) {
		if (ies->ies[i]->type->id == Q931_IE_CALL_IDENTITY) {
			ci = container_of(ies->ies[i],
				struct q931_ie_call_identity, ie);
		}
	}

	struct visdn_interface *intf = q931_call->intf->pvt;
	struct visdn_suspended_call *suspended_call;

	int found = FALSE;
	list_for_each_entry(suspended_call, &intf->suspended_calls, node) {
		if ((!ci && suspended_call->call_identity_len == 0) ||
		    (suspended_call->call_identity_len == ci->data_len &&
		     !memcmp(suspended_call->call_identity, ci->data,
					ci->data_len))) {

			found = TRUE;

			break;
		}
	}

	if (!found) {
		opbx_log(LOG_NOTICE, "Unable to find suspended call\n");

		if (list_empty(&intf->suspended_calls))
			cause = Q931_IE_C_CV_SUSPENDED_CALL_EXISTS_BUT_NOT_THIS;
		else
			cause = Q931_IE_C_CV_NO_CALL_SUSPENDED;

		goto err_call_not_found;
	}

	assert(suspended_call->opbx_chan);

	struct visdn_chan *visdn_chan = suspended_call->opbx_chan->pvt->pvt;

	q931_call->pvt = suspended_call->opbx_chan;
	visdn_chan->q931_call = q931_call;
	visdn_chan->suspended_call = NULL;

	if (!strcmp(suspended_call->opbx_chan->bridge->type, VISDN_CHAN_TYPE)) {
		// Wow, the remote channel is ISDN too, let's notify it!

		struct q931_ies response_ies = Q931_IES_INIT;

		struct visdn_chan *remote_visdn_chan =
				suspended_call->opbx_chan->bridge->pvt->pvt;

		struct q931_call *remote_call = remote_visdn_chan->q931_call;

		struct q931_ie_notification_indicator *notify =
			q931_ie_notification_indicator_alloc();
		notify->description = Q931_IE_NI_D_USER_RESUMED;
		q931_ies_add_put(&response_ies, &notify->ie);

		q931_notify_request(remote_call, &response_ies);
	}

	opbx_moh_stop(suspended_call->opbx_chan->bridge);
	q931_resume_response(q931_call, suspended_call->q931_chan, NULL);

	list_del(&suspended_call->node);
	free(suspended_call);

	return;

err_call_not_found:
err_opbx_chan:
	;
	struct q931_ies resp_ies = Q931_IES_INIT;
	struct q931_ie_cause *c = q931_ie_cause_alloc();
	c->coding_standard = Q931_IE_C_CS_CCITT;
	c->location = q931_ie_cause_location_call(q931_call);
	c->value = cause;
	q931_ies_add_put(&resp_ies, &c->ie);

	q931_resume_reject_request(q931_call, &resp_ies);

	return;
}

static void visdn_q931_setup_complete_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_setup_complete_indication_status status)
{
	FUNC_DEBUG();
}

static void visdn_q931_setup_confirm(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_setup_confirm_status status)
{
	FUNC_DEBUG();

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(q931_call);

	if (!opbx_chan)
		return;

	opbx_setstate(opbx_chan, OPBX_STATE_UP);
}

static void visdn_q931_setup_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct visdn_chan *visdn_chan;
	visdn_chan = visdn_alloc();
	if (!visdn_chan) {
		opbx_log(LOG_ERROR, "Cannot allocate visdn_chan\n");
		goto err_visdn_alloc;
	}

	visdn_chan->q931_call = q931_call_get(q931_call);

	struct visdn_interface *intf = q931_call->intf->pvt;

	int i;
	for(i=0; i<ies->count; i++) {
		if (ies->ies[i]->type->id == Q931_IE_SENDING_COMPLETE) {
			visdn_chan->sending_complete = TRUE;
		} else if (ies->ies[i]->type->id == Q931_IE_CALLED_PARTY_NUMBER) {
			struct q931_ie_called_party_number *cdpn =
				container_of(ies->ies[i],
					struct q931_ie_called_party_number, ie);

			if (!visdn_handle_called_number(visdn_chan, cdpn)) {

		                struct q931_ies ies = Q931_IES_INIT;

				struct q931_ie_cause *cause = q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location = q931_ie_cause_location_call(q931_call);
				cause->value = Q931_IE_C_CV_INVALID_NUMBER_FORMAT;
				q931_ies_add_put(&ies, &cause->ie);

				q931_reject_request(q931_call, &ies);

				return;
			}
		} else if (ies->ies[i]->type->id == Q931_IE_CALLING_PARTY_NUMBER) {
			struct q931_ie_calling_party_number *cgpn =
				container_of(ies->ies[i],
					struct q931_ie_calling_party_number, ie);

			const char *prefix = "";
			if (cgpn->type_of_number ==
					Q931_IE_CDPN_TON_NATIONAL)
				prefix = intf->national_prefix;
			else if (cgpn->type_of_number ==
					Q931_IE_CDPN_TON_INTERNATIONAL)
				prefix = intf->international_prefix;

			snprintf(visdn_chan->calling_number,
				sizeof(visdn_chan->calling_number),
				"<%s%s>", prefix, cgpn->number);

		} else if (ies->ies[i]->type->id == Q931_IE_BEARER_CAPABILITY) {

			// We should check the destination bearer capability
			// unfortunately we don't know if the destination is
			// compatible until we start the PBX... this is a
			// design flaw in Asterisk

			struct q931_ie_bearer_capability *bc =
				container_of(ies->ies[i],
					struct q931_ie_bearer_capability, ie);

			if (bc->information_transfer_capability ==
				Q931_IE_BC_ITC_UNRESTRICTED_DIGITAL) {

				visdn_chan->is_voice = FALSE;
				q931_call->tones_option = FALSE;

			} else  if (bc->information_transfer_capability ==
					Q931_IE_BC_ITC_SPEECH ||
				    bc->information_transfer_capability ==
					Q931_IE_BC_ITC_3_1_KHZ_AUDIO) {

				visdn_chan->is_voice = TRUE;
				q931_call->tones_option = intf->tones_option;
			} else {
		                struct q931_ies ies = Q931_IES_INIT;

				struct q931_ie_cause *cause = q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location =
					q931_ie_cause_location_call(q931_call);
				cause->value =
					Q931_IE_C_CV_BEARER_CAPABILITY_NOT_IMPLEMENTED;
				q931_ies_add_put(&ies, &cause->ie);

				q931_reject_request(q931_call, &ies);

				return;
			}
		}
	}

	struct opbx_channel *opbx_chan;
	opbx_chan = visdn_new(visdn_chan, OPBX_STATE_OFFHOOK);
	if (!opbx_chan)
		goto err_visdn_new;

	q931_call->pvt = opbx_chan;

	snprintf(opbx_chan->name, sizeof(opbx_chan->name), "VISDN/%s/%c%ld",
		q931_call->intf->name,
		q931_call->direction == Q931_CALL_DIRECTION_INBOUND ? 'I' : 'O',
		q931_call->call_reference);

	strncpy(opbx_chan->context,
		intf->context,
		sizeof(opbx_chan->context)-1);

	opbx_mutex_lock(&usecnt_lock);
	visdn.usecnt++;
	opbx_mutex_unlock(&usecnt_lock);
	opbx_update_use_count();

	if (strlen(visdn_chan->calling_number) &&
	    !intf->force_inbound_caller_id)
		opbx_chan->callerid = strdup(visdn_chan->calling_number);
	else
		opbx_chan->callerid = strdup(intf->default_inbound_caller_id);

	if (!intf->overlap_sending ||
	    visdn_chan->sending_complete) {
		if (opbx_exists_extension(NULL, intf->context,
				visdn_chan->called_number, 1,
				visdn_chan->calling_number)) {

			strncpy(opbx_chan->exten,
				visdn_chan->called_number,
				sizeof(opbx_chan->exten)-1);

			opbx_setstate(opbx_chan, OPBX_STATE_RING);

			visdn_chan->pbx_started = TRUE;

			if (opbx_pbx_start(opbx_chan)) {
				opbx_log(LOG_ERROR,
					"Unable to start PBX on %s\n",
					opbx_chan->name);
				opbx_hangup(opbx_chan);

		                struct q931_ies ies = Q931_IES_INIT;

				struct q931_ie_cause *cause = q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location = q931_ie_cause_location_call(q931_call);
				cause->value = Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;
				q931_ies_add_put(&ies, &cause->ie);

				q931_reject_request(q931_call, &ies);
			} else {
				q931_proceeding_request(q931_call, NULL);

				opbx_setstate(opbx_chan, OPBX_STATE_RING);
			}
		} else {
			opbx_log(LOG_NOTICE,
				"No extension '%s' in context '%s',"
				" rejecting call\n",
				visdn_chan->called_number,
				intf->context);

			opbx_hangup(opbx_chan);

	                struct q931_ies ies = Q931_IES_INIT;

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(q931_call);
			cause->value = Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION;
			q931_ies_add_put(&ies, &cause->ie);

			q931_reject_request(q931_call, &ies);
		}

	} else {
		if (!opbx_canmatch_extension(NULL,
				intf->context,
				visdn_chan->called_number, 1,
				visdn_chan->calling_number)) {

                        struct q931_ies ies_proc = Q931_IES_INIT;
			if (visdn_chan->is_voice) {
				struct q931_ie_progress_indicator *pi =
					q931_ie_progress_indicator_alloc();
				pi->coding_standard = Q931_IE_PI_CS_CCITT;
				pi->location = q931_ie_progress_indicator_location(
							visdn_chan->q931_call);
				pi->progress_description =
					Q931_IE_PI_PD_IN_BAND_INFORMATION;
				q931_ies_add_put(&ies_proc, &pi->ie);
			}

		        struct q931_ies ies_disc = Q931_IES_INIT;
			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(q931_call);
			cause->value = Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION;
			cause->value = Q931_IE_C_CV_NETWORK_OUT_OF_ORDER;
			q931_ies_add_put(&ies_disc, &cause->ie);

			q931_proceeding_request(q931_call, &ies_proc);
			q931_disconnect_request(q931_call, &ies_disc);

			return;
		}

		if (opbx_exists_extension(NULL, intf->context,
				visdn_chan->called_number, 1,
				visdn_chan->calling_number)) {

			strncpy(opbx_chan->exten,
				visdn_chan->called_number,
				sizeof(opbx_chan->exten)-1);

			opbx_setstate(opbx_chan, OPBX_STATE_RING);

			visdn_chan->pbx_started = TRUE;

			if (opbx_pbx_start(opbx_chan)) {
				opbx_log(LOG_ERROR,
					"Unable to start PBX on %s\n",
					opbx_chan->name);
				opbx_hangup(opbx_chan);

		                struct q931_ies ies_proc = Q931_IES_INIT;
				struct q931_ie_cause *cause = q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location = q931_ie_cause_location_call(q931_call);
				cause->value = Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;
				q931_ies_add_put(&ies_proc, &cause->ie);

                                struct q931_ies ies_disc = Q931_IES_INIT;
				if (visdn_chan->is_voice) {
					struct q931_ie_progress_indicator *pi =
						q931_ie_progress_indicator_alloc();
					pi->coding_standard = Q931_IE_PI_CS_CCITT;
					pi->location = q931_ie_progress_indicator_location(
								visdn_chan->q931_call);
					pi->progress_description =
						Q931_IE_PI_PD_IN_BAND_INFORMATION;
					q931_ies_add_put(&ies_disc, &pi->ie);
				}

				q931_proceeding_request(q931_call, &ies_proc);
				q931_disconnect_request(q931_call, &ies_disc);
			} else {
				if (!opbx_matchmore_extension(NULL, intf->context,
						visdn_chan->called_number, 1,
						visdn_chan->calling_number)) {

					q931_proceeding_request(q931_call, NULL);
				} else {
					q931_more_info_request(q931_call, NULL);
				}
			}
		} else {
			q931_more_info_request(q931_call, NULL);
		}
	}

	return;

err_visdn_new:
	// Free visdn_chan
err_visdn_alloc:
;
}

static void visdn_q931_status_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_status_indication_status status)
{
	FUNC_DEBUG();
}

static void visdn_q931_suspend_confirm(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_suspend_confirm_status status)
{
	FUNC_DEBUG();
}

struct visdn_dual {
	struct opbx_channel *chan1;
	struct opbx_channel *chan2;
};

static void visdn_q931_suspend_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(q931_call);

	enum q931_ie_cause_value cause;

	if (!opbx_chan) {
		opbx_log(LOG_WARNING, "Unexpexted opbx_chan\n");
		cause = Q931_IE_C_CV_RESOURCES_UNAVAILABLE;
		goto err_opbx_chan;
	}

	struct q931_ie_call_identity *ci = NULL;

	int i;
	for (i=0; i<ies->count; i++) {
		if (ies->ies[i]->type->id == Q931_IE_CALL_IDENTITY) {
			ci = container_of(ies->ies[i],
				struct q931_ie_call_identity, ie);
		}
	}

	struct visdn_interface *intf = q931_call->intf->pvt;
	struct visdn_suspended_call *suspended_call;
	list_for_each_entry(suspended_call, &intf->suspended_calls, node) {
		if ((!ci && suspended_call->call_identity_len == 0) ||
		    (ci && suspended_call->call_identity_len == ci->data_len &&
		     !memcmp(suspended_call->call_identity,
					ci->data, ci->data_len))) {

			cause = Q931_IE_C_CV_CALL_IDENITY_IN_USE;

			goto err_call_identity_in_use;
		}
	}

	suspended_call = malloc(sizeof(*suspended_call));
	if (!suspended_call) {
		cause = Q931_IE_C_CV_RESOURCES_UNAVAILABLE;
		goto err_suspend_alloc;
	}

	suspended_call->opbx_chan = opbx_chan;
	suspended_call->q931_chan = q931_call->channel;

	if (ci) {
		suspended_call->call_identity_len = ci->data_len;
		memcpy(suspended_call->call_identity, ci->data, ci->data_len);
	} else {
		suspended_call->call_identity_len = 0;
	}

	suspended_call->old_when_to_hangup = opbx_chan->whentohangup;

	list_add_tail(&suspended_call->node, &intf->suspended_calls);

	q931_suspend_response(q931_call, NULL);

	assert(opbx_chan->bridge);

	opbx_moh_start(opbx_chan->bridge, NULL);

	if (!strcmp(opbx_chan->bridge->type, VISDN_CHAN_TYPE)) {
		// Wow, the remote channel is ISDN too, let's notify it!

		struct q931_ies response_ies = Q931_IES_INIT;

		struct visdn_chan *remote_visdn_chan =
					opbx_chan->bridge->pvt->pvt;

		struct q931_call *remote_call = remote_visdn_chan->q931_call;

		struct q931_ie_notification_indicator *notify =
			q931_ie_notification_indicator_alloc();
		notify->description = Q931_IE_NI_D_USER_SUSPENDED;
		q931_ies_add_put(&response_ies, &notify->ie);

		q931_notify_request(remote_call, &response_ies);
	}

	struct visdn_chan *visdn_chan = opbx_chan->pvt->pvt;

	if (!opbx_chan->whentohangup ||
	    time(NULL) + 45 < opbx_chan->whentohangup)
		opbx_channel_setwhentohangup(opbx_chan, intf->T307);

	q931_call->pvt = NULL;
	visdn_chan->q931_call = NULL;
	visdn_chan->suspended_call = suspended_call;
	q931_call_put(q931_call);

	return;

err_suspend_alloc:
err_call_identity_in_use:
err_opbx_chan:
	;
	struct q931_ies resp_ies = Q931_IES_INIT;
	struct q931_ie_cause *c = q931_ie_cause_alloc();
	c->coding_standard = Q931_IE_C_CS_CCITT;
	c->location = q931_ie_cause_location_call(q931_call);
	c->value = cause;
	q931_ies_add_put(&resp_ies, &c->ie);

	q931_suspend_reject_request(q931_call, &resp_ies);

	return;
}

static void visdn_q931_timeout_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	FUNC_DEBUG();
}

static void visdn_q931_connect_channel(struct q931_channel *channel)
{
	FUNC_DEBUG();

	assert(channel);
	assert(channel->call);

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(channel->call);

	if (!opbx_chan)
		return;

	opbx_mutex_lock(&opbx_chan->lock);
	assert(opbx_chan->pvt);
	assert(opbx_chan->pvt->pvt);

	struct visdn_chan *visdn_chan = opbx_chan->pvt->pvt;

	char path[100], dest[100];
	snprintf(path, sizeof(path),
		"/sys/class/net/%s/visdn_channel/connected/../B%d",
		channel->intf->name,
		channel->id+1);

	memset(dest, 0, sizeof(dest));
	if (readlink(path, dest, sizeof(dest) - 1) < 0) {
		opbx_log(LOG_ERROR, "readlink(%s): %s\n", path, strerror(errno));
		goto err_readlink;
	}

	char *chanid = strrchr(dest, '/');
	if (!chanid || !strlen(chanid + 1)) {
		opbx_log(LOG_ERROR,
			"Invalid chanid found in symlink %s\n",
			dest);
		goto err_invalid_chanid;
	}

	strncpy(visdn_chan->visdn_chanid, chanid + 1,
		sizeof(visdn_chan->visdn_chanid));

	if (visdn_chan->is_voice) {
		visdn_debug("Connecting streamport to chan %s\n",
				visdn_chan->visdn_chanid);

		visdn_chan->channel_fd = open("/dev/visdn/streamport", O_RDWR);
		if (visdn_chan->channel_fd < 0) {
			opbx_log(LOG_ERROR,
				"Cannot open streamport: %s\n",
				strerror(errno));
			goto err_open;
		}

		struct visdn_connect vc;
		strcpy(vc.src_chanid, "");
		snprintf(vc.dst_chanid, sizeof(vc.dst_chanid), "%s",
			visdn_chan->visdn_chanid);
		vc.flags = 0;

		if (ioctl(visdn_chan->channel_fd, VISDN_IOC_CONNECT,
		    (caddr_t) &vc) < 0) {
			opbx_log(LOG_ERROR,
				"ioctl(VISDN_CONNECT): %s\n",
				strerror(errno));
			goto err_ioctl;
		}
	}

	opbx_mutex_unlock(&opbx_chan->lock);

	return;

err_ioctl:
err_open:
err_invalid_chanid:
err_readlink:

	opbx_mutex_unlock(&opbx_chan->lock);
}

static void visdn_q931_disconnect_channel(struct q931_channel *channel)
{
	FUNC_DEBUG();

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(channel->call);

	if (!opbx_chan)
		return;

	// FIXME
#if 0
	if (opbx_chan->generator)
		visdn_generator_stop(opbx_chan);
#endif
	struct visdn_chan *visdn_chan = opbx_chan->pvt->pvt;

	opbx_mutex_lock(&opbx_chan->lock);

	if (visdn_chan->channel_fd >= 0) {
		if (ioctl(visdn_chan->channel_fd,
				VISDN_IOC_DISCONNECT, NULL) < 0) {
			opbx_log(LOG_ERROR,
				"ioctl(VISDN_IOC_DISCONNECT): %s\n",
				strerror(errno));
		}

		if (close(visdn_chan->channel_fd) < 0) {
			opbx_log(LOG_ERROR,
				"close(visdn_chan->channel_fd): %s\n",
				strerror(errno));
		}

		visdn_chan->channel_fd = -1;
	}

	opbx_mutex_unlock(&opbx_chan->lock);
}

static pthread_t visdn_generator_thread = OPBX_PTHREADT_NULL;
OPBX_MUTEX_DEFINE_STATIC(visdn_generator_lock);
static struct opbx_channel *gen_chans[256];
static int gen_chans_num = 0;

#if 0

CMANTUNES: With the new channel generator thread, this is no longer necessary

static void *visdn_generator_thread_main(void *aaa)
{
	struct opbx_frame f;
	__u8 buf[256];

	f.frametype = OPBX_FRAME_VOICE;
	f.subclass = OPBX_FORMAT_ALAW;
	f.samples = 80;
	f.datalen = 80;
	f.data = buf;
	f.offset = 0;
	f.src = VISDN_CHAN_TYPE;
	f.mallocd = 0;
	f.delivery.tv_sec = 0;
	f.delivery.tv_usec = 0;

	visdn_debug("Generator thread started\n");

	while (gen_chans_num) {
		struct opbx_channel *chan;

		struct opbx_channel *gen_chans_copy[256];
		int gen_chans_copy_num = 0;

		opbx_mutex_lock(&visdn_generator_lock);
		gen_chans_copy_num = gen_chans_num;
		memcpy(gen_chans_copy, gen_chans,
			sizeof(*gen_chans_copy) * gen_chans_copy_num);
		opbx_mutex_unlock(&visdn_generator_lock);

		int ms = 500;
		chan = opbx_waitfor_n(gen_chans_copy, gen_chans_copy_num, &ms);
		if (chan) {
			void *tmp;
			int res;

			opbx_mutex_lock(&chan->lock);

			if (chan->generator) {
				tmp = chan->generatordata;
				chan->generatordata = NULL;
				res = chan->generator->generate(
					chan, tmp, f.datalen, f.samples);
				chan->generatordata = tmp;
				if (res) {
				        opbx_log(LOG_DEBUG,
						"Auto-deactivating"
						" generator\n");
				        opbx_generator_deactivate(chan);
				}
			}

			opbx_mutex_unlock(&chan->lock);
		}
	}

	visdn_generator_thread = OPBX_PTHREADT_NULL;

	visdn_debug("Generator thread stopped\n");

	return NULL;
}

static int visdn_generator_start(struct opbx_channel *chan)
{
	int res = -1;

	opbx_mutex_lock(&visdn_generator_lock);

	int i;
	for (i=0; i<gen_chans_num; i++) {
		if (gen_chans[i] == chan)
			goto already_generating;
	}

	if (gen_chans_num > sizeof(gen_chans)) {
		opbx_log(LOG_WARNING, "MAX 256 chans in dialtone generation\n");
		goto err_too_many_channels;
	}

	gen_chans[gen_chans_num] = chan;
	gen_chans_num++;

	if (visdn_generator_thread == OPBX_PTHREADT_NULL) {
		if (opbx_pthread_create(&visdn_generator_thread, NULL,
				visdn_generator_thread_main, NULL)) {
			opbx_log(LOG_WARNING,
				"Unable to create autoservice thread :(\n");
		} else
			pthread_kill(visdn_generator_thread, SIGURG);
	}

err_too_many_channels:
already_generating:
	opbx_mutex_unlock(&visdn_generator_lock);

	return res;
}

static int visdn_generator_stop(struct opbx_channel *chan)
{
	opbx_mutex_lock(&visdn_generator_lock);

	int i;
	for (i=0; i<gen_chans_num; i++) {
		if (gen_chans[i] == chan) {
			int j;
			for (j=i; j<gen_chans_num-1; j++)
				gen_chans[j] = gen_chans[j+1];

			break;
		}
	}

	if (i == gen_chans_num)
		goto err_chan_not_found;

	gen_chans_num--;


	if (gen_chans_num == 0 &&
	    visdn_generator_thread != OPBX_PTHREADT_NULL) {
		pthread_kill(visdn_generator_thread, SIGURG);
	}

	opbx_mutex_unlock(&visdn_generator_lock);

	/* Wait for it to un-block */
	while(chan->blocking)
		usleep(1000);

	return 0;

err_chan_not_found:

	opbx_mutex_unlock(&visdn_generator_lock);

	return 0;
}
#endif

static void visdn_q931_start_tone(struct q931_channel *channel,
	enum q931_tone_type tone)
{
	FUNC_DEBUG();

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(channel->call);

	// Unfortunately, after opbx_hangup the channel is not valid
	// anymore and we cannot generate further tones thought we should
	if (!opbx_chan)
		return;

	switch (tone) {
	case Q931_TONE_DIAL:
		opbx_indicate(opbx_chan, OPBX_CONTROL_OFFHOOK);
	break;

	case Q931_TONE_HANGUP:
		opbx_indicate(opbx_chan, OPBX_CONTROL_HANGUP);
	break;

	case Q931_TONE_BUSY:
		opbx_indicate(opbx_chan, OPBX_CONTROL_BUSY);
	break;

	case Q931_TONE_FAILURE:
		opbx_indicate(opbx_chan, OPBX_CONTROL_CONGESTION);
	break;
	default:;
	}
}

static void visdn_q931_stop_tone(struct q931_channel *channel)
{
	FUNC_DEBUG();

	struct opbx_channel *opbx_chan = callpvt_to_opbxchan(channel->call);

	if (!opbx_chan)
		return;

	opbx_indicate(opbx_chan, -1);
}

static void visdn_q931_management_restart_confirm(
	struct q931_global_call *gc,
	const struct q931_chanset *chanset)
{
	FUNC_DEBUG();
}

static void visdn_q931_timeout_management_indication(
	struct q931_global_call *gc)
{
	FUNC_DEBUG();
}

static void visdn_q931_status_management_indication(
	struct q931_global_call *gc)
{
	FUNC_DEBUG();
}

static void visdn_logger(int level, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	char msg[200];
	vsnprintf(msg, sizeof(msg), format, ap);
	va_end(ap);

	switch(level) {
	case Q931_LOG_DEBUG:
		if (visdn.debug_q931)
			opbx_verbose(VERBOSE_PREFIX_3 "%s", msg);
	break;

	case Q931_LOG_INFO:
		opbx_verbose(VERBOSE_PREFIX_3  "%s", msg);
	break;

	case Q931_LOG_NOTICE:
		opbx_log(__LOG_NOTICE, "libq931", 0, "", "%s", msg);
	break;

	case Q931_LOG_WARNING:
		opbx_log(__LOG_WARNING, "libq931", 0, "", "%s", msg);
	break;

	case Q931_LOG_ERR:
	case Q931_LOG_CRIT:
	case Q931_LOG_ALERT:
	case Q931_LOG_EMERG:
		opbx_log(__LOG_ERROR, "libq931", 0, "", "%s", msg);
	break;
	}
}

void visdn_q931_timer_update(struct q931_lib *lib)
{
	pthread_kill(visdn_q931_thread, SIGURG);
}

int load_module()
{
	int res = 0;

	// Initialize q.931 library.
	// No worries, internal structures are read-only and thread safe
	opbx_mutex_init(&visdn.lock);

	INIT_LIST_HEAD(&visdn.ifs);

	visdn.libq931 = q931_init();
	q931_set_logger_func(visdn.libq931, visdn_logger);

	visdn.libq931->pvt =
	visdn.libq931->timer_update = visdn_q931_timer_update;

	// Setup all callbacks for libq931 primitives
	visdn.libq931->alerting_indication =
		visdn_q931_alerting_indication;
	visdn.libq931->connect_indication =
		visdn_q931_connect_indication;
	visdn.libq931->disconnect_indication =
		visdn_q931_disconnect_indication;
	visdn.libq931->error_indication =
		visdn_q931_error_indication;
	visdn.libq931->info_indication =
		visdn_q931_info_indication;
	visdn.libq931->more_info_indication =
		visdn_q931_more_info_indication;
	visdn.libq931->notify_indication =
		visdn_q931_notify_indication;
	visdn.libq931->proceeding_indication =
		visdn_q931_proceeding_indication;
	visdn.libq931->progress_indication =
		visdn_q931_progress_indication;
	visdn.libq931->reject_indication =
		visdn_q931_reject_indication;
	visdn.libq931->release_confirm =
		visdn_q931_release_confirm;
	visdn.libq931->release_indication =
		visdn_q931_release_indication;
	visdn.libq931->resume_confirm =
		visdn_q931_resume_confirm;
	visdn.libq931->resume_indication =
		visdn_q931_resume_indication;
	visdn.libq931->setup_complete_indication =
		visdn_q931_setup_complete_indication;
	visdn.libq931->setup_confirm =
		visdn_q931_setup_confirm;
	visdn.libq931->setup_indication =
		visdn_q931_setup_indication;
	visdn.libq931->status_indication =
		visdn_q931_status_indication;
	visdn.libq931->suspend_confirm =
		visdn_q931_suspend_confirm;
	visdn.libq931->suspend_indication =
		visdn_q931_suspend_indication;
	visdn.libq931->timeout_indication =
		visdn_q931_timeout_indication;

	visdn.libq931->connect_channel =
		visdn_q931_connect_channel;
	visdn.libq931->disconnect_channel =
		visdn_q931_disconnect_channel;
	visdn.libq931->start_tone =
		visdn_q931_start_tone;
	visdn.libq931->stop_tone =
		visdn_q931_stop_tone;

	visdn.libq931->management_restart_confirm =
		visdn_q931_management_restart_confirm;
	visdn.libq931->timeout_management_indication =
		visdn_q931_timeout_management_indication;
	visdn.libq931->status_management_indication =
		visdn_q931_status_management_indication;

	visdn_reload_config();

	visdn.timer_fd = open("/dev/visdn/timer", O_RDONLY);
	if (visdn.timer_fd < 0) {
		opbx_log(LOG_ERROR, "Unable to open timer: %s\n",
			strerror(errno));
		return -1;
	}

	visdn.control_fd = open("/dev/visdn/control", O_RDONLY);
	if (visdn.control_fd < 0) {
		opbx_log(LOG_ERROR, "Unable to open control: %s\n",
			strerror(errno));
		return -1;
	}

	visdn.netlink_socket = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if(visdn.netlink_socket < 0) {
		opbx_log(LOG_ERROR, "Unable to open netlink socket: %s\n",
			strerror(errno));
		return -1;
	}

	struct sockaddr_nl snl;
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = RTMGRP_LINK;

	if (bind(visdn.netlink_socket,
			(struct sockaddr *)&snl,
			sizeof(snl)) < 0) {
		opbx_log(LOG_ERROR, "Unable to bind netlink socket: %s\n",
			strerror(errno));
		return -1;
	}

	// Enum interfaces and open them
	struct ifaddrs *ifaddrs;
	struct ifaddrs *ifaddr;

	if (getifaddrs(&ifaddrs) < 0) {
		opbx_log(LOG_ERROR, "getifaddr: %s\n", strerror(errno));
		return -1;
	}

	int fd;
	fd = socket(PF_LAPD, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		opbx_log(LOG_ERROR, "socket: %s\n", strerror(errno));
		return -1;
	}

	for (ifaddr = ifaddrs ; ifaddr; ifaddr = ifaddr->ifa_next) {
		struct ifreq ifreq;

		memset(&ifreq, 0, sizeof(ifreq));

		strncpy(ifreq.ifr_name,
			ifaddr->ifa_name,
			sizeof(ifreq.ifr_name));

		if (ioctl(fd, SIOCGIFHWADDR, &ifreq) < 0) {
			opbx_log(LOG_ERROR, "ioctl (%s): %s\n",
				ifaddr->ifa_name, strerror(errno));
			return -1;
		}

		if (ifreq.ifr_hwaddr.sa_family != ARPHRD_LAPD)
			continue;

		if (!(ifaddr->ifa_flags & IFF_UP))
			continue;

		visdn_add_interface(ifreq.ifr_name);

	}
	close(fd);
	freeifaddrs(ifaddrs);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (opbx_pthread_create(&visdn_q931_thread, &attr,
					visdn_q931_thread_main, NULL) < 0) {
		opbx_log(LOG_ERROR, "Unable to start q931 thread.\n");
		return -1;
	}

	if (opbx_channel_register_ex(VISDN_CHAN_TYPE, VISDN_DESCRIPTION,
			 OPBX_FORMAT_ALAW,
			 visdn_request, visdn_devicestate)) {
		opbx_log(LOG_ERROR, "Unable to register channel class %s\n",
			VISDN_CHAN_TYPE);
		return -1;
	}

	opbx_cli_register(&debug_visdn_generic);
	opbx_cli_register(&no_debug_visdn_generic);
	opbx_cli_register(&debug_visdn_q921);
	opbx_cli_register(&no_debug_visdn_q921);
	opbx_cli_register(&debug_visdn_q931);
	opbx_cli_register(&no_debug_visdn_q931);
	opbx_cli_register(&visdn_reload);
	opbx_cli_register(&show_visdn_channels);
	opbx_cli_register(&show_visdn_interfaces);
	opbx_cli_register(&show_visdn_calls);

	return res;
}

int unload_module(void)
{
	opbx_cli_unregister(&show_visdn_calls);
	opbx_cli_unregister(&show_visdn_interfaces);
	opbx_cli_unregister(&show_visdn_channels);
	opbx_cli_unregister(&visdn_reload);
	opbx_cli_unregister(&no_debug_visdn_q931);
	opbx_cli_unregister(&debug_visdn_q931);
	opbx_cli_unregister(&no_debug_visdn_q921);
	opbx_cli_unregister(&debug_visdn_q921);
	opbx_cli_unregister(&no_debug_visdn_generic);
	opbx_cli_unregister(&debug_visdn_generic);

	opbx_channel_unregister(VISDN_CHAN_TYPE);

	close(visdn.timer_fd);
	close(visdn.control_fd);

	if (visdn.libq931)
		q931_leave(visdn.libq931);

	return 0;
}

int reload(void)
{
	visdn_reload_config();

	return 0;
}

int usecount()
{
	int res;
	opbx_mutex_lock(&usecnt_lock);
	res = visdn.usecnt;
	opbx_mutex_unlock(&usecnt_lock);
	return res;
}

char *description()
{
	return VISDN_DESCRIPTION;
}
