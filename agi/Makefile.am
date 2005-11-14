AUTOMAKE_OPTS 			= gnu
opbxagi_PROGRAMS 		= eagi-sphinx-test eagi-test
opbxagi_SCRIPTS 		= agi-test.agi fastagi-test
eagi_sphinx_test_SOURCES 	= eagi-sphinx-test.c
eagi_test_SOURCES 		= eagi-test.c

install-exec-hook:
	if test -z "$(DESTDIR)"; \
	then \
		chown -R ${opbxrunuser}:${opbxrungroup} $(DESTDIR)${opbxagidir}; \
	fi; \
	chmod 0750 $(DESTDIR)${opbxagidir}

INCLUDES = -I$(top_srcdir)/include
