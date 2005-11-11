opbxlib_LTLIBRARIES = libopenpbx.la
libopenpbx_la_SOURCES = io.c sched.c logger.c frame.c config.c channel.c \
	generator.c translate.c file.c say.c pbx.c cli.c md5.c term.c \
	ulaw.c alaw.c callerid.c fskmodem.c image.c app.c \
	cdr.c tdd.c acl.c rtp.c manager.c \
	dsp.c chanvars.c indications.c autoservice.c db.c privacy.c \
	opbxmm.c enum.c srv.c dns.c aescrypt.c aestab.c aeskey.c \
	utils.c plc.c jitterbuf.c dnsmgr.c devicestate.c \
	netsock.c resonator.c slinfactory.c opbx_expr2.c \
	opbx_expr2f.c strcompat.c loader.c openpbx.c \
	stubfunctions-adsi.c stubfunctions-crypto.c stubfunctions-features.c \
	stubfunctions-monitor.c udptl.c udpfromto.c coef_in.h coef_out.h \
	ecdisa.h aesopt.h jitterbuf.h opbx_expr2.h
nodist_libopenpbx_la_SOURCES = defaults.h

libopenpbx_la_CFLAGS 	=  -fomit-frame-pointer -D_REENTRANT -Wall -Wstrict-prototypes 
libopenpbx_la_CFLAGS	+= -Wmissing-prototypes -Wmissing-declarations @SQLITE3_THREADSAFE_CFLAGS@ -I$(top_srcdir) -I$(top_srcdir)/include $(AM_CFLAGS) 
libopenpbx_la_LIBADD	= $(LIBLTDL) 
libopenpbx_la_LIBADD	+= @SQLITE3_THREADSAFE_LIBS@
libopenpbx_la_LIBADD 	+= ${top_builddir}/editline/src/libedit.la 
libopenpbx_la_LIBADD 	+= ${top_builddir}/stdtime/libtime.la 

if WANT_SRTP
libopenpbx_la_LIBADD	+= -lsrtp
endif WANT_SRTP

libopenpbx_la_LDFLAGS   = -no-undefined

BUILT_SOURCES = defaults.h
EXTRA_DIST = defaults.h.in
CLEANFILES = defaults.h defaults.h.tmp

@substitute@

defaults.h: defaults.h.in Makefile
	rm -f defaults.h.tmp
	$(substitute) $(srcdir)/defaults.h.in > defaults.h.tmp
	mv defaults.h.tmp defaults.h
