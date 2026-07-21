# Makefile for rpmbuild library.

AUTOMAKE_OPTIONS = 1.4 foreign

AM_CPPFLAGS = -I. \
	-I$(top_srcdir)/lib \
	-I$(top_srcdir)/rpmdb \
	-I$(top_srcdir)/rpmio \
	@INCPATH@

LIBS =

pkgincdir = $(pkgincludedir)
noinst_HEADERS = \
		 rpmbuild.h rpmspec.h \
		 buildio.h checkFiles.h interdep.h

check_SCRIPTS = test_conditionals test_dnl test_shrink test_macros test_description \
		test_caps test_removepostfixes \
		test_specstep \
		test_makecleanskiplist \
		test_sysusers \
		#
TESTS = $(check_SCRIPTS)

lib_LTLIBRARIES = librpmbuild.la
librpmbuild_la_SOURCES = \
	build.c checkFiles.c expression.c files.c interdep.c misc.c names.c \
	pack.c parseBuildInstallClean.c parseChangelog.c parseDescription.c \
	parseFiles.c parsePreamble.c parsePrep.c parseReqs.c parseScript.c \
	parseSpec.c poptBT.c reqprov.c spec.c
librpmbuild_la_LDFLAGS = -release @VERSION@.1
librpmbuild_la_LIBADD = $(top_builddir)/rpmio/librpmio.la \
	$(top_builddir)/rpmdb/librpmdb.la \
	$(top_builddir)/lib/librpm.la @LIBPOPT@ @WITH_CAP_LIB@
librpmbuild_la_DEPENDENCIES = $(top_builddir)/rpmio/librpmio.la \
	$(top_builddir)/rpmdb/librpmdb.la \
	$(top_builddir)/lib/librpm.la

.PHONY:	sources
sources:
	@echo $(librpmbuild_la_SOURCES:%=build/%)

.PHONY:	lclint
lclint:
	lclint ${DEFS} ${AM_CPPFLAGS} ${librpmbuild_la_SOURCES}
