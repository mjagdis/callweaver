AM_OPTIONS = gnu
noinst_PROGRAMS = test
noinst_LTLIBRARIES = libtime.la 

test_SOURCES = test.c
libtime_la_SOURCES = localtime.c tzinfo.h private.h
libtime_la_LDFLAGS = -no-undefined -avoid-version 

#-static
