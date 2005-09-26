#
# OpenPBX -- A telephony toolkit for Linux.
# 
# Top level Makefile
#
# Copyright (C) 1999-2005, Mark Spencer
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

.EXPORT_ALL_VARIABLES:

# Create OPTIONS variable
OPTIONS=
# If cross compiling, define these to suit
# CROSS_COMPILE=/opt/montavista/pro/devkit/arm/xscale_be/bin/xscale_be-
# CROSS_COMPILE_BIN=/opt/montavista/pro/devkit/arm/xscale_be/bin/
# CROSS_COMPILE_TARGET=/opt/montavista/pro/devkit/arm/xscale_be/target
CC=$(CROSS_COMPILE)gcc
HOST_CC=gcc
# CROSS_ARCH=Linux
# CROSS_PROC=arm
# SUB_PROC=xscale # or maverick

ifeq ($(CROSS_COMPILE),)
  OSARCH=$(shell uname -s)
  OSREV=$(shell uname -r)
else
  OSARCH=$(CROSS_ARCH)
  OSREV=$(CROSS_REV)
endif

# Remember the MAKELEVEL at the top
MAKETOPLEVEL?=$(MAKELEVEL)

######### More GSM codec optimization
######### Uncomment to enable MMXTM optimizations for x86 architecture CPU's
######### which support MMX instructions.  This should be newer pentiums,
######### ppro's, etc, as well as the AMD K6 and K7.  
#K6OPT  = -DK6OPT

#Overwite config files on "make samples"
OVERWRITE=y

#Tell gcc to optimize the code
OPTIMIZE+=-O6

#Include debug symbols in the executables (-g) and profiling info (-pg)
DEBUG=-g #-pg

# If you are running a radio application, define RADIO_RELAX so that the DTMF
# will be received more reliably
#OPTIONS += -DRADIO_RELAX

# If you don't have a lot of memory (e.g. embedded OpenPBX), define LOW_MEMORY
# to reduce the size of certain static buffers

#ifneq ($(CROSS_COMPILE),)
#OPTIONS += -DLOW_MEMORY
#endif

# Optional debugging parameters
DEBUG_THREADS = #-DDUMP_SCHEDULER #-DDEBUG_SCHEDULER #-DDEBUG_THREADS #-DDO_CRASH #-DDETECT_DEADLOCKS

# Uncomment next one to enable opbx_frame tracing (for debugging)
TRACE_FRAMES = #-DTRACE_FRAMES

# Uncomment next one to enable malloc debugging
# You can view malloc debugging with:
#   *CLI> show memory allocations [filename]
#   *CLI> show memory summary [filename]
#
MALLOC_DEBUG = #-include $(PWD)/include/openpbx/opbxmm.h

# Where to install openpbx after compiling
# Default -> leave empty
INSTALL_PREFIX=

# Staging directory
# Files are copied here temporarily during the install process
# For example, make DESTDIR=/tmp/openpbx woud put things in
# /tmp/openpbx/etc/openpbx
DESTDIR=

# Original busydetect routine
BUSYDETECT = #-DBUSYDETECT

# Improved busydetect routine, comment the previous one if you use this one
BUSYDETECT+= #-DBUSYDETECT_MARTIN 
# Detect the busy signal looking only at tone lengths
# For example if you have 3 beeps 100ms tone, 100ms silence separated by 500 ms of silence
BUSYDETECT+= #-DBUSYDETECT_TONEONLY
# Inforce the detection of busy singal (get rid of false hangups)
# Don't use together with -DBUSYDETECT_TONEONLY
BUSYDETECT+= #-DBUSYDETECT_COMPARE_TONE_AND_SILENCE

ifneq (${OSARCH},SunOS)
  ASTLIBDIR=$(INSTALL_PREFIX)/usr/lib/openpbx
  ASTVARLIBDIR=$(INSTALL_PREFIX)/var/lib/openpbx
  ASTETCDIR=$(INSTALL_PREFIX)/etc/openpbx
  ASTSPOOLDIR=$(INSTALL_PREFIX)/var/spool/openpbx
  ASTLOGDIR=$(INSTALL_PREFIX)/var/log/openpbx
  ASTHEADERDIR=$(INSTALL_PREFIX)/usr/include/openpbx
  ASTCONFPATH=$(ASTETCDIR)/openpbx.conf
  ASTBINDIR=$(INSTALL_PREFIX)/usr/bin
  ASTSBINDIR=$(INSTALL_PREFIX)/usr/sbin
  ASTVARRUNDIR=$(INSTALL_PREFIX)/var/run
  ASTMANDIR=$(INSTALL_PREFIX)/usr/share/man
  MODULES_DIR=$(ASTLIBDIR)/modules
  AGI_DIR=$(ASTVARLIBDIR)/agi-bin
