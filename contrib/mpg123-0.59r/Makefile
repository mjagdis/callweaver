###
###   mpg123  Makefile
###

# Where to install binary and manpage on "make install":

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
MANDIR=$(PREFIX)/man
SECTION=1

###################################################
######                                       ######
######   End of user-configurable settings   ######
######                                       ######
###################################################

all:
	$(MAKE) CC=gcc LDFLAGS= \
		OBJECTS='decode.o dct64.o audio_oss.o term.o' \
		CFLAGS='-O3  -mfpmath=sse -msse2  -mmmx -m3dnow  -ffast-math -I/usr/local/include -DLINUX -DREAL_IS_FLOAT' \
		mpg123-make


###########################################################################
###########################################################################
###########################################################################

sajberplay-make:
	@ $(MAKE) CFLAGS='$(CFLAGS)' BINNAME=sajberplay mpg123

mpg123m-make:
	@ $(MAKE) CFLAGS='$(CFLAGS)' BINNAME=mpg123m mpg123

mpg123-make:
	@ $(MAKE) CFLAGS='$(CFLAGS)' BINNAME=mpg123 mpg123

mpg123: mpg123.o common.o $(OBJECTS) decode_2to1.o decode_4to1.o \
		tabinit.o audio.o layer1.o layer2.o layer3.o buffer.o \
		getlopt.o httpget.o xfermem.o equalizer.o \
		decode_ntom.o Makefile wav.o readers.o getbits.o \
		control_generic.o
	$(CC) $(CFLAGS) $(LDFLAGS)  mpg123.o tabinit.o common.o layer1.o \
		layer2.o layer3.o audio.o buffer.o decode_2to1.o equalizer.o \
		decode_4to1.o getlopt.o httpget.o xfermem.o decode_ntom.o \
		wav.o readers.o getbits.o control_generic.o \
		$(OBJECTS) -o $(BINNAME) -lm $(AUDIO_LIB)

mpg123.exe: mpg123.o common.o $(OBJECTS) decode_2to1.o decode_4to1.o \
		tabinit.o audio.o layer1.o layer2.o layer3.o buffer.o \
		getlopt.o httpget.o Makefile wav.o readers.o getbits.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o mpg123.exe -lm $(LIBS)

###########################################################################
###########################################################################
###########################################################################

layer1.o:	mpg123.h
layer2.o:	mpg123.h l2tables.h
layer3.o:	mpg123.h huffman.h common.h getbits.h
decode.o:	mpg123.h
decode_2to1.o:	mpg123.h
decode_4to1.o:	mpg123.h
decode_ntom.o:	mpg123.h
decode_i386.o:	mpg123.h
common.o:	mpg123.h common.h
mpg123.o:	mpg123.c mpg123.h getlopt.h xfermem.h version.h buffer.h term.h
mpg123.h:	audio.h
audio.o:	mpg123.h
audio_oss.o:	mpg123.h
audio_sun.o:	mpg123.h
audio_sgi.o:	mpg123.h
audio_hp.o:	mpg123.h
audio_nas.o:	mpg123.h
audio_os2.o:	mpg123.h
audio_dummy.o:	mpg123.h
buffer.o:	mpg123.h xfermem.h buffer.h
getbits.o:	common.h mpg123.h
tabinit.o:	mpg123.h audio.h
getlopt.o:	getlopt.h
httpget.o:	mpg123.h
dct64.o:	mpg123.h
dct64_i386.o:	mpg123.h
xfermem.o:	xfermem.h
equalizer.o:	mpg123.h
control_sajber.o:	jukebox/controldata.h mpg123.h
wav.o:		mpg123.h
readers.o:	mpg123.h buffer.h common.h
term.o:		mpg123.h buffer.h term.h common.h

###########################################################################
###########################################################################
###########################################################################

clean:
	rm -f *.o *core *~ mpg123 gmon.out sajberplay system mpg123m

prepared-for-install:
	@if [ ! -x mpg123 ]; then \
		echo '###' ; \
		echo '###  Before doing "make install", you have to compile the software.' ; \
		echo '### Type "make" for more information.' ; \
		echo '###' ; \
		exit 1 ; \
	fi

system: mpg123.h system.c
	$(CC) -o $@ -Wall -O2 system.c

install:	prepared-for-install
	strip mpg123
	if [ -x /usr/ccs/bin/mcs ]; then /usr/ccs/bin/mcs -d mpg123; fi
	mkdir -p $(BINDIR)
	mkdir -p $(MANDIR)/man$(SECTION)
	cp -f mpg123 $(BINDIR)
	chmod 755 $(BINDIR)/mpg123
	cp -f mpg123.1 $(MANDIR)/man$(SECTION)
	chmod 644 $(MANDIR)/man$(SECTION)/mpg123.1

dist:	clean
	DISTNAME="`basename \`pwd\``" ; \
	sed '/prgDate/s_".*"_"'`date +%Y/%m/%d`'"_' version.h > version.new; \
	mv -f version.new version.h; \
	cd .. ; \
	rm -f "$$DISTNAME".tar.gz "$$DISTNAME".tar ; \
	tar cvf "$$DISTNAME".tar "$$DISTNAME" ; \
	gzip -9 "$$DISTNAME".tar


