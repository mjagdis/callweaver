/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2005 Anthony Minessale II (anthmct@yahoo.com)
 *
 * Disclaimed to Digium
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
 * \brief ChanSpy: Listen in on any channel.
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/features.h"
#include "callweaver/options.h"
#include "callweaver/slinfactory.h"
#include "callweaver/app.h"
#include "callweaver/utils.h"
#include "callweaver/say.h"
#include "callweaver/pbx.h"
#include "callweaver/translate.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"

OPBX_MUTEX_DEFINE_STATIC(modlock);

#define OPBX_NAME_STRLEN 256
#define ALL_DONE(u, ret) LOCAL_USER_REMOVE(u); return ret;

static void *chanspy_app;
static const char *chanspy_name = "ChanSpy";
static const char *chanspy_synopsis = "Tap into any type of callweaver channel and listen to audio";
static const char *chanspy_syntax = "Chanspy([scanspec][, options])";
static const char *chanspy_desc =
    "Valid Options:\n"
    " - q: quiet, don't announce channels beep, etc.\n"
    " - b: bridged, only spy on channels involved in a bridged call.\n"
    " - v(<x>):  adjust the heard volume by <x>dB (-24 to 24).\n"    
    " - g(<grp>): enforce group.  Match only calls where their ${SPYGROUP} is 'grp'.\n"
    " - r[(basename)]: Record session to monitor spool dir (with optional basename, default is 'chanspy')\n\n"
    "If <scanspec> is specified, only channel names *beginning* with that string will be scanned.\n"
    "('all' or an empty string are also both valid <scanspec>)\n\n"
    "While Spying:\n\n"
    "Dialing # cycles the volume level.\n"
    "Dialing * will stop spying and look for another channel to spy on.\n"
    "Dialing a series of digits followed by # builds a channel name to append to <scanspec>\n"
    "(e.g. run Chanspy(Agent) and dial 1234# while spying to jump to channel Agent/1234)\n\n"
    "";

#define OPTION_QUIET     (1 << 0)   /* Quiet, no announcement */
#define OPTION_BRIDGED   (1 << 1)   /* Only look at bridged calls */
#define OPTION_VOLUME    (1 << 2)   /* Specify initial volume */
#define OPTION_GROUP     (1 << 3)   /* Only look at channels in group */
#define OPTION_RECORD    (1 << 4)   /* Record */

OPBX_DECLARE_OPTIONS(chanspy_opts,{
    ['q'] = { OPTION_QUIET },
    ['b'] = { OPTION_BRIDGED },
    ['v'] = { OPTION_VOLUME, 1 },
    ['g'] = { OPTION_GROUP, 2 },
    ['r'] = { OPTION_RECORD, 3 },
});

STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

struct chanspy_translation_helper
{
    /* spy data */
    struct opbx_channel_spy spy;
    int volfactor;
    int fd;
    struct opbx_slinfactory slinfactory[2];
};

/* Prototypes */
static struct opbx_channel *local_get_channel_begin_name(char *name);
static struct opbx_channel *local_channel_walk(struct opbx_channel *chan);
static void spy_release(struct opbx_channel *chan, void *data);
static void *spy_alloc(struct opbx_channel *chan, void *params);
static struct opbx_frame *spy_queue_shift(struct opbx_channel_spy *spy, int qnum);
static void opbx_flush_spy_queue(struct opbx_channel_spy *spy);
static int spy_generate(struct opbx_channel *chan, void *data, int len);
static void start_spying(struct opbx_channel *chan, struct opbx_channel *spychan, struct opbx_channel_spy *spy);
static void stop_spying(struct opbx_channel *chan, struct opbx_channel_spy *spy);
static int channel_spy(struct opbx_channel *chan, struct opbx_channel *spyee, int *volfactor, int fd);
static int chanspy_exec(struct opbx_channel *chan, int argc, char **argv);

static __inline__ int db_to_scaling_factor(int db)
{
    return (int) (powf(10.0f, db/10.0f)*32768.0f);
}

