/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Eris Associates Limited, UK
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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

/*
 *
 * Generic support for blacklisting remote addresses
 * 
 */

/* Includes {{{1 */

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/blacklist.h"
#include "callweaver/callweaver_hash.h"
#include "callweaver/cli.h"
#include "callweaver/logger.h"
#include "callweaver/manager.h"
#include "callweaver/sched.h"
#include "callweaver/time.h"
#include "callweaver/utils.h"


/* Structure definition {{{1 */

struct cw_blacklist_entry {
	struct cw_object obj;
	struct cw_registry_entry *reg_entry;
	time_t start;
	unsigned int duration;
	struct sched_state expire;
	/* This must be last */
	struct sockaddr addr;
};


/* Local variables {{{1 */

/* We initially set duration to blacklist_min_duration seconds and set
 * a timer to remove the entry blacklist_remember seconds after the
 * duration expires. If we receive another failing request between the
 * blacklist duration ending and the entry being removed we re-blacklist
 * with double the duration (up to a maximum of blacklist_max_duration
 * seconds) and reschedule the removal.
 *
 * All times are in seconds.
 */
static unsigned int blacklist_min_duration = 60;
static unsigned int blacklist_max_duration = 0;
static unsigned int blacklist_remember = 24 * 60 * 60;


static struct sched_context *sched;


static const struct addrinfo blacklist_addrinfo_hints = {
	.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_IDN,
	.ai_family = AF_UNSPEC,
	.ai_socktype = SOCK_DGRAM,
};


/* Registry {{{1 */


static int blacklist_qsort_compare_by_addr(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct cw_blacklist_entry *entry_a = container_of(*objp_a, struct cw_blacklist_entry, obj);
	const struct cw_blacklist_entry *entry_b = container_of(*objp_b, struct cw_blacklist_entry, obj);

	return cw_sockaddr_cmp(&entry_a->addr, &entry_b->addr, -1, 0);
}


static int blacklist_object_match(struct cw_object *obj, const void *pattern)
{
	const struct cw_blacklist_entry *entry = container_of(obj, struct cw_blacklist_entry, obj);
	const struct sockaddr *addr = pattern;

	return !cw_sockaddr_cmp(&entry->addr, addr, -1, 0);
}


struct cw_registry blacklist_registry = {
	.name = "Blacklist",
	.qsort_compare = blacklist_qsort_compare_by_addr,
	.match = blacklist_object_match,
};


/* Internal functions {{{1 */


static void blacklist_entry_release(struct cw_object *obj)
{
	struct cw_blacklist_entry *entry = container_of(obj, struct cw_blacklist_entry, obj);

	cw_object_destroy(entry);
	free(entry);
}


static int blacklist_entry_remove(void *data)
{
	struct cw_blacklist_entry *entry = data;

	cw_log(CW_LOG_NOTICE, "Blacklist of %@ removed\n", &entry->addr);
	cw_manager_event(CW_EVENT_FLAG_SYSTEM, "Blacklist",
		2,
		cw_msg_tuple("Address", "%@", &entry->addr),
		cw_msg_tuple("State",   "%s", "Removed")
	);

	cw_registry_del(&blacklist_registry, entry->reg_entry);
	cw_object_put(entry);
	return 0;
}


enum blacklist_mode { BLACKLIST_DYNAMIC, BLACKLIST_STATIC, BLACKLIST_DELETE };

