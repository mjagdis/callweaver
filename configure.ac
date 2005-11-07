# Main Autoconf configuration file for the OpnePBX.org project
# Copyright <c> 2005 by OpenPBX org
# For a list of Authors check the AUTHORS file in the same directory
# as this file
# Work on the automake stuff was partly sponsored by Halo Kwadrat Sp. z o.o.
#
#       This program is free software; you can redistribute it and/or
#       modify it under the terms of the GNU General Public License
#       as published by the Free Software Foundation; either version
#       2 of the License, or (at your option) any later version.     
# 
# Process this file with autoconf to produce a configure script.
#
######
#
# initialisation of the GNU AutoTools Packages
#
######
AC_PREREQ(2.59)
AC_INIT(OpenPBX.org,1.1-beta,openpbx-dev@openpbx.org)
AM_INIT_AUTOMAKE(OpenPBX.org,1.1-beta)
dnl define GNU Source to make sure every compiler and tool 
dnl  knows that we use GNU compliant macros etc
AC_GNU_SOURCE
AC_LANG_C
AC_C_CONST

######
#
# Define and check for prerequisite programs
#
######
AC_PATH_PROG([GROUPADD], [groupadd])
AC_PATH_PROGS(GREP, ggrep grep,[echo Missing grep so skipping but I doubt we will get anywhere])
AC_PATH_PROGS(SVN, svn, [echo Missing subversion so some stuff will be borked],${PATH}:/opt/csw/bin)
AC_PATH_PROGS(RANLIB,ranlib granlib,[echo Skipping ranlib])
AC_PATH_PROGS(AR,ar gar,[echo Missing ar so skipping it but that will not work ...])
AC_PATH_PROGS(SHELL,bash sh,[echo No compatible shell found])
AC_PATH_PROGS(SH,bash sh,[echo No compatible shell found])
AC_PATH_PROGS(M4,gm4 m4,[echo No m4 found, who will process my macros now ?])
AC_PATH_PROGS(SED,gsed sed,[echo sed not found, doh!])
AC_PATH_PROGS(UNAME,uname,[echo uname not found so no version info will be available])
AC_PROG_CXX
AC_PROG_YACC
AC_PROG_CC
AC_PROG_CPP
AM_PROG_LEX
AX_SYS_PERLSHARPBANG
AC_PROG_INSTALL

dnl SVN Revision number, needed for OPBX_VERSION and inclusion into files version number
if test -n "$SVN" ; then
	AC_DEFINE_UNQUOTED([SVN_VERSION],"`${SVN} info | ${GREP} Revision | ${AWK} '{ print $2 }'`",[Which Subversion revision are we building here ?] )
	AC_SUBST([SVN_VERSION])
else
	AC_DEFINE_UNQUOTED([SVN_VERSION],"UNKNOWN",[Unknown Subversion because it's not on this machine.] )
	AC_SUBST([SVN_VERSION])
fi   

######
#
# Libtool definitions
#
######
dnl check for dlopen support (for shared modules)
AC_LIBTOOL_DLOPEN
AC_LIBLTDL_CONVENIENCE
dnl yes we will use libtool to create cygwin shared modules
AC_LIBTOOL_WIN32_DLL
AM_PROG_LIBTOOL   
AC_SUBST(INCLTDL)
AC_SUBST(LIBLTDL)

######
#
# OS dependant stuff
#
######

dnl Shamelessly stolen from editline configure.in to create the relevant cygwin stuff
dnl Platform-specific settings.  
case "${host}" in
  *-*-darwin*)
        AC_DEFINE([__Darwin__],,[Define if Darwin])
        AC_SUBST(__Darwin__)
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
	AC_DEFINE([__CYGWIN__],[1],[make sure __CYGWIN__ is defined ...])
        ;;
  *-*-netbsd*)
        ;;
  *-*-solaris2*)
        AC_DEFINE([SOLARIS],[1],[needed for optional declarations to be visible])
        ;;
  *)
        AC_MSG_RESULT(Unsupported operating system: ${host})
        ;;
esac  

######
#
# Build paths and variables
#
######     


dnl Create the relevant Build variables

if test -n $BUILD_OS ; then
	AC_DEFINE_UNQUOTED([BUILD_DATE],"`date -u "+%Y-%m-%d %H:%M:%S"` UTC",[The date of this build])
	AC_DEFINE_UNQUOTED([BUILD_OS],"`${UNAME} -s`",[Operating System we are building on])
	AC_DEFINE_UNQUOTED([BUILD_MACHINE],"`${UNAME} -m`",[Machine we are building on])
	AC_DEFINE_UNQUOTED([BUILD_HOSTNAME],"`${UNAME} -n`",[Hostname of our Box])
	AC_DEFINE_UNQUOTED([BUILD_KERNEL],"`${UNAME} -r`",[Kernel version of this build])
	AC_SUBST([BUILD_DATE])
	AC_SUBST([BUILD_OS])
	AC_SUBST([BUILD_MACHINE])
	AC_SUBST([BUILD_HOSTNAME])
	AC_SUBST([BUILD_KERNEL])
fi

dnl Installation directories
dnl Prefer modern Filesystem Hierarchy Standard over antiquated GNU standards.

AC_ARG_WITH([directory-layout],     
	[AC_HELP_STRING([--with-directory-layout],[Choose installation directory layout: singledir, lsb, fhs (default=singledir).])],,[with_directory_layout="singledir"])