#if 0
static struct opbx_channel *local_get_channel_by_name(char *name)
{
    struct opbx_channel *ret;

    opbx_mutex_lock(&modlock);
    if ((ret = opbx_get_channel_by_name_locked(name)))
        opbx_mutex_unlock(&ret->lock);
    opbx_mutex_unlock(&modlock);

    return ret;
}
#endif

static struct opbx_channel *local_channel_walk(struct opbx_channel *chan)
{
    struct opbx_channel *ret;
    opbx_mutex_lock(&modlock);
    if ((ret = opbx_channel_walk_locked(chan)))
        opbx_mutex_unlock(&ret->lock);
    opbx_mutex_unlock(&modlock);
    return ret;
}

static struct opbx_channel *local_get_channel_begin_name(char *name)
{
    struct opbx_channel *chan, *ret = NULL;
    opbx_mutex_lock(&modlock);
    chan = local_channel_walk(NULL);
    while (chan)
    {
        if (!strncmp(chan->name, name, strlen(name)))
        {
            ret = chan;
            break;
        }
        chan = local_channel_walk(chan);
    }
    opbx_mutex_unlock(&modlock);

    return ret;
}

static void spy_release(struct opbx_channel *chan, void *data)
{
    struct chanspy_translation_helper *csth = data;

    opbx_slinfactory_destroy(&csth->slinfactory[0]);
    opbx_slinfactory_destroy(&csth->slinfactory[1]);

    return;
}

static void *spy_alloc(struct opbx_channel *chan, void *params)
{
    struct chanspy_translation_helper *csth = params;
    opbx_slinfactory_init(&csth->slinfactory[0]);
    opbx_slinfactory_init(&csth->slinfactory[1]);
    return params;
}

static struct opbx_frame *spy_queue_shift(struct opbx_channel_spy *spy, int qnum)
{
    struct opbx_frame *f;

    if (qnum < 0  ||  qnum > 1)
        return NULL;

    f = spy->queue[qnum];
    if (f)
    {
        spy->queue[qnum] = f->next;
        return f;
    }
    return NULL;
}

static void opbx_flush_spy_queue(struct opbx_channel_spy *spy)
{
    struct opbx_frame *f = NULL;
    int x = 0;
    
    opbx_mutex_lock(&spy->lock);
    for (x = 0;  x < 2;  x++)
    {
        f = NULL;
        while ((f = spy_queue_shift(spy, x)))
            opbx_fr_free(f);
    }
    opbx_mutex_unlock(&spy->lock);
}

#if 0
static int extract_audio(short *buf, size_t len, struct opbx_trans_pvt *trans, struct opbx_frame *fr, int *maxsamp)
{
    struct opbx_frame *f;
    int size, retlen = 0;

    if (trans)
    {
        if ((f = opbx_translate(trans, fr, 0)))
        {
            size = (f->datalen > len) ? len : f->datalen;
            memcpy(buf, f->data, size);
            retlen = f->datalen;
            opbx_fr_free(f);
        }
        else
        {
            /* your guess is as good as mine why this will happen but it seems to only happen on iax and appears harmless */
            opbx_log(LOG_DEBUG, "Failed to translate frame from %s\n", opbx_getformatname(fr->subclass));
        }
    }
    else
    {
        size = (fr->datalen > len) ? len : fr->datalen;
        memcpy(buf, fr->data, size);
        retlen = fr->datalen;
    }

    if (retlen > 0  &&  (size = retlen / 2))
    {
        if (size > *maxsamp)
        {
            *maxsamp = size;
        }
    }

    return retlen;
}

static int spy_queue_ready(struct opbx_channel_spy *spy)
{
    int res = 0;

    opbx_mutex_lock(&spy->lock);
    if (spy->status == CHANSPY_RUNNING)
    {
        res = (spy->queue[0]  &&  spy->queue[1])  ?  1  :  0;
    }
    else
    {
        res = (spy->queue[0] || spy->queue[1])  ?  1  :  -1;
    }
    opbx_mutex_unlock(&spy->lock);
    return res;
}
#endif

