#
# OpenPBX -- A telephony toolkit for Linux.
# 
# Makefile for CDR backends (dynamically loaded)
#
# Copyright (C) 1999-2005, Digium
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

MODS=cdr_csv.so cdr_custom.so

CFLAGS+=-fPIC

ifeq ($(findstring BSD,${OSARCH}),BSD)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

#The problem with sparc is the best stuff is in newer versions of gcc (post 3.0) only.
#This works for even old (2.96) versions of gcc and provides a small boost either way.
#A ultrasparc cpu is really v9 but the stock debian stable 3.0 gcc doesn.t support it.
#So we go lowest common available by gcc and go a step down, still a step up from
#the default as we now have a better instruction set to work with. - Belgarath
ifeq ($(PROC),sparc64)
 PROC=ultrasparc
  CFLAGS += -mtune=$(PROC) -O3 -pipe -fomit-frame-pointer -mcpu=v8
endif

#
# unixODBC stuff...
#
ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/odbcinst.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/odbcinst.h),)
  MODS+=cdr_odbc.so
endif

all: depend $(MODS)

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

clean:
	rm -f *.so *.o .depend

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

ifneq ($(wildcard .depend),)
  include .depend
endif

cdr_odbc.so: cdr_odbc.o
	$(CC) $(SOLINK) -o $@ $< -lodbc $(MLFLAGS)

cdr_tds.so: cdr_tds.o
	$(CC) $(SOLINK) -o $@ $< -ltds $(MLFLAGS)

cdr_pgsql.so: cdr_pgsql.o
	$(CC) $(SOLINK) -o $@ $< -lpq -lz $(MLFLAGS)

cdr_sqlite.so: cdr_sqlite.o
	$(CC) $(SOLINK) -o $@ $< -lsqlite $(MLFLAGS)

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