case "${with_directory_layout}" in
     singledir)
	AC_MSG_NOTICE([You have chosen the SINGLEDIR directory layout...])
	ac_default_prefix='/opt/openpbx.org'
	;;
     lsb)
	AC_MSG_NOTICE([You have chosen the LSB directory layout...])
	ac_default_prefix='/usr/local'
        ;;
     fhs)
	AC_MSG_NOTICE([You have chosen the FHS directory layout...])
	ac_default_prefix='/opt/openpbx.org'
        ;;
     *)
	AC_MSG_ERROR([Unknown directory layout ${with_directory_layout}])
esac

if test -z "${opbxconfdir}"; then
   case "${with_directory_layout}" in
	singledir)
	   opbxconfdir='${prefix}/etc'
	   ;;
	lsb)
	   opbxconfdir='${sysconfdir}/openpbx.org'
	   ;;
        fhs)
           opbxconfdir='/etc${prefix}'
   esac
fi
AC_SUBST([opbxconfdir])

if test -z "${opbxconffile}"; then
	opbxconffile='${opbxconfdir}/openpbx.conf'
fi
AC_SUBST([opbxconffile])

if test -z "${opbxlibdir}"; then
   case "${with_directory_layout}" in
	singledir)
	   opbxlibdir='${libdir}'
	   ;;
	lsb)
	   opbxlibdir='${libdir}/openpbx.org'
	   ;;
        fhs)
           opbxlibdir='${libdir}'
           ;;
   esac
fi
AC_SUBST([opbxlibdir])

if test -z "${opbxmoddir}"; then
	opbxmoddir='${opbxlibdir}/modules'
fi
AC_SUBST([opbxmoddir])

if test -z "${opbxvardir}"; then
   case "${with_directory_layout}" in
	singledir)
	   opbxvardir='${prefix}/var'
	   ;;
	lsb)
	   opbxvardir='${localstatedir}/lib/openpbx.org'
	   ;;
        fhs)
           opbxvardir='/var${prefix}'
           ;;
   esac
fi
AC_SUBST([opbxvardir])

if test -z "${opbxdbdir}"; then
	opbxdbdir='${opbxvardir}'
fi
AC_SUBST([opbxdbdir])

if test -z "${opbxdbfile}"; then
	opbxdbfile='${opbxdbdir}/opbxdb'
fi
AC_SUBST([opbxdbfile])

if test -z "${opbxtmpdir}"; then
	opbxtmpdir='${opbxvardir}/tmp'
fi
AC_SUBST([opbxtmpdir])

if test -z "${opbxrundir}"; then
   case "${with_directory_layout}" in
	singledir)
	   opbxrundir='${prefix}/run'
	   ;;
	lsb)
	   opbxrundir='${localstatedir}/run/openpbx.org'
	   ;;
        fhs)
           opbxrundir='${opbxvardir}/run'
           ;;
   esac
fi
AC_SUBST([opbxrundir])

if test -z "${opbxpidfile}"; then
	opbxpidfile='${opbxrundir}/openpbx.pid'
fi
AC_SUBST([opbxpidfile])

if test -z "${opbxsocketfile}"; then
	opbxsocketfile='${opbxrundir}/openpbx.ctl'
fi
AC_SUBST([opbxsocketfile])

if test -z "${opbxlogdir}"; then
   case "${with_directory_layout}" in
	singledir)
	   opbxlogdir='${prefix}/log'
	   ;;
	lsb)
	   opbxlogdir='${localstatedir}/log/openpbx.org'
	   ;;
        fhs)
           opbxlogdir='${opbxvardir}/log'
           ;;
   esac
fi
AC_SUBST([opbxlogdir])

if test -z "${opbxspooldir}"; then
   case "${with_directory_layout}" in
	singledir)
	   opbxspooldir='${opbxvardir}/spool'
	   ;;
	lsb)
	   opbxspooldir='${localstatedir}/spool/openpbx.org'
	   ;;
        fhs)
           opbxspooldir='${opbxvardir}/spool'
           ;;
   esac
fi
AC_SUBST([opbxspooldir])

if test -z "${opbxdatadir}"; then
   case "${with_directory_layout}" in
	singledir)
	   opbxdatadir='${opbxvardir}/data'
	   ;;
	lsb)
	   opbxdatadir='${datadir}/openpbx.org'
	   ;;
        fhs)
           opbxdatadir='${prefix}/share'
           ;;
   esac
fi
AC_SUBST([opbxdatadir])

if test -z "${opbxdocdir}"; then
	opbxdocdir='${opbxdatadir}/doc'
fi
AC_SUBST([opbxdocdir])

if test -z "${opbxkeydir}"; then
	opbxkeydir='${opbxdatadir}/keys'
fi
AC_SUBST([opbxkeydir])

if test -z "${opbxsqlitedir}"; then
	opbxsqlitedir='${opbxdbdir}'
fi
AC_SUBST([opbxsqlitedir])

if test -z "${opbxagidir}"; then
	opbxagidir='${opbxdatadir}/agi'
fi
AC_SUBST([opbxagidir])

if test -z "${opbxsoundsdir}"; then
	opbxsoundsdir='${opbxdatadir}/sounds'
fi
AC_SUBST([opbxsoundsdir])

if test -z "${opbximagesdir}"; then
	opbximagesdir='${opbxdatadir}/images'
fi
AC_SUBST([opbximagesdir])

if test -z "${opbxmohdir}"; then
	opbxmohdir='${opbxdatadir}/moh'
fi
AC_SUBST([opbxmohdir])

######
#
# Check for headers, structures, typedefs, libs etc that are not realted to specific add on packages
#
######

