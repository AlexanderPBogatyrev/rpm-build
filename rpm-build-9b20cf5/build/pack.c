/** \ingroup rpmbuild
 * \file build/pack.c
 *  Assemble components of an RPM package.
 */

#include "system.h"

#include "rpmio_internal.h"
#include "rpmbuild.h"
#include "buildio.h"

#include "misc.h"
#include "signature.h"
#include "rpmlead.h"
#include "debug.h"

/*@access StringBuf @*/	/* compared with NULL */
/*@access TFI_t @*/	/* compared with NULL */
/*@access Header @*/	/* compared with NULL */
/*@access FD_t @*/	/* compared with NULL */
/*@access CSA_t @*/

/**
 */
static inline int genSourceRpmName(Spec spec)
	/*@modifies spec->sourceRpmName @*/
{
    if (spec->sourceRpmName == NULL) {
	const char *name, *version, *release;
	char fileName[BUFSIZ];

	(void) headerNVRD(spec->packages->header, &name, &version, &release, NULL);
	sprintf(fileName, "%s-%s-%s.%ssrc.rpm", name, version, release,
	    spec->noSource ? "no" : "");
	spec->sourceRpmName = xstrdup(fileName);
    }

    return 0;
}

/**
 * @todo Create transaction set *much* earlier.
 */
static int cpio_doio(FD_t fdo, /*@unused@*/ Header h, CSA_t csa,
		const char * fmode)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies fdo, csa, rpmGlobalMacroContext, fileSystem @*/
{
    const char * rootDir = "/";
    rpmdb rpmdb = NULL;
    rpmTransactionSet ts = rpmtransCreateSet(rpmdb, rootDir);
    TFI_t fi = csa->cpioList;
    const char *failedFile = NULL;
    FD_t cfd;
    int rc, ec;

    {
	/*@-nullpass@*/
	(void) Fflush(fdo);
	cfd = Fdopen(fdDup(Fileno(fdo)), fmode);
	/*@=nullpass@*/
    }
    if (cfd == NULL)
	return 1;

    rc = fsmSetup(fi->fsm, FSM_PKGBUILD, ts, fi, cfd,
		&csa->cpioArchiveSize, &failedFile);
    (void) Fclose(cfd);
    ec = fsmTeardown(fi->fsm);
    if (!rc) rc = ec;

    if (rc) {
	if (failedFile)
	    rpmError(RPMERR_CPIO, _("create archive failed on file %s: %s\n"),
		failedFile, cpioStrerror(rc));
	else
	    rpmError(RPMERR_CPIO, _("create archive failed: %s\n"),
		cpioStrerror(rc));
      rc = 1;
    }

    failedFile = _free(failedFile);
    ts = rpmtransFree(ts);

    return rc;
}

/**
 */
static int cpio_copy(FD_t fdo, CSA_t csa)
	/*@globals fileSystem@*/
	/*@modifies fdo, csa, fileSystem @*/
{
    char buf[BUFSIZ];
    size_t nb;

    while((nb = Fread(buf, sizeof(buf[0]), sizeof(buf), csa->cpioFdIn)) > 0) {
	if (Fwrite(buf, sizeof(buf[0]), nb, fdo) != nb) {
	    rpmError(RPMERR_CPIO, _("cpio_copy write failed: %s\n"),
			Fstrerror(fdo));
	    return 1;
	}
	csa->cpioArchiveSize += nb;
    }
    if (Ferror(csa->cpioFdIn)) {
	rpmError(RPMERR_CPIO, _("cpio_copy read failed: %s\n"),
		Fstrerror(csa->cpioFdIn));
	return 1;
    }
    return 0;
}

/**
 */
static /*@only@*/ /*@null@*/ StringBuf addFileToTagAux(Spec spec,
		const char * file, /*@only@*/ StringBuf sb)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies rpmGlobalMacroContext, fileSystem @*/
{
    char buf[BUFSIZ];
    const char * fn = buf;
    FILE * f;
    FD_t fd;

    /* XXX use rpmGenPath(rootdir, "%{_buildir}/%{_buildsubdir}/", file) */
    fn = rpmGetPath("%{_builddir}/", spec->buildSubdir, "/", file, NULL);

    fd = Fopen(fn, "r.ufdio");
    if (fn != buf) fn = _free(fn);
    if (fd == NULL || Ferror(fd)) {
	sb = freeStringBuf(sb);
	return NULL;
    }
    /*@-type@*/ /* FIX: cast? */
    if ((f = fdGetFp(fd)) != NULL)
    /*@=type@*/
    while (fgets(buf, sizeof(buf), f)) {
	/* XXX display fn in error msg */
	if (expandMacros(spec, spec->macros, buf, sizeof(buf))) {
	    rpmError(RPMERR_BADSPEC, _("line: %s\n"), buf);
	    sb = freeStringBuf(sb);
	    break;
	}
	appendStringBuf(sb, buf);
    }
    (void) Fclose(fd);

    return sb;
}

