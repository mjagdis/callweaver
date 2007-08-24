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
 * \brief Audio resampler interface
 */

#include "values.h"

#include "callweaver/resample.h"
#include "resample/callweaver_resample.h"


#define RESAMPLE_QUALITY        0

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MAXINT
#define MAXINT 32767
#endif

#ifndef MININT
#define MININT -32767
#endif




struct opbx_resampler_s {
    int                         rate_from;
    int                         rate_to;

    double                      ratio;

    float                       *buffer_in;
    float                       *buffer_out;

    uint32_t                    buffer_in_len;
    uint32_t                    buffer_out_len;

    opbx_core_resampler_t       *resampler;
    opbx_mpool_t                *pool;
    int                         pool_is_mine;
};

/*
    HELPER FUNCTIONS 
*/


static void float_to_int16t ( uint32_t len, float *in, int16_t *out) {
    int t;
    float f;

    for ( t=0; t<len; t++ ) {
        f = in[t] * (float) MAXINT;
        if ( f > MAXINT ) f = MAXINT;
        if ( f < MININT ) f = MAXINT;
        out[t] = f;
    }
}

static void int16t_to_float ( uint32_t len, int16_t *in, float *out) {
    int t;
    float f;
    
    for ( t=0; t<len; t++ ) {
        f = (float) in[t] / MAXINT;        
        out[t] = f;
    }
}

/*
    INTERFACE FUNCTIONS 
*/

opbx_resampler_t *opbx_resample_create( int from_rate, int to_rate, opbx_mpool_t *usepool ) {
    int                 pool_flags = 0,
                        pool_ret;
    opbx_resampler_t    *resampler;
    opbx_mpool_t        *pool = NULL;

    if ( from_rate == 0 ) {
        return NULL;
    }

    if ( !usepool ) {
        pool = opbx_mpool_open(pool_flags, 0, NULL, &pool_ret);
        //check pool exists
    }
    else
        pool = usepool;

    resampler = opbx_mpool_alloc( pool, sizeof(opbx_resampler_t), &pool_ret );  //TODO check pool_ret

    if ( !resampler ) {
        if ( !usepool )
            opbx_mpool_close(pool);
        return NULL;
    }

    resampler->pool = pool;

    if ( !usepool )
        resampler->pool_is_mine = 1;

    resampler->rate_from = from_rate;
    resampler->rate_to = to_rate;
    
    resampler->ratio = (double) to_rate / (double) from_rate;

    resampler->resampler = resample_open(RESAMPLE_QUALITY, resampler->ratio, resampler->ratio );

    return resampler;
}

uint32_t opbx_resample_execute( 
                            opbx_resampler_t *resampler, 
                            int16_t *from, 
                            uint32_t from_len, 
                            int16_t *to, 
                            uint32_t to_len ) 
{

    int pool_err;

    if ( !resampler && !resampler->pool ) {
        opbx_log(OPBX_LOG_ERROR, "Cannot resample without a memory pool.\n");
        return 0;                
    }
    
    /* 
        Check the input buffer. It it doesn't exists, then create. 
        If it exists and it is too small, then realloc.
     */

    if ( resampler->buffer_in && ( resampler->buffer_in_len < from_len ) ) {
        opbx_log(OPBX_LOG_ERROR, "Resizing the input buffer.\n");

        resampler->buffer_in = opbx_mpool_resize( 
                                    resampler->pool, 
                                    resampler->buffer_in, 
                                    resampler->buffer_in_len, from_len * sizeof(float),
                                    &pool_err );
        if ( !resampler->buffer_in ) {
            opbx_log(OPBX_LOG_ERROR,"Cannot resample input buffer.\n");
            return 0;
        }
        resampler->buffer_in_len = from_len * sizeof(float);
    }

    if ( !resampler->buffer_in ) {
        // create it
        resampler->buffer_in = opbx_mpool_alloc( resampler->pool, from_len * sizeof(float), &pool_err );
        if ( !resampler->buffer_in ) {
            opbx_log(OPBX_LOG_ERROR,"Cannot create input buffer.\n");
            return 0;
        }
        resampler->buffer_in_len = from_len * sizeof(float);        
    }

    /* The same applies to our output buffer */

    if ( resampler->buffer_out && ( resampler->buffer_out_len < to_len ) ) {
        opbx_log(OPBX_LOG_ERROR, "Resizing the output buffer.\n");

        resampler->buffer_out = opbx_mpool_resize( 
                                    resampler->pool, 
                                    resampler->buffer_out, 
                                    resampler->buffer_out_len, to_len * sizeof(float),
                                    &pool_err );
        if ( !resampler->buffer_out ) {
            opbx_log(OPBX_LOG_ERROR,"Cannot resample output buffer.\n");
            return 0;
        }
        resampler->buffer_in_len = from_len * sizeof(float);
    }

    if ( !resampler->buffer_out ) {
        // create it
        resampler->buffer_out = opbx_mpool_alloc( resampler->pool, to_len * sizeof(float), &pool_err );
        if ( !resampler->buffer_out ) {
            opbx_log(OPBX_LOG_ERROR,"Cannot create output buffer.\n");
            return 0;
        }
        resampler->buffer_out_len = to_len * sizeof(float);        
    }

    /* Convert slinear to float */

    int16t_to_float ( from_len, from, resampler->buffer_in);

    // copy from to buffer_in, converting to float.
    // Do the resample

    int tot, used;
    tot = resample_process(
                            resampler->resampler, 
                            resampler->ratio, 
                            resampler->buffer_in, 
                            resampler->buffer_in_len,
                            1,
                            &used,
                            resampler->buffer_out,
                            resampler->buffer_out_len
                          );

    /* Convert the resampled float buffer to slinear */

    float_to_int16t ( MIN(tot,to_len), resampler->buffer_out, to);

    if ( tot!=to_len ) {
        opbx_log(OPBX_LOG_WARNING,"Uh... weird... resampling buffer != to buffer ( %d != %d )\n", tot,to_len);
    }

    return MIN(tot,to_len);
}

int opbx_resample_destroy( opbx_resampler_t *resampler ) {

    resample_close(resampler->resampler);

    if ( resampler->pool_is_mine )
        opbx_mpool_close(resampler->pool);

    return 0;
}