static int spy_generate(struct opbx_channel *chan, void *data, int sample)
{

    struct chanspy_translation_helper *csth = data;
    struct opbx_frame frame, *f;
    int len0 = 0;
    int len1 = 0;
    int samp0 = 0;
    int samp1 = 0;
    int len;
    int x;
    int vf;
    int minsamp;
    int maxsamp;
    int16_t buf0[1280], buf1[1280], buf[1280];

    if (csth->spy.status == CHANSPY_DONE)
    {
        /* Channel is already gone more than likely */
        return -1;
    }

    len = sample * sizeof(int16_t);

    opbx_mutex_lock(&csth->spy.lock);
    while((f = csth->spy.queue[0]))
    {
        csth->spy.queue[0] = f->next;
        opbx_slinfactory_feed(&csth->slinfactory[0], f);
        opbx_fr_free(f);
    }
    opbx_mutex_unlock(&csth->spy.lock);
    opbx_mutex_lock(&csth->spy.lock);
    while((f = csth->spy.queue[1]))
    {
        csth->spy.queue[1] = f->next;
        opbx_slinfactory_feed(&csth->slinfactory[1], f);
        opbx_fr_free(f);
    }
    opbx_mutex_unlock(&csth->spy.lock);

    if (csth->slinfactory[0].size < len || csth->slinfactory[1].size < len)
    {
        return 0;
    }

    len0 = opbx_slinfactory_read(&csth->slinfactory[0], buf0, len);
    samp0 = len0/sizeof(int16_t);
    len1 = opbx_slinfactory_read(&csth->slinfactory[1], buf1, len);
    samp1 = len1/sizeof(int16_t);

    vf = db_to_scaling_factor(csth->volfactor) >> 4;

    /* Volume Control */
    for (x = 0;  x < samp0;  x++)
        buf0[x] = saturate((buf0[x]*vf) >> 11);
    for (x = 0;  x < samp1;  x++)
        buf1[x] = saturate((buf1[x]*vf) >> 11);

    minsamp = (samp0 < samp1)  ?  samp0  :  samp1;
    maxsamp = (samp0 > samp1)  ?  samp0  :  samp1;

    /* Mixing 2 way remote audio */
    if (samp0  &&  samp1)
    {
        for (x = 0;  x < minsamp;  x++)
            buf[x] = buf0[x] + buf1[x];
        if (samp0 > samp1)
        {
            for (  ;  x < samp0;  x++)
                buf[x] = buf0[x];
        }
        else
        {
            for (  ;  x < samp1;  x++)
                buf[x] = buf1[x];
        }
    }
    else if (samp0)
    {
        memcpy(buf, buf0, len0);
        x = samp0;
    }
    else if (samp1)
    {
        memcpy(buf, buf1, len1);
        x = samp1;
    }

    opbx_fr_init_ex(&frame, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, NULL);
    frame.data = buf;
    frame.samples = x;
    frame.datalen = x*sizeof(int16_t);

    if (opbx_write(chan, &frame))
        return -1;
    if (csth->fd) /* Write audio to file if open */
        write(csth->fd, buf1, len1);
    return 0;
}


static struct opbx_generator spygen =
    {
    alloc:
        spy_alloc,
    release:
        spy_release,
    generate:
        spy_generate,
    };

static void start_spying(struct opbx_channel *chan, struct opbx_channel *spychan, struct opbx_channel_spy *spy)
{

    struct opbx_channel_spy *cptr=NULL;
    struct opbx_channel *peer;

    opbx_log(LOG_WARNING, "Attaching %s to %s\n", spychan->name, chan->name);

    opbx_mutex_lock(&chan->lock);
    if (chan->spiers)
    {
        for (cptr = chan->spiers;  cptr  &&  cptr->next;  cptr = cptr->next)
            ;
        cptr->next = spy;
    }
    else
    {
        chan->spiers = spy;
    }
    opbx_mutex_unlock(&chan->lock);
    if ( opbx_test_flag(chan, OPBX_FLAG_NBRIDGE)  &&  (peer = opbx_bridged_channel(chan)))
        opbx_softhangup(peer, OPBX_SOFTHANGUP_UNBRIDGE);
}

