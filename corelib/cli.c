/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Standard Command Line Interface
 *
 */
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/signal.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>

#include "callweaver.h"
CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/cli.h"
#include "callweaver/module.h"
#include "callweaver/cli.h"
#include "callweaver/pbx.h"
#include "callweaver/channel.h"
#include "callweaver/manager.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/app.h"


static int cw_clicmd_qsort_compare(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct cw_clicmd *clicmd_a = container_of(*objp_a, struct cw_clicmd, obj);
	const struct cw_clicmd *clicmd_b = container_of(*objp_b, struct cw_clicmd, obj);
	int m, i;

	m = 0;
	for (i = 0; !m; i++) {
		if (clicmd_a->cmda[i] && clicmd_b->cmda[i])
			m = strcasecmp(clicmd_a->cmda[i], clicmd_b->cmda[i]);
		else if (!clicmd_a->cmda[i] && clicmd_b->cmda[i])
			m = 1;
		else if (clicmd_a->cmda[i] && !clicmd_b->cmda[i])
			m = -1;
		else
			break;
	}

	return m;
}

struct match_args {
    char **cmds;
    int ncmds;
    int exact;
};

static int clicmd_object_match(struct cw_object *obj, const void *pattern)
{
    struct cw_clicmd *clicmd = container_of(obj, struct cw_clicmd, obj);
    const struct match_args *args = pattern;
    int m, i;

    /* start optimistic */
    m = 1;
    for (i = 0; m && i < args->ncmds; i++) {
        /* If there are no more words in the candidate command, then we're there. */
        if (!clicmd->cmda[i] && !args->exact)
            break;
        /* If there are no more words in the command (and we're looking for
         * an exact match) or there is a difference between the two words,
         * then this is not a match
         */
        if (!clicmd->cmda[i] || strcasecmp(clicmd->cmda[i], args->cmds[i]))
            m = 0;
    }
    /* If more words are needed to complete the command then this is not
       a candidate (unless we're looking for a really inexact answer  */
    if (args->exact > -1 && clicmd->cmda[i])
        m = 0;

    return m;
}

struct cw_registry clicmd_registry = {
    .name = "CLI Command",
    .qsort_compare = cw_clicmd_qsort_compare,
    .match = clicmd_object_match,
};


static struct cw_clicmd *find_cli(char *cmds[], int exact)
{
	struct match_args args = {
		.cmds = cmds,
		.exact = exact,
	};
	struct cw_object *obj;
	struct cw_clicmd *clicmd;

	for (args.ncmds = 0; cmds[args.ncmds]; args.ncmds++);

	clicmd = NULL;
	for (; args.ncmds; args.ncmds--) {
		if ((obj = cw_registry_find(&clicmd_registry, 0, 0, &args))) {
			clicmd = container_of(obj, struct cw_clicmd, obj);
			break;
		}
	}
	return clicmd;
}


static const char help_help[] =
"Usage: help [topic]\n"
"       When called with a topic as an argument, displays usage\n"
"       information on the given command. If called without a\n"
"       topic, it provides a list of commands.\n";

static const char chanlist_help[] =
"Usage: show channels [concise|verbose]\n"
"       Lists currently defined channels and some information about them. If\n"
"       'concise' is specified, the format is abridged and in a more easily\n"
"       machine parsable format. If 'verbose' is specified, the output includes\n"
"       more and longer fields.\n";

static const char set_verbose_help[] =
"Usage: set verbose <level>\n"
"       Sets level of verbose messages to be displayed.  0 means\n"
"       no messages should be displayed. Equivalent to -v[v[v...]]\n"
"       on startup\n";

static const char set_debug_help[] =
"Usage: set debug <level>\n"
"       Sets level of core debug messages to be displayed.  0 means\n"
"       no messages should be displayed. Equivalent to -d[d[d...]]\n"
"       on startup.\n";

static const char softhangup_help[] =
"Usage: soft hangup <channel>\n"
"       Request that a channel be hung up. The hangup takes effect\n"
"       the next time the driver reads or writes from the channel\n";


