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
		echo "$PROGRAM is required and was not found."
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
version_compare libtoolize 1 5 20 || exit 1
version_compare automake 1 9 6 || exit 1
version_compare autoconf 2 59 || exit 1

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
