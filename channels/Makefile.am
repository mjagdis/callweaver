AUTOMAKE_OPTS			= gnu

opbxmod_LTLIBRARIES 		= chan_local.la chan_features.la chan_mgcp.la chan_sip.la chan_iax2.la chan_agent.la

chan_sip_la_SOURCES 		= chan_sip.c
chan_sip_la_LDFLAGS 		= -module -avoid-version -no-undefined
chan_sip_la_LIBADD  		= ${top_srcdir}/corelib/libopenpbx.la  
chan_iax2_la_SOURCES 		= chan_iax2.c iax2-parser.c iax2-provision.c iax2.h
chan_iax2_la_LDFLAGS 		= -module -avoid-version -no-undefined
chan_iax2_la_LIBADD             = ${top_srcdir}/corelib/libopenpbx.la   
chan_mgcp_la_SOURCES 		= chan_mgcp.c
chan_mgcp_la_LDFLAGS 		= -module -avoid-version -no-undefined
chan_mgcp_la_LIBADD             = ${top_srcdir}/corelib/libopenpbx.la   
chan_local_la_SOURCES 		= chan_local.c
chan_local_la_LDFLAGS 		= -module -avoid-version -no-undefined
chan_local_la_LIBADD            = ${top_srcdir}/corelib/libopenpbx.la   
chan_features_la_SOURCES  	= chan_features.c
chan_features_la_LDFLAGS 	= -module -avoid-version -no-undefined
chan_features_la_LIBADD         = ${top_srcdir}/corelib/libopenpbx.la   
chan_agent_la_SOURCES		= chan_agent.c
chan_agent_la_LDFLAGS		= -module -avoid-version -no-undefined
chan_agent_la_LIBADD		= ${top_srcdir}/corelib/libopenpbx.la  

if CHAN_EXOSIP2
	opbxmod_LTLIBRARIES     += chan_exosip2.la
	chan_exosip2_la_SOURCES = chan_exosip2.c
	chan_exosip2_la_LIBADD  = @EXOLIBS@ ${top_srcdir}/corelib/libopenpbx.la
	chan_exosip2_la_LDFLAGS = -module -avoid-version -no-undefined
endif CHAN_EXOSIP2 

if WANT_CHAN_UNICALL
	opbxmod_LTLIBRARIES     += chan_unicall.la
	chan_unicall_la_SOURCES = chan_unicall.c
	chan_unicall_la_LIBADD  = -lunicall ${top_srcdir}/corelib/libopenpbx.la
	chan_unicall_la_LDFLAGS = -module -avoid-version -no-undefined
endif WANT_CHAN_UNICALL

if HAVE_CAPI
opbxmod_LTLIBRARIES  += chan_capi.la
chan_capi_la_SOURCES = chan_capi.c c20msg.c
chan_capi_la_LIBADD  = ${top_srcdir}/corelib/libopenpbx.la -lcapi20
chan_capi_la_LDFLAGS = -module -avoid-version -no-undefined
endif HAVE_CAPI

INCLUDES = -I${top_srcdir}/corelib
