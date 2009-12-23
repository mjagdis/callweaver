/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief CallWeaver internal frame definitions.
 */

#ifndef _CALLWEAVER_FRAME_H
#define _CALLWEAVER_FRAME_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <inttypes.h>
#include <string.h>

#include "callweaver/time.h"
#include "callweaver/utils.h"


struct cw_codec_pref
{
    char order[32];
};

/*! Data structure associated with a single frame of data */
/* A frame of data read used to communicate between 
   between channels and applications */
struct cw_frame
{
    /*! Kind of frame */
    int frametype;
    /*! Subclass, frame dependent */
    int subclass;
    /*! Length of data */
    int datalen;
    /*! Number of samples in this frame */
    int samples;
    /*! The sample rate of this frame (usually 8000 Hz */
    int samplerate;
    /*! Was the data malloc'd?  i.e. should we free it when we discard the frame? */
    int mallocd;
    /*! How many bytes exist _before_ "data" that can be used if needed */
    int offset;
    /*! Pointer to actual data */
    void *data;
    /*! Global delivery time */        
    struct timeval delivery;
    /*! Next/Prev for linking stand alone frames */
    struct cw_frame *prev;
    /*! Next/Prev for linking stand alone frames */
    struct cw_frame *next;
    /*! Timing data flag */
    int has_timing_info;
    /*! Timestamp in milliseconds */
    long ts;
    /*! Length in milliseconds */
    long duration;
    /*! Sequence number */
    int seq_no;
    /*! Number of copies to send (for redundant transmission of special data) */
    int tx_copies;
    /*! Allocated data space */
    uint8_t local_data[0];
};

#define CW_FRIENDLY_OFFSET    64        /*! It's polite for a a new frame to
                          have this number of bytes for additional
                          headers.  */
#define CW_MIN_OFFSET         32        /*! Make sure we keep at least this much handy */

/*! Need the header be free'd? */
#define CW_MALLOCD_HDR                 (1 << 0)
/*! Need the data be free'd? */
#define CW_MALLOCD_DATA                (1 << 1)
#define CW_MALLOCD_DATA_WITH_HDR       (1 << 2)

/* Frame types */
/*! A DTMF digit, subclass is the digit */
#define CW_FRAME_DTMF         1
/*! Voice data, subclass is CW_FORMAT_* */
#define CW_FRAME_VOICE        2
/*! Video frame, maybe?? :) */
#define CW_FRAME_VIDEO        3
/*! A control frame, subclass is CW_CONTROL_* */
#define CW_FRAME_CONTROL      4
/*! An empty frame. */
#define CW_FRAME_NULL         5
/*! Inter CallWeaver Exchange private frame type */
#define CW_FRAME_IAX          6
/*! Text messages */
#define CW_FRAME_TEXT         7
/*! Image Frames */
#define CW_FRAME_IMAGE        8
/*! HTML Frame */
#define CW_FRAME_HTML         9
/*! Comfort noise frame (subclass is level of CNG in -dBov), 
    body may include zero or more 8-bit reflection coefficients */
#define CW_FRAME_CNG          10
/*! T.38, V.150 or other modem-over-IP data stream */
#define CW_FRAME_MODEM        11

/* MODEM subclasses */
/*! T.38 Fax-over-IP */
#define CW_MODEM_T38          1
/*! V.150 Modem-over-IP */
#define CW_MODEM_V150         2

/* HTML subclasses */
/*! Sending a URL */
#define CW_HTML_URL           1
/*! Data frame */
#define CW_HTML_DATA          2
/*! Beginning frame */
#define CW_HTML_BEGIN         4
/*! End frame */
#define CW_HTML_END           8
/*! Load is complete */
#define CW_HTML_LDCOMPLETE    16
/*! Peer is unable to support HTML */
#define CW_HTML_NOSUPPORT     17
/*! Send URL, and track */
#define CW_HTML_LINKURL       18
/*! No more HTML linkage */
#define CW_HTML_UNLINK        19
/*! Reject link request */
#define CW_HTML_LINKREJECT    20

/* Data formats for capabilities and frames alike */
#define CW_AUDIO_CODEC_MASK   0xFFFF

