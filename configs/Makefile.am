AUTOMAKE_OPTS = gnu
SAMPLES = adsi.conf.sample \
	adtranvofr.conf.sample \
	agents.conf.sample \
	capi.conf.sample \
	cdr.conf.sample \
	cdr_custom.conf.sample \
	cdr_manager.conf.sample \
	cdr_tds.conf.sample \
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
	rpt.conf.sample \
	rtp.conf.sample \
	sip.conf.sample \
	sip_notify.conf.sample \
	voicemail.conf.sample

if WANT_CDR_ODBC
SAMPLES	+= cdr_odbc.conf.sample
endif WANT_CDR_ODBC

if WANT_CDR_PGSQL
SAMPLES	+= cdr_pgsql.conf.sample
endif WANT_CDR_PGSQL

if WANT_CHAN_ALSA
SAMPLES	+= alsa.conf.sample
endif WANT_CHAN_ALSA

if WANT_CHAN_FAX
SAMPLES += chan_fax.conf.sample
endif WANT_CHAN_FAX

if WANT_CHAN_UNICALL
SAMPLES += unicall.conf.sample
endif

if WANT_CHAN_WOOMERA
SAMPLES += woomera.conf.sample
endif WANT_CHAN_WOOMERA

if WANT_CHAN_ZAP
SAMPLES += zapata.conf.sample
endif WANT_CHAN_ZAP

if WANT_RES_JABBER
SAMPLES += res_jabber.conf.sample
endif WANT_RES_JABBER

if WANT_RES_ODBC
SAMPLES += res_odbc.conf.sample
endif WANT_RES_ODBC

if WANT_RES_PGSQL
SAMPLES += res_pgsql.conf.sample
endif WANT_RES_PGSQL

if WANT_RES_SQLITE
SAMPLES += res_sqlite.conf.sample
endif WANT_RES_SQLITE

noinst_DATA = $(SAMPLES)

@substitute@

EXTRA_DIST = $(SAMPLES) openpbx.conf.sample.in logger.conf.sample.in musiconhold.conf.sample.in
CLEANFILES = openpbx.conf.sample logger.conf.sample musiconhold.conf.sample

openpbx.conf.sample: openpbx.conf.sample.in Makefile
	rm -f openpbx.conf.sample.tmp
	$(substitute) $(srcdir)/openpbx.conf.sample.in > openpbx.conf.sample.tmp
	mv openpbx.conf.sample.tmp openpbx.conf.sample

logger.conf.sample: logger.conf.sample.in Makefile
	rm -f logger.conf.sample.tmp
	$(substitute) $(srcdir)/logger.conf.sample.in > logger.conf.sample.tmp
	mv logger.conf.sample.tmp logger.conf.sample

musiconhold.conf.sample: musiconhold.conf.sample.in Makefile
	rm -f musiconhold.conf.sample.tmp
	$(substitute) $(srcdir)/musiconhold.conf.sample.in > musiconhold.conf.sample.tmp
	mv musiconhold.conf.sample.tmp musiconhold.conf.sample

install-data-local:
	mkdir -p $(DESTDIR)${opbxconfdir}; \
	if test -z "$(DESTDIR)"; \
	then \
		chown ${opbxrunuser}:${opbxrungroup} $(DESTDIR)${opbxconfdir}; \
	fi; \
	chmod 0750 $(DESTDIR)${opbxconfdir}; \
	for samplefile in $(SAMPLES); \
	do \
		configfile=`basename $${samplefile} .sample`; \
		if test ! -f $(DESTDIR)${opbxconfdir}/$${configfile}; \
		then \
			$(INSTALL) -m 0640 $${samplefile} $(DESTDIR)${opbxconfdir}/$${configfile}; \
			if test -z "$(DESTDIR)"; \
			then \
				chown ${opbxrunuser}:${opbxrungroup} $(DESTDIR)${opbxconfdir}/$${configfile}; \
			fi; \
		fi; \
	done
