# Makefile for rpm library.

AUTOMAKE_OPTIONS = 1.4 foreign

AM_CPPFLAGS = -I. \
	-I$(top_srcdir)/build \
	-I$(top_srcdir)/rpmdb \
	-I$(top_srcdir)/rpmio \
	@INCPATH@

pkgincdir = $(pkgincludedir)
noinst_HEADERS = \
	header.h misc.h rpmcli.h rpmlib.h stringbuf.h \
	al.h cpio.h depends.h fsm.h header_internal.h \
	manifest.h psm.h \
	rpmlead.h set.h signature.h

mylibpaths = \
	-L$(top_builddir)/lib/.libs \
	-L$(top_builddir)/rpmdb/.libs \
	-L$(top_builddir)/rpmio/.libs

mylibs = -lrpm -lrpmdb -lrpmio @LIBS@ @LIBINTL@
LIBS =

lib_LTLIBRARIES = librpm.la
librpm_la_SOURCES = \
	al.c closeall.c cpio.c depends.c formats.c fs.c fsm.c \
	header_internal.c manifest.c misc.c order.c package.c \
	problems.c poptI.c poptK.c poptQV.c psm.c query.c \
	rpmchecksig.c rpminstall.c rpmlead.c rpmlibprov.c rpmrc.c \
	set.c signature.c stringbuf.c transaction.c \
	verify.c
librpm_la_LDFLAGS = -release @VERSION@
librpm_la_LIBADD = $(top_builddir)/rpmdb/librpmdb.la $(top_builddir)/rpmio/librpmio.la @LIBPOPT@ @LIBBEECRYPT@ @WITH_SELINUX_LIB@
librpm_la_DEPENDENCIES = $(top_builddir)/rpmdb/librpmdb.la $(top_builddir)/rpmio/librpmio.la

noinst_PROGRAMS = test-set
test_set_SOURCES = set.c
test_set_CPPFLAGS = $(AM_CPPFLAGS) -DSELF_TEST
test_set_LDADD = $(top_builddir)/rpmio/librpmio.la

.PHONY:	sources
sources:
	@echo $(librpm_la_SOURCES:%=lib/%)

.PHONY:	lclint
lclint:
	lclint $(DEFS) $(AM_CPPFLAGS) $(librpm_la_SOURCES)

th: th.c librpm.la
	$(CC) $(CFLAGS) $(DEFS) $(AM_CPPFLAGS) -o $@ $< $(mylibpaths) $(mylibs) $(LIBS)

trb: trb.o librpm.la
	$(LINK) @LDFLAGS_STATIC@ $(CFLAGS) $(DEFS) $(AM_CPPFLAGS) -o $@ trb.o $(mylibs) $(LIBS)

trhn: trhn.o librpm.la
	$(LINK) @LDFLAGS_STATIC@ $(CFLAGS) $(DEFS) $(AM_CPPFLAGS) -o $@ trhn.o $(mylibs) $(LIBS)
