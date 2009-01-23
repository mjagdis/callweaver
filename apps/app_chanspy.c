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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
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
#include "callweaver/keywords.h"

CW_MUTEX_DEFINE_STATIC(modlock);

#define CW_NAME_STRLEN 256
#define ALL_DONE(u, ret) LOCAL_USER_REMOVE(u); return ret;

static void *chanspy_app;
static const char chanspy_name[] = "ChanSpy";
static const char chanspy_synopsis[] = "Tap into any type of callweaver channel and listen to audio";
static const char chanspy_syntax[] = "Chanspy([scanspec][, options])";
static const char chanspy_desc[] =
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

CW_DECLARE_OPTIONS(chanspy_opts,{
    ['q'] = { OPTION_QUIET },
    ['b'] = { OPTION_BRIDGED },
    ['v'] = { OPTION_VOLUME, 1 },
    ['g'] = { OPTION_GROUP, 2 },
    ['r'] = { OPTION_RECORD, 3 },
});


struct chanspy_translation_helper
{
    /* spy data */
    struct cw_channel_spy spy;
    int volfactor;
    int fd;
    struct cw_slinfactory slinfactory[2];
    struct cw_frame f;
    int16_t buf[1280];
};

/* Prototypes */
static void spy_release(struct cw_channel *chan, void *data);
static void *spy_alloc(struct cw_channel *chan, void *params);
static void cw_flush_spy_queues(struct cw_channel_spy *spy);
static struct cw_frame *spy_generate(struct cw_channel *chan, void *data, int len);
static void start_spying(struct cw_channel *chan, struct cw_channel *spychan, struct cw_channel_spy *spy);
static void stop_spying(struct cw_channel *chan, struct cw_channel_spy *spy);
static int channel_spy(struct cw_channel *chan, struct cw_channel *spyee, int *volfactor, int fd);
static int chanspy_exec(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len);

static __inline__ int db_to_scaling_factor(int db)
{
    return (int) (powf(10.0f, db/10.0f)*32768.0f);
}


static void spy_release(struct cw_channel *chan, void *data)
{
    struct chanspy_translation_helper *csth = data;

    cw_slinfactory_destroy(&csth->slinfactory[0]);
    cw_slinfactory_destroy(&csth->slinfactory[1]);

    return;
}

static void *spy_alloc(struct cw_channel *chan, void *params)
{
    struct chanspy_translation_helper *csth = params;
    cw_slinfactory_init(&csth->slinfactory[0]);
    cw_slinfactory_init(&csth->slinfactory[1]);
    return params;
}

static void cw_flush_spy_queues(struct cw_channel_spy *spy)
{
    struct cw_frame *f0;
    struct cw_frame *f1;
    cw_spy_empty_queues(spy, &f0, &f1);

    while (f0) {
        struct cw_frame *fnext = f0->next;
        cw_fr_free(f0);
        f0 = fnext;
    }
    while (f1) {
        struct cw_frame *fnext = f1->next;
        cw_fr_free(f1);
        f1 = fnext;
    }
}

