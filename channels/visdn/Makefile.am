AUTOMAKE_OPTS			= gnu

DEFS += -include $(top_builddir)/include/confdefs.h

cwmod_LTLIBRARIES 		=

if WANT_CHAN_VISDN

cwmod_LTLIBRARIES		+= chan_visdn.la

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

chan_visdn_la_LIBADD		= -lq931 ${top_builddir}/corelib/libcallweaver.la
chan_visdn_la_LDFLAGS = -module -avoid-version @NO_UNDEFINED@

endif WANT_CHAN_VISDN

INCLUDES = -I$(srcdir)/include -I${top_srcdir}/corelib -I$(top_srcdir)/include

