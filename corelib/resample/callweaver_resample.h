/**********************************************************************

  resample.h

  Real-time library interface by Dominic Mazzoni

  Based on resample-1.7:
    http://www-ccrma.stanford.edu/~jos/resample/

  License: LGPL - see the file LICENSE.txt for more information

**********************************************************************/

#ifndef LIBRESAMPLE_INCLUDED
#define LIBRESAMPLE_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */


typedef void cw_core_resampler_t;

cw_core_resampler_t *resample_open(int      highQuality,
                    double   minFactor,
                    double   maxFactor);

cw_core_resampler_t *resample_dup(const cw_core_resampler_t *handle);

int resample_get_filter_width(const cw_core_resampler_t *handle);

int resample_process(cw_core_resampler_t   *handle,
                     double  factor,
                     float  *inBuffer,
                     int     inBufferLen,
                     int     lastFlag,
                     int    *inBufferUsed,
                     float  *outBuffer,
                     int     outBufferLen);

void resample_close(cw_core_resampler_t *handle);

#ifdef __cplusplus
}		/* extern "C" */
#endif	/* __cplusplus */

#endif /* LIBRESAMPLE_INCLUDED */
