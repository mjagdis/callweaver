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

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif
    
#include "callweaver/mpool.h"
#include "callweaver/channel.h"

#include "callweaver/filestreams.h"

/*
    !\brief opaque structure that holds what's needed to stream/record a file
*/

struct opbx_filestream_session {
    // We can both open files and URIS
    // examples:        file:beep
    //                  http://10.1.1.1/uri/to/a/stream
    char                                *original_uri;

    // This is used, for example, while recording to a tmp file,
    // before actually moving the recorded file to the final place.
    char                                *tmp_filename;

    /* The channel we are working with */
    opbx_channel_t                      *channel;

    /* 
        The channel original formats. In the hope to remove the corresponding values from the 
        channel struct which will be done opaque.
    */
    int                                 channel_old_read_format;
    int                                 channel_old_write_format;

    /*  The pointer to the implementation this session is using */
    opbx_filestream_implementation_t    *implementation;

    /* Eventual translator, if needed */
    struct opbx_trans_pvt               *translator;

    /* Eventual resampler, if needed, when it will exist */
    /* struct opbx_trans_pvt               *translator; */

    /* The pool used by the session (and passed to the implementation) */
    opbx_mpool_t                        *pool;

    /* Private data used by the implementation session */
    void                                *pvt;

};

/* *************************************************************************
        REGISTERING FUNCTIONS ( used by the core )
   ************************************************************************* */

/*! \brief Registers a filestream format. 
    \param implementation is the implementation to register
    \return 0 on failure - not 0 on success
*/
int opbx_filestream_register( opbx_filestream_implementation_t *implementation );


/*! \brief Unregisters a filestream format */
int opbx_filestream_unregister( opbx_filestream_implementation_t *implementation );


/* *************************************************************************
        Creation and destruction of a filestream
   ************************************************************************* */

/*! \brief basing on the channel read/write codec, this function */
opbx_filestream_session_t *opbx_filestream_create( opbx_channel_t *chan, const char *uri );

/*! \brief destroys our filestream */
filestream_result_value opbx_filestream_destroy( opbx_filestream_session_t *fs );


/* *************************************************************************
        basic functions to manage the filestream
        ( only the ones relating with each implementation )
   ************************************************************************* */

/*! \brief */
struct opbx_frame       *opbx_filestream_readframe( opbx_filestream_session_t *s );

/*! \brief Writes a frame to a file */
filestream_result_value opbx_filestream_writeframe( opbx_filestream_session_t *fs, struct opbx_frame *f );

/*! \brief */
long                    opbx_filestream_tell( struct opbx_filestream *fs );

/*! \brief Seeks into stream */
filestream_result_value opbx_filestream_seek( opbx_filestream_session_t *fs, long sample_offset, filestream_seek whence );

/*! \brief Trunc stream at current location */
filestream_result_value opbx_filestream_trunc( opbx_filestream_session_t *fs );

/*! \brief */
filestream_result_value opbx_filestream_fastforward( opbx_filestream_session_t *fs, long ms );

/*! \brief */
filestream_result_value opbx_filestream_rewind( opbx_filestream_session_t *fs, long ms );


/* *************************************************************************
        basic functions to manage the filestream
        ( higher level functions )
   ************************************************************************* */

/*! \brief */
filestream_result_value opbx_filestream_wait( opbx_filestream_session_t *fs, const char *break_on_char );

/*! \brief */
filestream_result_value opbx_filestream_wait_valid_exten( opbx_filestream_session_t *fs, const char *context );

filestream_result_value opbx_filestream_wait_controlling( opbx_filestream_session_t *fs, const char *break_on_char, const char *forward_char, const char *rewind_char, int ms );

/*! \brief */
filestream_result_value opbx_filestream_full( opbx_filestream_session_t *fs, const char *break_on_char, int audiofd, int monfd );

/*! \brief */
filestream_result_value opbx_filestream_stream_start( opbx_filestream_session_t *fs, long ms );

/*! \brief Stops playback */
filestream_result_value opbx_filestream_stream_stop( opbx_filestream_session_t *fs );

/* *************************************************************************
        functions to manage simple files ...
        Check if those are needed or not.
   ************************************************************************* */

/*! \brief */
int opbx_filestream_rename(const char *oldname, const char *newname, const char *fmt);

/*! \brief */
int opbx_filestream_delete(const char *filename, const char *fmt);

/*! \brief */
int opbx_filestream_copy(const char *oldname, const char *newname, const char *fmt);




// I need to check how those are used in the core to propose a more suitable API
/*
opbx_filestream_session_t *opbx_filestream_readfile(
        const char *filename, 
        const char *type, 
        const char *comment, 
        int flags, 
        int check, 
        mode_t mode );

opbx_filestream_session_t *opbx_filestream_writefile(
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
//opbx_filestream_session_t *opbx_filestream_open(struct opbx_channel *chan, const char *filename);
//As opbx_filestream_open without _full but doesn't stops generator
//struct opbx_filestream *opbx_openstream_full(struct opbx_channel *chan, const char *filename, const char *preflang, int asis);
/*! \brief */
//struct opbx_filestream *opbx_openvstream(struct opbx_channel *chan, const char *filename, const char *preflang);
/*! \brief Applies an open stream to a channel. */
// Used only by OGI. rewirkable with higher levels
//int opbx_applystream(struct opbx_channel *chan, struct opbx_filestream *s);
/*! \brief play a open stream on a channel. */
// Used only by OGI. rewirkable with higher levels
//int opbx_playstream(struct opbx_filestream *s);

/*! \brief Checks if a suitable file implementation exists for a given channel.
    \param channel
    \param filename without extension
    \return file path if exists, null otherwise.
    \remark channel structure already contains the preferred format, language and sample rate.
*/
//char * opbx_filestream_suitablefile_exists(opbx_channel_t *channel, const char *filename);


/* ************************************************************************* */
/*
static int opbx_filestream_check_implementation_support( opbx_filestream_implementation_t *impl, char *filename ) {
    // Check for URI support
    // Check for sample rate support
    // Check for file size support
}

*/
