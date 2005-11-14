AUTOMAKE_OPTS = gnu
SAMPLES = adsi.conf.sample \
	adtranvofr.conf.sample \
	agents.conf.sample \
	capi.conf.sample \
	cdr.conf.sample \
	cdr_custom.conf.sample \
	cdr_manager.conf.sample \
	cdr_odbc.conf.sample \
	cdr_pgsql.conf.sample \
	cdr_tds.conf.sample \
	chan_fax.conf.sample \
	codecs.conf.sample \
	dnsmgr.conf.sample \
	dundi.conf.sample \
	enum.conf.sample \
	extconfig.conf.sample \
	extensions.conf.sample \
	features.conf.sample \
	iax.conf.sample \
	iaxprov.conf.sample \
	indications.conf.sample \
	logger.conf.sample \
	manager.conf.sample \
	meetme.conf.sample \
	mgcp.conf.sample \
	modem.conf.sample \
	modules.conf.sample \
	musiconhold.conf.sample \
	muted.conf.sample \
	openpbx.adsi.sample \
	openpbx.conf.sample \
	osp.conf.sample \
	privacy.conf.sample \
	queues.conf.sample \
	res_jabber.conf.sample \
	res_odbc.conf.sample \
	res_pgsql.conf.sample \
	rpt.conf.sample \
	rtp.conf.sample \
	sip.conf.sample \
	sip_notify.conf.sample \
	unicall.conf.sample \
	voicemail.conf.sample \
	woomera.conf.sample \
	zapata.conf.sample

opbxconf_DATA = $(SAMPLES)

@substitute@

EXTRA_DIST = $(SAMPLES) openpbx.conf.sample.in logger.conf.sample.in
CLEANFILES = openpbx.conf.sample logger.conf.sample musiconhold.conf.sample

openpbx.conf.sample: openpbx.conf.sample.in Makefile
	rm -f openpbx.conf.sample.tmp
	$(substitute) $(srcdir)/openpbx.conf.sample.in > openpbx.conf.sample.tmp
	mv openpbx.conf.sample.tmp openpbx.conf.sample

logger.conf.sample: logger.conf.sample.in Makefile
	rm -f logger.conf.sample.tmp
	$(substitute) $(srcdir)/logger.conf.sample.in > logger.conf.sample.tmp
	mv logger.conf.sample.tmp logger.conf.sample

install-data-hook:
	if test -z "$(DESTDIR)"; \
	then \
		chown -R ${opbxrunuser}:${opbxrungroup} $(DESTDIR)${opbxconfdir}; \
	fi
	chmod 0750 $(DESTDIR)${opbxconfdir}
	chmod 0640 $(DESTDIR)${opbxconfdir}/*.sample
	for samplefile in $(DESTDIR)${opbxconfdir}/*.sample; \
	do \
		configfile=`echo $${samplefile} | $(SED) 's/\.sample//'`; \
		if test \! -f $${configfile}; \
		then \
			cp -p $${samplefile} $${configfile}; \
		fi; \
	done
