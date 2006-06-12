AUTOMAKE_OPTS			= gnu

opbxmod_LTLIBRARIES 		=

if WANT_CHAN_FAX
opbxmod_LTLIBRARIES		+= chan_fax.la
chan_fax_la_SOURCES		= chan_fax.c faxmodem.c

chan_fax_la_CFLAGS		= $(AM_CFLAGS)
chan_fax_la_LIBADD		= ${top_builddir}/corelib/libopenpbx.la
chan_fax_la_LDFLAGS            = -module -avoid-version -no-undefined
endif WANT_CHAN_FAX

INCLUDES = -I${top_srcdir}/corelib -I$(top_srcdir)/include
