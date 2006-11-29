#!/usr/bin/env sh

UNAME=`uname`

if [ "x$UNAME" = "xFreeBSD" ]; then
    echo ""
    echo ""
    echo "******************************************"
    echo "***              NOTICE                ***"
    echo "******************************************"
    echo "                                          "
    echo "FreeBSD is buggy. Please use this         "
    echo "workaround if you  want to bootstrap      "
    echo "on FreeBSD.                               "
    echo "                                          "
    echo "cd /usr/local/share/aclocal19             "
    echo "ln -s ../aclocal/libtool15.m4 .           "
    echo "ln -s ../aclocal/ltdl15.m4 .              "
    echo "				      "
    echo "******************************************"
    echo ""
fi

debug ()
{
	# Outputs debug statments if DEBUG var is set
	if [ ! -z "$DEBUG" ]; then
		echo "DEBUG: $1"
	fi
}
version_compare()
{
	# Checks a command is found and the version is high enough
	PROGRAM=$1
	MAJOR=$2
	MINOR=$3
	MICRO=$4
	test -z "$MAJOR" && MAJOR=0
	test -z "$MINOR" && MINOR=0
	test -z "$MICRO" && MICRO=0

	debug "Checking $PROGRAM >= $MAJOR.$MINOR.$MICRO"

	WHICH_PATH=`whereis which | cut -f2 -d' '`
	COMMAND=`$WHICH_PATH $PROGRAM`
	if [ -z $COMMAND ]; then
		echo "$PROGRAM-$MAJOR.$MINOR.$MICRO is required and was not found."
		return 1
	else
		debug "Found $COMMAND"
	fi

	INS_VER=`$COMMAND --version | head -1 | sed 's/[^0-9]*//' | cut -d' ' -f1`
	INS_MAJOR=`echo $INS_VER | cut -d. -f1 | sed s/[a-zA-Z\-].*//g`
	INS_MINOR=`echo $INS_VER | cut -d. -f2 | sed s/[a-zA-Z\-].*//g`
	INS_MICRO=`echo $INS_VER | cut -d. -f3 | sed s/[a-zA-Z\-].*//g`
	test -z "$INS_MAJOR" && INS_MAJOR=0
	test -z "$INS_MINOR" && INS_MINOR=0
	test -z "$INS_MICRO" && INS_MICRO=0
	debug "Installed version: $INS_VER"

	if [ "$INS_MAJOR" -gt "$MAJOR" ]; then
		debug "MAJOR: $INS_MAJOR > $MAJOR"
		return 0
	elif [ "$INS_MAJOR" -eq "$MAJOR" ]; then
		debug "MAJOR: $INS_MAJOR = $MAJOR"
		if [ "$INS_MINOR" -gt "$MINOR" ]; then
			debug "MINOR: $INS_MINOR > $MINOR"
			return 0
		elif [ "$INS_MINOR" -eq "$MINOR" ]; then
			if [ "$INS_MICRO" -ge "$MICRO" ]; then
				debug "MICRO: $INS_MICRO >= $MICRO"
				return 0
			else
				debug "MICRO: $INS_MICRO < $MICRO"
			fi
		else
			debug "MINOR: $INS_MINOR < $MINOR"
		fi
	else
		debug "MAJOR: $INS_MAJOR < $MAJOR"
	fi

	echo "You have the wrong version of $PROGRAM. The minimum required version is $MAJOR.$MINOR.$MICRO"
	echo "    and the version installed is $INS_MAJOR.$INS_MINOR.$INS_MICRO ($COMMAND)."
	return 1
}

# Check for required version and die if unhappy

if [ "x$UNAME" = "xFreeBSD" ]; then
version_compare libtoolize 1 5 20 || exit 1
version_compare automake19 1 9 6 || exit 1
version_compare autoconf259 2 59 || exit 1
ACLOCAL=aclocal19
AUTOHEADER=autoheader259
AUTOMAKE=automake19
AUTOCONF=autoconf259
else
version_compare libtoolize 1 5 20 || exit 1
version_compare automake 1 9 6 || exit 1
version_compare autoconf 2 59 || exit 1
ACLOCAL=aclocal
AUTOHEADER=autoheader
AUTOMAKE=automake
AUTOCONF=autoconf
fi

libtoolize --copy --force --ltdl
#NetBSD seems to need this file writable
chmod u+w libltdl/configure

$ACLOCAL -I acmacros
$AUTOHEADER --force
$AUTOMAKE --copy --add-missing
$AUTOCONF --force

cd libltdl
$ACLOCAL
$AUTOMAKE --copy --add-missing
$AUTOHEADER --force
$AUTOCONF --force
cd ..

#pushd sqlite3-embedded
#libtoolize --copy --force
#$ACLOCAL
#$AUTOCONF --force
#popd

chmod ug+x debian/rules



if [ "x$UNAME" = "xNetBSD" ]; then
echo ""
echo "Please remember to run gmake instead of make on NetBSD"
echo ""
fi
