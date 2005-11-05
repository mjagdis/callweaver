AUTOMAKE_OPTS	= gnu
opbxmod_LTLIBRARIES	= cdr_csv.la cdr_custom.la

cdr_csv_la_SOURCES	= cdr_csv.c
cdr_custom_la_SOURCES	= cdr_custom.c

cdr_csv_la_LDFLAGS 	= -module -avoid-version -no-undefined
cdr_custom_la_LDFLAGS	= -module -avoid-version -no-undefined
cdr_csv_la_LIBADD	= ${top_srcdir}/corelib/libopenpbx.la
cdr_custom_la_LIBADD	= ${top_srcdir}/corelib/libopenpbx.la
