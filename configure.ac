#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.


AC_PREREQ(2.59)
AC_INIT(OpenPBX, 1,openpbx-dev@openpbx.org)
AM_INIT_AUTOMAKE(OpenPBX,1)

AC_PATH_PROG([GROUPADD], [groupadd])
AC_PATH_PROGS(GREP, ggrep grep,[echo Missing grep so skipping but I doubt we will get anywhere])
AC_PATH_PROGS(SVN, svn, [echo Missing subversion so some stuff will be borked],${PATH}:/opt/csw/bin)
AC_PATH_PROGS(RANLIB,ranlib granlib,[echo Skipping ranlib])
AC_PATH_PROGS(AR,ar gar,[echo Missing ar so skipping it but that will not work ...])
AC_PATH_PROGS(SHELL,bash sh,[echo No compatible shell found])
AC_PATH_PROGS(SH,bash sh,[echo No compatible shell found])
AC_PATH_PROGS(M4,gm4 m4,[echo No m4 found, who will process my macros now ?])
AC_PATH_PROGS(SED,gsed sed,[echo sed not found, doh!])

ASTLINK="-Wl,-E"
SOLINK="-shared -Xlinker -x"
VALGRIND="" #-DMAKE_VALGRIND_HAPPY
OPTIMIZECPU="no"
TOPDIR=`pwd`
AC_SUBST(TOPDIR)

dnl Shamelessly stolen from editline configure.in to create the relevant cygwin stuff
dnl Platform-specific settings.  The ABI can probably be determined
dnl programmatically, but doing so is error-prone, which makes it generally
dnl not worth the trouble.
AC_CANONICAL_HOST

case "${host}" in
  *-*-darwin*)
        ;;
  *-*-freebsd*)
        ;;
  *-aix*)
    AC_DEFINE(AIX,,[Define if AIX])
     broken_types=yes
    ;;
  *-osf4*)
    AC_DEFINE(OSF1,,[Define if OSF1])
    tru64_types=yes
    ;;
  *-osf5.1*)
    AC_DEFINE(OSF1)
    ;;
  *-tru64*)
    AC_DEFINE(OSF1)
    tru64_types=yes
    ;;
  *-*-linux* )
	;;
  *cygwin*)
	AC_DEFINE([_WIN32_WINNT],[0x0500],[Heya, it's windows])
	AC_DEFINE([INET_ADDRSTRLEN],[16],[cygwin detection does not work for that, anybody has an idea ?])
	AC_DEFINE([INET6_ADDRSTRLEN],[46],[cygwin detection does not work for that, anybody has an idea ?])
        ;;
  *-*-netbsd*)
        ;;
  *-*-solaris2*)
        ;;
  *)
        AC_MSG_RESULT(Unsupported operating system: ${host})
        ;;
esac    

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# Checks for programs.
AC_PROG_CXX
AC_PROG_YACC
AC_PROG_CC
AC_PROG_CPP
AC_PROG_LEX
AC_PROG_INSTALL

# Checks for libraries.
AC_CHECK_LIB([asound], [main])
AC_CHECK_LIB([c], [main])
AC_CHECK_LIB([crypto], [main])
AC_CHECK_LIB([db], [main])
AC_CHECK_LIB([dl], [main])
AC_CHECK_LIB([expat], [main])
AC_CHECK_LIB([ib], [main])
AC_CHECK_LIB([kdecore], [main])
AC_CHECK_LIB([kdeui], [main])
AC_CHECK_LIB([q931], [main],[AC_DEFINE([VISDN_LIBS],[-lq931],[libq931 required for visdn...found!])])
AC_CHECK_LIB([m], [main])
AC_CHECK_LIB([nbs], [main])
AC_CHECK_LIB([ncurses], [main],,[AC_CHECK_LIB([curses],[main])])
AC_CHECK_LIB([newt], [main])
AC_CHECK_LIB([nsl], [main])
AC_CHECK_LIB([odbc], [main])
AC_CHECK_LIB([ossaudio], [main])
AC_CHECK_LIB([pq], [main])
AC_CHECK_LIB([pri], [main])
AC_CHECK_LIB([pthread], [main])
AC_CHECK_LIB([qt], [main])
AC_CHECK_LIB([resolv], [main])
AC_CHECK_LIB([rt], [main])
AC_CHECK_LIB([socket], [main])
AC_CHECK_LIB([spandsp], [main])
AC_CHECK_LIB([sqlite], [main])
AC_CHECK_LIB([ssl], [main])
AC_CHECK_LIB([t], [main])
AC_CHECK_LIB([tds], [main])
AC_CHECK_LIB([termcap], [main])
AC_CHECK_LIB([tonezone], [main])
AC_CHECK_LIB([unicall], [main])
AC_CHECK_LIB([vpb], [main])
AC_CHECK_LIB([z], [main])
AC_CHECK_LIB([srtp], [main])
AC_CHECK_LIB([js], [main])


