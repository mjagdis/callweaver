 
// $Id: cli.c,v 1.6 2005/12/21 21:34:50 stevek Exp $

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

#include <stdio.h>
#include "cli.h"

//
// debug functions
//

static char conference_debug_usage[] = 
	"usage: conference debug <conference_name> [ on | off ]\n"
	"       enable debugging for a conference\n"
;

static struct opbx_cli_entry cli_debug = { 
	{ "conference", "debug", NULL }, 
	conference_debug, 
	"enable debugging for a conference", 
	conference_debug_usage 
} ;


int conference_debug( int fd, int argc, char *argv[] )
{
	if ( argc < 3 ) 
		return RESULT_SHOWUSAGE ;

	// get the conference name
	const char* name = argv[2] ;
		
	// get the new state
	int state = 0 ;
	
	if ( argc == 3 )
	{
		// no state specified, so toggle it
		state = -1 ; 
	}
	else
	{
		if ( strncasecmp( name, "YES", 4 ) == 0 )
			state = 1 ;
		else if ( strncasecmp( name, "NO", 3 ) == 0 )
			state = 0 ;
		else
			return RESULT_SHOWUSAGE ;
	}
	
	int new_state = set_conference_debugging( name, state ) ;
	
	if ( new_state == 1 )
	{
		opbx_cli( fd, "enabled conference debugging, name => %s, new_state => %d\n", 
			name, new_state ) ;
	}
	else if ( new_state == 0 )
	{
		opbx_cli( fd, "disabled conference debugging, name => %s, new_state => %d\n", 
			name, new_state ) ;
	}
	else
	{
		// error setting state
		opbx_cli( fd, "\nunable to set debugging state, name => %s\n\n", name ) ;
	}
	
	return RESULT_SUCCESS ;
}

//
// stats functions
//

static char conference_show_stats_usage[] = 
	"usage: conference show stats\n"
	"       display stats for active conferences.\n"
;

static struct opbx_cli_entry cli_show_stats = { 
	{ "conference", "show", "stats", NULL }, 
	conference_show_stats, 
	"show conference stats", 
	conference_show_stats_usage 
} ;

int conference_show_stats( int fd, int argc, char *argv[] )
{
	if ( argc < 3 ) 
		return RESULT_SHOWUSAGE ;

	// get count of active conferences
	int count = get_conference_count() ;
	
	opbx_cli( fd, "\n\nCONFERENCE STATS, ACTIVE( %d )\n\n", count ) ;	

	// if zero, go no further
	if ( count <= 0 )
		return RESULT_SUCCESS ;

	//
	// get the conference stats
	//
	
	// array of stats structs
	opbx_conference_stats stats[ count ] ;

	// get stats structs
	count = get_conference_stats( stats, count ) ;

	// make sure we were able to fetch some
	if ( count <= 0 )
	{
		opbx_cli( fd, "!!! error fetching conference stats, available => %d !!!\n", count ) ;
		return RESULT_SUCCESS ;
	}

	//
	// output the conference stats 
	//

	// output header
	opbx_cli( fd, "%-20.20s  %-40.40s  %-40.40s\n", "Name", "Connection Type", "Member Type" ) ;
	opbx_cli( fd, "%-20.20s  %-40.40s  %-40.40s\n", "----", "---------------", "-----------" ) ;

	opbx_conference_stats* s = NULL ;

	char ct[64] ;
	char mt[64] ;
	
	int i;
	for ( i = 0 ; i < count ; ++i )
	{
		s = &(stats[i]) ;
	
		// format connection type
		snprintf( ct, 40, "phone( %d ), iax( %d ), sip( %d )", 
			s->phone, s->iaxclient, s->sip ) ;
		
		// format memeber type
		snprintf( mt, 40, "moderators( %d ), listeners( %d )",
			s->moderators, s->listeners ) ;
			
		// output this conferences stats
		opbx_cli( fd, "%-20.20s  %-40.40s  %-40.40s\n", (char*)( &(s->name) ), ct, mt ) ;
	}

	opbx_cli( fd, "\n" ) ;	

	//
	// drill down to specific stats
	//

	if ( argc == 4 )
	{
		// show stats for a particular conference
		conference_show_stats_name( fd, argv[3] ) ;
	}

	return RESULT_SUCCESS ;
}

