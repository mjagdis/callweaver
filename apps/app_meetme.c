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
 * \brief Meet me conference bridge
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include DAHDI_H

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/config.h"
#include "callweaver/app.h"
#include "callweaver/dsp.h"
#include "callweaver/musiconhold.h"
#include "callweaver/manager.h"
#include "callweaver/options.h"
#include "callweaver/cli.h"
#include "callweaver/say.h"
#include "callweaver/utils.h"
#include "callweaver/keywords.h"


static const char tdesc[] = "MeetMe conference bridge";

static void *app;
static void *app2;
static void *app3;

static const char name[] = "MeetMe";
static const char name2[] = "MeetMeCount";
static const char name3[] = "MeetMeAdmin";

static const char synopsis[] = "MeetMe conference bridge";
static const char synopsis2[] = "MeetMe participant count";
static const char synopsis3[] = "MeetMe conference Administration";

static const char syntax[] = "MeetMe([confno[, options[, pin]]])";
static const char syntax2[] = "MeetMeCount(confno[, var])";
static const char syntax3[] = "MeetMeAdmin(confno,command[, user])";

static const char descrip[] =
    "Enters the user into a specified MeetMe conference.\n"
    "If the conference number is omitted, the user will be prompted to enter\n"
    "one. \n"
    "MeetMe returns 0 if user pressed # to exit (see option 'p'), otherwise -1.\n"
    "Please note: A DAHDI INTERFACE MUST BE INSTALLED FOR CONFERENCING TO WORK!\n\n"

    "The option string may contain zero or more of the following characters:\n"
    "      'm' -- set monitor only mode (Listen only, no talking)\n"
    "      't' -- set talk only mode. (Talk only, no listening)\n"
    "      'T' -- set talker detection (sent to manager interface and meetme list)\n"
    "      'i' -- announce user join/leave\n"
    "      'p' -- allow user to exit the conference by pressing '#'\n"
    "      'X' -- allow user to exit the conference by entering a valid single\n"
    "             digit extension ${MEETME_EXIT_CONTEXT} or the current context\n"
    "             if that variable is not defined.\n"
    "      'd' -- dynamically add conference\n"
    "      'D' -- dynamically add conference, prompting for a PIN\n"
    "      'e' -- select an empty conference\n"
    "      'E' -- select an empty pinless conference\n"
    "      'v' -- video mode\n"
    "      'r' -- Record conference (records as ${MEETME_RECORDINGFILE}\n"
    "             using format ${MEETME_RECORDINGFORMAT}). Default filename is\n"
    "             meetme-conf-rec-${CONFNO}-${UNIQUEID} and the default format is wav.\n"
    "      'q' -- quiet mode (don't play enter/leave sounds)\n"
    "      'c' -- announce user(s) count on joining a conference\n"
    "      'M' -- enable music on hold when the conference has a single caller\n"
    "      'x' -- close the conference when last marked user exits\n"
    "      'w' -- wait until the marked user enters the conference\n"
    "      'b' -- run OGI script specified in ${MEETME_OGI_BACKGROUND}\n"
    "         Default: conf-background.ogi\n"
    "        (Note: This does not work with non-DAHDI channels in the same conference)\n"
    "      's' -- Present menu (user or admin) when '*' is received ('send' to menu)\n"
    "      'a' -- set admin mode\n"
    "      'A' -- set marked mode\n"
    "      'P' -- always prompt for the pin even if it is specified\n";

static char descrip2[] =
    "Plays back the number of users in the specified MeetMe conference.\n"
    "If used in an expression playback will be skipped and the value returned.\n"
    "If var is specified, playback will be skipped and the value\n"
    "will be returned in the variable. Returns 0 on success or -1 on a hangup.\n"
    "A DAHDI INTERFACE MUST BE INSTALLED FOR CONFERENCING FUNCTIONALITY.\n";

static char descrip3[] =
    "Run admin command for conference\n"
    "      'K' -- Kick all users out of conference\n"
    "      'k' -- Kick one user out of conference\n"
    "      'e' -- Eject last user that joined\n"
    "      'L' -- Lock conference\n"
    "      'l' -- Unlock conference\n"
    "      'M' -- Mute conference\n"
    "      'm' -- Unmute conference\n"
    "      'N' -- Mute entire conference (except admin)\n"
    "      'n' -- Unmute entire conference (except admin)\n"
    "";


static struct cw_conference
{
    char confno[CW_MAX_EXTENSION];        /* Conference */
    struct cw_channel *chan;  /* Announcements channel */
    int fd;                     /* Announcements fd */
    int dahdiconf;                /* DAHDI Conf # */
    int users;                  /* Number of active users */
    int markedusers;            /* Number of marked users */
    struct cw_conf_user *firstuser;  /* Pointer to the first user struct */
    struct cw_conf_user *lastuser;   /* Pointer to the last user struct */
    time_t start;               /* Start time (s) */
    int recording;              /* recording status */
    int isdynamic;              /* Created on the fly? */
    int locked;                 /* Is the conference locked? */
    pthread_t recordthread;     /* thread for recording */
    char *recordingfilename;    /* Filename to record the Conference into */
    char *recordingformat;      /* Format to record the Conference in */
    char pin[CW_MAX_EXTENSION];            /* If protected by a PIN */
    char pinadmin[CW_MAX_EXTENSION];    /* If protected by a admin PIN */
    struct cw_conference *next;
} *confs;

struct volume
{
    int desired;                /* Desired volume adjustment */
};

struct cw_conf_user
{
    int user_no;                /* User Number */
    struct cw_conf_user *prevuser;        /* Pointer to the previous user */
    struct cw_conf_user *nextuser;        /* Pointer to the next user */
    int userflags;              /* Flags as set in the conference */
    int adminflags;             /* Flags set by the Admin */
    struct cw_channel *chan;  /* Connected channel */
    int talking;                /* Is user talking */
    int dahdichannel;             /* Is a DAHDI channel */
    char usrvalue[50];          /* Custom User Value */
    char namerecloc[CW_MAX_EXTENSION];    /* Name Recorded file Location */
    time_t jointime;            /* Time the user joined the conference */
    struct volume talk;
    struct volume listen;
};

static int audio_buffers;         /* The number of audio buffers to be allocated on pseudo channels when in a conference */

#define DEFAULT_AUDIO_BUFFERS 32  /* each buffer is 20ms, so this is 640ms total */

#define ADMINFLAG_MUTED (1 << 1)  /* User is muted */
#define ADMINFLAG_KICKME (1 << 2) /* User is kicked */
#define MEETME_DELAYDETECTTALK         300
#define MEETME_DELAYDETECTENDTALK     1000

enum volume_action
{
    VOL_UP,
    VOL_DOWN,
};

CW_MUTEX_DEFINE_STATIC(conflock);

static int admin_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result);

static void *recordthread(void *args);

#include "enter.h"
#include "leave.h"

#define ENTER    0
#define LEAVE    1

#define MEETME_RECORD_OFF           0
#define MEETME_RECORD_ACTIVE        1
#define MEETME_RECORD_TERMINATE     2

#define CONF_SIZE 320

#define CONFFLAG_ADMIN    (1 << 1)    /* If set the user has admin access on the conference */
#define CONFFLAG_MONITOR (1 << 2)    /* If set the user can only receive audio from the conference */
#define CONFFLAG_POUNDEXIT (1 << 3)    /* If set callweaver will exit conference when '#' is pressed */
#define CONFFLAG_STARMENU (1 << 4)    /* If set callweaver will provide a menu to the user what '*' is pressed */
#define CONFFLAG_TALKER (1 << 5)    /* If set the use can only send audio to the conference */
#define CONFFLAG_QUIET (1 << 6)        /* If set there will be no enter or leave sounds */
#define CONFFLAG_VIDEO (1 << 7)        /* Set to enable video mode */
#define CONFFLAG_OGI (1 << 8)        /* Set to run OGI Script in Background */
#define CONFFLAG_MOH (1 << 9)        /* Set to have music on hold when user is alone in conference */
#define CONFFLAG_MARKEDEXIT (1 << 10)    /* If set the MeetMe will return if all marked with this flag left */
#define CONFFLAG_WAITMARKED (1 << 11)    /* If set, the MeetMe will wait until a marked user enters */
#define CONFFLAG_EXIT_CONTEXT (1 << 12)    /* If set, the MeetMe will exit to the specified context */
#define CONFFLAG_MARKEDUSER (1 << 13)    /* If set, the user will be marked */
#define CONFFLAG_INTROUSER (1 << 14)    /* If set, user will be ask record name on entry of conference */
#define CONFFLAG_RECORDCONF (1<< 15)    /* If set, the MeetMe will be recorded */
#define CONFFLAG_MONITORTALKER (1 << 16) /* If set, the user will be monitored if the user is talking or not */
#define CONFFLAG_DYNAMIC (1 << 17)
#define CONFFLAG_DYNAMICPIN (1 << 18)
#define CONFFLAG_EMPTY (1 << 19)
#define CONFFLAG_EMPTYNOPIN (1 << 20)
#define CONFFLAG_ALWAYSPROMPT (1 << 21)
#define CONFFLAG_ANNOUNCEUSERCOUNT (1 << 22) /* If set, when user joins the conference, they will be told the number of users that are already in */


CW_DECLARE_OPTIONS(meetme_opts,{
    ['a'] = { CONFFLAG_ADMIN },
    ['c'] = { CONFFLAG_ANNOUNCEUSERCOUNT },
    ['T'] = { CONFFLAG_MONITORTALKER },
    ['i'] = { CONFFLAG_INTROUSER },
    ['m'] = { CONFFLAG_MONITOR },
    ['p'] = { CONFFLAG_POUNDEXIT },
    ['s'] = { CONFFLAG_STARMENU },
    ['t'] = { CONFFLAG_TALKER },
    ['q'] = { CONFFLAG_QUIET },
    ['M'] = { CONFFLAG_MOH },
    ['x'] = { CONFFLAG_MARKEDEXIT },
    ['X'] = { CONFFLAG_EXIT_CONTEXT },
    ['A'] = { CONFFLAG_MARKEDUSER },
    ['b'] = { CONFFLAG_OGI },
    ['w'] = { CONFFLAG_WAITMARKED },
    ['r'] = { CONFFLAG_RECORDCONF },
    ['d'] = { CONFFLAG_DYNAMIC },
    ['D'] = { CONFFLAG_DYNAMICPIN },
    ['e'] = { CONFFLAG_EMPTY },
    ['E'] = { CONFFLAG_EMPTYNOPIN },
    ['P'] = { CONFFLAG_ALWAYSPROMPT },
});

