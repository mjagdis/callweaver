#!/bin/bash 

# Yes, I realise this is only for developers, but this should be 
# documented and warned

# ***************   NOTICE  ****************
# FreeBSD is buggy. Please use this 
# workaround if you  want to bootstrap 
# on FreeBSD.
#
# cd /usr/local/share/aclocal19
# ln -s ../aclocal/libtool15.m4 .
# ln -s ../aclocal/ltdl15.m4 .
#
#*******************************************

# Check for required version and die if unhappy
libtoolize --version | grep 1.5.20 > /dev/null || 
  (
	echo You have the wrong, or missing version of libtool.  
	echo The required  version is 1.5.20 and is available from:
	echo http://www.gnu.org/software/libtool/
  ) 

automake --version | grep 1.9.6 > /dev/null ||
  (
	echo You have the wrong, or missing version of automake.  
	echo The required version is 1.9.6 and is available from:
	echo http://www.gnu.org/software/automake/
  )

autoconf --version | grep 2.59 > /dev/null ||
  (
	echo You have the wrong, or missing version of autoconf. 
	echo The required version is 2.59 and is available from:
	echo http://www.gnu.org/software/autoconf/
  )

libtoolize --copy --force --ltdl
aclocal -I acmacros
autoheader --force
automake --copy --add-missing
autoconf --force
pushd libltdl
aclocal
automake --copy --add-missing
autoheader --force
autoconf --force
popd
#pushd editline
#libtoolize --copy --force
#aclocal
#autoheader --force
#automake --copy --add-missing
#autoconf --force
#popd
#pushd sqlite3-embedded
#libtoolize --copy --force
#aclocal
#autoconf --force
#popd

chmod ug+x debian/rules
