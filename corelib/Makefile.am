DEFS += -include $(top_builddir)/include/confdefs.h

SUBDIRS=jitterbuffer resample

opbxlib_LTLIBRARIES = libcallweaver.la
libcallweaver_la_SOURCES = io.c sched.c logger.c frame.c config.c channel.c \
	generator.c translate.c say.c pbx.c function.c cli.c term.c \
	ulaw.c alaw.c phone_no_utils.c callerid.c image.c app.c \
	cdr.c acl.c rtp.c manager.c callweaver_hash.c\
	dsp.c chanvars.c indications.c autoservice.c db.c privacy.c \
	callweaver_mm.c enum.c srv.c dns.c aescrypt.c aestab.c aeskey.c \
	malloc.c utils.c dnsmgr.c devicestate.c \
	netsock.c slinfactory.c callweaver_expr2.c \
	callweaver_expr2f.c strcompat.c registry.c loader.c callweaver.c \
	stubfunctions-adsi.c stubfunctions-features.c \
	stubfunctions-monitor.c udp.c udptl.c udpfromto.c stun.c coef_in.h coef_out.h \
	ecdisa.h aesopt.h callweaver_expr2.h timer.c jitterbuffer/libcwjb.la \
	resample.c \
	directory_engine.c \
	hashtable.c mpool.c xml_parser.c eventlib.c \
	hashtable_helper.c \
	file.c filestreams.c crypto.c \
    lang/say_br.c \
    lang/say_cz.c \
    lang/say_da.c \
    lang/say_de.c \
    lang/say_en.c \
    lang/say_en_GB.c \
    lang/say_es.c \
    lang/say_fr.c \
    lang/say_gr.c \
    lang/say_he.c \
    lang/say_it.c \
    lang/say_nl.c \
    lang/say_nb_NO.c \
    lang/say_pl.c \
    lang/say_pt.c \
    lang/say_ru.c \
    lang/say_se.c \
    lang/say_zh_TW.c

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
libcallweaver_la_LIBADD 	+= ${top_builddir}/corelib/resample/libcwresample.la
libcallweaver_la_LIBADD	+= -lreadline

if WANT_SRTP
libcallweaver_la_LIBADD	+= -lsrtp
endif WANT_SRTP

libcallweaver_la_LDFLAGS   = -no-undefined

BUILT_SOURCES = defaults.h #callweaver_expr2.c callweaver_expr2.h callweaver_expr2f.c
EXTRA_DIST = defaults.h.in
CLEANFILES = defaults.h defaults.h.tmp

@substitute@

callweaver_expr2.c: callweaver_expr2.h: callweaver_expr2.y
	$(YACC) -d --name-prefix=opbx_yy -o callweaver_expr2.c callweaver_expr2.y

callweaver_expr2f.c: callweaver_expr2.l
	$(LEX) -f -t callweaver_expr2.l > callweaver_expr2f.c

defaults.h: defaults.h.in Makefile
	rm -f defaults.h.tmp
	$(substitute) $(srcdir)/defaults.h.in > defaults.h.tmp
	mv defaults.h.tmp defaults.h
