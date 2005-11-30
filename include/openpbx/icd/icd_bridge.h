/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#ifndef ICD_BRIDGE_H
#define ICD_BRIDGE_H

int icd_bridge_wait_ack(icd_caller * that);
int icd_bridge_call(icd_caller *bridger, icd_caller *bridgee);

struct opbx_channel *icd_request_and_dial(char *type, int format, void *data, int timeout, int *outstate,
    char *callerid, icd_caller * caller, icd_caller * peer, icd_caller_state req_state);

/* This is the function icd_caller calls to translate strings into an openpbx channel */
struct opbx_channel *icd_bridge_get_openpbx_channel(char *chanstring, char *context, char *priority,
    char *extension);

/* This is the function that icd_caller calls to dial out, typically for onhook agents */
int icd_bridge_dial_openpbx_channel(icd_caller * caller, char *chanstring, int timeout);

/* check the hangup status of the caller's chan (frontended for future cross compatibility) */
int icd_bridge__check_hangup(icd_caller * that);

void icd_bridge__safe_hangup(icd_caller * caller);

/* lift from * when we need to sleep a live up channel for wrapup */
int icd_safe_sleep(struct opbx_channel *chan, int ms);

int icd_bridge__wait_call_customer(icd_caller * that);
int icd_bridge__wait_call_agent(icd_caller * that);
void icd_bridge__unbridge_caller(icd_caller * caller, icd_unbridge_flag ubf);
void icd_bridge__parse_ubf(icd_caller * caller, icd_unbridge_flag ubf);
void icd_bridge__remasq(icd_caller * caller);

int icd_bridge__play_sound_file(struct opbx_channel *chan, char *file);
#endif

/* For Emacs:
 * Local Variables:
 * indent-tabs-mode:nil
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 expandtab:
 */

