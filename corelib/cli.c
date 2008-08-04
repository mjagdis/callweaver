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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

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

#include "libltdl/ltdl.h"


static const char *clicmd_registry_obj_name(struct cw_object *obj)
{
    struct cw_clicmd *it = container_of(obj, struct cw_clicmd, obj);
    return it->summary;
}

static int clicmd_registry_obj_cmp(struct cw_object *a, struct cw_object *b)
{
    struct cw_clicmd *clicmd_a = container_of(a, struct cw_clicmd, obj);
    struct cw_clicmd *clicmd_b = container_of(b, struct cw_clicmd, obj);
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

static int clicmd_registry_obj_match(struct cw_object *obj, const void *pattern)
{
    struct cw_clicmd *clicmd = container_of(obj, struct cw_clicmd, obj);
    const struct match_args *args = pattern;
    int m, i;

    /* start optimistic */
    m= 1;
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
    .obj_name = clicmd_registry_obj_name,
    .obj_cmp = clicmd_registry_obj_cmp,
    .obj_match = clicmd_registry_obj_match,
    .lock = CW_MUTEX_INIT_VALUE,
};


static struct cw_clicmd *find_cli(char *cmds[], int exact)
{
    struct match_args args = {
        .cmds = cmds,
        .exact = exact,
    };
    struct cw_object *obj;

    for (args.ncmds = 0; cmds[args.ncmds]; args.ncmds++);

    for (; args.ncmds; args.ncmds--) {
        obj = cw_registry_find(&clicmd_registry, &args);
        if (obj) {
            return container_of(obj, struct cw_clicmd, obj);
        }
    }
    return NULL;
}


extern unsigned long global_fin, global_fout;

void cw_cli(int fd, char *fmt, ...)
{
    char *stuff;
    int res = 0;
    va_list ap;

    va_start(ap, fmt);
    res = vasprintf(&stuff, fmt, ap);
    va_end(ap);
    if (res == -1) {
        cw_log(CW_LOG_ERROR, "Out of memory\n");
    } else {
        cw_carefulwrite(fd, stuff, strlen(stuff), 100);
        free(stuff);
    }
}


static char help_help[] =
"Usage: help [topic]\n"
"       When called with a topic as an argument, displays usage\n"
"       information on the given command. If called without a\n"
"       topic, it provides a list of commands.\n";

static char chanlist_help[] = 
"Usage: show channels [concise|verbose]\n"
"       Lists currently defined channels and some information about them. If\n"
"       'concise' is specified, the format is abridged and in a more easily\n"
"       machine parsable format. If 'verbose' is specified, the output includes\n"
"       more and longer fields.\n";

static char set_verbose_help[] = 
"Usage: set verbose <level>\n"
"       Sets level of verbose messages to be displayed.  0 means\n"
"       no messages should be displayed. Equivalent to -v[v[v...]]\n"
"       on startup\n";

static char set_debug_help[] = 
"Usage: set debug <level>\n"
"       Sets level of core debug messages to be displayed.  0 means\n"
"       no messages should be displayed. Equivalent to -d[d[d...]]\n"
"       on startup.\n";

static char softhangup_help[] =
"Usage: soft hangup <channel>\n"
"       Request that a channel be hung up. The hangup takes effect\n"
"       the next time the driver reads or writes from the channel\n";


static int handle_set_verbose(int fd, int argc, char *argv[])
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
        cw_cli(fd, "Verbosity was %d and is now %d\n", oldval, option_verbose);
    else if (oldval > 0 && option_verbose > 0)
        cw_cli(fd, "Verbosity is at least %d\n", option_verbose);
    else if (oldval > 0 && option_verbose == 0)
        cw_cli(fd, "Verbosity is now OFF\n");
    return RESULT_SUCCESS;
}

static int handle_set_debug(int fd, int argc, char *argv[])
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
        cw_cli(fd, "Core debug was %d and is now %d\n", oldval, option_debug);
    else if (oldval > 0 && option_debug > 0)
        cw_cli(fd, "Core debug is at least %d\n", option_debug);
    else if (oldval > 0 && option_debug == 0)
        cw_cli(fd, "Core debug is now OFF\n");
    return RESULT_SUCCESS;
}


