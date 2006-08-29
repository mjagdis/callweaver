/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Trivial application to send a TIFF file as a FAX
 * 
 * Copyright (C) 2003, 2005, Steve Underwood
 *
 * Steve Underwood <steveu@coppice.org>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif
 
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>

#include <spandsp.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL$", "$Revision$")

#include "openpbx/lock.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/translate.h"

static char *tdesc = "Trivial FAX Transmit Application";

static char *app = "TxFAX";

static char *synopsis = "Send a FAX file";

static char *descrip = 
"  TxFAX(filename[|caller][|debug][|ecm]):  Send a given TIFF file to the channel as a FAX.\n"
"The \"caller\" option makes the application behave as a calling machine,\n"
"rather than the answering machine. The default behaviour is to behave as\n"
"an answering machine.\n"
"The \"ecm\" option enables ECM.\n"
"Uses LOCALSTATIONID to identify itself to the remote end.\n"
"     LOCALHEADERINFO to generate a header line on each page.\n"
"Sets REMOTESTATIONID to the receiver CSID.\n"
"     PHASEESTATUS to the phase E result status.\n"
"Returns -1 when the user hangs up, or if the file does not exist.\n"
"Returns 0 otherwise.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

#define MAX_BLOCK_SIZE 240

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
/*- End of function --------------------------------------------------------*/
/* Removing alerts generated by the compiler.
static void t30_flush(t30_state_t *s, int which)
{
    //TODO:
}
*/
/*- End of function --------------------------------------------------------*/

static void phase_e_handler(t30_state_t *s, void *user_data, int result)
{
    struct opbx_channel *chan;
    char far_ident[21];
    char buf[11];
    
    chan = (struct opbx_channel *) user_data;
    t30_get_far_ident(s, far_ident);
    pbx_builtin_setvar_helper(chan, "REMOTESTATIONID", far_ident);
    snprintf(buf, sizeof(buf), "%d", result);
    pbx_builtin_setvar_helper(chan, "PHASEESTATUS", buf);
    opbx_log(LOG_DEBUG, "==============================================================================\n");
    if (result == T30_ERR_OK)
        opbx_log(LOG_DEBUG, "Fax successfully sent.\n");
    else
        opbx_log(LOG_DEBUG, "Fax send not successful - result (%d) %s.\n", result, t30_completion_code_to_str(result));
    opbx_log(LOG_DEBUG, "==============================================================================\n");
}
/*- End of function --------------------------------------------------------*/

static int t38_tx_packet_handler(t38_core_state_t *s, void *user_data, const uint8_t *buf, int len, int count)
{
    struct opbx_frame outf;
    struct opbx_channel *chan;

    chan = (struct opbx_channel *) user_data;
    memset(&outf, 0, sizeof(outf));
    outf.frametype = OPBX_FRAME_MODEM;
    outf.subclass = OPBX_MODEM_T38;
    outf.mallocd = 0;
    outf.datalen = len;
    outf.samples = 0;
    outf.data = (char *) buf;
    outf.offset = 0;
    outf.tx_copies = count;
    outf.src = "TxFAX";
    if (opbx_write(chan, &outf) < 0)
        opbx_log(LOG_WARNING, "Unable to write frame to channel; %s\n", strerror(errno));
    return 0;
}
/*- End of function --------------------------------------------------------*/

/* Return a monotonically increasing time, in microseconds */
static int64_t nowis(void)
{
    int64_t now;
#if 0
    struct timeval tv;

    gettimeofday(&tv, NULL);
    now = tv.tv_sec*1000000LL + tv.tv_usec;
#else
    struct timespec ts;
    
    if (clock_gettime(CLOCK_MONOTONIC, &ts))
        opbx_log(LOG_WARNING, "clock_gettime returned %s\n", strerror(errno));
    now = ts.tv_sec*1000000LL + ts.tv_nsec/1000;
#endif
    return now;
}
/*- End of function --------------------------------------------------------*/

