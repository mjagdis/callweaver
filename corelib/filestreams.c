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
    
#include "assert.h"

#include "callweaver/mpool.h"
#include "callweaver/channel.h"
#include "callweaver/options.h"

#include "callweaver/filestreams.h"
#include "callweaver/frame.h"
#include "callweaver/app.h"

//#define fse_log(lev,fmt,...) if ( option_debug > 3 ) opbx_log(lev, fmt, __VA_ARGS__ )
#define fse_log(lev,fmt,...) if ( option_debug > 3 ) opbx_log(OPBX_LOG_WARNING,"Filestream Engine: " fmt, __VA_ARGS__ )

/*
    !\brief opaque structure that holds what's needed to stream/record a file
*/

struct opbx_filestream_session {
    // We can both open files and URIS
    // examples:        file:beep
    //                  http://10.1.1.1/uri/to/a/stream
    char                                *uri;

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

/* ************************************************************************* */

OPBX_MUTEX_DEFINE_STATIC(implementation_lock);

static opbx_filestream_implementation_t *implementations = NULL;

/* *************************************************************************
        REGISTERING FUNCTIONS ( used by the core )
   ************************************************************************* */

int opbx_filestream_register( opbx_filestream_implementation_t *implementation )
{
    int res = -1;
    opbx_filestream_implementation_t *tmp;

    if ( !implementation ) {
	opbx_log(OPBX_LOG_ERROR, "Unable to register a NULL implementation\n");
	return res;
    }

    if (opbx_mutex_lock(&implementation_lock)) {
	opbx_log(OPBX_LOG_ERROR, "Unable to lock format list\n");
	return res;
    }

    tmp = implementations;

    while ( tmp ) {
	if (!strcasecmp(implementation->engine_name, tmp->engine_name)) {
	    opbx_log(OPBX_LOG_WARNING, "Tried to register filestream '%s' but it's already registered\n", implementation->engine_name);
            goto done;
	}
        tmp = tmp->next;
    }
    
    implementation->next=implementations;
    implementations = implementation;
    res = 0;
    opbx_log(OPBX_LOG_DEBUG, "Registered filestream %s\n", implementation->engine_name );

done:
    opbx_mutex_unlock(&implementation_lock);
    
    return res;
}

int opbx_filestream_unregister( opbx_filestream_implementation_t *implementation )
{
    int res = -1;
    opbx_filestream_implementation_t *tmp = NULL, *prev = NULL;

    if ( !implementation ) {
	opbx_log(OPBX_LOG_ERROR, "Unable to register a NULL implementation\n");
	return res;
    }

    if (opbx_mutex_lock(&implementation_lock)) {
	opbx_log(OPBX_LOG_ERROR, "Unable to lock format list\n");
	return res;
    }

    tmp = implementations;

    while ( tmp ) {
        if ( tmp == implementation ) {
            if ( prev )
                prev->next = tmp->next;
            else
                implementations = tmp->next;

            res = 0;
            goto done;
        }
        prev = tmp;
        tmp = tmp->next;
    }

done:
    opbx_mutex_unlock(&implementation_lock);

    return res;
}

/* *************************************************************************
        ACCESS TO PRIVATE DATA FUNCTIONS
   ************************************************************************* */

opbx_channel_t *opbx_filestream_session_get_channel( opbx_filestream_session_t *fs ) {
    return fs->channel;
}

const char *opbx_filestream_session_get_uri( opbx_filestream_session_t *fs ) {
    return fs->uri;
}

opbx_mpool_t *opbx_filestream_session_get_pool( opbx_filestream_session_t *fs ) {
    return fs->pool;
}

filestream_result_value opbx_filestream_session_set_pvt( opbx_filestream_session_t *fs, void *pvt ) {
    fs->pvt = pvt;
    return FS_RESULT_SUCCESS;
}

void *opbx_filestream_session_get_pvt( opbx_filestream_session_t *fs ) {
    return (void*) fs->pvt;
}


/* *************************************************************************
        Creation and destruction of a filestream
   ************************************************************************* */

static opbx_filestream_implementation_t *find_suitable_implementation( opbx_filestream_session_t *fs, const char *uri, char *requested_impl, filestream_result_value *mode) {
    char        *types[16], 
                *tmp, *tmpuri, *type, *file;
    int         maxsize = 15,
                nstrings,
                t;
    opbx_filestream_implementation_t    *impl, 
                                        *ret = NULL;;
    filestream_result_value             match = FS_RESULT_FILE_NOT_FOUND,
                                        ret_weight = FS_RESULT_FILE_NOT_FOUND;

    fse_log(LOG_DEBUG,"Looking for a suitable implementation %s","\n");

    impl = implementations;

    tmpuri = opbx_mpool_strdup( fs->pool, (char *) uri );
    type = tmpuri;

    if ( (file = strcasestr( tmpuri, "://" )) ) {
        *file = '\0';
        file=file + 3;
        type=tmpuri;
    }
    else
    {
        fse_log(LOG_DEBUG,"Bad URI format %s","\n");
        return NULL;
    }

    while ( impl ) {
        tmp = opbx_mpool_strdup( fs->pool, (char *) impl->supported_stream_types );

        nstrings = opbx_separate_app_args( tmp, ',', maxsize, types );

        for ( t=0; t<nstrings; t++ ) {
            fse_log(LOG_DEBUG,"Comparing  '%s' to '%s' for file %s ]\n", types[t], type, file );

            if ( !strncmp( types[t], type, strlen(types[t])) ) { 
                fse_log(LOG_DEBUG,"Match found: %s. Now looking for compatibility.\n", impl->engine_name );
                // Now compare codec && rate to find out if it's suitable

/*                
                if ( 
                        ( fs->channel->nativeformats & impl->codec_format ) 
                   ) 
                {
                    weight = 1;
                    //codec formats match 
                    if ( fs->channel->samples_per_second == impl->codec_rate )
                        //as well as codec rate 
                        weight = 2;
                }
*/
                // Then do the checks for file or stream existance (and matching)

                match = impl->findsuitablefile( impl, type, file);

                switch ( match ) {
                    case FS_RESULT_FILE_EXISTS_NATIVE:
                        if ( ret_weight != FS_RESULT_FILE_EXISTS_NATIVE ) {
                            ret = impl;
                            ret_weight = match;
                        }
                        break;
                    case FS_RESULT_FILE_EXISTS_NON_NATIVE:
                        if ( (ret_weight != FS_RESULT_FILE_EXISTS_NATIVE ) && (ret_weight != FS_RESULT_FILE_EXISTS_NON_NATIVE ) ) {
                            ret = impl;
                            ret_weight = match;
                        }
                        break;
                    case FS_RESULT_FILE_NOT_FOUND:
                        // do nothing
                        break;
                    default:
                        assert(0);
                        break;
                }

            }

        }

        impl = impl->next;
    }

    switch ( ret_weight ) {
        case FS_RESULT_FILE_EXISTS_NATIVE:
            opbx_log(OPBX_LOG_DEBUG,"found suitable file native.\n");
            break;
        case FS_RESULT_FILE_EXISTS_NON_NATIVE:
            opbx_log(OPBX_LOG_DEBUG,"found suitable file non native.\n");
            break;
        case FS_RESULT_FILE_NOT_FOUND:
            opbx_log(OPBX_LOG_DEBUG,"not found suitable file\n");
            // do nothing
            break;
        default:
            assert(0);
            return impl;
    }

    return ret;
}

opbx_filestream_session_t *opbx_filestream_create( opbx_channel_t *chan, const char *uri )
{
    opbx_filestream_implementation_t *impl;
    opbx_filestream_session_t *fs;
    filestream_result_value mode;
    opbx_mpool_t *pool;
    int pool_err;

    fse_log(LOG_DEBUG,"Creating filestream session.%s","\n" );

    //create the pool then
    pool = opbx_mpool_open( 0, 0, NULL, &pool_err);

    //create our stream, assign our pool to it and set the defaults.

    fs = opbx_mpool_alloc( pool, sizeof(opbx_filestream_session_t), &pool_err ); //TODO BETTER
    fs->pool = pool;

    fs->channel=chan;
    fs->uri=opbx_mpool_strdup( pool, (char *) uri);
    fs->implementation=NULL;
    fs->translator=NULL;
    fs->pvt=NULL;

    // then we check if we have a valid and suitable implementation to play the file.
    //TODO: find a way to request for a specific filestream implementation

    impl = find_suitable_implementation( fs, uri, NULL, &mode );

    if ( !impl ) {
        opbx_log(OPBX_LOG_ERROR,"Cannot find any suitable filestream to play requested uri: %s \n", uri);
        opbx_mpool_close( pool );
        return NULL;
    }

    if (  ( impl->init( fs, impl ) != FS_RESULT_SUCCESS ) ) {
        opbx_log(OPBX_LOG_ERROR,"Cannot initialize filestream engine '%s' to play requested uri: %s \n", impl->engine_name ,uri);
        opbx_mpool_close( pool );
        return NULL;
    }

    fs->implementation = impl;

    return fs;
}

filestream_result_value opbx_filestream_destroy( opbx_filestream_session_t *fs ) 
{
    opbx_mpool_t *pool;

    fse_log(LOG_DEBUG,"Closing filestream session.%s","\n" );

    if ( !fs ) {
        opbx_log(OPBX_LOG_WARNING,"Cannot destroy NULL filestream.\n");
        return FS_RESULT_FAILURE_GENERIC;
    }

    //TODO check fs is not null

    pool = fs->pool;
    //TODO: check for generators, translators and eventually stop them
    // and restore original channel formats, if changed.

    if ( fs && fs->implementation && fs->implementation->close )
        fs->implementation->close();

    opbx_mpool_close( pool );

    return FS_RESULT_SUCCESS;
}


/* *************************************************************************
        basic functions to manage the filestream
        ( only the ones relating with each implementation )
   ************************************************************************* */

struct opbx_frame       *opbx_filestream_readframe( opbx_filestream_session_t *fs )
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
    if ( fs && fs->implementation) {
        if ( fs->implementation->seek )
            return fs->implementation->seek(sample_offset, whence);
        else
            return FS_RESULT_FAILURE_UNIMPLEMENTED;
    }
    return FS_RESULT_FAILURE_GENERIC;
}

filestream_result_value opbx_filestream_trunc( opbx_filestream_session_t *fs )
{
    if ( fs && fs->implementation) {
        if ( fs->implementation->trunc )
            return fs->implementation->trunc();
        else
            return FS_RESULT_FAILURE_UNIMPLEMENTED;
    }
    return FS_RESULT_FAILURE_GENERIC;
}

filestream_result_value opbx_filestream_fastforward( opbx_filestream_session_t *fs, long ms )
{
    if ( fs && fs->implementation ) {
        if ( fs->implementation->seek ) {
            return fs->implementation->seek( ms, FS_SEEK_CUR);
        }
        else
            return FS_RESULT_FAILURE_UNIMPLEMENTED;
    }
    return FS_RESULT_FAILURE_GENERIC;
}

filestream_result_value opbx_filestream_rewind( opbx_filestream_session_t *fs, long ms )
{
    if ( fs && fs->implementation ) {
        if ( fs->implementation->seek ) {
            return fs->implementation->seek( -ms, FS_SEEK_CUR);
        }
        else
            return FS_RESULT_FAILURE_UNIMPLEMENTED;
    }
    return FS_RESULT_FAILURE_GENERIC;
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

