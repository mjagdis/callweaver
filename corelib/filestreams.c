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

int opbx_filestream_register( opbx_filestream_implementation_t *implementation )
{
    return -1;
}

int opbx_filestream_unregister( opbx_filestream_implementation_t *implementation )
{
    return -1;
}


/* *************************************************************************
        Creation and destruction of a filestream
   ************************************************************************* */

opbx_filestream_session_t *opbx_filestream_create( opbx_channel_t *chan, const char *uri )
{
    return NULL;
}

filestream_result_value opbx_filestream_destroy( opbx_filestream_session_t *fs ) 
{
    return FS_RESULT_SUCCESS;
}


/* *************************************************************************
        basic functions to manage the filestream
        ( only the ones relating with each implementation )
   ************************************************************************* */

struct opbx_frame       *opbx_filestream_readframe( opbx_filestream_session_t *s )
{
    return NULL;
}

filestream_result_value opbx_filestream_writeframe( opbx_filestream_session_t *fs, struct opbx_frame *f )
{
    return FS_RESULT_SUCCESS;
}

long                    opbx_filestream_tell( struct opbx_filestream *fs )
{
    return -1;
}

filestream_result_value opbx_filestream_seek( opbx_filestream_session_t *fs, long sample_offset, filestream_seek whence )
{
    return FS_RESULT_SUCCESS;
}

filestream_result_value opbx_filestream_trunc( opbx_filestream_session_t *fs )
{
    return FS_RESULT_SUCCESS;
}

filestream_result_value opbx_filestream_fastforward( opbx_filestream_session_t *fs, long ms )
{
    return FS_RESULT_SUCCESS;
}

filestream_result_value opbx_filestream_rewind( opbx_filestream_session_t *fs, long ms )
{
    return FS_RESULT_SUCCESS;
}


/* *************************************************************************
        basic functions to manage the filestream
        ( higher level functions )
   ************************************************************************* */

filestream_result_value opbx_filestream_wait( opbx_filestream_session_t *fs, const char *break_on_char )
{
    return FS_RESULT_SUCCESS;
}

filestream_result_value opbx_filestream_wait_valid_exten( opbx_filestream_session_t *fs, const char *context )
{
    return FS_RESULT_SUCCESS;
}

filestream_result_value opbx_filestream_wait_controlling( opbx_filestream_session_t *fs, const char *break_on_char, const char *forward_char, const char *rewind_char, int ms )
{
    return FS_RESULT_SUCCESS;
}

filestream_result_value opbx_filestream_full( opbx_filestream_session_t *fs, const char *break_on_char, int audiofd, int monfd )
{
    return FS_RESULT_SUCCESS;
}

filestream_result_value opbx_filestream_stream_start( opbx_filestream_session_t *fs, long ms )
{
    return FS_RESULT_SUCCESS;
}

filestream_result_value opbx_filestream_stream_stop( opbx_filestream_session_t *fs )
{
    return FS_RESULT_SUCCESS;
}

/* *************************************************************************
        functions to manage simple files ...
        Check if those are needed or not.
   ************************************************************************* */

int opbx_filestream_rename(const char *oldname, const char *newname, const char *fmt)
{
    return FS_RESULT_SUCCESS;
}

int opbx_filestream_delete(const char *filename, const char *fmt)
{
    return FS_RESULT_SUCCESS;
}

int opbx_filestream_copy(const char *oldname, const char *newname, const char *fmt)
{
    return FS_RESULT_SUCCESS;
}