static int handle_set_verbose(cw_dynstr_t *ds_p, int argc, char *argv[])
{
    int val = 0;
    int oldval = 0;

    /* Has a hidden 'at least' argument */
    if ((argc != 3) && (argc != 4))
        return RESULT_SHOWUSAGE;
    if ((argc == 4) && strcasecmp(argv[2], "atleast"))
        return RESULT_SHOWUSAGE;
    oldval = option_verbose;
    if (argc == 3)
        option_verbose = atoi(argv[2]);
    else {
        val = atoi(argv[3]);
        if (val > option_verbose)
            option_verbose = val;
    }
    if (oldval != option_verbose && option_verbose > 0)
        cw_dynstr_printf(ds_p, "Verbosity was %d and is now %d\n", oldval, option_verbose);
    else if (oldval > 0 && option_verbose > 0)
        cw_dynstr_printf(ds_p, "Verbosity is at least %d\n", option_verbose);
    else if (oldval > 0 && option_verbose == 0)
        cw_dynstr_printf(ds_p, "Verbosity is now OFF\n");
    return RESULT_SUCCESS;
}

static int handle_set_debug(cw_dynstr_t *ds_p, int argc, char *argv[])
{
    int val = 0;
    int oldval = 0;
    /* Has a hidden 'at least' argument */
    if ((argc != 3) && (argc != 4))
        return RESULT_SHOWUSAGE;
    if ((argc == 4) && strcasecmp(argv[2], "atleast"))
        return RESULT_SHOWUSAGE;
    oldval = option_debug;
    if (argc == 3)
        option_debug = atoi(argv[2]);
    else {
        val = atoi(argv[3]);
        if (val > option_debug)
            option_debug = val;
    }
    if (oldval != option_debug && option_debug > 0)
        cw_dynstr_printf(ds_p, "Core debug was %d and is now %d\n", oldval, option_debug);
    else if (oldval > 0 && option_debug > 0)
        cw_dynstr_printf(ds_p, "Core debug is at least %d\n", option_debug);
    else if (oldval > 0 && option_debug == 0)
        cw_dynstr_printf(ds_p, "Core debug is now OFF\n");
    return RESULT_SUCCESS;
}


static const char version_help[] =
"Usage: show version\n"
"       Shows CallWeaver version information.\n";

static const char uptime_help[] =
"Usage: show uptime [seconds]\n"
"       Shows CallWeaver uptime information.\n"
"       The seconds word returns the uptime in seconds only.\n";

static char *format_uptimestr(time_t timeval)
{
    int years = 0, weeks = 0, days = 0, hours = 0, mins = 0, secs = 0;
    char timestr[256]="";
    int bytes = 0;
    int maxbytes = 0;
    int offset = 0;
#define SECOND (1)
#define MINUTE (SECOND*60)
#define HOUR (MINUTE*60)
#define DAY (HOUR*24)
#define WEEK (DAY*7)
#define YEAR (DAY*365)
#define ESS(x) ((x == 1) ? "" : "s")

    maxbytes = sizeof(timestr);
    if (timeval < 0)
        return NULL;
    if (timeval > YEAR) {
        years = (timeval / YEAR);
        timeval -= (years * YEAR);
        if (years > 0) {
            snprintf(timestr + offset, maxbytes, "%d year%s, ", years, ESS(years));
            bytes = strlen(timestr + offset);
            offset += bytes;
            maxbytes -= bytes;
        }
    }
    if (timeval > WEEK) {
        weeks = (timeval / WEEK);
        timeval -= (weeks * WEEK);
        if (weeks > 0) {
            snprintf(timestr + offset, maxbytes, "%d week%s, ", weeks, ESS(weeks));
            bytes = strlen(timestr + offset);
            offset += bytes;
            maxbytes -= bytes;
        }
    }
    if (timeval > DAY) {
        days = (timeval / DAY);
        timeval -= (days * DAY);
        if (days > 0) {
            snprintf(timestr + offset, maxbytes, "%d day%s, ", days, ESS(days));
            bytes = strlen(timestr + offset);
            offset += bytes;
            maxbytes -= bytes;
        }
    }
    if (timeval > HOUR) {
        hours = (timeval / HOUR);
        timeval -= (hours * HOUR);
        if (hours > 0) {
            snprintf(timestr + offset, maxbytes, "%d hour%s, ", hours, ESS(hours));
            bytes = strlen(timestr + offset);
            offset += bytes;
            maxbytes -= bytes;
        }
    }
    if (timeval > MINUTE) {
        mins = (timeval / MINUTE);
        timeval -= (mins * MINUTE);
        if (mins > 0) {
            snprintf(timestr + offset, maxbytes, "%d minute%s, ", mins, ESS(mins));
            bytes = strlen(timestr + offset);
            offset += bytes;
            maxbytes -= bytes;
        }
    }
    secs = timeval;

    if (secs > 0) {
        snprintf(timestr + offset, maxbytes, "%d second%s", secs, ESS(secs));
    }

    return strlen(timestr) ? strdup(timestr) : NULL;
}

