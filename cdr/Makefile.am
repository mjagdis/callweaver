AUTOMAKE_OPTS	= gnu
opbxmod_LTLIBRARIES	= cdr_csv.la cdr_custom.la

cdr_csv_la_SOURCES	= cdr_csv.c
cdr_custom_la_SOURCES	= cdr_custom.c

cdr_csv_la_LDFLAGS 	= -module -avoid-version -no-undefined
cdr_custom_la_LDFLAGS	= -module -avoid-version -no-undefined
cdr_csv_la_LIBADD	= ${top_srcdir}/corelib/libopenpbx.la
cdr_custom_la_LIBADD	= ${top_srcdir}/corelib/libopenpbx.la

if WANT_CDR_PGSQL
opbxmod_LTLIBRARIES	+= cdr_pgsql.la
cdr_pgsql_la_SOURCES	= cdr_pgsql.c
cdr_pgsql_la_CFLAGS	= $(AM_CFLAGS) @PQINCPATH@ 
cdr_pgsql_la_LDFLAGS	= -module -avoid-version -no-undefined
cdr_pgsql_la_LIBADD	= @PQLIBPATH@ -lpq ${top_srcdir}/corelib/libopenpbx.la
endif WANT_CDR_PGSQL
