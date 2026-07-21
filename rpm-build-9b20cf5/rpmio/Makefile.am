# Makefile for rpm library.

AUTOMAKE_OPTIONS = 1.4 foreign

EXTRA_DIST = tdigest.c tficl.c tkey.c trpmio.c

EXTRA_PROGRAMS = tdigest tkey tring trpmio

AM_CPPFLAGS = -I. @INCPATH@

pkgincdir = $(pkgincludedir)
noinst_HEADERS = \
		 rpmio.h rpmurl.h rpmmacro.h rpmlog.h rpmmessages.h rpmerr.h ugid.h \
		 bsearch.h rpmio_internal.h rpmpgp.h

lib_LTLIBRARIES = librpmio.la
librpmio_la_SOURCES = base64.c digest.c macro.c rpmio.c rpmlog.c rpmmalloc.c \
	rpmpgp.c rpmrpc.c strcasecmp.c stubs.c url.c ugid.c
librpmio_la_LDFLAGS = -release @VERSION@
librpmio_la_LIBADD = @LIBBEECRYPT@ @LIBZ@ -llzma

clean-local:
	rm -f *.o

tdigest_SOURCES = tdigest.c
tdigest_LDADD = librpmio.la @LIBPOPT@

trpmio_SOURCES = trpmio.c
trpmio_LDADD = librpmio.la @LIBPOPT@

tkey_SOURCES = tkey.c
tkey_LDFLAGS = -all-static
tkey_LDADD = librpmio.la @LIBPOPT@

tring_SOURCES = tring.c
tring_LDFLAGS = -all-static
tring_LDADD = librpmio.la @LIBPOPT@

.PHONY:	sources
sources:
	@echo $(librpmio_la_SOURCES:%=rpmio/%)

.PHONY:	lclint
lclint:
	lclint $(DEFS) $(AM_CPPFLAGS) $(librpmio_la_SOURCES)

tficl.o: tficl.c
	$(COMPILE) -I/usr/include/ficl -o $@ -c tficl.c 

tficl: tficl.o
	$(LINK) -o $@ tficl.o -lficl