AC_C_BIGENDIAN(AC_DEFINE([__BYTE_ORDER],__BIG_ENDIAN,[Big Endian]),AC_DEFINE([__BYTE_ORDER],__LITTLE_ENDIAN,[Little Endian]))
AC_DEFINE([__LITTLE_ENDIAN],1234,[for the places where it is not defined])
AC_DEFINE([__BIG_ENDIAN],4321,[for the places where it is not defined])


### Overwrite your config files with the samples provided
AC_ARG_ENABLE(overwrite,     
 [  --enable-overwrite  overwrite your config with conf file samples],OVERWRITE=yes, )
 
if test "$OVERWRITE" = "yes" ; then 
	OVERWRITE="yes"
fi

### Select which BUSYDETECT to use
AC_ARG_ENABLE(busydetect,
 [  --enable-busydetect [=busydetect type]	select which busydetect version to use:
						none (default)
						toneonly
						martin
						compare_tone_and_silence],
[if test "x$enableval" = "xnone" ; then
	echo
   elif test "x$enableval" = "xtoneonly" ; then
      BUSYDETECT=BUSYDETECT_TONEONLY
   elif test "x$enableval" = "xmartin" ; then
      BUSYDETECT=BUSYDETECT_MARTIN
   elif test "x$enableval" = "xtone_and_silence" ; then
      BUSYDETECT=BUSYDETECT_TONE_AND_SILENCE
   else
      echo
      echo "Error!"
      echo "Unknown busydetect type"
      exit -1
   fi
])

AC_DEFINE([BUSYDTECT],$(BUSYDETECT),[defined way of finding out how to detect busy])


### Optimize for host cpu version
AC_ARG_ENABLE(optimization,     
 [  --disable-optimization    no detection or tuning flags for cpu version],, OPTIMIZECPU=yes)
if test $OPTIMIZECPU = "yes" ; then 
	DEBUG="-g"
fi

profile=no
AC_ARG_ENABLE(profiling,
	[  --enable-profiling			enable compiling with profiling support],profile=yes,)
if test $profile = "yes" ; then
	DEBUG="-g -pg"
fi


javascript=no
AC_ARG_ENABLE(javascript,
	[  --enable-javascript			Enable res_js javascript extension logic],javascript=yes,)
if test $javascript = "yes" ; then
	USE_RESJS="1"
	for w in /usr/lib /usr/local/lib /usr/sfw/lib /usr/lib64; do
	                # check for plain setups 
	                if test -f $w/libjs.a ; then
	                        LIBJS=$w/libjs.a
	                        break
	                fi
	done
fi

AC_ARG_WITH(libjs, [ --with-libjs=PREFIX    Location of libjs so and .a files.],
	LIBJS="$withval")

AC_SUBST(libjs_location)

AC_SUBST(USE_RESJS)
AC_SUBST(LIBJS)

AC_ARG_WITH(nspr-includes, [  --with-nspr-includes=PREFIX     Location of Mozilla nspr4 includes.],
	with_nspr_includes="$withval", with_nspr_includes="/usr/include/mozilla")
have_nspr_includes="no"
if test "x${with_nspr_includes}" != "xno"; then
	CPPFLAGS_save="$CPPFLAGS"
	
	AC_MSG_CHECKING(for Mozilla nspr4 includes in $with_nspr_includes)
	AC_MSG_RESULT("")
	
	CPPFLAGS="$CPPFLAGS -I$with_nspr_includes"
	AC_CHECK_HEADERS(nspr.h prio.h, [ moz_nspr_includes="yes" ])
	CPPFLAGS="$CPPFLAGS_save"
	
	if test "x{$moz_nspr_includes}" != "xno" -a "x{$moz_nspr_includes}" != "x" ; then
		have_nspr_includes="yes"
		NSPR_CFLAGS="-I$with_nspr_includes"
	fi