static int handle_showuptime(cw_dynstr_t *ds_p, int argc, char *argv[])
{
    time_t curtime, tmptime;
    char *timestr;
    int printsec;

    printsec = ((argc == 3) && (!strcasecmp(argv[2],"seconds")));
    if ((argc != 2) && (!printsec))
        return RESULT_SHOWUSAGE;

    time(&curtime);
    if (cw_startuptime) {
        tmptime = curtime - cw_startuptime;
        if (printsec) {
            cw_dynstr_printf(ds_p, "System uptime: %lu\n",tmptime);
        } else {
            timestr = format_uptimestr(tmptime);
            if (timestr) {
                cw_dynstr_printf(ds_p, "System uptime: %s\n", timestr);
                free(timestr);
            }
        }
    }       
    if (cw_lastreloadtime) {
        tmptime = curtime - cw_lastreloadtime;
        if (printsec) {
            cw_dynstr_printf(ds_p, "Last reload: %lu\n", tmptime);
        } else {
            timestr = format_uptimestr(tmptime);
            if ((timestr) && (!printsec)) {
                cw_dynstr_printf(ds_p, "Last reload: %s\n", timestr);
                free(timestr);
            } 
        }
    }
    return RESULT_SUCCESS;
}

static int handle_version(cw_dynstr_t *ds_p, int argc, char *argv[])
{
    CW_UNUSED(argv);

    if (argc != 2)
        return RESULT_SHOWUSAGE;

    cw_dynstr_printf(ds_p, "%s %s, %s %s\n",
        cw_version_string,
        "built on " BUILD_HOSTNAME,
        (strchr("aeiouhx", BUILD_MACHINE[0]) ? "an" : "a"),
        BUILD_MACHINE " running " BUILD_OS " on " BUILD_DATE);

    return RESULT_SUCCESS;
}


#define FORMAT_STRING  "%-20.20s %-20.20s %-7.7s %-30.30s\n"
#define FORMAT_STRING2 "%-20.20s %-20.20s %-7.7s %-30.30s\n"
#define CONCISE_FORMAT_STRING  "%s!%s!%s!%d!%s!%s!%s!%s!%d!%s!%s\n"
#define VERBOSE_FORMAT_STRING  "%-20.20s %-20.20s %-16.16s %4d %-7.7s %-12.12s %-15.15s %8.8s %-11.11s %-20.20s\n"
#define VERBOSE_FORMAT_STRING2 "%-20.20s %-20.20s %-16.16s %-4.4s %-7.7s %-12.12s %-15.15s %8.8s %-11.11s %-20.20s\n"

struct handle_chanlist_args {
	cw_dynstr_t *ds_p;
	int concise;
	int verbose;
	int numchans;
};

