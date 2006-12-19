/*
 * app_nconference
 *
 * NConference
 * A channel independent conference application for Asterisk
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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"  
#endif  
#include <stdio.h> 
#include "common.h"
#include "conference.h"
#include "member.h"
#include "frame.h"

int vdebug = 0;

/* 
   Map 'volume' levels from -5 through +5 into
   decibel (dB) settings for channel drivers
   Note: these are not a straight linear-to-dB
   conversion... the numbers have been modified
   to give the user a better level of adjustability
*/

static signed char gain_map[] = {
	-9, -6, -4, -2, 
	0,
	2,     4, 6, 9
};

/******************************************************************************
      Ring buffer functions
 ******************************************************************************/

void mix_slinear_frames( short *dst, const short *src, int samples, int startindex, int cbuffersize ) 
{
	if ( dst == NULL ) return ;
	if ( src == NULL ) return ;

	int i=0;
	int i_dst = 0;
	int i_src = 0;
	short val = 0, v1,v2;


#if  ( APP_NCONFERENCE_DEBUG == 1 )
	if (vdebug) 
	    opbx_log(OPBX_CONF_DEBUG, 
		"STARTING TO MIX: " 
		"SRC at %p   DST at %p   BUFFERSIZE: %d Samples %d  STARTINDEX: %d\n",	
		src, 		  dst,		cbuffersize,  samples,         startindex
	    );
#endif

	for ( i = 0 ; i < samples ; ++i ) 
	{
		i_dst = i;
		i_src = (startindex - samples + i_dst) % cbuffersize;
		if ( i_src < 0 ) i_src = i_src + cbuffersize; 

		v1  = dst[i_dst];
		v2  = src[i_src];

		val= v1;
		opbx_slinear_saturated_add(&val,&v2) ;
		dst[i_dst] = val ;

#if  ( APP_NCONFERENCE_DEBUG == 1 )
	    if ( vdebug ) 
	    opbx_log(OPBX_CONF_DEBUG,
		"start %d - DST(%3d) %08d (at %p)   SRC(%3d) %08d (at %p) VAL: %08d \n", 
		startindex, i_dst,  v1 ,&dst[i_dst],i_src,  v2,  &src[i_src], val);
#endif
	}

	return ;
}

struct opbx_frame* get_outgoing_frame( struct opbx_conference *conf, struct opbx_conf_member* member, int samples ) 
{
    //
    // sanity checks
    //
	
    // check on conf
    if ( conf == NULL ) 
    {
    	opbx_log( LOG_ERROR, "unable to queue null conference\n" ) ;
	return NULL ;
    }

    if ( conf->memberlist == NULL ) 
    {
    	opbx_log( LOG_ERROR, "unable to queue for a null memberlist\n" ) ;
	return NULL ;
    }

    // check on member
    if ( member == NULL )
    {
    	opbx_log( LOG_ERROR, "unable to queue frame for null member\n" ) ;
    	return NULL ;
    }

    memset(member->framedata,0,sizeof(member->framedata));

    //***********************************
    // Mixing procedure
    //***********************************

    int members =0;
    struct opbx_frame *f = NULL;
    struct opbx_conf_member *mixmember;
    short *dst = member->framedata;
    short *src = NULL;

    memset(member->framedata,0,sizeof(member->framedata));

    mixmember=conf->memberlist;
    while ( mixmember ) {
	if (
	    mixmember!=member && mixmember->is_speaking
	    &&
	    ( mixmember->type != MEMBERTYPE_CONSULTANT || ( mixmember->type == MEMBERTYPE_CONSULTANT && member->type == MEMBERTYPE_MASTER ) ) 
	   ) {
	    members++;

    	    src = mixmember->cbuf->buffer8k;
#if  ( APP_NCONFERENCE_DEBUG == 1 )
	    if(vdebug) 
	    opbx_log(OPBX_CONF_DEBUG,
		"Mixing memb %s Chan %s Ind %d Samples %d "
		" bufpos: %p with offset %p  FOR MEMBER %s\n", 
		mixmember->id, mixmember->chan->name, mixmember->cbuf->index8k, samples,
		member->framedata, member->framedata + OPBX_FRIENDLY_OFFSET/sizeof(short),
		member->chan->name
	    );
#endif
	    mix_slinear_frames( 
		    dst, src,
		    samples, mixmember->cbuf->index8k, sizeof(mixmember->cbuf->buffer8k)/sizeof(short) 
	    );
	}
	mixmember=mixmember->next;
    }

    //Building the frame
    f = calloc(1, sizeof(struct opbx_frame));
    if (f != NULL) {
        opbx_fr_init_ex(f, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, "Nconf");
	f->data = member->framedata;
	f->datalen = samples*sizeof(short);
	f->samples = samples;
	f->offset = 0;
    } else
	return NULL;

#if  ( APP_NCONFERENCE_DEBUG == 1 )
    if (vdebug && members) {
        int count=0;
        short *msrc;
	msrc = f->data + OPBX_FRIENDLY_OFFSET;

	if (vdebug) opbx_log(OPBX_CONF_DEBUG,
			"Offset %d  OFO: %d    Data at %p  SRC at %p memberdata at %p \n",
			f->offset, OPBX_FRIENDLY_OFFSET,
			f->data, &msrc, member->framedata 
		    );

	for( count=0; count<f->samples; count++ ) {
    	    opbx_log(OPBX_CONF_DEBUG,
		"DUMP POS %04d VALUE %08d    at %p \n",
		          count, msrc[count], &msrc[count] );
	}
    }
#endif
    return f ;
}


