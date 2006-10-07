%define snap 1902

%bcond_without	fedora

Name:		openpbx
Version:	0
Release:	1.svn%{snap}%{?dist}a
Summary:	The Truly Open Source PBX

Group:		Applications/Internet
License:	GPL
URL:		http://www.openpbx.org/
# svn://svn.openpbx.org/openpbx/trunk
Source0:	openpbx-r%{snap}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	spandsp-devel >= 0.0.3-1.pre24
BuildRequires:	libtool automake autoconf
BuildRequires:	fedora-usermgmt-devel bluez-libs-devel openldap-devel
BuildRequires:	libjpeg-devel

Requires:	fedora-usermgmt /sbin/chkconfig

%{?FE_USERADD_REQ}

%description
OpenPBX is an Open Source PBX and telephony development platform that
can both replace a conventional PBX and act as a platform for developing
custom telephony applications for delivering dynamic content over a
telephone similarly to how one can deliver dynamic content through a
web browser using CGI and a web server.

OpenPBX talks to a variety of telephony hardware including BRI, PRI,
POTS, Bluetooth headsets and IP telephony clients using SIP and IAX
protocols protocol (e.g. ekiga or kphone).  For more information and a
current list of supported hardware, see www.openpbx.com.


%package devel
Group:		Applications/Internet
Summary:	Development package for %{name}
Requires:	%{name} = %{version}-%{release}

%description devel
Use this package for developing and building modules for OpenPBX


%package postgresql
Group:		Applications/Internet
Summary:	PostgreSQL support for %{name}
Requires:	%{name} = %{version}-%{release}

%description postgresql
This package contains modules for OpenPBX which make use of PostgreSQL.

%prep
%setup -q -n openpbx

%build
./bootstrap.sh

%configure --with-directory-layout=lsb --with-chan_bluetooth --with-res_sqlite \
	   --with-chan_fax --with-chan_capi --with-chan_alsa --with-app_ldap \
	   --disable-zaptel --enable-t38 --enable-postgresql --with-cdr-pgsql \
	   --with-res_config_pqsql --with-cdr-odbc --with-res_config_odbc
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
rm -f $RPM_BUILD_ROOT/%{_libdir}/openpbx.org/modules/*.la
rm -f $RPM_BUILD_ROOT/%{_libdir}/openpbx.org/*.a
rm -f $RPM_BUILD_ROOT/%{_libdir}/openpbx.org/*.la
mkdir -p $RPM_BUILD_ROOT%{_initrddir}
install contrib/fedora/openpbx $RPM_BUILD_ROOT%{_initrddir}/openpbx

%clean
rm -rf $RPM_BUILD_ROOT

%pre
%__fe_groupadd 30 -r openpbx &>/dev/null || :
%__fe_useradd  30 -r -s /sbin/nologin -d %{_sysconfdir}/openpbx.org -M \
                    -c 'OpenPBX user' -g openpbx openpbx &>/dev/null || :
%post
/sbin/chkconfig --add openpbx

%preun
test "$1" != 0 || /sbin/chkconfig --del openpbx

%postun
%__fe_userdel  openpbx &>/dev/null || :
%__fe_groupdel openpbx &>/dev/null || :

%files
%defattr(-,root,root,-)
%doc COPYING CREDITS LICENSE BUGS AUTHORS SECURITY README HARDWARE
%{_initrddir}/openpbx
%{_sbindir}/openpbx
%{_bindir}/streamplayer
%dir %{_libdir}/openpbx.org
%{_libdir}/openpbx.org/lib*.so.*
%dir %{_libdir}/openpbx.org/modules
%{_libdir}/openpbx.org/modules/*.so
%exclude %{_libdir}/openpbx.org/modules/*pgsql.so
%exclude %{_libdir}/openpbx.org/modules/app_sql_postgres.so
%{_mandir}/man8/openpbx.8.gz
%dir %{_datadir}/openpbx.org
%dir %{_datadir}/openpbx.org/ogi
%{_datadir}/openpbx.org/ogi/*
%dir %attr(0755,openpbx,openpbx) %{_sysconfdir}/openpbx.org
%config(noreplace) %attr(0644,openpbx,openpbx) %{_sysconfdir}/openpbx.org/*
%attr(0755,openpbx,openpbx) %{_localstatedir}/spool/openpbx.org
%attr(0755,openpbx,openpbx) %{_localstatedir}/log/openpbx.org
%attr(0755,openpbx,openpbx) %{_localstatedir}/run/openpbx.org


%files devel
%defattr(-,root,root,-)
%dir %{_includedir}/openpbx
%{_includedir}/openpbx/*.h
%{_libdir}/openpbx.org/lib*.so

%files postgresql
%{_libdir}/openpbx.org/modules/*pgsql.so
%{_libdir}/openpbx.org/modules/app_sql_postgres.so

%changelog
* Thu Oct  5 2006 David Woodhouse <dwmw2@infradead.org>
- Initial build