/**
 */
static int addFileToTag(Spec spec, const char * file, Header h, int tag)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies h, rpmGlobalMacroContext, fileSystem @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    StringBuf sb = newStringBuf();
    char *s;

    if (hge(h, tag, NULL, (void **)&s, NULL)) {
	appendLineStringBuf(sb, s);
	(void) headerRemoveEntry(h, tag);
    }

    if ((sb = addFileToTagAux(spec, file, sb)) == NULL)
	return 1;
    
    (void) headerAddEntry(h, tag, RPM_STRING_TYPE, getStringBuf(sb), 1);

    sb = freeStringBuf(sb);
    return 0;
}

/**
 */
static int addFileToArrayTag(Spec spec, const char *file, Header h, int tag)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies h, rpmGlobalMacroContext, fileSystem @*/
{
    StringBuf sb = newStringBuf();
    char *s;

    if ((sb = addFileToTagAux(spec, file, sb)) == NULL)
	return 1;

    s = getStringBuf(sb);
    (void) headerAddOrAppendEntry(h, tag, RPM_STRING_ARRAY_TYPE, &s, 1);

    sb = freeStringBuf(sb);
    return 0;
}

/**
 */
static int processScriptFiles(Spec spec, Package pkg)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies pkg->header, rpmGlobalMacroContext, fileSystem @*/
{
    struct TriggerFileEntry *p;
    
    if (pkg->preInFile) {
	if (addFileToTag(spec, pkg->preInFile, pkg->header, RPMTAG_PREIN)) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open PreIn file: %s\n"), pkg->preInFile);
	    return RPMERR_BADFILENAME;
	}
    }
    if (pkg->preUnFile) {
	if (addFileToTag(spec, pkg->preUnFile, pkg->header, RPMTAG_PREUN)) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open PreUn file: %s\n"), pkg->preUnFile);
	    return RPMERR_BADFILENAME;
	}
    }
    if (pkg->postInFile) {
	if (addFileToTag(spec, pkg->postInFile, pkg->header, RPMTAG_POSTIN)) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open PostIn file: %s\n"), pkg->postInFile);
	    return RPMERR_BADFILENAME;
	}
    }
    if (pkg->postUnFile) {
	if (addFileToTag(spec, pkg->postUnFile, pkg->header, RPMTAG_POSTUN)) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open PostUn file: %s\n"), pkg->postUnFile);
	    return RPMERR_BADFILENAME;
	}
    }
    if (pkg->preTransFile) {
	if (addFileToTag(spec, pkg->preTransFile, pkg->header, RPMTAG_PRETRANS)) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open preTrans file: %s\n"), pkg->preTransFile);
	    return RPMERR_BADFILENAME;
	}
    }
    if (pkg->verifyFile) {
	if (addFileToTag(spec, pkg->verifyFile, pkg->header,
			 RPMTAG_VERIFYSCRIPT)) {
	    rpmError(RPMERR_BADFILENAME,
		     _("Could not open VerifyScript file: %s\n"), pkg->verifyFile);
	    return RPMERR_BADFILENAME;
	}
    }

    for (p = pkg->triggerFiles; p != NULL; p = p->next) {
	(void) headerAddOrAppendEntry(pkg->header, RPMTAG_TRIGGERSCRIPTPROG,
			       RPM_STRING_ARRAY_TYPE, &(p->prog), 1);
	if (p->script) {
	    (void) headerAddOrAppendEntry(pkg->header, RPMTAG_TRIGGERSCRIPTS,
				   RPM_STRING_ARRAY_TYPE, &(p->script), 1);
	} else if (p->fileName) {
	    if (addFileToArrayTag(spec, p->fileName, pkg->header,
				  RPMTAG_TRIGGERSCRIPTS)) {
		rpmError(RPMERR_BADFILENAME,
			 _("Could not open Trigger script file: %s\n"),
			 p->fileName);
		return RPMERR_BADFILENAME;
	    }
	} else {
	    /* This is dumb.  When the header supports NULL string */
	    /* this will go away.                                  */
	    char *bull = "";
	    (void) headerAddOrAppendEntry(pkg->header, RPMTAG_TRIGGERSCRIPTS,
				   RPM_STRING_ARRAY_TYPE, &bull, 1);
	}
    }

    return 0;
}

