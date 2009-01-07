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


#ifndef _CALLWEAVER_RESAMPLER_H
#define _CALLWEAVER_RESAMPLER_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "callweaver/mpool.h"
#include "callweaver/utils.h"


typedef struct cw_resampler_s cw_resampler_t;

extern cw_resampler_t *cw_resample_create( int from_rate, int to_rate, cw_mpool_t *usepool );

extern int cw_resample_destroy( cw_resampler_t *resampler );

extern uint32_t cw_resample_execute( 
                            cw_resampler_t *resampler, 
                            int16_t *from, 
                            uint32_t from_len, 
                            int16_t *to, 
                            uint32_t to_len );

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_RESAMPLER_H */
