# Makefile for rpm tools.

AUTOMAKE_OPTIONS = 1.4 foreign

AM_CPPFLAGS = -I. \
	-I$(top_srcdir)/build \
	-I$(top_srcdir)/lib \
	-I$(top_srcdir)/rpmdb \
	-I$(top_srcdir)/rpmio \
	@INCPATH@ \
	-I$(top_srcdir)/misc

EXTRA_DIST =	rpmchecksig.c

EXTRA_PROGRAMS = rpminject

#myLDFLAGS= -L$(top_builddir)/build -L$(top_builddir)/lib \
#	-L$(top_builddir)/rpmio

LDADD = \
	$(top_builddir)/build/librpmbuild.la \
	$(top_builddir)/lib/librpm.la \
	$(top_builddir)/rpmdb/librpmdb.la \
	$(top_builddir)/rpmio/librpmio.la \
	@LIBINTL@

noinst_PROGRAMS = \
	dump dumpdb rpmarchive rpmheader rpmlead rpmsignature setcmp.static

check_PROGRAMS = test_rpmvercmp
TESTS = $(check_PROGRAMS)

pkgbindir = @RPMCONFIGDIR@
pkgbin_PROGRAMS = remappath2 \
	filesize \
	dump_ld_config \
	relative \
	mkset \
	setcmp \
	suggest_bpp \
	mksysusers \
	# javadeps pdeath_execute

#bin_PROGRAMS = rpmvercmp rpmevrcmp

remappath2_SOURCES = remappath2.c
remappath2_CFLAGS = -D_FILE_OFFSET_BITS=64

javadeps_SOURCES = javadeps.c

filesize_SOURCES = filesize.c
filesize_LDADD =

relative_SOURCES = relative.c
relative_LDADD =

dump_ld_config_SOURCES = dump_ld_config.c
dump_ld_config_LDADD =

pdeath_execute_SOURCES = pdeath_execute.c
pdeath_execute_LDADD =

mkset_SOURCES = mkset.c
setcmp_SOURCES = setcmp.c

setcmp_static_SOURCES = setcmp.c
setcmp_static_LDADD = @LDFLAGS_STATIC@ $(LDADD)

suggest_bpp_SOURCES = suggest_bpp.c
suggest_bpp_LDADD = -lm $(LDADD)

mksysusers_SOURCES = mksysusers.c

rpmvercmp_SOURCES = rpmvercmp.c

rpmevrcmp_SOURCES = rpmevrcmp.c

test_rpmvercmp_SOURCES = test_rpmvercmp.c

$(PROGRAMS): $(LDADD)

gnash.o: gnash.c
	$(COMPILE) -o $@ -c gnash.c

gnash: gnash.o
	$(LINK) -all-static -o $@ gnash.o $(LDADD)

