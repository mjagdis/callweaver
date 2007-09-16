#
# CallWeaver.org
#
#
# $Id$
#
AUTOMAKE_OPTS			= gnu

DEFS += -include $(top_builddir)/include/confdefs.h

opbxmod_LTLIBRARIES 		=

if WANT_CHAN_IAX2

opbxmod_LTLIBRARIES		+= chan_iax2.la
chan_iax2_la_SOURCES            = chan_iax2.c iax2-parser.h iax2-parser.c iax2-provision.h iax2-provision.c iax2.h
chan_iax2_la_CFLAGS		= $(AM_CFLAGS) -I${top_srcdir}/corelib
chan_iax2_la_LDFLAGS 		= -module -avoid-version @NO_UNDEFINED@
chan_iax2_la_LIBADD             = @CALLWEAVER_LIB@

INCLUDES = -I${top_srcdir}/corelib -I$(top_srcdir)/include

endif WANT_CHAN_IAX2