static void stop_spying(struct opbx_channel *chan, struct opbx_channel_spy *spy)
{
    struct opbx_channel_spy *cptr = NULL;
    struct opbx_channel_spy *prev = NULL;

    /* If our status has changed, then the channel we're spying on is gone....
       DON'T TOUCH IT!!!  RUN AWAY!!! */
    if (spy->status != CHANSPY_RUNNING)
        return;

    opbx_mutex_lock(&chan->lock);
    for (cptr = chan->spiers;  cptr;  cptr = cptr->next)
    {
        if (cptr == spy)
        {
            if (prev)
            {
                prev->next = cptr->next;
                cptr->next = NULL;
            }
            else
            {
                chan->spiers = NULL;
            }
        }
        prev = cptr;
    }
    opbx_mutex_unlock(&chan->lock);
}

/* attempt to set the desired gain adjustment via the channel driver;
   if successful, clear it out of the csth structure so the
   generator will not attempt to do the adjustment itself
*/
static void set_volume(struct opbx_channel *chan, struct chanspy_translation_helper *csth)
{
    signed char volume_adjust;

    if (csth->volfactor > 24)
        volume_adjust = 24;
    else if (csth->volfactor < -24)
        volume_adjust = -24;
    else
        volume_adjust = csth->volfactor;
    if (!opbx_channel_setoption(chan, OPBX_OPTION_TXGAIN, &volume_adjust, sizeof(volume_adjust), 0))
        csth->volfactor = 0;
}

static int channel_spy(struct opbx_channel *chan, struct opbx_channel *spyee, int *volfactor, int fd)
{
    struct chanspy_translation_helper csth;
    int running = 1;
    int res = 0;
    int x = 0;
    char inp[24] = "";
    char *name = NULL;
    struct opbx_frame *f = NULL;

    if ((chan  &&  opbx_check_hangup(chan)) || (spyee  &&  opbx_check_hangup(spyee)))
        return 0;

    if (chan  &&  !opbx_check_hangup(chan)  &&  spyee  &&  !opbx_check_hangup(spyee))
    {
        memset(inp, 0, sizeof(inp));
        name = opbx_strdupa(spyee->name);
        if (option_verbose >= 2)
            opbx_verbose(VERBOSE_PREFIX_2 "Spying on channel %s\n", name);

        memset(&csth, 0, sizeof(csth));
        csth.spy.status = CHANSPY_RUNNING;
        opbx_mutex_init(&csth.spy.lock);
        csth.volfactor = *volfactor;
        set_volume(chan, &csth);

        if (fd)
            csth.fd = fd;
        start_spying(spyee, chan, &csth.spy);
        opbx_generator_activate(chan, &spygen, &csth);

        while (csth.spy.status == CHANSPY_RUNNING
               &&
               chan
               &&
               !opbx_check_hangup(chan)
               &&
               spyee
               &&
               !opbx_check_hangup(spyee)
               &&
               running == 1
               &&
               (res = opbx_waitfor(chan, -1) > -1))
        {
            if ((f = opbx_read(chan)) == NULL)
                break;
            res = 0;
            if (f->frametype == OPBX_FRAME_DTMF)
                res = f->subclass;
            opbx_fr_free(f);
            if (res == 0)
                continue;
            if (x == sizeof(inp))
                x = 0;
            if (res < 0)
                running = -1;
            if (res == 0)
                continue;
            if (res == '*')
            {
                running = 0;
            }
            else if (res == '#')
            {
                if (!opbx_strlen_zero(inp))
                {
                    running = x ? atoi(inp) : -1;
                    break;
                }
                *volfactor += 6;
                if (*volfactor > 24)
                    *volfactor = -24;
                if (option_verbose > 2)
                    opbx_verbose(VERBOSE_PREFIX_3 "Setting spy volume on %s to %d\n", chan->name, *volfactor);
                csth.volfactor = *volfactor;
                set_volume(chan, &csth);
            }
            else if (res >= '0'  &&  res <= '9')
            {
                inp[x++] = res;
            }
        }
        opbx_generator_deactivate(chan);
        stop_spying(spyee, &csth.spy);

        if (option_verbose >= 2)
            opbx_verbose(VERBOSE_PREFIX_2 "Done Spying on channel %s\n", name);
        opbx_flush_spy_queue(&csth.spy);
    }
    else
    {
        running = 0;
    }
    opbx_mutex_destroy(&csth.spy.lock);
    return running;
}

