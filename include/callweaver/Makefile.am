AUTOMAKE_OPTS = gnu

AM_CFLAGS			= @CW_CFLAGS@


cwinclude_HEADERS = acl.h \
                      app.h \
                      astobj.h \
                      astosp.h \
                      callweaver_db.h \
                      callweaver_expr.h \
                      callweaver_hash.h \
                      callweaver_pcm.h \
                      causes.h \
                      cdr.h \
                      channel.h \
                      chanvars.h \
                      cli.h \
                      config.h \
                      crypto.h \
                      curl.h \
                      devicestate.h \
                      dns.h \
                      dsp.h \
                      enum.h \
                      features.h \
                      frame.h \
                      generator.h \
                      generic_jb.h \
                      image.h \
                      indications.h \
                      io.h \
		      keywords.h \
                      linkedlists.h \
                      localtime.h \
                      lock.h \
                      logger.h \
                      manager.h \
                      module.h \
                      monitor.h \
                      musiconhold.h \
                      options.h \
                      pbx.h \
                      phone_no_utils.h \
		      printf.h \
                      privacy.h \
                      rtp.h \
                      say.h \
                      sched.h \
                      slinfactory.h \
                      srv.h \
                      strings.h \
                      stun.h \
                      time.h \
                      transcap.h \
                      translate.h \
                      udpfromto.h \
                      udptl.h \
                      unaligned.h \
                      utils.h \
	              file.h

keywords.h:	$(top_builddir)/utils/genkeywords
	$(top_builddir)/utils/genkeywords > $@
