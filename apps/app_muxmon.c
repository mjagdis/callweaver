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
 * \brief muxmon() - record a call natively
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/slinfactory.h"
#include "callweaver/app.h"
#include "callweaver/pbx.h"
#include "callweaver/translate.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"
#include "callweaver/cli.h"
#include "callweaver/options.h"


static const char tdesc[] = "Native Channel Monitoring Module";

static void *muxmon_app;
static const char muxmon_name[] = "MuxMon";
static const char muxmon_synopsis[] = "Record A Call Natively";
static const char muxmon_syntax[] = "MuxMon(file.ext[, options[, command]])\n";
static const char muxmon_descrip[] =
    "Records The audio on the current channel to the specified file.\n\n"
    "Valid Options:\n"
    " b      - Only save audio to the file while the channel is bridged. *does not include conferences*\n"
    " a      - Append to the file instead of overwriting it.\n"
    " v(<x>) - Adjust the heard volume by <x>dB (-24 to 24).\n"
    " V(<x>) - Adjust the spoken volume by <x>dB (-24 to 24).\n"
    " W(<x>) - Adjust the overall volume by <x>dB (-24 to 24).\n\n"
    "<command> will be executed when the recording is over\n"
    "Any strings matching ^{X} will be unescaped to ${X} and \n"
    "all variables will be evaluated at that time.\n"
    "The variable MUXMON_FILENAME will be present as well.\n"
    "";


CW_MUTEX_DEFINE_STATIC(modlock);

struct muxmon
{
    struct cw_channel *chan;
    char *filename;
    char *post_process;
    unsigned int flags;
    int readvol;
    int writevol;
};

typedef enum
{
    MUXFLAG_RUNNING = (1 << 0),
    MUXFLAG_APPEND = (1 << 1),
    MUXFLAG_BRIDGED = (1 << 2),
    MUXFLAG_VOLUME = (1 << 3),
    MUXFLAG_READVOLUME = (1 << 4),
    MUXFLAG_WRITEVOLUME = (1 << 5)
} muxflags;


CW_DECLARE_OPTIONS(muxmon_opts,{
    ['a'] = { MUXFLAG_APPEND },
    ['b'] = { MUXFLAG_BRIDGED },
    ['v'] = { MUXFLAG_READVOLUME, 1 },
    ['V'] = { MUXFLAG_WRITEVOLUME, 2 },
    ['W'] = { MUXFLAG_VOLUME, 3 },
});

static __inline__ int minmax(int x, int y)
{
    if (x > y)
        return y;
    if (x < -y)
        return -y;
    return x;
}

static __inline__ int db_to_scaling_factor(int db)
{
    return (int) (powf(10.0f, db/10.0f)*32768.0f);
}

static void stopmon(struct cw_channel *chan, struct cw_channel_spy *spy) 
{
    int count = 0;

    if (chan)
    {
	cw_channel_lock(chan);
	cw_spy_detach(chan, spy);
        cw_channel_unlock(chan);
    }
}

static void startmon(struct cw_channel *chan, struct cw_channel_spy *spy) 
{
    if (chan)
	cw_spy_attach(chan, spy);
}

static int spy_queue_translate(struct cw_channel_spy *spy,
                               struct cw_slinfactory *slinfactory0,
                               struct cw_slinfactory *slinfactory1)
{
    struct cw_frame *f, *f0, *f1;
    
    cw_spy_get_frames(spy, &f0, &f1);
    while (f0) {
        f = f0->next;
        cw_slinfactory_feed(slinfactory0, f0);
        cw_fr_free(f0);
	f0 = f;
    }
    while (f1) {
        f = f1->next;
        cw_slinfactory_feed(slinfactory1, f1);
        cw_fr_free(f1);
	f1 = f;
    }
    return 0;
}

