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

//#define fse_log(lev,fmt,...) if ( option_debug > 3 ) cw_log(lev, fmt, __VA_ARGS__ )
#define fse_log(lev,fmt,...) if ( option_debug > 3 ) cw_log(CW_LOG_WARNING,"Filestream Engine: " fmt, __VA_ARGS__ )

/*
    !\brief opaque structure that holds what's needed to stream/record a file
*/

struct cw_filestream_session {
    // We can both open files and URIS
    // examples:        file:beep
    //                  http://10.1.1.1/uri/to/a/stream
    char                                *uri;

    // This is used, for example, while recording to a tmp file,
    // before actually moving the recorded file to the final place.
    char                                *tmp_filename;

    /* The channel we are working with */
    cw_channel_t                      *channel;

    /* 
        The channel original formats. In the hope to remove the corresponding values from the 
        channel struct which will be done opaque.
    */
    int                                 channel_old_read_format;
    int                                 channel_old_write_format;

    /*  The pointer to the implementation this session is using */
    cw_filestream_implementation_t    *implementation;

    /* Eventual translator, if needed */
    struct cw_trans_pvt               *translator;

    /* Eventual resampler, if needed, when it will exist */
    /* struct cw_trans_pvt               *translator; */

    /* The pool used by the session (and passed to the implementation) */
    cw_mpool_t                        *pool;

    /* Private data used by the implementation session */
    void                                *pvt;

};

/* ************************************************************************* */

CW_MUTEX_DEFINE_STATIC(implementation_lock);

static cw_filestream_implementation_t *implementations = NULL;

/* *************************************************************************
        REGISTERING FUNCTIONS ( used by the core )
   ************************************************************************* */

int cw_filestream_register( cw_filestream_implementation_t *implementation )
{
    int res = -1;
    cw_filestream_implementation_t *tmp;

    if ( !implementation ) {
	cw_log(CW_LOG_ERROR, "Unable to register a NULL implementation\n");
	return res;
    }

    if (cw_mutex_lock(&implementation_lock)) {
	cw_log(CW_LOG_ERROR, "Unable to lock format list\n");
	return res;
    }

    tmp = implementations;

    while ( tmp ) {
	if (!strcasecmp(implementation->engine_name, tmp->engine_name)) {
	    cw_log(CW_LOG_WARNING, "Tried to register filestream '%s' but it's already registered\n", implementation->engine_name);
            goto done;
	}
        tmp = tmp->next;
    }
    
    implementation->next=implementations;
    implementations = implementation;
    res = 0;
    cw_log(CW_LOG_DEBUG, "Registered filestream %s\n", implementation->engine_name );

done:
    cw_mutex_unlock(&implementation_lock);
    
    return res;
}

int cw_filestream_unregister( cw_filestream_implementation_t *implementation )
{
    int res = -1;
    cw_filestream_implementation_t *tmp = NULL, *prev = NULL;

    if ( !implementation ) {
	cw_log(CW_LOG_ERROR, "Unable to register a NULL implementation\n");
	return res;
    }

    if (cw_mutex_lock(&implementation_lock)) {
	cw_log(CW_LOG_ERROR, "Unable to lock format list\n");
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
    cw_mutex_unlock(&implementation_lock);

    return res;
}

/* *************************************************************************
        ACCESS TO PRIVATE DATA FUNCTIONS
   ************************************************************************* */

cw_channel_t *cw_filestream_session_get_channel( cw_filestream_session_t *fs ) {
    return fs->channel;
}

const char *cw_filestream_session_get_uri( cw_filestream_session_t *fs ) {
    return fs->uri;
}

cw_mpool_t *cw_filestream_session_get_pool( cw_filestream_session_t *fs ) {
    return fs->pool;
}

filestream_result_value cw_filestream_session_set_pvt( cw_filestream_session_t *fs, void *pvt ) {
    fs->pvt = pvt;
    return FS_RESULT_SUCCESS;
}

void *cw_filestream_session_get_pvt( cw_filestream_session_t *fs ) {
    return (void*) fs->pvt;
}


/* *************************************************************************
        Creation and destruction of a filestream
   ************************************************************************* */

static cw_filestream_implementation_t *find_suitable_implementation( cw_filestream_session_t *fs, const char *uri, char *requested_impl, filestream_result_value *mode) {
    char        *types[16], 
                *tmp, *tmpuri, *type, *file;
    int         maxsize = 15,
                nstrings,
                t;
    cw_filestream_implementation_t    *impl, 
                                        *ret = NULL;;
    filestream_result_value             match = FS_RESULT_FILE_NOT_FOUND,
                                        ret_weight = FS_RESULT_FILE_NOT_FOUND;

    fse_log(LOG_DEBUG,"Looking for a suitable implementation %s","\n");

    impl = implementations;

    tmpuri = cw_mpool_strdup( fs->pool, (char *) uri );
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
        tmp = cw_mpool_strdup( fs->pool, (char *) impl->supported_stream_types );

        nstrings = cw_separate_app_args( tmp, ',', maxsize, types );

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
            cw_log(CW_LOG_DEBUG,"found suitable file native.\n");
            break;
        case FS_RESULT_FILE_EXISTS_NON_NATIVE:
            cw_log(CW_LOG_DEBUG,"found suitable file non native.\n");
            break;
        case FS_RESULT_FILE_NOT_FOUND:
            cw_log(CW_LOG_DEBUG,"not found suitable file\n");
            // do nothing
            break;
        default:
            assert(0);
            return impl;
    }

    return ret;
}

