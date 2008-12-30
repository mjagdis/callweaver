AUTOMAKE_OPTS 		        = gnu

cwmod_LTLIBRARIES             = filestream_sndfile.la



if WANT_FILESTREAM_SNDFILE
filestream_sndfile_la_SOURCES 	= filestream_sndfile.c
filestream_sndfile_la_LDFLAGS   = -module -avoid-version @NO_UNDEFINED@
filestream_sndfile_la_LIBADD	= @CALLWEAVER_LIB@  
endif WANT_FILESTREAM_SNDFILE

INCLUDES = -I$(top_builddir)/include -I$(top_srcdir)/include