int readRPM(const char *fileName, Spec *specp, struct rpmlead *lead,
		Header *sigs, CSA_t csa)
{
    FD_t fdi;
    Spec spec;
    rpmRC rc;

    fdi = (fileName != NULL)
	? Fopen(fileName, "r.ufdio")
	: fdDup(STDIN_FILENO);

    if (fdi == NULL || Ferror(fdi)) {
	rpmError(RPMERR_BADMAGIC, _("readRPM: open %s: %s\n"),
		(fileName ? fileName : "<stdin>"),
		Fstrerror(fdi));
	if (fdi) (void) Fclose(fdi);
	return RPMERR_BADMAGIC;
    }

    /* Get copy of lead */
    /*@-sizeoftype@*/
    if ((rc = Fread(lead, sizeof(char), sizeof(*lead), fdi)) != sizeof(*lead)) {
	rpmError(RPMERR_BADMAGIC, _("readRPM: read %s: %s\n"),
		(fileName ? fileName : "<stdin>"),
		Fstrerror(fdi));
	return RPMERR_BADMAGIC;
    }
    /*@=sizeoftype@*/

    /* XXX FIXME: EPIPE on <stdin> */
    if (Fseek(fdi, 0, SEEK_SET) == -1) {
	rpmError(RPMERR_FSEEK, _("%s: Fseek failed: %s\n"),
			(fileName ? fileName : "<stdin>"), Fstrerror(fdi));
	return RPMERR_FSEEK;
    }

    /* Reallocate build data structures */
    spec = newSpec();
    spec->packages = newPackage(spec);

    /* XXX the header just allocated will be allocated again */
    spec->packages->header = headerFree(spec->packages->header);

   /* Read the rpm lead, signatures, and header */
    rc = rpmReadPackageInfo(fdi, sigs, &spec->packages->header);
    switch (rc) {
    case RPMRC_BADMAGIC:
	rpmError(RPMERR_BADMAGIC, _("readRPM: %s is not an RPM package\n"),
		(fileName ? fileName : "<stdin>"));
	return RPMERR_BADMAGIC;
    case RPMRC_OK:
	break;
    case RPMRC_FAIL:
    case RPMRC_BADSIZE:
    case RPMRC_SHORTREAD:
    default:
	rpmError(RPMERR_BADMAGIC, _("readRPM: reading header from %s\n"),
		(fileName ? fileName : "<stdin>"));
	return RPMERR_BADMAGIC;
	/*@notreached@*/ break;
    }

    /*@-branchstate@*/
    if (specp)
	*specp = spec;
    else
	spec = freeSpec(spec);
    /*@=branchstate@*/

    if (csa != NULL)
	csa->cpioFdIn = fdi;
    else
	(void) Fclose(fdi);

    return 0;
}

#if 0
/*@unchecked@*/
static unsigned char header_magic[8] = {
        0x8e, 0xad, 0xe8, 0x01, 0x00, 0x00, 0x00, 0x00
};
#endif

#define	RPMPKGVERSION_MIN	30004
#define	RPMPKGVERSION_MAX	40003
/*@unchecked@*/
static int rpmpkg_version = -1;

static int rpmLeadVersion(void)
	/*@globals rpmpkg_version, rpmGlobalMacroContext @*/
	/*@modifies rpmpkg_version, rpmGlobalMacroContext @*/
{
    int rpmlead_version;

    /* Intitialize packaging version from macro configuration. */
    if (rpmpkg_version < 0) {
	rpmpkg_version = rpmExpandNumeric("%{?_package_version}");
	if (rpmpkg_version < RPMPKGVERSION_MIN)
	    rpmpkg_version = RPMPKGVERSION_MIN;
	if (rpmpkg_version > RPMPKGVERSION_MAX)
	    rpmpkg_version = RPMPKGVERSION_MAX;
    }

    rpmlead_version = rpmpkg_version / 10000;
    if (rpmlead_version < 3 || rpmlead_version > 4)
	rpmlead_version = 3;
    return rpmlead_version;
}

// Estimate the uncompressed size of cpio archive before it is actually written.
static uint64_t calcArchiveSize(TFI_t fi)
{
    uint64_t size = 124; // cpio trailer
    for (int i = 0; i < fi->fc; i++) {
	if (fi->actions[i] == FA_SKIP) // %ghost
	    continue;
	size += 110; // cpio header
	size += strlen(fi->apath[i]) + 1;
	size = (size + 3) & ~(uint64_t)3;
	if (!S_ISREG(fi->fsts[i].st_mode) && !S_ISLNK(fi->fsts[i].st_mode))
	    continue;
	if (fi->fsts[i].st_nlink > 1) {
	    // Only the first hardlink occurrence counts.
	    int found = 0;
	    for (int j = i - 1; j >= 0; j--) {
		if (fi->actions[j] == FA_SKIP)
		    continue;
		if (fi->fsts[i].st_dev != fi->fsts[j].st_dev)
		    continue;
		if (fi->fsts[i].st_ino != fi->fsts[j].st_ino)
		    continue;
		found = 1;
		break;
	    }
	    if (found)
		continue;
	}
	// Valid for symlinks, target not null-terminated.
	size += fi->fsts[i].st_size;
	size = (size + 3) & ~(uint64_t)3;
    }
    return size;
}

