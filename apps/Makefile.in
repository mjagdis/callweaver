#
# OpenPBX -- A telephony toolkit for Linux.
# 
# Makefile for PBX applications
#
# Copyright (C) 1999-2005, Digium
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

APPS=app_dial.so app_playback.so

#
# Obsolete things...
#
#APPS+=app_sql_postgres.so
#APPS+=app_sql_odbc.so

#
# Experimental things
#
#APPS+=app_ivrdemo.so
#APPS+=app_skel.so
#APPS+=app_rpt.so

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/linux/zaptel.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/zaptel.h),)
  APPS+=app_meetme.so app_flash.so
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/osp/osp.h $(CROSS_COMPILE_TARGET)/usr/include/osp/osp.h),)
APPS+=app_osplookup.so
endif

ifeq ($(findstring BSD,${OSARCH}),BSD)
CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

CFLAGS+=-fPIC

#
# If you have UnixODBC you can use ODBC voicemail
# storage
#
# Uncomment to use ODBC storage
#CFLAGS+=-DUSE_ODBC_STORAGE
# Uncomment for extended ODBC voicemail storage
#CFLAGS+=-DEXTENDED_ODBC_STORAGE
# See doc/README.odbcstorage for more information

all: $(APPS)

clean:
	rm -f *.so *.o look .depend

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

app_rpt.so : app_rpt.o
	$(CC) $(SOLINK) -o $@ $< -ltonezone

install: all
	for x in $(APPS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done
	rm -f $(DESTDIR)$(MODULES_DIR)/app_datetime.so
	rm -f $(DESTDIR)$(MODULES_DIR)/app_qcall.so

app_sql_postgres.o: app_sql_postgres.c
	$(CC) -pipe -I/usr/local/pgsql/include $(CFLAGS) -c -o app_sql_postgres.o app_sql_postgres.c

app_sql_postgres.so: app_sql_postgres.o
	$(CC) $(SOLINK) -o $@ $< -L/usr/local/pgsql/lib -lpq

app_sql_odbc.so: app_sql_odbc.o
	$(CC) $(SOLINK) -o $@ $< -lodbc

look:	look.c
	$(CC) -pipe -O6 -g look.c -o look -lncurses

ifeq (SunOS,$(shell uname))
app_chanspy.so: app_chanspy.o
	$(CC) $(SOLINK) -o $@ $< -lrt
endif


ifneq ($(wildcard .depend),)
 include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`

env:
	env
