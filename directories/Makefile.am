AUTOMAKE_OPTS 		        = gnu

cwmod_LTLIBRARIES             = direngine_embedded.la

direngine_embedded_la_SOURCES 	= direngine_embedded.c
direngine_embedded_la_LDFLAGS   = -module -avoid-version @NO_UNDEFINED@
direngine_embedded_la_LIBADD	= @CALLWEAVER_LIB@  

INCLUDES = -I$(top_srcdir)/include
