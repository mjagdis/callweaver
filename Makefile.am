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

##########
# DAEMON #
##########

libexecdir = ${opbxexecdir}
libexec_PROGRAMS = openpbx

openpbx_SOURCES = main.c

openpbx_LDADD	= ${top_builddir}/corelib/libopenpbx.la $(LIBLTDL)
openpbx_LDADD	+= -lreadline

openpbx_CFLAGS 	=  -fomit-frame-pointer -Wall -Wstrict-prototypes 
openpbx_CFLAGS	+= -Wmissing-prototypes -Wmissing-declarations -I$(top_srcdir)/include $(AM_CFLAGS)

#############
# UTILITIES #
#############

EXTRA_DIST = substitute.mak

utilitydir = ${opbxutilsdir}
utility_SCRIPTS = opbxcli safe_openpbx
CLEANFILES = $(utility_SCRIPTS)

@substitute@

opbxcli:
	$(substitute) $(top_srcdir)/opbxcli.in > opbxcli
	chmod +x opbxcli

safe_openpbx:
	$(substitute) $(top_srcdir)/contrib/scripts/safe_openpbx > safe_openpbx
	chmod +x safe_openpbx

#############
# MAN PAGES #
#############

install-man-pages:
if WANT_MAN_PAGES
	if test ! -d $(DESTDIR)${opbxmandir}; then \
		mkdir -p $(DESTDIR)${opbxmandir} ; \
	fi ; \
	cp $(top_srcdir)/openpbx.8 $(DESTDIR)${opbxmandir}/. ; \
	cp $(top_srcdir)/contrib/scripts/safe_openpbx.8 $(DESTDIR)${opbxmandir}/. ;
endif

uninstall-man-pages:
	if test -d $(DESTDIR)${opbxmandir}; then \
		rm -rf $(DESTDIR)${opbxmandir} ; \
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
	cp $(top_srcdir)/openpbx.sgml $(DESTDIR)${opbxdocdir}/. ;
endif

uninstall-sgml-docs:
	if test -f $(DESTDIR)${opbxdocdir}/openpbx.sgml; then \
		rm -f $(DESTDIR)${opbxdocdir}/openpbx.sgml ; \
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