cw_filestream_session_t *cw_filestream_create( cw_channel_t *chan, const char *uri )
{
    cw_filestream_implementation_t *impl;
    cw_filestream_session_t *fs;
    filestream_result_value mode;
    cw_mpool_t *pool;
    int pool_err;

    fse_log(LOG_DEBUG,"Creating filestream session.%s","\n" );

    //create the pool then
    pool = cw_mpool_open( 0, 0, NULL, &pool_err);

    //create our stream, assign our pool to it and set the defaults.

    fs = cw_mpool_alloc( pool, sizeof(cw_filestream_session_t), &pool_err ); //TODO BETTER
    fs->pool = pool;

    fs->channel=chan;
    fs->uri=cw_mpool_strdup( pool, (char *) uri);
    fs->implementation=NULL;
    fs->translator=NULL;
    fs->pvt=NULL;

    // then we check if we have a valid and suitable implementation to play the file.
    //TODO: find a way to request for a specific filestream implementation

    impl = find_suitable_implementation( fs, uri, NULL, &mode );

    if ( !impl ) {
        cw_log(CW_LOG_ERROR,"Cannot find any suitable filestream to play requested uri: %s \n", uri);
        cw_mpool_close( pool );
        return NULL;
    }

    if (  ( impl->init( fs, impl ) != FS_RESULT_SUCCESS ) ) {
        cw_log(CW_LOG_ERROR,"Cannot initialize filestream engine '%s' to play requested uri: %s \n", impl->engine_name ,uri);
        cw_mpool_close( pool );
        return NULL;
    }

    fs->implementation = impl;

    return fs;
}

filestream_result_value cw_filestream_destroy( cw_filestream_session_t *fs ) 
{
    cw_mpool_t *pool;

    fse_log(LOG_DEBUG,"Closing filestream session.%s","\n" );

    if ( !fs ) {
        cw_log(CW_LOG_WARNING,"Cannot destroy NULL filestream.\n");
        return FS_RESULT_FAILURE_GENERIC;
    }

    //TODO check fs is not null

    pool = fs->pool;
    //TODO: check for generators, translators and eventually stop them
    // and restore original channel formats, if changed.

    if ( fs && fs->implementation && fs->implementation->close )
        fs->implementation->close();

    cw_mpool_close( pool );

    return FS_RESULT_SUCCESS;
}


/* *************************************************************************
        basic functions to manage the filestream
        ( only the ones relating with each implementation )
   ************************************************************************* */

struct cw_frame       *cw_filestream_readframe( cw_filestream_session_t *fs )
{
    

    return NULL;
}

filestream_result_value cw_filestream_writeframe( cw_filestream_session_t *fs, struct cw_frame *f )
{
    return FS_RESULT_SUCCESS;
}

long                    cw_filestream_tell( struct cw_filestream *fs )
{
    return -1;
}



filestream_result_value cw_filestream_seek( cw_filestream_session_t *fs, long sample_offset, filestream_seek whence )
{
    if ( fs && fs->implementation) {
        if ( fs->implementation->seek )
            return fs->implementation->seek(sample_offset, whence);
        else
            return FS_RESULT_FAILURE_UNIMPLEMENTED;
    }
    return FS_RESULT_FAILURE_GENERIC;
}

filestream_result_value cw_filestream_trunc( cw_filestream_session_t *fs )
{
    if ( fs && fs->implementation) {
        if ( fs->implementation->trunc )
            return fs->implementation->trunc();
        else
            return FS_RESULT_FAILURE_UNIMPLEMENTED;
    }
    return FS_RESULT_FAILURE_GENERIC;
}

filestream_result_value cw_filestream_fastforward( cw_filestream_session_t *fs, long ms )
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

filestream_result_value cw_filestream_rewind( cw_filestream_session_t *fs, long ms )
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

filestream_result_value cw_filestream_wait( cw_filestream_session_t *fs, const char *break_on_char )
{
    return FS_RESULT_SUCCESS;
}

filestream_result_value cw_filestream_wait_valid_exten( cw_filestream_session_t *fs, const char *context )
{
    return FS_RESULT_SUCCESS;
}

filestream_result_value cw_filestream_wait_controlling( cw_filestream_session_t *fs, const char *break_on_char, const char *forward_char, const char *rewind_char, int ms )
{
    return FS_RESULT_SUCCESS;
}

filestream_result_value cw_filestream_full( cw_filestream_session_t *fs, const char *break_on_char, int audiofd, int monfd )
{
    return FS_RESULT_SUCCESS;
}

filestream_result_value cw_filestream_stream_start( cw_filestream_session_t *fs, long ms )
{
    return FS_RESULT_SUCCESS;
}

filestream_result_value cw_filestream_stream_stop( cw_filestream_session_t *fs )
{
    return FS_RESULT_SUCCESS;
}

/* *************************************************************************
        functions to manage simple files ...
        Check if those are needed or not.
   ************************************************************************* */

int cw_filestream_rename(const char *oldname, const char *newname, const char *fmt)
{
    return FS_RESULT_SUCCESS;
}

int cw_filestream_delete(const char *filename, const char *fmt)
{
    return FS_RESULT_SUCCESS;
}

int cw_filestream_copy(const char *oldname, const char *newname, const char *fmt)
{
    return FS_RESULT_SUCCESS;
}