static char version_help[] =
"Usage: show version\n"
"       Shows CallWeaver version information.\n";

static char uptime_help[] =
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

static int handle_showuptime(int fd, int argc, char *argv[])
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
            cw_cli(fd, "System uptime: %lu\n",tmptime);
        } else {
            timestr = format_uptimestr(tmptime);
            if (timestr) {
                cw_cli(fd, "System uptime: %s\n", timestr);
                free(timestr);
            }
        }
    }       
    if (cw_lastreloadtime) {
        tmptime = curtime - cw_lastreloadtime;
        if (printsec) {
            cw_cli(fd, "Last reload: %lu\n", tmptime);
        } else {
            timestr = format_uptimestr(tmptime);
            if ((timestr) && (!printsec)) {
                cw_cli(fd, "Last reload: %s\n", timestr);
                free(timestr);
            } 
        }
    }
    return RESULT_SUCCESS;
}

static int handle_version(int fd, int argc, char *argv[])
{
    if (argc != 2)
        return RESULT_SHOWUSAGE;

    cw_cli(fd, "%s %s, %s %s\n",
        cw_version_string,
        "built on " BUILD_HOSTNAME,
        (strchr("aeiouhx", BUILD_MACHINE[0]) ? "an" : "a"),
        BUILD_MACHINE " running " BUILD_OS " on " BUILD_DATE);

    return RESULT_SUCCESS;
}
static int handle_chanlist(int fd, int argc, char *argv[])
{
#define FORMAT_STRING  "%-20.20s %-20.20s %-7.7s %-30.30s\n"
#define FORMAT_STRING2 "%-20.20s %-20.20s %-7.7s %-30.30s\n"
#define CONCISE_FORMAT_STRING  "%s!%s!%s!%d!%s!%s!%s!%s!%d!%s!%s\n"
#define VERBOSE_FORMAT_STRING  "%-20.20s %-20.20s %-16.16s %4d %-7.7s %-12.12s %-15.15s %8.8s %-11.11s %-20.20s\n"
#define VERBOSE_FORMAT_STRING2 "%-20.20s %-20.20s %-16.16s %-4.4s %-7.7s %-12.12s %-15.15s %8.8s %-11.11s %-20.20s\n"

    struct cw_channel *c = NULL;
    struct cw_channel *bc = NULL;
    char durbuf[10] = "-";
    char locbuf[40];
    char appdata[40];
    int duration;
    int durh;
    int durm;
    int durs;
    int numchans = 0;
    int concise = 0;
    int verbose = 0;

    concise = (argc == 3  &&  (!strcasecmp(argv[2], "concise")));
    verbose = (argc == 3  &&  (!strcasecmp(argv[2], "verbose")));

    if (argc < 2  ||  argc > 3  ||  (argc == 3  &&  !concise  &&  !verbose))
        return RESULT_SHOWUSAGE;

    if (!concise  &&  !verbose)
    {
        cw_cli(fd,
                 FORMAT_STRING2,
                 "Channel",
                 "Location",
                 "State",
                 "Application");
    }
    else if (verbose)
    {
        cw_cli(fd,
                 VERBOSE_FORMAT_STRING2,
                 "Channel",
                 "Context",
                 "Extension",
                 "Priority",
                 "State",
                 "Application",
                 "CallerID",
                 "Duration",
                 "Accountcode",
                 "BridgedTo");
    }
    while ((c = cw_channel_walk_locked(c)))
    {
        bc = cw_bridged_channel(c);
        if ((concise  ||  verbose)  &&  c->cdr  &&  !cw_tvzero(c->cdr->start))
        {
            duration = (int)(cw_tvdiff_ms(cw_tvnow(), c->cdr->start) / 1000);
            if (verbose)
            {
                durh = duration / 3600;
                durm = (duration % 3600) / 60;
                durs = duration % 60;
                snprintf(durbuf, sizeof(durbuf), "%02d:%02d:%02d", durh, durm, durs);
            }
            else
            {
                snprintf(durbuf, sizeof(durbuf), "%d", duration);
            }
        }
        else
        {
            durbuf[0] = '\0';
        }
        if (concise)
        {
            cw_cli(fd,
                     CONCISE_FORMAT_STRING,
                     c->name,
                     c->context,
                     c->exten,
                     c->priority,
                     cw_state2str(c->_state),
                     c->appl  ?  c->appl  :  "(None)",
                     (c->cid.cid_num  &&  !cw_strlen_zero(c->cid.cid_num))  ?  c->cid.cid_num  :  "",
                     (c->accountcode  &&  !cw_strlen_zero(c->accountcode))  ?  c->accountcode  :  "",
                     c->amaflags,
                     durbuf,
                     bc  ?  bc->name  :  "(None)");
        }
        else if (verbose)
        {
            cw_cli(fd,
                     VERBOSE_FORMAT_STRING,
                     c->name,
                     c->context,
                     c->exten,
                     c->priority,
                     cw_state2str(c->_state),
                     c->appl  ?  c->appl : "(None)",
                     (c->cid.cid_num  &&  !cw_strlen_zero(c->cid.cid_num))  ?  c->cid.cid_num  :  "",
                     durbuf,
                     (c->accountcode  &&  !cw_strlen_zero(c->accountcode))  ?  c->accountcode  :  "",
                     bc  ?  bc->name  :  "(None)");
        }
        else
        {
            if (!cw_strlen_zero(c->context)  &&  !cw_strlen_zero(c->exten))
                snprintf(locbuf, sizeof(locbuf), "%s@%s:%d", c->exten, c->context, c->priority);
            else
                strcpy(locbuf, "(None)");
            if (c->appl)
                snprintf(appdata, sizeof(appdata), "%s", c->appl);
            else
                strcpy(appdata, "(None)");
            cw_cli(fd, FORMAT_STRING, c->name, locbuf, cw_state2str(c->_state), appdata);
        }
        numchans++;
        cw_mutex_unlock(&c->lock);
    }
    if (!concise)
    {
        cw_cli(fd, "%d active channel%s\n", numchans, (numchans != 1)  ?  "s"  :  "");
        if (option_maxcalls)
        {
            cw_cli(fd,
                     "%d of %d max active call%s (%5.2f%% of capacity)\n",
                     cw_active_calls(),
                     option_maxcalls,
                     (cw_active_calls() != 1)  ?  "s"  :  "",
                     ((float) cw_active_calls() / (float) option_maxcalls)*100.0);
        }
        else
        {
            cw_cli(fd, "%d active call%s\n", cw_active_calls(), (cw_active_calls() != 1)  ?  "s"  :  "");
        }
    }
    return RESULT_SUCCESS;

#undef FORMAT_STRING
#undef FORMAT_STRING2
#undef CONCISE_FORMAT_STRING
#undef VERBOSE_FORMAT_STRING
#undef VERBOSE_FORMAT_STRING2
}