static void *muxmon_thread(void *obj) 
{

    short buf0[1280], buf1[1280], buf[1280];
    char post_process[1024] = "";
    struct cw_slinfactory slinfactory[2];
    struct cw_channel_spy spy;
    struct cw_frame frame;
    struct cw_channel *bchan;
    struct muxmon *muxmon = obj;
    struct cw_filestream *fs = NULL;
    char *ext;
    char *name;
    int len0 = 0;
    int len1 = 0;
    int samp0 = 0;
    int samp1 = 0;
    int framelen;
    int minsamp;
    int x = 0;
    unsigned int oflags;
    
    name = cw_strdupa(muxmon->chan->name);

    framelen = 160*sizeof(int16_t);
    cw_fr_init_ex(&frame, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
    frame.data = buf;
    cw_set_flag(muxmon, MUXFLAG_RUNNING);
    oflags = O_CREAT|O_WRONLY;
    cw_slinfactory_init(&slinfactory[0]);
    cw_slinfactory_init(&slinfactory[1]);

    /* For efficiency, use a flag to bypass volume logic when it's not needed */
    if (muxmon->readvol  ||  muxmon->writevol)
        cw_set_flag(muxmon, MUXFLAG_VOLUME);

    if ((ext = strrchr(muxmon->filename, '.')))
        *(ext++) = '\0';
    else
        ext = "raw";

    memset(&spy, 0, sizeof(spy));
    spy.status = CHANSPY_RUNNING;
    spy.next = NULL;
    cw_mutex_init(&spy.lock);
    startmon(muxmon->chan, &spy);
    if (cw_test_flag(muxmon, MUXFLAG_RUNNING))
    {
        if (option_verbose > 1)
            cw_verbose(VERBOSE_PREFIX_2 "Begin Recording %s\n", name);

        oflags |= cw_test_flag(muxmon, MUXFLAG_APPEND) ? O_APPEND : O_TRUNC;
        
        if (!(fs = cw_writefile(muxmon->filename, ext, NULL, oflags, 0, 0644)))
        {
            cw_log(CW_LOG_ERROR, "Cannot open %s\n", muxmon->filename);
            spy.status = CHANSPY_DONE;
	    stopmon(muxmon->chan, &spy);
        }
        else
        {
            if (cw_test_flag(muxmon, MUXFLAG_APPEND))
                cw_seekstream(fs, 0, SEEK_END);

            while (cw_test_flag(muxmon, MUXFLAG_RUNNING))
            {
                samp0 =
                samp1 =
                len0 =
                len1 = 0;

                /* In case of hangup it is safer to start from testing this */
                if (spy.status != CHANSPY_RUNNING)
                {
                    cw_clear_flag(muxmon, MUXFLAG_RUNNING);
                    break;
                }
                if (cw_check_hangup(muxmon->chan))
                {
                    cw_clear_flag(muxmon, MUXFLAG_RUNNING);
                    break;
                }

                if (cw_test_flag(muxmon, MUXFLAG_BRIDGED))
                {
                    if (!(bchan = cw_bridged_channel(muxmon->chan))) {
                        usleep(1000);
                        sched_yield();
                        continue;
                    } else
                        cw_object_put(bchan);
                }
                
                spy_queue_translate(&spy, &slinfactory[0], &slinfactory[1]);
                
                if (slinfactory[0].size < framelen || slinfactory[1].size < framelen)
                {
                    usleep(1000);
                    sched_yield();
                    continue;
                }

                len0 = cw_slinfactory_read(&slinfactory[0], buf0, framelen);
                samp0 = len0/sizeof(int16_t);
                len1 = cw_slinfactory_read(&slinfactory[1], buf1, framelen);
                samp1 = len1/sizeof(int16_t);
                if (cw_test_flag(muxmon, MUXFLAG_VOLUME))
                {
                    for (x = 0;  x < samp0;  x++)
                        buf0[x] = saturate((buf0[x]*muxmon->readvol) >> 11);
                    for (x = 0;  x < samp1;  x++)
                        buf1[x] = saturate((buf1[x]*muxmon->writevol) >> 11);
                }
                
                if (samp0  &&  samp1)
                {
                    minsamp = (samp0 < samp1)  ?  samp0  :  samp1;
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

                frame.samples = x;
                frame.datalen = x*sizeof(int16_t);
                cw_writestream(fs, &frame);
        
                usleep(1000);
                sched_yield();
            }
        }
    }

    if (muxmon->post_process)
    {
        char *p;
        for (p = muxmon->post_process;  *p;  p++)
        {
            if (*p == '^'  &&  *(p+1) == '{')
                *p = '$';
        }
        pbx_substitute_variables_helper(muxmon->chan, muxmon->post_process, post_process, sizeof(post_process));
        free(muxmon->post_process);
        muxmon->post_process = NULL;
    }
    /* In case of channel hangup - this is dangerous. Cli stop command do clearing */
    if (spy.status == CHANSPY_RUNNING)
         stopmon(muxmon->chan, &spy);  
    if (option_verbose > 1)
        cw_verbose(VERBOSE_PREFIX_2 "Finished Recording %s\n", name);
    cw_mutex_destroy(&spy.lock);
    
    if (fs)
        cw_closestream(fs);

    cw_slinfactory_destroy(&slinfactory[0]);
    cw_slinfactory_destroy(&slinfactory[1]);

    cw_object_put(muxmon->chan);
    if (muxmon->filename)
        free(muxmon->filename);
    free(muxmon);

    if (!cw_strlen_zero(post_process))
    {
        if (option_verbose > 2)
            cw_verbose(VERBOSE_PREFIX_2 "Executing [%s]\n", post_process);
        cw_safe_system(post_process);
    }

    return NULL;
}

static void launch_monitor_thread(struct cw_channel *chan, char *filename, unsigned int flags, int readvol, int writevol, char *post_process) 
{
    pthread_t tid;
    struct muxmon *muxmon;
    int res;

    if (!(muxmon = calloc(1, sizeof(struct muxmon)))) {
        muxmon->chan = cw_object_dup(chan);
        muxmon->filename = strdup(filename);
        if (post_process)
            muxmon->post_process = strdup(post_process);
        muxmon->readvol = readvol;
        muxmon->writevol = writevol;
        muxmon->flags = flags;

        if (!(res = cw_pthread_create(&tid, &global_attr_rr_detached, muxmon_thread, muxmon)))
		return;

        cw_object_put(chan);
        if (muxmon->filename)
            free(filename);
        if (muxmon->post_process)
            free(post_process);
        free(muxmon);
        cw_log(CW_LOG_ERROR, "pthread_create failed: %s\n", strerror(res));
	return;
    }
    cw_log(CW_LOG_ERROR, "Out of memory\n");
    return;
}


static int muxmon_exec(struct cw_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
    struct cw_flags flags = {0};
    struct localuser *u;
    int res = 0;
    int x = 0;
    int readvol = 0;
    int writevol = 0;

    if (argc < 1 || argc > 3)
        return cw_function_syntax(muxmon_syntax);

    LOCAL_USER_ADD(u);

    if (argc > 1  &&  argv[1][0])
    {
        char *opts[3] = {};
        cw_parseoptions(muxmon_opts, &flags, opts, argv[1]);

        if (cw_test_flag(&flags, MUXFLAG_READVOLUME)  &&  opts[0])
        {
            if (sscanf(opts[0], "%d", &x) != 1)
                cw_log(CW_LOG_NOTICE, "volume must be a number between -24 and 24\n");
            else
                readvol = db_to_scaling_factor(minmax(x, 24)) >> 4;
        }
        
        if (cw_test_flag(&flags, MUXFLAG_WRITEVOLUME)  &&  opts[1])
        {
            if (sscanf(opts[1], "%d", &x) != 1)
                cw_log(CW_LOG_NOTICE, "volume must be a number between -24 and 24\n");
            else
                writevol = db_to_scaling_factor(minmax(x, 24)) >> 4;
        }

        if (cw_test_flag(&flags, MUXFLAG_VOLUME)  &&  opts[2])
        {
            if (sscanf(opts[2], "%d", &x) != 1)
            {
                cw_log(CW_LOG_NOTICE, "volume must be a number between -24 and 24\n");
            }
            else
            {
                readvol =
                writevol = db_to_scaling_factor(minmax(x, 24)) >> 4;
            }
        }
    }

    pbx_builtin_setvar_helper(chan, "MUXMON_FILENAME", argv[0]);
	launch_monitor_thread(chan, argv[0], flags.flags, readvol, writevol, argc > 1 ? argv[2] : NULL);

    LOCAL_USER_REMOVE(u);
    return res;
}


static int muxmon_cli(int fd, int argc, char **argv) 
{
    char *op;
    char *chan_name = NULL;
    struct cw_channel *chan;

    if (argc > 2)
    {
        op = argv[1];
        chan_name = argv[2];

        if (!(chan = cw_get_channel_by_name_prefix_locked(chan_name, strlen(chan_name))))
        {
            cw_cli(fd, "Invalid Channel!\n");
            return -1;
        }
	cw_channel_unlock(chan);

        if (!strcasecmp(op, "start"))
        {
            muxmon_exec(chan, argc - 3, argv + 3, NULL, 0);
        }
        else if (!strcasecmp(op, "stop"))
        {
            cw_channel_lock(chan);
	    cw_spy_detach_all(chan);
            cw_channel_unlock(chan);
        }
	cw_object_put(chan);
        return 0;
    }

    cw_cli(fd, "Usage: muxmon <start|stop> <chan_name> <args>\n");
    return -1;
}


static struct cw_clicmd cli_muxmon = {
    .cmda = { "muxmon", NULL, NULL },
    .handler = muxmon_cli, 
    .summary = "Execute a monitor command",
    .usage = "muxmon <start|stop> <chan_name> <args>\n",
};


static int unload_module(void)
{
    int res = 0;

    cw_cli_unregister(&cli_muxmon);
    res |= cw_unregister_function(muxmon_app);
    return res;
}

static int load_module(void)
{
    cw_cli_register(&cli_muxmon);
    muxmon_app = cw_register_function(muxmon_name, muxmon_exec, muxmon_synopsis, muxmon_syntax, muxmon_descrip);
    return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