static const char *istalking(int x)
{
    if (x > 0)
        return "(talking)";
    else if (x < 0)
        return "(unmonitored)";
    else
        return "(not talking)";
}

static int careful_write(int fd, unsigned char *data, int len, int block)
{
    int res;
    int x;
    while (len)
    {
        if (block)
        {
            x = DAHDI_IOMUX_WRITE | DAHDI_IOMUX_SIGEVENT;
            res = ioctl(fd, DAHDI_IOMUX, &x);
        }
        else
            res = 0;

        if (res >= 0)
            res = write(fd, data, len);
        if (res < 1)
        {
            if (errno != EAGAIN)
            {
                cw_log(CW_LOG_WARNING, "Failed to write audio data to conference: %s\n", strerror(errno));
                return -1;
            }
            return 0;
        }
        len -= res;
        data += res;
    }
    return 0;
}

/* Map 'volume' levels from -5 through +5 into
   decibel (dB) settings for channel drivers
   Note: these are not a straight linear-to-dB
   conversion... the numbers have been modified
   to give the user a better level of adjustability
*/
static signed char gain_map[] =
{
    -15,
    -13,
    -10,
    -6,
    0,
    0,
    0,
    6,
    10,
    13,
    15
};

static void tweak_volume(struct volume *vol, enum volume_action action)
{
    switch (action)
    {
    case VOL_UP:
        switch (vol->desired)
        {
        case 5:
            break;
        case 0:
            vol->desired = 2;
            break;
        case -2:
            vol->desired = 0;
            break;
        default:
            vol->desired++;
            break;
        }
        break;
    case VOL_DOWN:
        switch (vol->desired)
        {
        case -5:
            break;
        case 2:
            vol->desired = 0;
            break;
        case 0:
            vol->desired = -2;
            break;
        default:
            vol->desired--;
            break;
        }
    }
}

static void tweak_talk_volume(struct cw_conf_user *user, enum volume_action action)
{
    tweak_volume(&user->talk, action);
    cw_channel_setoption(user->chan, CW_OPTION_RXGAIN, gain_map[user->talk.desired + 5]);
}

static void tweak_listen_volume(struct cw_conf_user *user, enum volume_action action)
{
    tweak_volume(&user->listen, action);
    cw_channel_setoption(user->chan, CW_OPTION_TXGAIN, gain_map[user->talk.desired + 5]);
}

static void reset_volumes(struct cw_conf_user *user)
{
    cw_channel_setoption(user->chan, CW_OPTION_TXGAIN, 0);
    cw_channel_setoption(user->chan, CW_OPTION_RXGAIN, 0);
}

static void conf_play(struct cw_channel *chan, struct cw_conference *conf, int sound)
{
    unsigned char *data;
    int len;
    int res=-1;

    if (!chan->_softhangup)
        res = cw_autoservice_start(chan);
    cw_mutex_lock(&conflock);
    switch (sound)
    {
    case ENTER:
        data = enter;
        len = sizeof(enter);
        break;
    case LEAVE:
        data = leave;
        len = sizeof(leave);
        break;
    default:
        data = NULL;
        len = 0;
    }
    if (data)
        careful_write(conf->fd, data, len, 1);
    cw_mutex_unlock(&conflock);
    if (!res)
        cw_autoservice_stop(chan);
}

static struct cw_conference *build_conf(const char *confno, const char *pin, const char *pinadmin, int make, int dynamic)
{
    struct cw_conference *cnf;
    struct dahdi_confinfo ztc;
    cw_mutex_lock(&conflock);
    cnf = confs;
    while (cnf)
    {
        if (!strcmp(confno, cnf->confno))
            break;
        cnf = cnf->next;
    }
    if (!cnf && (make || dynamic))
    {
        cnf = malloc(sizeof(struct cw_conference));
        if (cnf)
        {
            /* Make a new one */
            memset(cnf, 0, sizeof(struct cw_conference));
            cw_copy_string(cnf->confno, confno, sizeof(cnf->confno));
            cw_copy_string(cnf->pin, pin, sizeof(cnf->pin));
            cw_copy_string(cnf->pinadmin, pinadmin, sizeof(cnf->pinadmin));
            cnf->markedusers = 0;
            cnf->chan = cw_request("DAHDI", CW_FORMAT_ULAW, (void *)"pseudo", NULL);
            if (cnf->chan)
            {
                cnf->fd = cnf->chan->fds[0];    /* for use by conf_play() */
            }
            else
            {
                cw_log(CW_LOG_WARNING, "Unable to open pseudo channel - trying device\n");
                cnf->fd = open("/dev/dahdi/pseudo", O_RDWR);
                if (cnf->fd < 0)
                {
                    cw_log(CW_LOG_WARNING, "Unable to open pseudo device\n");
                    free(cnf);
                    cnf = NULL;
                    goto cnfout;
                }
            }
            memset(&ztc, 0, sizeof(ztc));
            /* Setup a new DAHDI conference */
            ztc.chan = 0;
            ztc.confno = -1;
            ztc.confmode = DAHDI_CONF_CONFANN | DAHDI_CONF_CONFANNMON;
            if (ioctl(cnf->fd, DAHDI_SETCONF, &ztc))
            {
                cw_log(CW_LOG_WARNING, "Error setting conference\n");
                if (cnf->chan)
                    cw_hangup(cnf->chan);
                else
                    close(cnf->fd);
                free(cnf);
                cnf = NULL;
                goto cnfout;
            }
            /* Fill the conference struct */
            cnf->start = time(NULL);
            cnf->dahdiconf = ztc.confno;
            cnf->isdynamic = dynamic;
            cnf->firstuser = NULL;
            cnf->lastuser = NULL;
            cnf->locked = 0;
            if (option_verbose > 2)
                cw_verbose(VERBOSE_PREFIX_3 "Created MeetMe conference %d for conference '%s'\n", cnf->dahdiconf, cnf->confno);
            cnf->next = confs;
            confs = cnf;
        }
        else
            cw_log(CW_LOG_WARNING, "Out of memory\n");
    }
cnfout:
    cw_mutex_unlock(&conflock);
    return cnf;
}

static int confs_show(struct cw_dynstr *ds_p, int argc, char **argv)
{
    CW_UNUSED(argc);
    CW_UNUSED(argv);

    cw_dynstr_printf(ds_p, "Deprecated! Please use 'meetme' instead.\n");
    return RESULT_SUCCESS;
}

static const char show_confs_usage[] =
    "Deprecated! Please use 'meetme' instead.\n";

static struct cw_clicmd cli_show_confs =
{
    .cmda = {"show", "conferences", NULL},
    .handler = confs_show,
    .summary = "Show status of conferences",
    .usage = show_confs_usage,
};

static int conf_cmd(struct cw_dynstr *ds_p, int argc, char **argv)
{
    /* Process the command */
    char buf[1024] = "";
    const char header_format[] = "%-14s %-14s %-10s %-8s  %-8s\n";
    const char data_format[] = "%-12.12s   %4.4d          %4.4s       %02d:%02d:%02d  %-8s\n";
    struct cw_conference *cnf;
    struct cw_conf_user *user;
    int hr, min, sec;
    int i = 0, total = 0;
    time_t now;

    if (argc > 8)
        cw_dynstr_printf(ds_p, "Invalid Arguments.\n");
    /* Check for length so no buffer will overflow... */
    for (i = 0; i < argc; i++)
    {
        if (strlen(argv[i]) > 100)
            cw_dynstr_printf(ds_p, "Invalid Arguments.\n");
    }
    if (argc == 1)
    {
        /* 'MeetMe': List all the conferences */
        now = time(NULL);
        cnf = confs;
        if (!cnf)
        {
            cw_dynstr_printf(ds_p, "No active MeetMe conferences.\n");
            return RESULT_SUCCESS;
        }
        cw_dynstr_printf(ds_p, header_format, "Conf Num", "Parties", "Marked", "Activity", "Creation");
        while (cnf)
        {
            if (cnf->markedusers == 0)
                strcpy(buf, "N/A ");
            else
                snprintf(buf, sizeof(buf), "%4.4d", cnf->markedusers);
            hr = (now - cnf->start) / 3600;
            min = ((now - cnf->start) % 3600) / 60;
            sec = (now - cnf->start) % 60;

            cw_dynstr_printf(ds_p, data_format, cnf->confno, cnf->users, buf, hr, min, sec, cnf->isdynamic ? "Dynamic" : "Static");

            total += cnf->users;
            cnf = cnf->next;
        }
        cw_dynstr_printf(ds_p, "* Total number of MeetMe users: %d\n", total);
        return RESULT_SUCCESS;
    }
    if (argc < 3)
        return RESULT_SHOWUSAGE;

    if (strstr(argv[1], "lock"))
    {
        argv[3] = (char *)(strcmp(argv[1], "lock") == 0 ? "L" : "l");
        argc = 2;
    }
    else if (strstr(argv[1], "mute"))
    {
        if (argc < 4)
            return RESULT_SHOWUSAGE;
        if (strcmp(argv[1], "mute") == 0)
        {
            /* Mute */
            if (strcmp(argv[3], "all") == 0)
            {
                argv[3] = (char *)"N";
                argc = 2;
            }
            else
            {
                argv[4] = argv[3];
                argv[3] = (char *)"M";
                argc = 3;
            }
        }
        else
        {
            /* Unmute */
            if (strcmp(argv[3], "all") == 0)
            {
                argv[3] = (char *)"n";
                argc = 2;
            }
            else
            {
                argv[4] = argv[3];
                argv[3] = (char *)"m";
                argc = 3;
            }
        }
    }
    else if (strcmp(argv[1], "kick") == 0)
    {
        if (argc < 4)
            return RESULT_SHOWUSAGE;
        if (strcmp(argv[3], "all") == 0)
        {
            /* Kick all */
            argv[3] = (char *)"K";
            argc = 2;
        }
        else
        {
            /* Kick a single user */
            argv[4] = argv[3];
            argv[3] = (char *)"k";
            argc = 3;
        }
    }
    else if(strcmp(argv[1], "list") == 0)
    {
        /* List all the users in a conference */
        if (!confs)
        {
            cw_dynstr_printf(ds_p, "No active conferences.\n");
            return RESULT_SUCCESS;
        }
        cnf = confs;
        /* Find the right conference */
        while (cnf)
        {
            if (strcmp(cnf->confno, argv[2]) == 0)
                break;
            if (cnf->next)
            {
                cnf = cnf->next;
            }
            else
            {
                cw_dynstr_printf(ds_p, "No such conference: %s.\n",argv[2]);
                return RESULT_SUCCESS;
            }
        }
        /* Show all the users */
        user = cnf->firstuser;
        while (user)
        {
            cw_dynstr_printf(ds_p, "User #: %-2.2d %12.12s %-20.20s Channel: %s %s %s %s %s\n", user->user_no, user->chan->cid.cid_num ? user->chan->cid.cid_num : "<unknown>", user->chan->cid.cid_name ? user->chan->cid.cid_name : "<no name>", user->chan->name, (user->userflags & CONFFLAG_ADMIN) ? "(Admin)" : "", (user->userflags & CONFFLAG_MONITOR) ? "(Listen only)" : "", (user->adminflags & ADMINFLAG_MUTED) ? "(Admn Muted)" : "", istalking(user->talking));
            user = user->nextuser;
        }
        cw_dynstr_printf(ds_p,"%d users in that conference.\n",cnf->users);
        return RESULT_SUCCESS;
    }
    else
        return RESULT_SHOWUSAGE;

    admin_exec(NULL, argc, argv + 2, NULL);
    return 0;
}