// LZMA compressions levels 6-9 are equivalent, except for the dictionary size,
// which is 8-64M.  For smaller inputs, levels 7-9 are downgraded automatically.
static void downgradeLzmaLevel(char *mode, uint64_t archiveSize)
{
    if (!(mode[1] >= '0' && mode[1] <= '9'))
	return;
    char *p = &mode[2];
    if (*p == 'T') {
	do
	    p++;
	while (*p >= '0' && *p <= '9');
    }
    if (!(p[0] == '.' && (p[1] == 'l' || p[1] == 'x') && p[2] == 'z'))
	return;
#define S(m) ((m << 20) + (m << 10))
    // For small payloads, downgrade XZ->LZMA automatically.  XZ only makes
    // sense in multi-threaded mode (to speed up compression).  The default
    // block size in multi-threaded mode is three times the dictionary size.
    // There has to be at least two blocks, the second block not too small,
    // so we require at least five times the dictionary size.
#define T(m) ((m << 20) * 5)
#define ForceLzma memcpy(&mode[2], ".lzdio", 7)
    switch (mode[1]) {
    case '9':
	if (archiveSize < T(64)) ForceLzma;
	if (archiveSize > S(32)) break;
	mode[1] = '8';
	/*@fallthrough@*/
    case '8':
	if (archiveSize < T(32)) ForceLzma;
	if (archiveSize > S(16)) break;
	mode[1] = '7';
	/*@fallthrough@*/
    case '7':
	if (archiveSize < T(16)) ForceLzma;
	if (archiveSize > S(8)) break;
	mode[1] = '6';
	/*@fallthrough@*/
    case '6':
    case '5':
	if (archiveSize < T(8)) ForceLzma;
	break;
    case '4':
    case '3':
	if (archiveSize < T(4)) ForceLzma;
	break;
    case '2':
	if (archiveSize < T(2)) ForceLzma;
	break;
    case '1':
	if (archiveSize < T(1)) ForceLzma;
	break;
    case 0:
	// Dictionary size is 256K, but block size is 1M.
	if (archiveSize < (7<<18)) ForceLzma;
    }
#undef S
#undef T
}

static int haveTildeDep(Header h)
{
    HeaderIterator hi;
    int_32 tag, type, count;
    const void *ptr;
    int ret = 0;

    for (hi = headerInitIterator(h);
	 headerNextIterator(hi, &tag, &type, &ptr, &count);
	 ptr = headerFreeData(ptr, type))
    {
	switch (tag) {
	case RPMTAG_PROVIDEVERSION:
	case RPMTAG_REQUIREVERSION:
	case RPMTAG_OBSOLETEVERSION:
	case RPMTAG_CONFLICTVERSION:
	case RPMTAG_TRIGGERVERSION:
	    break;
	default:
	    continue;
	}
	if (type != RPM_STRING_ARRAY_TYPE)
	    continue;
	while (count--) {
	    const char *p = ((const char **)ptr)[count];

	    if (strchr(p, '~'))
		ret = 1;
	}
    }
    hi = headerFreeIterator(hi);
    return ret;
}

