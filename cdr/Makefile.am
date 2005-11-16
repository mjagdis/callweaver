AUTOMAKE_OPTS	= gnu
opbxmod_LTLIBRARIES	= cdr_csv.la cdr_custom.la

cdr_csv_la_SOURCES	= cdr_csv.c
cdr_csv_la_LDFLAGS 	= -module -avoid-version -no-undefined
cdr_csv_la_LIBADD	= ${top_builddir}/corelib/libopenpbx.la

cdr_custom_la_SOURCES	= cdr_custom.c
cdr_custom_la_LDFLAGS	= -module -avoid-version -no-undefined
cdr_custom_la_LIBADD	= ${top_builddir}/corelib/libopenpbx.la

if WANT_CDR_ODBC
opbxmod_LTLIBRARIES	+= cdr_odbc.la
cdr_odbc_la_SOURCES	= cdr_odbc.c
cdr_odbc_la_CFLAGS	= $(AM_CFLAGS) @ODBC_CFLAGS@
cdr_odbc_la_LDFLAGS	= -module -avoid-version -no-undefined
cdr_odbc_la_LIBADD	= @ODBC_LIBS@ ${top_builddir}/corelib/libopenpbx.la
endif WANT_CDR_ODBC

if WANT_CDR_PGSQL
opbxmod_LTLIBRARIES	+= cdr_pgsql.la
cdr_pgsql_la_SOURCES	= cdr_pgsql.c
cdr_pgsql_la_CFLAGS	= $(AM_CFLAGS) @PQINCPATH@ 
cdr_pgsql_la_LDFLAGS	= -module -avoid-version -no-undefined
cdr_pgsql_la_LIBADD	= @PQLIBPATH@ -lpq ${top_builddir}/corelib/libopenpbx.la
endif WANT_CDR_PGSQL

INCLUDES = -I$(top_srcdir)/include