static char showchan_help[] = 
"Usage: show channel <channel>\n"
"       Shows lots of information about the specified channel.\n";

static char debugchan_help[] = 
"Usage: debug channel <channel>\n"
"       Enables debugging on a specific channel.\n";

static char debuglevel_help[] = 
"Usage: debug level <level> [filename]\n"
"       Set debug to specified level (0 to disable).  If filename\n"
"is specified, debugging will be limited to just that file.\n";

static char nodebugchan_help[] = 
"Usage: no debug channel <channel>\n"
"       Disables debugging on a specific channel.\n";

static int handle_softhangup(int fd, int argc, char *argv[])
{
    struct cw_channel *c=NULL;
    if (argc != 3)
        return RESULT_SHOWUSAGE;
    c = cw_get_channel_by_name_locked(argv[2]);
    if (c) {
        cw_cli(fd, "Requested Hangup on channel '%s'\n", c->name);
        cw_softhangup(c, CW_SOFTHANGUP_EXPLICIT);
        cw_mutex_unlock(&c->lock);
    } else
        cw_cli(fd, "%s is not a known channel\n", argv[2]);
    return RESULT_SUCCESS;
}


static int handle_debuglevel(int fd, int argc, char *argv[])
{
    int newlevel;
    char *filename = "<any>";
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
    cw_cli(fd, "Debugging level set to %d, file '%s'\n", newlevel, filename);
    return RESULT_SUCCESS;
}