static void complete_confcmd(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
    static const char *cmds[] = {"lock", "unlock", "mute", "unmute", "kick", "list"};
    int x = 0;
    struct cw_conference *cnf = NULL;
    struct cw_conf_user *usr = NULL;
    char usrno[50] = "";

    if (lastarg == 1)
    {
        /* Command */
        for (x = 0; x < arraysize(cmds); x++)
        {
            if (!strncasecmp(cmds[x], argv[1], lastarg_len))
                cw_dynstr_printf(ds_p, "%s\n", cmds[x]);
        }
    }
    else if (lastarg == 2)
    {
        /* Conference Number */
        cw_mutex_lock(&conflock);
        for (cnf = confs; cnf; cnf = cnf->next)
        {
            if (!strncasecmp(argv[2], cnf->confno, lastarg_len))
                cw_dynstr_printf(ds_p, "%s\n", cnf->confno);
        }
        cw_mutex_unlock(&conflock);
    }
    else if (lastarg == 3)
    {
        /* User Number || Conf Command option*/
        if (!strcmp(argv[1], "mute") || !strcmp(argv[1], "kick"))
        {
            if ((!strcmp(argv[1], "kick") || !strcmp(argv[1],"mute")) && !(strncasecmp(argv[3], "all", lastarg_len)))
                cw_dynstr_printf(ds_p, "all\n");

            cw_mutex_lock(&conflock);

            for (cnf = confs; cnf; cnf = cnf->next)
            {
                if (!strcmp(argv[2], cnf->confno))
		{
                    /* Search for the user */
                    for (usr = cnf->firstuser; usr; usr = usr->nextuser)
                    {
                        snprintf(usrno, sizeof(usrno), "%d", usr->user_no);
                        if (!strncasecmp(argv[3], usrno, lastarg_len))
                            cw_dynstr_printf(ds_p, "%s\n", usrno);
                    }
		}
            }

            cw_mutex_unlock(&conflock);
        }
    }
}

static const char conf_usage[] =
    "Usage: meetme  (un)lock|(un)mute|kick|list <confno> <usernumber>\n"
    "       Executes a command for the conference or on a conferee\n";

static struct cw_clicmd cli_conf = {
        .cmda = { "meetme", NULL, NULL },
        .handler = conf_cmd,
        .summary = "Execute a command on a conference or conferee",
	.usage = conf_usage,
	.generator = complete_confcmd,
};

static void conf_flush(int fd, struct cw_channel *chan)
{
    int x;

    /* read any frames that may be waiting on the channel and throw them away */
    if (chan) {
        struct cw_frame *f;

        /* when no frames are available, this will wait for 1 millisecond maximum */
        while (cw_waitfor(chan, 1)) {
            if ((f = cw_read(chan)))
                cw_fr_free(f);
        }
    }

    /* flush any data sitting in the pseudo channel */
    x = DAHDI_FLUSH_ALL;
    if (ioctl(fd, DAHDI_FLUSH, &x))
        cw_log(CW_LOG_WARNING, "Error flushing channel\n");
}

/* Remove the conference from the list and free it.
   We assume that this was called while holding conflock. */
static int conf_free(struct cw_conference *conf)
{
    struct cw_conference *prev = NULL, *cur = confs;

    while (cur)
    {
        if (cur == conf)
        {
            if (prev)
                prev->next = conf->next;
            else
                confs = conf->next;
            break;
        }
        prev = cur;
        cur = cur->next;
    }

    if (!cur)
        cw_log(CW_LOG_WARNING, "Conference not found\n");

    if (conf->recording == MEETME_RECORD_ACTIVE)
    {
        conf->recording = MEETME_RECORD_TERMINATE;
        cw_mutex_unlock(&conflock);
        while (1)
        {
            cw_mutex_lock(&conflock);
            if (conf->recording == MEETME_RECORD_OFF)
                break;
            cw_mutex_unlock(&conflock);
        }
    }

    if (conf->chan)
        cw_hangup(conf->chan);
    else
        close(conf->fd);

    free(conf);

    return 0;
}

