/*
 * vim:softtabstop=4:noexpandtab
 *
 * CallWeaver -- An open source telephony toolkit.
 *
 * Trivial application to act as a T.38 gateway
 * 
 * Copyright (C) 2005, Anthony Minessale II
 * Copyright (C) 2003, 2005, Steve Underwood
 *
 * Anthony Minessale II <anthmct@yahoo.com>
 * Steve Underwood <steveu@coppice.org>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 */

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <tiffio.h>
#include <spandsp.h>

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/causes.h"
#include "callweaver/dsp.h"
#include "callweaver/app.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/lock.h"

static char *tdesc = "T.38 Gateway Dialer Application";

static void *t38gateway_app;
static const char *t38gateway_name = "T38Gateway";
static const char *t38gateway_synopsis = "A PSTN <-> T.38 gateway";
static const char *t38gateway_syntax = "T38Gateway(dialstring[, timeout[, options]])";
static const char *t38gateway_descrip =
"Options:\n\n"
" h -- Hangup if the call was successful.\n\n"
" r -- Indicate 'ringing' to the caller.\n\n";


static int opbx_check_hangup_locked(struct opbx_channel *chan)
{
    int res;

    opbx_mutex_lock(&chan->lock);
    res = opbx_check_hangup(chan);
    opbx_mutex_unlock(&chan->lock);
    return res;
}