static void blacklist_modify(const struct sockaddr *addr, socklen_t addrlen, enum blacklist_mode mode, time_t start, unsigned int duration, unsigned int remember)
{
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	struct sockaddr_in sinbuf;
	struct cw_object *obj;
        struct cw_blacklist_entry *entry = NULL;
	unsigned int hash;

	if (cw_sockaddr_is_mapped(addr)) {
		cw_sockaddr_unmap(&sinbuf, (struct sockaddr_in6 *)addr);
		addr = (struct sockaddr *)&sinbuf;
		addrlen = sizeof(sinbuf);
	}

	hash = cw_sockaddr_hash(addr, 0);

	pthread_mutex_lock(&lock);

        if ((obj = cw_registry_find(&blacklist_registry, 1, hash, addr))) {
		entry = container_of(obj, struct cw_blacklist_entry, obj);

		/* If the cw_sched_del succeeds we own the deleted job's reference
		 * to the entry so can hand it off again when we rescheduled the
		 * expire job.
		 */
		if (!cw_sched_del(sched, &entry->expire)) {
			if (mode == BLACKLIST_DYNAMIC) {
				entry->duration <<= 1;
				if (blacklist_max_duration && entry->duration > blacklist_max_duration)
					entry->duration = blacklist_max_duration;
			} else
				entry->duration = duration;
			entry->start = start;
		} else {
			cw_object_put(entry);
			entry = NULL;
		}
	}

	if (!entry && (entry = malloc(sizeof(*entry) - sizeof(entry->addr) + addrlen))) {
		cw_object_init(entry, NULL, 2);
		entry->obj.release = blacklist_entry_release;

		entry->duration = duration;
		entry->start = start;
		cw_sched_state_init(&entry->expire);
		memcpy(&entry->addr, addr, addrlen);

		entry->reg_entry = cw_registry_add(&blacklist_registry, hash, &entry->obj);
	}

	/* At this stage we own two counted references, either because we created a new
	 * entry with two or because we found an entry (1 ref) and removed the expire
	 * job (which gave us the second ref), so we have one to hand off when scheduling
	 * the expire job and one to release after we're done.
	 */
	if (entry) {
		if (remember || mode == BLACKLIST_DELETE)
			cw_sched_add(sched, &entry->expire, (entry->duration + remember) * 1000, blacklist_entry_remove, entry);
		else
			cw_object_put(entry);
	}

	pthread_mutex_unlock(&lock);

	if (entry && mode != BLACKLIST_DELETE) {
		if (entry->duration) {
			cw_log(CW_LOG_DEBUG, "Blacklisting %@ for %us\n", addr, entry->duration);
			cw_manager_event(CW_EVENT_FLAG_SYSTEM, "Blacklist",
				3,
				cw_msg_tuple("Address",  "%@", addr),
				cw_msg_tuple("State",    "%s", "Blocked"),
				cw_msg_tuple("Duration", "%d", entry->duration)
			);
		} else {
			cw_log(CW_LOG_WARNING, "Blacklisting %@ permanently\n", addr);
			cw_manager_event(CW_EVENT_FLAG_SYSTEM, "Blacklist",
				3,
				cw_msg_tuple("Address",  "%@", addr),
				cw_msg_tuple("State",    "%s", "Blocked"),
				cw_msg_tuple("Duration", "permanent")
			);
		}

		cw_object_put(entry);
	}
}


/* Public API {{{1 */


int cw_blacklist_check(const struct sockaddr *addr)
{
	struct sockaddr_in sinbuf;
	struct cw_object *obj;
	int blocked = 0;

	if (cw_sockaddr_is_mapped(addr)) {
		cw_sockaddr_unmap(&sinbuf, (struct sockaddr_in6 *)addr);
		addr = (struct sockaddr *)&sinbuf;
	}

        if ((obj = cw_registry_find(&blacklist_registry, 1, cw_sockaddr_hash(addr, 0), addr))) {
		struct cw_blacklist_entry *entry = container_of(obj, struct cw_blacklist_entry, obj);
		struct timespec now;

		cw_clock_gettime(global_cond_clock_monotonic, &now);
		blocked = (!entry->duration || now.tv_sec - entry->start < entry->duration);
		cw_object_put(entry);
	}

	return blocked;
}


void cw_blacklist_add(const struct sockaddr *addr)
{
	struct timespec now;

	cw_clock_gettime(global_cond_clock_monotonic, &now);
	blacklist_modify(addr, cw_sockaddr_len(addr), BLACKLIST_DYNAMIC, now.tv_sec, blacklist_min_duration, blacklist_remember);
}


/* CLI support {{{1 */


/* common functions {{{2 */


/* interval handling {{{3 */

static const char *cw_interval_parse(unsigned int *res, const char *s)
{
	*res = 0;

	while (isdigit(*s)) {
		char *p;
		unsigned int n = strtoul(s, &p, 10);
		s = p;

		switch (*s) {
			case 'd':
				s++;
				*res += n * 24 * 60 * 60;
				break;
			case 'h':
				s++;
				*res += n * 60 * 60;
				break;
			case 'm':
				s++;
				*res += n * 60;
				break;
			case 's':
				s++;
			case '\0':
				*res += n;
				break;
			default:
				break;
		}
	}

	return s;
}


static size_t cw_interval_print(struct cw_dynstr *ds_p, unsigned long interval)
{
	static struct {
		char units;
		unsigned long nsecs;
	} map[] = {
		{ 'd', 24 * 60 * 60 },
		{ 'h', 60 * 60 },
		{ 'm', 60 },
		{ 's', 1 }
	};
	size_t start = ds_p->used;
	int printing, i;

	printing = 0;
	for (i = 0; i < arraysize(map); i++) {
		if (interval > map[i].nsecs) {
			cw_dynstr_printf(ds_p, (printing ? "%02lu%c" : "%lu%c"), interval / map[i].nsecs, map[i].units);
			interval %= map[i].nsecs;
			printing = 1;
		} else if (printing)
			cw_dynstr_printf(ds_p, "00%c", map[i].units);
	}

	return ds_p->used - start;
}


/* completion handling {{{3 */

struct complete_blacklist_entries_args {
	struct cw_dynstr *ds_p;
	char *pattern;
	int pattern_len;
};