else
	AC_MSG_CHECKING(for Mozilla nspr4 includes)
	AC_MSG_RESULT(no)
fi

AC_SUBST(NSPR_CFLAGS)


AC_ARG_WITH(nspr-libs, [  --with-nspr-libs=PREFIX     Location of Mozilla nspr4 libs.],
	with_nspr_libs="$withval")
have_nspr_libs="no"
if test "x${with_nspr_libs}" != "xno" -a "x${have_nspr_includes}" != "xno"; then
	
	nsprlibs="-lnspr4"
	AC_CACHE_CHECK([for Mozilla nspr libraries], moz_nspr_libs,
	[
		
		if test "x${with_nspr_libs}" != "x"; then
			NSPR_LDFLAGS="-L$with_nspr_libs $nsprlibs"
		else
			NSPR_LDFLAGS="$nsprlibs"
		fi
		
		AC_TRY_LINK_FUNC(PR_Init, moz_nspr_libs="yes", moz_nspr_libs="no")
	])
	if test "x$moz_nspr_libs" != "xno"; then
		have_nspr_libs="yes"
		NSPR_LDFLAGS="-L$with_nspr_libs $nsprlibs"
	else
		NSPR_CLFAGS=""
	fi
else
	AC_MSG_CHECKING(for Mozilla nspr4 libraries)
	AC_MSG_RESULT(no)
fi
AC_SUBST(NSPR_LDFLAGS)

low_memory=no
AC_ARG_ENABLE(low_memory,
	[  --enable-low_memory			enable low_memory support],low_memory=yes,)
if test $low_memory = "yes" ; then
	LOW_MEMORY="-DLOW_MEMORY"
fi


srtp=no
AC_ARG_ENABLE(srtp,
        [  --enable-srtp                  enable Secure RTP based on libsrtp support],srtp=yes,)
if test $srtp = "yes" ; then
        ENABLE_SRTP="-DENABLE_SRTP"
fi

AC_SUBST(ENABLE_SRTP)

#
# Select user and group
#
OPBXRUNUSER="openpbx"
OPBXRUNGROUP="openpbx"

AC_ARG_WITH(openpbx-user,
	[  --with-openpbx-user=USER             use USER instead of default (openpbx)],[opbx_user="$withval"],[opbx_user="no"])

if test "x$opbx_user" != "xno" && test "x$opbx_user" != "xyes"; then
	OPBXRUNUSER="$opbx_user"
fi

AC_ARG_WITH(openpbx-group,
	[  --with-openpbx-group=GROUP           use GROUP instead of default (openpbx)],[opbx_group="$withval"],[opbx_group="no"])

if test "x$opbx_group" != "xno" && test "x$opbx_group" != "xyes"; then
	OPBXRUNGROUP="$opbx_group"
fi
# set vars
AC_SUBST(OPBXRUNUSER)
AC_SUBST(OPBXRUNGROUP)  


#### now for the channels. you can select some channels by using with, same will be for apps and ressources

### EXOSIP2 based SIP channel
chan_exosip2="no"
AC_ARG_WITH(chan_exosip2,
	[  --with-chan_exosip2=no		use chan_exosip2, an alternative SIP channel driver based on exosip (default=no)],[chan_exosip="$withval"],[chan_exosip2="no"])

AC_CHECK_LIB(ortp, [main],[HAVE_ORTP="yes"])
AC_CHECK_LIB(osip2,[main],[HAVE_OSIP2="yes"])
AC_CHECK_LIB(eXosip2,[main],[HAVE_EXOSIP2="yes"])

if test "x$chan_exosip2" != "no" && test "x$chan_exosip2" != "x" && test "x$HAVE_ORTP" == "xyes" && test "x$HAVE_OSIP2" == "xyes" && test "x$HAVE_EXOSIP2" == "xyes"; then
	EXOLIBS="-leXosip2 -losip2 -losipparser2 -lortp -lpthread"
fi

AC_SUBST([EXOLIBS])
####### apps