#define clean_frame(f) if(f) {opbx_fr_free(f); f = NULL;}
#define ALL_DONE(u,ret) {pbx_builtin_setvar_helper(chan, "DIALSTATUS", status); opbx_indicate(chan, -1); LOCAL_USER_REMOVE(u) ; return ret;}

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
    struct opbx_frame *f;
    struct opbx_frame *fr2;
    int timeout = -1;
    int running = RUNNING;
    struct opbx_dsp *dsp_cng = NULL;
    struct opbx_dsp *dsp_ced = NULL;

    if ((dsp_cng = opbx_dsp_new()) == NULL)
    {
        opbx_log(LOG_WARNING, "Unable to allocate DSP!\n");
    }
    else
    {
        opbx_dsp_set_threshold(dsp_cng, 256); 
        opbx_dsp_set_features(dsp_cng, DSP_FEATURE_DTMF_DETECT | DSP_FEATURE_FAX_CNG_DETECT);
        opbx_dsp_digitmode(dsp_cng, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_RELAXDTMF);
    }

    if ((dsp_ced = opbx_dsp_new()) == NULL)
    {
        opbx_log(LOG_WARNING, "Unable to allocate DSP!\n");
    }
    else
    {
        opbx_dsp_set_threshold(dsp_ced, 256); 
        opbx_dsp_set_features(dsp_ced, DSP_FEATURE_FAX_CED_DETECT);
    }

    channels[0] = chan;
    channels[1] = peer;

    while (running == RUNNING  &&  (running = ready_to_talk(channels[0], channels[1])))
    {

//opbx_log(LOG_NOTICE, "br: t38 status: [%d,%d]\n", chan->t38_status, peer->t38_status);

        if ((active = opbx_waitfor_n(channels, 2, &timeout)))
        {
            inactive = (active == channels[0])  ?   channels[1]  :  channels[0];
            if ((f = opbx_read(active)))
            {

                if (dsp_ced  &&  dsp_cng)
                    fr2 = opbx_frdup(f);
                else
                    fr2 = NULL;

                f->tx_copies = 1; /* TODO: this is only needed because not everything sets the tx_copies field properly */
		
    		if ( ( chan->t38_status == T38_NEGOTIATING ) || ( peer->t38_status == T38_NEGOTIATING ) ) {
		/*  TODO 
		    This is a very BASIC method to mute a channel. It should be improved
		    and we should send EMPTY frames (not just avoid sending them) 
		*/
		    opbx_log(LOG_NOTICE, "channels are muted.\n");
		}
		else
            	    opbx_write(inactive, f);
		    
                clean_frame(f);
                channels[0] = inactive;
                channels[1] = active;

                if (active == chan)
                {
                    /* Look for FAX CNG tone */
                    if (fr2  &&  dsp_cng)
                    {
                        if ((fr2 = opbx_dsp_process(active, dsp_cng, fr2)))
                        {
                            if (fr2->frametype == OPBX_FRAME_DTMF)
                            {
                                if (fr2->subclass == 'f')
                                {
                                    opbx_log(LOG_DEBUG, "FAX CNG detected in T38 Gateway !!!\n");
                                    opbx_app_request_t38(chan);
                                }
                            }
                        }
                    }
                }
                else
                {
                    /* Look for FAX CED tone or V.21 preamble */
                    if (fr2  &&  dsp_ced)
                    {
                        if ((fr2 = opbx_dsp_process(active, dsp_ced, fr2)))
                        {
                            if (fr2->frametype == OPBX_FRAME_DTMF)
                            {
                                if (fr2->subclass == 'F')
                                {
                                    opbx_log(LOG_DEBUG, "FAX CED detected in T38 Gateway !!!\n");
                                    opbx_app_request_t38(chan);
                                }
                            }
                        }
                    }
                }
                if (f != fr2)
                {
                    if (fr2)
                        opbx_fr_free(fr2);
                    fr2 = NULL;
                }
            }
            else
            {
                running = DONE;
            }
        }
        /* Check if we need to change to gateway operation */
        if ( 
	        ( chan->t38_status != T38_NEGOTIATING ) 
	     && ( peer->t38_status != T38_NEGOTIATING )
	     && ( chan->t38_status != peer->t38_status) 
	   ) {
            opbx_log(LOG_DEBUG, "Stop bridging frames. [ %d,%d]\n", chan->t38_status, peer->t38_status);
            break;
	}
    }

    if (dsp_cng)
        opbx_dsp_free(dsp_cng);
    if (dsp_ced)
        opbx_dsp_free(dsp_ced);

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
    opbx_log(LOG_DEBUG, "t38_tx_packet_handler: Sending %d copies of frame\n", count);
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

    if ( chan->t38_status == T38_NEGOTIATED )
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
    if ( channels[1]->t38_status != T38_NEGOTIATED)
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
    t38_gateway_set_transmit_on_idle(&t38_state, TRUE);

    span_log_set_message_handler(&t38_state.logging, span_message);
    span_log_set_message_handler(&t38_state.t38.logging, span_message);
    if (verbose)
    {
        span_log_set_level(&t38_state.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
        span_log_set_level(&t38_state.t38.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
    }
    t38_set_t38_version(&t38_state.t38, 0);
    t38_gateway_set_ecm_capability(&t38_state, 1);


    while (running == RUNNING  &&  (running = ready_to_talk(channels[0], channels[1])))
    {
//opbx_log(LOG_NOTICE, "gw: t38status: [%d,%d]\n", chan->t38_status, peer->t38_status);

        if ((active = opbx_waitfor_n(channels, 2, &timeout)))
        {
            if (active == channels[0])
            {
                if ((f = opbx_read(active)))
                {
                    t38_core_rx_ifp_packet(&t38_state.t38, f->data, f->datalen, f->seq_no);
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

static int t38gateway_exec(struct opbx_channel *chan, int argc, char **argv)
{
    int res = 0;
    struct localuser *u;
    char *dest = NULL;
    struct opbx_channel *peer;
    int state = 0, ready = 0;
    int timeout;
    int format = chan->nativeformats;
    struct opbx_frame *f;
    int verbose;
    char status[256];
    struct opbx_channel *active = NULL;
    struct opbx_channel *channels[2];
    
    if (argc < 1  ||  argc > 3  ||  !argv[0][0])
    {
        opbx_log(LOG_ERROR, "Syntax: %s\n", t38gateway_syntax);
        return -1;
    }

    LOCAL_USER_ADD(u);

    verbose = TRUE;

    timeout = (argc > 1 && argv[1][0] ? atoi(argv[1]) * 1000 : 60000);

    if ((dest = strchr(argv[0], '/')))
    {
        int cause = 0;
        *dest = '\0';
        dest++;

        if (!(peer = opbx_request(argv[0], format, dest, &cause)))
        {
            strncpy(status, "CHANUNAVAIL", sizeof(status) - 1); /* assume as default */
            opbx_log(LOG_ERROR, "Error creating channel %s/%s\n", argv[0], dest);
            ALL_DONE(u, 0);
        }

        if (peer)
        {
            opbx_channel_inherit_variables(chan, peer);
            peer->appl = "AppT38GW (Outgoing Line)";
            peer->whentohangup = 0;
            if (peer->cid.cid_num)
                free(peer->cid.cid_num);
            peer->cid.cid_num = NULL;
            if (peer->cid.cid_name)
                free(peer->cid.cid_name);
            peer->cid.cid_name = NULL;
            if (peer->cid.cid_ani)
                free(peer->cid.cid_ani);
            peer->cid.cid_ani = NULL;
            peer->transfercapability = chan->transfercapability;
            peer->adsicpe = chan->adsicpe;
            peer->cid.cid_tns = chan->cid.cid_tns;
            peer->cid.cid_ton = chan->cid.cid_ton;
            peer->cid.cid_pres = chan->cid.cid_pres;
            peer->cdrflags = chan->cdrflags;
            if (chan->cid.cid_rdnis)
                peer->cid.cid_rdnis = strdup(chan->cid.cid_rdnis);
            if (chan->cid.cid_num) 
                peer->cid.cid_num = strdup(chan->cid.cid_num);
            if (chan->cid.cid_name) 
                peer->cid.cid_name = strdup(chan->cid.cid_name);
            if (chan->cid.cid_ani) 
                peer->cid.cid_ani = strdup(chan->cid.cid_ani);
            opbx_copy_string(peer->language, chan->language, sizeof(peer->language));
            opbx_copy_string(peer->accountcode, chan->accountcode, sizeof(peer->accountcode));
            peer->cdrflags = chan->cdrflags;
            if (opbx_strlen_zero(peer->musicclass))
                opbx_copy_string(peer->musicclass, chan->musicclass, sizeof(peer->musicclass));        
        }
        if (argc > 2 && strchr(argv[2], 'r'))
            opbx_indicate(chan, OPBX_CONTROL_RINGING);
    }
    else
    {
        opbx_log(LOG_ERROR, "Error creating channel. Invalid name %s\n", argv[0]);
        ALL_DONE(u, 0);
    }
    if ((res = opbx_call(peer, dest, 0)) < 0)
        ALL_DONE(u, -1); 
    strncpy(status, "CHANUNAVAIL", sizeof(status) - 1); /* assume as default */
    channels[0] = peer;
    channels[1] = chan;
    /* While we haven't timed out and we still have no channel up */
    while (timeout  &&  (peer->_state != OPBX_STATE_UP))
    {
        active = opbx_waitfor_n(channels, 2, &timeout);
        if (active)
        {
          /* Timed out, so we are done trying */
            if (timeout == 0)
            {
                strncpy(status, "NOANSWER", sizeof(status) - 1);
                opbx_log(LOG_NOTICE, "Timeout on peer\n");
                break;
            }
            /* -1 means go forever */
            if (timeout > -1)
            {
                /* res holds the number of milliseconds remaining */
                if (timeout < 0)
                {
                    timeout = 0;
                    strncpy(status, "NOANSWER", sizeof(status) - 1);
                }
            }
            if (active == peer)
            {
               if ((f = opbx_read(active)) == NULL)
               {
                  state = OPBX_CONTROL_HANGUP;
		  chan->hangupcause = peer->hangupcause;
                  res = 0;
                  break;
               }

               if (f->frametype == OPBX_FRAME_CONTROL)
               {
                  if (f->subclass == OPBX_CONTROL_RINGING)
                  {
                     state = f->subclass;
                     opbx_indicate(chan, OPBX_CONTROL_RINGING);
                     opbx_fr_free(f);
                     break;
                  }
                  else if ((f->subclass == OPBX_CONTROL_BUSY)  ||  (f->subclass == OPBX_CONTROL_CONGESTION))
                  {
                     state = f->subclass;
		     chan->hangupcause = peer->hangupcause;
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
            }
            else
            {
                /* orig channel reports something */
                if ((f = opbx_read(active)) == NULL)
                {
                    state = OPBX_CONTROL_HANGUP;
                    opbx_log(LOG_DEBUG, "Hangup from remote channel\n");
                    res = 0;
                    break;
                }
                if (f->frametype == OPBX_FRAME_CONTROL)
                {
                    if (f->subclass == OPBX_CONTROL_HANGUP)
                    {
                        state = f->subclass;
                        res = 0;
                        opbx_fr_free(f);
                        break;
                    }
                }
            }
            opbx_fr_free(f);
        }
    }

    res = 1;
    if (ready  &&  ready_to_talk(chan, peer))
    {
        if (!opbx_channel_make_compatible(chan, peer))
        {
            opbx_answer(chan);
            peer->appl = t38gateway_name;
	    
            /* FIXME original patch removes the if line below - trying with it before removing it */
            if (argc > 2  &&  strchr(argv[2], 'r'))
                opbx_indicate(chan, -1);

            opbx_set_callerid(peer, chan->cid.cid_name, chan->cid.cid_num, chan->cid.cid_num);
	    chan->hangupcause = OPBX_CAUSE_NORMAL_CLEARING;
	    
            if ( res && ( chan->t38_status == peer->t38_status ) )
            {
                // Same on both sides, so just bridge 
                opbx_log(LOG_DEBUG, "Bridging frames [ %d,%d]\n", chan->t38_status, peer->t38_status);
                res = opbx_bridge_frames(chan, peer);
            }
	    
            if ( res  
		&& ( ( chan->t38_status == T38_STATUS_UNKNOWN ) || ( peer->t38_status != T38_STATUS_UNKNOWN ) )
		&& ( chan->t38_status != peer->t38_status ) )
            {
                // Different on each side, so gateway 
                opbx_log(LOG_DEBUG, "Doing T.38 gateway [ %d,%d]\n", chan->t38_status, peer->t38_status);
                res = opbx_t38_gateway(chan, peer, verbose);
            }
        }
        else
        {
            opbx_log(LOG_ERROR, "failed to make remote_channel %s/%s Compatible\n", argv[0], dest);
        }
    }
    else
    {
        opbx_log(LOG_ERROR, "failed to get remote_channel %s/%s\n", argv[0], dest);
    }
    if (state == OPBX_CONTROL_ANSWER)
       strncpy(status, "ANSWER", sizeof(status) - 1);
    if (state == OPBX_CONTROL_BUSY)
        strncpy(status, "BUSY", sizeof(status) - 1);
    if (state == OPBX_CONTROL_CONGESTION)
         strncpy(status, "CONGESTION", sizeof(status) - 1);
    if (state == OPBX_CONTROL_HANGUP)
         strncpy(status, "CANCEL", sizeof(status) - 1);
    pbx_builtin_setvar_helper(chan, "DIALSTATUS", status);
    opbx_log(LOG_NOTICE, "T38Gateway exit with %s\n", status);
    if (peer)
        opbx_hangup(peer);

    /* Hangup if the call worked and you spec the h flag */
    ALL_DONE(u, (!res  &&  (argc > 2  &&  strchr(argv[2], 'h')))  ?  -1  :  0);
}

int unload_module(void)
{
    int res = 0;

    STANDARD_HANGUP_LOCALUSERS;
    res |= opbx_unregister_application(t38gateway_app);
    return res;
}

int load_module(void)
{
    t38gateway_app = opbx_register_application(t38gateway_name, t38gateway_exec, t38gateway_synopsis, t38gateway_syntax, t38gateway_descrip);
    return 0;
}

char *description(void)
{
    return tdesc;
}

int usecount(void)
{
    int res;

    STANDARD_USECOUNT(res);
    return res;
}