#define DEBUGCHAN_FLAG  0x80000000
/* XXX todo: merge next two functions!!! */
static int handle_debugchan(int fd, int argc, char *argv[])
{
    struct cw_channel *c=NULL;
    int is_all;
    if (argc != 3)
        return RESULT_SHOWUSAGE;

    is_all = !strcasecmp("all", argv[2]);
    if (is_all) {
        global_fin |= DEBUGCHAN_FLAG;
        global_fout |= DEBUGCHAN_FLAG;
        c = cw_channel_walk_locked(NULL);
    } else {
        c = cw_get_channel_by_name_locked(argv[2]);
        if (c == NULL)
            cw_cli(fd, "No such channel %s\n", argv[2]);
    }
    while(c) {
        if (!(c->fin & DEBUGCHAN_FLAG) || !(c->fout & DEBUGCHAN_FLAG)) {
            c->fin |= DEBUGCHAN_FLAG;
            c->fout |= DEBUGCHAN_FLAG;
            cw_cli(fd, "Debugging enabled on channel %s\n", c->name);
        }
        cw_mutex_unlock(&c->lock);
        if (!is_all)
            break;
        c = cw_channel_walk_locked(c);
    }
    cw_cli(fd, "Debugging on new channels is enabled\n");
    return RESULT_SUCCESS;
}

static int handle_nodebugchan(int fd, int argc, char *argv[])
{
    struct cw_channel *c=NULL;
    int is_all;
    if (argc != 4)
        return RESULT_SHOWUSAGE;
    is_all = !strcasecmp("all", argv[3]);
    if (is_all) {
        global_fin &= ~DEBUGCHAN_FLAG;
        global_fout &= ~DEBUGCHAN_FLAG;
        c = cw_channel_walk_locked(NULL);
    } else {
        c = cw_get_channel_by_name_locked(argv[3]);
        if (c == NULL)
            cw_cli(fd, "No such channel %s\n", argv[3]);
    }
    while(c) {
        if ((c->fin & DEBUGCHAN_FLAG) || (c->fout & DEBUGCHAN_FLAG)) {
            c->fin &= ~DEBUGCHAN_FLAG;
            c->fout &= ~DEBUGCHAN_FLAG;
            cw_cli(fd, "Debugging disabled on channel %s\n", c->name);
        }
        cw_mutex_unlock(&c->lock);
        if (!is_all)
            break;
        c = cw_channel_walk_locked(c);
    }
    cw_cli(fd, "Debugging on new channels is disabled\n");
    return RESULT_SUCCESS;
}
        
    

