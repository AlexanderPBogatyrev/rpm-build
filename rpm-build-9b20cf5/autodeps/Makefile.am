# Makefile for autodep generators.

AUTOMAKE_OPTIONS = 1.4 foreign

all:

configdir = ${prefix}/lib/rpm
config_DATA = 0common-files.req.list # .provides.sh
config_SCRIPTS = \
	autodep-functions \
	find-provides \
	find-requires \
	find-scriptlet-requires \
	cpp.req cpp.req.files \
	debuginfo.prov debuginfo.prov.files debuginfo.req debuginfo.req.files \
	files.req files.req.files \
	lib.prov lib.prov.files lib.req lib.req.files shlib.req.awk \
	pam.req pam.req.files pam.prov pam.prov.files \
	pkgconfig.req pkgconfig.req.files pkgconfiglib.req \
	rpmlib.req rpmlib.req.files \
	shebang.req shebang.req.files \
	shell.prov shell.prov.files shell.req shell.req.files \
	static.req static.req.files \
	symlinks.req symlinks.req.files \
	systemd-services.req systemd-services.req.files \
	sysusers.prov sysusers.prov.files \
	#

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(configdir)
	@LN_S@ pkgconfig.req $(DESTDIR)$(configdir)/pkgconfig.prov
	@LN_S@ pkgconfig.req.files $(DESTDIR)$(configdir)/pkgconfig.prov.files
	@LN_S@ pkgconfig.req.files $(DESTDIR)$(configdir)/pkgconfiglib.req.files
