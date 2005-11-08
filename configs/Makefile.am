AUTOMAKE_OPTS = gnu
opbxconf_DATA = adsi.conf.sample \
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
	res_jabber.conf.sample
	res_odbc.conf.sample \
	res_pgsql.conf.sample \
	rpt.conf.sample \
	rtp.conf.sample \
	sip.conf.sample \
	sip_notify.conf.sample \
	unicall.conf.sample \
	voicemail.conf.sample \
	vpb.conf.sample \
	zapata.conf.sample

@substitute@

openpbx.conf.sample: openpbx.conf.sample.in Makefile
	rm -f openpbx.conf.sample.tmp
	$(substitute) openpbx.conf.sample.in > openpbx.conf.sample.tmp
	mv openpbx.conf.sample.tmp openpbx.conf.sample

logger.conf.sample: logger.conf.sample.in Makefile
	rm -f logger.conf.sample.tmp
	$(substitute) logger.conf.sample.in > logger.conf.sample.tmp
	mv logger.conf.sample.tmp logger.conf.sample
