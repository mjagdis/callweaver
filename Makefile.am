#
#	Main Automake file for OpenPBX
#
#	Author: Michael "cypromis" Bielicki <michal.bielickie@halo2.pl>
#	Copyright <c> 2005 Halo Kwadrat Sp. z o.o.
#
#	This program is free software; you can redistribute it and/or
#	modify it under the terms of the GNU General Public License
#	as published by the Free Software Foundation; either version
#	2 of the License, or (at your option) any later version.


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

sbin_PROGRAMS = openpbx 

openpbx_SOURCES 	= main.c
openpbx_LDADD		= ${top_srcdir}/corelib/libopenpbx.la ${top_srcdir}/editline/src/libedit.la $(LIBLTDL)

openpbx_CFLAGS 	=  -fomit-frame-pointer -Wall -Wstrict-prototypes 
openpbx_CFLAGS		+= -Wmissing-prototypes -Wmissing-declarations $(AM_CFLAGS)

man_MANS = openpbx.8

EXTRA_DIST = openpbx.sgml $(man_MANS)

SUBDIRS =   libltdl libs editline stdtime sqlite3-embedded corelib . pbx funcs
SUBDIRS +=  cdr apps codecs formats channels agi utils res configs sounds

install-data-hook:
	mkdir -p $(DESTDIR)$(opbxspooldir)
	mkdir -p $(DESTDIR)$(opbxlogdir)
	mkdir -p $(DESTDIR)$(opbxrundir)
	mkdir -p $(DESTDIR)$(opbxsqlitedir)
	mkdir -p $(DESTDIR)$(opbximagesdir)
	mkdir -p $(DESTDIR)$(opbxdocdir)
	mkdir -p $(DESTDIR)$(opbxkeydir)
	mkdir -p $(DESTDIR)$(opbxvardir)
	mkdir -p $(DESTDIR)$(opbxdbdir)
