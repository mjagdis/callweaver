AUTOMAKE_OPTS                   = gnu
opbxmod_LTLIBRARIES             =

if WANT_APP_CONFERENCE
opbxmod_LTLIBRARIES                     += app_conference.la
app_conference_la_SOURCES                 = app_conference.c frame.c cli.c \
					conference.c member.c \
					libspeex/misc.c libspeex/preprocess.c \
					libspeex/smallft.c
app_conference_la_LDFLAGS                 = -module -avoid-version -no-undefined
app_conference_la_LIBADD                  = ${top_builddir}/corelib/libopenpbx.la
endif WANT_APP_CONFERENCE