static int chanspy_exec(struct opbx_channel *chan, int argc, char **argv)
{
    struct localuser *u;
    struct opbx_channel *peer = NULL;
    struct opbx_channel *prev = NULL;
    char name[OPBX_NAME_STRLEN];
    char peer_name[OPBX_NAME_STRLEN + 5];
    char *ptr = NULL;
    char *mygroup = NULL;
    char *recbase = NULL;
    int res = -1;
    int volfactor = 0;
    int silent = 0;
    int bronly = 0;
    int chosen = 0;
    int count = 0;
    int waitms = 100;
    int num = 0;
    int oldrf = 0;
    int oldwf = 0;
    int fd = 0;
    struct opbx_flags flags;
    signed char zero_volume = 0;

    if (argc < 1  ||  argc > 2)
    {
        opbx_log(LOG_ERROR, "Syntax: %s\n", chanspy_syntax);
        return -1;
    }

    LOCAL_USER_ADD(u);

    oldrf = chan->readformat;
    oldwf = chan->writeformat;
    if (opbx_set_read_format(chan, OPBX_FORMAT_SLINEAR) < 0)
    {
        opbx_log(LOG_ERROR, "Could Not Set Read Format.\n");
        LOCAL_USER_REMOVE(u);
        return -1;
    }

    if (opbx_set_write_format(chan, OPBX_FORMAT_SLINEAR) < 0)
    {
        opbx_log(LOG_ERROR, "Could Not Set Write Format.\n");
        LOCAL_USER_REMOVE(u);
        return -1;
    }

    opbx_answer(chan);

    opbx_set_flag(chan, OPBX_FLAG_SPYING); /* so nobody can spy on us while we are spying */

    if (argc < 2  ||  !argv[1][0]  ||  !strcmp(argv[1], "all"))
        argv[1] = NULL;

    if (argv[1])
    {
        char *opts[3];

        opbx_parseoptions(chanspy_opts, &flags, opts, argv[1]);
        if (opbx_test_flag(&flags, OPTION_GROUP))
            mygroup = opts[1];
        if (opbx_test_flag(&flags, OPTION_RECORD))
        {
            if (!(recbase = opts[2]))
                recbase = "chanspy";
        }
        silent = opbx_test_flag(&flags, OPTION_QUIET);
        bronly = opbx_test_flag(&flags, OPTION_BRIDGED);
        if (opbx_test_flag(&flags, OPTION_VOLUME)  &&  opts[1])
        {
            int vol;

            if ((sscanf(opts[0], "%d", &vol) != 1)  ||  (vol > 24)  ||  (vol < -24))
                opbx_log(LOG_NOTICE, "Volume factor must be a number between -24dB and 24dB\n");
            else
                volfactor = vol;
        }
    }

    if (recbase)
    {
        char filename[512];

        snprintf(filename,sizeof(filename),"%s/%s.%ld.raw",opbx_config_OPBX_MONITOR_DIR, recbase, time(NULL));
        if ((fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644)) <= 0)
        {
            opbx_log(LOG_WARNING, "Cannot open %s for recording\n", filename);
            fd = 0;
        }
    }

    for (;;)
    {
        if (!silent)
        {
            if ((res = opbx_streamfile(chan, "beep", chan->language)) == 0)
                res = opbx_waitstream(chan, "");
            if (res < 0)
            {
                opbx_clear_flag(chan, OPBX_FLAG_SPYING);
                break;
            }
        }

        count = 0;
        if ((res = opbx_waitfordigit(chan, waitms)) < 0)
        {
            opbx_clear_flag(chan, OPBX_FLAG_SPYING);
            break;
        }

        peer = local_channel_walk(NULL);
        prev = NULL;
        while (peer)
        {
            if (peer != chan)
            {
                char *group = NULL;
                int igrp = 1;

                if (peer == prev  &&  !chosen)
                    break;
                chosen = 0;
                group = pbx_builtin_getvar_helper(peer, "SPYGROUP");
                if (mygroup)
                {
                    if (!group  ||  strcmp(mygroup, group))
                        igrp = 0;
                }

                if (igrp
                    &&
                    (argv[0] == NULL  ||  ((strlen(argv[0]) < strlen(peer->name)
                    &&
                    strncasecmp(peer->name, argv[0], strlen(argv[0])) == 0))))
                {
                    if (peer
                        &&
                        (!bronly  ||  opbx_bridged_channel(peer))
                        &&
                        !opbx_check_hangup(peer)
                        &&
                        !opbx_test_flag(peer, OPBX_FLAG_SPYING))
                    {
                        int x = 0;
                        
                        strncpy(peer_name, "spy-", 5);
                        strncpy(peer_name + strlen(peer_name), peer->name, OPBX_NAME_STRLEN);
                        ptr = strchr(peer_name, '/');
                        *ptr = '\0';
                        ptr++;
                        for (x = 0;  x < strlen(peer_name);  x++)
                        {
                            if (peer_name[x] == '/')
                                break;
                            peer_name[x] = tolower(peer_name[x]);
                        }

                        if (!silent)
                        {
                            if (opbx_fileexists(peer_name, NULL, NULL) != -1)
                            {
                                res = opbx_streamfile(chan, peer_name, chan->language);
                                if (!res)
                                    res = opbx_waitstream(chan, "");
                                if (res)
                                    break;
                            }
                            else
                            {
                                res = opbx_say_character_str(chan, peer_name, "", chan->language);
                            }
                            if ((num = atoi(ptr)))
                                opbx_say_digits(chan, atoi(ptr), "", chan->language);
                        }
                        count++;
                        prev = peer;
                        res = channel_spy(chan, peer, &volfactor, fd);
                        if (res == -1)
                            break;
                        if (res > 1  &&  argv[0])
                        {
                            snprintf(name, OPBX_NAME_STRLEN, "%s/%d", argv[0], res);
                            if ((peer = local_get_channel_begin_name(name)))
                                chosen = 1;
                            continue;
                        }
                    }
                }
            }
            if ((peer = local_channel_walk(peer)) == NULL)
                break;
        }
        waitms = (count)  ?  100  :  5000;
    }

    if (fd > 0)
        close(fd);

    if (oldrf  &&  opbx_set_read_format(chan, oldrf) < 0)
        opbx_log(LOG_ERROR, "Could Not Set Read Format.\n");

    if (oldwf  &&  opbx_set_write_format(chan, oldwf) < 0)
        opbx_log(LOG_ERROR, "Could Not Set Write Format.\n");

    opbx_clear_flag(chan, OPBX_FLAG_SPYING);

    opbx_channel_setoption(chan, OPBX_OPTION_TXGAIN, &zero_volume, sizeof(zero_volume), 0);

    ALL_DONE(u, res);
}

int unload_module(void)
{
    int res = 0;

    STANDARD_HANGUP_LOCALUSERS;
    res |= opbx_unregister_application(chanspy_app);
    return res;
}

int load_module(void)
{
    chanspy_app = opbx_register_application(chanspy_name, chanspy_exec, chanspy_synopsis, chanspy_syntax, chanspy_desc);
    return 0;
}

char *description(void)
{
    return (char *) chanspy_synopsis;
}

int usecount(void)
{
    int res;

    STANDARD_USECOUNT(res);
    return res;
}