static int handle_chanlist_one(struct cw_object *obj, void *data)
{
	char buf[30] = "-";
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);
	struct handle_chanlist_args *args = data;
	struct cw_channel *bc = NULL;
	int duration;

	bc = cw_bridged_channel(chan);

	if ((args->concise || args->verbose) && chan->cdr && !cw_tvzero(chan->cdr->start)) {
		duration = (int)(cw_tvdiff_ms(cw_tvnow(), chan->cdr->start) / 1000);
		if (args->verbose) {
			snprintf(buf, sizeof(buf), "%02d:%02d:%02d", duration / 3600, (duration % 3600) / 60, duration % 60);
		} else {
			snprintf(buf, sizeof(buf), "%d", duration);
		}
	} else {
		buf[0] = '\0';
	}

	if (args->concise) {
		cw_dynstr_printf(args->ds_p, CONCISE_FORMAT_STRING, chan->name,
			chan->context, chan->exten, chan->priority,
			cw_state2str(chan->_state),
			(chan->appl ? chan->appl : "(None)"),
			(!cw_strlen_zero(chan->cid.cid_num) ? chan->cid.cid_num : ""),
			(!cw_strlen_zero(chan->accountcode) ? chan->accountcode : ""),
			chan->amaflags,
			buf,
			(bc ? bc->name : "(None)"));
	} else if (args->verbose) {
		cw_dynstr_printf(args->ds_p, VERBOSE_FORMAT_STRING, chan->name,
			chan->context, chan->exten, chan->priority,
			cw_state2str(chan->_state),
			(chan->appl ? chan->appl : "(None)"),
			(!cw_strlen_zero(chan->cid.cid_num) ? chan->cid.cid_num : ""),
			buf,
			(!cw_strlen_zero(chan->accountcode) ? chan->accountcode : ""),
			(bc ? bc->name : "(None)"));
	} else {
		if (!cw_strlen_zero(chan->context) && !cw_strlen_zero(chan->exten))
			snprintf(buf, sizeof(buf), "%s@%s:%d", chan->exten, chan->context, chan->priority);
		else
			strcpy(buf, "(None)");

		cw_dynstr_printf(args->ds_p, FORMAT_STRING, chan->name,
			buf,
			cw_state2str(chan->_state),
			(chan->appl ? chan->appl : "(None)"));
	}

	args->numchans++;

	if (bc)
		cw_object_put(bc);

	return 0;
}

static int handle_chanlist(cw_dynstr_t *ds_p, int argc, char *argv[])
{
	struct handle_chanlist_args args;

	args.concise = (argc == 3  &&  (!strcasecmp(argv[2], "concise")));
	args.verbose = (argc == 3  &&  (!strcasecmp(argv[2], "verbose")));

	if (argc < 2  ||  argc > 3  ||  (argc == 3  &&  !args.concise  &&  !args.verbose))
		return RESULT_SHOWUSAGE;

	if (!args.concise  &&  !args.verbose)
		cw_dynstr_printf(ds_p, FORMAT_STRING2, "Channel", "Location", "State", "Application");
	else if (args.verbose)
		cw_dynstr_printf(ds_p, VERBOSE_FORMAT_STRING2, "Channel", "Context", "Extension", "Priority", "State", "Application", "CallerID", "Duration", "Accountcode", "BridgedTo");

	args.ds_p = ds_p;
	args.numchans = 0;
	cw_registry_iterate_ordered(&channel_registry, handle_chanlist_one, &args);

	if (!args.concise) {
		cw_dynstr_printf(ds_p, "%d active channel%s\n", args.numchans, (args.numchans != 1)  ?  "s"  :  "");
		if (option_maxcalls) {
			cw_dynstr_printf(ds_p,
				"%d of %d max active call%s (%5.2f%% of capacity)\n",
				cw_active_calls(),
				option_maxcalls,
				(cw_active_calls() != 1)  ?  "s"  :  "",
				((float) cw_active_calls() / (float) option_maxcalls)*100.0);
		} else {
			cw_dynstr_printf(ds_p, "%d active call%s\n", cw_active_calls(), (cw_active_calls() != 1)  ?  "s"  :  "");
		}
	}

	return RESULT_SUCCESS;
}
#undef FORMAT_STRING
#undef FORMAT_STRING2
#undef CONCISE_FORMAT_STRING
#undef VERBOSE_FORMAT_STRING
#undef VERBOSE_FORMAT_STRING2


static const char showchan_help[] =
"Usage: show channel <channel>\n"
"       Shows lots of information about the specified channel.\n";

static const char debugchan_help[] =
"Usage: debug channel <channel>\n"
"       Enables debugging on a specific channel.\n";

static const char debuglevel_help[] =
"Usage: debug level <level> [filename]\n"
"       Set debug to specified level (0 to disable).  If filename\n"
"is specified, debugging will be limited to just that file.\n";

static const char nodebugchan_help[] =
"Usage: no debug channel <channel>\n"
"       Disables debugging on a specific channel.\n";


