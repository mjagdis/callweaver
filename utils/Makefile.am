AUTOMAKE_OPTS = gnu

AM_CFLAGS			= @CW_CFLAGS@


noinst_SCRIPTS = cc
noinst_PROGRAMS = genkeywords
cwutils_PROGRAMS = streamplayer # check_expr 

streamplayer_SOURCES = streamplayer.c ${top_srcdir}/corelib/strcompat.c

genkeywords_SOURCES = genkeywords.c
genkeywords_CFLAGS	= $(AM_CFLAGS) -I$(top_srcdir)/include

# check_expr_SOURCES = check_expr.c ../cw_expr2.c ../cw_expr2f.c
# check_expr_CFLAGS  = $(AM_CFLAGS) -DNO_OPX_MM -D_GNU_SOURCE -DSTANDALONE

if USE_NEWT
    cwutils_PROGRAMS += cwman
    cwman_CFLAGS = $(AM_CFLAGS)
    cwman_SOURCES = cwman.c ${top_srcdir}/corelib/utils.c
    cwman_LDFLAGS = @OPENSSL_LDFLAGS@
    cwman_LDADD = -lnewt @OPENSSL_LIBS@
endif USE_NEWT

if WANT_SMSQ
    cwutils_PROGRAMS += smsq
    smsq_CFLAGS = $(AM_CFLAGS) -DSPOOLDIR=\"$(cwspooldir)\"
    smsq_SOURCES = smsq.c
    smsq_LDADD = -lpopt
endif WANT_SMSQ
