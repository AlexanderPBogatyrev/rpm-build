# Makefile for rpm library.

AUTOMAKE_OPTIONS = 1.4 foreign

AM_CPPFLAGS = -I. \
	-I$(top_srcdir)/build \
	-I$(top_srcdir)/lib \
	-I$(top_srcdir)/rpmio \
	@INCPATH@

LIBDB = -ldb
LIBELF = @WITH_LIBELF_LIB@
LIBPOPT = @LIBPOPT@

pkgincdir = $(pkgincludedir)
noinst_HEADERS = \
		 hdrinline.h rpmdb.h \
		 fprint.h rpmhash.h jhash.h

mylibpaths = -L$(top_builddir)/lib/.libs
mylibs = -lrpm

LIBS =

lib_LTLIBRARIES = librpmdb.la
librpmdb_la_SOURCES = db3.c \
	dbconfig.c hdrNVR.c header.c fprint.c legacy.c merge.c poptDB.c \
	rpmhash.c rpmdb.c rpmvercmp.c tagname.c tagtbl.c
librpmdb_la_LDFLAGS = -release @VERSION@
librpmdb_la_LIBADD = $(LIBDB) $(LIBELF) $(LIBPOPT) $(top_builddir)/rpmio/librpmio.la @WITH_SELINUX_LIB@
librpmdb_la_DEPENDENCIES = $(top_builddir)/rpmio/librpmio.la

tagtbl.c: Makefile.am $(top_srcdir)/lib/rpmlib.h 
	@echo '/*@-bounds@*/' > $@
	@echo '#include "system.h"' >> $@
	@echo '#include "rpmlib.h"' >> $@
	@echo '' >> $@
	@echo '/*@observer@*/ /*@unchecked@*/' >> $@
	@echo 'const struct headerTagTableEntry_s rpmTagTable[] = {' >> $@
	${AWK} '/(RPMTAG_[A-Z0-9]*)[ \t]+([0-9]*)/ && !/internal/ { if ($$2 == "=") { printf("\t{ \"%s\",	%s },\n", $$1, $$3) } else { printf("\t{ \"%s\",	%s },\n", $$2, $$3) } }' < $(top_srcdir)/lib/rpmlib.h >> $@
	@echo '	{ NULL, 0 }' >> $@
	@echo '};' >> $@
	@echo '' >> $@
	@echo '/*@unchecked@*/' >> $@
	@echo 'const int rpmTagTableSize = sizeof(rpmTagTable) / sizeof(rpmTagTable[0]) - 1;' >> $@
	@echo '/*@=bounds@*/' >> $@

BUILT_SOURCES = tagtbl.c

clean-local:
	rm -f $(DB3LOBJS) *.o

.PHONY:	sources
sources:
	@echo $(librpmdb_la_SOURCES:%=rpmdb/%)

.PHONY:	lclint
lclint:
	lclint $(DEFS) $(AM_CPPFLAGS) $(librpmdb_la_SOURCES)

tdbi: librpmdb.la tdbi.o
	$(LINK) -all-static $@.o $< $(mylibpaths) $(mylibs) $(LIBS)