else
  ASTLIBDIR=$(INSTALL_PREFIX)/opt/openpbx/lib
  ASTVARLIBDIR=$(INSTALL_PREFIX)/var/opt/openpbx/lib
  ASTETCDIR=$(INSTALL_PREFIX)/etc/opt/openpbx
  ASTSPOOLDIR=$(INSTALL_PREFIX)/var/opt/openpbx/spool
  ASTLOGDIR=$(INSTALL_PREFIX)/var/opt/openpbx/log
  ASTHEADERDIR=$(INSTALL_PREFIX)/opt/openpbx/usr/include/openpbx
  ASTCONFPATH=$(ASTETCDIR)/openpbx.conf
  ASTBINDIR=$(INSTALL_PREFIX)/opt/openpbx/usr/bin
  ASTSBINDIR=$(INSTALL_PREFIX)/opt/openpbx/usr/sbin
  ASTVARRUNDIR=$(INSTALL_PREFIX)/var/opt/openpbx/run
  ASTMANDIR=$(INSTALL_PREFIX)/opt/openpbx/usr/share/man
  MODULES_DIR=$(ASTLIBDIR)/modules
  AGI_DIR=$(ASTVARLIBDIR)/agi-bin
endif

ASTCFLAGS=

# Pentium Pro Optimize
#PROC=i686

# Pentium & VIA processors optimize
#PROC=i586

#PROC=k6
#PROC=ppc

#Uncomment this to use the older DSP routines
#ASTCFLAGS+=-DOLD_DSP_ROUTINES

# Determine by a grep 'DocumentRoot' of your httpd.conf file
HTTP_DOCSDIR=/var/www/html
# Determine by a grep 'ScriptAlias' of your httpd.conf file
HTTP_CGIDIR=/var/www/cgi-bin

# If the file .openpbx.makeopts is present in your home directory, you can
# include all of your favorite Makefile options so that every time you download
# a new version of OpenPBX, you don't have to edit the makefile to set them. 
# The file, /etc/openpbx.makeopts will also be included, but can be overridden
# by the file in your home directory.

ifneq ($(wildcard /etc/openpbx.makeopts),)
  include /etc/openpbx.makeopts
endif

ifneq ($(wildcard ~/.openpbx.makeopts),)
  include ~/.openpbx.makeopts
endif

ifeq (${OSARCH},Linux)
  ifeq ($(CROSS_COMPILE),)
    PROC?=$(shell uname -m)
  else
    PROC=$(CROSS_PROC)
  endif

  ifeq ($(PROC),x86_64)
    # You must have GCC 3.4 to use k8, otherwise use athlon
    PROC=k8
    #PROC=athlon
    OPTIONS+=-m64
  endif

  ifeq ($(PROC),sparc64)
    #The problem with sparc is the best stuff is in newer versions of gcc (post 3.0) only.
    #This works for even old (2.96) versions of gcc and provides a small boost either way.
    #A ultrasparc cpu is really v9 but the stock debian stable 3.0 gcc doesn't support it.
    #So we go lowest common available by gcc and go a step down, still a step up from
    #the default as we now have a better instruction set to work with. - Belgarath
    PROC=ultrasparc
    OPTIONS+=$(shell if $(CC) -mtune=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-mtune=$(PROC)"; fi)
    OPTIONS+=$(shell if $(CC) -mcpu=v8 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-mcpu=v8"; fi)
    OPTIONS+=-fomit-frame-pointer
  endif

  ifeq ($(PROC),arm)
    # The Cirrus logic is the only heavily shipping arm processor with a real floating point unit
    ifeq ($(SUB_PROC),maverick)
      OPTIONS+=-fsigned-char -mcpu=ep9312
    else
      ifeq ($(SUB_PROC),xscale)
        OPTIONS+=-fsigned-char -msoft-float -mcpu=xscale
      else
        OPTIONS+=-fsigned-char -msoft-float 
      endif
    endif
  endif
  MPG123TARG=linux
endif

ifeq ($(findstring BSD,${OSARCH}),BSD)
  PROC=$(shell uname -m)
  ASTCFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

PWD=$(shell pwd)
GREP=grep

ifeq (${OSARCH},SunOS)
  GREP=/usr/xpg4/bin/grep
  M4=/usr/local/bin/m4
endif

INCLUDE=-Iinclude -I../include
ASTCFLAGS+=-pipe  -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations $(DEBUG) $(INCLUDE) -D_REENTRANT -D_GNU_SOURCE #-DMAKE_VALGRIND_HAPPY
ASTCFLAGS+=$(OPTIMIZE)

ifneq ($(PROC),ultrasparc)
  ASTCFLAGS+=$(shell if $(CC) -march=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=$(PROC)"; fi)
endif

ifeq ($(PROC),ppc)
  ASTCFLAGS+=-fsigned-char
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/osp/osp.h),)
  ASTCFLAGS+=-DOSP_SUPPORT -I$(CROSS_COMPILE_TARGET)/usr/include/osp
endif

ifeq (${OSARCH},FreeBSD)
  BSDVERSION=$(shell make -V OSVERSION -f $(CROSS_COMPILE_TARGET)/usr/share/mk/bsd.port.subdir.mk)
  ASTCFLAGS+=$(shell if test ${BSDVERSION} -lt 500016 ; then echo "-D_THREAD_SAFE"; fi)
  LIBS+=$(shell if test  ${BSDVERSION} -lt 502102 ; then echo "-lc_r"; else echo "-pthread"; fi)
  ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/spandsp),)
    ASTCFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include/spandsp
  endif
  MPG123TARG=freebsd
