SUBDIRS=jitterbuffer

opbxlib_LTLIBRARIES = libcallweaver.la
libcallweaver_la_SOURCES = io.c sched.c logger.c frame.c config.c channel.c \
	generator.c translate.c file.c say.c pbx.c cli.c term.c \
	ulaw.c alaw.c phone_no_utils.c old_callerid.c image.c app.c \
	cdr.c acl.c rtp.c manager.c callweaver_hash.c\
	dsp.c chanvars.c indications.c autoservice.c db.c privacy.c \
	callweaver_mm.c enum.c srv.c dns.c aescrypt.c aestab.c aeskey.c \
	utils.c dnsmgr.c devicestate.c \
	netsock.c slinfactory.c callweaver_expr2.c \
	callweaver_expr2f.c strcompat.c loader.c callweaver.c \
	stubfunctions-adsi.c stubfunctions-crypto.c stubfunctions-features.c \
	stubfunctions-monitor.c udp.c udptl.c udpfromto.c stun.c coef_in.h coef_out.h \
	ecdisa.h aesopt.h callweaver_expr2.h timer.c jitterbuffer/libcwjb.la

nodist_libcallweaver_la_SOURCES = defaults.h

if WANT_DEBUG
libcallweaver_la_CFLAGS 	=  -D_REENTRANT -Wall -Wstrict-prototypes 
else
libcallweaver_la_CFLAGS 	=  -fomit-frame-pointer -D_REENTRANT -Wall -Wstrict-prototypes 
endif
libcallweaver_la_CFLAGS	+= -Wmissing-prototypes -Wmissing-declarations @SQLITE3_THREADSAFE_CFLAGS@ -I$(top_srcdir) -I$(top_srcdir)/include $(AM_CFLAGS) 
libcallweaver_la_CFLAGS	+= @SSL_CFLAGS@
#libcallweaver_la_CFLAGS	+= -DDETECT_DEADLOCKS -DOPBX_MUTEX_INIT_W_CONSTRUCTORS -DTHREAD_CRASH
libcallweaver_la_LIBADD	=  -lspandsp -ltiff $(LIBLTDL) @SSL_LIBS@
libcallweaver_la_LIBADD	+= @SQLITE3_THREADSAFE_LIBS@
libcallweaver_la_LIBADD 	+= ${top_builddir}/stdtime/libtime.la 
libcallweaver_la_LIBADD 	+= ${top_builddir}/corelib/jitterbuffer/libcwjb.la
libcallweaver_la_LIBADD	+= -lreadline

if WANT_SRTP
libcallweaver_la_LIBADD	+= -lsrtp
endif WANT_SRTP

libcallweaver_la_LDFLAGS   = -no-undefined

BUILT_SOURCES = defaults.h
EXTRA_DIST = defaults.h.in
CLEANFILES = defaults.h defaults.h.tmp

@substitute@

defaults.h: defaults.h.in Makefile
	rm -f defaults.h.tmp
	$(substitute) $(srcdir)/defaults.h.in > defaults.h.tmp
	mv defaults.h.tmp defaults.h