static int txfax_exec(struct opbx_channel *chan, void *data)
{
    int res = 0;
    char source_file[256];
    char *x;
    char *s;
    char *t;
    char *v;
    int option;
    int len;
    fax_state_t fax;
    t38_terminal_state_t t38;
    int calling_party;
    int verbose;
    int samples;
    int call_is_t38_mode;
    int ecm = FALSE;
    
    struct localuser *u;
    struct opbx_frame *inf = NULL;
    struct opbx_frame outf;

    int original_read_fmt;
    int original_write_fmt;
    int64_t now;
    int64_t next;
    int64_t passage;
    int delay;
    
    uint8_t __buf[sizeof(uint16_t)*MAX_BLOCK_SIZE + 2*OPBX_FRIENDLY_OFFSET];
    uint8_t *buf = __buf + OPBX_FRIENDLY_OFFSET;

    if (chan == NULL)
    {
        opbx_log(LOG_WARNING, "Fax transmit channel is NULL. Giving up.\n");
        return -1;
    }

    span_set_message_handler(span_message);

    /* The next few lines of code parse out the filename and header from the input string */
    if (data == NULL)
    {
        /* No data implies no filename or anything is present */
        opbx_log(LOG_WARNING, "Txfax requires an argument (filename)\n");
        return -1;
    }
    
    calling_party = FALSE;
    verbose = FALSE;
    source_file[0] = '\0'; 

    for (option = 0, v = s = data;  v;  option++, s++)
    {
        t = s;
        v = strchr(s, '|');
        s = (v)  ?  v  :  s + strlen(s);
        strncpy((char *) buf, t, s - t);
        buf[s - t] = '\0';
        if (option == 0)
        {
            /* The first option is always the file name */
            len = s - t;
            if (len > 255)
                len = 255;
            strncpy(source_file, t, len);
            source_file[len] = '\0';
        }
        else if (strncmp("caller", t, s - t) == 0)
        {
            calling_party = TRUE;
        }
        else if (strncmp("debug", t, s - t) == 0)
        {
            verbose = TRUE;
        }
        else if (strncmp("ecm", t, s - t) == 0)
        {
            ecm = TRUE;
        }
        else if (strncmp("start", t, s - t) == 0)
        {
            /* TODO: handle this */
        }
        else if (strncmp("end", t, s - t) == 0)
        {
            /* TODO: handle this */
        }
    }

    /* Done parsing */

    LOCAL_USER_ADD(u);

    if (chan->_state != OPBX_STATE_UP)
    {
        /* Shouldn't need this, but checking to see if channel is already answered
         * Theoretically the PBX should already have answered before running the app */
        res = opbx_answer(chan);
    }
    
    if (!res)
    {
        original_read_fmt = chan->readformat;
        if (original_read_fmt != OPBX_FORMAT_SLINEAR)
        {
            res = opbx_set_read_format(chan, OPBX_FORMAT_SLINEAR);
            if (res < 0)
            {
                opbx_log(LOG_WARNING, "Unable to set to linear read mode, giving up\n");
                return -1;
            }
        }
        original_write_fmt = chan->writeformat;
        if (original_write_fmt != OPBX_FORMAT_SLINEAR)
        {
            res = opbx_set_write_format(chan, OPBX_FORMAT_SLINEAR);
            if (res < 0)
            {
                opbx_log(LOG_WARNING, "Unable to set to linear write mode, giving up\n");
                res = opbx_set_read_format(chan, original_read_fmt);
                if (res)
                    opbx_log(LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
                return -1;
            }
        }

        memset(&fax, 0, sizeof(fax));
        memset(&t38, 0, sizeof(t38));

        fax_init(&fax, calling_party);
	fax_set_transmit_on_idle(&fax,TRUE);
        if (verbose)
        {
            span_log_set_level(&fax.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
            span_log_set_level(&fax.t30_state.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
        }
        x = pbx_builtin_getvar_helper(chan, "LOCALSTATIONID");
        if (x  &&  x[0])
            t30_set_local_ident(&fax.t30_state, x);
        x = pbx_builtin_getvar_helper(chan, "LOCALHEADERINFO");
        if (x  &&  x[0])
            t30_set_header_info(&fax.t30_state, x);
        t30_set_tx_file(&fax.t30_state, source_file, -1, -1);
        //t30_set_phase_b_handler(&fax.t30_state, phase_b_handler, chan);
        //t30_set_phase_d_handler(&fax.t30_state, phase_d_handler, chan);
        t30_set_phase_e_handler(&fax.t30_state, phase_e_handler, chan);

        t38_terminal_init(&t38, calling_party, t38_tx_packet_handler, chan);
        if (verbose)
        {
            span_log_set_level(&t38.t30_state.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
            span_log_set_level(&t38.t38.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
            span_log_set_level(&t38.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
        }
        x = pbx_builtin_getvar_helper(chan, "LOCALSTATIONID");
        if (x  &&  x[0])
            t30_set_local_ident(&t38.t30_state, x);
        x = pbx_builtin_getvar_helper(chan, "LOCALHEADERINFO");
        if (x  &&  x[0])
            t30_set_header_info(&t38.t30_state, x);
        t30_set_tx_file(&t38.t30_state, source_file, -1, -1);
        //t30_set_phase_b_handler(&t38.t30_state, phase_b_handler, chan);
        //t30_set_phase_d_handler(&t38.t30_state, phase_d_handler, chan);
        t30_set_phase_e_handler(&t38.t30_state, phase_e_handler, chan);

	if (ecm) {
    	    t30_set_ecm_capability(&fax.t30_state, TRUE);
	    t30_set_supported_compressions(&fax.t30_state, T30_SUPPORT_T4_1D_COMPRESSION | T30_SUPPORT_T4_2D_COMPRESSION | T30_SUPPORT_T6_COMPRESSION);
	}
	
        call_is_t38_mode = FALSE;
        passage = nowis();
        next = passage + 30000;
        while (1)
        {
            now = nowis();
            delay = (next < now)  ?  0  :  (next - now + 500)/1000;
            if ((res = opbx_waitfor(chan, delay)) < 0)
                break;
            if (call_is_t38_mode)
	    {
                now = nowis();
                t38_terminal_send_timeout(&t38, (now - passage)/125);
                passage = now;
                /* End application when T38/T30 has finished */
                if ((t38.current_rx_type == T30_MODEM_DONE)  ||  (t38.current_tx_type == T30_MODEM_DONE))
                    break;
            }
            if (res == 0)
            {

		if (!call_is_t38_mode) {
            	    samples = 1;
            	    if ((len = fax_tx(&fax, (int16_t *) &buf[OPBX_FRIENDLY_OFFSET], samples))) {
                	memset(&outf, 0, sizeof(outf));
                	outf.frametype = OPBX_FRAME_VOICE;
                	outf.subclass = OPBX_FORMAT_SLINEAR;
                	outf.datalen = len*sizeof(int16_t);
                	outf.samples = len;
                	outf.data = &buf[OPBX_FRIENDLY_OFFSET];
                	outf.offset = OPBX_FRIENDLY_OFFSET;
                	outf.src = "TxFAX";
                	if (opbx_write(chan, &outf) < 0) {
                    	    opbx_log(LOG_WARNING, "Unable to write frame to channel; %s\n", strerror(errno));
                    	    break;
                	}
			passage = now;
            		next += 20000; // If I remember well, spandsp generates 1 frame every 20ms
		    }
		} else 

            	    next += 30000;
            	continue;
            }
            if ((inf = opbx_read(chan)) == NULL)
            {
                res = -1;
                break;
            }
            if (inf->frametype == OPBX_FRAME_VOICE  &&  !call_is_t38_mode)
            {
                if (fax_rx(&fax, inf->data, inf->samples))
                    break;
                samples = (inf->samples <= MAX_BLOCK_SIZE)  ?  inf->samples  :  MAX_BLOCK_SIZE;
                if ((len = fax_tx(&fax, (int16_t *) &buf[OPBX_FRIENDLY_OFFSET], samples)))
                {
                    memset(&outf, 0, sizeof(outf));
                    outf.frametype = OPBX_FRAME_VOICE;
                    outf.subclass = OPBX_FORMAT_SLINEAR;
                    outf.datalen = len*sizeof(int16_t);
                    outf.samples = len;
                    outf.data = &buf[OPBX_FRIENDLY_OFFSET];
                    outf.offset = OPBX_FRIENDLY_OFFSET;
                    outf.src = "TxFAX";
                    if (opbx_write(chan, &outf) < 0)
                    {
                        opbx_log(LOG_WARNING, "Unable to write frame to channel; %s\n", strerror(errno));
                        break;
                    }
                }
            }
            else if (inf->frametype == OPBX_FRAME_MODEM  &&  inf->subclass == OPBX_MODEM_T38)
            {
                //printf("T.38 frame received\n");
                if (!call_is_t38_mode)
        	{
            		call_is_t38_mode = TRUE;
		    	passage = now;
                }
        	t38_core_rx_ifp_packet(&t38.t38, inf->seq_no, inf->data, inf->datalen);
            }
            opbx_frfree(inf);
        }
        if (inf == NULL)
        {
            opbx_log(LOG_DEBUG, "Got hangup\n");
            res = -1;
        }

        if (!call_is_t38_mode)
	    t30_terminate(&fax.t30_state);
	else
	    t30_terminate(&t38.t30_state);

        if (original_read_fmt != OPBX_FORMAT_SLINEAR)
        {
            if ((res = opbx_set_read_format(chan, original_read_fmt)))
                opbx_log(LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
        }
        if (original_write_fmt != OPBX_FORMAT_SLINEAR)
        {
            if ((res = opbx_set_write_format(chan, original_write_fmt)))
                opbx_log(LOG_WARNING, "Unable to restore write format on '%s'\n", chan->name);
        }
        fax_release(&fax);
    }
    else
    {
        opbx_log(LOG_WARNING, "Could not answer channel '%s'\n", chan->name);
    }
    LOCAL_USER_REMOVE(u);
    return res;
}
/*- End of function --------------------------------------------------------*/

int unload_module(void)
{
    STANDARD_HANGUP_LOCALUSERS;
    return opbx_unregister_application(app);
}
/*- End of function --------------------------------------------------------*/

int load_module(void)
{
    return opbx_register_application(app, txfax_exec, synopsis, descrip);
}
/*- End of function --------------------------------------------------------*/

char *description(void)
{
    return tdesc;
}
/*- End of function --------------------------------------------------------*/

int usecount(void)
{
    int res;

    STANDARD_USECOUNT(res);
    return res;
}
/*- End of function --------------------------------------------------------*/
/*- End of file ------------------------------------------------------------*/