endif # FreeBSD

ifeq (${OSARCH},NetBSD)
  ASTCFLAGS+=-pthread
  INCLUDE+=-I$(CROSS_COMPILE_TARGET)/usr/pkg/include
  MPG123TARG=netbsd
endif

ifeq (${OSARCH},OpenBSD)
  ASTCFLAGS+=-pthread
endif

ifeq (${OSARCH},SunOS)
  ASTCFLAGS+=-Wcast-align -DSOLARIS
  INCLUDE+=-Iinclude/solaris-compat -I$(CROSS_COMPILE_TARGET)/usr/local/ssl/include
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/linux/zaptel.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/zaptel.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/pkg/include/zaptel.h),)
  ASTCFLAGS+=-DZAPTEL_OPTIMIZATIONS
endif

LIBEDIT=editline/libedit.a

ifneq ($(wildcard .version),)
  OPENPBXVERSION=$(shell cat .version)
  OPENPBXVERSIONNUM=$(shell awk -F. '{printf "%02d%02d%02d", $$1, $$2, $$3}' .version)
  RPMVERSION=$(shell sed 's/[-\/:]/_/g' .version)
else
  RPMVERSION=unknown
endif

ifneq ($(wildcard CVS),)
  OPENPBXVERSIONNUM=999999
  ifneq ($(wildcard CVS/Tag),)
    OPENPBXVERSION=$(shell echo "CVS-`sed 's/^T//g' CVS/Tag`-`date +"%D-%T"`")
  else
    OPENPBXVERSION=CVS HEAD
  endif
else
  OPENPBXVERSIONNUM=000000
endif

ASTCFLAGS+= $(DEBUG_THREADS)
ASTCFLAGS+= $(TRACE_FRAMES)
ASTCFLAGS+= $(MALLOC_DEBUG)
ASTCFLAGS+= $(BUSYDETECT)
ASTCFLAGS+= $(OPTIONS)
ASTCFLAGS+= -fomit-frame-pointer 
SUBDIRS=res channels pbx apps codecs formats agi cdr funcs utils stdtime

OBJS=io.o sched.o logger.o frame.o loader.o config.o channel.o \
	translate.o file.o say.o pbx.o cli.o md5.o term.o \
	ulaw.o alaw.o callerid.o fskmodem.o image.o app.o \
	cdr.o tdd.o acl.o rtp.o manager.o openpbx.o \
	dsp.o chanvars.o indications.o autoservice.o db.o privacy.o \
	opbxmm.o enum.o srv.o dns.o aescrypt.o aestab.o aeskey.o \
	utils.o plc.o jitterbuf.o dnsmgr.o devicestate.o \
	netsock.o slinfactory.o opbx_expr2.o opbx_expr2f.o

