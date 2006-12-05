/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Trivial application to receive a TIFF FAX file
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

#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <pthread.h>
#include <errno.h>

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
#include "openpbx/dsp.h"
#include "openpbx/manager.h"

static char *tdesc = "Trivial FAX Receive Application";

static char *app = "RxFAX";

static char *synopsis = "Receive a FAX to a file";

static char *descrip = 
"  RxFAX(filename[|caller][|debug][|ecm]): Receives a FAX from the channel into the\n"
"given filename. If the file exists it will be overwritten. The file\n"
"should be in TIFF/F format.\n"
"The \"caller\" option makes the application behave as a calling machine,\n"
"rather than the answering machine. The default behaviour is to behave as\n"
"an answering machine.\n"
"The \"ecm\" option enables ECM.\n"
"Uses LOCALSTATIONID to identify itself to the remote end.\n"
"     LOCALHEADERINFO to generate a header line on each page.\n"
"Sets REMOTESTATIONID to the sender CSID.\n"
"     FAXPAGES to the number of pages received.\n"
"     FAXBITRATE to the transmition rate.\n"
"     FAXRESOLUTION to the resolution.\n"
"     PHASEESTATUS to the phase E result status.\n"
"     PHASEESTRING to the phase E result string.\n"
"Returns -1 when the user hangs up.\n"
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

/* remove compiler warnings
static void t30_flush(t30_state_t *s, int which)
{
    //TODO:
}
*/
/*- End of function --------------------------------------------------------*/

static void phase_e_handler(t30_state_t *s, void *user_data, int result)
{
    struct opbx_channel *chan;
    t30_stats_t t;
    char local_ident[21];
    char far_ident[21];
    char buf[128];
    
    chan = (struct opbx_channel *) user_data;
    t30_get_transfer_statistics(s, &t);
    t30_get_local_ident(s, local_ident);
    t30_get_far_ident(s, far_ident);
    pbx_builtin_setvar_helper(chan, "REMOTESTATIONID", far_ident);
    snprintf(buf, sizeof(buf), "%d", t.pages_transferred);
    pbx_builtin_setvar_helper(chan, "FAXPAGES", buf);
    snprintf(buf, sizeof(buf), "%d", t.y_resolution);
    pbx_builtin_setvar_helper(chan, "FAXRESOLUTION", buf);
    snprintf(buf, sizeof(buf), "%d", t.bit_rate);
    pbx_builtin_setvar_helper(chan, "FAXBITRATE", buf);
    snprintf(buf, sizeof(buf), "%d", result);
    pbx_builtin_setvar_helper(chan, "PHASEESTATUS", buf);
    snprintf(buf, sizeof(buf), "%s", t30_completion_code_to_str(result));
    pbx_builtin_setvar_helper(chan, "PHASEESTRING", buf);

    opbx_log(LOG_DEBUG, "==============================================================================\n");
    if (result == T30_ERR_OK)
    {
        opbx_log(LOG_DEBUG, "Fax successfully received.\n");
        opbx_log(LOG_DEBUG, "Remote station id: %s\n", far_ident);
        opbx_log(LOG_DEBUG, "Local station id:  %s\n", local_ident);
        opbx_log(LOG_DEBUG, "Pages transferred: %i\n", t.pages_transferred);
        opbx_log(LOG_DEBUG, "Image resolution:  %i x %i\n", t.x_resolution, t.y_resolution);
        opbx_log(LOG_DEBUG, "Transfer Rate:     %i\n", t.bit_rate);
        manager_event(EVENT_FLAG_CALL,
                      "FaxReceived", "Channel: %s\nExten: %s\nCallerID: %s\nRemoteStationID: %s\nLocalStationID: %s\nPagesTransferred: %i\nResolution: %i\nTransferRate: %i\nFileName: %s\n",
                      chan->name,
                      chan->exten,
                      (chan->cid.cid_num)  ?  chan->cid.cid_num  :  "",
                      far_ident,
                      local_ident,
                      t.pages_transferred,
                      t.y_resolution,
                      t.bit_rate,
                      s->rx_file);
    }
    else
    {
        opbx_log(LOG_DEBUG, "Fax receive not successful - result (%d) %s.\n", result, t30_completion_code_to_str(result));
    }
    opbx_log(LOG_DEBUG, "==============================================================================\n");
}
/*- End of function --------------------------------------------------------*/