static int handle_softhangup(cw_dynstr_t *ds_p, int argc, char *argv[])
{
    struct cw_channel *c = NULL;

    if (argc != 3)
        return RESULT_SHOWUSAGE;

    if ((c = cw_get_channel_by_name_locked(argv[2]))) {
        cw_dynstr_printf(ds_p, "Requested Hangup on channel '%s'\n", c->name);
        cw_softhangup(c, CW_SOFTHANGUP_EXPLICIT);
        cw_channel_unlock(c);
	cw_object_put(c);
    } else
        cw_dynstr_printf(ds_p, "%s is not a known channel\n", argv[2]);
    return RESULT_SUCCESS;
}


static int handle_debuglevel(cw_dynstr_t *ds_p, int argc, char *argv[])
{
    const char *filename = "<any>";
    int newlevel;

    if ((argc < 3) || (argc > 4))
        return RESULT_SHOWUSAGE;
    if (sscanf(argv[2], "%d", &newlevel) != 1)
        return RESULT_SHOWUSAGE;
    option_debug = newlevel;
    if (argc == 4) {
        filename = argv[3];
        cw_copy_string(debug_filename, filename, sizeof(debug_filename));
    } else {
        debug_filename[0] = '\0';
    }
    cw_dynstr_printf(ds_p, "Debugging level set to %d, file '%s'\n", newlevel, filename);
    return RESULT_SUCCESS;
}

static int debugchan_one(struct cw_object *obj, void *data)
{
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);
	cw_dynstr_t *ds_p = data;

	cw_channel_lock(chan);

	cw_set_flag(chan, CW_FLAG_DEBUG_IN);
	cw_set_flag(chan, CW_FLAG_DEBUG_OUT);
	cw_dynstr_printf(ds_p, "Debugging enabled on channel %s\n", chan->name);

	cw_channel_unlock(chan);
	return 0;
}

static int nodebugchan_one(struct cw_object *obj, void *data)
{
	struct cw_channel *chan = container_of(obj, struct cw_channel, obj);
	cw_dynstr_t *ds_p = data;

	cw_channel_lock(chan);

	cw_clear_flag(chan, CW_FLAG_DEBUG_IN);
	cw_clear_flag(chan, CW_FLAG_DEBUG_OUT);
	cw_dynstr_printf(ds_p, "Debugging disabled on channel %s\n", chan->name);

	cw_channel_unlock(chan);
	return 0;
}

/* XXX todo: merge next two functions!!! */
static int handle_debugchan(cw_dynstr_t *ds_p, int argc, char *argv[])
{
	struct cw_channel *chan;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	if (strcasecmp("all", argv[2])) {
		if ((chan = cw_get_channel_by_name_locked(argv[2]))) {
			debugchan_one(&chan->obj, ds_p);
			cw_channel_unlock(chan);
			cw_object_put(chan);
		} else
			cw_dynstr_printf(ds_p, "No such channel %s\n", argv[2]);
		return RESULT_SUCCESS;
	}

	cw_debugchan_flags = (CW_FLAG_DEBUG_IN | CW_FLAG_DEBUG_OUT);
	cw_registry_iterate_ordered(&channel_registry, debugchan_one, ds_p);
	cw_dynstr_printf(ds_p, "Debugging on new channels is enabled\n");

	return RESULT_SUCCESS;
}

static int handle_nodebugchan(cw_dynstr_t *ds_p, int argc, char *argv[])
{
	struct cw_channel *chan;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	if (strcasecmp("all", argv[3])) {
		if ((chan = cw_get_channel_by_name_locked(argv[3]))) {
			nodebugchan_one(&chan->obj, ds_p);
			cw_channel_unlock(chan);
			cw_object_put(chan);
		} else
			cw_dynstr_printf(ds_p, "No such channel %s\n", argv[3]);
		return RESULT_SUCCESS;
	}

	cw_debugchan_flags = 0;
	cw_registry_iterate_ordered(&channel_registry, nodebugchan_one, ds_p);
	cw_dynstr_printf(ds_p, "Debugging on new channels is disabled\n");

	return RESULT_SUCCESS;
}


