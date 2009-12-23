AM_OPTIONS = gnu

AM_CFLAGS			= @CW_CFLAGS@


if FALSE
noinst_PROGRAMS = test

test_SOURCES = test.c
test_LDADD = @CALLWEAVER_LIB@
endif FALSE

noinst_LTLIBRARIES = libtime.la 

libtime_la_SOURCES = localtime.c tzfile.h private.h
libtime_la_CFLAGS = $(AM_CFLAGS) -DCW_API_IMPLEMENTATION
libtime_la_LDFLAGS = @NO_UNDEFINED@ -avoid-version 

INCLUDES = -I$(top_builddir)/include -I$(top_srcdir)/include

#-static
