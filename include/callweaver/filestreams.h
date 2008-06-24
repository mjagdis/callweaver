/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - Navynet SRL
 *
 * Massimo Cetra <devel@navynet.it>
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 * \brief Generic FileStreams Support.
 */


/* 
    NOTE: 
        uri formats may be in the form
            type://${destination}

        "type" represents the type of stream. 
        The file:// type identifies an audio file on our server.

        "type" can be even registered by a particular implementation so,
        if you write a filestream implemention registers to handle the
        shoutme:// "type", it will be the only one that will manage it.

        The same type can be handled by different implementations.
        The first that supports it without translations will be chosen.
        If no one is found, then the 1st one handling the file with a translation
        will be chosen.

*/

#ifndef _CALLWEAVER_FILESTREAMS_H
#define _CALLWEAVER_FILESTREAMS_H

#include "callweaver/mpool.h"

/*!\brief Those flags indicate what the stream is doing. */
typedef enum {
    FS_READ,
    FS_WRITE,
    FS_NEED_TRANSLATION
    // probably more needed
} filestream_status_flags;

/*!\brief Those flags indicate how to seek the file. */
typedef enum {
    FS_SEEK_SET, 
    FS_SEEK_CUR, 
    FS_SEEK_END
} filestream_seek;

/*!\brief This enums is indicates the eventual error codes. */
typedef enum {
    FS_RESULT_SUCCESS,
    FS_RESULT_FAILURE_GENERIC,
    FS_RESULT_FAILURE_UNIMPLEMENTED,
    FS_RESULT_FAILURE_INTERRUPTED,
    FS_RESULT_FAILURE_NOTFOUND,

    FS_RESULT_FILE_EXISTS_NATIVE,
    FS_RESULT_FILE_EXISTS_NON_NATIVE,
    FS_RESULT_FILE_NOT_FOUND

    // probably more needed
} filestream_result_value ;

typedef struct cw_filestream_implementation cw_filestream_implementation_t;
typedef struct cw_filestream_session cw_filestream_session_t;

struct cw_filestream_implementation {
    const char  *engine_name;
    const char  *description;

    /* comma separated stream types like: "file://,http://,whateveryouwant:// */
    const char  *supported_stream_types;

    /* audio format and rate to this implementation can read/write */
    int         codec_format;
    int         codec_rate;

    /* Initializes a filestream session */
    //We need to parse a pointer to the implementation (basically itself) because any angine, with the
    //same code, can be set up to work with different codecs/rates.
    //Those informations are contained in the impl. structure so we want to have it.

    filestream_result_value (*init)( cw_filestream_session_t *session, cw_filestream_implementation_t *impl );

    /* Check if a suitable file exists and can be played with this implementation */
    filestream_result_value (*findsuitablefile)( cw_filestream_implementation_t *impl, char *type, char *file );

    /* Read the next frame from the filestream (if available) and report back when to get next one (in ms) */
    struct cw_frame *(*read)( int *whennext );

    /* Write a frame to a session */
    filestream_result_value (*write)( struct cw_frame * );

    /* OLD API. Do we needit ? */
    filestream_result_value (*rewrite)( FILE *f );


    /* seek num samples into file, whence(think normal seek) */
    filestream_result_value (*seek)( long ms, filestream_seek whence );

    /* tell current position - returns negative upon error */
    long (*tell)( void );


    /* trunc file to current position */
    filestream_result_value (*trunc)( void );

    /* Close file, and destroy filestream structure */
    filestream_result_value (*close)( void );


    /* Linked list pointer. Must NOT be used and MUST be set to NULL when initializing. */
    cw_filestream_implementation_t *next;
    
};



/* *************************************************************************
        REGISTERING FUNCTIONS ( used by the core )
   ************************************************************************* */

/*! \brief Registers a filestream format. 
    \param implementation is the implementation to register
    \return 0 on failure - not 0 on success
*/
int cw_filestream_register( cw_filestream_implementation_t *implementation );


/*! \brief Unregisters a filestream format */
int cw_filestream_unregister( cw_filestream_implementation_t *implementation );


/* *************************************************************************
        ACCESS TO PRIVATE DATA FUNCTIONS
   ************************************************************************* */

cw_channel_t *cw_filestream_session_get_channel( cw_filestream_session_t *fs );

const char *cw_filestream_session_get_uri( cw_filestream_session_t *fs );

cw_mpool_t *cw_filestream_session_get_pool( cw_filestream_session_t *fs );

