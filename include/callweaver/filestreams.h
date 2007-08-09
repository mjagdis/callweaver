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

#ifndef _CALLWEAVER_FILESTREAMS_H
#define _CALLWEAVER_FILESTREAMS_H


/*!\brief Those flags indicate what the stream is doing. */
typedef enum {
    FS_READ,
    FS_WRITE
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
    FS_RESULT_FAILURE_UNSUPPORTED,
    FS_RESULT_FAILURE_INTERRUPTED,
    FS_RESULT_FILE_EXISTS_NATIVE,
    FS_RESULT_FILE_EXISTS_NON_NATIVE,
    FS_RESULT_FILE_DONT_EXIST
    // probably more needed
} filestream_result_value ;

typedef struct opbx_filestream_implementation opbx_filestream_implementation_t;
typedef struct opbx_filestream_session opbx_filestream_session_t;

struct opbx_filestream_implementation {
    char        *name;
    char        *description;

    /* comma separated stream types like: "file://,http://,whateveryouwant:// */
    char        *supported_stream_types;

    /* audio format and rate to this implementation can read/write */
    int         codec_format;
    int         codec_rate;

    /* Initializes a filestream session */
    opbx_filestream_session_t *(*init)( opbx_mpool_t *pool, opbx_filestream_session_t *session, char *uri );

    /* Check if a suitable file exists and can be played with this implementation */
    filestream_result_value *(*findsuitablefile)( const char *uri );

    /* Read the next frame from the filestream (if available) and report back when to get next one (in ms) */
    struct opbx_frame *(*read)( int *whennext );

    /* Write a frame to a session */
    filestream_result_value (*write)( struct opbx_frame * );

    /* OLD API. Do we needit ? */
    filestream_result_value *(*rewrite)( FILE *f );


    /* seek num samples into file, whence(think normal seek) */
    filestream_result_value (*seek)( long ms, filestream_seek whence );

    /* tell current position - returns negative upon error */
    long (*tell)( void );


    /* trunc file to current position */
    filestream_result_value (*trunc)( void );

    /* Close file, and destroy filestream structure */
    filestream_result_value (*close)( void );


    /* Linked list pointer. Must NOT be used and MUST be set to NULL when initializing. */
    opbx_filestream_implementation_t *next;
    
};

#endif // _CALLWEAVER_FILESTREAMS_H
