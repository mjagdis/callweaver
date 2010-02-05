AUTOMAKE_OPTS = gnu

libexecdir = ${cwutilsdir}
libexec_SCRIPTS = safe_callweaver

noinst_DATA = callweaver \
	      rc.callweaver.debian \
	      rc.callweaver.gentoo \
	      rc.callweaver.mandrake \
	      rc.callweaver.redhat \
	      rc.callweaver.slackware \
	      rc.callweaver.suse

man_MANS = safe_callweaver.8

EXTRA_DIST = safe_callweaver.in safe_callweaver.8.in callweaver.in \
	     rc.callweaver.debian.in \
	     rc.callweaver.gentoo.in \
	     rc.callweaver.mandrake.in \
	     rc.callweaver.redhat.in \
	     rc.callweaver.slackware.in \
	     rc.callweaver.suse.in

CLEANFILES = $(libexec_SCRIPTS) $(noinst_DATA) $(man_MANS)


@substitute@


safe_callweaver:	safe_callweaver.in Makefile
	$(substitute) $(srcdir)/safe_callweaver.in > safe_callweaver
	chmod +x safe_callweaver

safe_callweaver.8:	safe_callweaver.8.in Makefile
	$(substitute) $(srcdir)/safe_callweaver.8.in > safe_callweaver.8

callweaver:	callweaver.in Makefile
	$(substitute) $(srcdir)/callweaver.in > callweaver

rc.callweaver.%:	rc.callweaver.%.in Makefile
	$(substitute) $(srcdir)/$@.in > $@


install-exec-hook:
	cd "$(DESTDIR)/$(cwutilsdir)" \
	&& $(LN_S) -f safe_callweaver cw_coreanalyse

install-data-hook:
	cfgdir=/etc/sysconfig; \
	test -d $$cfgdir || cfgdir=/etc/default; \
	test -f $(DESTDIR)$$cfgdir/callweaver \
	|| $(INSTALL) -m 0644 callweaver $(DESTDIR)$$cfgdir/callweaver
	@if [ -r /etc/debian_version ]; then \
		echo $(INSTALL) -m 0755 rc.callweaver.debian $(DESTDIR)/etc/init.d/callweaver; \
		$(INSTALL) -m 0755 rc.callweaver.debian $(DESTDIR)/etc/init.d/callweaver; \
	elif [ -r /etc/redhat-release ]; then \
		echo $(INSTALL) -m 0755 rc.callweaver.redhat $(DESTDIR)/etc/init.d/callweaver; \
		$(INSTALL) -m 0755 rc.callweaver.redhat $(DESTDIR)/etc/init.d/callweaver; \
	elif [ -r /etc/SuSE-release ]; then \
		echo $(INSTALL) -m 0755 rc.callweaver.suse $(DESTDIR)/etc/init.d/callweaver; \
		$(INSTALL) -m 0755 rc.callweaver.suse $(DESTDIR)/etc/init.d/callweaver; \
	else \
		echo; \
		echo '*** No init script has been installed.'; \
		echo '*** One of the init scripts in ./system MAY be usable...'; \
		echo; \
	fi
