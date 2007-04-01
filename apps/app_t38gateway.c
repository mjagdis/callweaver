/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Dialing Application
 *
 * Copyright (C) 2005, Anthony Minessale II
 *
 * Anthony Minessale II <anthmct@yahoo.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <openpbx/file.h>
#include <openpbx/logger.h>
#include <openpbx/channel.h>
#include <openpbx/dsp.h>
#include <openpbx/app.h>
#include <openpbx/pbx.h>
#include <openpbx/module.h>
#include <openpbx/lock.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <tiffio.h>
#include <spandsp.h>

static char *desc = "Alternate Dialer Application";

static char *tdesc = "\n"
"Usage T38Gateway(<dialstring>[|<timeout>|<options>])\n\n"
"Options:\n\n"
" h -- Hangup if the call was successful.\n\n"
" r -- Indicate 'ringing' to the caller.\n\n";

static char *app = "T38Gateway";
static char *synopsis = "A PSTN <-> T.38 gateway";

static int opbx_check_hangup_locked(struct opbx_channel *chan)
{
    int res;

    opbx_mutex_lock(&chan->lock);
    res = opbx_check_hangup(chan);
    opbx_mutex_unlock(&chan->lock);
    return res;
}

#define clean_frame(f) if(f) {opbx_fr_free(f); f = NULL;}
#define ALL_DONE(u,ret) {opbx_indicate(chan, -1); LOCAL_USER_REMOVE(u) ; return ret;}

#define ready_to_talk(chan,peer) ((!chan  ||  !peer  ||  opbx_check_hangup_locked(chan)  ||  opbx_check_hangup_locked(peer))  ?  0  :  1)

#define DONE_WITH_ERROR -1
#define RUNNING 1
#define DONE 0

#define MAX_BLOCK_SIZE 240

STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

static void span_message(int level, const char *msg)
{
    int opbx_level;
    
    if (level == SPAN_LOG_ERROR)
        opbx_level = __LOG_ERROR;
    else if (level == SPAN_LOG_WARNING)
        opbx_level = __LOG_WARNING;
    else
        opbx_level = __LOG_DEBUG;
    //opbx_level = __LOG_WARNING;
    opbx_log(opbx_level, __FILE__, __LINE__, __PRETTY_FUNCTION__, msg);
}

/* opbx_bridge_audio(chan,peer);
   this is a no-nonsense optionless bridge function that probably needs to grow a little.
   This function makes no attempt to perform a native bridge or do anything cool because it's
   main usage is for situations where you are doing a translated codec in a VOIP gateway
   where you simply want to forward the call elsewhere.
   This is my perception of what opbx_channel_bridge may have looked like in the beginning ;)
*/
static int opbx_bridge_frames(struct opbx_channel *chan, struct opbx_channel *peer)
{
    struct opbx_channel *active = NULL;
    struct opbx_channel *inactive = NULL;
    struct opbx_channel *channels[2];
    struct opbx_frame *f, *fr2;
    int timeout = -1;
    int running = RUNNING;

    struct opbx_dsp *dsp = NULL;

    if ( !( dsp = opbx_dsp_new() ) )
        opbx_log(LOG_WARNING, "Unable to allocate DSP!\n");
    else {
	opbx_dsp_set_threshold(dsp, 256); 
	opbx_dsp_set_features (dsp, DSP_FEATURE_DTMF_DETECT | DSP_FEATURE_FAX_DETECT);
	opbx_dsp_digitmode    (dsp, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_RELAXDTMF);
    }

    channels[0] = chan;
    channels[1] = peer;

    while (running == RUNNING  &&  (running = ready_to_talk(channels[0], channels[1])))
    {
        if ((active = opbx_waitfor_n(channels, 2, &timeout)))
        {
            inactive = (active == channels[0])  ?   channels[1]  :  channels[0];
            if ((f = opbx_read(active)))
            {

		if ( ( active == chan ) && dsp )
		    fr2 = opbx_frdup(f);

                f->tx_copies = 1; /* TODO: this is only needed because not everything sets the tx_copies field properly */
                opbx_write(inactive, f);
                clean_frame(f);
                channels[0] = inactive;
                channels[1] = active;

		if ( ( active == chan ) && fr2 && dsp) {
        	    fr2 = opbx_dsp_process(active, dsp, fr2);
        	    if (fr2) {
        		if (fr2->frametype == OPBX_FRAME_DTMF)
        		{
            		    if (fr2->subclass == 'f')
            		    {
    				opbx_log(LOG_DEBUG, "Fax detected in T38 Gateway !!!\n");
                		opbx_app_request_t38(active);
			    }
			}
			if (f != fr2)
            		    opbx_fr_free(fr2);
		    }
		}
            }
            else
            {
                running = DONE;
            }
        }
        /* Check if we need to change to gateway operation */
        if (chan->t38mode_enabled != peer->t38mode_enabled) {
            break;
	}
    }

    if (dsp)
        opbx_dsp_free(dsp);

    return running;
}