/*! G.723.1 compression */
#define CW_FORMAT_G723_1      (1 << 0)
/*! GSM compression */
#define CW_FORMAT_GSM         (1 << 1)
/*! Raw mu-law data (G.711) */
#define CW_FORMAT_ULAW        (1 << 2)
/*! Raw A-law data (G.711) */
#define CW_FORMAT_ALAW        (1 << 3)
/*! G.726 ADPCM at 32kbps) */
#define CW_FORMAT_G726        (1 << 4)
/*! IMA/DVI/Intel ADPCM */
#define CW_FORMAT_DVI_ADPCM   (1 << 5)
/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
#define CW_FORMAT_SLINEAR     (1 << 6)
/*! LPC10, 180 samples/frame */
#define CW_FORMAT_LPC10       (1 << 7)
/*! G.729A audio */
#define CW_FORMAT_G729A       (1 << 8)
/*! SpeeX Free Compression */
#define CW_FORMAT_SPEEX       (1 << 9)
/*! iLBC Free Compression */
#define CW_FORMAT_ILBC        (1 << 10)
/*! Oki ADPCM */
#define CW_FORMAT_OKI_ADPCM   (1 << 11)
/*! G.722 */
#define CW_FORMAT_G722        (1 << 12)
/*! Maximum audio format */
#define CW_FORMAT_MAX_AUDIO   (1 << 15)
/*! JPEG Images */
#define CW_FORMAT_JPEG        (1 << 16)
/*! PNG Images */
#define CW_FORMAT_PNG         (1 << 17)
/*! H.261 Video */
#define CW_FORMAT_H261        (1 << 18)
/*! H.263 Video */
#define CW_FORMAT_H263        (1 << 19)
/*! H.263+ Video */
#define CW_FORMAT_H263_PLUS   (1 << 20)
/*! H.264 Video */
#define CW_FORMAT_H264        (1 << 21)
/*! Maximum video format */
#define CW_FORMAT_MAX_VIDEO   (1 << 24)

/* Control frame types */
/*! Stop playing indications */
#define CW_STATE_STOP_INDICATING  -1      
/*! Other end has hungup */
#define CW_CONTROL_HANGUP         1
/*! Local ring */
#define CW_CONTROL_RING           2
/*! Remote end is ringing */
#define CW_CONTROL_RINGING        3
/*! Remote end has answered */
#define CW_CONTROL_ANSWER         4
/*! Remote end is busy */
#define CW_CONTROL_BUSY           5
/*! Make it go off hook */
#define CW_CONTROL_TAKEOFFHOOK    6
/*! Line is off hook */
#define CW_CONTROL_OFFHOOK        7
/*! Congestion (circuits busy) */
#define CW_CONTROL_CONGESTION     8
/*! Flash hook */
#define CW_CONTROL_FLASH          9
/*! Wink */
#define CW_CONTROL_WINK           10
/*! Set a low-level option */
#define CW_CONTROL_OPTION         11
/*! Key Radio */
#define    CW_CONTROL_RADIO_KEY   12
/*! Un-Key Radio */
#define    CW_CONTROL_RADIO_UNKEY 13
/*! Indicate PROGRESS */
#define CW_CONTROL_PROGRESS       14
/*! Indicate CALL PROCEEDING */
#define CW_CONTROL_PROCEEDING     15
/*! Indicate call is placed on hold */
#define CW_CONTROL_HOLD           16
/*! Indicate call is left from hold */
#define CW_CONTROL_UNHOLD         17
/*! Indicate video frame update */
#define CW_CONTROL_VIDUPDATE      18

#define CW_SMOOTHER_FLAG_G729     (1 << 0)
#define CW_SMOOTHER_FLAG_BE       (1 << 1)

/* Option identifiers and flags */
#define CW_OPTION_FLAG_REQUEST    0
#define CW_OPTION_FLAG_ACCEPT     1
#define CW_OPTION_FLAG_REJECT     2
#define CW_OPTION_FLAG_QUERY      4
#define CW_OPTION_FLAG_ANSWER     5
#define CW_OPTION_FLAG_WTF        6

/* Verify touchtones by muting audio transmission 
    (and reception) and verify the tone is still present */
#define CW_OPTION_TONE_VERIFY     1        

/* Put a compatible channel into TDD (TTY for the hearing-impared) mode */
#define    CW_OPTION_TDD          2

/* Relax the parameters for DTMF reception (mainly for radio use) */
#define    CW_OPTION_RELAXDTMF    3

/* Set (or clear) Audio (Not-Clear) Mode */
#define    CW_OPTION_AUDIO_MODE   4

/* Set channel transmit gain */
/* Option data is a single signed char
   representing number of decibels (dB)
   to set gain to (on top of any gain
   specified in channel driver)
*/
#define CW_OPTION_TXGAIN          5

/* Set channel receive gain */
/* Option data is a single signed char
   representing number of decibels (dB)
   to set gain to (on top of any gain
   specified in channel driver)
*/
#define CW_OPTION_RXGAIN          6

/* Enable/disable echo cancellation */
#define CW_OPTION_ECHOCANCEL      7

/* Turn conference muting on/off */
#define CW_OPTION_MUTECONF        8


