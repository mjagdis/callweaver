
DEFS += -include $(top_builddir)/include/global.h

noinst_LTLIBRARIES             = libcwresample.la

libcwresample_la_SOURCES        = \
                                    filterkit.c \
                                    resample.c \
                                    resamplesubs.c

libcwresample_la_CFLAGS = -DCW_API_IMPLEMENTATION
libcwresample_la_LDFLAGS = @NO_UNDEFINED@

INCLUDES = -I$(top_builddir)/include -I${top_srcdir}/corelib -I$(top_srcdir)/include

