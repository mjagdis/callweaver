AUTOMAKE_OPTS = gnu

bin_PROGRAMS = streamplayer # check_expr 
streamplayer_SOURCES = streamplayer.c ${top_srcdir}/corelib/strcompat.c
# check_expr_SOURCES = check_expr.c ../opbx_expr2.c ../opbx_expr2f.c
# check_expr_CFLAGS  = -DNO_OPX_MM -D_GNU_SOURCE -DSTANDALONE $(AM_CFLAGS)

if USE_NEWT
	bin_PROGRAMS += opbxman
	opbxman_SOURCES = opbxman.c ${top_srcdir}/corelib/md5.c
	opbxman_LDADD = -lnewt
endif USE_NEWT