static int conf_run(struct cw_channel *chan, struct cw_conference *conf, int confflags)
{
    char ogifiledefault[] = "conf-background.ogi";
    struct cw_conf_user *user = malloc(sizeof(struct cw_conf_user));
    struct cw_conf_user *usr = NULL;
    struct cw_var_t *var;
    int fd;
    struct dahdi_confinfo ztc, ztc_empty;
    struct cw_frame *f;
    struct cw_channel *c;
    struct cw_frame fr;
    int outfd;
    int ms;
    int nfds;
    int res;
    int flags;
    int retrydahdi;
    int origfd;
    int musiconhold = 0;
    int firstpass = 0;
    int lastmarked = 0;
    int currentmarked = 0;
    int ret = -1;
    int x;
    int menu_active = 0;
    int using_pseudo = 0;
    int duration=20;
    struct cw_dsp *dsp=NULL;

    char meetmesecs[30] = "";
    char exitcontext[CW_MAX_CONTEXT] = "";
    char recordingtmp[CW_MAX_EXTENSION] = "";
    int dtmf;

    struct dahdi_bufferinfo bi;
    char __buf[CONF_SIZE + CW_FRIENDLY_OFFSET];
    char *buf = __buf + CW_FRIENDLY_OFFSET;

    if (!user)
    {
        cw_log(CW_LOG_ERROR, "Out of memory\n");
        return(ret);
    }
    memset(user, 0, sizeof(struct cw_conf_user));

    if (confflags & CONFFLAG_RECORDCONF && conf->recording !=MEETME_RECORD_ACTIVE)
    {
        if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_MEETME_RECORDINGFILE, "MEETME_RECORDINGFILE"))) {
            conf->recordingfilename = cw_strdupa(var->value);
            cw_object_put(var);
        } else {
            snprintf(recordingtmp,sizeof(recordingtmp),"meetme-conf-rec-%s-%s",conf->confno,chan->uniqueid);
            conf->recordingfilename = cw_strdupa(recordingtmp);
        }
        if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_MEETME_RECORDINGFORMAT, "MEETME_RECORDINGFORMAT"))) {
            conf->recordingformat = cw_strdupa(var->value);
            cw_object_put(var);
        } else {
            snprintf(recordingtmp,sizeof(recordingtmp), "wav");
            conf->recordingformat = cw_strdupa(recordingtmp);
        }
        cw_verbose(VERBOSE_PREFIX_4 "Starting recording of MeetMe Conference %s into file %s.%s.\n", conf->confno, conf->recordingfilename, conf->recordingformat);
        cw_pthread_create(&conf->recordthread, &global_attr_detached, recordthread, conf);
    }

    user->user_no = 0; /* User number 0 means starting up user! (dead - not in the list!) */

    time(&user->jointime);

    if (conf->locked)
    {
        /* Sorry, but this confernce is locked! */
        if (!cw_streamfile(chan, "conf-locked", chan->language))
            cw_waitstream(chan, "");
        goto outrun;
    }

    if (confflags & CONFFLAG_MARKEDUSER)
        conf->markedusers++;

    cw_mutex_lock(&conflock);
    if (conf->firstuser == NULL)
    {
        /* Fill the first new User struct */
        user->user_no = 1;
        user->nextuser = NULL;
        user->prevuser = NULL;
        conf->firstuser = user;
        conf->lastuser = user;
    }
    else
    {
        /* Fill the new user struct */
        user->user_no = conf->lastuser->user_no + 1;
        user->prevuser = conf->lastuser;
        user->nextuser = NULL;
        if (conf->lastuser->nextuser != NULL)
        {
            cw_log(CW_LOG_WARNING, "Error in User Management!\n");
            cw_mutex_unlock(&conflock);
            goto outrun;
        }
        else
        {
            conf->lastuser->nextuser = user;
            conf->lastuser = user;
        }
    }
    user->chan = chan;
    user->userflags = confflags;
    user->adminflags = 0;
    user->talking = -1;
    cw_mutex_unlock(&conflock);
    if (confflags & CONFFLAG_EXIT_CONTEXT)
    {
        if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_MEETME_EXIT_CONTEXT, "MEETME_EXIT_CONTEXT"))) {
            cw_copy_string(exitcontext, var->value, sizeof(exitcontext));
            cw_object_put(var);
	} else if (!cw_strlen_zero(chan->proc_context))
            cw_copy_string(exitcontext, chan->proc_context, sizeof(exitcontext));
        else
            cw_copy_string(exitcontext, chan->context, sizeof(exitcontext));
    }

    if (!(confflags & CONFFLAG_QUIET) && (confflags & CONFFLAG_INTROUSER))
    {
        snprintf(user->namerecloc,sizeof(user->namerecloc),"%s/meetme/meetme-username-%s-%d",cw_config[CW_SPOOL_DIR],conf->confno,user->user_no);
        cw_record_review(chan,"vm-rec-name",user->namerecloc, 10,"sln", &duration, NULL);
    }

    conf->users++;

    if (!(confflags & CONFFLAG_QUIET))
    {
        if (conf->users == 1 && !(confflags & CONFFLAG_WAITMARKED))
            if (!cw_streamfile(chan, "conf-onlyperson", chan->language))
                cw_waitstream(chan, "");
        if ((confflags & CONFFLAG_WAITMARKED) && conf->markedusers == 0)
            if (!cw_streamfile(chan, "conf-waitforleader", chan->language))
                cw_waitstream(chan, "");
    }

    if (!(confflags & CONFFLAG_QUIET) && (confflags & CONFFLAG_ANNOUNCEUSERCOUNT) && conf->users > 1)
    {
        int keepplaying=1;

        if (conf->users == 2)
        {
            if (!cw_streamfile(chan,"conf-onlyone",chan->language))
            {
                res = cw_waitstream(chan, CW_DIGIT_ANY);
                if (res > 0)
                    keepplaying=0;
                else if (res == -1)
                    goto outrun;
            }
        }
        else
        {
            if (!cw_streamfile(chan, "conf-thereare", chan->language))
            {
                res = cw_waitstream(chan, CW_DIGIT_ANY);
                if (res > 0)
                    keepplaying=0;
                else if (res == -1)
                    goto outrun;
            }
            if (keepplaying)
            {
                res = cw_say_number(chan, conf->users - 1, CW_DIGIT_ANY, chan->language, (char *) NULL);
                if (res > 0)
                    keepplaying=0;
                else if (res == -1)
                    goto outrun;
            }
            if (keepplaying && !cw_streamfile(chan, "conf-otherinparty", chan->language))
            {
                res = cw_waitstream(chan, CW_DIGIT_ANY);
                if (res > 0)
                    keepplaying=0;
                else if (res == -1)
                    goto outrun;
            }
        }
    }

    /* Set it into linear mode (write) */
    if (cw_set_write_format(chan, CW_FORMAT_SLINEAR) < 0)
    {
        cw_log(CW_LOG_WARNING, "Unable to set '%s' to write linear mode\n", chan->name);
        goto outrun;
    }

    /* Set it into linear mode (read) */
    if (cw_set_read_format(chan, CW_FORMAT_SLINEAR) < 0)
    {
        cw_log(CW_LOG_WARNING, "Unable to set '%s' to read linear mode\n", chan->name);
        goto outrun;
    }
    cw_indicate(chan, -1);
    retrydahdi = strcmp(chan->type, "DAHDI");
    user->dahdichannel = !retrydahdi;
