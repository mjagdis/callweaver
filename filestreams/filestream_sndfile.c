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
    global variables
*/

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include "assert.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "callweaver.h"
#include "callweaver/options.h"
#include "callweaver/mpool.h"
#include "callweaver/channel.h"
#include "callweaver/frame.h"
#include "callweaver/module.h"

#include "callweaver/filestreams.h"


/* **************************************************************************
         SPECIFIC FILESTREAM IMPLEMENTATION FUNCTIONS
   *********************************************************************** */

typedef struct private_s {
    cw_filestream_implementation_t    *myimpl;
    FILE                                *FD;
} private_t;


static filestream_result_value my_file_exists ( int codec_format, int codec_rate, char *filename, char **path ) {
    int res;
    char file[256];
    struct stat st;
    filestream_result_value ret = FS_RESULT_FILE_NOT_FOUND;

    // Ok, we are playing a file on our filesystem.
    // This module will look under the sound directory for a file path
    // like: $cw_sound_dir/sndfile/$codec_rate/$codec_format/$filename.*

    snprintf( file, sizeof(file), "%s/sndfile/%d/%s/%s.wav", cw_config_CW_SOUNDS_DIR, 
              codec_rate, cw_getformatname(codec_format), filename );

    res = stat(file, &st);  // This throws 0 on success.    
    cw_log(CW_LOG_DEBUG,"fsi_findsuitablefile: stats of %s\n", file);

    if ( res ) {
        ret = FS_RESULT_FILE_EXISTS_NON_NATIVE;

        snprintf( file, sizeof(file), "%s/sndfile/%s/%s/%s.wav", cw_config_CW_SOUNDS_DIR, 
                  "default", cw_getformatname(codec_format), filename );
        res = stat(file, &st);  // This throws 0 on success.

        cw_log(CW_LOG_DEBUG,"fsi_findsuitablefile: stats of %s\n", file);

        if ( res ) {
            snprintf( file, sizeof(file), "%s/sndfile/%s/%s/%s.wav", cw_config_CW_SOUNDS_DIR, 
                      "default", "default", filename );
            res = stat(file, &st);  // This throws 0 on success.
            cw_log(CW_LOG_DEBUG,"fsi_findsuitablefile: stats of %s\n", file);
            if ( res ) 
                ret = FS_RESULT_FILE_NOT_FOUND;
        }
    }
    else
        ret = FS_RESULT_FILE_EXISTS_NATIVE;

    if ( res ) {
        *path = NULL;
    }
    else
    {
        if ( path )
            *path = strdup(file);
    }

    return ret;
}

static filestream_result_value   fsi_init( cw_filestream_session_t *session, cw_filestream_implementation_t *impl )
{

    cw_mpool_t        *pool = cw_filestream_session_get_pool( session );
    private_t           *pvt;    
    int                 pool_err;
    const char          *uri;

    assert( pool!=NULL );

    pvt = cw_mpool_alloc( pool, sizeof(private_t), &pool_err );
    pvt->myimpl = impl;
    cw_filestream_session_set_pvt( session, (void*) pvt );
    uri = cw_filestream_session_get_uri( session );    

    /*
        now we should parse the URI and,
        do the required operations to set our pvt stuff on 
    */

    return FS_RESULT_SUCCESS;
}

static filestream_result_value   fsi_findsuitablefile( cw_filestream_implementation_t *impl, char *type, char *filename )
{
    filestream_result_value ret = FS_RESULT_FILE_NOT_FOUND;
    char *name = NULL;

    cw_log(CW_LOG_DEBUG,"fsi_findsuitablefile\n");

    if ( !strcmp(type,"file") ) {
        if ( (ret=my_file_exists( impl->codec_format, impl->codec_rate, filename, &name ))!=FS_RESULT_FILE_NOT_FOUND ) {
            cw_log(CW_LOG_DEBUG,"fsi_findsuitablefile: found file with path %s\n", name);
            free(name);
        }
    }
    else
    {
        // Whatever they passed us (shouldn't happen) we are not supporting it.
    }

    return ret;
}



static struct cw_frame *fsi_read( int *whennext )
{
    return NULL;
}

static filestream_result_value fsi_write( struct cw_frame *frame )
{
    return FS_RESULT_SUCCESS;
}

static filestream_result_value fsi_rewrite( FILE *f )
{
    return FS_RESULT_SUCCESS;
}



static filestream_result_value fsi_seek( long ms, filestream_seek whence )
{
    return FS_RESULT_SUCCESS;
}

static long fsi_tell( void )
{
    return -1;
}

static filestream_result_value fsi_trunc( void )
{
    return FS_RESULT_SUCCESS;
}

static filestream_result_value fsi_close( void )
{
    return FS_RESULT_SUCCESS;
}



/* **************************************************************************
         GENERAL MODULE STUFF (loading/unloading)
   *********************************************************************** */

#define FSI_NAME               "sndfile"
#define FSI_DESC               "FileStream SNDFile"
#define FSI_STREAMTYPE_FILE   "file"

static cw_filestream_implementation_t sndfile_implementation = {
    FSI_NAME,
    FSI_DESC,
    FSI_STREAMTYPE_FILE,
    CW_FORMAT_SLINEAR,
    8000,
    fsi_init,
    fsi_findsuitablefile,
    fsi_read,
    fsi_write,
    fsi_rewrite,
    fsi_seek,
    fsi_tell,
    fsi_trunc,
    fsi_close,
    NULL
};

static int load_module(void)
{
    return cw_filestream_register( &sndfile_implementation );
}

static int unload_module(void)
{
    return cw_filestream_unregister( &sndfile_implementation );
}


MODULE_INFO(load_module, NULL, unload_module, NULL, FSI_DESC)
