AUTOMAKE_OPTS = gnu

# Note: the contents of the "after" directory are not actually installed.
# The "after" directory just contains overrides for standard vim stuff
# to give improved operation with callweaver code (e.g. knowledge that
# "%...@" is a valid conversion due to our printf extensions).
# To use it simply copy or link to your own ~/.vim/after.


noinst_DATA = doc/callweaver.txt \
	      doc/tags \
	      ftdetect/callweaver.vim \
              syntax/cw-callweaver.vim \
              syntax/cw-cdr-custom.vim \
              syntax/cw-cdr-pgsql-custom.vim \
              syntax/cw-chan-dahdi.vim \
              syntax/cw-conf.vim \
              syntax/cw-extensions.vim \
              syntax/cw-generic.vim \
              syntax/cw-sip.vim \
              syntax/cw-voicemail.vim \
              syntax/syntax.vim


install-data-hook:
	@umask 0022; \
	for path in /usr/share/vim/site /usr/share/vim/vimfiles /usr/local/share/vim/site /usr/local/share/vim/vimfiles; do \
		if [ -d "$${path}" ]; then \
			for file in $(noinst_DATA); do \
				echo $(INSTALL) -m 0644 $(srcdir)/$${file} $(DESTDIR)$${path}/$${file}; \
				$(INSTALL) -m 0644 $(srcdir)/$${file} $(DESTDIR)$${path}/$${file}; \
			done; \
			echo vim +"helptags $${path}/doc" +'quit'; \
			vim +"helptags $${path}/doc" +'quit'; \
		fi; \
	done; \
	path=`echo ~${cwrunuser}`; \
	for file in $(noinst_DATA); do \
		echo $(INSTALL) -D -m 0644 $(srcdir)/$${file} $(DESTDIR)$${path}/.vim/$${file}; \
		$(INSTALL) -D -m 0644 $(srcdir)/$${file} $(DESTDIR)$${path}/.vim/$${file}; \
	done; \
	echo vim +"helptags $${path}/doc" +'quit'; \
	vim +"helptags $${path}/doc" +'quit'