dnl check for required libraries (also for addons)
dnl needed to move check for sqlite threadsafe here since it needs to check if we have a threadsafe sqlite3 BEFORE we check for libpthread)
#AC_CHECK_SQLITE3_THREADSAFE
AC_CHECK_LIB([asound], [main])
AC_CHECK_LIB([c], [main])
AC_CHECK_LIB([db], [main])
dnl # AC_CHECK_LIB([expat], [main]) # why would we need this one ?
AC_CHECK_LIB([ib], [main])
AC_CHECK_LIB([m], [main])
AC_CHECK_LIB([ncurses], [main],,[AC_CHECK_LIB([curses],[main])])
AC_CHECK_LIB([newt], [main],[AC_DEFINE([HAVE_LIBNEWT],["yes"],[yes there is newt])])
AM_CONDITIONAL([USE_NEWT],[test -n "${HAVE_LIBNEWT}"])
AC_CHECK_LIB([nsl], [main])
AC_CHECK_LIB([odbc], [main],[AC_DEFINE([HAVE_LIBODBC],["yes"],[say hi to libodbc])])
AC_CHECK_LIB([ossaudio], [main])
AC_CHECK_LIB([pri], [main],[AC_DEFINE([HAVE_LIBPRI],["yes"],[say hi to libpri])])
AC_CHECK_LIB([pthread], [main])
AC_CHECK_LIB([resolv], [main])
AC_CHECK_LIB([rt], [main])
AC_CHECK_LIB([socket], [main])
AC_CHECK_LIB([speex], [main],[AC_DEFINE([HAVE_LIBSPEEX],["yes"],[yay we can do speex])])
AM_CONDITIONAL([HAS_SPEEX],[test -n "${HAVE_LIBSPEEX}"])
AC_CHECK_LIB([t], [main])
# AC_CHECK_LIB([termcap], [main])
AC_CHECK_LIB([tonezone], [main],[AC_DEFINE([HAVE_LIBTONEZONE],["yes"],[OK, zaptel is there])])
AM_CONDITIONAL([HAVE_ZAPTEL],[test -n "${HAVE_LIBTONEZONE}"])

AC_CHECK_LIB([z], [main], [HAVE_LIBZ=1])

AC_ARG_WITH([pbx_dundi],
	[AC_HELP_STRING([--with-pbx_dundi],[Build and install pbx_dundi (default=yes)])],,
	[with_pbx_dundi="yes"])

AM_CONDITIONAL([WANT_PBX_DUNDI],[test "${with_pbx_dundi}" = yes -a -n "${HAVE_LIBZ}"])

dnl checks for library functions
AC_FUNC_ALLOCA
AC_CHECK_FUNC(usleep, [TARGET_CFLAGS="$TARGET_CFLAGS -DHAVE_USLEEP=1"])
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
AC_CHECK_FUNCS([atexit bzero dup2 endpwent floor ftruncate getcwd gethostbyname gethostname gettimeofday inet_ntoa isascii localtime_r memchr memmove memset mkdir munmap pow putenv re_comp regcomp rint select setenv socket sqrt strsep strcasecmp strchr strcspn strdup strerror strncasecmp strndup strrchr strspn strstr strtol strtoq unsetenv utime vasprintf])  
dnl Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_STDBOOL
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([netinet/in.h])
dnl This does not work currently .. some bug in cygwin autoconf
dnl AC_CHECK_HEADERS([w32api/windows.h])
dnl AC_CHECK_HEADERS([w32api/winsock2.h],[],[],
dnl [#if HAVE_W32API_WINDOWS_H
dnl # include <w32api/windows.h>
dnl # endif
dnl ])
dnl AC_CHECK_HEADERS([w32api/ws2tcpip.h],[],[],
dnl [#if HAVE_W32API_WINSOCK2_H
dnl # include <w32api/windows.h>
dnl # include <w32api/winsock2.h>
dnl # endif
dnl ])
AC_CHECK_HEADER([dlfcn.h],[AM_CONDITIONAL([NEED_DLFCN_H],[true = yes])])       

dnl Checks for structures
AC_STRUCT_TM
AC_STRUCT_TIMEZONE
AC_CHECK_MEMBERS([struct stat.st_blksize])  

dnl compiler specifics
AC_C_INLINE
AC_C_CONST
AC_C_VOLATILE

dnl Check for types
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T  
AC_CHECK_TYPES([ptrdiff_t])  

dnl Checks for declarations
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

AC_C_BIGENDIAN(AC_DEFINE([__BYTE_ORDER],__BIG_ENDIAN,[Big Endian]),AC_DEFINE([__BYTE_ORDER],__LITTLE_ENDIAN,[Little Endian]))
AC_DEFINE([__LITTLE_ENDIAN],1234,[for the places where it is not defined])
AC_DEFINE([__BIG_ENDIAN],4321,[for the places where it is not defined])

######
#
# Optional stuff
#
######

dnl Overwrite your config files with the examples provided
AC_ARG_ENABLE(overwrite,     
	[AC_HELP_STRING([--enable-overwrite],[overwrite your config with conf file samples])],[OVERWRITE="$enableval"],[OVERWRITE="no"])

dnl Select which Busydetect routine to use
AC_ARG_ENABLE(busydetect,
	[AC_HELP_STRING([--enable-busydetect=TYPE],[select which busydetect version to use: none (default) toneonly martin compare_tone_and_silence])],,[enable_busydetect="none"])

AC_MSG_CHECKING([for busydetect type])
if test "x$enable_busydetect" != "xnone" ; then
   	case $enable_busydetect in
		toneonly)
			BUSYDETECT=BUSYDETECT_TONEONLY
			;;
		martin)
			BUSYDETECT=BUSYDETECT_MARTIN
			;;
		tone_and_silence)
			BUSYDETECT=BUSYDETECT_TONE_AND_SILENCE
			;;
		*)	
			AC_MSG_ERROR([Unknown busydetect type "$enable_busydetect"])
			;;
	esac