######  icd
#app_icd = "no"
#AC_ARG_WITH(app_icd,
#	[ --with-app_icd=no			use app_icd, a full blown inteligent call distribution system (default=no) *** HIGHLY EXPERIMENTAL FOR THE MOMENT ****],[app_icd="$withval"],[app_icd="no"])
#
#

##### ressources

#SVN Revision number For version number
if test -n $SVN ; then
	SUBVERSION=`${SVN} info | ${GREP} Revision | awk '{ print $2 }'`
fi


MYSQL="yes"

AC_MSG_CHECKING(for MySQL)
AC_ARG_WITH(mysql,
[  --with-mysql[=DIR]        Normally, configure will scan all possible default
                          installation paths for mysql. When its fail, use
                          this command to tell configure where mysql
                          installation root directory is.
                          ],
MYSQL="$withval"
dnl if not given argument, assume standard
)

if test "${MYSQL}" = "yes"; then
        # Autodetect
        for w in /usr/include /usr/local/include /usr/mysql /usr/local/mysql /usr/local/mysql/include /opt /opt/mysql /usr/sfw/include; do
                # check for plain setups
                if test -f $w/mysql.h; then
                        MYSQL_INCDIR=$w
                        break
                fi
                # check for "/usr/include/<packagename>" type setups
                if test -f $w/mysql/mysql.h; then
                        MYSQL_INCDIR=$w/mysql
                        break
                fi
                # check for "/usr/<packagename>/include" type setups
                if test -f $w/mysql/include/mysql.h; then
                        MYSQL_INCDIR=$w/mysql/include
                        break
                fi
        done

        for w in /usr/lib /usr/local/lib /usr/mysql /usr/local/mysql /usr/local/mysql/lib /opt /opt/mysql /usr/sfw/lib; do
                # check for plain setups
                if test -f $w/libmysqlclient.a -o -f $w/libmysqlclient.so; then
                        MYSQL_LIBDIR=$w
                        break
                fi
                # check for "/usr/lib/<packagename>" type setups
                if test -f $w/mysql/libmysqlclient.a -o -f $w/mysql/libmysqlclient.so; then
                        MYSQL_LIBDIR=$w/mysql
                        break
                fi
                # check for "/usr/<packagename>/lib" type setups
                if test -f $w/mysql/lib/libmysqlclient.a -o -f $w/mysql/lib/libmysqlclient.so; then
                        MYSQL_LIBDIR=$w/mysql/lib
                        break
                fi
        done
else
        # Manual detection for <withval>/include/<packagename>
        # and <withval>/include.
        if test -f $withval/include/mysql/mysql.h; then
                MYSQL_INCDIR=$withval/include/mysql
        elif test -f $withval/include/mysql.h; then
                MYSQL_INCDIR=$withval/include
        fi

        # Manual detection for <withval>/lib/<packagename>
        # and <withval>/lib.

        if test -f $withval/lib/mysql/libmysqlclient.a -o -f $withval/lib/mysql/libmysqlclient.so; then
                MYSQL_LIBDIR=$withval/lib/mysql
        elif test -f $withval/lib/libmysqlclient.a -o -f $withval/lib/libmysqlclient.so; then
                MYSQL_LIBDIR=$withval/lib
        fi
fi

## Did we find anything?
if test "${MYSQL}" = "no"; then
        AC_MSG_RESULT(skipped)
else
        if test -z "$MYSQL_LIBDIR" ; then
                AC_MSG_RESULT(no)
                echo Invalid MySQL directory - unable to find libmysqlclient.a or libmysqlclient.so.
        elif test -z "$MYSQL_INCDIR" ; then
                AC_MSG_RESULT(no)
                echo Invalid MySQL directory - unable to find mysql.h.
        else
                TEMP_LIBS="$TEMP_LIBS -L$MYSQL_LIBDIR -lmysqlclient"
                #Add -lz for some mysql installs....
                TEMP_LIBS="$TEMP_LIBS -lz"
                TEMP_CFLAGS="$TEMP_CFLAGS -DHAVE_MYSQL -I $MYSQL_INCDIR"
                AC_MSG_RESULT(yes)
                MYSQL_SUPPORT="yes"
        fi
fi

dnl Start Check for Postgresql
AC_DEFUN(PGSQL_INC_CHK,[if test -r $i$1/libpq-fe.h; then PGSQL_DIR=$i; PGSQL_INCDIR=$i$1])
PGSQL="yes"

