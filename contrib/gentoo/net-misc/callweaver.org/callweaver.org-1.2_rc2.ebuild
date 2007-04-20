# Copyright 1999-2005 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

inherit eutils 

IUSE="alsa debug fax jabber odbc osp oss mgr2 mysql postgres pri profile speex t38 zap"

DESCRIPTION="open Source Private Branch Exchange System"
HOMEPAGE="http://www.callweaver.org/"
SRC_URI="http://www.callweaver.org/callweaver.org-1.2_rc2.tar.gz"


#S=${WORKDIR}/${PN}
S=/var/tmp/portage/callweaver.org-1.2_rc2/work/callweaver.org-1.2_rc2/

SLOT="0"
LICENSE="GPL-2"
KEYWORDS="x86"

RDEPEND="virtual/libc
	sys-libs/ncurses
	sys-libs/zlib
	sys-libs/libcap
	dev-libs/libedit
	dev-libs/openssl
	>=media-libs/spandsp-0.0.3_pre27
	osp? ( net-libs/osptoolkit )
	pri? ( net-libs/libpri )
	zap? ( net-misc/zaptel )
	alsa? ( media-libs/alsa-lib )
	mgr2? ( net-libs/libunicall )
	mysql? ( dev-db/mysql )
	odbc? ( dev-db/unixODBC )
	speex? ( media-libs/speex )
	jabber? ( net-libs/loudmouth )
	postgres? ( dev-db/libpq )"


DEPEND="${RDEPEND}
	sys-devel/flex
	>=sys-devel/automake-1.9.6
	>=sys-devel/autoconf-2.59
	>=sys-devel/libtool-1.5.20"

src_compile() {
	econf \
		--libdir=/usr/lib \
		--datadir=/var/lib \
		--localstatedir=/var \
		--sharedstatedir=/var/lib/callweaver.org \
		--with-directory-layout=lsb 		\
		`use_with speex codec_speex`		\
		`use_with jabber res_jabber`		\
		`use_with postgres cdr_pgsql`		\
		`use_with postgres res_config_pgsql`	\
		`use_with odbc res_odbc`		\
		`use_with odbc res_config_odbc`		\
		`use_with odbc cdr_odbc`		\
		`use_with mysql res_mysql`		\
		`use_with mysql res_config_mysql`		\
		`use_with mysql cdr_mysql`		\
		`use_with zap chan_zap`			\
		`use_with mgr2 chan_unicall`		\
		`use_with fax chan_fax`			\
		`use_with fax app_rxfax`		\
		`use_with fax app_txfax`		\
		`use_with t38 app_rxfax`		\
		`use_with t38 app_txfax`		\
		`use_enable debug`			\
		`use_enable mysql`			\
		`use_enable postgres postgresql`			\
		`use_enable profile`			\
		`use_enable t38`			\
		|| die "configure failed" 

	emake || die "make failed"
}

src_install() {
	make DESTDIR=${D} install || die "make install failed"

	dodoc README INSTALL AUTHORS COPYING NEWS BUGS
	dodoc TODO_FOR_AUTOMAKE SECURITY CREDITS HARDWARE LICENSE

	dodoc doc/README* doc/*.txt doc/*.pdf

	docinto samples
	dodoc ${D}etc/callweaver.org/*.sample

	# remove dir
	rm -rf ${D}var/lib/callweaver.org/doc

	newconfd ${FILESDIR}/callweaver.confd callweaver
	exeinto /etc/init.d
	newexe ${FILESDIR}/callweaver.rc6 callweaver


	# don't delete these
	keepdir /var/{log,run,spool}/callweaver.org
	keepdir /var/lib/callweaver.org/{images,keys}
}

pkg_preinst() {
	if [[ -z "$(egetent passwd callweaver)" ]]; then
		einfo "Creating callweaver group and user..."
		enewgroup callweaver
		enewuser callweaver -1 -1 /var/lib/callweaver callweaver
	fi
}

pkg_postinst() {
	# only change permissions if callweaver wasn't installed before
	einfo "Fixing permissions..."

	chmod -R u=rwX,g=rX,o=	${ROOT}etc/callweaver.org
	chown -R root:callweaver   ${ROOT}etc/callweaver.org

	for x in lib log run spool; do
		chmod -R u=rwX,g=rX,o=	${ROOT}var/${x}/callweaver.org
		chown -R callweaver:callweaver  ${ROOT}var/${x}/callweaver.org
	done
}

pkg_config() {
	# TODO: ask user if he want to reset permissions back to sane defaults
	einfo "Do you want to reset the permissions and ownerships of callweaver.org to"
	einfo "the default values (y/N)?"
	read res

	res="$(echo $res | tr [[:upper:]] [[:lower:]])"

	if [[ "$res" = "y" ]] || \
	   [[ "$res" = "yes" ]]
	then
		einfo "First time installation, fixing permissions..."

		chmod -R u=rwX,g=rX,o=	${ROOT}etc/callweaver.org
		chown -R root:callweaver   ${ROOT}etc/callweaver.org

		for x in lib log run spool; do
			chmod -R u=rwX,g=rX,o=	${ROOT}var/${x}/callweaver.org
			chown -R callweaver:callweaver  ${ROOT}var/${x}/callweaver.org
		done
	fi
}