static int handle_showchan(int fd, int argc, char *argv[])
{
    struct cw_channel *c=NULL;
    struct timeval now;
    char buf[2048];
    char cdrtime[256];
    long elapsed_seconds=0;
    int hour=0, min=0, sec=0;
    
    if (argc != 3)
        return RESULT_SHOWUSAGE;
    now = cw_tvnow();
    c = cw_get_channel_by_name_locked(argv[2]);
    if (!c) {
        cw_cli(fd, "%s is not a known channel\n", argv[2]);
        return RESULT_SUCCESS;
    }
    if(c->cdr) {
        elapsed_seconds = now.tv_sec - c->cdr->start.tv_sec;
        hour = elapsed_seconds / 3600;
        min = (elapsed_seconds % 3600) / 60;
        sec = elapsed_seconds % 60;
        snprintf(cdrtime, sizeof(cdrtime), "%dh%dm%ds", hour, min, sec);
    } else
        strcpy(cdrtime, "N/A");
    cw_cli(fd, 
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
        c->name, c->type, c->uniqueid,
        (c->cid.cid_num ? c->cid.cid_num : "(N/A)"),
        (c->cid.cid_name ? c->cid.cid_name : "(N/A)"),
        (c->cid.cid_dnid ? c->cid.cid_dnid : "(N/A)" ), cw_state2str(c->_state), c->_state, c->rings, c->nativeformats, c->writeformat, c->readformat,
        c->fds[0], c->fin & 0x7fffffff, (c->fin & 0x80000000) ? " (DEBUGGED)" : "",
        c->fout & 0x7fffffff, (c->fout & 0x80000000) ? " (DEBUGGED)" : "", (long)c->whentohangup,
        cdrtime, c->_bridge ? c->_bridge->name : "<none>", cw_bridged_channel(c) ? cw_bridged_channel(c)->name : "<none>", 
        c->jb.conf.impl,
        c->jb.conf.flags,
        c->jb.conf.max_size,
        c->jb.conf.resync_threshold,
        c->jb.conf.timing_compensation,
        c->jb.flags,
        c->context, c->exten, c->priority, (int)c->callgroup, 
        (int)c->pickupgroup, ( c->appl ? c->appl : "(N/A)" ),
        (cw_test_flag(c, CW_FLAG_BLOCKING) ? c->blockproc : "(Not Blocking)"),
        c->t38_status
        );
    
    if(pbx_builtin_serialize_variables(c,buf,sizeof(buf)))
        cw_cli(fd,"      Variables:\n%s\n",buf);
    if(c->cdr && cw_cdr_serialize_variables(c->cdr,buf, sizeof(buf), '=', '\n', 1))
        cw_cli(fd,"  CDR Variables:\n%s\n",buf);
    
    cw_mutex_unlock(&c->lock);
    return RESULT_SUCCESS;
}

static void complete_show_channels(int fd, char *argv[], int lastarg, int lastarg_len)
{
    static const char *choices[] = { "concise", "verbose" };
    int i;

    if (lastarg == 2) {
        for (i = 0; i < sizeof(choices) / sizeof(choices[0]); i++) {
            if (!strncasecmp(argv[2], choices[i], lastarg_len))
                cw_cli(fd, "%s\n", choices[i]);
        }
    }
}

static void complete_ch_helper(int fd, char *word, int word_len)
{
    struct cw_channel *c = NULL;

    while ((c = cw_channel_walk_locked(c))) {
        if (!strncasecmp(word, c->name, word_len))
            cw_cli(fd, "%s\n", c->name);
        cw_mutex_unlock(&c->lock);
    }
}

static void complete_ch_3(int fd, char *argv[], int lastarg, int lastarg_len)
{
    if (lastarg == 2)
        complete_ch_helper(fd, argv[2], lastarg_len);
}

static void complete_ch_4(int fd, char *argv[], int lastarg, int lastarg_len)
{
    if (lastarg == 3)
        complete_ch_helper(fd, argv[3], lastarg_len);
}


static int handle_help(int fd, int argc, char *argv[]);

static struct cw_clicmd builtins[] = {
    {
        .cmda = { "debug", "channel", NULL },
        .handler = handle_debugchan,
        .summary = "Enable debugging on a channel",
        .usage = debugchan_help,
        .generator = complete_ch_3
    },
    {
        .cmda = { "debug", "level", NULL },
        .handler = handle_debuglevel,
        .summary = "Set global debug level",
        .usage = debuglevel_help
    },
    {
        .cmda = { "help", NULL },
        .handler = handle_help,
        .summary = "Display help list, or specific help on a command",
        .usage = help_help
    },
    {
        .cmda = { "no", "debug", "channel", NULL },
        .handler = handle_nodebugchan,
        .summary = "Disable debugging on a channel",
        .usage = nodebugchan_help,
        .generator = complete_ch_4
    },
    {
        .cmda = { "set", "debug", NULL },
        .handler = handle_set_debug,
        .summary = "Set level of debug chattiness",
        .usage = set_debug_help
    },
    {
        .cmda = { "set", "verbose", NULL },
        .handler = handle_set_verbose,
        .summary = "Set level of verboseness",
        .usage = set_verbose_help
    },
    {
        .cmda = { "show", "channel", NULL },
        .handler = handle_showchan,
        .summary = "Display information on a specific channel",
        .usage = showchan_help,
        .generator = complete_ch_3
    },
    {
        .cmda = { "show", "channels", NULL },
        .handler = handle_chanlist,
        .summary = "Display information on channels",
        .usage = chanlist_help,
        .generator = complete_show_channels
    },
    {
        .cmda = { "show", "uptime", NULL },
        .handler = handle_showuptime,
        .summary = "Show uptime information",
        .usage = uptime_help
    },
    {
        .cmda = { "show", "version", NULL },
        .handler = handle_version,
        .summary = "Display version info",
        .usage = version_help
    },
    {
        .cmda = { "soft", "hangup", NULL },
        .handler = handle_softhangup,
        .summary = "Request a hangup on a given channel",
        .usage = softhangup_help,
        .generator = complete_ch_3
    },
};