AC_MSG_CHECKING(for PostgreSQL)
AC_ARG_WITH(pgsql,
[  --with-pgsql[=DIR]        Include PostgreSQL support.  DIR is the PostgreSQL
                          base install directory. If not specified configure will
                          search in /usr, /usr/local and /usr/local/pgsql.
                          ],
PGSQL="$withval"
dnl if not given argument, assume standard
)
if test "${PGSQL}" = "no"; then
        AC_MSG_RESULT(skipped)
fi

if test "${PGSQL}" = "yes"; then
        for i in /usr /usr/local /usr/local/pgsql $PHP_PGSQL; do
                PGSQL_INC_CHK(/include)
                el[]PGSQL_INC_CHK(/include/pgsql)
                el[]PGSQL_INC_CHK(/include/postgresql)
                fi
        done

        if test -z "$PGSQL_DIR"; then
                AC_MSG_RESULT(Cannot find libpq-fe.h. Please specify the installation path of PostgreSQL)
        else
		HAVE_PGSQL="1"
                PGSQL_INCLUDE="-I$PGSQL_INCDIR"
                PGSQL_LIBDIR=$PGSQL_DIR/lib
                AC_MSG_RESULT(yes)
                test -d $PGSQL_DIR/lib/pgsql && PGSQL_LIBDIR=$PGSQL_DIR/lib/pgsql
                LDFLAGS="$TEMP_LDFLAGS -L$PGSQL_LIBDIR"
                AC_CHECK_LIB(pq, PQcmdTuples,AC_DEFINE(HAVE_PQCMDTUPLES,1,[ ]))
                AC_CHECK_LIB(pq, PQoidValue,AC_DEFINE(HAVE_PQOIDVALUE,1,[ ]))
                AC_CHECK_LIB(pq, PQclientEncoding,AC_DEFINE(HAVE_PQCLIENTENCODING,1,[ ]))
                AC_CHECK_LIB(pq, pg_encoding_to_char,AC_DEFINE(HAVE_PGSQL_WITH_MULTIBYTE_SUPPORT,1,[ ]))
                LDFLAGS=""     
                AC_DEFINE(HAVE_PGSQL,1,[ ])
                TEMP_LIBS="$TEMP_LIBS -L$PGSQL_LIBDIR -lpq"
                TEMP_CFLAGS="$TEMP_CFLAGS -DHAVE_PGSQL $PGSQL_INCLUDE"
                PostgreSQL_SUPPORT="yes"
        fi
fi
dnl End Postgresql



             
# Checks for header files.
AC_FUNC_ALLOCA
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([visdn/visdn.h],[AC_DEFINE([WITH_VISDN],["yes"],[visdn seems to be installed])])
AC_CHECK_HEADERS([netinet/in.h])
AC_CHECK_HEADERS([w32api/windows.h])
AC_CHECK_HEADERS([w32api/winsock2.h],[],[],
[#if HAVE_W32API_WINDOWS_H
# include <w32api/windows.h>
# endif
])
AC_CHECK_HEADERS([w32api/ws2tcpip.h],[],[],
[#if HAVE_W32API_WINSOCK2_H
# include <w32api/windows.h>
# include <w32api/winsock2.h>
# endif
])
#AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h limits.h locale.h malloc.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h strings.h sys/ioctl.h sys/param.h sys/socket.h sys/time.h syslog.h termios.h unistd.h utime.h])
# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_HEADER_STDBOOL
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_HEADER_TIME
AC_STRUCT_TM
AC_C_VOLATILE
AC_CHECK_TYPES([ptrdiff_t])
AC_CHECK_DECLS(INET_ADDRSTRLEN,[],[],[#if HAVE_NETINET_IN_H
# include <netinet/in.h>
# endif
#if HAVE_W32API_WS32TCPIP_H
# include w32api/windows.h
# include w32api/winsock2.h  
# include w32api/ws2tcpip.h
#endif
])
AC_CHECK_DECLS(INET6_ADDRSTRLEN,[],[],[#if HAVE_NETINET_IN_H
# include <netinet/in.h>
# endif
#if HAVE_W32API_WS32TCPIP_H
# include w32api/windows.h
# include w32api/winsock2.h
# include w32api/ws2tcpip.h
#endif
])   
AC_CHECK_DECLS(__BEGIN_DECLS,[],AC_DEFINE(__BEGIN_DECLS,,[for stupid ast-db1]))
AC_CHECK_DECLS(__END_DECLS,[],AC_DEFINE(__END_DECLS,,[for stupid ast-db1])) 
# Now comes the types check, horrible shit
if test "$tru64_types" = "yes"; then
        AC_CHECK_TYPE(u_int8_t, unsigned char)
        AC_CHECK_TYPE(u_int16_t, unsigned short)
        AC_CHECK_TYPE(u_int32_t, unsigned int)
