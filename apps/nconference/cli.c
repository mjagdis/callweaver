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
#include "conference.h"
#include "member.h"
#include "frame.h"
#include "sounds.h"
#include "cli.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")


/* ************************************************************************
       RELATED APPLICATIONS - NCONFERENCEADMIN - TO BE FINISHED, IF NEEDED
 * ************************************************************************/
/*

static void *admin_app;
static char *admin_name = "NConferenceAdmin";
static char *admin_synopsis= "NConference participant count";
static char *admin_syntax = "NConference(confno[, var])";
static char *admin_description =
"Plays back the number of users in the specified\n"
"conference. If var is specified, playback will be skipped and the value\n"
"will be returned in the variable. Returns 0.\n";

// TODO - Do it if someone needs it

static int app_admin_exec(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	struct localuser *u;
	struct cw_conference *conf;

	if (argc < 2 || argc > 3 || !argv[0][0] || !argv[1][0])
		return cw_function_syntax(admin_syntax);

	// Find the right conference 
	if (!(conf = find_conf(argv[0]))) {
		return -1;
	}

//	cw_mutex_unlock(&conflock);

	LOCAL_USER_ADD(u);
	LOCAL_USER_REMOVE(u);
	
}
*/

/* ***************************************************************
       RELATED APPLICATIONS - NCONFERENCECOUNT
 * ***************************************************************/


static void *count_app;
static char count_name[] = "NConferenceCount";
static char count_synopsis[] = "NConference members count";
static char count_syntax[] = "NConferenceCount(confno[, var])";
static char count_description[] =
"Plays back the number of users in the specified\n"
"conference to the conference members. \n"
"If used in an expression context, e.g. Set(var=${NconferenceCount(confnno)})\n"
"playback will be skipped and the value returned\n"
"If var is specified, playback will be skipped and the value\n"
"will be returned in the variable. Returns 0.\n";


