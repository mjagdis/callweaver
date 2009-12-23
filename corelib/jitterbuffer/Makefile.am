#
# CallWeaver.org
#
#
# $Id$
#
AUTOMAKE_OPTS = gnu

AM_CFLAGS			= @CW_CFLAGS@


noinst_LTLIBRARIES = libcwjb.la

libcwjb_la_SOURCES = jitterbuf_scx.c jitterbuf_stevek.c generic_jb.c \
			jitterbuf_speakup.c

libcwjb_la_CFLAGS = $(AM_CFLAGS) -DCW_API_IMPLEMENTATION
libcwjb_la_LDFLAGS = @NO_UNDEFINED@

INCLUDES = -I$(top_builddir)/include -I${top_srcdir}/corelib -I$(top_srcdir)/include