static void phase_d_handler(t30_state_t *s, void *user_data, int result)
{
    struct opbx_channel *chan;
    t30_stats_t t;
    
    chan = (struct opbx_channel *) user_data;
    if (result)
    {
        t30_get_transfer_statistics(s, &t);
        opbx_log(LOG_DEBUG, "==============================================================================\n");
        opbx_log(LOG_DEBUG, "Pages transferred:  %i\n", t.pages_transferred);
        opbx_log(LOG_DEBUG, "Image size:         %i x %i\n", t.width, t.length);
        opbx_log(LOG_DEBUG, "Image resolution    %i x %i\n", t.x_resolution, t.y_resolution);
        opbx_log(LOG_DEBUG, "Transfer Rate:      %i\n", t.bit_rate);
        opbx_log(LOG_DEBUG, "Bad rows            %i\n", t.bad_rows);
        opbx_log(LOG_DEBUG, "Longest bad row run %i\n", t.longest_bad_row_run);
        opbx_log(LOG_DEBUG, "Compression type    %s\n", t4_encoding_to_str(t.encoding));
        opbx_log(LOG_DEBUG, "Image size (bytes)  %i\n", t.image_size);
        opbx_log(LOG_DEBUG, "==============================================================================\n");
    }
}
/*- End of function --------------------------------------------------------*/

static int t38_tx_packet_handler(t38_core_state_t *s, void *user_data, const uint8_t *buf, int len, int count)
{
    struct opbx_frame outf;
    struct opbx_channel *chan;

    chan = (struct opbx_channel *) user_data;
    opbx_fr_init_ex(&outf, OPBX_FRAME_MODEM, OPBX_MODEM_T38, "RxFAX");
    outf.datalen = len;
    outf.data = (uint8_t *) buf;
    outf.tx_copies = count;
    if (opbx_write(chan, &outf) < 0)
        opbx_log(LOG_WARNING, "Unable to write frame to channel; %s\n", strerror(errno));
    return 0;
}
/*- End of function --------------------------------------------------------*/

