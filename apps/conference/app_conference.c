 
// $Id: app_conference.c,v 0.4 2005/10/26 21:02:07 stevek Exp $

/*
 * app_conference
 *
 * A channel independent conference application for Asterisk
 *
 * Copyright (C) 2002, 2003 Junghanns.NET GmbH
 * Copyright (C) 2003, 2004 HorizonLive.com, Inc.
 *
 * Klaus-Peter Junghanns <kapejod@ns1.jnetdns.de>
 *
 * This program may be modified and distributed under the 
 * terms of the GNU Public License.
 *
 */

#ifdef HAVE_CONFIG_H 
#include "confdefs.h" 
#endif 
#include <stdio.h>
#include "app_conference.h"
#include "common.h"

/*

a conference has n + 1 threads, where n is the number of 
members and 1 is a conference thread which sends audio
back to the members. 

each member thread reads frames from the channel and
add's them to the member's frame queue. 

the conference thread reads frames from each speaking members
queue, mixes them, and then re-queues them for the member thread
to send back to the user.

*/

//
// app_conference text descriptions
//

static char *tdesc = "Channel Independent Conference Application" ;
static char *app = "Conference" ;
static char *synopsis = "Channel Independent Conference" ;
static char *descrip = 
"Usage: Conference(ConferenceName/Flags/Priority[/VADSTART/VADCONTINUE])\n"
"\n"
"    * ConferenceName: Whatever you want to name the conference\n"
"    * Flags one of more of the following:\n"
"          o M: Moderator (presently same as speaker)\n"
"          o S: Speaker\n"
"          o L: Listener\n"
"          o T: \"Telephone caller\" (just for stats?).\n"
"          o V: Do VAD on this caller\n"
"          o D: Use Denoise filter on this caller.\n"
"          o d: Send manager events when DTMF is received.\n"
"    * Priority: Currently ignored; was to be a \"speaking priority\" so a\n"
"        higher priority caller could \"override\" others.\n"
"    * VADSTART: Optional: \"probability\" to use to detect start of speech.\n"
"    * VADCONTINUE: Optional: \"probability\" to use to detect continuation\n"
"        of speech.\n"
"\n"
"returns 0 if the user exits with the '#' key, or -1 if the user hangs up.\n" ;

//
// functions defined in asterisk/module.h
//

STANDARD_LOCAL_USER ;
LOCAL_USER_DECL;

int unload_module( void )
{
	opbx_log( LOG_NOTICE, "unloading app_conference module\n" ) ;

	STANDARD_HANGUP_LOCALUSERS ; // defined in asterisk/module.h

	// register conference cli functions
	unregister_conference_cli() ;

	return opbx_unregister_application( app ) ;
}

int load_module( void )
{
	opbx_log( LOG_NOTICE, "loading app_conference module [ $Revision: 1.22 $ ]\n" ) ;

	// intialize conference
	init_conference() ;

	// register conference cli functions
	register_conference_cli() ;

	return opbx_register_application( app, app_conference_main, synopsis, descrip ) ;
}

char *description( void )
{
	return tdesc ;
}

int usecount( void )
{
	int res;
	STANDARD_USECOUNT( res ) ; // defined in asterisk/module.h
	return res;
}


//
// main app_conference function
//

int app_conference_main( struct opbx_channel* chan, void* data ) 
{
	int res = 0 ;
	struct localuser *u ;
	
	// defined in asterisk/module.h
	LOCAL_USER_ADD( u ) ; 

	// call member thread function
	res = member_exec( chan, data ) ;

	// defined in asterisk/module.h
	LOCAL_USER_REMOVE( u ) ;
	
	return res ;
}

//
// utility functions
//

// now returns milliseconds
long usecdiff( struct timeval* timeA, struct timeval* timeB )
{
	long a_secs = timeA->tv_sec - timeB->tv_sec ;
	long b_secs = (long)( timeA->tv_usec / 1000 ) - (long)( timeB->tv_usec / 1000 ) ;
	long u_secs = ( a_secs * 1000 ) + b_secs ;
	return u_secs ;
}

// increment a timeval by ms milliseconds
void add_milliseconds( struct timeval* tv, long ms )
{
	// add the microseconds to the microseconds field
	tv->tv_usec += ( ms * 1000 ) ;

	// calculate the number of seconds to increment
	long s = ( tv->tv_usec / 1000000 ) ;

	// adjust the microsends field
	if ( s > 0 ) tv->tv_usec -= ( s * 1000000 ) ;

	// increment the seconds field
	tv->tv_sec += s ;

	return ;
}