static int handle_showchan(cw_dynstr_t *ds_p, int argc, char *argv[])
{
    char buf[2048];
    char cdrtime[256];
    struct timeval now;
    struct cw_channel *chan, *bchan;
    long secs;

    if (argc != 3)
        return RESULT_SHOWUSAGE;

    if (!(chan = cw_get_channel_by_name_locked(argv[2]))) {
        cw_dynstr_printf(ds_p, "%s is not a known channel\n", argv[2]);
        return RESULT_SUCCESS;
    }

    if (chan->cdr) {
        now = cw_tvnow();
        secs = now.tv_sec - chan->cdr->start.tv_sec;
        snprintf(cdrtime, sizeof(cdrtime), "%ldh%ldm%lds", secs / 3600, (secs % 3600) / 60, secs % 60);
    } else
        strcpy(cdrtime, "N/A");

    bchan = cw_bridged_channel(chan);

    cw_dynstr_printf(ds_p,
        " -- General --\n"
        "           Name: %s\n"
        "           Type: %s\n"
        "       UniqueID: %s\n"
        "      Caller ID: %s\n"
        " Caller ID Name: %s\n"
        "    DNID Digits: %s\n"
        "          State: %s (%d)\n"
        "          Rings: %d\n"
        "   NativeFormat: %d\n"
        "    WriteFormat: %d\n"
        "     ReadFormat: %d\n"
        "1st File Descriptor: %d\n"
        "      Frames in: %d%s\n"
        "     Frames out: %d%s\n"
        " Time to Hangup: %ld\n"
        "   Elapsed Time: %s\n"
        "  Direct Bridge: %s\n"
        "Indirect Bridge: %s\n"
        " -- Jitterbuffer --\n"
        " Implementation: %s\n"
        "    Conf. Flags: 0x%x\n"
        "       Max Size: %ld\n"
        "  Resync Thresh: %ld\n"
        "   Timing Comp.: %ld\n"
        "    State Flags: 0x%x\n"        
        " --   PBX   --\n"
        "        Context: %s\n"
        "      Extension: %s\n"
        "       Priority: %d\n"
        "     Call Group: %d\n"
        "   Pickup Group: %d\n"
        "    Application: %s\n"
        "    Blocking in: %s\n"
        "    T38 mode on: %d\n",
        chan->name, chan->type, chan->uniqueid,
        (chan->cid.cid_num ? chan->cid.cid_num : "(N/A)"),
        (chan->cid.cid_name ? chan->cid.cid_name : "(N/A)"),
        (chan->cid.cid_dnid ? chan->cid.cid_dnid : "(N/A)" ), cw_state2str(chan->_state), chan->_state, chan->rings, chan->nativeformats, chan->writeformat, chan->readformat,
        chan->fds[0],
        chan->fin, (cw_test_flag(chan, CW_FLAG_DEBUG_IN) ? " (DEBUGGED)" : ""),
        chan->fout, (cw_test_flag(chan, CW_FLAG_DEBUG_OUT) ? " (DEBUGGED)" : ""),
        (long)chan->whentohangup,
        cdrtime, (chan->_bridge ? chan->_bridge->name : "<none>"), (bchan ? bchan->name : "<none>"),
        chan->jb.conf.impl,
        chan->jb.conf.flags,
        chan->jb.conf.max_size,
        chan->jb.conf.resync_threshold,
        chan->jb.conf.timing_compensation,
        chan->jb.flags,
        chan->context, chan->exten, chan->priority, (int)chan->callgroup,
        (int)chan->pickupgroup, ( chan->appl ? chan->appl : "(N/A)" ),
        (cw_test_flag(chan, CW_FLAG_BLOCKING) ? chan->blockproc : "(Not Blocking)"),
        chan->t38_status
        );

    if (pbx_builtin_serialize_variables(chan, buf, sizeof(buf)))
        cw_dynstr_printf(ds_p, "      Variables:\n%s\n", buf);
    if (chan->cdr && cw_cdr_serialize_variables(chan->cdr, buf, sizeof(buf), '=', '\n', 1))
        cw_dynstr_printf(ds_p, "  CDR Variables:\n%s\n", buf);
    
    cw_channel_unlock(chan);
    if (bchan)
	    cw_object_put(bchan);
    cw_object_put(chan);
    return RESULT_SUCCESS;
}