dahdiretry:
    origfd = chan->fds[0];
    if (retrydahdi)
    {
        fd = open("/dev/dahdi/pseudo", O_RDWR);
        if (fd < 0)
        {
            cw_log(CW_LOG_WARNING, "Unable to open pseudo channel: %s\n", strerror(errno));
            goto outrun;
        }
        using_pseudo = 1;
        /* Make non-blocking */
        flags = fcntl(fd, F_GETFL);
        if (flags < 0)
        {
            cw_log(CW_LOG_WARNING, "Unable to get flags: %s\n", strerror(errno));
            close(fd);
            goto outrun;
        }
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
        {
            cw_log(CW_LOG_WARNING, "Unable to set flags: %s\n", strerror(errno));
            close(fd);
            goto outrun;
        }
        /* Setup buffering information */
        memset(&bi, 0, sizeof(bi));
        bi.bufsize = CONF_SIZE/2;
        bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
        bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
        bi.numbufs = audio_buffers;
        if (ioctl(fd, DAHDI_SET_BUFINFO, &bi))
        {
            cw_log(CW_LOG_WARNING, "Unable to set buffering information: %s\n", strerror(errno));
            close(fd);
            goto outrun;
        }
        x = 1;
        if (ioctl(fd, DAHDI_SETLINEAR, &x))
        {
            cw_log(CW_LOG_WARNING, "Unable to set linear mode: %s\n", strerror(errno));
            close(fd);
            goto outrun;
        }
        nfds = 1;
    }
    else
    {
        /* XXX Make sure we're not running on a pseudo channel XXX */
        fd = chan->fds[0];
        nfds = 0;
    }
    memset(&ztc, 0, sizeof(ztc));
    memset(&ztc_empty, 0, sizeof(ztc_empty));
    /* Check to see if we're in a conference... */
    ztc.chan = 0;
    if (ioctl(fd, DAHDI_GETCONF, &ztc))
    {
        cw_log(CW_LOG_WARNING, "Error getting conference\n");
        close(fd);
        goto outrun;
    }
    if (ztc.confmode)
    {
        /* Whoa, already in a conference...  Retry... */
        if (!retrydahdi)
        {
            cw_log(CW_LOG_DEBUG, "DAHDI channel is in a conference already, retrying with pseudo\n");
            retrydahdi = 1;
            goto dahdiretry;
        }
    }
    memset(&ztc, 0, sizeof(ztc));
    /* Add us to the conference */
    ztc.chan = 0;
    ztc.confno = conf->dahdiconf;
    cw_mutex_lock(&conflock);
    if (!(confflags & CONFFLAG_QUIET) && (confflags & CONFFLAG_INTROUSER) && conf->users > 1)
    {
        if (conf->chan && cw_fileexists(user->namerecloc, NULL, NULL))
        {
            if (!cw_streamfile(conf->chan, user->namerecloc, chan->language))
                cw_waitstream(conf->chan, "");
            if (!cw_streamfile(conf->chan, "conf-hasjoin", chan->language))
                cw_waitstream(conf->chan, "");
        }
    }

    if (confflags & CONFFLAG_MONITOR)
        ztc.confmode = DAHDI_CONF_CONFMON | DAHDI_CONF_LISTENER;
    else if (confflags & CONFFLAG_TALKER)
        ztc.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER;
    else
        ztc.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;

    if (ioctl(fd, DAHDI_SETCONF, &ztc))
    {
        cw_log(CW_LOG_WARNING, "Error setting conference\n");
        close(fd);
        cw_mutex_unlock(&conflock);
        goto outrun;
    }
    cw_log(CW_LOG_DEBUG, "Placed channel %s in DAHDI conf %d\n", chan->name, conf->dahdiconf);

    cw_manager_event(CW_EVENT_FLAG_CALL, "MeetmeJoin",
		  4,
                  cw_msg_tuple("Channel",  "%s", chan->name),
                  cw_msg_tuple("Uniqueid", "%s", chan->uniqueid),
                  cw_msg_tuple("Meetme",   "%s", conf->confno),
                  cw_msg_tuple("Usernum",  "%d", user->user_no)
    );

    if (!firstpass && !(confflags & CONFFLAG_MONITOR) && !(confflags & CONFFLAG_ADMIN))
    {
        firstpass = 1;
        if (!(confflags & CONFFLAG_QUIET))
            if (!(confflags & CONFFLAG_WAITMARKED) || (conf->markedusers >= 1))
                conf_play(chan, conf, ENTER);
    }
    conf_flush(fd, chan);
    cw_mutex_unlock(&conflock);
    if (confflags & CONFFLAG_OGI)
    {

        if (user->dahdichannel)
        {
            /*  Set CONFMUTE mode on DAHDI channel to mute DTMF tones */
            cw_channel_setoption(user->chan, CW_OPTION_TONE_VERIFY, 1);
        }

        /* Get name of OGI file to run from $(MEETME_OGI_BACKGROUND)
          or use default filename of conf-background.ogi */
        if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_MEETME_OGI_BACKGROUND, "MEETME_OGI_BACKGROUND"))) {
            char *tmp;
            if ((tmp = strdup(var->value))) {
                ret = cw_function_exec_str(chan, CW_KEYWORD_OGI, "OGI", tmp, NULL);
                free(tmp);
            } else {
                cw_log(CW_LOG_ERROR, "Out of memory!\n");
                ret = -1;
            }
            cw_object_put(var);
        } else
            ret = cw_function_exec_str(chan, CW_KEYWORD_OGI, "OGI", ogifiledefault, NULL);

        if (user->dahdichannel)
        {
            /*  Remove CONFMUTE mode on DAHDI channel */
            cw_channel_setoption(user->chan, CW_OPTION_TONE_VERIFY, 0);
        }
    }
    else
    {
        if (user->dahdichannel && (confflags & CONFFLAG_STARMENU))
        {
            /*  Set CONFMUTE mode on DAHDI channel to mute DTMF tones when the menu is enabled */
            cw_channel_setoption(user->chan, CW_OPTION_TONE_VERIFY, 1);
        }
        if (confflags &  CONFFLAG_MONITORTALKER && !(dsp = cw_dsp_new()))
        {
            cw_log(CW_LOG_WARNING, "Unable to allocate DSP!\n");
            res = -1;
        }
        for(;;)
        {
            int menu_was_active = 0;

            outfd = -1;
            ms = -1;

            /* if we have just exited from the menu, and the user had a channel-driver
               volume adjustment, restore it
            */
            if (!menu_active  &&  menu_was_active  &&  user->listen.desired)
                cw_channel_setoption(user->chan, CW_OPTION_RXGAIN, gain_map[user->talk.desired + 5]);

            menu_was_active = menu_active;

            currentmarked = conf->markedusers;
            if (!(confflags & CONFFLAG_QUIET)  &&  (confflags & CONFFLAG_MARKEDUSER) && (confflags & CONFFLAG_WAITMARKED) && lastmarked == 0)
            {
                if (currentmarked == 1 && conf->users > 1)
                {
                    cw_say_number(chan, conf->users - 1, CW_DIGIT_ANY, chan->language, (char *) NULL);
                    if (conf->users - 1 == 1)
                    {
                        if (!cw_streamfile(chan, "conf-userwilljoin", chan->language))
                            cw_waitstream(chan, "");
                    }
                    else
                    {
                        if (!cw_streamfile(chan, "conf-userswilljoin", chan->language))
                            cw_waitstream(chan, "");
                    }
                }
                if (conf->users == 1 && ! (confflags & CONFFLAG_MARKEDUSER))
                    if (!cw_streamfile(chan, "conf-onlyperson", chan->language))
                        cw_waitstream(chan, "");
            }

            c = cw_waitfor_nandfds(&chan, 1, &fd, nfds, NULL, &outfd, &ms);

            /* Update the struct with the actual confflags */
            user->userflags = confflags;

            if (confflags & CONFFLAG_WAITMARKED)
            {
                if(currentmarked == 0)
                {
                    if (lastmarked != 0)
                    {
                        if (!(confflags & CONFFLAG_QUIET))
                            if (!cw_streamfile(chan, "conf-leaderhasleft", chan->language))
                                cw_waitstream(chan, "");
                        if(confflags & CONFFLAG_MARKEDEXIT)
                            break;
                        else
                        {
                            ztc.confmode = DAHDI_CONF_CONF;
                            if (ioctl(fd, DAHDI_SETCONF, &ztc))
                            {
                                cw_log(CW_LOG_WARNING, "Error setting conference\n");
                                close(fd);
                                goto outrun;
                            }
                        }
                    }
                    if (musiconhold == 0 && (confflags & CONFFLAG_MOH))
                    {
                        cw_moh_start(chan, NULL);
                        musiconhold = 1;
                    }
                    else
                    {
                        ztc.confmode = DAHDI_CONF_CONF;
                        if (ioctl(fd, DAHDI_SETCONF, &ztc))
                        {
                            cw_log(CW_LOG_WARNING, "Error setting conference\n");
                            close(fd);
                            goto outrun;
                        }
                    }
                }
                else if(currentmarked >= 1 && lastmarked == 0)
                {
                    if (confflags & CONFFLAG_MONITOR)
                        ztc.confmode = DAHDI_CONF_CONFMON | DAHDI_CONF_LISTENER;
                    else if (confflags & CONFFLAG_TALKER)
                        ztc.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER;
                    else
                        ztc.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
                    if (ioctl(fd, DAHDI_SETCONF, &ztc))
                    {
                        cw_log(CW_LOG_WARNING, "Error setting conference\n");
                        close(fd);
                        goto outrun;
                    }
                    if (musiconhold && (confflags & CONFFLAG_MOH))
                    {
                        cw_moh_stop(chan);
                        musiconhold = 0;
                    }
                    if ( !(confflags & CONFFLAG_QUIET) && !(confflags & CONFFLAG_MARKEDUSER))
                    {
                        if (!cw_streamfile(chan, "conf-placeintoconf", chan->language))
                            cw_waitstream(chan, "");
                        conf_play(chan, conf, ENTER);
                    }
                }
            }

            /* trying to add moh for single person conf */
            if ((confflags & CONFFLAG_MOH) && !(confflags & CONFFLAG_WAITMARKED))
            {
                if (conf->users == 1)
                {
                    if (musiconhold == 0)
                    {
                        cw_moh_start(chan, NULL);
                        musiconhold = 1;
                    }
                }
                else
                {
                    if (musiconhold)
                    {
                        cw_moh_stop(chan);
                        musiconhold = 0;
                    }
                }
            }

            /* Leave if the last marked user left */
            if (currentmarked == 0 && lastmarked != 0 && (confflags & CONFFLAG_MARKEDEXIT))
            {
                ret = -1;
                break;
            }

            /* Check if the admin changed my modes */
            if (user->adminflags)
            {
                /* Set the new modes */
                if ((user->adminflags & ADMINFLAG_MUTED) && (ztc.confmode & DAHDI_CONF_TALKER))
                {
                    ztc.confmode ^= DAHDI_CONF_TALKER;
                    if (ioctl(fd, DAHDI_SETCONF, &ztc))
                    {
                        cw_log(CW_LOG_WARNING, "Error setting conference - Un/Mute \n");
                        ret = -1;
                        break;
                    }
                }
                if (!(user->adminflags & ADMINFLAG_MUTED) && !(confflags & CONFFLAG_MONITOR) && !(ztc.confmode & DAHDI_CONF_TALKER))
                {
                    ztc.confmode |= DAHDI_CONF_TALKER;
                    if (ioctl(fd, DAHDI_SETCONF, &ztc))
                    {
                        cw_log(CW_LOG_WARNING, "Error setting conference - Un/Mute \n");
                        ret = -1;
                        break;
                    }
                }
                if (user->adminflags & ADMINFLAG_KICKME)
                {
                    /* You have been kicked. */
                    if (!cw_streamfile(chan, "conf-kicked", chan->language))
                        cw_waitstream(chan, "");
                    ret = 0;
                    break;
                }
            }
            else if (!(confflags & CONFFLAG_MONITOR) && !(ztc.confmode & DAHDI_CONF_TALKER))
            {
                ztc.confmode |= DAHDI_CONF_TALKER;
                if (ioctl(fd, DAHDI_SETCONF, &ztc))
                {
                    cw_log(CW_LOG_WARNING, "Error setting conference - Un/Mute \n");
                    ret = -1;
                    break;
                }
            }

            if (c)
            {
                if (c->fds[0] != origfd)
                {
                    if (using_pseudo)
                    {
                        /* Kill old pseudo */
                        close(fd);
                        using_pseudo = 0;
                    }
                    cw_log(CW_LOG_DEBUG, "Ooh, something swapped out under us, starting over\n");
                    retrydahdi = strcmp(c->type, "DAHDI");
                    user->dahdichannel = !retrydahdi;
                    goto dahdiretry;
                }
                f = cw_read(c);
                if (!f)
                    break;
                if ((f->frametype == CW_FRAME_VOICE) && (f->subclass == CW_FORMAT_SLINEAR))
                {
                    if (confflags &  CONFFLAG_MONITORTALKER)
                    {
                        int totalsilence;
                        if (user->talking == -1)
                            user->talking = 0;

                        res = cw_dsp_silence(dsp, f, &totalsilence);
                        if (!user->talking && totalsilence < MEETME_DELAYDETECTTALK)
                        {
                            user->talking = 1;
                            cw_manager_event(CW_EVENT_FLAG_CALL, "MeetmeTalking",
					  4,
                                          cw_msg_tuple("Channel",  "%s\r\n", chan->name),
                                          cw_msg_tuple("Uniqueid", "%s\r\n", chan->uniqueid),
                                          cw_msg_tuple("Meetme",   "%s\r\n", conf->confno),
                                          cw_msg_tuple("Usernum",  "%d\r\n", user->user_no)
			    );
                        }
                        if (user->talking && totalsilence > MEETME_DELAYDETECTENDTALK)
                        {
                            user->talking = 0;
                            cw_manager_event(CW_EVENT_FLAG_CALL, "MeetmeStopTalking",
					  4,
                                          cw_msg_tuple("Channel",  "%s\r\n", chan->name),
                                          cw_msg_tuple("Uniqueid", "%s\r\n", chan->uniqueid),
                                          cw_msg_tuple("Meetme",   "%s\r\n", conf->confno),
                                          cw_msg_tuple("Usernum",  "%d\r\n", user->user_no)
			    );
                        }
                    }
                    if (using_pseudo)
                    {
                        careful_write(fd, f->data, f->datalen, 0);
                    }
                }
                else if ((f->frametype == CW_FRAME_DTMF) && (confflags & CONFFLAG_EXIT_CONTEXT))
                {
                    char tmp[2];

                    tmp[0] = f->subclass;
                    tmp[1] = '\0';
                    if (cw_goto_if_exists_n(chan, exitcontext, tmp, 1))
                    {
                        ret = 0;
                        break;
                    }
                    else if (option_debug > 1)
                        cw_log(CW_LOG_DEBUG, "Exit by single digit did not work in meetme. Extension %s does not exist in context %s\n", tmp, exitcontext);
                }
                else if ((f->frametype == CW_FRAME_DTMF) && (f->subclass == '#') && (confflags & CONFFLAG_POUNDEXIT))
                {
                    ret = 0;
                    break;
                }
                else if (((f->frametype == CW_FRAME_DTMF) && (f->subclass == '*') && (confflags & CONFFLAG_STARMENU)) || ((f->frametype == CW_FRAME_DTMF) && menu_active))
                {
                    if (ioctl(fd, DAHDI_SETCONF, &ztc_empty))
                    {
                        cw_log(CW_LOG_WARNING, "Error setting conference\n");
                        close(fd);
                        cw_mutex_unlock(&conflock);
                        goto outrun;
                    }

                    /* if we are entering the menu, and the user has a channel-driver
                       volume adjustment, clear it
                    */
                    if (!menu_active && user->talk.desired)
                        cw_channel_setoption(user->chan, CW_OPTION_RXGAIN, 0);

                    if (musiconhold)
                        cw_moh_stop(chan);
                    if ((confflags & CONFFLAG_ADMIN))
                    {
                        /* Admin menu */
                        if (!menu_active)
                        {
                            menu_active = 1;
                            /* Record this sound! */
                            if (!cw_streamfile(chan, "conf-adminmenu", chan->language))
                                dtmf = cw_waitstream(chan, CW_DIGIT_ANY);
                            else
                                dtmf = 0;
                        }
                        else
                        {
                            dtmf = f->subclass;
                        }
                        if (dtmf)
                        {
                            switch (dtmf)
                            {
                            case '1': /* Un/Mute */
                                menu_active = 0;
                                if (ztc.confmode & DAHDI_CONF_TALKER)
                                {
                                    ztc.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER;
                                    confflags |= CONFFLAG_MONITOR ^ CONFFLAG_TALKER;
                                }
                                else
                                {
                                    ztc.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
                                    confflags ^= CONFFLAG_MONITOR | CONFFLAG_TALKER;
                                }
                                if (ioctl(fd, DAHDI_SETCONF, &ztc))
                                {
                                    cw_log(CW_LOG_WARNING, "Error setting conference - Un/Mute \n");
                                    ret = -1;
                                    break;
                                }
                                if (ztc.confmode & DAHDI_CONF_TALKER)
                                {
                                    if (!cw_streamfile(chan, "conf-unmuted", chan->language))
                                        cw_waitstream(chan, "");
                                }
                                else
                                {
                                    if (!cw_streamfile(chan, "conf-muted", chan->language))
                                        cw_waitstream(chan, "");
                                }
                                break;
                            case '2': /* Un/Lock the Conference */
                                menu_active = 0;
                                if (conf->locked)
                                {
                                    conf->locked = 0;
                                    if (!cw_streamfile(chan, "conf-unlockednow", chan->language))
                                        cw_waitstream(chan, "");
                                }
                                else
                                {
                                    conf->locked = 1;
                                    if (!cw_streamfile(chan, "conf-lockednow", chan->language))
                                        cw_waitstream(chan, "");
                                }
                                break;
                            case '3': /* Eject last user */
                                menu_active = 0;
                                usr = conf->lastuser;
                                if ((usr->chan->name == chan->name)||(usr->userflags & CONFFLAG_ADMIN))
                                {
                                    if(!cw_streamfile(chan, "conf-errormenu", chan->language))
                                        cw_waitstream(chan, "");
                                }
                                else
                                    usr->adminflags |= ADMINFLAG_KICKME;
                                cw_stopstream(chan);
                                break;
                            case '4':
                                tweak_listen_volume(user, VOL_DOWN);
                                break;
                            case '5': /* en/disable flag marked to self as admin */
                                if (! (confflags & CONFFLAG_MARKEDUSER)) {
                                    conf->markedusers++;
                                    confflags ^= CONFFLAG_MARKEDUSER;
                                } else {
                                    conf->markedusers--;
                                    confflags |= CONFFLAG_MARKEDUSER;
				}
				break;
                            case '6':
                                tweak_listen_volume(user, VOL_UP);
                                break;
                            case '7':
                                tweak_talk_volume(user, VOL_DOWN);
                                break;
                            case '9':
                                tweak_talk_volume(user, VOL_UP);
                                break;
                            default:
                                menu_active = 0;
                                /* Play an error message! */
                                if (!cw_streamfile(chan, "conf-errormenu", chan->language))
                                    cw_waitstream(chan, "");
                                break;
                            }
                        }
                    }
                    else
                    {
                        /* User menu */
                        if (!menu_active)
                        {
                            menu_active = 1;
                            /* Record this sound! */
                            if (!cw_streamfile(chan, "conf-usermenu", chan->language))
                                dtmf = cw_waitstream(chan, CW_DIGIT_ANY);
                            else
                                dtmf = 0;
                        }
                        else
                        {
                            dtmf = f->subclass;
                        }
                        if (dtmf)
                        {
                            switch (dtmf)
                            {
                            case '1': /* Un/Mute */
                                menu_active = 0;
                                if (ztc.confmode & DAHDI_CONF_TALKER)
                                {
                                    ztc.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER;
                                    confflags |= CONFFLAG_MONITOR ^ CONFFLAG_TALKER;
                                }
                                else if (!(user->adminflags & ADMINFLAG_MUTED))
                                {
                                    ztc.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
                                    confflags ^= CONFFLAG_MONITOR | CONFFLAG_TALKER;
                                }
                                if (ioctl(fd, DAHDI_SETCONF, &ztc))
                                {
                                    cw_log(CW_LOG_WARNING, "Error setting conference - Un/Mute \n");
                                    ret = -1;
                                    break;
                                }
                                if (ztc.confmode & DAHDI_CONF_TALKER)
                                {
                                    if (!cw_streamfile(chan, "conf-unmuted", chan->language))
                                        cw_waitstream(chan, "");
                                }
                                else
                                {
                                    if (!cw_streamfile(chan, "conf-muted", chan->language))
                                        cw_waitstream(chan, "");
                                }
                                break;
                            case '4':
                                tweak_listen_volume(user, VOL_DOWN);
                                break;
                            case '6':
                                tweak_listen_volume(user, VOL_UP);
                                break;
                            case '7':
                                tweak_talk_volume(user, VOL_DOWN);
                                break;
                            case '8':
                                menu_active = 0;
                                break;
                            case '9':
                                tweak_talk_volume(user, VOL_UP);
                                break;
                            default:
                                menu_active = 0;
                                /* Play an error message! */
                                if (!cw_streamfile(chan, "conf-errormenu", chan->language))
                                    cw_waitstream(chan, "");
                                break;
                            }
                        }
                    }
                    if (musiconhold)
                        cw_moh_start(chan, NULL);

                    if (ioctl(fd, DAHDI_SETCONF, &ztc))
                    {
                        cw_log(CW_LOG_WARNING, "Error setting conference\n");
                        close(fd);
                        cw_mutex_unlock(&conflock);
                        goto outrun;
                    }
                    conf_flush(fd, chan);
                }
                else if (option_debug)
                {
                    cw_log(CW_LOG_DEBUG, "Got unrecognized frame on channel %s, f->frametype=%d,f->subclass=%d\n",chan->name,f->frametype,f->subclass);
                }
                cw_fr_free(f);
            }
            else if (outfd > -1)
            {
                res = read(outfd, buf, CONF_SIZE);
                if (res > 0)
                {
                    struct cw_frame *fout;
                    cw_fr_init_ex(&fr, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
                    fr.datalen = res;
                    fr.samples = res/2;
                    fr.data = buf;
                    fr.offset = CW_FRIENDLY_OFFSET;
                    fout = &fr;
                    if (cw_write(chan, &fout) < 0)
                    {
                        cw_log(CW_LOG_WARNING, "Unable to write frame to channel: %s\n", strerror(errno));
                        /* break; */
                    }
                    cw_fr_free(fout);
                }
                else
                {
                    cw_log(CW_LOG_WARNING, "Failed to read frame: %s\n", strerror(errno));
                }
            }
            lastmarked = currentmarked;
        }
    }
    if (using_pseudo)
        close(fd);
    else
    {
        /* Take out of conference */
        ztc.chan = 0;
        ztc.confno = 0;
        ztc.confmode = 0;
        if (ioctl(fd, DAHDI_SETCONF, &ztc))
        {
            cw_log(CW_LOG_WARNING, "Error setting conference\n");
        }
    }

    reset_volumes(user);

    cw_mutex_lock(&conflock);
    if (!(confflags & CONFFLAG_QUIET) && !(confflags & CONFFLAG_MONITOR) && !(confflags & CONFFLAG_ADMIN))
        conf_play(chan, conf, LEAVE);

    if (!(confflags & CONFFLAG_QUIET) && (confflags & CONFFLAG_INTROUSER))
    {
        if (cw_fileexists(user->namerecloc, NULL, NULL))
        {
            if ((conf->chan) && (conf->users > 1))
            {
                if (!cw_streamfile(conf->chan, user->namerecloc, chan->language))
                    cw_waitstream(conf->chan, "");
                if (!cw_streamfile(conf->chan, "conf-hasleft", chan->language))
                    cw_waitstream(conf->chan, "");
            }
            cw_filedelete(user->namerecloc, NULL);
        }
    }
    cw_mutex_unlock(&conflock);


outrun:
    cw_mutex_lock(&conflock);
    if (confflags & CONFFLAG_MONITORTALKER && dsp)
        cw_dsp_free(dsp);

    if (user->user_no)
    { /* Only cleanup users who really joined! */
        cw_manager_event(CW_EVENT_FLAG_CALL, "MeetmeLeave",
		4,
                      cw_msg_tuple("Channel", "%s", chan->name),
                      cw_msg_tuple("Uniqueid", "%s", chan->uniqueid),
                      cw_msg_tuple("Meetme", "%s", conf->confno),
                      cw_msg_tuple("Usernum", "%d", user->user_no)
	);
        conf->users--;
        if (confflags & CONFFLAG_MARKEDUSER)
            conf->markedusers--;
        if (!conf->users)
        {
            /* No more users -- close this one out */
            conf_free(conf);
        }
        else
        {
            /* Remove the user struct */
            if (user == conf->firstuser)
            {
                if (user->nextuser)
                {
                    /* There is another entry */
                    user->nextuser->prevuser = NULL;
                }
                else
                {
                    /* We are the only entry */
                    conf->lastuser = NULL;
                }
                /* In either case */
                conf->firstuser = user->nextuser;
            }
            else if (user == conf->lastuser)
            {
                if (user->prevuser)
                    user->prevuser->nextuser = NULL;
                else
                    cw_log(CW_LOG_ERROR, "Bad bad bad!  We're the last, not the first, but nobody before us??\n");
                conf->lastuser = user->prevuser;
            }
            else
            {
                if (user->nextuser)
                    user->nextuser->prevuser = user->prevuser;
                else
                    cw_log(CW_LOG_ERROR, "Bad! Bad! Bad! user->nextuser is NULL but we're not the end!\n");
                if (user->prevuser)
                    user->prevuser->nextuser = user->nextuser;
                else
                    cw_log(CW_LOG_ERROR, "Bad! Bad! Bad! user->prevuser is NULL but we're not the beginning!\n");
            }
        }
        /* Return the number of seconds the user was in the conf */
        snprintf(meetmesecs, sizeof(meetmesecs), "%d", (int) (time(NULL) - user->jointime));
        pbx_builtin_setvar_helper(chan, "MEETMESECS", meetmesecs);
    }
    free(user);
    cw_mutex_unlock(&conflock);
    return ret;
}