static void join(char *dest, size_t destsize, char *w[], int tws)
{
    int x;
    /* Join words into a string */
    if (!dest || destsize < 1) {
        return;
    }
    dest[0] = '\0';
    for (x=0;w[x];x++) {
        if (x)
            strncat(dest, " ", destsize - strlen(dest) - 1);
        strncat(dest, w[x], destsize - strlen(dest) - 1);
    }
    if (tws && !cw_strlen_zero(dest))
        strncat(dest, " ", destsize - strlen(dest) - 1);
}

static char *find_best(char *argv[])
{
    static char cmdline[80];
    struct cw_clicmd *clicmd;
    int x;

    /* See how close we get, then print the  */
    char *myargv[CW_MAX_CMD_LEN];
    for (x=0;x<CW_MAX_CMD_LEN;x++)
        myargv[x]=NULL;
    for (x=0;argv[x];x++) {
        myargv[x] = argv[x];
        if (!(clicmd = find_cli(myargv, -1)))
            break;
        cw_object_put(clicmd);
    }
    join(cmdline, sizeof(cmdline), myargv, 0);
    return cmdline;
}


struct help_workhorse_args {
    char matchstr[80];
    int fd;
    int match;
};

static int help_workhorse_one(struct cw_object *obj, void *data)
{
    char fullcmd[80];
    struct cw_clicmd *clicmd = container_of(obj, struct cw_clicmd, obj);
    struct help_workhorse_args *args = data;

    join(fullcmd, sizeof(fullcmd), clicmd->cmda, 0);

    if (!args->match || !strncasecmp(args->matchstr, fullcmd, strlen(args->matchstr)))
        cw_cli(args->fd, "%25.25s  %s\n", fullcmd, clicmd->summary);

    return 0;
}

static int help_workhorse(int fd, char *match[])
{
    struct help_workhorse_args args = {
        .match = 0,
        .matchstr = "",
        .fd = fd,
    };

    if (match) {
        args.match = 1;
        join(args.matchstr, sizeof(args.matchstr), match, 0);
    }

    cw_registry_iterate(&clicmd_registry, help_workhorse_one, &args);
    return 0;
}

static int handle_help(int fd, int argc, char *argv[]) {
    struct cw_clicmd *clicmd;
    int ret;

    ret = RESULT_SHOWUSAGE;
    if (argc == 0)
        ret = RESULT_SUCCESS;
    else if (argc > 1) {
        ret = RESULT_SUCCESS;
        clicmd = find_cli(argv + 1, 1);
        if (clicmd) {
            if (clicmd->usage)
                cw_cli(fd, "%s", clicmd->usage);
            else
                cw_cli(fd, "No help available\n");
            cw_object_put(clicmd);
        } else {
            clicmd = find_cli(argv + 1, -1);
            if (clicmd) {
                ret = help_workhorse(fd, argv + 1);
                cw_object_put(clicmd);
            } else
                cw_cli(fd, "No such command\n");
        }
    } else
        ret = help_workhorse(fd, NULL);

    return ret;
}

