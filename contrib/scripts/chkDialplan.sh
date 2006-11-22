#!/bin/sh
#
#	Shell extensions.conf validation/modification script
#
#	chkDialplan.sh
#
#	Author: Santiago Baranda (sbaranda_at_gmail_dot_com)
#

DEF_APPS="applications.txt"
DEF_EXTENS="extensions.conf"
INC_LIST=$DEF_EXTENS

#
# Build include file list and disable duplicates
#
build_include_list() {
	local inc_file=$1
	local inc_validation=`grep "#" $inc_file`
	if ! [ -z "$inc_validation" ]; then
		for _inc_file in `grep "#" $inc_file | cut -f2 -d'#' | cut -f2 -d' '`
		do
			inc_dupl=`echo $INC_LIST | grep $_inc_file`
			if [ -z "$inc_dupl" ]; then
				INC_LIST=`echo $INC_LIST $_inc_file`
			fi
			if [ -r $_inc_file ]; then
				inc_validation=`grep "#" $_inc_file`
				if ! [ -z "$inc_validation" ]; then
					build_include_list $_inc_file
				fi
			fi
		done
	fi
}

#
# Check default files
#
check_def_files() {
	# Check $DEF_APPS
	if ! [ -f $DEF_APPS ]; then
		echo "$DEF_APPS missing or not found"
		echo "Please check if it exists or if it is in the same directory as $0 and try again"
		exit 0
	elif ! [ -r $DEF_APPS ]; then
		echo "$DEF_APPS not readable or not found"
		echo "Please check the permissions or if it is in the same directory as $0 and try again"
		exit 0
	fi
	# Check $DEF_EXTENS
	if ! [ -f $DEF_EXTENS ]; then
		echo "$DEF_EXTENS missing or not found"
		echo "Please check if it exists or if it is in the same directory as $0 and try again"
		exit 0 
	elif ! [ -r $DEF_EXTENS ]; then
		echo "$DEF_EXTENS not readable or not found"
		echo "Please check the permissions or if it is in the same directory as $0 and try again"
		exit 0
	fi
}

#
# Modify files
#
modify_files() {
	for inc_file in $INC_LIST
	do
		cp $inc_file $inc_file.bak
		cat $inc_file.bak |
		while read ext_line;
		do
                	if ! [ -z "$ext_line" ]; then
				dir_validation=`echo $ext_line | grep "^\["`
				inc_validation=`echo $ext_line | grep "#"`
				if [ -z "$dir_validation" ] && [ -z "$inc_validation" ]; then
					ext_oval=`echo $ext_line | cut -f3 -d',' | cut -f1 -d'('`
					ext_nval=`grep -iw ^$ext_oval $DEF_APPS`
					if ! [ -z "$ext_nval" ]; then
						ext_mod=`echo $ext_line | sed 's/'$ext_oval'/'$ext_nval'/i'`
					elif ! [ -z `echo $ext_oval | grep -i macro` ]; then
						ext_mod=`echo $ext_line | sed 's/'$ext_oval'/Proc/i'`
					elif ! [ -z `echo $ext_oval | grep -i agi` ]; then
						ext_mod=`echo $ext_line | sed 's/\.agi/\.ogi/i'`
						ext_mod=`echo $ext_line | sed 's/agi/OGI/i'`
					fi
				elif ! [ -z `echo $ext_line | grep -i "^\[macro\-"` ]; then
					ext_oval=`echo $ext_line | cut -f2 -d '[' | cut -f1 -d ']' | cut -c1-5`
					ext_mod=`echo $ext_line | sed 's/'$ext_oval'/proc/i'`
				else
					ext_mod=`echo $ext_line`
				fi
			fi
			echo $ext_mod >> tmp_$inc_file
			ext_mod=""
		done
		mv tmp_$inc_file $inc_file
	done
	echo "Modification process finished"
	echo "Your original files have been renamed to .bak"
}

#
# Validate Files
#
validate_files() {
	if [ -f "validation.log" ]; then
		rm validation.log
	fi
	for inc_file in $INC_LIST
	do
		echo $inc_file >> validation.log
		cat $inc_file |
		while read ext_line;
		do
			if ! [ -z "$ext_line" ]; then
				dir_validation=`echo $ext_line | grep "^\["`
				inc_validation=`echo $ext_line | grep "#"`
				if [ -z "$dir_validation" ] && [ -z "$inc_validation" ]; then
					ext_oval=`echo $ext_line | cut -f3 -d',' | cut -f1 -d'('`
					ext_nval=`grep -iw ^$ext_oval $DEF_APPS`
					if ! [ -z "$ext_nval" ]; then
						ext_ver=`awk '{printf("%5d: %s\n", FNR, $0)}' $inc_file | grep $ext_oval`
						ext_ver=`echo $ext_ver should be $ext_nval`
					elif ! [ -z `echo $ext_oval | grep -i macro` ]; then
						ext_oval=`echo $ext_line | cut -f3 -d','`
						ext_ver=`awk '{printf("%5d: %s\n", FNR, $0)}' $inc_file | grep $ext_oval`
						ext_ver=`echo $ext_ver should be Proc to launch the application`
					elif ! [ -z `echo $ext_oval | grep -i agi` ]; then
						ext_ver=`awk '{printf("%5d: %s\n", FNR, $0)}' $inc_file | grep $ext_oval`
						ext_ver=`echo $ext_ver should be OGI to launch the application`
						ext_ver=`echo $ext_ver and .ogi for the filename\'s extensions`
					fi
				elif ! [ -z `echo $ext_line | grep -i "^\[macro\-"` ]; then
					ext_oval=`echo $ext_line | cut -f2 -d '[' | cut -f1 -d ']'`
					ext_ver=`awk '{printf("%5d: %s\n", FNR, $0)}' $inc_file | grep $ext_oval`
					ext_ver=`echo $ext_ver should be [proc or [Proc`
				fi
			fi
			if ! [ -z "$ext_ver" ]; then
				echo $ext_ver >> validation.log
			fi
			ext_ver=""
		done
	done
	echo "Validation process finished"
	echo "To check the generated log file open the validation.log file with your preferred editor (ie: vi)"
}

case "$1" in
	modify)
		check_def_files
		build_include_list $DEF_EXTENS
		modify_files
	;;
	validate)
		check_def_files
		build_include_list $DEF_EXTENS
		validate_files
	;;
	*)
		printf "Usage: %s [modify|validate]\n" $0
		printf "Options: \n"
		printf "\tmodify: Modifies your %s and all the included files\n" $DEF_EXTENS
		printf "\t\tYour original files are renamed to .bak\n"
		printf "\t\tWARNING: If you run this script it will overwrite your existing .bak files\n"
		printf "\tvalidate: Validates your %s and all the included files\n" $DEF_EXTENS
		printf "\t\tIt will generate a logfile called validation.log\n"
		printf "\t\tWARNING: If you run this script it will overwrite your existing validation.log file\n"
	;;
esac