static struct cw_conference *find_conf(struct cw_channel *chan, char *confno, int make, int dynamic, char *dynamic_pin)
{
    struct cw_config *cfg;
    struct cw_variable *var;
    struct cw_conference *cnf;

    /* Check first in the conference list */
    cw_mutex_lock(&conflock);
    cnf = confs;
    while (cnf)
    {
        if (!strcmp(confno, cnf->confno))
            break;
        cnf = cnf->next;
    }
    cw_mutex_unlock(&conflock);

    if (!cnf)
    {
        if (dynamic)
        {
            /* No need to parse meetme.conf */
            cw_log(CW_LOG_DEBUG, "Building dynamic conference '%s'\n", confno);
            if (dynamic_pin)
            {
                if (dynamic_pin[0] == 'q')
                {
                    /* Query the user to enter a PIN */
                    cw_app_getdata(chan, "conf-getpin", dynamic_pin, CW_MAX_EXTENSION - 1, 0);
                }
                cnf = build_conf(confno, dynamic_pin, "", make, dynamic);
            }
            else
            {
                cnf = build_conf(confno, "", "", make, dynamic);
            }
        }
        else
        {
            /* Check the config */
            cfg = cw_config_load("meetme.conf");
            if (!cfg)
            {
                cw_log(CW_LOG_WARNING, "No meetme.conf file :(\n");
                return NULL;
            }
            var = cw_variable_browse(cfg, "rooms");
            while (var)
            {
                if (!strcasecmp(var->name, "conf"))
                {
                    /* Separate the PIN */
                    char *pin, *pinadmin, *conf;

                    pinadmin = cw_strdupa(var->value);
                    conf = strsep(&pinadmin, "|,");
                    pin = strsep(&pinadmin, "|,");
                    if (!strcasecmp(conf, confno))
                    {
                        /* Bingo it's a valid conference */
                        if (pin)
                            if (pinadmin)
                                cnf = build_conf(confno, pin, pinadmin, make, dynamic);
                            else
                                cnf = build_conf(confno, pin, "", make, dynamic);
                        else
                            if (pinadmin)
                                cnf = build_conf(confno, "", pinadmin, make, dynamic);
                            else
                                cnf = build_conf(confno, "", "", make, dynamic);
                        break;
                    }
                }
                var = var->next;
            }
            if (!var)
            {
                cw_log(CW_LOG_DEBUG, "%s isn't a valid conference\n", confno);
            }
            cw_config_destroy(cfg);
        }
    }
    else if (dynamic_pin)
    {
        /* Correct for the user selecting 'D' instead of 'd' to have
           someone join into a conference that has already been created
           with a pin. */
        if (dynamic_pin[0] == 'q')
            dynamic_pin[0] = '\0';
    }
    return cnf;
}