ifeq (${OSARCH},Linux)
  LIBS=-ldl -lpthread -lncurses -lm -lresolv  #-lnjamd
else
  LIBS+=-lncurses -lm
endif

ifeq (${OSARCH},Darwin)
  LIBS+=-lresolv
  ASTCFLAGS+=-D__Darwin__
  AUDIO_LIBS=-framework CoreAudio
  OBJS+=poll.o dlfcn.o
  ASTLINK=-Wl,-dynamic
  SOLINK=-dynamic -bundle -undefined suppress -force_flat_namespace
else
#These are used for all but Darwin
  ASTLINK=-Wl,-E 
  SOLINK=-shared -Xlinker -x
endif

ifeq (${OSARCH},FreeBSD)
  LIBS+=-lcrypto
endif

ifeq (${OSARCH},NetBSD)
  LIBS+=-lpthread -lcrypto -lm -L$(CROSS_COMPILE_TARGET)/usr/pkg/lib -lncurses
endif

ifeq (${OSARCH},OpenBSD)
  LIBS=-lcrypto -lpthread -lm -lncurses
endif

ifeq (${OSARCH},SunOS)
  LIBS+=-lpthread -ldl -lnsl -lsocket -lresolv -L$(CROSS_COMPILE_TARGET)/usr/local/ssl/lib
  OBJS+=strcompat.o
  ASTLINK=
  SOLINK=-shared -fpic -L$(CROSS_COMPILE_TARGET)/usr/local/ssl/lib
endif

ifeq ($(MAKETOPLEVEL),$(MAKELEVEL))
  CFLAGS+=$(ASTCFLAGS)
endif

LIBS+=-lssl

INSTALL=install

_all: all
	@echo " +--------- OpenPBX Build Complete ----------+"  
	@echo " + OpenPBX has successfully been built, but  +"  
	@echo " + cannot be run before being installed by   +"  
	@echo " + running:                                  +"  
	@echo " +                                           +"
	@echo " +               $(MAKE) install                +"  
	@echo " +-------------------------------------------+"  

all: cleantest depend openpbx subdirs 

#ifneq ($(wildcard tags),)
ctags: tags
#endif

ifneq ($(wildcard TAGS),)
all: TAGS
endif

noclean: depend openpbx subdirs

editline/config.h:
	cd editline && unset CFLAGS LIBS && ./configure ; \

editline/libedit.a: FORCE
	cd editline && unset CFLAGS LIBS && test -f config.h || ./configure
	$(MAKE) -C editline libedit.a

db1-ast/libdb1.a: FORCE
	@if [ -d db1-ast ]; then \
		$(MAKE) -C db1-ast libdb1.a ; \
	else \
		echo "You need to do a svn update not just svn update"; \
		exit 1; \
	fi

ifneq ($(wildcard .depend),)
  include .depend
endif

ifneq ($(wildcard .tags-depend),)
  include .tags-depend
endif

opbx_expr2.c:
	bison -d --name-prefix=opbx_yy opbx_expr2.y -o opbx_expr2.c

opbx_expr2f.c:
	flex --full opbx_expr2.fl

testexpr2: opbx_expr2f.c opbx_expr2.c opbx_expr2.h
	gcc -g -c -DSTANDALONE opbx_expr2f.c
	gcc -g -c -DSTANDALONE opbx_expr2.c
	gcc -g -o testexpr2 opbx_expr2f.o opbx_expr2.o
	rm opbx_expr2.o opbx_expr2f.o

manpage: openpbx.8

