AUTOMAKE_OPTS			= gnu

opbxmod_LTLIBRARIES 		=

if WANT_CHAN_AGENT
opbxmod_LTLIBRARIES     	+= chan_agent.la
chan_agent_la_SOURCES		= chan_agent.c
chan_agent_la_LDFLAGS		= -module -avoid-version -no-undefined
chan_agent_la_LIBADD		= ${top_builddir}/corelib/libopenpbx.la  
endif WANT_CHAN_AGENT

if WANT_CHAN_CAPI
opbxmod_LTLIBRARIES  		+= chan_capi.la
chan_capi_la_SOURCES 		= chan_capi.c c20msg.c
chan_capi_la_LIBADD  		= -lcapi20 ${top_builddir}/corelib/libopenpbx.la
chan_capi_la_LDFLAGS 		= -module -avoid-version -no-undefined
endif WANT_CHAN_CAPI

if WANT_CHAN_EXOSIP2
opbxmod_LTLIBRARIES     	+= chan_exosip2.la
chan_exosip2_la_SOURCES 	= chan_exosip2.c
chan_exosip2_la_CFLAGS  	= $(AM_CFLAGS) @CHAN_EXOSIP2_CFLAGS@
chan_exosip2_la_LIBADD  	= @CHAN_EXOSIP2_LIBS@ ${top_builddir}/corelib/libopenpbx.la
chan_exosip2_la_LDFLAGS 	= -module -avoid-version -no-undefined
endif WANT_CHAN_EXOSIP2 

if WANT_CHAN_FAX
opbxmod_LTLIBRARIES		+= chan_fax.la
chan_fax_la_SOURCES		= chan_fax.c
chan_fax_la_LIBADD		= -lspandsp -ltiff ${top_builddir}/corelib/libopenpbx.la
chan_fax_la_LDFLAGS		= -module -avoid-version -no-undefined
endif WANT_CHAN_FAX

if WANT_CHAN_FEATURES
opbxmod_LTLIBRARIES		+= chan_features.la
chan_features_la_SOURCES  	= chan_features.c
chan_features_la_LDFLAGS 	= -module -avoid-version -no-undefined
chan_features_la_LIBADD         = ${top_builddir}/corelib/libopenpbx.la   
endif WANT_CHAN_FEATURES

if WANT_CHAN_IAX2
opbxmod_LTLIBRARIES		+= chan_iax2.la
chan_iax2_la_SOURCES            = chan_iax2.c iax2-parser.h iax2-parser.c iax2-provision.h iax2-provision.c iax2.h
chan_iax2_la_LDFLAGS 		= -module -avoid-version -no-undefined
chan_iax2_la_LIBADD             = ${top_builddir}/corelib/libopenpbx.la
endif WANT_CHAN_IAX2

if WANT_CHAN_LOCAL
opbxmod_LTLIBRARIES		+= chan_local.la
chan_local_la_SOURCES 		= chan_local.c
chan_local_la_LDFLAGS 		= -module -avoid-version -no-undefined
chan_local_la_LIBADD            = ${top_builddir}/corelib/libopenpbx.la   
endif WANT_CHAN_LOCAL

if WANT_CHAN_MGCP
opbxmod_LTLIBRARIES		+= chan_mgcp.la
chan_mgcp_la_SOURCES 		= chan_mgcp.c
chan_mgcp_la_LDFLAGS 		= -module -avoid-version -no-undefined
chan_mgcp_la_LIBADD             = ${top_builddir}/corelib/libopenpbx.la   
endif WANT_CHAN_MGCP

if WANT_CHAN_SIP
opbxmod_LTLIBRARIES		+= chan_sip.la
chan_sip_la_SOURCES 		= chan_sip.c
chan_sip_la_LDFLAGS 		= -module -avoid-version -no-undefined
chan_sip_la_LIBADD  		= ${top_builddir}/corelib/libopenpbx.la
endif WANT_CHAN_SIP

if WANT_CHAN_UNICALL
opbxmod_LTLIBRARIES     	+= chan_unicall.la
chan_unicall_la_SOURCES 	= chan_unicall.c
chan_unicall_la_LIBADD 		= -lunicall ${top_builddir}/corelib/libopenpbx.la
chan_unicall_la_LDFLAGS 	= -module -avoid-version -no-undefined
endif WANT_CHAN_UNICALL

if WANT_CHAN_VISDN
opbxmod_LTLIBRARIES		+= chan_visdn.la
chan_visdn_la_SOURCES		= chan_visdn.c
chan_visdn_la_LIBADD		= -lq931 ${top_builddir}/corelib/libopenpbx.la
chan_visdn_la_LDFLAGS		= -module -avoid-version -no-undefined
endif WANT_CHAN_VISDN

if WANT_CHAN_WOOMERA
opbxmod_LTLIBRARIES		+= chan_woomera.la
chan_woomera_la_SOURCES		= chan_woomera.c
chan_woomera_la_LIBADD		= ${top_builddir}/corelib/libopenpbx.la
chan_woomera_la_LDFLAGS		= -module -avoid-version -no-undefined
endif WANT_CHAN_WOOMERA

if WANT_CHAN_ZAP
opbxmod_LTLIBRARIES		+= chan_zap.la
chan_zap_la_SOURCES		= chan_zap.c
chan_zap_la_CFLAGS		= $(AM_CFLAGS)
chan_zap_la_LIBADD 		= -ltonezone
if HAVE_LIBPRI
chan_zap_la_CFLAGS		+= -DZAPATA_PRI
chan_zap_la_LIBADD      	+= -lpri
endif HAVE_LIBPRI
chan_zap_la_LIBADD		+= ${top_builddir}/corelib/libopenpbx.la
chan_zap_la_LDFLAGS		= -module -avoid-version -no-undefined
endif WANT_CHAN_ZAP

INCLUDES = -I${top_srcdir}/corelib -I$(top_srcdir)/include