/*--- count_exec: The MeetmeCount application */
static int count_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    static int deprecated_var = 0;
    char val[80] = "0";
    struct localuser *u;
    int res = 0;
    struct cw_conference *conf;
    int count;

    if (argc < 1 || argc > 2)
        return cw_function_syntax(syntax2);

    LOCAL_USER_ADD(u);

    conf = find_conf(chan, argv[0], 0, 0, NULL);
    if (conf)
        count = conf->users;
    else
        count = 0;

    if (result)
    {
        cw_dynstr_printf(result, "%d", count);
    }
    else if (argc > 1)
    {
        if (!deprecated_var)
        {
            cw_log(CW_LOG_WARNING, "Deprecated usage. Use Set(varname=${%s(args)}) instead.\n", name2);
            deprecated_var = 1;
        }
        snprintf(val, sizeof(val), "%d",count);
        pbx_builtin_setvar_helper(chan, argv[1], val);
    }
    else
    {
        if (chan->_state != CW_STATE_UP)
            cw_answer(chan);
        res = cw_say_number(chan, count, "", chan->language, (char *) NULL); /* Needs gender */
    }

    LOCAL_USER_REMOVE(u);
    return res;
}

/*--- conf_exec: The meetme() application */
static int conf_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    char confno[CW_MAX_EXTENSION] = "";
    char the_pin[CW_MAX_EXTENSION] = "";
    struct cw_flags confflags =
        {
            0
        };
    struct cw_conference *cnf;
    struct localuser *u;
    int res=-1;
    int allowretry = 0;
    int retrycnt = 0;
    int dynamic = 0;
    int empty = 0, empty_no_pin = 0;
    int always_prompt = 0;

    CW_UNUSED(result);

    if (argc > 3)
        return cw_function_syntax(syntax);

    LOCAL_USER_ADD(u);

    if (argc == 0 || !argv[0][0])
        allowretry = 1;
    cw_copy_string(confno, argv[0], sizeof(confno));

    if (argc > 2)
        cw_copy_string(the_pin, argv[2], sizeof(the_pin));

    if (argc > 1 && argv[1][0])
    {
        cw_parseoptions(meetme_opts, &confflags, NULL, argv[1]);
        dynamic = cw_test_flag(&confflags, CONFFLAG_DYNAMIC | CONFFLAG_DYNAMICPIN);
        if (cw_test_flag(&confflags, CONFFLAG_DYNAMICPIN) && (argc < 3 || !argv[2][0]))
            strcpy(the_pin, "q");

        empty = cw_test_flag(&confflags, CONFFLAG_EMPTY | CONFFLAG_EMPTYNOPIN);
        empty_no_pin = cw_test_flag(&confflags, CONFFLAG_EMPTYNOPIN);
        always_prompt = cw_test_flag(&confflags, CONFFLAG_ALWAYSPROMPT);
    }

    if (chan->_state != CW_STATE_UP)
        cw_answer(chan);

    do
    {
        if (retrycnt > 3)
            allowretry = 0;
        if (empty)
        {
            int i, map[1024];
            struct cw_config *cfg;
            struct cw_variable *var;
            int confno_int;

            memset(map, 0, sizeof(map));

            cw_mutex_lock(&conflock);
            for (cnf = confs; cnf; cnf = cnf->next)
            {
                if (sscanf(cnf->confno, "%d", &confno_int) == 1)
                {
                    /* Disqualify in use conference */
                    if (confno_int >= 0 && confno_int < 1024)
                        map[confno_int]++;
                }
            }
            cw_mutex_unlock(&conflock);

            /* We only need to load the config file for static and empty_no_pin (otherwise we don't care) */
            if ((empty_no_pin) || (!dynamic))
            {
                cfg = cw_config_load("meetme.conf");
                if (cfg)
                {
                    var = cw_variable_browse(cfg, "rooms");
                    while (var)
                    {
                        if (!strcasecmp(var->name, "conf"))
                        {
                            char *stringp = cw_strdupa(var->value);
                            char *confno_tmp = strsep(&stringp, "|,");
                            int found = 0;
                            if (sscanf(confno_tmp, "%d", &confno_int) == 1)
                            {
                                if ((confno_int >= 0) && (confno_int < 1024))
                                {
                                    if (stringp && empty_no_pin)
                                    {
                                        map[confno_int]++;
                                    }
                                }
                            }
                            if (! dynamic)
                            {
                                /* For static:  run through the list and see if this conference is empty */
                                cw_mutex_lock(&conflock);
                                cnf = confs;
                                while (cnf)
                                {
                                    if (!strcmp(confno_tmp, cnf->confno))
                                    {
                                        /* The conference exists, therefore it's not empty */
                                        found = 1;
                                        break;
                                    }
                                    cnf = cnf->next;
                                }
                                cw_mutex_unlock(&conflock);
                                if (!found)
                                {
                                    /* At this point, we have a confno_tmp (static conference) that is empty */
                                    if ((empty_no_pin && ((!stringp) || (stringp && (stringp[0] == '\0')))) || (!empty_no_pin))
                                    {
                                        /* Case 1:  empty_no_pin and pin is nonexistent (NULL)
                                         * Case 2:  empty_no_pin and pin is blank (but not NULL)
                                         * Case 3:  not empty_no_pin
                                         */
                                        cw_copy_string(confno, confno_tmp, sizeof(confno));
                                        break;
                                        /* XXX the map is not complete (but we do have a confno) */
                                    }
                                }
                            }
                        }
                        var = var->next;
                    }
                    cw_config_destroy(cfg);
                }
            }
            /* Select first conference number not in use */
            if (cw_strlen_zero(confno) && dynamic)
            {
                for (i = 0; i < arraysize(map); i++)
                {
                    if (!map[i])
                    {
                        snprintf(confno, sizeof(confno), "%d", i);
                        break;
                    }
                }
            }

            /* Not found? */
            if (cw_strlen_zero(confno))
            {
                res = cw_streamfile(chan, "conf-noempty", chan->language);
                if (!res)
                    cw_waitstream(chan, "");
            }
            else
            {
                if (sscanf(confno, "%d", &confno_int) == 1)
                {
                    res = cw_streamfile(chan, "conf-enteringno", chan->language);
                    if (!res)
                    {
                        cw_waitstream(chan, "");
                        res = cw_say_digits(chan, confno_int, "", chan->language);
                    }
                }
                else
                {
                    cw_log(CW_LOG_ERROR, "Could not scan confno '%s'\n", confno);
                }
            }
        }
        while (allowretry && (cw_strlen_zero(confno)) && (++retrycnt < 4))
        {
            /* Prompt user for conference number */
            res = cw_app_getdata(chan, "conf-getconfno", confno, sizeof(confno) - 1, 0);
            if (res < 0)
            {
                /* Don't try to validate when we catch an error */
                confno[0] = '\0';
                allowretry = 0;
                break;
            }
        }
        if (confno[0])
        {
            /* Check the validity of the conference */
            cnf = find_conf(chan, confno, 1, dynamic, the_pin);
            if (!cnf)
            {
                res = cw_streamfile(chan, "conf-invalid", chan->language);
                if (!res)
                    cw_waitstream(chan, "");
                res = -1;
                if (allowretry)
                    confno[0] = '\0';
            }
            else
            {
                if ((!cw_strlen_zero(cnf->pin) &&  !cw_test_flag(&confflags, CONFFLAG_ADMIN)) || (!cw_strlen_zero(cnf->pinadmin) && cw_test_flag(&confflags, CONFFLAG_ADMIN)))
                {
                    char pin[CW_MAX_EXTENSION]="";
                    int j;

                    /* Allow the pin to be retried up to 3 times */
                    for (j=0; j<3; j++)
                    {
                        if (*the_pin && (always_prompt==0))
                        {
                            cw_copy_string(pin, the_pin, sizeof(pin));
                            res = 0;
                        }
                        else
                        {
                            /* Prompt user for pin if pin is required */
                            res = cw_app_getdata(chan, "conf-getpin", pin + strlen(pin), sizeof(pin) - 1 - strlen(pin), 0);
                        }
                        if (res >= 0)
                        {
                            if (!strcasecmp(pin, cnf->pin)  || (!cw_strlen_zero(cnf->pinadmin) && !strcasecmp(pin, cnf->pinadmin)))
                            {

                                /* Pin correct */
                                allowretry = 0;
                                if (!cw_strlen_zero(cnf->pinadmin) && !strcasecmp(pin, cnf->pinadmin))
                                    cw_set_flag(&confflags, CONFFLAG_ADMIN);
                                /* Run the conference */
                                res = conf_run(chan, cnf, confflags.flags);
                                break;
                            }
                            else
                            {
                                /* Pin invalid */
                                res = cw_streamfile(chan, "conf-invalidpin", chan->language);
                                if (!res)
                                    cw_waitstream(chan, CW_DIGIT_ANY);
                                if (res < 0)
                                    break;
                                pin[0] = res;
                                pin[1] = '\0';
                                res = -1;
                                if (allowretry)
                                    confno[0] = '\0';
                            }
                        }
                        else
                        {
                            /* failed when getting the pin */
                            res = -1;
                            allowretry = 0;
                            /* see if we need to get rid of the conference */
                            cw_mutex_lock(&conflock);
                            if (!cnf->users)
                            {
                                conf_free(cnf);
                            }
                            cw_mutex_unlock(&conflock);
                            break;
                        }

                        /* Don't retry pin with a static pin */
                        if (*the_pin && (always_prompt==0))
                        {
                            break;
                        }
                    }
                }
                else
                {
                    /* No pin required */
                    allowretry = 0;

                    /* Run the conference */
                    res = conf_run(chan, cnf, confflags.flags);
                }
            }
        }
    }
    while (allowretry);

    LOCAL_USER_REMOVE(u);

    return res;
}