int writeRPM(Header *hdrp, const char *fileName, int type,
		    CSA_t csa, char *passPhrase, const char **cookie)
{
    FD_t fd = NULL;
    FD_t ifd = NULL;
    int count, sigtype;
    const char * sigtarget;
    const char * sha1 = NULL;
    const char *s;
    char buf[BUFSIZ];
    Header h;
    Header sig = NULL;
    int rc = 0;

    rpmioThreads = 0;

    uint64_t archiveSize = calcArchiveSize(csa->cpioList);
    if (archiveSize >= UINT32_MAX) { // sic, UINT32_MAX proper is kind of special
	rpmError(RPMERR_FWRITE, "cpio archive too big - %uM\n",
				 (unsigned)(archiveSize>>20));
	return RPMERR_FWRITE;
    }

    /* Transfer header reference form *hdrp to h. */
    h = headerLink(*hdrp);
    *hdrp = headerFree(*hdrp);

    if (Fileno(csa->cpioFdIn) < 0) {
	csa->cpioArchiveSize = 0;
	/* Add a bogus archive size to the Header */
	(void) headerAddEntry(h, RPMTAG_ARCHIVESIZE, RPM_INT32_TYPE,
		&csa->cpioArchiveSize, 1);
    }

    /* Binary packages now have explicit Provides: name = version-release. */
    if (type == RPMLEAD_BINARY)
	providePackageNVR(h);

    char *rpmio_flags = NULL;
    const char *N, *dash;

    /* Save payload information */
    /*@-branchstate@*/
    switch(type) {
    case RPMLEAD_SOURCE:
	rpmio_flags = rpmExpand("%{?_source_payload}", NULL);
	break;
    case RPMLEAD_BINARY:
	headerName(h, &N);
	dash = strrchr(N, '-');
	if (dash && strcmp(dash, "-debuginfo") == 0)
	    rpmio_flags = rpmExpand("%{?_debuginfo_payload}", NULL);
	if (!(rpmio_flags && *rpmio_flags))
	    rpmio_flags = rpmExpand("%{?_binary_payload}", NULL);
	break;
    }
    /*@=branchstate@*/
    if (!(rpmio_flags && *rpmio_flags == 'w')) {
	rpmio_flags = _free(rpmio_flags);
	rpmio_flags = xstrdup("w9.gzdio");
    }
    downgradeLzmaLevel(rpmio_flags, archiveSize);
    s = strchr(rpmio_flags, '.');
    if (s) {
	(void) headerAddEntry(h, RPMTAG_PAYLOADFORMAT, RPM_STRING_TYPE, "cpio", 1);
	if (s[1] == 'g' && s[2] == 'z')
	    (void) headerAddEntry(h, RPMTAG_PAYLOADCOMPRESSOR, RPM_STRING_TYPE,
		"gzip", 1);
#ifdef HAVE_BZLIB_H
	if (s[1] == 'b' && s[2] == 'z') {
	    (void) headerAddEntry(h, RPMTAG_PAYLOADCOMPRESSOR, RPM_STRING_TYPE,
		"bzip2", 1);
	    (void) rpmlibNeedsFeature(h, "PayloadIsBzip2", NULL);
	}
#endif
	if (s[1] == 'l' && s[2] == 'z') {
	    (void) headerAddEntry(h, RPMTAG_PAYLOADCOMPRESSOR, RPM_STRING_TYPE,
		"lzma", 1);
	    (void) rpmlibNeedsFeature(h, "PayloadIsLzma", NULL);
	}
	if (s[1] == 'x' && s[2] == 'z') {
	    (void) headerAddEntry(h, RPMTAG_PAYLOADCOMPRESSOR, RPM_STRING_TYPE,
		"xz", 1);
	    (void) rpmlibNeedsFeature(h, "PayloadIsXz", NULL);
	}
	strcpy(buf, rpmio_flags);
	buf[s - rpmio_flags] = '\0';
	(void) headerAddEntry(h, RPMTAG_PAYLOADFLAGS, RPM_STRING_TYPE, buf+1, 1);
    }

    /* check if the package has a dependency with a '~' */
    if (haveTildeDep(h))
	(void) rpmlibNeedsFeature(h, "TildeInVersions", "4.10.0-1");

    /* Create and add the cookie */
    if (cookie) {
	if (!headerGetEntry(h, RPMTAG_BUILDHOST, NULL, (void **)&s, NULL))
	    s = buildHost();
	sprintf(buf, "%s %d", s, (int) (*getBuildTime()));
	*cookie = xstrdup(buf);
	(void) headerAddEntry(h, RPMTAG_COOKIE, RPM_STRING_TYPE, *cookie, 1);
    }
    
    /* Reallocate the header into one contiguous region. */
    h = headerReload(h, RPMTAG_HEADERIMMUTABLE);
    if (h == NULL) {	/* XXX can't happen */
	rc = RPMERR_RELOAD;
	rpmError(RPMERR_RELOAD, _("Unable to create immutable header region.\n"));
	goto exit;
    }
    /* Re-reference reallocated header. */
    *hdrp = headerLink(h);

    /*
     * Write the header+archive into a temp file so that the size of
     * archive (after compression) can be added to the header.
     */
    if (makeTempFile(NULL, &sigtarget, &fd)) {
	rc = RPMERR_CREATE;
	rpmError(RPMERR_CREATE, _("Unable to open temp file.\n"));
	goto exit;
    }

    if (headerWrite(fd, h, HEADER_MAGIC_YES)) {
	rc = RPMERR_NOSPACE;
	rpmError(RPMERR_NOSPACE, _("Unable to write temp header\n"));
    } else { /* Write the archive and get the size */
	if (csa->cpioList != NULL) {
	    rc = cpio_doio(fd, h, csa, rpmio_flags);
	} else if (Fileno(csa->cpioFdIn) >= 0) {
	    rc = cpio_copy(fd, csa);
	} else {
	    rc = RPMERR_BADARG;
	    rpmError(RPMERR_BADARG, _("Bad CSA data\n"));
	}
    }

    if (rc)
	goto exit;

    /*
     * Set the actual archive size, and rewrite the header.
     * This used to be done using headerModifyEntry(), but now that headers
     * have regions, the value is scribbled directly into the header data
     * area. Some new scheme for adding the final archive size will have
     * to be devised if headerGetEntryMinMemory() ever changes to return
     * a pointer to memory not in the region, probably by appending
     * the archive size to the header region rather than including the
     * archive size within the header region.
     */
    if (Fileno(csa->cpioFdIn) < 0) {
	if (archiveSize != csa->cpioArchiveSize) {
	    rpmError(RPMERR_FWRITE, "wrong archive size: %" PRIu64 " -> %u\n",
				     archiveSize, csa->cpioArchiveSize);
	    rc = RPMERR_FWRITE;
	    goto exit;
	}
	HGE_t hge = (HGE_t)headerGetEntryMinMemory;
	uint32_t *sizep;
	if (hge(h, RPMTAG_ARCHIVESIZE, NULL, (void *)&sizep, NULL))
	    *sizep = csa->cpioArchiveSize;
	else {
	    rpmError(RPMERR_FWRITE, "cannot add RPMTAG_ARCHIVESIZE\n");
	    rc = RPMERR_FWRITE;
	    goto exit;
	}
    }

    (void) Fflush(fd);
    if (Fseek(fd, 0L, SEEK_SET) == -1) {
	rc = RPMERR_FSEEK;
	rpmError(RPMERR_FSEEK, _("%s: Fseek failed: %s\n"),
			sigtarget, Fstrerror(fd));
    }

    fdInitDigest(fd, PGPHASHALGO_SHA1, 0);
    if (headerWrite(fd, h, HEADER_MAGIC_YES)) {
	rc = RPMERR_NOSPACE;
	rpmError(RPMERR_NOSPACE, _("Unable to write final header\n"));
    }
    (void) Fflush(fd);
    fdFiniDigest(fd, PGPHASHALGO_SHA1, (void **)&sha1, NULL, 1);

    (void) Fclose(fd);
    fd = NULL;
    (void) Unlink(fileName);

    if (rc)
	goto exit;

    /* Generate the signature */
    (void) fflush(stdout);
    sig = rpmNewSignature();
    (void) rpmAddSignature(sig, sigtarget, RPMSIGTAG_SIZE, passPhrase);
    (void) rpmAddSignature(sig, sigtarget, RPMSIGTAG_MD5, passPhrase);
    if ((sigtype = rpmLookupSignatureType(RPMLOOKUPSIG_QUERY)) > 0) {
	rpmMessage(RPMMESS_NORMAL, _("Generating signature: %d\n"), sigtype);
	(void) rpmAddSignature(sig, sigtarget, sigtype, passPhrase);
    }
    
    if (sha1) {
	(void) headerAddEntry(sig, RPMTAG_SHA1HEADER, RPM_STRING_TYPE, sha1, 1);
	sha1 = _free(sha1);
    }

    /* Reallocate the signature into one contiguous region. */
    sig = headerReload(sig, RPMTAG_HEADERSIGNATURES);
    if (sig == NULL) {	/* XXX can't happen */
	rc = RPMERR_RELOAD;
	rpmError(RPMERR_RELOAD, _("Unable to reload signature header.\n"));
	goto exit;
    }

    /* Open the output file */
    fd = Fopen(fileName, "w.ufdio");
    if (fd == NULL || Ferror(fd)) {
	rc = RPMERR_CREATE;
	rpmError(RPMERR_CREATE, _("Could not open %s: %s\n"),
		fileName, Fstrerror(fd));
	goto exit;
    }

    /* Write the lead section into the package. */
    {	int archnum = -1;
	int osnum = -1;
	struct rpmlead lead;

	if (Fileno(csa->cpioFdIn) < 0) {
#ifndef	DYING
	    rpmGetArchInfo(NULL, &archnum);
	    rpmGetOsInfo(NULL, &osnum);
#endif
	} else if (csa->lead != NULL) {
	    archnum = csa->lead->archnum;
	    osnum = csa->lead->osnum;
	}

	memset(&lead, 0, sizeof(lead));
	lead.major = rpmLeadVersion();
	lead.minor = 0;
	lead.type = type;
	lead.archnum = archnum;
	lead.osnum = osnum;
	lead.signature_type = RPMSIGTYPE_HEADERSIG;

	{   const char *name, *version, *release;
	    (void) headerNVR(h, &name, &version, &release);
	    sprintf(buf, "%s-%s-%s", name, version, release);
	    strncpy(lead.name, buf, sizeof(lead.name));
	}

	if (writeLead(fd, &lead)) {
	    rc = RPMERR_NOSPACE;
	    rpmError(RPMERR_NOSPACE, _("Unable to write package: %s\n"),
		 Fstrerror(fd));
	    goto exit;
	}
    }

    /* Write the signature section into the package. */
    rc = rpmWriteSignature(fd, sig);
    if (rc)
	goto exit;

    /* Append the header and archive */
    ifd = Fopen(sigtarget, "r.ufdio");
    if (ifd == NULL || Ferror(ifd)) {
	rc = RPMERR_READ;
	rpmError(RPMERR_READ, _("Unable to open sigtarget %s: %s\n"),
		sigtarget, Fstrerror(ifd));
	goto exit;
    }

    /* Add signatures to header, and write header into the package. */
    {	Header nh = headerRead(ifd, HEADER_MAGIC_YES);

	if (nh == NULL) {
	    rc = RPMERR_READ;
	    rpmError(RPMERR_READ, _("Unable to read header from %s: %s\n"),
			sigtarget, Fstrerror(ifd));
	    goto exit;
	}

#ifdef	NOTYET
	(void) headerMergeLegacySigs(nh, sig);
#endif

	rc = headerWrite(fd, nh, HEADER_MAGIC_YES);
	nh = headerFree(nh);

	if (rc) {
	    rc = RPMERR_NOSPACE;
	    rpmError(RPMERR_NOSPACE, _("Unable to write header to %s: %s\n"),
			fileName, Fstrerror(fd));
	    goto exit;
	}
    }
	
    /* Write the payload into the package. */
    while ((count = Fread(buf, sizeof(buf[0]), sizeof(buf), ifd)) > 0) {
	if (count == -1) {
	    rc = RPMERR_READ;
	    rpmError(RPMERR_READ, _("Unable to read payload from %s: %s\n"),
		     sigtarget, Fstrerror(ifd));
	    goto exit;
	}
	if (Fwrite(buf, sizeof(buf[0]), count, fd) != count) {
	    rc = RPMERR_NOSPACE;
	    rpmError(RPMERR_NOSPACE, _("Unable to write payload to %s: %s\n"),
		     fileName, Fstrerror(fd));
	    goto exit;
	}
    }
    rc = 0;

exit:
    sha1 = _free(sha1);
    h = headerFree(h);
    sig = rpmFreeSignature(sig);
    if (ifd) {
	(void) Fclose(ifd);
	ifd = NULL;
    }
    if (fd) {
	(void) Fclose(fd);
	fd = NULL;
    }
    if (sigtarget) {
	(void) Unlink(sigtarget);
	sigtarget = _free(sigtarget);
    }

    /* Expand rpmio_flags for an actual 'T' value. */
    if (rpmioThreads) {
	char *src_flags = xstrdup(rpmio_flags);
	char *psrc = src_flags;
#define SIZE_SPRINTF_U (sizeof(unsigned int) * 3)
	rpmio_flags = xrealloc(rpmio_flags, strlen(rpmio_flags) + 1 + SIZE_SPRINTF_U);
	char *pdst = rpmio_flags;
	while (*psrc) {
	    if (*psrc == 'T') {
		int n = snprintf(pdst, SIZE_SPRINTF_U + 1, "T%u", rpmioThreads);
		assert(n < SIZE_SPRINTF_U + 1);
		pdst += n;
		while (isdigit(*++psrc));
		break; /* Only can expand once. */
	    } else
		*pdst++ = *psrc++;
	}
	while (*psrc)
	    *pdst++ = *psrc++;
	*pdst = '\0';
	free(src_flags);
    }

    if (rc == 0)
	rpmMessage(RPMMESS_NORMAL, _("Wrote: %s (%s)\n"),
		   fileName, rpmio_flags);
    else
	(void) Unlink(fileName);

    rpmio_flags = _free(rpmio_flags);
    return rc;
}