#if 0
static int extract_audio(short *buf, size_t len, struct cw_trans_pvt *trans, struct cw_frame *fr, int *maxsamp)
{
    struct cw_frame *f;
    int size, retlen = 0;

    if (trans)
    {
        if ((f = cw_translate(trans, fr, 0)))
        {
            size = (f->datalen > len) ? len : f->datalen;
            memcpy(buf, f->data, size);
            retlen = f->datalen;
            cw_fr_free(f);
        }
        else
        {
            /* your guess is as good as mine why this will happen but it seems to only happen on iax and appears harmless */
            cw_log(CW_LOG_DEBUG, "Failed to translate frame from %s\n", cw_getformatname(fr->subclass));
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

static int spy_queue_ready(struct cw_channel_spy *spy)
{
    int res = 0;

    cw_mutex_lock(&spy->lock);
    if (spy->status == CHANSPY_RUNNING)
    {
        res = (spy->queue[0]  &&  spy->queue[1])  ?  1  :  0;
    }
    else
    {
        res = (spy->queue[0] || spy->queue[1])  ?  1  :  -1;
    }
    cw_mutex_unlock(&spy->lock);
    return res;
}
#endif

static struct cw_frame *spy_generate(struct cw_channel *chan, void *data, int sample)
{

    struct chanspy_translation_helper *csth = data;
    struct cw_frame *f0, *f1;
    int len0 = 0;
    int len1 = 0;
    int samp0 = 0;
    int samp1 = 0;
    int len;
    int x;
    int vf;
    int minsamp;
    int maxsamp;
    int16_t buf0[1280], buf1[1280];

    if (csth->spy.status == CHANSPY_DONE)
    {
        /* Channel is already gone more than likely */
        return NULL;
    }

    len = sample * sizeof(int16_t);

    cw_spy_get_frames(&csth->spy, &f0, &f1);
    while (f0) {
	struct cw_frame *f = f0->next;
        cw_slinfactory_feed(&csth->slinfactory[0], f0);
        cw_fr_free(f0);
	f0 = f;
    }
    while (f1) {
	struct cw_frame *f = f1->next;
        cw_slinfactory_feed(&csth->slinfactory[1], f1);
        cw_fr_free(f1);
	f1 = f;
    }

    cw_fr_init_ex(&csth->f, CW_FRAME_VOICE, CW_FORMAT_SLINEAR);
    csth->f.data = csth->buf;
    csth->f.datalen = csth->f.samples = 0;

    if (csth->slinfactory[0].size < len || csth->slinfactory[1].size < len)
        return &csth->f;

    len0 = cw_slinfactory_read(&csth->slinfactory[0], buf0, len);
    samp0 = len0/sizeof(int16_t);
    len1 = cw_slinfactory_read(&csth->slinfactory[1], buf1, len);
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
            csth->buf[x] = buf0[x] + buf1[x];
        if (samp0 > samp1)
        {
            for (  ;  x < samp0;  x++)
                csth->buf[x] = buf0[x];
        }
        else
        {
            for (  ;  x < samp1;  x++)
                csth->buf[x] = buf1[x];
        }
    }
    else if (samp0)
    {
        memcpy(csth->buf, buf0, len0);
        x = samp0;
    }
    else if (samp1)
    {
        memcpy(csth->buf, buf1, len1);
        x = samp1;
    }

    csth->f.samples = x;
    csth->f.datalen = x*sizeof(int16_t);

    if (csth->fd >= 0) /* Write audio to file if open */
        write(csth->fd, buf1, len1);
    return &csth->f;
}


static struct cw_generator spygen =
    {
    alloc:
        spy_alloc,
    release:
        spy_release,
    generate:
        spy_generate,
    };

static void start_spying(struct cw_channel *chan, struct cw_channel *spychan, struct cw_channel_spy *spy)
{

    cw_log(CW_LOG_WARNING, "Attaching %s to %s\n", spychan->name, chan->name);

    cw_spy_attach(chan, spy);
}

static void stop_spying(struct cw_channel *chan, struct cw_channel_spy *spy)
{
    /* If our status has changed, then the channel we're spying on is gone....
       DON'T TOUCH IT!!!  RUN AWAY!!! */
    if (spy->status != CHANSPY_RUNNING)
        return;

    cw_channel_lock(chan);
    cw_spy_detach(chan, spy);
    cw_channel_unlock(chan);
}

/* attempt to set the desired gain adjustment via the channel driver;
   if successful, clear it out of the csth structure so the
   generator will not attempt to do the adjustment itself
*/
static void set_volume(struct cw_channel *chan, struct chanspy_translation_helper *csth)
{
    signed char volume_adjust;

    if (csth->volfactor > 24)
        volume_adjust = 24;
    else if (csth->volfactor < -24)
        volume_adjust = -24;
    else
        volume_adjust = csth->volfactor;
    if (!cw_channel_setoption(chan, CW_OPTION_TXGAIN, &volume_adjust, sizeof(volume_adjust)))
        csth->volfactor = 0;
}

static int channel_spy(struct cw_channel *chan, struct cw_channel *spyee, int *volfactor, int fd)
{
    struct chanspy_translation_helper csth;
    int running = 1;
    int res = 0;
    int x = 0;
    char inp[24] = "";
    char *name = NULL;
    struct cw_frame *f = NULL;

    if ((chan  &&  cw_check_hangup(chan)) || (spyee  &&  cw_check_hangup(spyee)))
        return 0;

    if (chan  &&  !cw_check_hangup(chan)  &&  spyee  &&  !cw_check_hangup(spyee))
    {
        memset(inp, 0, sizeof(inp));
        name = cw_strdupa(spyee->name);
        if (option_verbose >= 2)
            cw_verbose(VERBOSE_PREFIX_2 "Spying on channel %s\n", name);

        memset(&csth, 0, sizeof(csth));
        csth.spy.status = CHANSPY_RUNNING;
        cw_mutex_init(&csth.spy.lock);
        csth.volfactor = *volfactor;
        set_volume(chan, &csth);
        
        if (fd)
            csth.fd = fd;
        start_spying(spyee, chan, &csth.spy);
        cw_generator_activate(chan, &chan->generator, &spygen, &csth);

        while (csth.spy.status == CHANSPY_RUNNING
               &&
               chan
               &&
               !cw_check_hangup(chan)
               &&
               spyee
               &&
               !cw_check_hangup(spyee)
               &&
               running == 1
               &&
               (res = cw_waitfor(chan, -1) > -1))
        {
            if ((f = cw_read(chan)) == NULL)
                break;
            res = 0;
            if (f->frametype == CW_FRAME_DTMF)
                res = f->subclass;
            cw_fr_free(f);
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
                if (!cw_strlen_zero(inp))
                {
                    running = x ? atoi(inp) : -1;
                    break;
                }
                *volfactor += 6;
                if (*volfactor > 24)
                    *volfactor = -24;
                if (option_verbose > 2)
                    cw_verbose(VERBOSE_PREFIX_3 "Setting spy volume on %s to %d\n", chan->name, *volfactor);
                csth.volfactor = *volfactor;
                set_volume(chan, &csth);
            }
            else if (res >= '0'  &&  res <= '9')
            {
                inp[x++] = res;
            }
        }
        cw_generator_deactivate(&chan->generator);
        stop_spying(spyee, &csth.spy);

        if (option_verbose >= 2)
            cw_verbose(VERBOSE_PREFIX_2 "Done Spying on channel %s\n", name);
        cw_flush_spy_queues(&csth.spy);
    }
    else
    {
        running = 0;
    }
    cw_mutex_destroy(&csth.spy.lock);
    return running;
}


struct chanspy_by_prefix_args {
	struct cw_channel *chan;
	const char *group;
	int fd;
	int volfactor;
	int again:1;
	int bronly:1;
	int first:1;
	int silent:1;
	size_t name_len;
	const char *prefix;
	char name[CW_NAME_STRLEN];
};

static int chanspy_by_prefix_one(struct cw_object *obj, void *data)
{
	char peer_name[CW_NAME_STRLEN + sizeof("spy-")];
	struct cw_channel *peer = container_of(obj, struct cw_channel, obj);
	struct cw_channel *bchan;
	struct chanspy_by_prefix_args *args = data;
	struct cw_var_t *group = NULL;
	char *ptr;
	int igrp = 1;
	int res = 0;

	if (peer == args->chan)
		goto out;

	res = 1;

	if (args->group) {
		igrp = 0;
		if ((group = pbx_builtin_getvar_helper(peer, CW_KEYWORD_SPYGROUP, "SPYGROUP"))) {
			if (!strcmp(args->group, group->value))
				igrp = 1;
			cw_object_put(group);
		}
	}

	if (igrp && (!args->prefix || !strncasecmp(peer->name, args->name, args->name_len))) {
		if (args->bronly) {
			if (!(bchan = cw_bridged_channel(peer)))
				goto out_ok;
			cw_object_put(bchan);
		}
		if (!cw_check_hangup(peer) && !cw_test_flag(peer, CW_FLAG_SPYING)) {
			if (!args->silent) {
				strncpy(peer_name, "spy-", 5);
				strncpy(peer_name + sizeof("spy-") - 1, peer->name, CW_NAME_STRLEN);
				for (ptr = peer_name; *ptr && *ptr != '/'; ptr++)
					*ptr = tolower(*ptr);
				if (*ptr == '/')
					*(ptr++) = '\0';

				if (cw_fileexists(peer_name, NULL, NULL)) {
					if (cw_streamfile(args->chan, peer_name, args->chan->language) || cw_waitstream(args->chan, ""))
						goto out;
				} else {
					if (cw_say_character_str(args->chan, peer_name, "", args->chan->language))
						goto out;
				}
				if ((res = atoi(ptr)) && cw_say_digits(args->chan, res, "", args->chan->language))
					goto out;
			}

			if ((res = channel_spy(args->chan, peer, &args->volfactor, args->fd)) == -1)
				goto out;

			if (res > 1 && args->prefix) {
				args->name_len = snprintf(args->name, sizeof(args->name), "%s/%d", args->prefix, res);
				args->again = 1;
				goto out;
			}
		}
	}

out_ok:
	res = 0;
out:
	return res;
}

static int chanspy_exec(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	struct chanspy_by_prefix_args args;
	struct cw_flags flags;
	struct localuser *u;
	char *recbase = NULL;
	int waitms;
	int oldrf;
	int oldwf;
	int res = -1;
	signed char zero_volume = 0;

	if (argc < 1 || argc > 2)
		return cw_function_syntax(chanspy_syntax);

	LOCAL_USER_ADD(u);

	oldrf = chan->readformat;
	oldwf = chan->writeformat;
	if (cw_set_read_format(chan, CW_FORMAT_SLINEAR) < 0) {
		cw_log(CW_LOG_ERROR, "Could Not Set Read Format.\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if (cw_set_write_format(chan, CW_FORMAT_SLINEAR) < 0) {
		cw_log(CW_LOG_ERROR, "Could Not Set Write Format.\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	cw_answer(chan);

	cw_set_flag(chan, CW_FLAG_SPYING); /* so nobody can spy on us while we are spying */

	if (argc < 2  ||  !argv[1][0]  ||  !strcmp(argv[1], "all"))
		argv[1] = NULL;

	args.chan = chan;
	cw_copy_string(args.name, argv[0], sizeof(args.name));
	args.prefix = argv[0];
	args.name_len = strlen(argv[0]);
	args.group = NULL;
	args.fd = -1;
	args.volfactor = 0;
	args.again = 0;
	args.bronly = 0;
	args.first = 1;
	args.silent = 0;

	if (argv[1]) {
		char *opts[3];
		cw_parseoptions(chanspy_opts, &flags, opts, argv[1]);
		if (cw_test_flag(&flags, OPTION_GROUP))
			args.group = opts[1];
		if (cw_test_flag(&flags, OPTION_RECORD)) {
			if (!(recbase = opts[2]))
				recbase = "chanspy";
		}
		args.silent = cw_test_flag(&flags, OPTION_QUIET);
		args.bronly = cw_test_flag(&flags, OPTION_BRIDGED);
		if (cw_test_flag(&flags, OPTION_VOLUME)  &&  opts[1]) {
			int vol;

			if ((sscanf(opts[0], "%d", &vol) != 1)  ||  (vol > 24)  ||  (vol < -24))
				cw_log(CW_LOG_NOTICE, "Volume factor must be a number between -24dB and 24dB\n");
			else
				args.volfactor = vol;
		}
	}

	if (recbase) {
		char filename[512];
		snprintf(filename,sizeof(filename),"%s/%s.%ld.raw",cw_config_CW_MONITOR_DIR, recbase, time(NULL));
		if ((args.fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644)) <= 0)
			cw_log(CW_LOG_WARNING, "Cannot open %s for recording\n", filename);
	}

	waitms = 100;
	do {
		if (!args.silent) {
			if ((res = cw_streamfile(chan, "beep", chan->language)) == 0)
				res = cw_waitstream(chan, "");
			if (res < 0)
				break;
		}

		args.again = 0;

		if ((res = cw_waitfordigit(chan, waitms)) < 0)
			break;

		cw_registry_iterate_ordered(&channel_registry, chanspy_by_prefix_one, &args);

		waitms = 5000;
	} while (args.again);


	if (args.fd >= 0)
		close(args.fd);

	if (oldrf  &&  cw_set_read_format(chan, oldrf) < 0)
		cw_log(CW_LOG_ERROR, "Could Not Set Read Format.\n");

	if (oldwf  &&  cw_set_write_format(chan, oldwf) < 0)
		cw_log(CW_LOG_ERROR, "Could Not Set Write Format.\n");

	cw_clear_flag(chan, CW_FLAG_SPYING);

	cw_channel_setoption(chan, CW_OPTION_TXGAIN, &zero_volume, sizeof(zero_volume));

	ALL_DONE(u, res);
}

static int unload_module(void)
{
    int res = 0;

    res |= cw_unregister_function(chanspy_app);
    return res;
}

static int load_module(void)
{
    if (!spygen.is_initialized)
        cw_object_init(&spygen, CW_OBJECT_CURRENT_MODULE, 0);

    chanspy_app = cw_register_function(chanspy_name, chanspy_exec, chanspy_synopsis, chanspy_syntax, chanspy_desc);
    return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, chanspy_synopsis)
