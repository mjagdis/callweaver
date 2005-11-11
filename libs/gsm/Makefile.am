AUTOMAKE_OPTS 		= gsm
opbxlib_LTLIBRARIES 	= libopbxgsm.la
libopbxgsm_la_SOURCES 	= src/add.c src/code.c src/debug.c src/decode.c \
			  src/long_term.c src/lpc.c src/preprocess.c \
			  src/rpe.c src/gsm_destroy.c src/gsm_decode.c \
			  src/gsm_encode.c src/gsm_implode.c src/gsm_create.c \
			  src/gsm_print.c src/gsm_option.c src/short_term.c \
			  src/table.c inc/gsm.h inc/config.h inc/private.h \
			  inc/proto.h inc/unproto.h 
libopbxgsm_a_CFLAGS  	= -DWAV49 -DSASR $(AM_CFLAGS) -Iinc
libopbxgsm_la_LDFLAGS	= -no-undefined 
INCLUDES		= -I$(srcdir)/inc -Isrc

