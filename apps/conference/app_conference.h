
// $Id: app_conference.h,v 1.11 2005/12/16 22:31:58 stevek Exp $

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

#ifndef _OPENPBX_CONF_H
#define _OPENPBX_CONF_H

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

/* openpbx includes */
#include "openpbx/lock.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/config.h"
#include "openpbx/app.h"
#include "openpbx/dsp.h"
#include "openpbx/musiconhold.h"
#include "openpbx/manager.h"
#include "openpbx/options.h"
#include "openpbx/cli.h"
#include "openpbx/say.h"
#include "openpbx/utils.h"

/* standard includes */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include <pthread.h>

#if (SILDET == 2)
#include "libspeex/speex_preprocess.h"
#endif

//
// app_conference defines
//

// debug logging level

// LOG_NOTICE for debugging, LOG_DEBUG for production
#ifdef APP_CONFERENCE_DEBUG
#define OPBX_CONF_DEBUG LOG_NOTICE
#else
#define OPBX_CONF_DEBUG LOG_DEBUG
#endif

//
// feature defines
//

// number of times the last non-silent frame should be 
// repeated after silence starts
//#define OPBX_CONF_CACHE_LAST_FRAME 1

//
// debug defines
//

#define DEBUG_USE_TIMELOG

#define DEBUG_FRAME_TIMESTAMPS

// #define DEBUG_OUTPUT_PCM

//
// !!! THESE CONSTANTS SHOULD BE CLEANED UP AND CLARIFIED !!!
//

//
// sample information for AST_FORMAT_SLINEAR format
//

#define OPBX_CONF_SAMPLE_RATE 8000
#define OPBX_CONF_SAMPLE_SIZE 16
#define OPBX_CONF_FRAME_INTERVAL 20

//
// so, since we cycle approximately every 20ms, 
// we can compute the following values:
//
// 160 samples per 20 ms frame -or- 
// ( 8000 samples-per-second * ( 20 ms / 1000 ms-per-second ) ) = 160 samples
//
// 320 bytes ( 2560 bits ) of data  20 ms frame -or-
// ( 160 samples * 16 bits-per-sample / 8 bits-per-byte ) = 320 bytes
//

// 160 samples 16-bit signed linear
#define OPBX_CONF_BLOCK_SAMPLES 160

// 2 bytes per sample ( i.e. 16-bit )
#define OPBX_CONF_BYTES_PER_SAMPLE 2

// 320 bytes for each 160 sample frame of 16-bit audio 
#define OPBX_CONF_FRAME_DATA_SIZE 320

// 1000 ms-per-second / 20 ms-per-frame = 50 frames-per-second
#define OPBX_CONF_FRAMES_PER_SECOND ( 1000 / OPBX_CONF_FRAME_INTERVAL )


//
// buffer and queue values
//

// account for friendly offset when allocating buffer for frame
#define OPBX_CONF_BUFFER_SIZE ( OPBX_CONF_FRAME_DATA_SIZE + OPBX_FRIENDLY_OFFSET )

// maximum number of frames queued per member
#define OPBX_CONF_MAX_QUEUE 100

// minimum number of frames queued per member
#define OPBX_CONF_MIN_QUEUE 0

// number of queued frames before we start dropping
#define OPBX_CONF_QUEUE_DROP_THRESHOLD 4

// number of milliseconds between frame drops
#define OPBX_CONF_QUEUE_DROP_TIME_LIMIT 750

//
// timer and sleep values
//

// milliseconds we're willing to wait for a channel
// event before we check for outgoing frames
#define OPBX_CONF_WAITFOR_LATENCY 40

// milliseconds to sleep before trying to process frames
#define OPBX_CONF_CONFERENCE_SLEEP 40 

// milliseconds to wait between state notification updates
#define OPBX_CONF_NOTIFICATION_SLEEP 500

//
// warning threshold values
//

// number of frames behind before warning
#define OPBX_CONF_OUTGOING_FRAMES_WARN 50

// number of milliseconds off AST_CONF_FRAME_INTERVAL before warning
#define OPBX_CONF_INTERVAL_WARNING 1000

//
// silence detection values
//

// toggle silence detection
#define ENABLE_SILENCE_DETECTION 1

// silence threshold
#define OPBX_CONF_SILENCE_THRESHOLD 128

// speech tail (delay before dropping silent frames, in ms.
// #define OPBX_CONF_SPEECH_TAIL 180

// number of frames to ignore speex_preprocess() after speech detected
#define OPBX_CONF_SKIP_SPEEX_PREPROCESS 20

// our speex probability values
#define OPBX_CONF_PROB_START 0.05
#define OPBX_CONF_PROB_CONTINUE 0.02

//
// format translation values
//

// AST_FORMAT_MAX_AUDIO is 1 << 15, so we support 0..15
#define AC_SUPPORTED_FORMATS 15
#define AC_SLINEAR_INDEX 6

//
// app_conference functions
//

// main module function
int app_conference_main( struct opbx_channel* chan, void* data ) ;

// utility functions
long usecdiff( struct timeval* timeA, struct timeval* timeB ) ;
void add_milliseconds( struct timeval* tv, long ms ) ;

#endif


