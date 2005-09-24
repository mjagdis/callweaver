Summary: OpenPBX
Name: openpbx
Distribution: RedHat
Version: CVS
Release: 1
Copyright: Linux Support Services, inc.
Group: Utilities/System
Vendor: Linux Support Services, inc.
Packager: Robert Vojta <vojta@ipex.cz>
BuildRoot: /tmp/openpbx

%description
OpenPBX is an Open Source PBX and telephony development platform that
can both replace a conventional PBX and act as a platform for developing
custom telephony applications for delivering dynamic content over a
telephone similarly to how one can deliver dynamic content through a
web browser using CGI and a web server.

OpenPBX talks to a variety of telephony hardware including BRI, PRI, 
POTS, and IP telephony clients using the Inter-OpenPBX eXchange
protocol (e.g. gnophone or miniphone).  For more information and a
current list of supported hardware, see www.openpbxpbx.com.

%package        devel
Summary:        Header files for building OpenPBX modules
Group:          Development/Libraries

%description devel
This package contains the development  header files that are needed
to compile 3rd party modules.

%post
ln -sf /var/spool/openpbx/vm /var/lib/openpbx/sounds/vm

%files
#
# Configuration files
#
%attr(0755,root,root) %dir    /etc/openpbx
%config(noreplace) %attr(0640,root,root) /etc/openpbx/*.conf
%config(noreplace) %attr(0640,root,root) /etc/openpbx/*.adsi

#
# RedHat specific init script file
#
%attr(0755,root,root)       /etc/rc.d/init.d/openpbx

#
# Modules
#
%attr(0755,root,root) %dir /usr/lib/openpbx
%attr(0755,root,root) %dir /usr/lib/openpbx/modules
%attr(0755,root,root)      /usr/lib/openpbx/modules/*.so

#
# OpenPBX
#
%attr(0755,root,root)      /usr/sbin/openpbx
%attr(0755,root,root)      /usr/sbin/safe_openpbx
%attr(0755,root,root)      /usr/sbin/astgenkey
%attr(0755,root,root)      /usr/sbin/astman
%attr(0755,root,root)      /usr/sbin/autosupport
%attr(0755,root,root)      /usr/sbin/smsq
%attr(0755,root,root)      /usr/sbin/stereorize

#
# CDR Locations
#
%attr(0755,root,root) %dir /var/log/openpbx
%attr(0755,root,root) %dir /var/log/openpbx/cdr-csv
#
# Running directories
#
%attr(0755,root,root) %dir /var/run
#
# Sound files
#
%attr(0755,root,root) %dir /var/lib/openpbx
%attr(0755,root,root) %dir /var/lib/openpbx/sounds
%attr(0644,root,root)      /var/lib/openpbx/sounds/*.gsm
%attr(0755,root,root) %dir /var/lib/openpbx/sounds/digits
%attr(0644,root,root)      /var/lib/openpbx/sounds/digits/*.gsm
%attr(0755,root,root) %dir /var/lib/openpbx/sounds/letters
%attr(0644,root,root)      /var/lib/openpbx/sounds/letters/*.gsm
%attr(0755,root,root) %dir /var/lib/openpbx/sounds/phonetic
%attr(0644,root,root)      /var/lib/openpbx/sounds/phonetic/*.gsm
%attr(0755,root,root) %dir /var/lib/openpbx/mohmp3
%attr(0644,root,root)      /var/lib/openpbx/mohmp3/*
%attr(0755,root,root) %dir /var/lib/openpbx/images
%attr(0644,root,root)      /var/lib/openpbx/images/*
%attr(0755,root,root) %dir /var/lib/openpbx/keys
%attr(0644,root,root)      /var/lib/openpbx/keys/*
%attr(0755,root,root) %dir /var/lib/openpbx/agi-bin
%attr(0755,root,root) %dir /var/lib/openpbx/agi-bin/*
#
# Man page
#
%attr(0644,root,root)      /usr/share/man/man8/openpbx.8.gz
#
# Firmware
#
%attr(0755,root,root) %dir /var/lib/openpbx/firmware
%attr(0755,root,root) %dir /var/lib/openpbx/firmware/iax
%attr(0755,root,root)      /var/lib/openpbx/firmware/iax/*.bin

#
# Example voicemail files
#
%attr(0755,root,root) %dir /var/spool/openpbx
%attr(0755,root,root) %dir /var/spool/openpbx/voicemail
%attr(0755,root,root) %dir /var/spool/openpbx/voicemail/default
%attr(0755,root,root) %dir /var/spool/openpbx/voicemail/default/1234
%attr(0755,root,root) %dir /var/spool/openpbx/voicemail/default/1234/INBOX
%attr(0644,root,root)      /var/spool/openpbx/voicemail/default/1234/*.gsm

#
# Misc other spool
#
%attr(0755,root,root) %dir /var/spool/openpbx/system
%attr(0755,root,root) %dir /var/spool/openpbx/tmp

%files devel
#
# Include files
#
%attr(0755,root,root) %dir %{_includedir}/openpbx
%attr(0644,root,root) %{_includedir}/openpbx/*.h