/*@unchecked@*/
static int_32 copyTags[] = {
    RPMTAG_CHANGELOGTIME,
    RPMTAG_CHANGELOGNAME,
    RPMTAG_CHANGELOGTEXT,
    0
};

int packageBinaries(Spec spec)
{
    struct cpioSourceArchive_s csabuf;
    CSA_t csa = &csabuf;
    int rc;
    const char *errorString;
    Package pkg;

    for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
	const char *fn;

	if (pkg->fileList == NULL)
	    continue;

	if ((rc = processScriptFiles(spec, pkg)))
	    return rc;
	
	if (spec->cookie) {
	    (void) headerAddEntry(pkg->header, RPMTAG_COOKIE,
			   RPM_STRING_TYPE, spec->cookie, 1);
	}

	/* Copy changelog from src rpm */
	headerCopyTags(spec->packages->header, pkg->header, copyTags);
	
	(void) headerAddEntry(pkg->header, RPMTAG_RPMVERSION,
		       RPM_STRING_TYPE, VERSION, 1);
	if (!headerIsEntry(pkg->header, RPMTAG_BUILDHOST))
	    (void) headerAddEntry(pkg->header, RPMTAG_BUILDHOST,
		       RPM_STRING_TYPE, buildHost(), 1);
	(void) headerAddEntry(pkg->header, RPMTAG_BUILDTIME,
		       RPM_INT32_TYPE, getBuildTime(), 1);

	providePackageNVR(pkg->header);

    {	const char * optflags = rpmExpand("%{?optflags}", NULL);
	(void) headerAddEntry(pkg->header, RPMTAG_OPTFLAGS, RPM_STRING_TYPE,
			optflags, 1);
	optflags = _free(optflags);
    }

	(void) genSourceRpmName(spec);
	(void) headerAddEntry(pkg->header, RPMTAG_SOURCERPM, RPM_STRING_TYPE,
		       spec->sourceRpmName, 1);
	
	{   const char *binFormat = rpmGetPath("%{_rpmfilename}", NULL);
	    char *binRpm, *binDir;
	    binRpm = headerSprintf(pkg->header, binFormat, rpmTagTable,
			       rpmHeaderFormats, &errorString);
	    binFormat = _free(binFormat);
	    if (binRpm == NULL) {
		const char *name;
		(void) headerName(pkg->header, &name);
		rpmError(RPMERR_BADFILENAME, _("Could not generate output "
		     "filename for package %s: %s\n"), name, errorString);
		return RPMERR_BADFILENAME;
	    }
	    fn = rpmGetPath("%{_rpmdir}/", binRpm, NULL);
	    if ((binDir = strchr(binRpm, '/')) != NULL) {
		struct stat st;
		const char *dn;
		*binDir = '\0';
		dn = rpmGetPath("%{_rpmdir}/", binRpm, NULL);
		if (Stat(dn, &st) < 0) {
		    switch(errno) {
		    case  ENOENT:
			if (MkdirP(dn, 0755) == 0)
			    /*@switchbreak@*/ break;
			/*@fallthrough@*/
		    default:
			rpmError(RPMERR_BADFILENAME,_("cannot create %s: %s\n"),
			    dn, strerror(errno));
			/*@switchbreak@*/ break;
		    }
		}
		dn = _free(dn);
	    }
	    binRpm = _free(binRpm);
	}

	memset(csa, 0, sizeof(*csa));
	csa->cpioArchiveSize = 0;
	/*@-type@*/ /* LCL: function typedefs */
	csa->cpioFdIn = fdNew("init (packageBinaries)");
	/*@-assignexpose -newreftrans@*/
/*@i@*/	csa->cpioList = pkg->cpioList;
	/*@=assignexpose =newreftrans@*/

	rc = writeRPM(&pkg->header, fn, RPMLEAD_BINARY,
		    csa, spec->passPhrase, NULL);
	csa->cpioFdIn = fdFree(csa->cpioFdIn, "init (packageBinaries)");
	/*@=type@*/
	fn = _free(fn);
	if (rc)
	    return rc;
    }
    
    return 0;
}