fi
AC_MSG_RESULT([$enable_busydetect])

AC_SUBST([BUSYDETECT])
AC_DEFINE_UNQUOTED([BUSYDETECT],"${BUSYDETECT}",[defined way of finding out how to detect busy])

dnl Optimize for host cpu version
AC_ARG_ENABLE(optimization,     
	[AC_HELP_STRING([--disable-optimization],[no detection or tuning flags for cpu version])],
	[OPTIMIZECPU="$enableval"],[OPTIMIZECPU="yes"])

dnl Enable debugging
AC_ARG_ENABLE(debug,
	[AC_HELP_STRING([--enable-debug],[build with debug information])],,[enable_debug="no"])

AM_CONDITIONAL([DEBUG],[test "x$enable_debug" != "xno"])

dnl Enable frame tracing
AC_ARG_ENABLE(frame_tracing,
        [AC_HELP_STRING([--enable-frame_tracing],[build with frame tracer])],,[enable_frame_tracing="no"])

AM_CONDITIONAL([TRACE_FRAMES],[test "x$enable_frame_tracing" != "xno"])

dnl Enable valgrind shit
AC_ARG_ENABLE(valgrind,
        [AC_HELP_STRING([--enable-valgrind],[build with valgrind information])],,[enable_valgrind="no"])

AM_CONDITIONAL([VALGRIND],[test "x$enable_valgrind" != "xno" ])

dnl Enable profiling
AC_ARG_ENABLE(profiling,
	[AC_HELP_STRING([--enable-profiling],[build with profiling support])],,[enable_profiling="no"])

AM_CONDITIONAL([PROFILING],[test "x$enable_profiling" != "xno"])

dnl Low memory support
AC_ARG_ENABLE(low_memory,
	[AC_HELP_STRING([--enable-low_memory],[enable low_memory support])],,[enable_low_memory="no"])

AM_CONDITIONAL([LOW_MEMORY],[test "x$enable_low_memory" != "xno"])

dnl Select user and group
opbxrunuser="openpbx"
opbxrungroup="openpbx"

AC_ARG_WITH(openpbx-user,
	[AC_HELP_STRING([--with-openpbx-user=USER],[use USER instead of default (openpbx)])],[opbx_user="$withval"],[opbx_user="no"])

if test "x$opbx_user" != "xno" && test "x$opbx_user" != "xyes"; then
	opbxrunuser="$opbx_user"
fi

AC_ARG_WITH(openpbx-group,
	[AC_HELP_STRING([--with-openpbx-group=GROUP],[use GROUP instead of default (openpbx)])],[opbx_group="$withval"],[opbx_group="no"])

if test "x$opbx_group" != "xno" && test "x$opbx_group" != "xyes"; then
	opbxrungroup="$opbx_group"
fi
# set vars
AC_SUBST(opbxrunuser)
AC_SUBST(opbxrungroup)  

AC_ARG_ENABLE([srtp],
	[AC_HELP_STRING([--enable-srtp],[Enable support for SRTP.])],,[enable_srtp="no"])

if test "${enable_srtp}" = "yes"; then
   AC_CHECK_HEADER([srtp/srtp.h],,[enable_srtp="no"])
   AC_CHECK_LIB([srtp],[srtp_init],,[enable_srtp="no"])
fi

if test "${enable_srtp}" = "yes"; then
   AC_DEFINE([ENABLE_SRTP],,[Enable support for SRTP.])
fi
AM_CONDITIONAL([WANT_SRTP],[test "$enable_srtp" = "yes"])

#######
#
# now for channels,apps and ressources
#
######

dnl res_crypto and res_osp and app_osplookup will not work without ssl
CHECK_SSL

dnl visdn based channel for BRI support, ... build the ppp application for visdn as well
AC_ARG_ENABLE(chan_visdn,
	[AC_HELP_STRING([--enable-chan_visdn],[use chan_visdn, an alternative channel for HFC-PCI BRI card support, currently only supports european ISDN BRI (default=no)])],
[   AC_MSG_RESULT(yes)
    AC_CHECK_HEADERS([visdn/visdn.h],[AC_DEFINE([VISDN_HEADERS],["yes"],[visdn seems to be installed])])
    AC_CHECK_LIB([q931], [main],[AC_DEFINE([VISDN_LIBS],[-lq931],[libq931 required for visdn...found!])])

     if test -n "$VISDN_LIBS" && test -n "$VISDN_HEADERS"
     then
     	WANT_VISDN="yes"
     fi],
[   AC_MSG_RESULT(no)
])
   
AM_CONDITIONAL([WANT_VISDN],[test -n "${WANT_VISDN}"])


dnl EXOSIP2 based SIP channel
AC_ARG_WITH(chan_exosip2,
	[AC_HELP_STRING([--with-chan_exosip2],[use chan_exosip2, an alternative SIP channel driver based on exosip (default=no)])],,[with_chan_exosip2="no"])

if test "${with_chan_exosip2}" = "yes"; then
   PKG_CHECK_MODULES([GLIB2],
	[glib-2.0],
	[HAVE_GLIB2="yes"])
   AC_CHECK_LIB(ortp,[main],[HAVE_ORTP="yes"])
   AC_CHECK_LIB(osip2,[main],[HAVE_OSIP2="yes"])
   AC_CHECK_LIB(eXosip2,[main],[HAVE_EXOSIP2="yes"])

   if test -n "${HAVE_ORTP}" -a -n "${HAVE_OSIP2}" -a -n "${HAVE_EXOSIP2}"; then
      CHAN_EXOSIP2_CFLAGS=""
      CHAN_EXOSIP2_LIBS="-leXosip2 -losip2 -losipparser2 -lortp -lpthread"
      if test -n "${HAVE_GLIB2}"; then
         CHAN_EXOSIP2_CFLAGS="${CHAN_EXOSIP2_CFLAGS} ${GLIB2_CFLAGS}"
	 CHAN_EXOSIP2_LIBS="${CHAN_EXOSIP2_LIBS} ${GLIB2_LIBS}"
      fi
   fi
