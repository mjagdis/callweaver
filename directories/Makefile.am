AUTOMAKE_OPTS 		        = gnu

opbxmod_LTLIBRARIES             = direngine_embedded.la

direngine_embedded_la_SOURCES 	= direngine_embedded.c
direngine_embedded_la_LDFLAGS   = -module -avoid-version -no-undefined
direngine_embedded_la_LIBADD	= ${top_builddir}/corelib/libcallweaver.la  

INCLUDES = -I$(top_srcdir)/include
