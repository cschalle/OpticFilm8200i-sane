##  Makefile.am -- an automake template for Makefile.in file
##  Copyright (C) 2009 Chris Bagwell and Sane Developers.
##
##  This file is part of the "Sane" build infra-structure.  See
##  included LICENSE file for license information.

EXTRA_PROGRAMS = test tstbackend

bin_PROGRAMS = scanimage

if COMPILE_SANED
sbin_PROGRAMS = saned
else
EXTRA_PROGRAMS += saned
endif

AM_CPPFLAGS += -I. -I$(srcdir) -I$(top_builddir)/include -I$(top_srcdir)/include

scanimage_SOURCES = scanimage.c jpegtopdf.c jpegtopdf.h sicc.c sicc.h stiff.c stiff.h
scanimage_LDADD = ../backend/libsane.la ../sanei/libsanei.la ../lib/liblib.la \
                  $(PNG_LIBS) $(JPEG_LIBS)

saned_SOURCES = saned.c
saned_CPPFLAGS = $(AM_CPPFLAGS) $(AVAHI_CFLAGS)
saned_LDADD = ../backend/libsane.la ../sanei/libsanei.la ../lib/liblib.la \
              $(SYSLOG_LIBS) $(SYSTEMD_LIBS) $(AVAHI_LIBS)

test_SOURCES = test.c
test_LDADD = ../lib/liblib.la ../backend/libsane.la

tstbackend_SOURCES = tstbackend.c
tstbackend_LDADD = ../lib/liblib.la ../backend/libsane.la

if preloadable_backends_enabled
if WITH_GENESYS_TESTS
##  Because the genesys backend is implemented in C++, programs need
##  to link against the standard C++ library.  The work-around below
##  will work for the GNU C++ compiler with the GNU standard library
##  for C++.  Other build scenarios may need work.
scanimage_LDADD += -lstdc++
saned_LDADD += -lstdc++
test_LDADD += -lstdc++
tstbackend_LDADD += -lstdc++
endif
endif

BUILT_SOURCES = saned@.service saned.xinetd.conf

EXTRA_DIST = saned.socket saned@.service.in saned.xinetd.conf.in

CLEANFILES = $(EXTRA_PROGRAMS) $(BUILT_SOURCES)

SUFFIXES = .in
.in:
	$(AM_V_GEN)
	@if $(AM_V_P); then echo Generating $@ from $^; fi
	@sed -e 's|@DATADIR@|$(datadir)|g' \
	     -e 's|@CONFIGDIR@|$(configdir)|g' \
	     -e 's|@DOCDIR@|$(docdir)|g' \
	     -e 's|@LIBDIR@|$(libdir)/sane|g' \
	     -e 's|@BINDIR@|$(bindir)|g' \
	     -e 's|@SBINDIR@|$(sbindir)|g' \
	     -e 's|@PACKAGEVERSION@|$(PACKAGE_VERSION)|g' $? > $@
