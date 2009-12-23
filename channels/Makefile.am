#
# CallWeaver.org
#
#
# $Id$
#
AUTOMAKE_OPTS			= gnu

AM_CFLAGS			= @CW_CFLAGS@


noinst_LTLIBRARIES 		=
cwmod_LTLIBRARIES 		=

SUBDIRS				= iax2 sccp fax misdn visdn capi

noinst_LTLIBRARIES		+= chan_skel.la
chan_skel_la_SOURCES		= chan_skel.c
chan_skel_la_LDFLAGS            = -module -avoid-version @NO_UNDEFINED@
chan_skel_la_LIBADD		= @CALLWEAVER_LIB@

if WANT_CHAN_ALSA
cwmod_LTLIBRARIES		+= chan_alsa.la
chan_alsa_la_SOURCES		= chan_alsa.c
chan_alsa_la_CFLAGS		= $(AM_CFLAGS) @ALSA_CFLAGS@
chan_alsa_la_LIBADD		= @ALSA_LIBS@ @CALLWEAVER_LIB@
chan_alsa_la_LDFLAGS            = -module -avoid-version @NO_UNDEFINED@
endif WANT_CHAN_ALSA

if WANT_CHAN_AGENT
cwmod_LTLIBRARIES     	+= chan_agent.la
chan_agent_la_SOURCES		= chan_agent.c
chan_agent_la_LDFLAGS		= -module -avoid-version @NO_UNDEFINED@
chan_agent_la_LIBADD		= @CALLWEAVER_LIB@  
endif WANT_CHAN_AGENT

if WANT_CHAN_LOCAL
cwmod_LTLIBRARIES		+= chan_local.la
chan_local_la_SOURCES 		= chan_local.c
chan_local_la_LDFLAGS 		= -module -avoid-version @NO_UNDEFINED@
chan_local_la_LIBADD            = @CALLWEAVER_LIB@   
endif WANT_CHAN_LOCAL

if WANT_CHAN_MGCP
cwmod_LTLIBRARIES		+= chan_mgcp.la
chan_mgcp_la_SOURCES 		= chan_mgcp.c
chan_mgcp_la_LDFLAGS 		= -module -avoid-version @NO_UNDEFINED@
chan_mgcp_la_LIBADD             = @CALLWEAVER_LIB@   
endif WANT_CHAN_MGCP

if WANT_CHAN_SIP
cwmod_LTLIBRARIES		+= chan_sip.la
chan_sip_la_SOURCES 		= chan_sip.c
chan_sip_la_LDFLAGS 		= -module -avoid-version @NO_UNDEFINED@
chan_sip_la_LIBADD  		= @CALLWEAVER_LIB@
endif WANT_CHAN_SIP

if WANT_CHAN_UNICALL
cwmod_LTLIBRARIES     	+= chan_unicall.la
chan_unicall_la_SOURCES 	= chan_unicall.c
chan_unicall_la_LIBADD 		= -lunicall -lsupertone @CALLWEAVER_LIB@
chan_unicall_la_LDFLAGS 	= -module -avoid-version @NO_UNDEFINED@
endif WANT_CHAN_UNICALL

if WANT_CHAN_WOOMERA
cwmod_LTLIBRARIES		+= chan_woomera.la
chan_woomera_la_SOURCES		= chan_woomera.c
chan_woomera_la_LIBADD		= @CALLWEAVER_LIB@
chan_woomera_la_LDFLAGS		= -module -avoid-version @NO_UNDEFINED@
endif WANT_CHAN_WOOMERA

if WANT_CHAN_BLUETOOTH
cwmod_LTLIBRARIES		+= chan_bluetooth.la
chan_bluetooth_la_SOURCES	= chan_bluetooth.c
chan_bluetooth_la_LIBADD	= @CALLWEAVER_LIB@ -lbluetooth
chan_bluetooth_la_LDFLAGS	= -module -avoid-version @NO_UNDEFINED@
endif WANT_CHAN_BLUETOOTH

if WANT_CHAN_ZAP
cwmod_LTLIBRARIES		+= chan_zap.la
chan_zap_la_SOURCES		= chan_zap.c
chan_zap_la_CFLAGS		= $(AM_CFLAGS)
chan_zap_la_LIBADD 		= -ltonezone
if HAVE_LIBPRI
chan_zap_la_CFLAGS		+= -DZAPATA_PRI
chan_zap_la_LIBADD      	+= -lpri
endif HAVE_LIBPRI
chan_zap_la_LIBADD		+= @CALLWEAVER_LIB@
chan_zap_la_LDFLAGS		= -module -avoid-version @NO_UNDEFINED@
endif WANT_CHAN_ZAP

INCLUDES = -I$(top_builddir)/include -I${top_srcdir}/corelib -I$(top_srcdir)/include
