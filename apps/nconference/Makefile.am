#
# CallWeaver.org
#
#
# $Id$
#
AUTOMAKE_OPTS                   = gnu

AM_CFLAGS			= @CW_CFLAGS@


cwmod_LTLIBRARIES             =

cwmod_LTLIBRARIES                     += app_nconference.la
app_nconference_la_SOURCES              = frame.c conference.c member.c sounds.c dtmf.c vad.c cli.c \
					  jitterbuffer.c \
					  app_nconference.c 
app_nconference_la_LDFLAGS              = -module -avoid-version @NO_UNDEFINED@
app_nconference_la_LIBADD               = @CALLWEAVER_LIB@
app_nconference_la_CFLAGS		= $(AM_CFLAGS) 

INCLUDES = -I$(top_builddir)/include -I$(top_srcdir)/include