static int app_count_exec(struct cw_channel *chan, int argc, char **argv, char *buf, size_t len)
{
	struct localuser *u;

	int res = 0;
	struct cw_conference *conf;
	int count;
	char val[80] = "0"; 

	if (argc < 1 || argc > 2 || !argv[0][0])
		return cw_function_syntax(count_syntax);

	LOCAL_USER_ADD(u);

	conf = find_conf(argv[0]);

	if (conf) {
		cw_mutex_lock(&conf->lock);
		count = conf->membercount;
		cw_mutex_unlock(&conf->lock);
	}
	else
		count = 0;

	if (buf) {
		snprintf(buf, len, "%i", count);
	} else if (argc > 1 && argv[1][0]) {
		snprintf(val, sizeof(val), "%i", count);
		pbx_builtin_setvar_helper(chan, argv[1], val);
	} else if (conf != NULL) {
		char tmp[10];
		snprintf(tmp, sizeof(tmp), "%d", count);
	        add_command_to_queue( conf, NULL, CONF_ACTION_QUEUE_SOUND,  0, "conf-thereare" );
	        add_command_to_queue( conf, NULL, CONF_ACTION_QUEUE_NUMBER, 0, tmp );
	        add_command_to_queue( conf, NULL, CONF_ACTION_QUEUE_SOUND,  0, "conf-peopleinconf" );
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

/* ***************************************************************
       CLI FUNCTIONS
 * ***************************************************************/


int nconference_admin_exec( struct cw_dynstr **ds_p, int argc, char *argv[] );
static void nconference_admin_complete(struct cw_dynstr **ds_p, char *argv[], int lastarg, int lastarg_len);

static const char nconference_admin_usage[] =
	"usage: NConference <command>  <conference_name> <usernumber>\n"
	"       Admin commands for conference\n"
	"       <command> can be: kick, list, lock, mute, show, unlock, unmute\n"
;

static struct cw_clicmd nconference_admin_cli = { 
	.cmda = { "NConference", NULL, NULL },
	.handler = nconference_admin_exec,
	.summary = "Administration Tool for NConference",
	.usage = nconference_admin_usage,
	.generator = nconference_admin_complete,
} ;

int nconference_admin_exec( struct cw_dynstr **ds_p, int argc, char *argv[] )
{
	struct cw_conference *conf 	= NULL;
	struct cw_conf_member *member	= NULL;
	char cmdline [512];
	int i = 0;
	int total = 0;

	if ( argc < 2 ) 
		return RESULT_SHOWUSAGE ;

	if (argc > 4)
		cw_dynstr_printf(ds_p, "Invalid Arguments.\n");

	// Check for length so no buffer will overflow... 
	for (i = 0; i < argc; i++) {
		if (strlen(argv[i]) > 100)
			cw_dynstr_printf(ds_p, "Invalid Arguments.\n");
	}

	if (argc == 2 && strstr(argv[1], "show") ) {
		// Show all the conferences 
		conf = conflist;
		if (!conf) {
			cw_dynstr_printf(ds_p, "No active conferences.\n");
			return RESULT_SUCCESS;
		}
		cw_dynstr_printf(ds_p, " %-s    %7s\n", "Conf. Num", "Members");
		while(conf) {
			if (conf->membercount == 0)
				cw_copy_string(cmdline, "N/A ", sizeof(cmdline) );
			else 
				snprintf(cmdline, sizeof(cmdline), "%4d", conf->membercount);
			cw_dynstr_printf(ds_p, " %-9s    %7d\n", conf->name, conf->membercount );
			total += conf->membercount; 	
			conf = conf->next;
		}
		cw_dynstr_printf(ds_p, "*Total number of members: %d\n", total);
		return RESULT_SUCCESS;
	}

	// The other commands require more arguments
	if (argc < 3)
		return RESULT_SHOWUSAGE;


	/* Find the right conference */
	if (!(conf = find_conf(argv[2]))) {
		cw_dynstr_printf(ds_p, "No such conference: %s.\n", argv[2]);
			return RESULT_SUCCESS;
	}

	// Find the right member
	if (argc > 3 ) {
	    member = find_member(conf,argv[3] );
	    if ( strcmp( argv[3],"all" ) && ( member == NULL ) ) {
		cw_dynstr_printf(ds_p, "No such member: %s in conference %s.\n", argv[3], argv[2]);
			return RESULT_SUCCESS;
	    }
	}



	if      ( !strcmp(argv[1], "list") ) {
		member = conf->memberlist;
		total = 0;
		cw_dynstr_printf(ds_p, " %-14s  %-14s  %9s %6s %3s\n", "Channel", "Type","Speaking","Muted","VAD");
		while ( member != NULL )
		{
		    cw_dynstr_printf(ds_p, " %-14s  %-14s  %9d %6d %3d\n",
			member->chan->name,
			membertypetostring( member->type ),
			member->is_speaking,
			member->talk_mute,
			member->enable_vad
		    );
		    total ++;
		    member = member->next ;
		}
		cw_dynstr_printf(ds_p, "*Total members: %d \n", total );
	}
	else if ( !strcmp(argv[1], "unlock") ) {
	    if ( conf->is_locked == 0 )
		cw_dynstr_printf(ds_p, "Conference: %s is already unlocked.\n", conf->name);
 	    else { 
		conf->is_locked = 0;
	        add_command_to_queue( conf, NULL, CONF_ACTION_QUEUE_SOUND, 0, "conf-unlockednow" );
	    }
	}
	else if ( !strcmp(argv[1], "lock") ) {
	    if ( conf->is_locked == 1 )
		cw_dynstr_printf(ds_p, "Conference: %s is already locked.\n", conf->name);
 	    else { 
		conf->is_locked = 1;
	        add_command_to_queue( conf, NULL, CONF_ACTION_QUEUE_SOUND, 0, "conf-lockednow" );
	    }
	}


	else if ( !strcmp(argv[1], "mute") ) {
	    if ( member == NULL ) {
	    	    add_command_to_queue( conf, NULL, CONF_ACTION_MUTE_ALL, 1, "" );
	    } 
	    else 
	    {
		member->talk_mute = 1;
		conference_queue_sound( member, "conf-muted" );
		cw_dynstr_printf(ds_p, "Conference: %s - Member %s is now muted.\n", conf->name, member->chan->name);
	    }
	}
	else if ( !strcmp(argv[1], "unmute") ) {
	    if ( member == NULL ) {
	    	    add_command_to_queue( conf, NULL, CONF_ACTION_MUTE_ALL, 0, "" );
	    }
	    else 
	    {
		member->talk_mute = 0;
		conference_queue_sound( member, "conf-unmuted" );
		cw_dynstr_printf(ds_p, "Conference: %s - Member %s is now unmuted.\n", conf->name, member->chan->name);
	    }
	}

	else if ( !strcmp(argv[1], "kick") ) {
	    if ( member == NULL ) {
		    cw_dynstr_printf(ds_p, "Conference: %s - Member is not correct.\n", conf->name);
	    }
	    else 
	    {
		queue_incoming_silent_frame(member,5);
		conference_queue_sound( member, "conf-kicked" );
		member->force_remove_flag = 1;
		cw_dynstr_printf(ds_p, "Conference: %s - Member %s has been kicked.\n", conf->name, member->chan->name);
	    }
	}


    return RESULT_SUCCESS;
}




static void nconference_admin_complete(struct cw_dynstr **ds_p, char *argv[], int lastarg, int lastarg_len) {
	static const char *cmds[] 	= {"lock", "unlock", "mute", "unmute", "kick", "list", "show"};
	int x = 0;
	struct cw_conference *cnf 	= NULL;
	struct cw_conf_member *usr 	= NULL;

	if (lastarg == 1) {
		/* Command */
		for (x = 0; x < arraysize(cmds); x++) {
			if (!strncasecmp(cmds[x], argv[1], lastarg_len))
				cw_dynstr_printf(ds_p, "%s\n", cmds[x]);
		}
	} 
	else if (lastarg == 2) {
		// Conference Number 
		cw_mutex_lock(&conflist_lock);
		for (cnf = conflist; cnf; cnf = cnf->next) {
			if (!strncasecmp(argv[2], cnf->name, lastarg_len))
				cw_dynstr_printf(ds_p, "%s\n", cnf->name);
		}
		cw_mutex_unlock(&conflist_lock);
	} 
	else if (lastarg == 3) {
		// User Number || Conf Command option
		if ( !strcmp(argv[1], "mute") || !strcmp(argv[1], "kick") || !strcmp(argv[1], "lock") ) {
			if ( !(strncasecmp(argv[3], "all", lastarg_len)))
				cw_dynstr_printf(ds_p, "all\n");

			cw_mutex_lock(&conflist_lock);

			for (cnf = conflist; cnf; cnf = cnf->next) {
				if (!strcmp(argv[2], cnf->name)) {
					// Search for the user 
					for (usr = cnf->memberlist; usr; usr = usr->next) {
						if (!strncasecmp(argv[3], usr->chan->name, lastarg_len))
							cw_dynstr_printf(ds_p, "%s\n", usr->chan->name);
					}
					break;
				}
			}

			cw_mutex_unlock(&conflist_lock);
		}
	}
}

/* ***************************************************************
       cli initialization function
 * ***************************************************************/

void register_conference_cli( void ) 
{
	cw_cli_register( &nconference_admin_cli ) ;
	count_app = cw_register_function(count_name, app_count_exec, count_synopsis, count_syntax, count_description);
	//admin_app = cw_register_function(admin_name, app_admin_exec, admin_synopsis, admin_syntax, admin_description);
}

void unregister_conference_cli( void )
{
	cw_cli_unregister( &nconference_admin_cli ) ;
	cw_unregister_function(count_app);
	//cw_unregister_function(app_admin);
}


