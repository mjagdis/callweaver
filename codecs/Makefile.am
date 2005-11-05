AUTOMAKE_OPTS		= gnu foreign

opbxmod_LTLIBRARIES	= codec_lpc10.la codec_adpcm.la \
			  codec_ulaw.la codec_alaw.la codec_a_mu.la \
			  codec_g726.la codec_gsm.la codec_ilbc.la 

codec_gsm_la_SOURCES	= codec_gsm.c slin_gsm_ex.h gsm_slin_ex.h
codec_gsm_la_CFLAGS     = $(AM_CFLAGS) -I${top_srcdir}/libs/gsm/inc
codec_gsm_la_LIBADD	= ${top_srcdir}/libs/gsm/libopbxgsm.la ${top_srcdir}/corelib/libopenpbx.la
codec_gsm_la_LDFLAGS	= -module -avoid-version -no-undefined
codec_lpc10_la_SOURCES	= codec_lpc10.c slin_lpc10_ex.h lpc10_slin_ex.h
codec_lpc10_la_CFLAGS   = $(AM_CFLAGS) -I${top_srcdir}/libs/lpc10
codec_lpc10_la_LIBADD	= ${top_srcdir}/libs/lpc10/libopbxlpc10.la ${top_srcdir}/corelib/libopenpbx.la -lm
codec_lpc10_la_LDFLAGS	= -module -avoid-version -no-undefined
codec_adpcm_la_SOURCES	= codec_adpcm.c slin_adpcm_ex.h adpcm_slin_ex.h
codec_adpcm_la_LDFLAGS	= -module -avoid-version -no-undefined
codec_adpcm_la_LIBADD	= ${top_srcdir}/corelib/libopenpbx.la 
codec_ulaw_la_SOURCES	= codec_ulaw.c slin_ulaw_ex.h ulaw_slin_ex.h
codec_ulaw_la_LDFLAGS	= -module -avoid-version -no-undefined
codec_ulaw_la_LIBADD	= ${top_srcdir}/corelib/libopenpbx.la 
codec_alaw_la_SOURCES	= codec_alaw.c slin_ulaw_ex.h ulaw_slin_ex.h
codec_alaw_la_LDFLAGS	= -module -avoid-version -no-undefined
codec_alaw_la_LIBADD	= ${top_srcdir}/corelib/libopenpbx.la 
codec_a_mu_la_SOURCES	= codec_a_mu.c ula_slin_ex.h
codec_a_mu_la_LDFLAGS	= -module -avoid-version -no-undefined
codec_a_mu_la_LIBADD	= ${top_srcdir}/corelib/libopenpbx.la 
codec_g726_la_SOURCES	= codec_g726.c g726_slin_ex.h slin_g726_ex.h
codec_g726_la_LDFLAGS	= -module -avoid-version -no-undefined
codec_g726_la_LIBADD	= ${top_srcdir}/corelib/libopenpbx.la
codec_ilbc_la_SOURCES	= codec_ilbc.c ilbc_slin_ex.h slin_ilbc_ex.h
codec_ilbc_la_CFLAGS    = $(AM_CFLAGS) -I${top_srcdir}/libs/ilbc
codec_ilbc_la_LDFLAGS	= -module -avoid-version -no-undefined
codec_ilbc_la_LIBADD	= ${top_srcdir}/libs/ilbc/libopbxilbc.la ${top_srcdir}/corelib/libopenpbx.la

if HAS_SPEEX
	opbxmod_LTLIBRARIES	+= codec_speex_la
	codec_speex_la_SOURCES	= codec_speex.c slin_speex_ex.h speex_slin_ex.h
	codec_speex_la_LIBADD	= @LIBSPEEX@ ${top_srcdir}/corelib/libopenpbx.la
	codec_speex_la_CFLAGS	= @SPEEX_HEADER@ $(AM_CFLAGS) -fPIC
	codec_speex_la_LDFLAGS  = -module -avoid-version -no-undefined
endif HAS_SPEEX
