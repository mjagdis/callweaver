AUTOMAKE_OPTS			= gnu

opbxmod_LTLIBRARIES 		=

if WANT_CHAN_VISDN

opbxmod_LTLIBRARIES		+= chan_visdn.la

chan_visdn_la_SOURCES = \
	chan_visdn.c	\
	intf.c		\
	huntgroup.c	\
	ton.c		\
	overlap.c	\
	disconnect.c	\
	numbers_list.c
		
noinst_HEADERS = \
	chan_visdn.h	\
	intf.h		\
	huntgroup.h	\
	ton.h		\
	overlap.h	\
	disconnect.h	\
	numbers_list.h	\
	util.h		\
	$(srcdir)/include/list.h

chan_visdn_la_LIBADD		= -lq931 ${top_builddir}/corelib/libopenpbx.la
chan_visdn_la_LDFLAGS = -module -avoid-version -no-undefined

endif WANT_CHAN_VISDN

INCLUDES = -I$(srcdir)/include -I${top_srcdir}/corelib -I$(top_srcdir)/include

