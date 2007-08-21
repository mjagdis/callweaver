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
 * \brief Generic FileStreams Support template.
 */

/*
    global variables
*/

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif
    
#include "callweaver/mpool.h"
#include "callweaver/channel.h"
#include "callweaver/frame.h"

#include "callweaver/filestreams.h"


/* **************************************************************************
         SPECIFIC FILESTREAM IMPLEMENTATION FUNCTIONS
   *********************************************************************** */

static opbx_filestream_session_t *fsi_init( opbx_mpool_t *pool, opbx_filestream_session_t *session, char *uri )
{
    return NULL;
}

static filestream_result_value   fsi_findsuitablefile( const char *uri )
{
    return FS_RESULT_SUCCESS;
}

static struct opbx_frame *fsi_read( int *whennext )
{
    return NULL;
}

static filestream_result_value fsi_write( struct opbx_frame *frame )
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

#define FSI_NAME               "filestream_template"
#define FSI_DESC               "FileStream Template - Example."
#define FSI_STREAMTYPES        "example://"

static opbx_filestream_implementation_t sndfile_implementation = {
    FSI_NAME,
    FSI_DESC,
    FSI_STREAMTYPES,
    OPBX_FORMAT_SLINEAR,
    OPBX_FORMAT_SLINEAR,
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
    return opbx_filestream_register( &sndfile_implementation );
}

static int unload_module(void)
{
    return opbx_filestream_unregister( &sndfile_implementation );
}


MODULE_INFO(load_module, NULL, unload_module, NULL, FSI_DESC)