int conference_show_stats_name( int fd, const char* name )
{
	// not implemented yet
	return RESULT_SUCCESS ;
}

//
// play sound
//

static char conference_play_sound_usage[] = 
	"usage: conference play sound <channel-id> <sound-file> [mute]\n"
	"       play sound <sound-file> to conference member <channel-id>.\n"
	"       mute the channel if 'mute' is present.\n"
;

static struct opbx_cli_entry cli_play_sound = { 
	{ "conference", "play", "sound", NULL }, 
	conference_play_sound, 
	"play a sound to a conference member", 
	conference_play_sound_usage 
} ;

int conference_play_sound( int fd, int argc, char *argv[] )
{
	char *channel, *file;
	int mute = 0;
	struct opbx_conf_member *member;
	struct opbx_conf_soundq *newsound;
	struct opbx_conf_soundq **q;

	if ( argc < 5 ) 
		return RESULT_SHOWUSAGE ;

	channel = argv[3];
	file = argv[4];

	if(argc > 5 && !strcmp(argv[5], "mute"))
	    mute = 1;
	

	member = find_member(channel, 1);
	if(!member) {
	    opbx_cli(fd, "Member %s not found\n", channel);
	    return RESULT_FAILURE;
	}

	newsound = calloc(1,sizeof(struct opbx_conf_soundq));
	newsound->stream = opbx_openstream(member->chan, file, NULL);
	if(!newsound->stream) { 
	    opbx_cli(fd, "Sound %s not found\n", file);
	    free(newsound);
	    opbx_mutex_unlock(&member->lock);
	    return RESULT_FAILURE;
	}
	member->chan->stream = NULL;
	
	newsound->muted = mute;	
	opbx_copy_string(newsound->name, file, sizeof(newsound->name));

	// append sound to the end of the list.
	for(q=&member->soundq; *q; q = &((*q)->next)) ;;

	*q = newsound;
	
	opbx_mutex_unlock(&member->lock);

	opbx_cli( fd, "Playing sound %s to member %s %s\n",
		      file, channel, mute ? "with mute" : "");	
	

	return RESULT_SUCCESS ;
}

//
// stop sounds
//

static char conference_stop_sounds_usage[] = 
	"usage: conference stop sounds <channel-id>\n"
	"       stop sounds for conference member <channel-id>.\n"
;

static struct opbx_cli_entry cli_stop_sounds = { 
	{ "conference", "stop", "sounds", NULL }, 
	conference_stop_sounds, 
	"stop sounds for a conference member", 
	conference_stop_sounds_usage 
} ;

int conference_stop_sounds( int fd, int argc, char *argv[] )
{
	char *channel;
	struct opbx_conf_member *member;
	struct opbx_conf_soundq *sound;
	struct opbx_conf_soundq *next;

	if ( argc < 4 ) 
		return RESULT_SHOWUSAGE ;

	channel = argv[3];

	member = find_member(channel, 1);
	if(!member) {
	    opbx_cli(fd, "Member %s not found\n", channel);
	    return RESULT_FAILURE;
	}



	// clear all sounds
	sound = member->soundq;
	member->soundq = NULL;

	while(sound) {
	    next = sound->next;
	    opbx_closestream(sound->stream);
	    free(sound);
	    sound = next;
	}

	// reset write format, since we're done playing the sound
	if ( opbx_set_write_format( member->chan, member->write_format ) < 0 ) 
	{
		opbx_log( LOG_ERROR, "unable to set write format to %d\n",
		    member->write_format ) ;
	}

	opbx_mutex_unlock(&member->lock);

	opbx_cli( fd, "Stopped sounds to member %s\n", channel);	
	

	return RESULT_SUCCESS ;
}


//
// cli initialization function
//

void register_conference_cli( void ) 
{
	opbx_cli_register( &cli_debug ) ;
	opbx_cli_register( &cli_show_stats ) ;
	opbx_cli_register( &cli_play_sound ) ;
	opbx_cli_register( &cli_stop_sounds ) ;
}

void unregister_conference_cli( void )
{
	opbx_cli_unregister( &cli_debug ) ;
	opbx_cli_unregister( &cli_show_stats ) ;
	opbx_cli_unregister( &cli_play_sound ) ;
	opbx_cli_unregister( &cli_stop_sounds ) ;
}