static struct cw_conf_user* find_user(struct cw_conference *conf, char *callerident)
{
    struct cw_conf_user *user = NULL;
    char usrno[1024] = "";
    if (conf && callerident)
    {
        user = conf->firstuser;
        while (user)
        {
            snprintf(usrno, sizeof(usrno), "%d", user->user_no);
            if (strcmp(usrno, callerident) == 0)
                return user;
            user = user->nextuser;
        }
    }
    return NULL;
}

/*--- admin_exec: The MeetMeadmin application */
/* MeetMeAdmin(confno, command, caller) */
static int admin_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    struct cw_conference *cnf;
    struct cw_conf_user *user;
    struct localuser *u;

    CW_UNUSED(result);

    if (argc < 2 || argc > 3)
        return cw_function_syntax(syntax3);

    LOCAL_USER_ADD(u);
    cw_mutex_lock(&conflock);

    cnf = confs;
    while (cnf)
    {
        if (strcmp(cnf->confno, argv[0]) == 0)
            break;
        cnf = cnf->next;
    }

    user = (argc > 2)  ?  find_user(cnf, argv[2])  :  NULL;

    if (cnf)
    {
        switch ((int) (*argv[1]))
        {
        case 'L':
            /* L: Lock */
            cnf->locked = 1;
            break;
        case 'l':
            /* l: Unlock */
            cnf->locked = 0;
            break;
        case 'K':
            /* K: kick all users*/
            user = cnf->firstuser;
            while (user)
            {
                user->adminflags |= ADMINFLAG_KICKME;
                if (user->nextuser)
                    user = user->nextuser;
                else
                    break;
            }
            break;
        case 'e':
            /* e: Eject last user*/
            user = cnf->lastuser;
            if (!(user->userflags & CONFFLAG_ADMIN))
            {
                user->adminflags |= ADMINFLAG_KICKME;
                break;
            }
            cw_log(CW_LOG_NOTICE, "Not kicking last user, is an Admin!\n");
            break;
        case 'M':
            /* M: Mute */
            if (user)
                user->adminflags |= ADMINFLAG_MUTED;
            else
                cw_log(CW_LOG_NOTICE, "Specified User not found!\n");
            break;
        case 'N':
            /* N: Mute all users */
            user = cnf->firstuser;
            while (user)
            {
                if (user && !(user->userflags & CONFFLAG_ADMIN))
                    user->adminflags |= ADMINFLAG_MUTED;
                if (user->nextuser)
                    user = user->nextuser;
                else
                    break;
            }
            break;
        case 'm':
            /* m: Unmute */
            if (user && (user->adminflags & ADMINFLAG_MUTED))
                user->adminflags ^= ADMINFLAG_MUTED;
            else
                cw_log(CW_LOG_NOTICE, "Specified User not found or he muted himself!");
            break;
        case 'n':
            /* n: Unmute all users */
            user = cnf->firstuser;
            while (user)
            {
                if (user && (user-> adminflags & ADMINFLAG_MUTED))
                    user->adminflags ^= ADMINFLAG_MUTED;
                if (user->nextuser)
                    user = user->nextuser;
                else
                    break;
            }
            break;
        case 'k':
            /* k: Kick user */
            if (user)
                user->adminflags |= ADMINFLAG_KICKME;
            else
                cw_log(CW_LOG_NOTICE, "Specified User not found!");
            break;
        }
    }
    else
    {
        cw_log(CW_LOG_NOTICE, "Conference Number not found\n");
    }

    cw_mutex_unlock(&conflock);
    LOCAL_USER_REMOVE(u);
    return 0;
}

static void *recordthread(void *args)
{
    struct cw_conference *cnf;
    struct cw_frame *f=NULL;
    int flags;
    struct cw_filestream *s;
    int res=0;

    cnf = (struct cw_conference *)args;
    if( !cnf || !cnf->chan )
    {
        pthread_exit(0);
    }
    cw_stopstream(cnf->chan);
    flags = O_CREAT|O_TRUNC|O_WRONLY;
    s = cw_writefile(cnf->recordingfilename, cnf->recordingformat, NULL, flags, 0, 0644);

    if (s)
    {
        cnf->recording = MEETME_RECORD_ACTIVE;
        while (cw_waitfor(cnf->chan, -1) > -1)
        {
            f = cw_read(cnf->chan);
            if (!f)
            {
                res = -1;
                break;
            }
            if (f->frametype == CW_FRAME_VOICE)
            {
                res = cw_writestream(s, f);
                if (res)
                    break;
            }
            cw_fr_free(f);
            if (cnf->recording == MEETME_RECORD_TERMINATE)
            {
                cw_mutex_lock(&conflock);
                cw_mutex_unlock(&conflock);
                break;
            }
        }
        cnf->recording = MEETME_RECORD_OFF;
        cw_closestream(s);
    }
    pthread_exit(0);
}


static void load_config(void)
{
	struct cw_config *cfg;
	char *val;

	audio_buffers = DEFAULT_AUDIO_BUFFERS;

	if (!(cfg = cw_config_load("meetme.conf")))
		return;

	if ((val = cw_variable_retrieve(cfg, "general", "audiobuffers"))) {
		if ((sscanf(val, "%d", &audio_buffers) != 1)) {
			cw_log(CW_LOG_WARNING, "audiobuffers setting must be a number, not '%s'\n", val);
			audio_buffers = DEFAULT_AUDIO_BUFFERS;
		} else if ((audio_buffers < DAHDI_DEFAULT_NUM_BUFS) || (audio_buffers > DAHDI_MAX_NUM_BUFS)) {
			cw_log(CW_LOG_WARNING, "audiobuffers setting must be between %d and %d\n", DAHDI_DEFAULT_NUM_BUFS, DAHDI_MAX_NUM_BUFS);
			audio_buffers = DEFAULT_AUDIO_BUFFERS;
		}
		if (audio_buffers != DEFAULT_AUDIO_BUFFERS)
			cw_log(CW_LOG_NOTICE, "Audio buffers per channel set to %d\n", audio_buffers);
	}

	cw_config_destroy(cfg);
}


static int unload_module(void)
{
    int res = 0;
    cw_cli_unregister(&cli_show_confs);
    cw_cli_unregister(&cli_conf);
    res |= cw_unregister_function(app3);
    res |= cw_unregister_function(app2);
    res |= cw_unregister_function(app);
    return res;
}

static int load_module(void)
{
    load_config();

    cw_cli_register(&cli_show_confs);
    cw_cli_register(&cli_conf);
    app3 = cw_register_function(name3, admin_exec, synopsis3, syntax3, descrip3);
    app2 = cw_register_function(name2, count_exec, synopsis2, syntax2, descrip2);
    app = cw_register_function(name, conf_exec, synopsis, syntax, descrip);
    return 0;
}

static int reload_module(void)
{
	load_config();
	return 0;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, tdesc)
