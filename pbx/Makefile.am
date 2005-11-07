AUTOCONF_OPTS 		= gnu

opbxmod_LTLIBRARIES 	= pbx_spool.la pbx_dundi.la pbx_loopback.la pbx_config.la pbx_realtime.la

pbx_config_la_SOURCES 	= pbx_config.c
pbx_config_la_LDFLAGS	= -module -avoid-version -no-undefined
pbx_config_la_LIBADD	= ${top_srcdir}/corelib/libopenpbx.la
pbx_spool_la_SOURCES 	= pbx_spool.c
pbx_spool_la_LDFLAGS	= -module -avoid-version -no-undefined
pbx_spool_la_LIBADD    = ${top_srcdir}/corelib/libopenpbx.la
pbx_dundi_la_SOURCES 	= pbx_dundi.c dundi-parser.c dundi-parser.h
pbx_dundi_la_LIBADD 	= -lz ${top_srcdir}/corelib/libopenpbx.la
pbx_dundi_la_LDFLAGS 	= -module -avoid-version -no-undefined
pbx_loopback_la_SOURCES = pbx_loopback.c
pbx_loopback_la_LDFLAGS = -module -avoid-version -no-undefined
pbx_loopback_la_LIBADD	= ${top_srcdir}/corelib/libopenpbx.la
pbx_realtime_la_SOURCES = pbx_realtime.c
pbx_realtime_la_LDFLAGS = -module -avoid-version -no-undefined
pbx_realtime_la_LIBADD  = ${top_srcdir}/corelib/libopenpbx.la