fi

AM_CONDITIONAL([WANT_CHAN_EXOSIP2],[test -n "${HAVE_ORTP}" -a -n "${HAVE_OSIP2}" -a -n "${HAVE_EXOSIP2}"])

AC_SUBST([CHAN_EXOSIP2_CFLAGS])
AC_SUBST([CHAN_EXOSIP2_LIBS])

####### apps

dnl app_icd
#app_icd = "no"
#AC_ARG_WITH(app_icd,
#	[ --with-app_icd=no			use app_icd, a full blown inteligent call distribution system (default=no) *** HIGHLY EXPERIMENTAL FOR THE MOMENT ****],,[with_app_icd="no"])
#
#

##### ressources

dnl res_osp ... open settlement protocol handling
dnl look for Transnexus OSP Toolkit (stolen from the OpenH323 configure.ac)

dnl MSWIN_DISPLAY    transnexus_osp,Transnexus OSP Toolkit
dnl MSWIN_CHECK_FILE transnexus_osp,include/osp/ospcryptowrap.h,H323_TRANSNEXUS_OSP=1
dnl MSWIN_DIR_SYMBOL transnexus_osp,H323_TRANSNEXUS_OSP_DIR
dnl MSWIN_CHECK_DIR  transnexus_osp,lib\
dnl MSWIN_CHECK_DIR  transnexus_osp,pthread\lib
dnl MSWIN_CHECK_DIR  transnexus_osp,pthread\include

AC_ARG_ENABLE(transnexusosp,
       [AC_HELP_STRING([--disable-transnexusosp],[disable support for the Transnexus OSP toolkit])])

if test "${enable_transnexusosp}z" = "z" ; then
  enable_transnexusosp="yes"
fi

if test "x$enable_transnexusosp" = "xno" ; then
  AC_MSG_NOTICE(Disabling support for the Transnexus OSP toolkit)
else
  AC_CHECK_HEADERS(osp/osp.h, HAS_TRANSNEXUS_OSP=1)
  if test "${HAS_TRANSNEXUS_OSP}x" = "1x" ; then
    AC_CHECK_LIB(osp, OSPPInit, HAS_TRANSNEXUS_OSP=1, HAS_TRANSNEXUS_OSP=, -lssl)
    if test "${HAS_TRANSNEXUS_OSP}x" = "1x" ; then
      TRANSNEXUS_OSP=1
      AC_DEFINE([TRANSNEXUS_OSP],"1",[yes we have osp ...])
      OSP_LIBS="-losp"
      OSP_INCLUDE="#include <osp/osp.h>"
    else
      unset ac_cv_lib_osp_OSPPInit
      AC_CHECK_LIB(osp, OSPPInit, HAS_TRANSNEXUS_OSP=1, HAS_TRANSNEXUS_OSP=, -lssl)
      if test "${HAS_TRANSNEXUS_OSP}x" = "x" ; then
         unset TRANSNEXUS_OSP
      else
        TRANSNEXUS_OSP=1
        AC_DEFINE([TRANSNEXUS_OSP],"1",[yes we have OSP])
        OSP_CFLAGS="-I/usr/local/include"
        OSP_LDLIBS="${LDLIBS} -losp"
        OSP_INCLUDE='#include "/usr/local/include/osp/osp.h"'
      fi
    fi
  fi
fi

AC_SUBST(TRANSNEXUS_OSP)
AC_SUBST(OSP_LIBS)
AC_SUBST(OSP_INCLUDE)
AC_SUBST(OSP_CFLAGS)
AM_CONDITIONAL([WANT_OSP],[test -n "${TRANSNEXUS_OSP}"])

#res_jabber

dnl Selector for jabber support
AC_ARG_ENABLE(jabber,
	[AC_HELP_STRING([--enable-jabber],[Enable jabber support.])],,
	[enable_jabber_support="no"])

dnl Loudmouth/Jabber library detection :)
if test "${enable_jabber_support}" = "yes"; then
   PKG_CHECK_MODULES([LOUDMOUTH],
	[loudmouth-1.0 >= 1.0.0],
	[HAVE_LOUDMOUTH=1],
	[AC_MSG_WARN([loudmouth library not found ... jabber related features (res_jabber, app_icd) will not be built])])
fi

AC_ARG_WITH([res_jabber],
	[AC_HELP_STRING([--with-res_jabber],[Build and install res_jabber (default=no)])],,
	[with_res_jabber="no"])

AM_CONDITIONAL([WANT_RES_JABBER],[test "${enable_jabber}" = "yes" -a "${with_res_jabber}" = "yes" -a -n "${HAVE_LOUDMOUTH}"])

# res_sqlite
dnl Sqlite ressource module
SQLITE3_THREADSAFE_LIBS='${top_srcdir}/sqlite3-embedded/libopbxsqlite3.la -lpthread'
SQLITE3_THREADSAFE_CFLAGS='-DTEMP_STORE=2 -DTHREADSAFE=1 -DSQLITE_OMIT_CURSOR -DNO_TCL -I${top_srcdir}/sqlite3-embedded'
case "${host}" in
  *cygwin*)
	SQLITE3_THREADSAFE_CFLAGS="${SQLITE3_THREADSAFE_CFLAGS} -DOS_WIN"
        ;;
  *)
	SQLITE3_THREADSAFE_CFLAGS="${SQLITE3_THREADSAFE_CFLAGS} -DOS_UNIX"
        ;;