static void complete_show_channels(cw_dynstr_t *ds_p, char *argv[], int lastarg, int lastarg_len)
{
    static const char *choices[] = { "concise", "verbose" };
    int i;

    if (lastarg == 2) {
        for (i = 0; i < sizeof(choices) / sizeof(choices[0]); i++) {
            if (!strncasecmp(argv[2], choices[i], lastarg_len))
                cw_dynstr_printf(ds_p, "%s\n", choices[i]);
        }
    }
}


static void complete_ch_3(cw_dynstr_t *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	if (lastarg == 2)
		cw_complete_channel(ds_p, argv[2], lastarg_len);
}

static void complete_ch_4(cw_dynstr_t *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	if (lastarg == 3)
		cw_complete_channel(ds_p, argv[3], lastarg_len);
}


struct cli_generator_args {
	args_t av;
	cw_dynstr_t *ds_p;
	int lastarg_len;
};

static int cli_generator_one(struct cw_object *obj, void *data)
{
	struct cw_clicmd *clicmd = container_of(obj, struct cw_clicmd, obj);
	struct cli_generator_args *args = data;
	int i;

	for (i = 0; i < args->av.used; i++) {
		if (!clicmd->cmda[i] || strcasecmp(args->av.data[i], clicmd->cmda[i]))
			break;
	}

	/* If we matched everything we were given we have a result. */
	if (i == args->av.used && clicmd->cmda[i]) {
		if (!strncasecmp(clicmd->cmda[i], args->av.data[args->av.used], args->lastarg_len))
			cw_dynstr_printf(args->ds_p, "%s\r\n", clicmd->cmda[i]);
	} else if (!clicmd->cmda[i] && clicmd->generator)
		clicmd->generator(args->ds_p, args->av.data, args->av.used, args->lastarg_len);

	return 0;
}

static void help_complete(cw_dynstr_t *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	struct cli_generator_args args = {
		.av = CW_DYNARRAY_INIT,
		.ds_p = ds_p,
		.lastarg_len = lastarg_len,
	};

	args.av.data = argv + 1;
	args.av.used = lastarg - 1;

	cw_registry_iterate(&clicmd_registry, cli_generator_one, &args);
}

void cw_cli_generator(cw_dynstr_t *ds_p, char *cmd)
{
	struct cli_generator_args args = {
		.av = CW_DYNARRAY_INIT,
		.ds_p = ds_p,
	};

	if (!cw_separate_app_args(&args.av, cmd, " \t")) {
		if (args.av.used == 0)
			args.av.data[0] = (char *)"";
		else
			args.av.used--;

		args.lastarg_len = strlen(args.av.data[args.av.used]);

		cw_registry_iterate(&clicmd_registry, cli_generator_one, &args);
	}

	cw_dynarray_free(&args.av);
}


void cw_cli_command(cw_dynstr_t *ds_p, char *cmd)
{
	args_t args = CW_DYNARRAY_INIT;
	struct cw_clicmd *clicmd;

	if (!cw_separate_app_args(&args, cmd, " \t") && args.used > 0) {
		if (args.used && !args.data[args.used - 1][0])
			args.data[--args.used] = NULL;

		clicmd = find_cli(args.data, 0);
		if (clicmd) {
			switch (clicmd->handler(ds_p, args.used, args.data)) {
				case RESULT_SHOWUSAGE:
					cw_dynstr_printf(ds_p, "%s", clicmd->usage);
					break;
			}
			cw_object_put(clicmd);
		} else
			cw_dynstr_printf(ds_p, "No such command. Type \"help\" for help.\n");
	}

	cw_dynarray_free(&args);
}


struct help_workhorse_args {
	char **match;
	cw_dynstr_t *ds_p;
};