static char *parse_args(char *s, int *argc, char *argv[], int max, int *trailingwhitespace)
{
    char *dup, *cur;
    int x = 0;
    int quoted = 0;
    int escaped = 0;
    int whitespace = 1;

    *trailingwhitespace = 0;
    if (!(dup = strdup(s)))
        return NULL;

    cur = dup;
    while (*s) {
        if ((*s == '"') && !escaped) {
            quoted = !quoted;
            if (quoted & whitespace) {
                /* If we're starting a quoted string, coming off white space, start a new argument */
                if (x >= (max - 1)) {
                    cw_log(CW_LOG_WARNING, "Too many arguments, truncating\n");
                    break;
                }
                argv[x++] = cur;
                whitespace = 0;
            }
            escaped = 0;
        } else if (((*s == ' ') || (*s == '\t')) && !(quoted || escaped)) {
            /* If we are not already in whitespace, and not in a quoted string or
               processing an escape sequence, and just entered whitespace, then
               finalize the previous argument and remember that we are in whitespace
            */
            if (!whitespace) {
                *(cur++) = '\0';
                whitespace = 1;
            }
        } else if ((*s == '\\') && !escaped) {
            escaped = 1;
        } else {
            if (whitespace) {
                /* If we are coming out of whitespace, start a new argument */
                if (x >= (max - 1)) {
                    cw_log(CW_LOG_WARNING, "Too many arguments, truncating\n");
                    break;
                }
                argv[x++] = cur;
                whitespace = 0;
            }
            *(cur++) = *s;
            escaped = 0;
        }
        s++;
    }
    /* Null terminate */
    *(cur++) = '\0';
    argv[x] = NULL;
    *argc = x;
    *trailingwhitespace = whitespace;
    return dup;
}


struct cli_generator_args {
    char matchstr[80];
    char *argv[CW_MAX_ARGS];
    char *word;
    int fd;
    int lastarg;
    int lastarg_len;
};

static int cli_generator_one(struct cw_object *obj, void *data)
{
    struct cw_clicmd *clicmd = container_of(obj, struct cw_clicmd, obj);
    struct cli_generator_args *args = data;
    int i;

    for (i = 0; i < args->lastarg; i++) {
        if (!clicmd->cmda[i] || strcasecmp(args->argv[i], clicmd->cmda[i]))
            break;
    }

    /* If we matched everything we were given we have a result. */
    if (i == args->lastarg && clicmd->cmda[i]) {
        if (!strncasecmp(clicmd->cmda[i], args->argv[args->lastarg], args->lastarg_len))
            cw_cli(args->fd, "%s\r\n", clicmd->cmda[i]);
    } else if (!clicmd->cmda[i] && clicmd->generator) {
        clicmd->generator(args->fd, args->argv, args->lastarg, args->lastarg_len);
    }

    return 0;
}

void cw_cli_generator(int fd, char *text)
{
    struct cli_generator_args args = {
        .fd = fd,
    };
    char *dup;
    int tws;

    if ((dup = parse_args(text, &args.lastarg, args.argv, arraysize(args.argv) - 1, &tws))) {
        join(args.matchstr, sizeof(args.matchstr), args.argv, tws);
	if (tws || args.lastarg == 0)
		args.argv[args.lastarg] = "";
	else
		args.lastarg--;

	args.lastarg_len = strlen(args.argv[args.lastarg]);

        cw_registry_iterate(&clicmd_registry, cli_generator_one, &args);
        free(dup);
    }
}


int cw_cli_command(int fd, char *s)
{
    char *argv[CW_MAX_ARGS];
    struct cw_clicmd *clicmd;
    int x;
    char *dup;
    int tws;

    if ((dup = parse_args(s, &x, argv, sizeof(argv) / sizeof(argv[0]), &tws))) {
        /* We need at least one entry, or ignore */
        if (x > 0) {
            clicmd = find_cli(argv, 0);
            if (clicmd) {
                switch(clicmd->handler(fd, x, argv)) {
                case RESULT_SHOWUSAGE:
                    cw_cli(fd, "%s", clicmd->usage);
                    break;
                }
                cw_object_put(clicmd);
            } else 
                cw_cli(fd, "No such command '%s' (type 'help' for help)\n", find_best(argv));
        }
        free(dup);
    } else {
        cw_log(CW_LOG_WARNING, "Out of memory\n");
        return -1;
    }
    return 0;
}


void cw_cli_init(void)
{
    cw_cli_register_multiple(builtins, arraysize(builtins));
}