esac  
AC_SUBST([SQLITE3_THREADSAFE_LIBS])
AC_SUBST([SQLITE3_THREADSAFE_CFLAGS])
 

dnl Selector for res_sqlite support
AC_ARG_WITH(res_sqlite,
        [AC_HELP_STRING([--with-res_sqlite],[Build and install res_sqlite (default=no)])],,[with_res_sqlite="no"])

AM_CONDITIONAL([WANT_RES_SQLITE],[test -n "${want_res_sqlite}"])

dnl postgresql -- first need to check if it is there and than can check for if someone wants res_config_pgsql and cdr_pgsql
CT_CHECK_POSTGRES_DB

AC_ARG_WITH(res_config_pgsql,
	[AC_HELP_STRING([--with-res_config_pgsql],[Build and install res_config_pgsql (default = no)])],,
	[with_res_config_pgsql="no"])

AM_CONDITIONAL([WANT_RES_CONFIG_PGSQL],[test "$with_res_config_pgsql" = "yes"])

AC_ARG_WITH(cdr_pgsql,
	[AC_HELP_STRING([--with-cdr_pgsql],[Build and install cdr_pgsql (default = no)])],,
	[with_cdr_pgsql="no"])

AM_CONDITIONAL([WANT_CDR_PGSQL],[test "$with_cdr_pgsql" = "yes"])

#res_odbc
AC_ARG_ENABLE(res_odbc,
	[AC_HELP_STRING([--enable-res_odbc],[build res_odbc (default=no)])],
[   AC_MSG_RESULT(yes)
    if test -n "${HAVE_LIBODBC}"
    then
	WANT_RES_ODBC=yes
    fi],
[   AC_MSG_RESULT(no)
])

AM_CONDITIONAL([WANT_RES_ODBC],[test -n "${WANT_RES_ODBC}"])

  

dnl Selector for res_odbc support
AC_ARG_ENABLE(res_config_odbc,
        [AC_HELP_STRING([--enable-res_config_odbc],[build res_config_odbc (default=no)])],
[   AC_MSG_RESULT(yes)
    if test -n  "${HAVE_LIBODBC}"
    then
        WANT_RES_CONFIG_ODBC=yes
    fi],
[   AC_MSG_RESULT(no)
])

AM_CONDITIONAL([WANT_RES_CONFIG_ODBC],[test -n "${WANT_RES_CONFIG_ODBC}"])

#
# Optional Packages (odbc,mysql,loudmouth for jabber,pgsql,dbi and system sqlite. Also system speex codec
# and system ilbc codec library
#
######
#
# MySQL
#
AC_DEFUN([MYSQL_INC_CHK],
	[if test -r $i$1/mysql.h; then MYSQL_INCDIR=$i$1; break; fi])
AC_DEFUN([MYSQL_LIB_CHK],
	[if test -r $i$1/libmysqlclient.a -o -r $i$1/libmysqlclient.so; then MYSQL_LIBDIR=$i$1; break; fi])

AC_MSG_CHECKING(for MySQL)
AC_ARG_WITH(mysql,
	[AC_HELP_STRING([--with-mysql=DIR],[Normally, configure will scan all possible default installation paths for mysql. When it fails use this command to tell configure where MySQL installation root directory is.])],,[with_mysql="no"])
AC_ARG_WITH(mysql-includes,
	[AC_HELP_STRING([--with-mysql-includes=DIR],[Location of MySQL includes])],,[with_mysql_includes=""])

AC_ARG_WITH(mysql-libs,
	[AC_HELP_STRING([--with-mysql-libs=DIR],[Location of MySQL libraries],,[with_mysql_libs=""])])

locations="/usr /usr/local /opt /usr/sfw"

if test "x${with_mysql}" != "xno" ; then

	if test "x${with_mysql}" != "xyes" ; then
		locations="$with_mysql $locations"
	fi

	lib_locations="$locations"
	include_locations="$locations"

	if test -n "x${with_mysql_includes}" && \
	   test "x${with_mysql_includes}" != "xno" && \
	   test "x${with_mysql_includes}" != "xyes"
	then
		include_locations="$with_mysql_includes $locations"
	fi

	if test -n "x${with_mysql_libs}" && \
	   test "x${with_mysql_libs}" != "xno" && \
	   test "x${with_mysql_libs}" != "xyes"
	then
		lib_locations="$with_mysql_libs $locations"
	fi

        # Autodetect
        for i in $include_locations; do
		MYSQL_INC_CHK(/mysql)
		MYSQL_INC_CHK(/mysql/include)
		MYSQL_INC_CHK(/include)
		MYSQL_INC_CHK(/include/mysql)
        done

        for i in $lib_locations; do
		MYSQL_LIB_CHK(/mysql)
		MYSQL_LIB_CHK(/mysql/lib)
		MYSQL_LIB_CHK(/lib)
		MYSQL_LIB_CHK(/lib/mysql)
        done

	# result
	if test -z "${MYSQL_LIBDIR}" ; then
		AC_MSG_ERROR([MySQL libs not found])
	elif test -z "${MYSQL_INCDIR}" ; then
		AC_MSG_ERROR([MySQL includes not found])
	fi

	AC_MSG_RESULT([yes])
	HAVE_MYSQL="yes"

	# special check, adding -I/usr/include or -I/usr/local/include
	# is considered evil and may break things
	if test "x${MYSQL_INCDIR}" != "x/usr/include" && \
	   test "x${MYSQL_INCDIR}" != "x/usr/local/include"
	then
		MYSQL_INCLUDE="-I$MYSQL_INCDIR"
	fi

	MYSQL_LIBS="-L$MYSQL_LIBDIR -lmysqlclient"
	MYSQL_DEFINES="-DHAVE_MYSQL"

	# include zlib if available 
	# (hmm does mysql need it? no idea how to check that...)
	if test "x${HAVE_LIBZ}" = "xyes" ; then
		MYSQL_LIBS="$MYSQL_LIBS -lz"
	fi

	AC_DEFINE(HAVE_MYSQL,,[MySQL support])
	AC_SUBST(MYSQL_INCLUDE)
	AC_SUBST(MYSQL_LIBS)
	AC_SUBST(MYSQL_DEFINES)
	AC_SUBST(HAVE_MYSQL)

	unset lib_locations include_locations
