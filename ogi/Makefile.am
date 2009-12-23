AUTOMAKE_OPTS 			= gnu

AM_CFLAGS			= @CW_CFLAGS@


cwogi_PROGRAMS 		= eogi-sphinx-test eogi-test
cwogi_SCRIPTS 		= ogi-test.ogi fastogi-test
eogi_sphinx_test_SOURCES 	= eogi-sphinx-test.c
eogi_test_SOURCES 		= eogi-test.c

install-data-hook:
	if test -z "$(DESTDIR)"; \
	then \
		chown -R ${cwrunuser}:${cwrungroup} $(DESTDIR)${cwogidir}; \
	fi; \
	chmod 0750 $(DESTDIR)${cwogidir}

INCLUDES = -I$(top_builddir)/include -I$(top_srcdir)/include