int packageSources(Spec spec)
{
    struct cpioSourceArchive_s csabuf;
    CSA_t csa = &csabuf;
    int rc;
    time_t buildtime;
    int_32 buildtime32;
    int_32 *override_buildtime = NULL;

    const char *env_buildtime = getenv("SOURCE_DATE_EPOCH");

    /* Add some cruft */
    (void) headerAddEntry(spec->sourceHeader, RPMTAG_RPMVERSION,
		   RPM_STRING_TYPE, VERSION, 1);
    if (!headerIsEntry(spec->sourceHeader, RPMTAG_BUILDHOST))
        (void) headerAddEntry(spec->sourceHeader, RPMTAG_BUILDHOST,
		   RPM_STRING_TYPE, buildHost(), 1);

    if (env_buildtime && *env_buildtime) {
	char *endptr;
	errno = 0;
	buildtime = strtol(env_buildtime, &endptr, 10);
	if (env_buildtime == endptr || *endptr
	    || ((buildtime == LONG_MIN || buildtime == LONG_MAX)
		&& errno != 0)) {
	    rpmlog(RPMLOG_ERR, _("unable to parse %s=%s\n"),
		   "SOURCE_DATE_EPOCH", env_buildtime);
	} else {
	    buildtime32 = (typeof(buildtime32)) buildtime;
	    override_buildtime = &buildtime32;
	}
    }

    (void) headerAddEntry(spec->sourceHeader, RPMTAG_BUILDTIME,
		   RPM_INT32_TYPE, override_buildtime ?: getBuildTime() , 1);

    (void) genSourceRpmName(spec);

    spec->cookie = _free(spec->cookie);
    
    /* XXX this should be %_srpmdir */
    {
	const char *fn = rpmGetPath("%{_srcrpmdir}/", spec->sourceRpmName, NULL);
	{
		const char *dn = rpmGetPath("%{_srcrpmdir}/", NULL);
		struct stat st;

		if (Stat(dn, &st) < 0) {
		    switch(errno) {
		    case  ENOENT:
			if (MkdirP(dn, 0755) == 0)
			    /*@switchbreak@*/ break;
			/*@fallthrough@*/
		    default:
			rpmError(RPMERR_BADFILENAME,_("cannot create %s: %s\n"),
			    dn, strerror(errno));
			/*@switchbreak@*/ break;
		    }
		}
		dn = _free(dn);
	}
	memset(csa, 0, sizeof(*csa));
	csa->cpioArchiveSize = 0;
	/*@-type@*/ /* LCL: function typedefs */
	csa->cpioFdIn = fdNew("init (packageSources)");
	/*@-assignexpose -newreftrans@*/
/*@i@*/	csa->cpioList = spec->sourceCpioList;
	/*@=assignexpose =newreftrans@*/

	rc = writeRPM(&spec->sourceHeader, fn, RPMLEAD_SOURCE,
		csa, spec->passPhrase, &(spec->cookie));
	csa->cpioFdIn = fdFree(csa->cpioFdIn, "init (packageSources)");
	/*@=type@*/
	fn = _free(fn);
    }
    return rc;
}