static int complete_blacklist_entries_one(struct cw_object *obj, void *data)
{
	struct cw_blacklist_entry *entry = container_of(obj, struct cw_blacklist_entry, obj);
	struct complete_blacklist_entries_args *args = data;
	size_t mark = args->ds_p->used;

	cw_dynstr_printf(args->ds_p, "%@", &entry->addr);

	if (!args->pattern || !strncmp(&args->ds_p->data[mark], args->pattern, args->pattern_len))
		cw_dynstr_printf(args->ds_p, "\n");
	else
		cw_dynstr_truncate(args->ds_p, mark);

	return 0;
}

static void complete_blacklist_entries(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	if (lastarg == 2) {
		struct complete_blacklist_entries_args args = {
			.ds_p = ds_p,
			.pattern = argv[lastarg],
			.pattern_len = lastarg_len,
		};

		cw_registry_iterate_ordered(&blacklist_registry, complete_blacklist_entries_one, &args);
	}
}


/* blacklist add {{{2 */

static const char blacklist_add_usage[] =
"Usage: blacklist add address [duration [remember]]\n";


static int blacklist_add(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int res = RESULT_SHOWUSAGE;

	CW_UNUSED(ds_p);

	if (argc >= 3 && argc <= 5) {
		const char *a3, *a4;
		unsigned int duration = 0;
		unsigned int remember = 0;

		a3 = a4 = "";

		if (argc > 3) {
			a3 = cw_interval_parse(&duration, argv[3]);
			if (*a3)
				cw_dynstr_printf(ds_p, "duration not parsable at %s. Intervals should be [[[<n>d]<n>h]<n>m]<n>s\n", a3);
		}

		if (argc > 4) {
			a4 = cw_interval_parse(&remember, argv[4]);
			if (*a4)
				cw_dynstr_printf(ds_p, "remember not parsable at %s. Intervals should be [[[<n>d]<n>h]<n>m]<n>s\n", a4);
		}

		if (!*a3 && !*a4) {
			struct addrinfo *addrs;
			int err;

			if (!(err = getaddrinfo(argv[2], NULL, &blacklist_addrinfo_hints, &addrs))) {
				struct timespec now;
				struct addrinfo *addr;

				cw_clock_gettime(global_cond_clock_monotonic, &now);

				for (addr = addrs; addr; addr = addr->ai_next)
					blacklist_modify(addr->ai_addr, addr->ai_addrlen, BLACKLIST_STATIC, now.tv_sec, duration, remember);

				freeaddrinfo(addrs);
				res = RESULT_SUCCESS;
			} else
				cw_dynstr_printf(ds_p, "%s\n", gai_strerror(err));
		}
	}

	return res;
}


/* blacklist remove {{{2 */

static const char blacklist_remove_usage[] =
"Usage: blacklist remove address\n";


static int blacklist_remove(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int res = RESULT_SHOWUSAGE;

	CW_UNUSED(ds_p);

	if (argc != 3) {
		struct addrinfo *addrs;
		int err;

		if (!(err = getaddrinfo(argv[2], NULL, &blacklist_addrinfo_hints, &addrs))) {
			struct addrinfo *addr;

			for (addr = addrs; addr; addr = addr->ai_next)
				blacklist_modify(addr->ai_addr, addr->ai_addrlen, BLACKLIST_DELETE, 0, 0, 0);

			freeaddrinfo(addrs);
			res = RESULT_SUCCESS;
		} else
			cw_dynstr_printf(ds_p, "%s\n", gai_strerror(err));
	}

	return res;
}


/* blacklist show {{{2 */

static const char blacklist_show_usage[] =
"Usage: blacklist show [address]\n"
"       Lists blacklisted peers.\n";

struct blacklist_show_args {
	struct cw_dynstr *ds_p;
	struct timespec now;
	char *pattern;
	int pattern_len;
};

static int blacklist_show_one(struct cw_object *obj, void *data)
{
	struct cw_blacklist_entry *entry = container_of(obj, struct cw_blacklist_entry, obj);
	struct blacklist_show_args *args = data;
	size_t mark = args->ds_p->used;

	cw_dynstr_printf(args->ds_p, "%@", &entry->addr);

	if (!args->pattern || !strncmp(&args->ds_p->data[mark], args->pattern, args->pattern_len)) {
		int d = 41 - (args->ds_p->used - mark);

		if (d > 0)
			cw_dynstr_printf(args->ds_p, "%*.*s ", d, d, "                                         ");

		if (entry->duration) {
			unsigned long interval;
			int i;

			if ((i = 16 - cw_interval_print(args->ds_p, entry->duration)) < 0)
				i = 0;
			cw_dynstr_printf(args->ds_p, "%-*.*s ", i, i, "");

			interval = args->now.tv_sec - entry->start;
			if (entry->duration <= interval)
				cw_dynstr_printf(args->ds_p, "%-16s ", "expired");
			else {
				 if ((i = 16 - cw_interval_print(args->ds_p, entry->duration - interval)) < 0)
					 i = 0;
				cw_dynstr_printf(args->ds_p, "%-*.*s ", i, i, "");
			}
		} else {
			cw_dynstr_tprintf(args->ds_p, 2,
				cw_fmtval("%-16s ", "unlimited"),
				cw_fmtval("%-16s ", "unlimited")
			);
		}

		if (cw_sched_state_scheduled(&entry->expire)) {
			cw_interval_print(args->ds_p, entry->expire.when.tv_sec - args->now.tv_sec);
			cw_dynstr_printf(args->ds_p, "\n");
		} else
			cw_dynstr_printf(args->ds_p, "never\n");
	} else
		cw_dynstr_truncate(args->ds_p, mark);

	return 0;
}