struct cw_option_header {
    /* Always keep in network byte order */
#if __BYTE_ORDER == __BIG_ENDIAN
        u_int16_t flag:3;
        u_int16_t option:13;
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN
        u_int16_t option:13;
        u_int16_t flag:3;
#else
#error Byte order not defined
#endif
#endif
        u_int8_t data[0];
};


/*! \brief Initialises a frame to a given type
 *
 * \param frame		frame to initialise
 * \param type		frame type
 * \param subtype	frame sub-type
 *
 * Initialise the contents of a frame.
 *
 * \sa cw_fr_init()
 *
 * \return Nothing
 */
static inline void cw_fr_init_ex(struct cw_frame *frame, int type, int subtype)
{
	memset(frame, 0, sizeof(struct cw_frame));
	frame->frametype = type;
	frame->subclass = subtype;
	frame->samplerate = 8000;
	frame->tx_copies = 1;
}


/*! \brief Initialises a frame to be null
 *
 * \param frame		frame to initialise
 *
 * Initialise the contents of a frame.
 *
 * \sa cw_fr_init_ex()
 *
 * \return Nothing
 */
static inline void cw_fr_init(struct cw_frame *frame)
{
	memset(frame, 0, sizeof(struct cw_frame));
	frame->frametype = CW_FRAME_NULL;
	frame->samplerate = 8000;
	frame->seq_no = -1;
	frame->tx_copies = 1;
}

/*! \brief Free a frame
 *
 * \param frame		frame to free
 *
 * Free a frame, releasing any memory that was allocated for it.
 *
 * \return Nothing
 */
static inline void cw_fr_free(struct cw_frame *frame)
{
	/* Most frames are not malloc'd at all */
	if (unlikely(frame->mallocd)) {
		/* If they are they are most likely the result of a cw_frisolate or
		 * a cw_fr_dup so the data is contiguous with the header.
		 */
		if (unlikely(frame->data && (frame->mallocd & CW_MALLOCD_DATA)))
			free(frame->data - frame->offset);

		if (likely((frame->mallocd & CW_MALLOCD_HDR)))
			free(frame);
	}
}


/*! \brief Isolate a frame
 *
 * \param frame		frame to isolate
 *
 * Take a frame, and replace any non-malloc'd header or data with malloc'd copies.
 * If you need to store frames for any reason you should call this function to ensure
 * that you are not storing pointers into space (such as stack space) that may change,
 * or disappear entirely, under you.
 *
 * Note: the frame pointer returned may or may not be the same as the frame pointer
 * passed. If the frame header has been moved into malloc'd space the returned
 * pointer WILL be different and the original frame WILL have been freed.
 *
 * \sa cw_frdup()
 *
 * \return Replacement frame on success, NULL on error
 */
extern CW_API_PUBLIC struct cw_frame *cw_frisolate(struct cw_frame *frame);


/*! \brief Duplicate a frame
 *
 * \param frame		frame to duplicate
 *
 * Duplicates a frame -- should only rarely be used, typically cw_frisolate is good enough
 *
 * \sa cw_frisolate()
 *
 * \return Duplicate frame on success, NULL on error
 */
extern CW_API_PUBLIC struct cw_frame *cw_frdup(struct cw_frame *frame);


extern CW_API_PUBLIC void cw_swapcopy_samples(void *dst, const void *src, int samples);

/* Helpers for byteswapping native samples to/from 
   little-endian and big-endian. */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define cw_frame_byteswap_le(fr) do { ; } while(0)
#define cw_frame_byteswap_be(fr) do { struct cw_frame *__f = (fr); cw_swapcopy_samples(__f->data, __f->data, __f->samples); } while(0)
#else
#define cw_frame_byteswap_le(fr) do { struct cw_frame *__f = (fr); cw_swapcopy_samples(__f->data, __f->data, __f->samples); } while(0)
#define cw_frame_byteswap_be(fr) do { ; } while(0)
#endif


/*! Get the name of a format */
/*!
 * \param format id of format
 * \return A static string containing the name of the format or "UNKN" if unknown.
 */
extern CW_API_PUBLIC char *cw_getformatname(int format);

/*! Get the names of a set of formats */
/*!
 * \param buf a buffer for the output string
 * \param n size of buf (bytes)
 * \param format the format (combined IDs of codecs)
 * Prints a list of readable codec names corresponding to "format".
 * ex: for format=CW_FORMAT_GSM|CW_FORMAT_SPEEX|CW_FORMAT_ILBC it will return "0x602 (GSM|SPEEX|ILBC)"
 * \return The return value is buf.
 */
extern CW_API_PUBLIC char *cw_getformatname_multiple(char *buf, size_t size, int format);

/*!
 * \param name string of format
 * Gets a format from a name.
 * This returns the form of the format in binary on success, 0 on error.
 */
extern CW_API_PUBLIC int cw_getformatbyname(const char *name);

