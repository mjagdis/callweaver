
// $Id: member.h,v 1.7 2005/11/02 22:42:42 stevek Exp $

/*
 * app_conference
 *
 * A channel independent conference application for Asterisk
 *
 * Copyright (C) 2002, 2003 Junghanns.NET GmbH
 * Copyright (C) 2003-2005  HorizonLive.com, Inc.
 *
 * Klaus-Peter Junghanns <kapejod@ns1.jnetdns.de>
 * Steve Kann <stevek@stevek.com>
 *
 * This program may be modified and distributed under the 
 * terms of the GNU Public License.
 *
 */

#ifndef _APP_CONF_MEMBER_H
#define _APP_CONF_MEMBER_H

//
// includes
//

#include "app_conference.h"
#include "common.h"


//
// struct declarations
//

struct opbx_conf_soundq 
{
	char name[256];
	struct opbx_filestream *stream; // the stream
	int muted; // should incoming audio be muted while we play?
	struct opbx_conf_soundq *next;
};

struct opbx_conf_member 
{
	opbx_mutex_t lock ; // member data mutex
	
	struct opbx_channel* chan ; // member's channel  
	char* channel_name ; // member's channel name

	// values passed to create_member () via *data
	int priority ;	// highest priority gets the channel
	char* flags ;	// raw member-type flags
	char type ;		// L = ListenOnly, M = Moderator, S = Standard (Listen/Talk)
	char* id ;		// member id

	// determine by flags and channel name	
	char connection_type ; // T = telephone, X = iaxclient, S = sip
	
	// vad voice probability thresholds
	float vad_prob_start ;
	float vad_prob_continue ;
	
	// ready flag
	short ready_for_outgoing ;
	
	// input frame queue
	conf_frame* inFrames ;
	conf_frame* inFramesTail ;	
	unsigned int inFramesCount ;

	// input/output smoother
	struct opbx_smoother *inSmoother;
	struct opbx_packer *outPacker;
	int smooth_size_in;
	int smooth_size_out;
	int smooth_multiple;

	// frames needed by conference_exec
	unsigned int inFramesNeeded ;

	// used when caching last frame
	conf_frame* inFramesLast ;
	unsigned int inFramesRepeatLast ;
	unsigned short okayToCacheLast ;
		
	// output frame queue
	conf_frame* outFrames ;
	conf_frame* outFramesTail ;	
	unsigned int outFramesCount ;
		
	// time we last dropped a frame
	struct timeval last_in_dropped ;
	struct timeval last_out_dropped ;
	
	// ( not currently used )
	// int samplesperframe ; 
	
	// used for determining need to mix frames
	// and for management interface notification
	short speaking_state_prev ;
	short speaking_state_notify ;
	short speaking_state ;
	
	// pointer to next member in single-linked list	
	struct opbx_conf_member* next ;
	
	// accounting values
	unsigned long frames_in ; 
	unsigned long frames_in_dropped ;
	unsigned long frames_out ;
	unsigned long frames_out_dropped ;

	// for counting sequentially dropped frames
	unsigned int sequential_drops ;
	unsigned long since_dropped ;

	// start time
	struct timeval time_entered ;
	struct timeval lastsent_timeval ;
		
	// flag indicating we should remove this member
	short remove_flag ;

#if ( SILDET == 2 )
	// pointer to speex preprocessor dsp
	SpeexPreprocessState *dsp ;
	// number of frames to ignore speex_preprocess()
	int ignore_speex_count;
#else
	// placeholder when preprocessing is not enabled
	void* dsp ;
#endif

	// audio format this member is using
	int write_format ;
	int read_format ;

	int write_format_index ;
	int read_format_index ;
	
	// member frame translators
	struct opbx_trans_pvt* to_slinear ;
	struct opbx_trans_pvt* from_slinear ;

	// For playing sounds
	struct opbx_conf_soundq *soundq;

#ifdef DEBUG_OUTPUT_PCM
	FILE *incoming_fh;
#endif
	
	int send_dtmf:1;
} ;

struct conf_member 
{
	struct opbx_conf_member* realmember ;
	struct conf_member* next ;
} ;

//
// function declarations
//

int member_exec( struct opbx_channel* chan, void* data ) ;

struct opbx_conf_member* create_member( struct opbx_channel* chan, const char* data ) ;
struct opbx_conf_member* delete_member( struct opbx_conf_member* member ) ;

// incoming queue
int queue_incoming_frame( struct opbx_conf_member* member, struct opbx_frame* fr ) ;
conf_frame* get_incoming_frame( struct opbx_conf_member* member ) ;

// outgoing queue
int queue_outgoing_frame( struct opbx_conf_member* member, const struct opbx_frame* fr, struct timeval delivery ) ;
int __queue_outgoing_frame( struct opbx_conf_member* member, const struct opbx_frame* fr, struct timeval delivery ) ;
conf_frame* get_outgoing_frame( struct opbx_conf_member* member ) ;

void send_state_change_notifications( struct opbx_conf_member* member ) ;

//
// meta-info accessors functions
//

short memberIsPhoneClient( struct opbx_conf_member* member ) ;
short memberIsIaxClient( struct opbx_conf_member* member ) ;
short memberIsSIPClient( struct opbx_conf_member* member ) ;

short memberIsModerator( struct opbx_conf_member* member ) ;
short memberIsListener( struct opbx_conf_member* member ) ;

//
// packer functions
// 

struct opbx_packer;

extern struct opbx_packer *opbx_packer_new(int bytes);
extern void opbx_packer_set_flags(struct opbx_packer *packer, int flags);
extern int opbx_packer_get_flags(struct opbx_packer *packer);
extern void opbx_packer_free(struct opbx_packer *s);
extern void opbx_packer_reset(struct opbx_packer *s, int bytes);
extern int opbx_packer_feed(struct opbx_packer *s, const struct opbx_frame *f);
extern struct opbx_frame *opbx_packer_read(struct opbx_packer *s);

#endif