static int t38_tx_packet_handler(t38_core_state_t *s, void *user_data, const uint8_t *buf, int len, int count)
{
    struct opbx_frame outf;
    struct opbx_channel *chan;

    chan = (struct opbx_channel *) user_data;
    opbx_fr_init_ex(&outf, OPBX_FRAME_MODEM, OPBX_MODEM_T38, "T38Gateway");
    outf.datalen = len;
    outf.data = (uint8_t *) buf;
opbx_log(LOG_WARNING, "t38_tx_packet_handler: Sending %d copies of frame\n", count);
    outf.tx_copies = count;
    if (opbx_write(chan, &outf) < 0)
        opbx_log(LOG_WARNING, "Unable to write frame to channel; %s\n", strerror(errno));
    return 0;
}

static int opbx_t38_gateway(struct opbx_channel *chan, struct opbx_channel *peer, int verbose)
{
    struct opbx_channel *active = NULL;
    struct opbx_channel *inactive = NULL;
    struct opbx_channel *channels[2];
    struct opbx_frame *f;
    struct opbx_frame outf;
    int timeout = -1;
    int running = RUNNING;
    int original_read_fmt;
    int original_write_fmt;
    int res;
    int samples;
    int len;
    t38_gateway_state_t t38_state;
    uint8_t __buf[sizeof(uint16_t)*MAX_BLOCK_SIZE + 2*OPBX_FRIENDLY_OFFSET];
    uint8_t *buf = __buf + OPBX_FRIENDLY_OFFSET;

    if (chan->t38mode_enabled)
    {
        channels[0] = chan;
        channels[1] = peer;
    }
    else
    {
        channels[0] = peer;
        channels[1] = chan;
    }

    original_read_fmt = channels[1]->readformat;
    original_write_fmt = channels[1]->writeformat;
    if (!channels[1]->t38mode_enabled)
    {
        if (original_read_fmt != OPBX_FORMAT_SLINEAR)
        {
            if ((res = opbx_set_read_format(channels[1], OPBX_FORMAT_SLINEAR)) < 0)
            {
                opbx_log(LOG_WARNING, "Unable to set to linear read mode, giving up\n");
                return -1;
            }
        }
        if (original_write_fmt != OPBX_FORMAT_SLINEAR)
        {
            if ((res = opbx_set_write_format(channels[1], OPBX_FORMAT_SLINEAR)) < 0)
            {
                opbx_log(LOG_WARNING, "Unable to set to linear write mode, giving up\n");
                if ((res = opbx_set_read_format(channels[1], original_read_fmt)))
                    opbx_log(LOG_WARNING, "Unable to restore read format on '%s'\n", channels[1]->name);
                return -1;
            }
        }
    }

    if (t38_gateway_init(&t38_state, t38_tx_packet_handler, channels[0]) == NULL)
    {
        opbx_log(LOG_WARNING, "Unable to start the T.38 gateway\n");
        return -1;
    }

    span_log_set_message_handler(&t38_state.logging, span_message);
    span_log_set_message_handler(&t38_state.t38.logging, span_message);
    if (verbose)
    {
        span_log_set_level(&t38_state.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
        span_log_set_level(&t38_state.t38.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
    }
    t38_set_t38_version(&t38_state.t38, 0);
    t38_gateway_ecm_control(&t38_state, 1);


    while (running == RUNNING  &&  (running = ready_to_talk(channels[0], channels[1])))
    {
        if ((active = opbx_waitfor_n(channels, 2, &timeout)))
        {
            if (active == channels[0])
            {
                if ((f = opbx_read(active)))
                {
                    t38_core_rx_ifp_packet(&t38_state.t38, f->seq_no, f->data, f->datalen);
                    clean_frame(f);
                }
                else
                {
                    running = DONE;
                }
            }
            else
            {
                if ((f = opbx_read(active)))
                {
                    if (t38_gateway_rx(&t38_state, f->data, f->samples))
                        break;
                    samples = (f->samples <= MAX_BLOCK_SIZE)  ?  f->samples  :  MAX_BLOCK_SIZE;
                    if ((len = t38_gateway_tx(&t38_state, (int16_t *) &buf[OPBX_FRIENDLY_OFFSET], samples)))
                    {
                        opbx_fr_init_ex(&outf, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, "T38Gateway");
                        outf.datalen = len*sizeof(int16_t);
                        outf.samples = len;
                        outf.data = &buf[OPBX_FRIENDLY_OFFSET];
                        outf.offset = OPBX_FRIENDLY_OFFSET;
                        if (opbx_write(channels[1], &outf) < 0)
                        {
                            opbx_log(LOG_WARNING, "Unable to write frame to channel; %s\n", strerror(errno));
                            break;
                        }
                    }
                    clean_frame(f);
                }
                else
                {
                    running = DONE;
                }
                inactive = (active == channels[0])  ?   channels[1]  :  channels[0];
            }
        }
    }

    if (original_read_fmt != OPBX_FORMAT_SLINEAR)
    {
        if ((res = opbx_set_read_format(channels[1], original_read_fmt)))
            opbx_log(LOG_WARNING, "Unable to restore read format on '%s'\n", channels[1]->name);
    }
    if (original_write_fmt != OPBX_FORMAT_SLINEAR)
    {
        if ((res = opbx_set_write_format(channels[1], original_write_fmt)))
            opbx_log(LOG_WARNING, "Unable to restore write format on '%s'\n", channels[1]->name);
    }
    return running;
}

static int t38gateway_exec(struct opbx_channel *chan, void *data)
{
    int res = 0;
    struct localuser *u;
    char *tech = NULL;
    char *dest = NULL;
    char *to = NULL;
    char *flags = "";
    struct opbx_channel *peer;
    int state = 0, ready = 0;
    int timeout = 60000;
    int format = chan->nativeformats;
    struct opbx_frame *f;
    int verbose;
    
    if (!data)
    {
        opbx_log(LOG_WARNING, "t38gateway requires an argument\n");
        return -1;
    }
    LOCAL_USER_ADD(u);
    verbose = TRUE;
    tech = opbx_strdupa((char *) data);
    if ((to = strchr(tech, '|')))
    {
        *to = '\0';
        to++;

        if ((flags = strchr(to, '|')))
        {
            *flags = '\0';
            flags++;
        }

        if ((timeout = atoi(to)))
            timeout *= 1000;
        else
            timeout = 60000;
    }
    if ((dest = strchr(tech, '/')))
    {
        int cause = 0;
        *dest = '\0';
        dest++;

        if (!(peer = opbx_request(tech, format, dest, &cause)))
        {
            opbx_log(LOG_ERROR, "Error creating channel %s/%s\n", tech, dest);
            ALL_DONE(u, 0);
        }
        if (flags  &&  strchr(flags, 'r'))
            opbx_indicate(chan, OPBX_CONTROL_RINGING);
    }
    else
    {
        opbx_log(LOG_ERROR, "Error creating channel. Invalid name %s\n", tech);
        ALL_DONE(u, 0);
    }

    if ((res = opbx_call(peer, dest, 0)) < 0)
        ALL_DONE(u, -1); 

    /* While we haven't timed out and we still have no channel up */
    while (timeout  &&  (peer->_state != OPBX_STATE_UP))
    {
        res = opbx_waitfor(peer, timeout);
        /* Something is not cool */
        if (res < 0)
            break;
        /* Timed out, so we are done trying */
        if (res == 0)
            break;
        /* -1 means go forever */
        if (timeout > -1)
        {
            /* res holds the number of milliseconds remaining */
            timeout = res;
            if (timeout < 0)
                timeout = 0;
        }
        f = opbx_read(peer);
        if (f == NULL)
        {
            state = OPBX_CONTROL_HANGUP;
            res = 0;
            break;
        }
        if (f->frametype == OPBX_FRAME_CONTROL)
        {
            if (f->subclass == OPBX_CONTROL_RINGING)
            {
                state = f->subclass;
            }
            else if ((f->subclass == OPBX_CONTROL_BUSY)  ||  (f->subclass == OPBX_CONTROL_CONGESTION))
            {
                state = f->subclass;
                opbx_fr_free(f);
                break;
            }
            else if (f->subclass == OPBX_CONTROL_ANSWER)
            {
                /* This is what we are hoping for */
                state = f->subclass;
                opbx_fr_free(f);
                ready = 1;
                break;
            } 
            /* else who cares */
        }
        opbx_fr_free(f);
    }

    res = 1;
    if (ready  &&  ready_to_talk(chan, peer))
    {
        if (!opbx_channel_make_compatible(chan, peer))
        {
            opbx_answer(chan);
            peer->appl = app;
            peer->data = opbx_strdupa(chan->name);
            if (flags  &&  strchr(flags, 'r'))
                opbx_indicate(chan, -1);

            opbx_set_callerid(peer, chan->cid.cid_name, chan->cid.cid_num, chan->cid.cid_num);

            if (res  &&  chan->t38mode_enabled == peer->t38mode_enabled)
            {
                // Same on both sides, so just bridge 
                opbx_log(LOG_NOTICE, "Bridging frames\n");
                res = opbx_bridge_frames(chan, peer);
            }
            if (res  &&  chan->t38mode_enabled != peer->t38mode_enabled)
            {
                // Different on each side, so gateway 
                opbx_log(LOG_NOTICE, "Doing T.38 gateway\n");
                res = opbx_t38_gateway(chan, peer, verbose);
            }

        }
        else
        {
            opbx_log(LOG_ERROR, "failed to make remote_channel %s/%s Compatible\n", tech, dest);
        }
    }
    else
    {
        opbx_log(LOG_ERROR, "failed to get remote_channel %s/%s\n", tech, dest);
    }

    if (peer)
        opbx_hangup(peer);

    /* Hangup if the call worked and you spec the h flag */
    ALL_DONE(u, (!res  &&  (flags  &&  strchr(flags, 'h')))  ?  -1  :  0);
}

int unload_module(void)
{
    STANDARD_HANGUP_LOCALUSERS;
    return opbx_unregister_application(app);
}

int load_module(void)
{
    return opbx_register_application(app, t38gateway_exec, synopsis, tdesc);
}

char *description(void)
{
    return desc;
}

int usecount(void)
{
    int res;

    STANDARD_USECOUNT(res);
    return res;
}
