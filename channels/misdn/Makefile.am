AUTOMAKE_OPTS			= gnu

AM_CFLAGS			= @CW_CFLAGS@


cwmod_LTLIBRARIES 		=

if WANT_CHAN_MISDN
cwmod_LTLIBRARIES		+= chan_misdn.la
chan_misdn_la_SOURCES		= chan_misdn.c chan_misdn_config.h asn1.c \
				  asn1.h fac.c fac.h isdn_lib.c isdn_lib.h \
				  isdn_lib_intern.h isdn_msg_parser.c misdn_config.c


chan_misdn_la_CFLAGS		= $(AM_CFLAGS) -Wno-error @MISDN_CFLAGS@ -D_GNU_SOURCE -DCHAN_MISDN_VERSION=\"0.4.0-rc28\"
chan_misdn_la_LIBADD		= @MISDN_LDFLAGS@ -lisdnnet -lmISDN @CALLWEAVER_LIB@
chan_misdn_la_LDFLAGS            = -module -avoid-version @NO_UNDEFINED@
endif WANT_CHAN_MISDN

INCLUDES = -I$(top_builddir)/include -I${top_srcdir}/corelib -I$(top_srcdir)/include