openpbx.8: openpbx.sgml
	rm -f openpbx.8
	docbook2man openpbx.sgml
	mv ./*.8 openpbx.8

openpbx.pdf: openpbx.sgml
	docbook2pdf openpbx.sgml

openpbx.ps: openpbx.sgml
	docbook2ps openpbx.sgml

openpbx.html: openpbx.sgml
	docbook2html openpbx.sgml
	mv r1.html openpbx.html

openpbx.txt: openpbx.sgml
	docbook2txt openpbx.sgml

defaults.h: FORCE
	build_tools/make_defaults_h > $@.tmp
	if cmp -s $@.tmp $@ ; then echo ; else \
		mv $@.tmp $@ ; \
	fi
	rm -f $@.tmp

include/openpbx/build.h:
	build_tools/make_build_h > $@.tmp
	if cmp -s $@.tmp $@ ; then echo ; else \
		mv $@.tmp $@ ; \
	fi
	rm -f $@.tmp

# only force 'build.h' to be made for a non-'install' run
ifeq ($(findstring install,$(MAKECMDGOALS)),)
include/openpbx/build.h: FORCE
endif

include/openpbx/version.h: FORCE
	build_tools/make_version_h > $@.tmp
	if cmp -s $@.tmp $@ ; then echo; else \
		mv $@.tmp $@ ; \
	fi
	rm -f $@.tmp

stdtime/libtime.a: FORCE
	@if [ -d stdtime ]; then \
		$(MAKE) -C stdtime libtime.a ; \
	else \
		echo "You need to do a svn update -d not just svn update"; \
		exit 1; \
	fi

openpbx: editline/libedit.a db1-ast/libdb1.a stdtime/libtime.a $(OBJS)
	$(CC) $(DEBUG) -o openpbx $(ASTLINK) $(OBJS) $(LIBEDIT) db1-ast/libdb1.a stdtime/libtime.a $(LIBS)

muted: muted.o
	$(CC) $(AUDIO_LIBS) -o muted muted.o

subdirs: 
	for x in $(SUBDIRS); do $(MAKE) -C $$x || exit 1 ; done

clean:
	for x in $(SUBDIRS); do $(MAKE) -C $$x clean || exit 1 ; done
	rm -f *.o *.so openpbx .depend
	rm -f defaults.h
	rm -f include/openpbx/build.h
	rm -f include/openpbx/version.h
	rm -f .tags-depend .tags-sources tags TAGS
	@if [ -f editline/Makefile ]; then $(MAKE) -C editline distclean ; fi
	@if [ -d mpg123-0.59r ]; then $(MAKE) -C mpg123-0.59r clean; fi
	$(MAKE) -C db1-ast clean
	$(MAKE) -C stdtime clean

datafiles: all
	sh mkpkgconfig $(DESTDIR)/usr/lib/pkgconfig
	for y in sounds/*; do \
		mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/$$y ; \
		mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/$$y/digits ; \
		mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/$$y/priv-callerintros ; \
		for x in $$y/digits/*.gsm; do \
			if $(GREP) -q "^%`basename $$x`%" sounds.txt; then \
				install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/$$y/digits ; \
			else \
				echo "No description for $$x"; \
				exit 1; \
			fi; \
		done ; \
		mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/$$y/dictate ; \
		for x in $$y/dictate/*.gsm; do \
			if $(GREP) -q "^%`basename $$x`%" sounds.txt; then \
				install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/$$y/dictate ; \
			else \
				echo "No description for $$x"; \
				exit 1; \
			fi; \
		done ; \
		mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/$$y/letters ; \
		for x in $$y/letters/*.gsm; do \
			if $(GREP) -q "^%`basename $$x`%" sounds.txt; then \
				install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/$$y/letters ; \
			else \
				echo "No description for $$x"; \
				exit 1; \
			fi; \
		done ; \
		mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/$$y/phonetic ; \
		for x in $$y/phonetic/*.gsm; do \
			if $(GREP) -q "^%`basename $$x`%" sounds.txt; then \
				install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/$$y/phonetic ; \
			else \
				echo "No description for $$x"; \
				exit 1; \
			fi; \
		done ; \
		for x in $$y/demo-* $$y/vm-* $$y/transfer* $$y/pbx-* $$y/ss-* $$y/beep* $$y/dir-* $$y/conf-* $$y/agent-* $$y/invalid* $$y/tt-* $$y/auth-* $$y/privacy-* $$y/queue-* $$y/spy-* $$y/priv-* $$y/screen-*; do \
			if $(GREP) -q "^%`basename $$x`%" sounds.txt; then \
				install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/$$y ; \
			else \
				echo "No description for $$x"; \
				exit 1; \
			fi; \
		done ; \
	done
	# make allison-en default
	@if [ ! -f $(DESTDIR)$(ASTVARLIBDIR)/sounds/en ] && [ ! -d $(DESTDIR)$(ASTVARLIBDIR)/sounds/en ] ; then \
		ln -s $(DESTDIR)$(ASTVARLIBDIR)/sounds/allison-en $(DESTDIR)$(ASTVARLIBDIR)/sounds/en; \
	fi
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/mohmp3
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/images
	for x in images/*.jpg; do \
		install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/images ; \
	done
	mkdir -p $(DESTDIR)$(AGI_DIR)

update: 
	@if [ -d .svn ]; then \
		echo "Updating from SVN..." ; \
		svn update | tee update.out; \
		rm -f .version; \
		if [ `grep -c ^C update.out` -gt 0 ]; then \
			echo ; echo "The following files have conflicts:" ; \
			grep ^C update.out | cut -d' ' -f2- ; \
		fi ; \
		rm -f update.out; \
	else \
		echo "Not SVN";  \
	fi

NEWHEADERS=$(notdir $(wildcard include/openpbx/*.h))
OLDHEADERS=$(filter-out $(NEWHEADERS),$(notdir $(wildcard $(DESTDIR)$(ASTHEADERDIR)/*.h)))

bininstall: all
	mkdir -p $(DESTDIR)$(MODULES_DIR)
	mkdir -p $(DESTDIR)$(ASTSBINDIR)
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	mkdir -p $(DESTDIR)$(ASTBINDIR)
	mkdir -p $(DESTDIR)$(ASTVARRUNDIR)
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/voicemail
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/dictate
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/system
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/tmp
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/meetme
	install -m 755 openpbx $(DESTDIR)$(ASTSBINDIR)/
	install -m 755 contrib/scripts/opbxgenkey $(DESTDIR)$(ASTSBINDIR)/
	install -m 755 contrib/scripts/autosupport $(DESTDIR)$(ASTSBINDIR)/	
	if [ ! -f $(DESTDIR)$(ASTSBINDIR)/safe_openpbx ]; then \
		cat contrib/scripts/safe_openpbx | sed 's|__OPENPBX_SBIN_DIR__|$(ASTSBINDIR)|;' > $(DESTDIR)$(ASTSBINDIR)/safe_openpbx ;\
		chmod 755 $(DESTDIR)$(ASTSBINDIR)/safe_openpbx;\
	fi
	for x in $(SUBDIRS); do $(MAKE) -C $$x install || exit 1 ; done
	install -d $(DESTDIR)$(ASTHEADERDIR)
	install -m 644 include/openpbx/*.h $(DESTDIR)$(ASTHEADERDIR)
	if [ -n "$(OLDHEADERS)" ]; then \
		rm -f $(addprefix $(DESTDIR)$(ASTHEADERDIR)/,$(OLDHEADERS)) ;\
	fi
	rm -f $(DESTDIR)$(ASTVARLIBDIR)/sounds/voicemail
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/sounds
	mkdir -p $(DESTDIR)$(ASTLOGDIR)/cdr-csv
	mkdir -p $(DESTDIR)$(ASTLOGDIR)/cdr-custom
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/keys
	mkdir -p $(DESTDIR)$(ASTMANDIR)/man8
	install -m 644 openpbx.8 $(DESTDIR)$(ASTMANDIR)/man8
	install -m 644 contrib/scripts/opbxgenkey.8 $(DESTDIR)$(ASTMANDIR)/man8
	install -m 644 contrib/scripts/autosupport.8 $(DESTDIR)$(ASTMANDIR)/man8
	install -m 644 contrib/scripts/safe_openpbx.8 $(DESTDIR)$(ASTMANDIR)/man8
	( cd $(DESTDIR)$(ASTVARLIBDIR)/sounds  ; ln -s $(ASTSPOOLDIR)/voicemail . )
	if [ -f mpg123-0.59r/mpg123 ]; then $(MAKE) -C mpg123-0.59r install; fi
	@echo " +---- OpenPBX Installation Complete --------+"  
	@echo " +                                           +"
	@echo " +    YOU MUST READ THE SECURITY DOCUMENT    +"
	@echo " +                                           +"
	@echo " + OpenPBX has successfully been installed.  +"  
	@echo " + If you would like to install the sample   +"  
	@echo " + configuration files (overwriting any      +"
	@echo " + existing config files), run:              +"  
	@echo " +                                           +"
	@echo " +               $(MAKE) samples                +"
	@echo " +                                           +"
	@echo " +-----------------  or ---------------------+"
	@echo " +                                           +"
	@echo " + You can go ahead and install the openpbx  +"
	@echo " + program documentation now or later run:   +"
	@echo " +                                           +"
	@echo " +              $(MAKE) progdocs                +"
	@echo " +                                           +"
	@echo " + **Note** This requires that you have      +"
	@echo " + doxygen installed on your local system    +"
	@echo " +-------------------------------------------+"
	@$(MAKE) -s oldmodcheck

NEWMODS=$(notdir $(wildcard */*.so))
OLDMODS=$(filter-out $(NEWMODS),$(notdir $(wildcard $(DESTDIR)$(MODULES_DIR)/*.so)))

oldmodcheck:
	@if [ -n "$(OLDMODS)" ]; then \
		echo " WARNING WARNING WARNING" ;\
		echo "" ;\
		echo " Your OpenPBX modules directory, located at" ;\
		echo " $(DESTDIR)$(MODULES_DIR)" ;\
		echo " contains modules that were not installed by this " ;\
		echo " version of OpenPBX. Please ensure that these" ;\
		echo " modules are compatible with this version before" ;\
		echo " attempting to run OpenPBX." ;\
		echo "" ;\
		for f in $(OLDMODS); do \
			echo "    $$f" ;\
		done ;\
		echo "" ;\
		echo " WARNING WARNING WARNING" ;\
	fi

install: all datafiles bininstall

upgrade: all bininstall

adsi:
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	for x in configs/*.adsi; do \
		if [ ! -f $(DESTDIR)$(ASTETCDIRX)/$$x ]; then \
			install -m 644 $$x $(DESTDIR)$(ASTETCDIR)/`basename $$x` ; \
		fi ; \
	done

samples: adsi
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	for x in configs/*.sample; do \
		if [ -f $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample` ]; then \
			if [ "$(OVERWRITE)" = "y" ]; then \
				if cmp -s $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample` $$x ; then \
					echo "Config file $$x is unchanged"; \
					continue; \
				fi ; \
				mv -f $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample` $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample`.old ; \
			else \
				echo "Skipping config file $$x"; \
				continue; \
			fi ;\
		fi ; \
		install -m 644 $$x $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample` ;\
	done
	if [ "$(OVERWRITE)" = "y" ] || [ ! -f $(DESTDIR)$(ASTETCDIR)/openpbx.conf ]; then \
		echo "[directories]" > $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo "astetcdir => $(ASTETCDIR)" >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo "astmoddir => $(MODULES_DIR)" >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo "astvarlibdir => $(ASTVARLIBDIR)" >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo "astagidir => $(AGI_DIR)" >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo "astspooldir => $(ASTSPOOLDIR)" >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo "astrundir => $(ASTVARRUNDIR)" >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo "astlogdir => $(ASTLOGDIR)" >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo "" >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo "; Changing the following lines may compromise your security." >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo ";[files]" >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo ";astctlpermissions = 0660" >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo ";astctlowner = root" >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo ";astctlgroup = apache" >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
		echo ";astctl = openpbx.ctl" >> $(DESTDIR)$(ASTETCDIR)/openpbx.conf ; \
	else \
		echo "Skipping openpbx.conf creation"; \
	fi
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/sounds ; \
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/mohmp3 ; \
	rm -f $(DESTDIR)$(ASTVARLIBDIR)/mohmp3/sample-hold.mp3
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/INBOX
	:> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/unavail.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isunavail; do \
		cat $(DESTDIR)$(ASTVARLIBDIR)/sounds/en/$$x.gsm >> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/unavail.gsm ; \
	done
	:> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/busy.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isonphone; do \
		cat $(DESTDIR)$(ASTVARLIBDIR)/sounds/en/$$x.gsm >> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/busy.gsm ; \
	done

webvmail:
	@[ -d $(DESTDIR)$(HTTP_DOCSDIR)/ ] || ( printf "http docs directory not found.\nUpdate assignment of variable HTTP_DOCSDIR in Makefile!\n" && exit 1 )
	@[ -d $(DESTDIR)$(HTTP_CGIDIR) ] || ( printf "cgi-bin directory not found.\nUpdate assignment of variable HTTP_CGIDIR in Makefile!\n" && exit 1 )
	install -m 4755 -o root -g root contrib/scripts/vmail.cgi $(DESTDIR)$(HTTP_CGIDIR)/vmail.cgi
	mkdir -p $(DESTDIR)$(HTTP_DOCSDIR)/_openpbx
	for x in images/*.gif; do \
		install -m 644 $$x $(DESTDIR)$(HTTP_DOCSDIR)/_openpbx/; \
	done
	@echo " +--------- OpenPBX Web Voicemail -----------+"  
	@echo " +                                           +"
	@echo " + OpenPBX Web Voicemail is installed in     +"
	@echo " + your cgi-bin directory:                   +"
	@echo " + $(DESTDIR)$(HTTP_CGIDIR)"
	@echo " + IT USES A SETUID ROOT PERL SCRIPT, SO     +"
	@echo " + IF YOU DON'T LIKE THAT, UNINSTALL IT!     +"
	@echo " +                                           +"
	@echo " + Other static items have been stored in:   +"
	@echo " + $(DESTDIR)$(HTTP_DOCSDIR)"
	@echo " +                                           +"
	@echo " + If these paths do not match your httpd    +"
	@echo " + installation, correct the definitions     +"
	@echo " + in your Makefile of HTTP_CGIDIR and       +"
	@echo " + HTTP_DOCSDIR                              +"
	@echo " +                                           +"
	@echo " +-------------------------------------------+"  

spec: 
	sed "s/^Version:.*/Version: $(RPMVERSION)/g" redhat/openpbx.spec > openpbx.spec ; \

rpm: __rpm

__rpm: include/openpbx/version.h spec
	rm -rf /tmp/openpbx ; \
	mkdir -p /tmp/openpbx/redhat/RPMS/i386 ; \
	$(MAKE) DESTDIR=/tmp/openpbx install ; \
	$(MAKE) DESTDIR=/tmp/openpbx samples ; \
	mkdir -p /tmp/openpbx/etc/rc.d/init.d ; \
	cp -f redhat/openpbx /tmp/openpbx/etc/rc.d/init.d/ ; \
	rpmbuild --rcfile /usr/lib/rpm/rpmrc:redhat/rpmrc -bb openpbx.spec

progdocs:
	doxygen contrib/openpbx-ng-doxygen

mpg123:
	@wget -V >/dev/null || (echo "You need wget" ; false )
	[ -f mpg123-0.59r.tar.gz ] || wget http://www.mpg123.de/mpg123/mpg123-0.59r.tar.gz
	[ -d mpg123-0.59r ] || tar xfz mpg123-0.59r.tar.gz
	$(MAKE) -C mpg123-0.59r $(MPG123TARG)

config:
	if [ -d /etc/rc.d/init.d ]; then \
		install -m 755 contrib/init.d/rc.redhat.openpbx /etc/rc.d/init.d/openpbx; \
		/sbin/chkconfig --add openpbx; \
	elif [ -d /etc/init.d ]; then \
		install -m 755 init.openpbx /etc/init.d/openpbx; \
	fi 

dont-optimize:
	$(MAKE) OPTIMIZE= K6OPT= install

valgrind: dont-optimize

depend: include/openpbx/build.h include/openpbx/version.h .depend defaults.h 
	for x in $(SUBDIRS); do $(MAKE) -C $$x depend || exit 1 ; done

.depend: include/openpbx/version.h
	build_tools/mkdep ${CFLAGS} $(wildcard *.c)

.tags-depend:
	@echo -n ".tags-depend: " > $@
	@find . -maxdepth 1 -name \*.c -printf "\t%p \\\\\n" >> $@
	@find . -maxdepth 1 -name \*.h -printf "\t%p \\\\\n" >> $@
	@find ${SUBDIRS} -name \*.c -printf "\t%p \\\\\n" >> $@
	@find ${SUBDIRS} -name \*.h -printf "\t%p \\\\\n" >> $@
	@find include -name \*.h -printf "\t%p \\\\\n" >> $@
	@echo >> $@

.tags-sources:
	@rm -f $@
	@find . -maxdepth 1 -name \*.c -print >> $@
	@find . -maxdepth 1 -name \*.h -print >> $@
	@find ${SUBDIRS} -name \*.c -print >> $@
	@find ${SUBDIRS} -name \*.h -print >> $@
	@find include -name \*.h -print >> $@

tags: .tags-depend .tags-sources
	ctags -L .tags-sources -o $@

ctags: tags

TAGS: .tags-depend .tags-sources
	etags -o $@ `cat .tags-sources`

etags: TAGS

FORCE:

%_env:
	$(MAKE) -C $(shell echo $@ | sed "s/_env//g") env

env:
	env

# If the cleancount has been changed, force a make clean.
# .cleancount is the global clean count, and .lastclean is the 
# last clean count we had
# We can avoid this by making noclean

cleantest:
	if cmp -s .cleancount .lastclean ; then echo ; else \
		$(MAKE) clean; cp -f .cleancount .lastclean;\
	fi

