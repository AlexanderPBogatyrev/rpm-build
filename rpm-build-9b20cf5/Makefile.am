# Top level Makefile for rpm

AUTOMAKE_OPTIONS = 1.4 foreign

EXTRA_DIST = CHANGES CREDITS Doxyheader GROUPS INSTALL \
	gendiff installplatform platform* \
	po/*.in po/*.po po/rpm.pot \
	rpmpopt-$(VERSION) rpmqv.c

SUBDIRS = po rpmio rpmdb lib build tools scripts autodeps doc .

AM_CPPFLAGS = \
	-I$(top_srcdir)/build \
	-I$(top_srcdir)/lib \
	-I$(top_srcdir)/rpmdb \
	-I$(top_srcdir)/rpmio \
	@INCPATH@

myLDFLAGS = # @LDFLAGS_STATIC@

mylibLDADD = $(top_builddir)/lib/librpm.la $(top_builddir)/rpmio/librpmio.la \
	     $(top_builddir)/rpmdb/librpmdb.la @LIBPOPT@
myLDADD = $(mylibLDADD) $(top_builddir)/build/librpmbuild.la

rpmbindir = `echo $(bindir) | sed -e s,usr/bin,bin,`
rpmbin_PROGRAMS =

#bin_PROGRAMS =		rpm rpm2cpio
bin_SCRIPTS =		gendiff

pkgdatadir =		@RPMCONFIGDIR@
pkglibdir =		@RPMCONFIGDIR@
pkglibexecdir =		@RPMCONFIGDIR@
pkgdata_DATA =		rpmrc rpmpopt-$(VERSION) macros
pkglibexec_PROGRAMS =	rpmb # rpmd rpmi rpmk rpmq
pkglibexec_SCRIPTS =	find-provides find-requires

rpmpopt-$(VERSION): rpmpopt
	cp rpmpopt $@

noinst_HEADERS = build.h debug.h system.h

rpm_SOURCES =
rpm_LDFLAGS =		$(myLDFLAGS)
rpm_LDADD =		rpm.o $(myLDADD)
rpm.o:	rpmqv.c
	$(COMPILE) -DIAM_RPMDB -DIAM_RPMEIU -DIAM_RPMK -DIAM_RPMQV -o $@ -c rpmqv.c

rpm_static_SOURCES =
rpm_static_LDFLAGS =		@LDFLAGS_STATIC@ $(myLDFLAGS) -pthread
rpm_static_LDADD =		rpm.static.o $(myLDADD)
rpm.static.o:	rpmqv.c
	$(COMPILE) -DIAM_RPMDB -DIAM_RPMEIU -DIAM_RPMK -DIAM_RPMQV -o $@ -c rpmqv.c

rpmb_SOURCES =		build.c
rpmb_LDFLAGS =		$(myLDFLAGS)
rpmb_LDADD =		rpmb.o $(myLDADD)
rpmb.o:	rpmqv.c
	$(COMPILE) -DIAM_RPMBT -o $@ -c rpmqv.c

rpmd_SOURCES =
rpmd_LDFLAGS =		$(myLDFLAGS)
rpmd_LDADD =		rpmd.o $(mylibLDADD)
rpmd.o:	rpmqv.c
	$(COMPILE) -DIAM_RPMDB -o $@ -c rpmqv.c

rpmi_SOURCES =
rpmi_LDFLAGS =		$(myLDFLAGS)
rpmi_LDADD =		rpmi.o $(mylibLDADD)
rpmi.o:	rpmqv.c
	$(COMPILE) -DIAM_RPMEIU -o $@ -c rpmqv.c

rpmk_SOURCES =
rpmk_LDFLAGS =		$(myLDFLAGS)
rpmk_LDADD =		rpmk.o $(mylibLDADD)
rpmk.o:	rpmqv.c
	$(COMPILE) -DIAM_RPMK -o $@ -c rpmqv.c

rpmq_SOURCES =
rpmq_LDFLAGS =		$(myLDFLAGS)
rpmq_LDADD =		rpmq.o $(myLDADD)
rpmq.o:	rpmqv.c
	$(COMPILE) -DIAM_RPMQV -o $@ -c rpmqv.c

rpm2cpio_SOURCES =	rpm2cpio.c
rpm2cpio_LDFLAGS =	$(myLDFLAGS)
rpm2cpio_LDADD =	$(mylibLDADD)

rpm2cpio_static_SOURCES =	rpm2cpio.c
rpm2cpio_static_LDFLAGS =	@LDFLAGS_STATIC@ $(myLDFLAGS)
rpm2cpio_static_LDADD =		$(mylibLDADD)

$(PROGRAMS): 		$(myLDADD) @WITH_APIDOCS_TARGET@

.PHONY:	lclint
lclint:
	lclint $(DEFS) $(AM_CPPFLAGS) rpmqv.c $(rpmb_SOURCES) \
		`make -s sources -C build` \
		`make -s sources -C lib` \
		`make -s sources -C rpmdb` \
		`make -s sources -C rpmio`

CVSTAG = r$(subst .,-,$(VERSION))

pkgsrcdir = $(prefix)/src/RPM

install-data-local:
	@: $(mkinstalldirs) $(DESTDIR)$(rpmbindir)
	@: @LN_S@ ..$(bindir)/rpm $(DESTDIR)$(rpmbindir)/
	@: $(mkinstalldirs) $(DESTDIR)$(varprefix)/lib/rpm
	@: rm -f $(DESTDIR)$(prefix)/lib/rpmrc
	@: @LN_S@ rpm/rpmrc $(DESTDIR)$(prefix)/lib/rpmrc
	@: rm -f $(DESTDIR)$(prefix)/lib/rpmpopt
	@: @LN_S@ rpm/rpmpopt-$(VERSION) $(DESTDIR)$(prefix)/lib/rpmpopt
	@rm -f $(DESTDIR)$(prefix)/lib/rpm/rpmt
	@@LN_S@ rpmb $(DESTDIR)$(prefix)/lib/rpm/rpmt
	@: rm -f $(DESTDIR)$(prefix)/lib/rpm/rpme
	@: @LN_S@ rpmi $(DESTDIR)$(prefix)/lib/rpm/rpme
	@: rm -f $(DESTDIR)$(prefix)/lib/rpm/rpmu
	@: @LN_S@ rpmi $(DESTDIR)$(prefix)/lib/rpm/rpmu
	@: rm -f $(DESTDIR)$(prefix)/lib/rpm/rpmv
	@: @LN_S@ rpmq $(DESTDIR)$(prefix)/lib/rpm/rpmv
	rm -f $(DESTDIR)$(bindir)/rpmbuild
	@LN_S@ ../lib/rpm/rpmb $(DESTDIR)$(bindir)/rpmbuild
	@: rm -f $(DESTDIR)$(bindir)/rpmquery
	@: LN_S@ ../lib/rpm/rpmq $(DESTDIR)$(bindir)/rpmquery
	@: rm -f $(DESTDIR)$(bindir)/rpmverify
	@: LN_S@ ../lib/rpm/rpmv $(DESTDIR)$(bindir)/rpmverify
	@: rm -f $(DESTDIR)$(bindir)/rpmsign
	@: LN_S@ ../lib/rpm/rpmk $(DESTDIR)$(bindir)/rpmsign
	@: rm -f $(DESTDIR)$(bindir)/rpmdb
	@: LN_S@ ../lib/rpm/rpmd $(DESTDIR)$(bindir)/rpmdb
	@for bf in e i u ; do \
	    : rm -f $(DESTDIR)$(bindir)/rpm$$bf ; \
	    : @LN_S@ ../lib/rpm/rpm$$bf $(DESTDIR)$(bindir)/rpm$$bf ; \
	done
	rm -f $(DESTDIR)$(bindir)/relative
	@LN_S@ ../lib/rpm/relative $(DESTDIR)$(bindir)/relative
	@for dir in BUILD RPMS SOURCES SPECS SRPMS ; do \
	    $(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/$$dir; \
	done
	@case "@host_cpu@" in \
	*86)	$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/i386 ;\
		$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/i486 ;\
		$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/i586 ;\
		$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/i686 ;\
		$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/athlon ;\
		$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/athlon_xp ;\
		$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/pentium2 ;\
		$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/pentium3 ;\
		$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/pentium4 ;;\
	alpha*) $(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/alpha ;\
		$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/alphaev6 ;;\
	sparc*) $(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/sparc ;\
		$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/sparcv9 ;\
		$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/sparc64 ;;\
	ia64*)	$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/ia64 ;;\
	s390*)	$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/s390 ;;\
	mipsel*) $(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/mipsel ;;\
	mips*)  $(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/mips ;;\
	powerpc*) $(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/ppc ;\
		$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/ppc64 ;;\
	*)	$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/@host_cpu@ ;;\
	esac
	$(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/noarch
	@case "@host_os@" in \
	mint) $(mkinstalldirs) $(DESTDIR)$(pkgsrcdir)/RPMS/m68kmint ;;\
	solaris*|linux*) \
	   chmod u+x ./installplatform; DESTDIR="$(DESTDIR)" pkglibdir="$(pkglibdir)" ./installplatform rpmrc macros platform ;; \
	esac
	@: $(mkinstalldirs) $(DESTDIR)/var/tmp

.PHONY:	setperms
setperms:
	@for f in $(rpmbin_PROGRAMS) ; do\
	    $(__CHOWN) ${RPMUSER}.${RPMGROUP} $(DESTDIR)$(rpmbindir)/$$f ;\
	    $(__CHMOD) g+s $(DESTDIR)$(rpmbindir)/$$f ;\
	done
	@for f in $(bin_PROGRAMS) ; do\
	    $(__CHOWN) ${RPMUSER}.${RPMGROUP} $(DESTDIR)$(bindir)/$$f ;\
	done
	@for f in $(pkglibexec_PROGRAMS) ; do\
	    $(__CHOWN) ${RPMUSER}.${RPMGROUP} $(DESTDIR)$(pkglibexecdir)/$$f ;\
	    $(__CHMOD) g+s $(DESTDIR)$(pkglibexecdir)/$$f ;\
	done
	@for f in $(pkglibexec_SCRIPTS) ; do\
	    $(__CHOWN) ${RPMUSER}.${RPMGROUP} $(DESTDIR)$(pkglibexecdir)/$$f ;\
	done
	@$(__CHOWN) ${RPMUSER}.${RPMGROUP} $(DESTDIR)$(pkglibexecdir)
	@$(__CHOWN) -R ${RPMUSER}.${RPMGROUP} $(DESTDIR)$(varprefix)/lib/rpm
	-@$(__CHMOD) 0664 $(DESTDIR)$(varprefix)/lib/rpm/[A-Z]*
	-@$(__CHMOD) 0775 $(DESTDIR)$(varprefix)/lib/rpm
	-@$(__CHMOD) 0664 $(DESTDIR)$(varprefix)/lib/rpm/__db.*

.PHONY:	unsetgid
unsetgid:
	@for f in $(rpmbin_PROGRAMS) ; do\
	    $(__CHMOD) g-s $(DESTDIR)$(rpmbindir)/$$f ;\
	done
	@for f in $(pkglibexec_PROGRAMS) ; do\
	    $(__CHMOD) g-s $(DESTDIR)$(pkglibexecdir)/$$f ;\
	done

.PHONY: tar
tar:
	rm -rf /tmp/rpm-$(VERSION)
	$(MAKE) DESTDIR=/tmp/rpm-$(VERSION) install
	cd /tmp/rpm-$(VERSION) ; tar cvf /tmp/rpm-$(VERSION).tar .

.PHONY: noconfig
noconfig:
	find . -name "Makefile" -exec rm {} \; 
	rm -f *gz *rpm config.*

.PHONY: archive
archive: 
	@cvs -d `cat CVS/Root` diff 2>&1 > /dev/null || { \
	    echo " " ; \
	    echo "==> Please resolve differences between the repository and" ; \
	    echo "==> your rpm check out before tagging." ; \
	    echo " " ; \
	    cvs -n up ; \
	    echo " " ; \
	    exit 1 ; \
	}
	@cvs -d `cat CVS/Root` -Q tag -F $(CVSTAG) .
	@make dist
	@echo " "
	@echo "The final archive is ./rpm-$(VERSION).tar.gz."

.PHONY:	nextsrpm
nextsrpm: all archive
	@sudo ./rpm -ta rpm-$(VERSION).tar.gz

.PHONY:	doxygen
doxygen @WITH_APIDOCS_TARGET@: Doxyfile
	rm -rf $@
	mkdir -p $@
	- [ X"@__DOXYGEN@" != Xno ] && @__DOXYGEN@

ctags:
	find . -type f -name "*.[ch]*" | xargs ctags

cscope:
	cscope -b -R

cref: ctags cscope

ACLOCAL_AMFLAGS = -I m4