else
	AC_MSG_RESULT(skipped)
	HAVE_MYSQL="no"
fi

AC_CHECK_LIB([unicall],[main],[AC_DEFINE([HAVE_LIBUNICALL],["yes"],[yes we have unicall])])

AC_ARG_ENABLE(chan_unicall,
	[AC_HELP_STRING([--enable-chan_unicall],[Build and install chan_unicall.])],
[ AC_MSG_RESULT(yes)
  if test -n "${HAVE_LIBUNICALL}"
  then
	WANT_CHAN_UNICALL=yes
  fi],
[ AC_MSG_RESULT(no)
])

AM_CONDITIONAL([WANT_CHAN_UNICALL],[test -n "${WANT_CHAN_UNICALL}"])

# res_js


AC_ARG_ENABLE(javascript,
        [AC_HELP_STRING([--enable-javascript],[Enable the use of javascript (default=no)])],,
	[enable_javascript="no"])

if test "${enable_javascript}" = "yes"; then
   AC_CHECK_LIB([js],[main],[HAVE_LIBJS=1])

   if test -n "$HAVE_LIBJS"; then
      JS_CFLAGS="-DXP_UNIX -DSVR4 -DSYSV -D_BSD_SOURCE -DPOSIX_SOURCE -DHAVE_LOCALTIME_R -DX86_LINUX  -DDEBUG -DDEBUG_root -DJS_THREADSAFE -D_GNU_SOURCE"
      JS_LDFLAGS="-ljs"
      AC_SUBST(JS_CFLAGS)
      AC_SUBST(JS_LDFLAGS)
   fi

   AC_PATH_PROG([NSPRCONFIG], [nspr-config])

   if test -n "${NSPRCONFIG}"; then
      HAVE_NSPR=1
      NSPR_CFLAGS=`${NSPRCONFIG} --cflags`
      NSPR_LDFLAGS=`${NSPRCONFIG} --libs`
      AC_SUBST(NSPR_CFLAGS)
      AC_SUBST(NSPR_LDFLAGS)
   else
      PKG_CHECK_MODULES([NSPR],
	[mozilla-nspr],
	[HAVE_NSPR=1])
      AC_SUBST(NSPR_CFLAGS)
      AC_SUBST(NSPR_LDFLAGS)
   fi
fi

AM_CONDITIONAL([WANT_RES_JS],[test "${enable_javascript}" = "yes" -a -n "${HAVE_LIBJS}" -a -n "${HAVE_NSPR}"])  

AC_ARG_ENABLE([chan_capi],
	[AC_HELP_STRING([--enable-chan_capi],[Build capi channel module])],,[enable_chan_capi="no"])

WANT_CHAN_CAPI="no"
if test "x${enable_chan_capi}" != "xno" ; then

	AC_CHECK_HEADERS([capi20.h],
		[AC_DEFINE([HAVE_CAPI20_H]) HAVE_CAPI20_H="yes"])

	AC_CHECK_HEADERS([capiutils.h],
		[AC_DEFINE([HAVE_CAPIUTILS_H]) HAVE_CAPIUTILS_H="yes"])

	if test -n "${HAVE_CAPI20_H}" -a -n "${HAVE_CAPIUTILS_H}"
	then
		WANT_CHAN_CAPI="yes"
	fi
fi

AM_CONDITIONAL([HAVE_CAPI], [test "${WANT_CHAN_CAPI}" = "yes"])
 
 #
 # udpfromto.c checks
 #
 AC_ARG_ENABLE(udpfromto,
       [AC_HELP_STRING([--enable-udpfromto],[Enable extended UDP support for multi-homed systems])],
       [enable_udpfromto="${enableval}"],
       [enable_udpfromto="no"])
 
 if test "x$enable_udpfromto" != "xno" ; then
       #
       # IP_PKTINFO (Linux)
       #
       AC_CHECK_DECL([IP_PKTINFO],[have_IP_PKTINFO="yes"],[have_IP_PKTINFO="no"],
           [#ifdef HAVE_NETINET_IN_H
               #include <netinet/in.h>
            #endif
           ])
       if test "x$have_IP_PKTINFO" = "xyes" ; then
               AC_DEFINE([HAVE_IP_PKTINFO],,[Needed for recv-/sendfromto on Linux])
       fi
 
       #
       # IP_RECVDSTADDR (*BSD)
       #
       AC_CHECK_DECL([IP_RECVDSTADDR],[have_IP_RECVDSTADDR="yes"],[have_IP_RECVDSTADDR="no"],
           [#ifdef HAVE_NETINET_IN_H
               #include <netinet/in.h>
            #endif
           ])
       #
       # IP_SENDSRCADDR (*BSD)
       #
       AC_CHECK_DECL([IP_SENDSRCADDR],[have_IP_SENDSRCADDR="yes"],[have_IP_SENDSRCADDR="no"],
           [#ifdef HAVE_NETINET_IN_H
               #include <netinet/in.h>
            #endif
           ])
       if test "x$have_IP_RECVDSTADDR" = "xyes" && \
          test "x$have_IP_SENDSRCADDR" = "xyes" ; then
               AC_DEFINE([HAVE_IP_RECVDSTADDR],,[Needed for recv-/sendfromto on *BSD])
               AC_DEFINE([HAVE_IP_SENDSRCADDR],,[Needed for recv-/sendfromto on *BSD])
       fi
 fi     