else
	if test "$broken_types" = "yes" ; then
		AC_CHECK_TYPE(u_int8_t, unsigned char)
		AC_CHECK_TYPE(u_int16_t, unsigned short)
		AC_CHECK_TYPE(u_int32_t, unsigned long int)
	else
		AC_CHECK_TYPE(u_int8_t, uint8_t)
		AC_CHECK_TYPE(u_int16_t, uint16_t)
		AC_CHECK_TYPE(u_int32_t, uint32_t)
	fi
fi

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_FUNC_STRCOLL
AC_FUNC_STRFTIME
AC_FUNC_STRNLEN
AC_FUNC_STRTOD
AC_FUNC_UTIME_NULL
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([atexit bzero dup2 endpwent floor ftruncate getcwd gethostbyname gethostname gettimeofday inet_ntoa isascii localtime_r memchr memmove memset mkdir munmap pow putenv re_comp regcomp rint select setenv socket sqrt strsep strcasecmp strchr strcspn strdup strerror strncasecmp strndup strrchr strspn strstr strtol strtoq unsetenv utime])


if test "x${OPTIMIZECPU}" = "xno"; then
		dnl Try to autodetect cpu type
		CPU_NAME="unknown"
		CPU_TYPE="unknown"
		if test -e "/proc/cpuinfo" ; then
			intel[[30]]="-march=i386"
			intel[[32]]="-march=i386"
			intel[[34]]="-march=i386"
			intel[[40]]="-march=i486"
			intel[[41]]="-march=i486"
			intel[[42]]="-march=i486"
			intel[[43]]="-march=i486"
			intel[[44]]="-march=i486"
			intel[[45]]="-march=i486"
			intel[[47]]="-march=i486"
			intel[[48]]="-march=i486"
			intel[[51]]="-march=pentium"
			intel[[52]]="-march=pentium"
			intel[[54]]="-march=pentium-mmx"
			intel[[61]]="-march=pentiumpro"
			intel[[63]]="-march=pentium2"
			intel[[65]]="-march=pentium2"
			intel[[66]]="-march=pentium2"
			intel[[67]]="-march=pentium3"
			intel[[68]]="-march=pentium3"
			intel[[610]]="-march=pentium3"
			intel[[611]]="-march=pentium3"
			intel[[150]]="-march=pentium4"
			intel[[151]]="-march=pentium4"
			intel[[152]]="-march=pentium4"
			amd[[50]]="-march=i586"
			amd[[51]]="-march=i586"
			amd[[52]]="-march=i586"
			amd[[53]]="-march=i586"
			amd[[56]]="-march=k6"
			amd[[57]]="-march=k6"
			amd[[58]]="-march=k6-2"
			amd[[59]]="-march=k6-3"
			amd[[61]]="-march=athlon"
			amd[[62]]="-march=athlon"
			amd[[63]]="-march=athlon"
			amd[[64]]="-march=athlon"
			amd[[66]]="-march=athlon"
			amd[[67]]="-march=athlon"
			amd[[68]]="-march=athlon"
			amd[[155]]="-march=athlon64"
			CPU_TYPE="known"
			CPU_FAMILY=`cat /proc/cpuinfo | ${GREP} "cpu family" | head -n1`
			CPU_MODEL=`cat /proc/cpuinfo | ${GREP} model[[^\ ]] | head -n1`
			CPU_NAME=`cat /proc/cpuinfo | ${GREP} "model name" | head -n1`
			CPU_FLAGS=`cat /proc/cpuinfo | ${GREP} "flags" | head -n1`
			CPU_VENDOR=`cat /proc/cpuinfo | ${GREP} "vendor_id" | head -n1`
			CPU_FAMILY=${CPU_FAMILY#*: }
			CPU_MODEL=${CPU_MODEL#*: }
			CPU_NAME=${CPU_NAME#*: }
			CPU_FLAGS=${CPU_FLAGS#*: }
			CPU_VENDOR=${CPU_VENDOR#*: }
			if test "x${CPU_VENDOR}" = "xGenuineIntel" ; then
				CPU_OPTIONS=${intel[[$CPU_FAMILY$CPU_MODEL]]}
			fi
			if test "x${CPU_VENDOR}" = "xAuthenticAMD" ; then
				CPU_OPTIONS=${amd[[$CPU_FAMILY$CPU_MODEL]]}
			fi
			if test "x${CPU_OPTIONS}" = "x" ; then
				CPU_TYPE="unknown"
			fi
			CPU_EXT=""
			for i in $CPU_FLAGS ; do
				case $i in
				fpu)
					CPU_FPU="-mfpmath=387"
					;;
				mmx)
					CPU_EXT="$CPU_EXT -mmmx"
					;;
				sse)
					CPU_FPU="-mfpmath=sse -msse"
					;;
				sse2)
					CPU_FPU="-mfpmath=sse -msse2"
					;;
				3dnow)
					CPU_EXT="$CPU_EXT -m3dnow"
					;;
				esac
			done
			CPU_OPTIONS="-O3 $CPU_OPTIONS $CPU_FPU $CPU_EXT"
		fi