static int blacklist_show(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct blacklist_show_args args = {
		.ds_p = ds_p,
	};
	int res = RESULT_SHOWUSAGE;

	CW_UNUSED(argv);

	if (argc >= 2 && argc <= 3) {
		cw_clock_gettime(global_cond_clock_monotonic, &args.now);
		if ((args.pattern = argv[2]))
			args.pattern_len = strlen(args.pattern);

		cw_dynstr_tprintf(ds_p, 4,
			cw_fmtval("%-41s ", "Address"),
			cw_fmtval("%-16s ", "Duration"),
			cw_fmtval("%-16s ", "Remaining"),
			cw_fmtval("%s\n", "Remove")
		);

		cw_registry_iterate_ordered(&blacklist_registry, blacklist_show_one, &args);

		res = RESULT_SUCCESS;
	}

	return res;
}
#undef FORMAT
#undef FORMAT2


/* blacklist set {{{2 */

static const char blacklist_set_usage[] =
"Usage: blacklist set [min_duration|max_duration|remember [value]]\n";


static struct {
	const char *name;
	unsigned int *p;
} blacklist_set_map[] = {
	{ "min_duration", &blacklist_min_duration },
	{ "max_duration", &blacklist_max_duration },
	{ "remember",     &blacklist_remember     },
};


static int blacklist_set(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int i, res = RESULT_SHOWUSAGE;

	CW_UNUSED(argc);

	if (argv[2]) {
		for (i = 0; i < arraysize(blacklist_set_map); i++) {
			if (!strcmp(argv[2], blacklist_set_map[i].name)) {
				if (argv[3])
					*(blacklist_set_map[i].p) = strtoul(argv[3], NULL, 10);
				else
					cw_dynstr_printf(ds_p, "%u", *(blacklist_set_map[i].p));
				res = RESULT_SUCCESS;
			}
		}
	} else {
		for (i = 0; i < arraysize(blacklist_set_map); i++)
			cw_dynstr_printf(ds_p, "%s\t%u\n", blacklist_set_map[i].name, *(blacklist_set_map[i].p));
		res = RESULT_SUCCESS;
	}

	return res;
}


static void complete_blacklist_set(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	if (lastarg == 2) {
		int i;

		for (i = 0; i < arraysize(blacklist_set_map); i++)
			if (!strncmp(argv[2], blacklist_set_map[i].name, lastarg_len))
				cw_dynstr_printf(ds_p, "%s\n", blacklist_set_map[i].name);
	}
}


/* CLI table {{{2 */


static struct cw_clicmd  my_clis[] = {
	{
		.cmda = { "blacklist", "add", NULL },
		.handler = blacklist_add,
		.generator = complete_blacklist_entries,
		.summary = "Blacklist an address",
		.usage = blacklist_add_usage,
	},
	{
		.cmda = { "blacklist", "remove", NULL },
		.handler = blacklist_remove,
		.generator = complete_blacklist_entries,
		.summary = "Remove an address from the blacklist",
		.usage = blacklist_remove_usage,
	},
	{
		.cmda = { "blacklist", "show", NULL },
		.handler = blacklist_show,
		.generator = complete_blacklist_entries,
		.summary = "Show blacklisted addresses",
		.usage = blacklist_show_usage,
	},
	{
		.cmda = { "blacklist", "set", NULL },
		.handler = blacklist_set,
		.generator = complete_blacklist_set,
		.summary = "Set or show blacklist parameters",
		.usage = blacklist_set_usage,
	}
};


/* }}} */


/* Initialization {{{1 */


int cw_blacklist_init(void)
{
	int res = 0;

	cw_registry_init(&blacklist_registry, 1024);

	if (!(sched = sched_context_create(1))) {
		cw_log(CW_LOG_WARNING, "Unable to create schedule context\n");
		res = -1;
	}

	cw_cli_register_multiple(my_clis, arraysize(my_clis));

	return res;
}
