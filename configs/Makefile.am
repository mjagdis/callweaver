AUTOMAKE_OPTS = gnu
opbxconf_DATA = adsi.conf.sample \
	adtranvofr.conf.sample \
	agents.conf.sample \
	cdr.conf.sample \
	cdr_custom.conf.sample \
	cdr_manager.conf.sample \
	cdr_odbc.conf.sample \
	cdr_pgsql.conf.sample \
	cdr_tds.conf.sample \
	chan_capi.sample \
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
	logger.conf \
	manager.conf.sample \
	meetme.conf.sample \
	mgcp.conf.sample \
	modem.conf.sample \
	modules.conf.sample \
	musiconhold.conf.sample \
	muted.conf.sample \
	openpbx.conf \
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

openpbx.conf: openpbx.conf.in Makefile
	rm -f openpbx.conf.tmp
	$(substitute) openpbx.conf.in > openpbx.conf.tmp
	mv openpbx.conf.tmp openpbx.conf

logger.conf: logger.conf.in Makefile
	rm -f logger.conf.tmp
	$(substitute) logger.conf.in > logger.conf.tmp
	mv logger.conf.tmp logger.conf