else
	CPU_OPTIONS=""
fi


AC_SUBST(CPU_TYPE)
AC_SUBST(CPU_FAMILY)
AC_SUBST(CPU_MODEL)
AC_SUBST(CPU_NAME)
AC_SUBST(CPU_FLAGS)
AC_SUBST(CPU_VENDOR)
AC_SUBST(CPU_FAMILY)
AC_SUBST(CPU_MODEL)
AC_SUBST(CPU_NAME)
AC_SUBST(CPU_FLAGS)
AC_SUBST(CPU_VENDOR)
AC_SUBST(SVN)
AC_SUBST(GREP)
AC_SUBST(GROUPADD)
AC_SUBST(VALGRIND)
AC_SUBST(SOLINK)
AC_SUBST(ASTLINK)
AC_SUBST(CPU_OPTIONS)
AC_SUBST(all_debug)
AC_SUBST(DEBUG)
AC_SUBST(DEBUG_THREADS)
AC_SUBST(TRACE_FRAMES)
AC_SUBST(MALLOC_DEBUG)
AC_SUBST(OVERWRITE)
AC_SUBST(LOW_MEMORY)
AC_SUBST(prefix)
AC_SUBST(PGSQL_INCLUDE)
AC_SUBST(PGSQL_LIBDIR)
AC_SUBST(HAVE_PGSQL)
AC_SUBST(BUSYDETECT)
AC_CONFIG_HEADER(include/openpbx/confdefs.h)  
AC_CONFIG_FILES([Makefile
                 agi/Makefile
                 apps/Makefile
                 cdr/Makefile
                 channels/Makefile
                 codecs/Makefile
                 codecs/gsm/Makefile
                 codecs/ilbc/Makefile
                 codecs/lpc10/Makefile
                 db1-ast/Makefile
                 editline/Makefile
                 formats/Makefile
                 funcs/Makefile
                 pbx/Makefile
                 res/Makefile
                 sqlite/Makefile
                 stdtime/Makefile
                 utils/Makefile])
AC_OUTPUT



echo ""
echo "   ************************"
echo "   *   Configure status   *"
echo "   ************************"
echo ""

if test "${MYSQL_SUPPORT}" = "yes"; then
        echo "MYSQL Support:       Yes"
else
        echo "MYSQL Support:       No"
fi

if test "${PostgreSQL_SUPPORT}" = "yes"; then
        echo "PostgreSQL Support:  Yes"
else
        echo "PostgreSQL Support:  No"
fi
echo ""

echo "CFLAGS: $CFLAGS"
echo "LIBS: $LIBS"
echo "LDFLAGS: $LDFLAGS"
echo "BUSYDETECT: $BUSYDETECT"
echo "CPU_OPTIONS: $CPU_OPTIONS"

echo
echo  "Install prefix:       $prefix"
echo