/*! Get a name from a format */
/*!
 * \param codec codec number (1,2,4,8,16,etc.)
 * Gets a name from a format
 * This returns a static string identifying the format on success, 0 on error.
 */
extern CW_API_PUBLIC char *cw_codec2str(int codec);

struct cw_smoother;

extern CW_API_PUBLIC struct cw_smoother *cw_smoother_new(int bytes);
extern CW_API_PUBLIC void cw_smoother_set_flags(struct cw_smoother *smoother, int flags);
extern CW_API_PUBLIC int cw_smoother_test_flag(struct cw_smoother *s, int flag);
extern CW_API_PUBLIC int cw_smoother_get_flags(struct cw_smoother *smoother);
extern CW_API_PUBLIC void cw_smoother_free(struct cw_smoother *s);
extern CW_API_PUBLIC void cw_smoother_reset(struct cw_smoother *s, int bytes);
extern CW_API_PUBLIC int __cw_smoother_feed(struct cw_smoother *s, struct cw_frame *f, int swap);
extern CW_API_PUBLIC struct cw_frame *cw_smoother_read(struct cw_smoother *s);

extern CW_API_PUBLIC int cw_codec_sample_rate(struct cw_frame *f);

#define cw_smoother_feed(s,f) __cw_smoother_feed(s, f, 0)
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define cw_smoother_feed_be(s,f) __cw_smoother_feed(s, f, 1)
#define cw_smoother_feed_le(s,f) __cw_smoother_feed(s, f, 0)
#else
#define cw_smoother_feed_be(s,f) __cw_smoother_feed(s, f, 0)
#define cw_smoother_feed_le(s,f) __cw_smoother_feed(s, f, 1)
#endif

extern CW_API_PUBLIC void cw_frame_dump(const char *name, const struct cw_frame *f, const char *prefix);

/* Initialize a codec preference to "no preference" */
extern CW_API_PUBLIC void cw_codec_pref_init(struct cw_codec_pref *pref);

/* Codec located at  a particular place in the preference index */
extern CW_API_PUBLIC int cw_codec_pref_index(struct cw_codec_pref *pref, int index);

/* Remove a codec from a preference list */
extern CW_API_PUBLIC void cw_codec_pref_remove(struct cw_codec_pref *pref, int format);

/* Append a codec to a preference list, removing it first if it was already there */
extern CW_API_PUBLIC int cw_codec_pref_append(struct cw_codec_pref *pref, int format);

/* Select the best format according to preference list from supplied options. 
   If "find_best" is non-zero then if nothing is found, the "Best" format of 
   the format list is selected, otherwise 0 is returned. */
extern CW_API_PUBLIC int cw_codec_choose(struct cw_codec_pref *pref, int formats, int find_best);

/* Parse an "allow" or "deny" line and update the mask and pref if provided */
extern CW_API_PUBLIC void cw_parse_allow_disallow(struct cw_codec_pref *pref, int *mask, const char *list, int allowing);

/* Dump codec preference list into a string */
extern CW_API_PUBLIC int cw_codec_pref_string(struct cw_codec_pref *pref, char *buf, size_t size);

/* Shift a codec preference list up or down 65 bytes so that it becomes an ASCII string */
extern CW_API_PUBLIC void cw_codec_pref_convert(struct cw_codec_pref *pref, char *buf, size_t size, int right);

/* Returns the number of samples contained in the frame */
extern CW_API_PUBLIC int cw_codec_get_samples(struct cw_frame *f);

/* Returns the number of bytes for the number of samples of the given format */
extern CW_API_PUBLIC int cw_codec_get_len(int format, int samples);

/* Gets duration in ms of interpolation frame for a format */
static inline int cw_codec_interp_len(int format) 
{ 
    return (format == CW_FORMAT_ILBC) ? 30 : 20;
}

/*!
  \brief Adjusts the volume of the audio samples contained in a frame.
  \param f The frame containing the samples (must be CW_FRAME_VOICE and CW_FORMAT_SLINEAR)
  \param adjustment The number of dB to adjust up or down.
  \return 0 for success, non-zero for an error
 */
extern CW_API_PUBLIC int cw_frame_adjust_volume(struct cw_frame *f, int adjustment);

/*!
  \brief Sums two frames of audio samples.
  \param f1 The first frame (which will contain the result)
  \param f2 The second frame
  \return 0 for success, non-zero for an error

  The frames must be CW_FRAME_VOICE and must contain CW_FORMAT_SLINEAR samples,
  and must contain the same number of samples.
 */
extern CW_API_PUBLIC int cw_frame_slinear_sum(struct cw_frame *f1, struct cw_frame *f2);

extern CW_API_PUBLIC struct cw_frame cw_null_frame;

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_FRAME_H */