static int help_workhorse_one(struct cw_object *obj, void *data)
{
	struct cw_clicmd *clicmd = container_of(obj, struct cw_clicmd, obj);
	struct help_workhorse_args *args = data;
	size_t mark;
	int i;

	for (i = 0; args->match[i]; i++)
		if (strcasecmp(args->match[i], clicmd->cmda[i]))
			goto out;

	mark = cw_dynstr_end(args->ds_p);

	/* Assumption: the literal words of commands do not contain whitespace
	 * quotes, backslashes or anything else that would need escaping if a
	 * subsequent cw_separate_app_args() is to return the correct data.
	 */
	for (i = 0; clicmd->cmda[i]; i++)
		cw_dynstr_printf(args->ds_p, "%s%s", (i ? " " : ""), clicmd->cmda[i]);

	if (!args->ds_p->error) {
		if (args->ds_p->used - mark < 25)
			cw_dynstr_printf(args->ds_p, "%.*s", 25 - (int)(args->ds_p->used - mark), "                         ");
		cw_dynstr_printf(args->ds_p, "%s\n", clicmd->summary);
	}

out:
	return 0;
}

static int help_workhorse(cw_dynstr_t *ds_p, char **match)
{
    struct help_workhorse_args args = {
        .match = match,
        .ds_p = ds_p,
    };

    cw_registry_iterate_ordered(&clicmd_registry, help_workhorse_one, &args);
    return 0;
}

static int handle_help(cw_dynstr_t *ds_p, int argc, char *argv[]) {
	struct cw_clicmd *clicmd;
	int ret;

	ret = RESULT_SHOWUSAGE;
	if (argc == 0)
		ret = RESULT_SUCCESS;
	else if (argc > 1) {
		ret = RESULT_SUCCESS;
		if ((clicmd = find_cli(argv + 1, 1))) {
			cw_dynstr_printf(ds_p, "%s", (clicmd->usage ? clicmd->usage : "No help available\n"));
			cw_object_put(clicmd);
		} else if ((clicmd = find_cli(argv + 1, -1))) {
			ret = help_workhorse(ds_p, argv + 1);
			cw_object_put(clicmd);
		} else
			cw_dynstr_printf(ds_p, "No such command\n");
	} else
		ret = help_workhorse(ds_p, NULL);

	return ret;
}


static struct cw_clicmd builtins[] = {
    {
        .cmda = { "debug", "channel", NULL },
        .handler = handle_debugchan,
        .summary = "Enable debugging on a channel",
        .usage = debugchan_help,
        .generator = complete_ch_3,
    },
    {
        .cmda = { "debug", "level", NULL },
        .handler = handle_debuglevel,
        .summary = "Set global debug level",
        .usage = debuglevel_help,
    },
    {
        .cmda = { "help", NULL },
        .handler = handle_help,
        .summary = "Display help list, or specific help on a command",
        .usage = help_help,
        .generator = help_complete,
    },
    {
        .cmda = { "no", "debug", "channel", NULL },
        .handler = handle_nodebugchan,
        .summary = "Disable debugging on a channel",
        .usage = nodebugchan_help,
        .generator = complete_ch_4,
    },
    {
        .cmda = { "set", "debug", NULL },
        .handler = handle_set_debug,
        .summary = "Set level of debug chattiness",
        .usage = set_debug_help,
    },
    {
        .cmda = { "set", "verbose", NULL },
        .handler = handle_set_verbose,
        .summary = "Set level of verboseness",
        .usage = set_verbose_help,
    },
    {
        .cmda = { "show", "channel", NULL },
        .handler = handle_showchan,
        .summary = "Display information on a specific channel",
        .usage = showchan_help,
        .generator = complete_ch_3,
    },
    {
        .cmda = { "show", "channels", NULL },
        .handler = handle_chanlist,
        .summary = "Display information on channels",
        .usage = chanlist_help,
        .generator = complete_show_channels,
    },
    {
        .cmda = { "show", "uptime", NULL },
        .handler = handle_showuptime,
        .summary = "Show uptime information",
        .usage = uptime_help,
    },
    {
        .cmda = { "show", "version", NULL },
        .handler = handle_version,
        .summary = "Display version info",
        .usage = version_help,
    },
    {
        .cmda = { "soft", "hangup", NULL },
        .handler = handle_softhangup,
        .summary = "Request a hangup on a given channel",
        .usage = softhangup_help,
        .generator = complete_ch_3,
    },
};


void cw_cli_init(void)
{
    cw_cli_register_multiple(builtins, arraysize(builtins));
}