filestream_result_value cw_filestream_session_set_pvt( cw_filestream_session_t *fs, void *pvt );

void *cw_filestream_session_get_pvt( cw_filestream_session_t *fs );


/* *************************************************************************
        Creation and destruction of a filestream
   ************************************************************************* */

/*! \brief basing on the channel read/write codec, this function */
cw_filestream_session_t *cw_filestream_create( cw_channel_t *chan, const char *uri );

/*! \brief destroys our filestream */
filestream_result_value cw_filestream_destroy( cw_filestream_session_t *fs );


/* *************************************************************************
        basic functions to manage the filestream
        ( only the ones relating with each implementation )
   ************************************************************************* */

/*! \brief */
struct cw_frame       *cw_filestream_readframe( cw_filestream_session_t *fs );

/*! \brief Writes a frame to a file */
filestream_result_value cw_filestream_writeframe( cw_filestream_session_t *fs, struct cw_frame *f );

/*! \brief */
long                    cw_filestream_tell( struct cw_filestream *fs );

/*! \brief Seeks into stream */
filestream_result_value cw_filestream_seek( cw_filestream_session_t *fs, long sample_offset, filestream_seek whence );

/*! \brief Trunc stream at current location */
filestream_result_value cw_filestream_trunc( cw_filestream_session_t *fs );

/*! \brief */
filestream_result_value cw_filestream_fastforward( cw_filestream_session_t *fs, long ms );

/*! \brief */
filestream_result_value cw_filestream_rewind( cw_filestream_session_t *fs, long ms );


/* *************************************************************************
        basic functions to manage the filestream
        ( higher level functions )
   ************************************************************************* */

/*! \brief */
filestream_result_value cw_filestream_wait( cw_filestream_session_t *fs, const char *break_on_char );

/*! \brief */
filestream_result_value cw_filestream_wait_valid_exten( cw_filestream_session_t *fs, const char *context );

filestream_result_value cw_filestream_wait_controlling( cw_filestream_session_t *fs, const char *break_on_char, const char *forward_char, const char *rewind_char, int ms );

/*! \brief */
filestream_result_value cw_filestream_full( cw_filestream_session_t *fs, const char *break_on_char, int audiofd, int monfd );

/*! \brief */
filestream_result_value cw_filestream_stream_start( cw_filestream_session_t *fs, long ms );

/*! \brief Stops playback */
filestream_result_value cw_filestream_stream_stop( cw_filestream_session_t *fs );

/* *************************************************************************
        functions to manage simple files ...
        Check if those are needed or not.
   ************************************************************************* */

/*! \brief */
int cw_filestream_rename(const char *oldname, const char *newname, const char *fmt);

/*! \brief */
int cw_filestream_delete(const char *filename, const char *fmt);

/*! \brief */
int cw_filestream_copy(const char *oldname, const char *newname, const char *fmt);




// I need to check how those are used in the core to propose a more suitable API
/*
cw_filestream_session_t *cw_filestream_readfile(
        const char *filename, 
        const char *type, 
        const char *comment, 
        int flags, 
        int check, 
        mode_t mode );

cw_filestream_session_t *cw_filestream_writefile(
        const char *filename, 
        const char *type, 
        const char *comment, 
        int flags, 
        int check, 
        mode_t mode);
*/

// The followings are part of the OLD interface.
// Probably are not needed anymore.

/*! \brief Like prepare??*/
//cw_filestream_session_t *cw_filestream_open(struct cw_channel *chan, const char *filename);
//As cw_filestream_open without _full but doesn't stops generator
//struct cw_filestream *cw_openstream_full(struct cw_channel *chan, const char *filename, const char *preflang, int asis);
/*! \brief play a open stream on a channel. */
// Used only by OGI. rewirkable with higher levels
//int cw_playstream(struct cw_filestream *s);

/*! \brief Checks if a suitable file implementation exists for a given channel.
    \param channel
    \param filename without extension
    \return file path if exists, null otherwise.
    \remark channel structure already contains the preferred format, language and sample rate.
*/
//char * cw_filestream_suitablefile_exists(cw_channel_t *channel, const char *filename);


/* ************************************************************************* */
/*
static int cw_filestream_check_implementation_support( cw_filestream_implementation_t *impl, char *filename ) {
    // Check for URI support
    // Check for sample rate support
    // Check for file size support
}

*/



#endif // _CALLWEAVER_FILESTREAMS_H
