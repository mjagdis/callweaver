AUTOMAKE_OPTS 		= gnu

AM_CFLAGS			= @CW_CFLAGS@


cwmod_LTLIBRARIES = format_au.la format_g723_1.la format_g726.la	\
format_g729.la format_gsm.la format_h263.la format_pcm.la		\
format_pcm_alaw.la format_sln.la format_wav.la format_wav_gsm.la

format_au_la_SOURCES 		= format_au.c
format_au_la_LDFLAGS            = -module -avoid-version @NO_UNDEFINED@
format_au_la_LIBADD		= @CALLWEAVER_LIB@  

format_g723_1_la_SOURCES 	= format_g723_1.c
format_g723_1_la_LDFLAGS        = -module -avoid-version @NO_UNDEFINED@
format_g723_1_la_LIBADD           = @CALLWEAVER_LIB@  

format_g726_la_SOURCES 		= format_g726.c
format_g726_la_LDFLAGS          = -module -avoid-version @NO_UNDEFINED@
format_g726_la_LIBADD           = @CALLWEAVER_LIB@  

format_g729_la_SOURCES 		= format_g729.c
format_g729_la_LDFLAGS          = -module -avoid-version @NO_UNDEFINED@
format_g729_la_LIBADD		= @CALLWEAVER_LIB@

format_gsm_la_SOURCES 		= format_gsm.c
format_gsm_la_LDFLAGS           = -module -avoid-version @NO_UNDEFINED@
format_gsm_la_LIBADD		= @CALLWEAVER_LIB@ 

format_h263_la_SOURCES 		= format_h263.c
format_h263_la_LDFLAGS          = -module -avoid-version @NO_UNDEFINED@
format_h263_la_LIBADD           = @CALLWEAVER_LIB@  

if WANT_FORMAT_JPEG
cwmod_LTLIBRARIES		+= format_jpeg.la
format_jpeg_la_SOURCES 		= format_jpeg.c
format_jpeg_la_LDFLAGS          = -module -avoid-version @NO_UNDEFINED@
format_jpeg_la_LIBADD           = @CALLWEAVER_LIB@  
endif WANT_FORMAT_JPEG

if WANT_FORMAT_OGGVORBIS
cwmod_LTLIBRARIES		+= format_ogg_vorbis.la
format_ogg_vorbis_la_SOURCES 	= format_ogg_vorbis.c
format_ogg_vorbis_la_LDFLAGS     = -module -avoid-version @NO_UNDEFINED@
format_ogg_vorbis_la_LIBADD      = @CALLWEAVER_LIB@ @OGG_LIBS@ @VORBIS_LIBS@
endif WANT_FORMAT_OGGVORBIS

format_pcm_alaw_la_SOURCES 	= format_pcm_alaw.c
format_pcm_alaw_la_LDFLAGS      = -module -avoid-version @NO_UNDEFINED@
format_pcm_alaw_la_LIBADD	= @CALLWEAVER_LIB@  

format_pcm_la_SOURCES 		= format_pcm.c
format_pcm_la_LDFLAGS           = -module -avoid-version @NO_UNDEFINED@
format_pcm_la_LIBADD		= @CALLWEAVER_LIB@  

format_sln_la_SOURCES 		= format_sln.c
format_sln_la_LDFLAGS           = -module -avoid-version @NO_UNDEFINED@
format_sln_la_LIBADD		= @CALLWEAVER_LIB@  

format_wav_gsm_la_SOURCES 	= format_wav_gsm.c
format_wav_gsm_la_LDFLAGS       = -module -avoid-version @NO_UNDEFINED@
format_wav_gsm_la_LIBADD	= @CALLWEAVER_LIB@  

format_wav_la_SOURCES 		= format_wav.c
format_wav_la_LDFLAGS           = -module -avoid-version @NO_UNDEFINED@
format_wav_la_LIBADD		= @CALLWEAVER_LIB@  

INCLUDES = -I$(top_builddir)/include -I$(top_srcdir)/include
