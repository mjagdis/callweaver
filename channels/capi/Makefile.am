AUTOMAKE_OPTS			= gnu

DEFS += -include $(top_builddir)/include/confdefs.h

cwmod_LTLIBRARIES 		=

if WANT_CHAN_CAPI
cwmod_LTLIBRARIES  		+= chan_capi.la
chan_capi_la_SOURCES 		= chan_capi.c c20msg.c chan_capi_rtp.c
chan_capi_la_LIBADD  		= -lcapi20 @CALLWEAVER_LIB@
chan_capi_la_LDFLAGS 		= -module -avoid-version @NO_UNDEFINED@
endif WANT_CHAN_CAPI

INCLUDES = -I${top_srcdir}/corelib -I$(top_srcdir)/include
