/*
 * app_nconference
 *
 * NConference
 * A channel independent conference application for CallWeaver
 *
 * Copyright (C) 2002, 2003 Navynet SRL
 * http://www.navynet.it
 *
 * Massimo "CtRiX" Cetra - ctrix (at) navynet.it
 *
 * This program may be modified and distributed under the 
 * terms of the GNU Public License V2.
 *
 */
#include <stdio.h>
#include "common.h"
#include "app_nconference.h"
#include "conference.h"
#include "member.h"
#include "cli.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")


/************************************************************
 *        Text Descriptions
 ***********************************************************/

static char tdesc[] = "Navynet Channel Independent Conference Application" ;

static void *conference_app;
static const char conference_name[] = APP_CONFERENCE_NAME ;
static const char conference_synopsis[] = "Navynet Channel Independent Conference" ;
static const char conference_syntax[] = APP_CONFERENCE_NAME "(confno/options/pin)";
static const char conference_description[] =
"The options string may contain zero or more of the following:\n"
"   'M': Caller is Moderator (can do everything).\n"
"   'S': Caller is Speaker.\n"
"   'L': Caller is Listener (cannot talk).\n"
"   'T': Caller is Talker (cannot listen but can only speak).\n"
"   'C': Caller is Consultant (can talk only to moderator).\n"
#if ENABLE_VAD
"   'V': Do VAD (Voice Activity Detection).\n"
#endif
"   'd': Disable DTMF handling (keypad functions).\n"
"   'x': Don't auto destroy conference when all users quit.\n"
"        (it's destroyed after 300 seconds of inactivity)\n"
"	 Note: works only with M option set.\n"
"   'q': Quiet mode. Don't play enter sounds when entering.\n"
"   'b': Beep mode. Only beep to announce if a member join/left.\n"
"   'm': Disable music on hold while the conference has a single caller.\n"
"\n"
" If 'pin' is set, then if the member is a Moderator that pin is inherited \n"
"by the conference (otherwise pin is empty), if the member is not a Moderator \n"
"and the conference is locked, that pin is used to gain access to the conference.\n"
"\n"
"Please note that the options parameter list delimiter is '/'\n"
"Returns 0 if the user exits with the '#' key, or -1 if the user hangs up.\n" ;


static int unload_module( void ) {
	int res = 0;
	unregister_conference_cli();
	res |= cw_unregister_function( conference_app ) ;
	return res;
}

static int load_module( void ) {
	init_conference() ;
	register_conference_cli();
	conference_app = cw_register_function( conference_name, app_conference_main, conference_synopsis, conference_syntax, conference_description ) ;
	return 0;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)


/************************************************************
 *        Main Conference function
 ***********************************************************/

int app_conference_main( struct cw_channel* chan, int argc, char **argv, struct cw_dynstr *result) {
	struct localuser *u ;
	int res = 0 ;

	CW_UNUSED(result);

	LOCAL_USER_ADD( u ) ; 
	res = member_exec( chan, argc, argv ) ;
	LOCAL_USER_REMOVE( u ) ;	
	return res ;
}
