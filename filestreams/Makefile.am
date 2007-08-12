AUTOMAKE_OPTS 		        = gnu

opbxmod_LTLIBRARIES             = filestream_sndfile.la



if WANT_FILESTREAM_SNDFILE
filestream_sndfile_la_SOURCES 	= filestream_sndfile.c
filestream_sndfile_la_LDFLAGS   = -module -avoid-version -no-undefined
filestream_sndfile_la_LIBADD	= ${top_builddir}/corelib/libcallweaver.la  
endif WANT_FILESTREAM_SNDFILE

INCLUDES = -I$(top_srcdir)/include