AC_ARG_WITH(res_noise,
	[AC_HELP_STRING([--with-res_noise],[Build res_noise.])],,[with_res_noise="yes"])

AM_CONDITIONAL([WANT_RES_NOISE],[test "${with_res_noise}" = "yes"])
             
AC_SUBST(SVN)
AC_SUBST(GREP)
AC_SUBST(GROUPADD)
AC_SUBST(SOLINK)
AC_SUBST(ASTLINK)
AC_SUBST(CPU_OPTIONS)
AC_SUBST(all_debug)
AC_SUBST(DEBUG_THREADS)
AC_SUBST(MALLOC_DEBUG)
AC_SUBST(OVERWRITE)
AC_SUBST(LOW_MEMORY)
AC_SUBST(prefix)

AC_SUBST_FILE(substitute)
substitute=substitute.mak

AM_CONFIG_HEADER(include/confdefs.h)  

######
#
# Define in which subdirectories we have to run configure as well
#
######
AC_CONFIG_SUBDIRS(editline libltdl)    
AC_OUTPUT([Makefile
		 libs/Makefile
                 libs/gsm/Makefile
                 libs/ilbc/Makefile
                 libs/lpc10/Makefile
                 agi/Makefile
                 apps/Makefile
                 cdr/Makefile
                 channels/Makefile
                 codecs/Makefile
 		 corelib/Makefile
                 formats/Makefile
                 funcs/Makefile
                 pbx/Makefile
                 res/Makefile
                 stdtime/Makefile
                 utils/Makefile
		 configs/musiconhold.conf.sample
		 configs/Makefile
 		 agi/agi-test.agi
		 agi/fastagi-test
		 sounds/Makefile
		 sqlite3-embedded/Makefile
		 sqlite3-embedded/mkopcodec.awk
		 sqlite3-embedded/mkopcodeh.awk
])



echo ""
echo "   ************************"
echo "   *   Configure status   *"
echo "   ************************"
echo ""

echo "MySQL Support:       ${HAVE_MYSQL}"

if test "x${HAVE_MYSQL}" = "xyes" ; then
	echo "MySQL includes:      ${MYSQL_INCDIR}"
	echo "MySQL libs:          ${MYSQL_LIBDIR}"
fi

echo "Loudmouth/Jabber Support:  ${HAVE_LIBLOUDMOUTH}"
if test "x${HAVE_LIBLOUDMOUTH}" = "xyes" ; then
        echo "Loudmouth CFLAGS: ${LOUDMOUTH_CFLAGS}"
        echo "Loudmouth LIBS:     ${LOUDMOUTH_LIBS}"
fi     

echo "CAPI Channel module: ${WANT_CHAN_CAPI}"

echo ""

echo "CFLAGS: 		$CFLAGS"
echo "LIBS: 		$LIBS"
echo "LDFLAGS: 		$LDFLAGS"
echo "BUSYDETECT: 	${BUSYDETECT}"
echo "CPU_OPTIONS: 	${CPU_OPTIONS}"
echo "BUILD_OS: 	${BUILD_OS}"
echo "BUILD_MACHINE: 	${BUILD_MACHINE}"
echo "BUILD_HOST: 	${BUILD_HOST}"
echo "SVN:		${SVN}"
echo "SVN_VERSION:	${SVN_VERSION}"
echo ""
echo "prefix:           ${prefix}"
echo "exec_prefix:      ${exec_prefix}"
echo ""
echo "bindir:           ${bindir}"
echo "sbindir:          ${sbindir}"
echo "libdir:           ${libdir}"
echo "sysconfdir:       ${sysconfdir}"
echo "localstatedir:    ${localstatedir}"
echo "datadir:          ${datadir}"
echo ""
echo "opbxconfdir:      ${opbxconfdir}"
echo "opbxconffile:     ${opbxconffile}"
echo "opbxlibdir:       ${opbxlibdir}"
echo "opbxmoddir:       ${opbxmoddir}"
echo "opbxvardir:       ${opbxvardir}"
echo "opbxdbdir:        ${opbxdbdir}"
echo "opbxdbfile:       ${opbxdbfile}"
echo "opbxtmpdir:       ${opbxtmpdir}"
echo "opbxrundir:       ${opbxrundir}"
echo "opbxpidfile:      ${opbxpidfile}"
echo "opbxsocketfile:   ${opbxsocketfile}"
echo "opbxlogdir:       ${opbxlogdir}"
echo "opbxspooldir:     ${opbxspooldir}"
echo "opbxdatadir:      ${opbxdatadir}"
echo "opbxdocdir:       ${opbxdocdir}"
echo "opbxkeydir:       ${opbxkeydir}"
echo "opbxsqlitedir:    ${opbxsqlitedir}"
echo "opbxagidir:       ${opbxagidir}"
echo "opbxsoundsdir:    ${opbxsoundsdir}"
echo "opbximagesdir:    ${opbximagesdir}"
echo "opbxmohdir:       ${opbxmohdir}"