void copy_frame_content( struct member_cbuffer *cbuf, struct opbx_frame *sfr ) 
{
    int count=0;

    short *src;
    short *dst;
    int i_src = 0;
    int i_dst = 0;

    src = sfr->data+sfr->offset;
    src = sfr->data;
    dst = cbuf->buffer8k;
    
    for( count=0; count<sfr->samples; count++ ) {
        i_src = count;
        i_dst = (cbuf->index8k+count) % OPBX_CONF_CBUFFER_8K_SIZE;

#if  ( APP_NCONFERENCE_DEBUG == 1 )
        if (vdebug) opbx_log(OPBX_CONF_DEBUG,
	    "(%d)POSITION from %04d   (at %p)   to %04d (at %p)     VALUE %08d \n",
	    count,          i_src,&(dst[i_dst]), i_dst,&(src[i_src]), src[i_src] );
#endif

	dst[i_dst] = src[i_src];
    }

    cbuf->index8k=( i_dst + 1 ) % OPBX_CONF_CBUFFER_8K_SIZE;
#if  ( APP_NCONFERENCE_DEBUG == 1 )
    if (vdebug) opbx_log(OPBX_CONF_DEBUG,"Set index to %d \n", cbuf->index8k);
#endif
}



int queue_incoming_frame( struct opbx_conf_member *member, struct opbx_frame *fr ) 
{
    //
    // sanity checks
    //
	
    // check on frame
    if ( fr == NULL ) 
    {
    	opbx_log( LOG_ERROR, "unable to queue null frame\n" ) ;
    	return -1 ;
    }
	
    // check on member
    if ( member == NULL )
    {
    	opbx_log( LOG_ERROR, "unable to queue frame for null member\n" ) ;
    	return -1 ;
    }

    // check on circular buffer
    if ( member->cbuf == NULL )
    {
    	opbx_log( LOG_ERROR, "unable to queue frame for null circular buffer\n" ) ;
    	return -1 ;
    }

    //
    // Do the queing and add the frames to the circular buffer
    //

    int res = 0;
    struct opbx_frame *sfr;

//    copy_frame_content(member->cbuf, fr); return 0; // This code is to bypass the Smoother on the input frames

    // Feed the smoother if exists
    if ( member->inSmoother != NULL )
	res = opbx_smoother_feed( member->inSmoother, fr );

    if ( !res && member->inSmoother ) {
	while ( ( sfr = opbx_smoother_read( member->inSmoother ) ) ) {
	    copy_frame_content(member->cbuf, sfr);
	    opbx_fr_free(sfr);
	}
	opbx_smoother_reset(member->inSmoother, member->smooth_size_in);
    } 
    else {
	copy_frame_content(member->cbuf, fr);
    }
/**/
    return 0 ;
}


int queue_incoming_silent_frame( struct opbx_conf_member *member) {
    struct opbx_frame f;
    int t = 0;

    memset(member->framedata,0,sizeof(member->framedata));

    opbx_fr_init_ex(&f, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, "Nconf");
    f.data = member->framedata;
    f.datalen = member->samples * sizeof(short);
    f.samples = member->samples;
    f.offset = 0;

    // Actually queue some frames
    for (t = 0; t < 5; t++ )
	queue_incoming_frame(member,&f);

    return 0;
}



/******************************************************************************
      utility functions
 ******************************************************************************/

// now returns milliseconds
long usecdiff( struct timeval* timeA, struct timeval* timeB )
{
    long a_secs = timeA->tv_sec - timeB->tv_sec ;
    long b_secs = (long)( timeA->tv_usec / 1000 ) - (long)( timeB->tv_usec / 1000 ) ;
    long u_secs = ( a_secs * 1000 ) + b_secs ;
    return u_secs ;
}

int set_talk_volume(struct opbx_conf_member *member, struct opbx_frame *f, int is_talk)
{
    int ret=0;
    signed char gain_adjust;
    int volume = member->talk_volume;

    gain_adjust = gain_map[volume + 5];

    if (is_talk) {
	/* 
    	 *   attempt to make the adjustment in the channel driver first
	 */
	if ( !member->talk_volume_adjust) {
	    ret=opbx_channel_setoption(member->chan, OPBX_OPTION_RXGAIN, &gain_adjust, sizeof(gain_adjust), 0);
	    if (ret)
		member->talk_volume_adjust=1;
	}
	if (member->talk_volume_adjust && f) {
	    ret=opbx_frame_adjust_volume(f, gain_adjust);
        }
    }
    else
    {
	// Listen volume
	ret=opbx_frame_adjust_volume(f, gain_adjust);
    }
    return ret;
}
