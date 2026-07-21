# Makefile for rpm scripts.

AUTOMAKE_OPTIONS = 1.4 foreign

EXTRA_DIST = \
	posttrans-filetriggers 0ldconfig.filetrigger \
	functions .provides.sh \
	find-debuginfo-files process-debuginfo process-lto \
	brp-sign-kmodules \
	brp-adjust_libraries brp-alt \
	brp-check_contents brp-cleanup brp-compress brp-debuginfo \
	brp-dupe-bin \
	brp-fix-perms brp-fixup brp-strip-lto \
	brp-verify_elf brp-verify-info \
	brp-verify-unit \
	compress_files \
	find-lang \
	makecleanskiplist \
	rpmb-find-package \
	fixup-binconfig fixup-pkgconfig fixup-libtool fixup-libraries \
	fixup-desktop fixup-desktop.awk \
	is_elf_so_executable \
	ldd \
	percolate \
	provided_symbols \
	rpm2cpio.sh \
	tmpdir.sh \
	install strip \
	verify-elf

installprefix = $(DESTDIR)

all:

configdir = ${prefix}/lib/rpm
libexecdir = ${prefix}/libexec
config_SCRIPTS = \
	find-debuginfo-files process-debuginfo process-lto \
	brp-alt \
	compress_files \
	find-lang \
	makecleanskiplist \
	rpmb-find-package \
	fixup-binconfig fixup-pkgconfig fixup-libtool fixup-libraries \
	fixup-gnuconfig fixup-desktop fixup-desktop.awk \
	is_elf_so_executable \
	ldd \
	percolate \
	provided_symbols \
	rpm2cpio.sh \
	tmpdir.sh \
	verify-elf \
	rpmb-functions \
	# 0ldconfig.filetrigger posttrans-filetriggers

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(configdir)/brp.d
	$(mkinstalldirs) $(DESTDIR)$(libexecdir)/rpm-build
	$(INSTALL) -m755 brp-cleanup $(DESTDIR)$(configdir)/brp.d/008-cleanup.brp
	$(INSTALL) -m755 brp-fix-perms $(DESTDIR)$(configdir)/brp.d/016-fix-perms.brp
	$(INSTALL) -m755 brp-fixup $(DESTDIR)$(configdir)/brp.d/024-fixup.brp
	$(INSTALL) -m755 brp-check_contents $(DESTDIR)$(configdir)/brp.d/028-check_contents.brp
	$(INSTALL) -m755 brp-compress $(DESTDIR)$(configdir)/brp.d/032-compress.brp
	$(INSTALL) -m755 brp-verify-info $(DESTDIR)$(configdir)/brp.d/040-verify-info.brp
	$(INSTALL) -m755 brp-verify-unit $(DESTDIR)$(configdir)/brp.d/044-verify-unit.brp
	$(INSTALL) -m755 brp-adjust_libraries $(DESTDIR)$(configdir)/brp.d/048-adjust_libraries.brp
	$(INSTALL) -m755 brp-debuginfo $(DESTDIR)$(configdir)/brp.d/056-debuginfo.brp
	$(INSTALL) -m755 brp-sign-kmodules $(DESTDIR)$(configdir)/brp.d/057-brp-sign-kmodules.brp
	$(INSTALL) -m755 brp-strip-lto $(DESTDIR)$(configdir)/brp.d/061-brp-strip-lto.brp
	$(INSTALL) -m755 brp-dupe-bin $(DESTDIR)$(configdir)/brp.d/089-brp-dupe-bin.brp
	$(INSTALL) -m755 process-debuginfo $(DESTDIR)$(configdir)/process-debuginfo
	$(INSTALL) -m755 process-lto $(DESTDIR)$(configdir)/process-lto
	$(INSTALL) -m755 brp-verify_elf $(DESTDIR)$(configdir)/brp.d/064-verify_elf.brp
	$(INSTALL) -m755 install $(DESTDIR)$(libexecdir)/rpm-build/install
	$(INSTALL) -m755 strip $(DESTDIR)$(libexecdir)/rpm-build/strip
