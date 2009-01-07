AUTOMAKE_OPTS		= gnu foreign

DEFS += -include $(top_builddir)/include/global.h

cwmod_LTLIBRARIES	= codec_lpc10.la codec_dvi_adpcm.la codec_oki_adpcm.la \
			  codec_ulaw.la codec_alaw.la codec_a_mu.la \
			  codec_g722.la codec_g722_16k_8k.la codec_g726_32kbps.la \
			  codec_gsm.la

codec_gsm_la_SOURCES	= codec_gsm.c
codec_gsm_la_CFLAGS     = $(AM_CFLAGS)
codec_gsm_la_LIBADD		= -lspandsp -ltiff @CALLWEAVER_LIB@
codec_gsm_la_LDFLAGS	= -module -avoid-version @NO_UNDEFINED@
codec_lpc10_la_SOURCES	= codec_lpc10.c
codec_lpc10_la_CFLAGS   = $(AM_CFLAGS) -I${top_srcdir}/libs/lpc10
codec_lpc10_la_LIBADD	= -lspandsp -ltiff @CALLWEAVER_LIB@ -lm
codec_lpc10_la_LDFLAGS	= -module -avoid-version @NO_UNDEFINED@
codec_dvi_adpcm_la_SOURCES	= codec_dvi_adpcm.c
codec_dvi_adpcm_la_LDFLAGS	= -module -avoid-version @NO_UNDEFINED@
codec_dvi_adpcm_la_LIBADD	= -lspandsp -ltiff @CALLWEAVER_LIB@ 
codec_oki_adpcm_la_SOURCES	= codec_oki_adpcm.c
codec_oki_adpcm_la_LDFLAGS	= -module -avoid-version @NO_UNDEFINED@
codec_oki_adpcm_la_LIBADD	= -lspandsp -ltiff @CALLWEAVER_LIB@ 
codec_ulaw_la_SOURCES	= codec_ulaw.c
codec_ulaw_la_LDFLAGS	= -module -avoid-version @NO_UNDEFINED@
codec_ulaw_la_LIBADD	= -lspandsp -ltiff @CALLWEAVER_LIB@ 
codec_alaw_la_SOURCES	= codec_alaw.c
codec_alaw_la_LDFLAGS	= -module -avoid-version @NO_UNDEFINED@
codec_alaw_la_LIBADD	= -lspandsp -ltiff @CALLWEAVER_LIB@ 
codec_a_mu_la_SOURCES	= codec_a_mu.c
codec_a_mu_la_LDFLAGS	= -module -avoid-version @NO_UNDEFINED@
codec_a_mu_la_LIBADD	= -lspandsp -ltiff @CALLWEAVER_LIB@ 
codec_g722_la_SOURCES   = codec_g722.c
codec_g722_la_LDFLAGS	= -module -avoid-version @NO_UNDEFINED@
codec_g722_la_LIBADD	= -lspandsp -ltiff @CALLWEAVER_LIB@
codec_g722_16k_8k_la_SOURCES   = codec_g722_16k_8k.c
codec_g722_16k_8k_la_LDFLAGS	= -module -avoid-version @NO_UNDEFINED@
codec_g722_16k_8k_la_LIBADD	= -lspandsp -ltiff @CALLWEAVER_LIB@
codec_g726_32kbps_la_SOURCES   = codec_g726_32kbps.c
codec_g726_32kbps_la_LDFLAGS	= -module -avoid-version @NO_UNDEFINED@
codec_g726_32kbps_la_LIBADD	= -lspandsp -ltiff @CALLWEAVER_LIB@

if WANT_CODEC_SPEEX
cwmod_LTLIBRARIES	+= codec_speex.la
codec_speex_la_SOURCES	= codec_speex.c
codec_speex_la_LIBADD	= -lspeex @CALLWEAVER_LIB@
codec_speex_la_CFLAGS	= $(AM_CFLAGS) @SPEEX_CFLAGS@
codec_speex_la_LDFLAGS  = -module -avoid-version @NO_UNDEFINED@
endif WANT_CODEC_SPEEX

INCLUDES = -I$(top_builddir)/include -I$(top_srcdir)/include