/* Return a monotonically increasing time, in microseconds */
static int64_t nowis(void)
{
    int64_t now;
#ifndef HAVE_POSIX_TIMERS
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

static int rxfax_exec(struct opbx_channel *chan, void *data)
{
    int res = 0;
    char template_file[256];
    char target_file[256];
    char *s;
    char *t;
    char *v;
    char *x;
    int option;
    int len;
    int i;
    fax_state_t fax;
    t38_terminal_state_t t38;
    int calling_party;
    int verbose;
    int ecm = FALSE;
    int rxpkt;
    int samples;
    int call_is_t38_mode;

    struct localuser *u;
    struct opbx_frame *inf = NULL;
    struct opbx_frame outf;

    int original_read_fmt;
    int original_write_fmt;
    int64_t now;
    int64_t next;
    int64_t passage;
    int delay;

    time_t begin,thistime;

    pbx_builtin_setvar_helper(chan, "REMOTESTATIONID", "");
    pbx_builtin_setvar_helper(chan, "FAXPAGES", "");
    pbx_builtin_setvar_helper(chan, "FAXRESOLUTION", "");
    pbx_builtin_setvar_helper(chan, "FAXBITRATE", "");
    pbx_builtin_setvar_helper(chan, "PHASEESTATUS", "");
    pbx_builtin_setvar_helper(chan, "PHASEESTRING", "");

    uint8_t __buf[sizeof(uint16_t)*MAX_BLOCK_SIZE + 2*OPBX_FRIENDLY_OFFSET];
    uint8_t *buf = __buf + OPBX_FRIENDLY_OFFSET;

    if (chan == NULL)
    {
        opbx_log(LOG_WARNING, "Fax receive channel is NULL. Giving up.\n");
        return -1;
    }

    /* The next few lines of code parse out the filename and header from the input string */
    if (data == NULL)
    {
        /* No data implies no filename or anything is present */
        opbx_log(LOG_WARNING, "Rxfax requires an argument (filename)\n");
        return -1;
    }
    
    calling_party = FALSE;
    verbose = FALSE;
    target_file[0] = '\0';

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
            strncpy(target_file, t, len);
            target_file[len] = '\0';
            /* Allow the use of %d in the file name for a wild card of sorts, to
               create a new file with the specified name scheme */
            if ((x = strchr(target_file, '%'))  &&  x[1] == 'd')
            {
                strcpy(template_file, target_file);
                i = 0;
                do
                {
                    snprintf(target_file, 256, template_file, 1);
                    i++;
                }
                while (opbx_fileexists(target_file, "", chan->language) != -1);
            }
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
    }

    /* Done parsing */

    LOCAL_USER_ADD(u);

    if (chan->_state != OPBX_STATE_UP)
    {
        /* Shouldn't need this, but checking to see if channel is already answered
         * Theoretically asterisk should already have answered before running the app */
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
                LOCAL_USER_REMOVE(u);
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
                LOCAL_USER_REMOVE(u);
                return -1;
            }
        }
        fax_init(&fax, calling_party);
        span_log_set_message_handler(&fax.logging, span_message);
        span_log_set_message_handler(&fax.t30_state.logging, span_message);
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
        t30_set_rx_file(&fax.t30_state, target_file, -1);
        //t30_set_phase_b_handler(&fax.t30_state, phase_b_handler, chan);
        t30_set_phase_d_handler(&fax.t30_state, phase_d_handler, chan);
        t30_set_phase_e_handler(&fax.t30_state, phase_e_handler, chan);

        t38_terminal_init(&t38, calling_party, t38_tx_packet_handler, chan);
        span_log_set_message_handler(&t38.logging, span_message);
        span_log_set_message_handler(&t38.t30_state.logging, span_message);
        span_log_set_message_handler(&t38.t38.logging, span_message);
        if (verbose)
        {
            span_log_set_level(&t38.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
            span_log_set_level(&t38.t30_state.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
            span_log_set_level(&t38.t38.logging, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
        }
        x = pbx_builtin_getvar_helper(chan, "LOCALSTATIONID");
        if (x  &&  x[0])
            t30_set_local_ident(&t38.t30_state, x);
        x = pbx_builtin_getvar_helper(chan, "LOCALHEADERINFO");
        if (x  &&  x[0])
            t30_set_header_info(&t38.t30_state, x);
        t30_set_rx_file(&t38.t30_state, target_file, -1);
        //t30_set_phase_b_handler(&t38.t30_state, phase_b_handler, chan);
        //t30_set_phase_d_handler(&t38.t30_state, phase_d_handler, chan);
        t30_set_phase_e_handler(&t38.t30_state, phase_e_handler, chan);

        /* Support for different image sizes && resolutions*/
        t30_set_supported_image_sizes(&fax.t30_state, T30_SUPPORT_US_LETTER_LENGTH | T30_SUPPORT_US_LEGAL_LENGTH | T30_SUPPORT_UNLIMITED_LENGTH
                                                        | T30_SUPPORT_215MM_WIDTH | T30_SUPPORT_255MM_WIDTH | T30_SUPPORT_303MM_WIDTH);
        t30_set_supported_resolutions(&fax.t30_state, T30_SUPPORT_STANDARD_RESOLUTION | T30_SUPPORT_FINE_RESOLUTION | T30_SUPPORT_SUPERFINE_RESOLUTION
                                                        | T30_SUPPORT_R8_RESOLUTION | T30_SUPPORT_R16_RESOLUTION);
        t30_set_supported_image_sizes(&t38.t30_state, T30_SUPPORT_US_LETTER_LENGTH | T30_SUPPORT_US_LEGAL_LENGTH | T30_SUPPORT_UNLIMITED_LENGTH
                                                        | T30_SUPPORT_215MM_WIDTH | T30_SUPPORT_255MM_WIDTH | T30_SUPPORT_303MM_WIDTH);
        t30_set_supported_resolutions(&t38.t30_state, T30_SUPPORT_STANDARD_RESOLUTION | T30_SUPPORT_FINE_RESOLUTION | T30_SUPPORT_SUPERFINE_RESOLUTION
                                                        | T30_SUPPORT_R8_RESOLUTION | T30_SUPPORT_R16_RESOLUTION);

        if (ecm)
        {
            t30_set_ecm_capability(&fax.t30_state, TRUE);
            t30_set_supported_compressions(&fax.t30_state, T30_SUPPORT_T4_1D_COMPRESSION | T30_SUPPORT_T4_2D_COMPRESSION | T30_SUPPORT_T6_COMPRESSION);
            t30_set_ecm_capability(&t38.t30_state, TRUE);
            t30_set_supported_compressions(&t38.t30_state, T30_SUPPORT_T4_1D_COMPRESSION | T30_SUPPORT_T4_2D_COMPRESSION | T30_SUPPORT_T6_COMPRESSION);
            opbx_log(LOG_DEBUG, "Enabling ECM mode for app_rxfax\n"  );
        }
        
        call_is_t38_mode = FALSE;
        passage = nowis();
        next = passage + 30000;
        rxpkt = 0;
        time(&begin);
        while ((res = opbx_waitfor(chan, 30)) > -1)
        {
            time(&thistime);
            if ((thistime - begin) >= 20  &&  (!rxpkt))
            {
                opbx_log(LOG_DEBUG, "No data received for %ld seconds. Hanging up.\n", (int) thistime - begin);
                break;
            }

            now = nowis();
            delay = (next < now)  ?  0  :  (next - now + 500)/1000;
            if ((res = opbx_waitfor(chan, delay)) < 0)
                break;
            // increment received packet count.
            rxpkt += res;
                
            if (!call_is_t38_mode)
            {
                if (chan->t38mode_enabled == 1)
                {
                    call_is_t38_mode = TRUE;
                    opbx_log(LOG_DEBUG, "T38 switchover detected\n" );
                }
            }

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
                next += 30000;
                continue;
            }
            if ((inf = opbx_read(chan)) == NULL)
            {
                res = -1;
                break;
            }

            time(&begin);
            if (inf->frametype == OPBX_FRAME_VOICE  &&  !call_is_t38_mode)
            {
                if (fax_rx(&fax, inf->data, inf->samples))
                    break;
                samples = (inf->samples <= MAX_BLOCK_SIZE)  ?  inf->samples  :  MAX_BLOCK_SIZE;
                if ((len = fax_tx(&fax, (int16_t *) &buf[OPBX_FRIENDLY_OFFSET], samples)))
                {
                    opbx_fr_init_ex(&outf, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, "RxFAX");
                    outf.datalen = len*sizeof(int16_t);
                    outf.samples = len;
                    outf.data = &buf[OPBX_FRIENDLY_OFFSET];
                    outf.offset = OPBX_FRIENDLY_OFFSET;
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
            else if (inf->frametype == 5)
            {
                // DTMF packet
            }
            else if (inf->frametype == OPBX_FRAME_VOICE && call_is_t38_mode)
            {
                // VOICE While in T38 mode packet
            }
            else if ( inf->frametype == OPBX_FRAME_NULL)
            {
                // NULL PACKET
            }
            else if (inf->frametype == 0  &&  !call_is_t38_mode)
            {
                // We received unknown frametype.
                // This happens when a T38 switchover has been performed and
                // we consider RTP frames as UDPTL. Let's switch to t38 mode.
                call_is_t38_mode = TRUE;
                passage = now;
            }
            else
            {
                if (verbose)
                    opbx_log(LOG_DEBUG," Unknown pkt received: frametype: %d subclass: %d t38_mode: %d\n",
                        inf->frametype, inf->subclass, call_is_t38_mode );
            }
            opbx_fr_free(inf);
        }
        if (inf == NULL)
        {
            opbx_log(LOG_DEBUG, "Got hangup\n");
            res = -1;
        }
        
        //opbx_log(LOG_WARNING, "Terminating fax\n");
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
    return opbx_register_application(app, rxfax_exec, synopsis, descrip);
}

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
