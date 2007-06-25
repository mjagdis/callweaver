#
# Main Automake file for CallWeaver.org
#
# Author: Michael "cypromis" Bielicki <michal.bielickie at halo2.pl>
# Copyright (c) 2005, 2006, 2007 Halo Kwadrat Sp. z o.o.
#
# This program is free software and it comes WITHOUT any warranty.
# You may redistribute it and/or modify it under the terms of the
# GNU General Public License (GPL) version 2 as published by the
# Free Software Foundation, but not any later version of the GPL.
#
# $Id$
#
###########
# GENERAL #
###########

AUTOMAKE_OPTS = gnu foreign
ACLOCAL_AMFLAGS = -I acmacros
SH = @SH@
SHELL = @SHELL@
AWK = @AWK@
AR = @AR@
GREP = @GREP@
SED = @SED@
M4 = @M4@
SVN = @SVN@

## LDFLAGS		= --as-needed --enable-new-dtags

##########
# DAEMON #
##########

libexecdir = ${opbxexecdir}
libexec_PROGRAMS = callweaver

callweaver_SOURCES = main.c

callweaver_LDADD = ${top_builddir}/corelib/libcallweaver.la $(LIBLTDL)
callweaver_LDADD += -lreadline

callweaver_CFLAGS =  -fomit-frame-pointer -Wall -Wstrict-prototypes 
callweaver_CFLAGS += -Wmissing-prototypes -Wmissing-declarations -I$(top_srcdir)/include $(AM_CFLAGS)

#############
# UTILITIES #
#############

EXTRA_DIST = substitute.mak

utilitydir = ${opbxutilsdir}
utility_SCRIPTS = callweaver_cli safe_callweaver
CLEANFILES = $(utility_SCRIPTS)

@substitute@

callweaver_cli:
	$(substitute) $(top_srcdir)/callweaver_cli.in > callweaver_cli
	chmod +x callweaver_cli

safe_callweaver:
	$(substitute) $(top_srcdir)/safe_callweaver.in > safe_callweaver
	chmod +x safe_callweaver

#############
# MAN PAGES #
#############

install-man-pages:
if WANT_MAN_PAGES
	if test ! -d $(DESTDIR)${opbxmandir}/man8; then \
		mkdir -p $(DESTDIR)${opbxmandir}/man8 ; \
	fi ; \
	cp $(top_srcdir)/callweaver.8 $(DESTDIR)${opbxmandir}/man8/. ; \
	cp $(top_srcdir)/safe_callweaver.8 $(DESTDIR)${opbxmandir}/man8/. ;
endif

uninstall-man-pages:
	if test -d $(DESTDIR)${opbxmandir}/man8; then \
		rm -rf $(DESTDIR)${opbxmandir}/man8/*callweaver.8 ; \
	fi ;

##############
# OTHER DOCS #
##############

install-docs: install-readme install-license install-credits install-sgml-docs

uninstall-docs:
	if test -d $(DESTDIR)${opbxdocdir}; then \
		rm -rf $(DESTDIR)${opbxdocdir} ; \
	fi ;

# README file
install-readme:
if WANT_README
	if test ! -d $(DESTDIR)${opbxdocdir}; then \
		mkdir -p $(DESTDIR)${opbxdocdir} ; \
	fi ; \
	cp $(top_srcdir)/README $(DESTDIR)${opbxdocdir}/. ;
endif

uninstall-readme:
	if test -f $(DESTDIR)${opbxdocdir}/README; then \
		if test ! -d $(DESTDIR)${opbxdocdir}; then \
			mkdir -p $(DESTDIR)${opbxdocdir} ; \
		fi ; \
		rm -f $(DESTDIR)${opbxdocdir}/README ; \
	fi ;

# LICENSE file
install-license:
if WANT_LICENSE
	if test ! -d $(DESTDIR)${opbxdocdir}; then \
		mkdir -p $(DESTDIR)${opbxdocdir} ; \
	fi ; \
	cp $(top_srcdir)/LICENSE $(DESTDIR)${opbxdocdir}/. ;
endif

uninstall-license:
	if test -f $(DESTDIR)${opbxdocdir}/LICENSE; then \
		rm -f $(DESTDIR)${opbxdocdir}/LICENSE ; \
	fi ;

# CREDITS file
install-credits:
if WANT_CREDITS
	if test ! -d $(DESTDIR)${opbxdocdir}; then \
		mkdir -p $(DESTDIR)${opbxdocdir} ; \
	fi ; \
	cp $(top_srcdir)/CREDITS $(DESTDIR)${opbxdocdir}/. ;
endif

uninstall-credits:
	if test -f $(DESTDIR)${opbxdocdir}/CREDITS; then \
		rm -f $(DESTDIR)${opbxdocdir}/CREDITS ; \
	fi ;

# SGML docs
install-sgml-docs:
if WANT_SGML_DOCS
	if test ! -d $(DESTDIR)${opbxdocdir}; then \
		mkdir -p $(DESTDIR)${opbxdocdir} ; \
	fi ; \
	cp $(top_srcdir)/callweaver.sgml $(DESTDIR)${opbxdocdir}/. ;
endif

uninstall-sgml-docs:
	if test -f $(DESTDIR)${opbxdocdir}/callweaver.sgml; then \
		rm -f $(DESTDIR)${opbxdocdir}/callweaver.sgml ; \
	fi ;

###################
# SUB DIRECTORIES #
###################

SUBDIRS =   libltdl libs
SUBDIRS +=  stdtime @SQLITE3_SUBDIR@ include corelib . pbx funcs
SUBDIRS +=  cdr apps codecs formats channels ogi utils res configs

############################
# INSTALLATION DIRECTORIES #
############################

# mutable data
INSTALL_DIRS =	$(opbxvardir)

# built-in database
INSTALL_DIRS +=	$(opbxdbdir) $(opbxsqlitedir)

# loadable modules
INSTALL_DIRS +=	$(opbxmoddir) $(opbxicdmoddir)

# CDRs and log files
INSTALL_DIRS +=	$(opbxlogdir) $(opbxlogdir)/cdr-custom $(opbxlogdir)/cdr-csv

# pid file and console socket
INSTALL_DIRS +=	$(opbxrundir)

# spool files
INSTALL_DIRS +=	$(opbxspooldir) $(opbxspooldir)/outgoing $(opbxspooldir)/voicemail

# on-hold music
INSTALL_DIRS +=	$(opbxmohdir)

# crypto keys
INSTALL_DIRS +=	$(opbxkeydir)

# images
INSTALL_DIRS +=	$(opbximagesdir)

# create directories, set ownership and permissions
install-data-hook:
	for install_dir in $(INSTALL_DIRS); \
	do \
		if test ! -d "$(DESTDIR)$${install_dir}"; \
		then \
			mkdir -p $(DESTDIR)$${install_dir}; \
			chmod 0750 $(DESTDIR)$${install_dir}; \
		fi; \
		if test -z "$(DESTDIR)"; \
		then \
			chown ${opbxrunuser}:${opbxrungroup} $(DESTDIR)$${install_dir}; \
		fi; \
	done

# END OF FILE
