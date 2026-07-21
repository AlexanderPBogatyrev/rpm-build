/** \ingroup rpmtrans payload
 * \file lib/psm.c
 * Package state machine to handle a package from a transaction set.
 */

#include "system.h"

#include <signal.h>

#include "psm.h"
#include "rpmmacro.h"
#include "rpmurl.h"

#include "depends.h"

#include "rpmlead.h"		/* writeLead proto */
#include "signature.h"		/* signature constants */
#include "ugid.h"
#include "misc.h"
#include "rpmcli.h"		/* HACK for rpmShowProgress */
#include "db.h"			/* HACK for relock */
#include "rpmdb.h"		/* XXX for db_chrootDone */
#include "debug.h"

/*@access Header @*/		/* compared with NULL */
/*@access rpmTransactionSet @*/	/* compared with NULL */
/*@access rpmdbMatchIterator @*/ /* compared with NULL */
/*@access TFI_t @*/		/* compared with NULL */
/*@access FSM_t @*/		/* compared with NULL */
/*@access PSM_t @*/		/* compared with NULL */
/*@access FD_t @*/		/* compared with NULL */
/*@access rpmdb @*/		/* compared with NULL */

#ifdef	DYING
/*@-redecl -declundef -exportheadervar@*/
extern const char * chroot_prefix;
/*@=redecl =declundef =exportheadervar@*/
#endif

static int upgrade_honor_buildtime(void)
{
    static int honor_buildtime = -1;

    if (honor_buildtime < 0)
	honor_buildtime = rpmExpandNumeric("%{?_upgrade_honor_buildtime}%{?!_upgrade_honor_buildtime:1}") ? 1 : 0;

    return honor_buildtime;
}

static int rpm_cmp_tag_int(Header first, Header second, rpmTag tag)
{
    int_32 * one, * two;

    if (!headerGetEntry(first, tag, NULL, (void **) &one, NULL))
	one = NULL;
    if (!headerGetEntry(second, tag, NULL, (void **) &two, NULL))
	two = NULL;

    if (!one && !two)
	return 0;
    else if (!one && two)
	return -1;
    else if (one && !two)
	return 1;
    else if (*one < *two)
	return -1;
    else if (*one > *two)
	return 1;
    else
	return 0;
}

static int rpm_cmp_tag_version(Header first, Header second, rpmTag tag)
{
    const char * one, * two;

    if (!headerGetEntry(first, tag, NULL, (void **) &one, NULL))
	one = NULL;
    if (!headerGetEntry(second, tag, NULL, (void **) &two, NULL))
	two = NULL;

    if (!one && !two)
	return 0;
    else if (!one && two)
	return -1;
    else if (one && !two)
	return 1;
    else
	return rpmvercmp(one, two);
}

int rpmVersionCompare(Header first, Header second)
{
    int rc;

    if ((rc = rpm_cmp_tag_int(first, second, RPMTAG_EPOCH)))
	return rc;

    if ((rc = rpm_cmp_tag_version(first, second, RPMTAG_VERSION)))
	return rc;

    if ((rc = rpm_cmp_tag_version(first, second, RPMTAG_RELEASE)))
	return rc;

    /* hack to make packages upgrade between branches possible */
    const char * fdt;
    const char * sdt;

    if (!headerGetEntry(first, RPMTAG_DISTTAG, NULL, (void **) &fdt, NULL))
	fdt = NULL;
    if (!headerGetEntry(second, RPMTAG_DISTTAG, NULL, (void **) &sdt, NULL))
	sdt = NULL;

    if (fdt && sdt) {
	size_t flen = 0, slen = 0;
	const char * p;

	if ((p = strchrnul(fdt, '+')))
	    flen = p - fdt;

	if ((p = strchrnul(sdt, '+')))
	    slen = p - sdt;

	if (flen < slen) {
	    rc = memcmp(fdt, sdt, flen) > 0 ? 1 : -1;
	} else if (flen > slen) {
	    rc = memcmp(fdt, sdt, slen) < 0 ? -1 : 1;
	} else /* flen == slen */ {
	    rc = memcmp(fdt, sdt, flen);
	}

	if (rc) {
	    if (rc > 0)
		return 1;
	    else
		return -1;
	}
    }

    if (upgrade_honor_buildtime())
	return rpm_cmp_tag_int(first, second, RPMTAG_BUILDTIME);

    return 0;
}

void loadFi(Header h, TFI_t fi)
{
    HGE_t hge;
    HFD_t hfd;
    uint_32 * uip;
    char * strp;
    int len;
    int rc;
    int i;
    
    if (fi->fsm == NULL)
	fi->fsm = newFSM();

    /* XXX avoid gcc noise on pointer (4th arg) cast(s) */
    hge = (fi->type == TR_ADDED)
	? (HGE_t) headerGetEntryMinMemory : (HGE_t) headerGetEntry;
    fi->hge = hge;
    fi->hae = (HAE_t) headerAddEntry;
    fi->hme = (HME_t) headerModifyEntry;
    fi->hre = (HRE_t) headerRemoveEntry;
    fi->hfd = hfd = headerFreeData;

    if (h && fi->h == NULL)	fi->h = headerLink(h);

    /* Duplicate name-version-release so that headers can be free'd. */
    strp = NULL;
    rc = hge(fi->h, RPMTAG_NAME, NULL, (void **) &strp, NULL);
    fi->name = strp ? xstrdup(strp) : NULL;
    strp = NULL;
    rc = hge(fi->h, RPMTAG_VERSION, NULL, (void **) &strp, NULL);
    fi->version = strp ? xstrdup(strp) : NULL;
    strp = NULL;
    rc = hge(fi->h, RPMTAG_RELEASE, NULL, (void **) &strp, NULL);
    fi->release = strp ? xstrdup(strp) : NULL;
    strp = NULL;
    rc = hge(fi->h, RPMTAG_SHA1HEADER, NULL, (void **) &strp, NULL);
    fi->digest = strp ? xstrdup(strp) : NULL;

    /* -1 means not found */
    rc = hge(fi->h, RPMTAG_EPOCH, NULL, (void **) &uip, NULL);
    fi->epoch = (rc ? *uip : -1);
    /* 0 means unknown */
    rc = hge(fi->h, RPMTAG_ARCHIVESIZE, NULL, (void **) &uip, NULL);
    fi->archiveSize = (rc ? *uip : 0);

    if (!hge(fi->h, RPMTAG_BASENAMES, NULL, (void **) &fi->bnl, &fi->fc)) {
	fi->dc = 0;
	fi->fc = 0;
	return;
    }

    rc = hge(fi->h, RPMTAG_DIRINDEXES, NULL, (void **) &fi->dil, NULL);
    rc = hge(fi->h, RPMTAG_DIRNAMES, NULL, (void **) &fi->dnl, &fi->dc);
    rc = hge(fi->h, RPMTAG_FILEMODES, NULL, (void **) &fi->fmodes, NULL);
    rc = hge(fi->h, RPMTAG_FILEFLAGS, NULL, (void **) &fi->fflags, NULL);
    rc = hge(fi->h, RPMTAG_FILESIZES, NULL, (void **) &fi->fsizes, NULL);
    rc = hge(fi->h, RPMTAG_FILESTATES, NULL, (void **) &fi->fstates, NULL);

    fi->action = FA_UNKNOWN;
    fi->flags = 0;

    /* actions is initialized earlier for added packages */
    if (fi->actions == NULL)
	fi->actions = xcalloc(fi->fc, sizeof(*fi->actions));

    switch (fi->type) {
    case TR_ADDED:
	fi->mapflags =
		CPIO_MAP_PATH | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID;
	rc = hge(fi->h, RPMTAG_FILEMD5S, NULL, (void **) &fi->fmd5s, NULL);
	rc = hge(fi->h, RPMTAG_FILELINKTOS, NULL, (void **) &fi->flinks, NULL);
	rc = hge(fi->h, RPMTAG_FILELANGS, NULL, (void **) &fi->flangs, NULL);
	rc = hge(fi->h, RPMTAG_FILEMTIMES, NULL, (void **) &fi->fmtimes, NULL);
	rc = hge(fi->h, RPMTAG_FILERDEVS, NULL, (void **) &fi->frdevs, NULL);

	rc = hge(fi->h, RPMTAG_FILEUSERNAME, NULL, (void **) &fi->fuser, NULL);
	rc = hge(fi->h, RPMTAG_FILEGROUPNAME, NULL, (void **) &fi->fgroup, NULL);

	/* 0 makes for noops */
	fi->replacedSizes = xcalloc(fi->fc, sizeof(*fi->replacedSizes));

	break;
    case TR_REMOVED:
	fi->mapflags = 
		CPIO_MAP_ABSOLUTE | CPIO_MAP_ADDDOT | CPIO_ALL_HARDLINKS |
		CPIO_MAP_PATH | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID;
	rc = hge(fi->h, RPMTAG_FILEMD5S, NULL, (void **) &fi->fmd5s, NULL);
	rc = hge(fi->h, RPMTAG_FILELINKTOS, NULL, (void **) &fi->flinks, NULL);
	fi->fsizes = memcpy(xmalloc(fi->fc * sizeof(*fi->fsizes)),
				fi->fsizes, fi->fc * sizeof(*fi->fsizes));
	fi->fflags = memcpy(xmalloc(fi->fc * sizeof(*fi->fflags)),
				fi->fflags, fi->fc * sizeof(*fi->fflags));
	fi->fmodes = memcpy(xmalloc(fi->fc * sizeof(*fi->fmodes)),
				fi->fmodes, fi->fc * sizeof(*fi->fmodes));
	/* XXX there's a tedious segfault here for some version(s) of rpm */
	if (fi->fstates)
	    fi->fstates = memcpy(xmalloc(fi->fc * sizeof(*fi->fstates)),
				fi->fstates, fi->fc * sizeof(*fi->fstates));
	else
	    fi->fstates = xcalloc(1, fi->fc * sizeof(*fi->fstates));
	fi->dil = memcpy(xmalloc(fi->fc * sizeof(*fi->dil)),
				fi->dil, fi->fc * sizeof(*fi->dil));
	fi->h = headerFree(fi->h);
	break;
    }

    fi->dnlmax = -1;
    for (i = 0; i < fi->dc; i++) {
	if ((len = strlen(fi->dnl[i])) > fi->dnlmax)
	    fi->dnlmax = len;
    }

    fi->bnlmax = -1;
    for (i = 0; i < fi->fc; i++) {
	if ((len = strlen(fi->bnl[i])) > fi->bnlmax)
	    fi->bnlmax = len;
    }

    fi->dperms = 0755;
    fi->fperms = 0644;

    /*@-nullstate@*/	/* FIX: fi->h is NULL for TR_REMOVED */
    return;
    /*@=nullstate@*/
}

TFI_t freeFi(TFI_t fi)
{
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);

    fi->name = _free(fi->name);
    fi->version = _free(fi->version);
    fi->release = _free(fi->release);
    fi->digest = _free(fi->digest);
    fi->actions = _free(fi->actions);
    fi->replacedSizes = _free(fi->replacedSizes);
    fi->replaced = _free(fi->replaced);

    fi->bnl = hfd(fi->bnl, -1);
    fi->dnl = hfd(fi->dnl, -1);
    fi->obnl = hfd(fi->obnl, -1);
    fi->odnl = hfd(fi->odnl, -1);
    fi->flinks = hfd(fi->flinks, -1);
    fi->fmd5s = hfd(fi->fmd5s, -1);
    fi->fuser = hfd(fi->fuser, -1);
    fi->fgroup = hfd(fi->fgroup, -1);
    fi->flangs = hfd(fi->flangs, -1);

    fi->apath = _free(fi->apath);
    fi->fuids = _free(fi->fuids);
    fi->fgids = _free(fi->fgids);
    fi->fmapflags = _free(fi->fmapflags);
    fi->fsts = _free(fi->fsts);

    fi->fsm = freeFSM(fi->fsm);

    switch (fi->type) {
    case TR_ADDED:
	    break;
    case TR_REMOVED:
	fi->fsizes = hfd(fi->fsizes, -1);
	fi->fflags = hfd(fi->fflags, -1);
	fi->fmodes = hfd(fi->fmodes, -1);
	fi->fstates = hfd(fi->fstates, -1);
	fi->dil = hfd(fi->dil, -1);
	break;
    }

    fi->h = headerFree(fi->h);

    return NULL;
}

/*@observer@*/ const char * fiTypeString(TFI_t fi)
{
    switch(fi->type) {
    case TR_ADDED:	return " install";
    case TR_REMOVED:	return "   erase";
    default:		return "???";
    }
    /*@noteached@*/
}

/**
 * Macros to be defined from per-header tag values.
 * @todo Should other macros be added from header when installing a package?
 */
/*@observer@*/ /*@unchecked@*/
static struct tagMacro {
/*@observer@*/ /*@null@*/ const char *	macroname; /*!< Macro name to define. */
    rpmTag	tag;		/*!< Header tag to use for value. */
} tagMacros[] = {
    { "name",		RPMTAG_NAME },
    { "version",	RPMTAG_VERSION },
    { "release",	RPMTAG_RELEASE },
    { "epoch",		RPMTAG_EPOCH },
    { NULL, 0 }
};

/**
 * Define per-header macros.
 * @param fi		transaction element file info
 * @param h		header
 * @return		0 always
 */
static int rpmInstallLoadMacros(TFI_t fi, Header h)
	/*@globals rpmGlobalMacroContext, internalState @*/
	/*@modifies rpmGlobalMacroContext, internalState @*/
{
    HGE_t hge = (HGE_t) fi->hge;
    struct tagMacro * tagm;
    union {
/*@unused@*/ void * ptr;
/*@unused@*/ const char ** argv;
	const char * str;
	int_32 * i32p;
    } body;
    char numbuf[32];
    rpmTagType type;

    for (tagm = tagMacros; tagm->macroname != NULL; tagm++) {
	if (!hge(h, tagm->tag, &type, (void **) &body, NULL))
	    continue;
	switch (type) {
	case RPM_INT32_TYPE:
	    sprintf(numbuf, "%d", *body.i32p);
	    addMacro(NULL, tagm->macroname, NULL, numbuf, -1);
	    /*@switchbreak@*/ break;
	case RPM_STRING_TYPE:
	    addMacro(NULL, tagm->macroname, NULL, body.str, -1);
	    /*@switchbreak@*/ break;
	case RPM_NULL_TYPE:
	case RPM_CHAR_TYPE:
	case RPM_INT8_TYPE:
	case RPM_INT16_TYPE:
	case RPM_BIN_TYPE:
	case RPM_STRING_ARRAY_TYPE:
	case RPM_I18NSTRING_TYPE:
	default:
	    /*@switchbreak@*/ break;
	}
    }
    return 0;
}

/**
 * Mark files in database shared with this package as "replaced".
 * @param psm		package state machine data
 * @return		0 always
 */
static int markReplacedFiles(PSM_t psm)
	/*@globals fileSystem @*/
	/*@modifies psm, fileSystem @*/
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    HGE_t hge = (HGE_t)fi->hge;
    const struct sharedFileInfo * replaced = fi->replaced;
    const struct sharedFileInfo * sfi;
    rpmdbMatchIterator mi;
    Header h;
    unsigned int * offsets;
    unsigned int prev;
    int num, xx;

    if (!(fi->fc > 0 && fi->replaced))
	return 0;

    num = prev = 0;
    for (sfi = replaced; sfi->otherPkg; sfi++) {
	if (prev && prev == sfi->otherPkg)
	    continue;
	prev = sfi->otherPkg;
	num++;
    }
    if (num == 0)
	return 0;

    offsets = alloca(num * sizeof(*offsets));
    num = prev = 0;
    for (sfi = replaced; sfi->otherPkg; sfi++) {
	if (prev && prev == sfi->otherPkg)
	    continue;
	prev = sfi->otherPkg;
	offsets[num++] = sfi->otherPkg;
    }

    mi = rpmdbInitIterator(ts->rpmdb, RPMDBI_PACKAGES, NULL, 0);
    xx = rpmdbAppendIterator(mi, offsets, num);
    xx = rpmdbSetIteratorRewrite(mi, 1);

    sfi = replaced;
    while ((h = rpmdbNextIterator(mi)) != NULL) {
	char * secStates;
	int modified;
	int count;

	modified = 0;

	if (!hge(h, RPMTAG_FILESTATES, NULL, (void **)&secStates, &count))
	    continue;
	
	prev = rpmdbGetIteratorOffset(mi);
	num = 0;
	while (sfi->otherPkg && sfi->otherPkg == prev) {
	    assert(sfi->otherFileNum < count);
	    if (secStates[sfi->otherFileNum] != RPMFILE_STATE_REPLACED) {
		secStates[sfi->otherFileNum] = RPMFILE_STATE_REPLACED;
		if (modified == 0) {
		    /* Modified header will be rewritten. */
		    modified = 1;
		    xx = rpmdbSetIteratorModified(mi, modified);
		}
		num++;
	    }
	    sfi++;
	}
    }
    mi = rpmdbFreeIterator(mi);

    return 0;
}

/**
 * Create directory if it does not exist, make sure path is writable.
 * @note This will try to create all necessary components of directory path.
 * @param dpath		directory path
 * @param dname		directory use
 * @return		rpmRC return code
 */
static rpmRC chkdir (const char * dpath, const char * dname)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    struct stat st;
    int rc;

    if ((rc = Stat(dpath, &st)) < 0) {
	int ut = urlPath(dpath, NULL);
	switch (ut) {
	case URL_IS_PATH:
	case URL_IS_UNKNOWN:
	    if (errno != ENOENT)
		break;
	    /*@fallthrough@*/
	case URL_IS_FTP:
	case URL_IS_HTTP:
	    rc = MkdirP(dpath, 0755);
	    break;
	case URL_IS_DASH:
	    break;
	}
	if (rc < 0) {
	    rpmError(RPMERR_CREATE, _("cannot create %%%s %s\n"),
			dname, dpath);
	    return RPMRC_FAIL;
	}
    }
    if ((rc = Access(dpath, W_OK))) {
	rpmError(RPMERR_CREATE, _("cannot write to %%%s %s\n"), dname, dpath);
	return RPMRC_FAIL;
    }
    return RPMRC_OK;
}

rpmRC rpmInstallSourcePackage(const char * rootDir, FD_t fd,
			const char ** specFilePtr,
			rpmCallbackFunction notify, rpmCallbackData notifyData,
			char ** cookie)
{
    rpmdb rpmdb = NULL;
    rpmTransactionSet ts = rpmtransCreateSet(rpmdb, rootDir);
    TFI_t fi = xcalloc(sizeof(*fi), 1);
    const char * _sourcedir = NULL;
    const char * _specdir = NULL;
    const char * specFile = NULL;
    HGE_t hge;
    HFD_t hfd;
    Header h = NULL;
    struct psm_s psmbuf;
    PSM_t psm = &psmbuf;
    int isSource;
    rpmRC rc;
    int i;

    ts->notify = notify;
    /*@-temptrans -assignexpose@*/
    ts->notifyData = notifyData;
    /*@=temptrans =assignexpose@*/

    /*@-mustmod@*/	/* LCL: segfault */
    rc = rpmReadPackageHeader(fd, &h, &isSource, NULL, NULL);
    /*@=mustmod@*/
    if (rc)
	goto exit;

    if (!isSource) {
	rpmError(RPMERR_NOTSRPM, _("source package expected, binary found\n"));
	rc = RPMRC_FAIL;
	goto exit;
    }

    (void) rpmtransAddPackage(ts, h, fd, NULL, 0, NULL);
    if (ts->addedPackages.list == NULL) {	/* XXX can't happen */
	rc = RPMRC_FAIL;
	goto exit;
    }

    fi->type = TR_ADDED;
    fi->ap = ts->addedPackages.list;
    loadFi(h, fi);
    hge = fi->hge;
    hfd = (fi->hfd ? fi->hfd : headerFreeData);
    h = headerFree(h);	/* XXX reference held by transaction set */

    (void) rpmInstallLoadMacros(fi, fi->h);

    memset(psm, 0, sizeof(*psm));
    psm->ts = ts;
    psm->fi = fi;

    if (cookie) {
	*cookie = NULL;
	if (hge(fi->h, RPMTAG_COOKIE, NULL, (void **) cookie, NULL))
	    *cookie = xstrdup(*cookie);
    }

    /* XXX FIXME: can't do endian neutral MD5 verification yet. */
    fi->fmd5s = hfd(fi->fmd5s, -1);

    /* XXX FIXME: don't do per-file mapping, force global flags. */
    fi->fmapflags = _free(fi->fmapflags);
    fi->mapflags = CPIO_MAP_PATH | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID;

    fi->uid = getuid();
    fi->gid = getgid();
    fi->astriplen = 0;
    fi->striplen = 0;

    fi->fuids = xcalloc(sizeof(*fi->fuids), fi->fc);
    fi->fgids = xcalloc(sizeof(*fi->fgids), fi->fc);
    for (i = 0; i < fi->fc; i++) {
	fi->fuids[i] = fi->uid;
	fi->fgids[i] = fi->gid;
    }

    for (i = 0; i < fi->fc; i++)
	fi->actions[i] = FA_CREATE;

    rpmBuildFileList(fi->h, &fi->apath, NULL);

    i = fi->fc;
    if (headerIsEntry(fi->h, RPMTAG_COOKIE))
	for (i = 0; i < fi->fc; i++)
		if (fi->fflags[i] & RPMFILE_SPECFILE) break;

    if (i == fi->fc) {
	/* Find the spec file by name. */
	for (i = 0; i < fi->fc; i++) {
	    const char * t = fi->apath[i];
	    t += strlen(fi->apath[i]) - 5;
	    if (!strcmp(t, ".spec")) break;
	}
    }

    _sourcedir = rpmGenPath(ts->rootDir, "%{_sourcedir}", "");
    rc = chkdir(_sourcedir, "sourcedir");
    if (rc) {
	rc = RPMRC_FAIL;
	goto exit;
    }

    _specdir = rpmGenPath(ts->rootDir, "%{_specdir}", "");
    rc = chkdir(_specdir, "specdir");
    if (rc) {
	rc = RPMRC_FAIL;
	goto exit;
    }

    /* Build dnl/dil with {_sourcedir, _specdir} as values. */
    if (i < fi->fc) {
	int speclen = strlen(_specdir) + 2;
	int sourcelen = strlen(_sourcedir) + 2;
	char * t;

	fi->dnl = hfd(fi->dnl, -1);

	fi->dc = 2;
	fi->dnl = xmalloc(fi->dc * sizeof(*fi->dnl) + fi->fc * sizeof(*fi->dil) +
			speclen + sourcelen);
	fi->dil = (int *)(fi->dnl + fi->dc);
	memset(fi->dil, 0, fi->fc * sizeof(*fi->dil));
	fi->dil[i] = 1;
	/*@-dependenttrans@*/
	fi->dnl[0] = t = (char *)(fi->dil + fi->fc);
	fi->dnl[1] = t = stpcpy( stpcpy(t, _sourcedir), "/") + 1;
	/*@=dependenttrans@*/
	(void) stpcpy( stpcpy(t, _specdir), "/");

	t = xmalloc(speclen + strlen(fi->bnl[i]) + 1);
	(void) stpcpy( stpcpy( stpcpy(t, _specdir), "/"), fi->bnl[i]);
	specFile = t;
    } else {
	rpmError(RPMERR_NOSPEC, _("source package contains no .spec file\n"));
	rc = RPMRC_FAIL;
	goto exit;
    }

    psm->goal = PSM_PKGINSTALL;

    /*@-compmempass@*/	/* FIX: psm->fi->dnl should be owned. */
    rc = psmStage(psm, PSM_PROCESS);

    (void) psmStage(psm, PSM_FINI);
    /*@=compmempass@*/

    if (rc) rc = RPMRC_FAIL;

exit:
    if (specFilePtr && specFile && rc == RPMRC_OK)
	*specFilePtr = specFile;
    else
	specFile = _free(specFile);

    _specdir = _free(_specdir);
    _sourcedir = _free(_sourcedir);

    if (h) h = headerFree(h);

    if (fi) {
	freeFi(fi);
	fi = _free(fi);
    }
    ts = rpmtransFree(ts);

    return rc;
}

/*@observer@*/ /*@unchecked@*/
static char * SCRIPT_PATH =
	"PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/X11R6/bin";

/**
 * Return scriptlet name from tag.
 * @param tag		scriptlet tag
 * @return		name of scriptlet
 */
static /*@observer@*/ const char * tag2sln(int tag)
	/*@*/
{
    switch (tag) {
    case RPMTAG_PREIN:		return "%pre";
    case RPMTAG_POSTIN:		return "%post";
    case RPMTAG_PREUN:		return "%preun";
    case RPMTAG_POSTUN:		return "%postun";
    case RPMTAG_VERIFYSCRIPT:	return "%verify";
    }
    return "%unknownscript";
}

/**
 * Run scriptlet with args.
 *
 * Run a script with an interpreter. If the interpreter is not specified,
 * /bin/sh will be used. If the interpreter is /bin/sh, then the args from
 * the header will be ignored, passing instead arg1 and arg2.
 * 
 * @param psm		package state machine data
 * @param h		header
 * @param sln		name of scriptlet section
 * @param progArgc	no. of args from header
 * @param progArgv	args from header, progArgv[0] is the interpreter to use
 * @param script	scriptlet from header
 * @param arg1		no. instances of package installed after scriptlet exec
 *			(-1 is no arg)
 * @param arg2		ditto, but for the target package
 * @return		0 on success, 1 on error
 */
static int runScript(PSM_t psm, Header h,
		const char * sln,
		int progArgc, const char ** progArgv, 
		const char * script, int arg1, int arg2)
	/*@globals rpmGlobalMacroContext,
		fileSystem, internalState@*/
	/*@modifies psm, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    const char ** argv = NULL;
    int argc = 0;
    const char ** prefixes = NULL;
    int numPrefixes;
    rpmTagType ipt;
    const char * oldPrefix;
    int maxPrefixLength;
    int len;
    char * prefixBuf = NULL;
    pid_t child;
    int status = 0;
    const char * fn = NULL;
    int i, xx;
    int freePrefixes = 0;
    FD_t out;
    rpmRC rc = RPMRC_OK;
    const char *n = NULL, *v = NULL, *r = NULL, *d = NULL;
    char arg1_str [sizeof(int)*3+1] = "";
    char arg2_str [sizeof(int)*3+1] = "";

    if (!progArgv && !script)
	return 0;

    if (!progArgv) {
	argv = alloca(5 * sizeof(*argv));
	argv[0] = "/bin/sh";
	argc = 1;
    } else {
	argv = alloca((progArgc + 4) * sizeof(*argv));
	memcpy(argv, progArgv, progArgc * sizeof(*argv));
	argc = progArgc;
    }

    if (h)
	xx = headerNVRD(h, &n, &v, &r, &d);

    if (arg1 >= 0)
	sprintf(arg1_str, "%d", arg1);
    if (arg2 >= 0)
	sprintf(arg2_str, "%d", arg2);

    if (h && hge(h, RPMTAG_INSTPREFIXES, &ipt, (void **) &prefixes, &numPrefixes)) {
	freePrefixes = 1;
    } else if (h && hge(h, RPMTAG_INSTALLPREFIX, NULL, (void **) &oldPrefix, NULL)) {
	prefixes = &oldPrefix;
	numPrefixes = 1;
    } else {
	numPrefixes = 0;
    }

    maxPrefixLength = 0;
    for (i = 0; i < numPrefixes; i++) {
	len = strlen(prefixes[i]);
	if (len > maxPrefixLength) maxPrefixLength = len;
    }
    prefixBuf = alloca(maxPrefixLength + 50);

    if (script) {
	FD_t fd;
	/*@-branchstate@*/
	if (makeTempFile((!ts->chrootDone ? ts->rootDir : "/"), &fn, &fd)) {
	    if (freePrefixes) free(prefixes);
	    return 1;
	}
	/*@=branchstate@*/

	if (rpmIsDebug() &&
	    (!strcmp(argv[0], "/bin/sh") || !strcmp(argv[0], "/bin/bash")))
	    xx = Fwrite("set -x\n", sizeof(char), 7, fd);

	xx = Fwrite(script, sizeof(script[0]), strlen(script), fd);
	xx = Fclose(fd);

	{   const char * sn = fn;
	    if (!ts->chrootDone &&
		!(ts->rootDir[0] == '/' && ts->rootDir[1] == '\0'))
	    {
		sn += strlen(ts->rootDir)-1;
	    }
	    argv[argc++] = sn;
	}

	if (*arg1_str)
	    argv[argc++] = arg1_str;
	if (*arg2_str)
	    argv[argc++] = arg2_str;
    }

    argv[argc] = NULL;

    if (ts->scriptFd != NULL) {
	if (rpmIsVerbose()) {
	    out = fdDup(Fileno(ts->scriptFd));
	} else {
	    out = Fopen("/dev/null", "w.fdio");
	    if (Ferror(out)) {
		out = fdDup(Fileno(ts->scriptFd));
	    }
	}
    } else {
	out = fdDup(STDOUT_FILENO);
#ifdef	DYING
	out = fdLink(out, "runScript persist");
#endif
    }
    if (out == NULL) return 1;	/* XXX can't happen */
    
    /*@-branchstate@*/
    if (!(child = fork())) {
	const char * rootDir;
	int pipes[2];

	pipes[0] = pipes[1] = 0;
	/* make stdin inaccessible */
	xx = pipe(pipes);
	xx = close(pipes[1]);
	xx = dup2(pipes[0], STDIN_FILENO);
	xx = close(pipes[0]);

	if (ts->scriptFd != NULL) {
	    if (Fileno(ts->scriptFd) != STDERR_FILENO)
		xx = dup2(Fileno(ts->scriptFd), STDERR_FILENO);
	    if (Fileno(out) != STDOUT_FILENO)
		xx = dup2(Fileno(out), STDOUT_FILENO);
	    /* make sure we don't close stdin/stderr/stdout by mistake! */
	    if (Fileno(out) > STDERR_FILENO && Fileno(out) != Fileno(ts->scriptFd)) {
		xx = Fclose (out);
	    }
	    if (Fileno(ts->scriptFd) > STDERR_FILENO) {
		xx = Fclose (ts->scriptFd);
	    }
	}

	{   const char *ipath = rpmExpand("PATH=%{_install_script_path}", NULL);
	    const char *path = SCRIPT_PATH;

	    if (ipath && ipath[5] != '%')
		path = ipath;
	    xx = doputenv(path);
	    /*@-modobserver@*/
	    ipath = _free(ipath);
	    /*@=modobserver@*/
	}

	for (i = 0; i < numPrefixes; i++) {
	    sprintf(prefixBuf, "RPM_INSTALL_PREFIX%d=%s", i, prefixes[i]);
	    xx = doputenv(prefixBuf);

	    /* backwards compatibility */
	    if (i == 0) {
		sprintf(prefixBuf, "RPM_INSTALL_PREFIX=%s", prefixes[i]);
		xx = doputenv(prefixBuf);
	    }
	}

	if (n)
	dosetenv ("RPM_INSTALL_NAME", n, 1);

	if (*arg1_str)
	    dosetenv ("RPM_INSTALL_ARG1", arg1_str, 1);
	if (*arg2_str)
	    dosetenv ("RPM_INSTALL_ARG2", arg2_str, 1);

	if ( rpm_close_all() ) {
		perror( "rpm_close_all" );
		_exit( -1 );
	}

	/* executed scripts exepcts default SIGPIPE handler. */
        signal (SIGPIPE, SIG_DFL);

	if ((rootDir = ts->rootDir) != NULL)	/* XXX can't happen */
	switch(urlIsURL(rootDir)) {
	case URL_IS_PATH:
	    rootDir += sizeof("file://") - 1;
	    rootDir = strchr(rootDir, '/');
	    /*@fallthrough@*/
	case URL_IS_UNKNOWN:
	    if (!ts->chrootDone && !(rootDir[0] == '/' && rootDir[1] == '\0')) {
		/*@-unrecog -superuser @*/
		xx = chroot(rootDir);
		/*@=unrecog =superuser @*/
	    }
	    (void) chdir("/");
	    (void) umask(022);
	    /* Permit libselinux to do the scriptlet exec. */
	    if (ts->selinuxEnabled)
		xx = rpm_execcon(0, argv[0], (char *const *)argv, environ);
	    else
		xx = execv(argv[0], (char *const *)argv);
	    break;
	default:
	    break;
	}

 	_exit(-1);
	/*@notreached@*/
    }
    /*@=branchstate@*/

    if (waitpid(child, &status, 0) < 0) {
	rpmError(RPMERR_SCRIPT,
     _("execution of %s scriptlet from %s-%s-%s%s%s failed, waitpid returned %s\n"),
		 sln, n, v, r, d ? ":" : "", d ? : "", strerror (errno));
	/* XXX what to do here? */
	rc = RPMRC_OK;
    } else {
	if (!WIFEXITED(status) || WEXITSTATUS(status)) {
	    rpmError(RPMERR_SCRIPT,
     _("execution of %s scriptlet from %s-%s-%s%s%s failed, exit status %d\n"),
		     sln, n, v, r, d ? ":" : "", d ? : "", WEXITSTATUS(status));
	    rc = RPMRC_FAIL;
	}
    }

    if (freePrefixes) prefixes = hfd(prefixes, ipt);

    xx = Fclose(out);	/* XXX dup'd STDOUT_FILENO */
    
    if (script) {
	if (!rpmIsDebug())
	    xx = unlink(fn);
	fn = _free(fn);
    }

    return rc;
}

/**
 * Retrieve and run scriptlet from header.
 * @param psm		package state machine data
 * @return		rpmRC return code
 */
static rpmRC runInstScript(PSM_t psm)
	/*@globals rpmGlobalMacroContext,
		fileSystem, internalState @*/
	/*@modifies psm, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    TFI_t fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    void ** programArgv;
    int programArgc;
    const char ** argv;
    rpmTagType ptt, stt;
    const char * script;
    rpmRC rc = RPMRC_OK;
    int xx;

    /*
     * headerGetEntry() sets the data pointer to NULL if the entry does
     * not exist.
     */
    xx = hge(fi->h, psm->progTag, &ptt, (void **) &programArgv, &programArgc);
    xx = hge(fi->h, psm->scriptTag, &stt, (void **) &script, NULL);

    if (programArgv && ptt == RPM_STRING_TYPE) {
	argv = alloca(sizeof(char *));
	*argv = (const char *) programArgv;
    } else {
	argv = (const char **) programArgv;
    }

    rc = runScript(psm, fi->h, tag2sln(psm->scriptTag), programArgc, argv,
		script, psm->scriptArg, -1);

    programArgv = hfd(programArgv, ptt);
    script = hfd(script, stt);
    return rc;
}

/**
 * @param psm		package state machine data
 * @param sourceH
 * @param triggeredH
 * @param arg2
 * @param triggersAlreadyRun
 * @return
 */
static int handleOneTrigger(PSM_t psm, Header sourceH, Header triggeredH,
			int arg2, unsigned char * triggersAlreadyRun)
	/*@globals rpmGlobalMacroContext,
		fileSystem, internalState@*/
	/*@modifies psm, triggeredH, *triggersAlreadyRun, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    const char ** triggerNames;
    const char ** triggerEVR;
    const char ** triggerScripts;
    const char ** triggerProgs;
    int_32 * triggerFlags;
    int_32 * triggerIndices;
    rpmTagType tnt, tvt, tft;
    const char * triggerPackageName;
    const char * sourceName;
    int numTriggers;
    rpmRC rc = RPMRC_OK;
    int i;
    int skip;
    int xx;

    if (!(	hge(triggeredH, RPMTAG_TRIGGERNAME, &tnt, 
			(void **) &triggerNames, &numTriggers) &&
		hge(triggeredH, RPMTAG_TRIGGERFLAGS, &tft,
			(void **) &triggerFlags, NULL) &&
		hge(triggeredH, RPMTAG_TRIGGERVERSION, &tvt,
			(void **) &triggerEVR, NULL))
	)
	return 0;

    xx = headerName(sourceH, &sourceName);

    for (i = 0; i < numTriggers; i++) {
	rpmTagType tit, tst, tpt;

	if (!(triggerFlags[i] & psm->sense)) continue;
	if (strcmp(triggerNames[i], sourceName)) continue;

	/*
	 * For some reason, the TRIGGERVERSION stuff includes the name of
	 * the package which the trigger is based on. We need to skip
	 * over that here. I suspect that we'll change our minds on this
	 * and remove that, so I'm going to just 'do the right thing'.
	 */
	skip = strlen(triggerNames[i]);
	if (!strncmp(triggerEVR[i], triggerNames[i], skip) &&
	    (triggerEVR[i][skip] == '-'))
	    skip++;
	else
	    skip = 0;

	if (!headerMatchesDepFlags(sourceH, triggerNames[i],
		triggerEVR[i] + skip, triggerFlags[i]))
	    continue;

	if (!(	hge(triggeredH, RPMTAG_TRIGGERINDEX, &tit,
		       (void **) &triggerIndices, NULL) &&
		hge(triggeredH, RPMTAG_TRIGGERSCRIPTS, &tst,
		       (void **) &triggerScripts, NULL) &&
		hge(triggeredH, RPMTAG_TRIGGERSCRIPTPROG, &tpt,
		       (void **) &triggerProgs, NULL))
	    )
	    continue;

	xx = headerName(triggeredH, &triggerPackageName);

	{   int arg1;
	    int index;

	    arg1 = rpmdbCountPackages(ts->rpmdb, triggerPackageName);
	    if (arg1 < 0) {
		/* XXX W2DO? same as "execution of script failed" */
		rc = RPMRC_FAIL;
	    } else {
		arg1 += psm->countCorrection;
		index = triggerIndices[i];
		if (triggersAlreadyRun == NULL ||
		    triggersAlreadyRun[index] == 0)
		{
		    rc = runScript(psm, triggeredH, "%trigger", 1,
			    triggerProgs + index, triggerScripts[index], 
			    arg1, arg2);
		    if (triggersAlreadyRun != NULL)
			triggersAlreadyRun[index] = 1;
		}
	    }
	}

	triggerIndices = hfd(triggerIndices, tit);
	triggerScripts = hfd(triggerScripts, tst);
	triggerProgs = hfd(triggerProgs, tpt);

	/*
	 * Each target/source header pair can only result in a single
	 * script being run.
	 */
	break;
    }

    triggerNames = hfd(triggerNames, tnt);
    triggerFlags = hfd(triggerFlags, tft);
    triggerEVR = hfd(triggerEVR, tvt);

    return rc;
}

/**
 * Run trigger scripts in the database that are fired by this header.
 * @param psm		package state machine data
 * @return		0 on success, 1 on error
 */
static int runTriggers(PSM_t psm)
	/*@globals rpmGlobalMacroContext,
		fileSystem, internalState @*/
	/*@modifies psm, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    int numPackage;
    rpmRC rc = RPMRC_OK;

    numPackage = rpmdbCountPackages(ts->rpmdb, fi->name) + psm->countCorrection;
    if (numPackage < 0)
	return 1;

    {	Header triggeredH;
	rpmdbMatchIterator mi;
	int countCorrection = psm->countCorrection;

	psm->countCorrection = 0;
	mi = rpmdbInitIterator(ts->rpmdb, RPMTAG_TRIGGERNAME, fi->name, 0);
	while((triggeredH = rpmdbNextIterator(mi)) != NULL) {
	    rc |= handleOneTrigger(psm, fi->h, triggeredH, numPackage, NULL);
	}

	mi = rpmdbFreeIterator(mi);
	psm->countCorrection = countCorrection;
    }

    return rc;
}

/**
 * Run triggers from this header that are fired by headers in the database.
 * @param psm		package state machine data
 * @return		0 on success, 1 on error
 */
static int runImmedTriggers(PSM_t psm)
	/*@globals rpmGlobalMacroContext,
		fileSystem, internalState @*/
	/*@modifies psm, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    const char ** triggerNames;
    int numTriggers;
    int_32 * triggerIndices;
    rpmTagType tnt, tit;
    int numTriggerIndices;
    unsigned char * triggersRun;
    rpmRC rc = RPMRC_OK;

    if (!(	hge(fi->h, RPMTAG_TRIGGERNAME, &tnt,
			(void **) &triggerNames, &numTriggers) &&
		hge(fi->h, RPMTAG_TRIGGERINDEX, &tit,
			(void **) &triggerIndices, &numTriggerIndices))
	)
	return 0;

    triggersRun = alloca(sizeof(*triggersRun) * numTriggerIndices);
    memset(triggersRun, 0, sizeof(*triggersRun) * numTriggerIndices);

    {	Header sourceH = NULL;
	int i;

	for (i = 0; i < numTriggers; i++) {
	    rpmdbMatchIterator mi;

	    if (triggersRun[triggerIndices[i]] != 0) continue;
	
	    mi = rpmdbInitIterator(ts->rpmdb, RPMTAG_NAME, triggerNames[i], 0);

	    while((sourceH = rpmdbNextIterator(mi)) != NULL) {
		rc |= handleOneTrigger(psm, sourceH, fi->h, 
				rpmdbGetIteratorCount(mi),
				triggersRun);
	    }

	    mi = rpmdbFreeIterator(mi);
	}
    }
    triggerIndices = hfd(triggerIndices, tit);
    triggerNames = hfd(triggerNames, tnt);
    return rc;
}

/*@observer@*/ static const char * pkgStageString(pkgStage a)
	/*@*/
{
    switch(a) {
    case PSM_UNKNOWN:		return "unknown";

    case PSM_PKGINSTALL:	return "  install";
    case PSM_PKGERASE:		return "    erase";
    case PSM_PKGCOMMIT:		return "   commit";

    case PSM_INIT:		return "init";
    case PSM_PRE:		return "pre";
    case PSM_PROCESS:		return "process";
    case PSM_POST:		return "post";
    case PSM_UNDO:		return "undo";
    case PSM_FINI:		return "fini";

    case PSM_CREATE:		return "create";
    case PSM_NOTIFY:		return "notify";
    case PSM_DESTROY:		return "destroy";
    case PSM_COMMIT:		return "commit";

    case PSM_CHROOT_IN:		return "chrootin";
    case PSM_CHROOT_OUT:	return "chrootout";
    case PSM_SCRIPT:		return "script";
    case PSM_TRIGGERS:		return "triggers";
    case PSM_IMMED_TRIGGERS:	return "immedtriggers";

    case PSM_RPMIO_FLAGS:	return "rpmioflags";

    case PSM_RPMDB_LOAD:	return "rpmdbload";
    case PSM_RPMDB_ADD:		return "rpmdbadd";
    case PSM_RPMDB_REMOVE:	return "rpmdbremove";

    default:			return "???";
    }
    /*@noteached@*/
}

static void fi_log(TFI_t fi, const char *s)
{
#if HAVE_SYSLOG_H
	if (!geteuid())
	{
            const char * const *dp = NULL;
	    int_32 *uip = 0;
	    fi->hge(fi->h, RPMTAG_BUILDTIME, NULL, (void **) &uip, NULL);
	    fi->hge(fi->h, RPMTAG_DISTTAG, NULL, (void **) &dp, NULL);
	    if (-1 == fi->epoch)
		syslog (LOG_NOTICE, "%s-%s-%s%s%s %u %s\n",
                        fi->name, fi->version, fi->release,
                        dp ? " " : "", dp ? *dp : "",
                        uip ? *uip : 0, s);
	    else
		syslog (LOG_NOTICE, "%s-%u:%s-%s%s%s %u %s\n",
                        fi->name, fi->epoch, fi->version, fi->release,
                        dp ? " " : "", dp ? *dp : "",
                        uip ? *uip : 0, s);
	}
#endif
}

/**
 * @todo Packages w/o files never get a callback, hence don't get displayed
 * on install with -v.
 */
int psmStage(PSM_t psm, pkgStage stage)
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    HGE_t hge = fi->hge;
    HME_t hme = fi->hme;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    rpmRC rc = psm->rc;
    int saveerrno;
    int xx;

    /*@-branchstate@*/
    switch (stage) {
    case PSM_UNKNOWN:
	break;
    case PSM_INIT:
	rpmMessage(RPMMESS_DEBUG, _("%s: %s-%s-%s has %d files, test = %d\n"),
		psm->stepName, fi->name, fi->version, fi->release,
		fi->fc, (ts->transFlags & RPMTRANS_FLAG_TEST));

	/*
	 * When we run scripts, we pass an argument which is the number of 
	 * versions of this package that will be installed when we are
	 * finished.
	 */
	psm->npkgs_installed = rpmdbCountPackages(ts->rpmdb, fi->name);
	if (psm->npkgs_installed < 0) {
	    rc = RPMRC_FAIL;
	    break;
	}

	if (psm->goal == PSM_PKGINSTALL) {
	    char b[80];
	    psm->scriptArg = psm->npkgs_installed + 1;

assert(psm->mi == NULL);
	    psm->mi = rpmdbInitIterator(ts->rpmdb, RPMTAG_NAME, fi->name, 0);
	    sprintf(b, "%u", fi->epoch);
	    xx = rpmdbSetIteratorRE(psm->mi, RPMTAG_EPOCH,
			RPMMIRE_DEFAULT, b);
	    xx = rpmdbSetIteratorRE(psm->mi, RPMTAG_VERSION,
			RPMMIRE_DEFAULT, fi->version);
	    xx = rpmdbSetIteratorRE(psm->mi, RPMTAG_RELEASE,
			RPMMIRE_DEFAULT, fi->release);
	    xx = rpmdbSetIteratorRE(psm->mi, RPMTAG_SHA1HEADER,
			RPMMIRE_DEFAULT, fi->digest);

	    while (rpmdbNextIterator(psm->mi)) {
		fi->record = rpmdbGetIteratorOffset(psm->mi);
		/*@loopbreak@*/ break;
	    }
	    psm->mi = rpmdbFreeIterator(psm->mi);
	    rc = RPMRC_OK;

	    if (fi->fc > 0 && fi->fstates == NULL) {
		fi->fstates = xmalloc(sizeof(*fi->fstates) * fi->fc);
		memset(fi->fstates, RPMFILE_STATE_NORMAL, fi->fc);
	    }

	    if (fi->fc <= 0)				break;
	    if (ts->transFlags & RPMTRANS_FLAG_JUSTDB)	break;
	
	    /*
	     * Old format relocatable packages need the entire default
	     * prefix stripped to form the cpio list, while all other packages
	     * need the leading / stripped.
	     */
	    {   const char * p;
		rc = hge(fi->h, RPMTAG_DEFAULTPREFIX, NULL, (void **) &p, NULL);
		fi->striplen = (rc ? strlen(p) + 1 : 1); 
	    }
	    fi->mapflags =
		CPIO_MAP_PATH | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID;
	
	    if (headerIsEntry(fi->h, RPMTAG_ORIGBASENAMES))
		buildOrigFileList(fi->h, &fi->apath, NULL);
	    else
		rpmBuildFileList(fi->h, &fi->apath, NULL);
	
	    if (fi->fuser == NULL)
		xx = hge(fi->h, RPMTAG_FILEUSERNAME, NULL,
				(void **) &fi->fuser, NULL);
	    if (fi->fgroup == NULL)
		xx = hge(fi->h, RPMTAG_FILEGROUPNAME, NULL,
				(void **) &fi->fgroup, NULL);
	    if (fi->fuids == NULL)
		fi->fuids = xcalloc(sizeof(*fi->fuids), fi->fc);
	    if (fi->fgids == NULL)
		fi->fgids = xcalloc(sizeof(*fi->fgids), fi->fc);
	    rc = RPMRC_OK;
	}
	if (psm->goal == PSM_PKGERASE) {
	    psm->scriptArg = psm->npkgs_installed - 1;
	
	    /* Retrieve installed header. */
	    rc = psmStage(psm, PSM_RPMDB_LOAD);
	}
	break;
    case PSM_PRE:
	if (ts->transFlags & RPMTRANS_FLAG_TEST)	break;

	/* Change root directory if requested and not already done. */
	rc = psmStage(psm, PSM_CHROOT_IN);

	if (psm->goal == PSM_PKGINSTALL) {
	    psm->scriptTag = RPMTAG_PREIN;
	    psm->progTag = RPMTAG_PREINPROG;

	    if (!(ts->transFlags & RPMTRANS_FLAG_NOTRIGGERPREIN)) {
		/* XXX FIXME: implement %triggerprein. */
	    }

	    if (!(ts->transFlags & RPMTRANS_FLAG_NOPRE)) {
		rc = psmStage(psm, PSM_SCRIPT);
		if (rc) {
		    rpmError(RPMERR_SCRIPT,
			_("%s: %s scriptlet failed (%d), skipping %s-%s-%s\n"),
			psm->stepName, tag2sln(psm->scriptTag), rc,
			fi->name, fi->version, fi->release);
		    break;
		}
	    }
	}

	if (psm->goal == PSM_PKGERASE) {
	    psm->scriptTag = RPMTAG_PREUN;
	    psm->progTag = RPMTAG_PREUNPROG;
	    psm->sense = RPMSENSE_TRIGGERUN;
	    psm->countCorrection = -1;

	    if (!(ts->transFlags & RPMTRANS_FLAG_NOTRIGGERUN)) {
		/* Run triggers in other package(s) this package sets off. */
		rc = psmStage(psm, PSM_TRIGGERS);
		if (rc) break;

		/* Run triggers in this package other package(s) set off. */
		rc = psmStage(psm, PSM_IMMED_TRIGGERS);
		if (rc) break;
	    }

	    if (!(ts->transFlags & RPMTRANS_FLAG_NOPREUN))
		rc = psmStage(psm, PSM_SCRIPT);
	}
	break;
    case PSM_PROCESS:
	if (ts->transFlags & RPMTRANS_FLAG_TEST)	break;

	if (psm->goal == PSM_PKGINSTALL) {
	    struct availablePackage * alp = fi->ap;
	    int i;
	    int isSource;

	    if (fi->fc <= 0)				break;
	    if (ts->transFlags & RPMTRANS_FLAG_JUSTDB)	break;

	    isSource = !headerIsEntry(fi->h, RPMTAG_SOURCERPM);

	    for (i = 0; i < fi->fc; i++) {
		uid_t uid;
		gid_t gid;

		uid = fi->uid;
		gid = fi->gid;
		if (isSource)
		    fi->fmodes[i] &= ~S_ISUID;
		else if (fi->fuser && unameToUid(fi->fuser[i], &uid)) {
		    rpmMessage(RPMMESS_WARNING,
			_("user %s does not exist - using root\n"),
			fi->fuser[i]);
		    uid = 0;
		    /* XXX this diddles header memory. */
		    fi->fmodes[i] &= ~S_ISUID;	/* turn off the suid bit */
		}

		if (isSource)
		    fi->fmodes[i] &= ~S_ISGID;
		else if (fi->fgroup && gnameToGid(fi->fgroup[i], &gid)) {
		    rpmMessage(RPMMESS_WARNING,
			_("group %s does not exist - using root\n"),
			fi->fgroup[i]);
		    gid = 0;
		    /* XXX this diddles header memory. */
		    fi->fmodes[i] &= ~S_ISGID;	/* turn off the sgid bit */
		}
		if (fi->fuids) fi->fuids[i] = uid;
		if (fi->fgids) fi->fgids[i] = gid;
	    }

	    /* Retrieve type of payload compression. */
	    rc = psmStage(psm, PSM_RPMIO_FLAGS);

	    if (alp->fd == NULL) {	/* XXX can't happen */
		rc = RPMRC_FAIL;
		break;
	    }
	    /*@-nullpass@*/	/* LCL: alp->fd != NULL here. */
	    psm->cfd = Fdopen(fdDup(Fileno(alp->fd)), psm->rpmio_flags);
	    /*@=nullpass@*/
	    if (psm->cfd == NULL) {	/* XXX can't happen */
		rc = RPMRC_FAIL;
		break;
	    }

	    rc = fsmSetup(fi->fsm, FSM_PKGINSTALL, ts, fi,
			psm->cfd, NULL, &psm->failedFile);
	    xx = fsmTeardown(fi->fsm);

	    saveerrno = errno; /* XXX FIXME: Fclose with libio destroys errno */
	    xx = Fclose(psm->cfd);
	    psm->cfd = NULL;
	    /*@-mods@*/
	    errno = saveerrno; /* XXX FIXME: Fclose with libio destroys errno */
	    /*@=mods@*/

	    if (!rc)
		rc = psmStage(psm, PSM_COMMIT);

	    if (rc) {
		rpmError(RPMERR_CPIO,
			_("unpacking of archive failed%s%s: %s\n"),
			(psm->failedFile != NULL ? _(" on file ") : ""),
			(psm->failedFile != NULL ? psm->failedFile : ""),
			cpioStrerror(rc));
		rc = RPMRC_FAIL;

		/* XXX notify callback on error. */
		psm->what = RPMCALLBACK_UNPACK_ERROR;
		psm->amount = 0;
		psm->total = 0;
		xx = psmStage(psm, PSM_NOTIFY);

		break;
	    }
	    psm->what = RPMCALLBACK_INST_PROGRESS;
	    psm->amount = (fi->archiveSize ? fi->archiveSize : 100);
	    psm->total = psm->amount;
	    xx = psmStage(psm, PSM_NOTIFY);
	}
	if (psm->goal == PSM_PKGERASE) {

	    if (fi->fc <= 0)				break;
	    if (ts->transFlags & RPMTRANS_FLAG_JUSTDB)	break;
	    if (ts->transFlags & RPMTRANS_FLAG_APPLYONLY)	break;

	    psm->what = RPMCALLBACK_UNINST_START;
	    psm->amount = fi->fc;	/* XXX W2DO? looks wrong. */
	    psm->total = fi->fc;
	    xx = psmStage(psm, PSM_NOTIFY);

	    rc = fsmSetup(fi->fsm, FSM_PKGERASE, ts, fi,
			NULL, NULL, &psm->failedFile);
	    xx = fsmTeardown(fi->fsm);

	    psm->what = RPMCALLBACK_UNINST_STOP;
	    psm->amount = 0;		/* XXX W2DO? looks wrong. */
	    psm->total = fi->fc;
	    xx = psmStage(psm, PSM_NOTIFY);

	}
	break;
    case PSM_POST:
	if (ts->transFlags & RPMTRANS_FLAG_TEST)	break;

	if (psm->goal == PSM_PKGINSTALL) {
	    int_32 installTime = (int_32) time(NULL);

	    if (fi->fstates != NULL && fi->fc > 0)
		xx = headerAddEntry(fi->h, RPMTAG_FILESTATES, RPM_CHAR_TYPE,
				fi->fstates, fi->fc);

	    xx = headerAddEntry(fi->h, RPMTAG_INSTALLTIME, RPM_INT32_TYPE,
				&installTime, 1);

	    /*
	     * If this package has already been installed, remove it from
	     * the database before adding the new one.
	     */
	    if (fi->record && !(ts->transFlags & RPMTRANS_FLAG_APPLYONLY)) {
		rc = psmStage(psm, PSM_RPMDB_REMOVE);
		if (rc) break;
	    }

	    rc = psmStage(psm, PSM_RPMDB_ADD);
	    if (rc) break;

	    psm->scriptTag = RPMTAG_POSTIN;
	    psm->progTag = RPMTAG_POSTINPROG;
	    psm->sense = RPMSENSE_TRIGGERIN;
	    psm->countCorrection = 0;

	    if (!(ts->transFlags & RPMTRANS_FLAG_NOPOST)) {
		rc = psmStage(psm, PSM_SCRIPT);
		if (rc) break;
	    }
	    if (!(ts->transFlags & RPMTRANS_FLAG_NOTRIGGERIN)) {
		/* Run triggers in other package(s) this package sets off. */
		rc = psmStage(psm, PSM_TRIGGERS);
		if (rc) break;

		/* Run triggers in this package other package(s) set off. */
		rc = psmStage(psm, PSM_IMMED_TRIGGERS);
		if (rc) break;
	    }

	    if (!(ts->transFlags & RPMTRANS_FLAG_APPLYONLY))
		rc = markReplacedFiles(psm);

	}
	if (psm->goal == PSM_PKGERASE) {

	    psm->scriptTag = RPMTAG_POSTUN;
	    psm->progTag = RPMTAG_POSTUNPROG;
	    psm->sense = RPMSENSE_TRIGGERPOSTUN;
	    psm->countCorrection = -1;

	    if (!(ts->transFlags & RPMTRANS_FLAG_NOPOSTUN)) {
		rc = psmStage(psm, PSM_SCRIPT);
		/* XXX WTFO? postun failures don't cause erasure failure. */
	    }

	    if (!(ts->transFlags & RPMTRANS_FLAG_NOTRIGGERPOSTUN)) {
		/* Run triggers in other package(s) this package sets off. */
		rc = psmStage(psm, PSM_TRIGGERS);
		if (rc) break;
	    }

	    if (!(ts->transFlags & RPMTRANS_FLAG_APPLYONLY))
		rc = psmStage(psm, PSM_RPMDB_REMOVE);
	}

	/* Restore root directory if changed. */
	xx = psmStage(psm, PSM_CHROOT_OUT);
	break;
    case PSM_UNDO:
	break;
    case PSM_FINI:
	/* Restore root directory if changed. */
	xx = psmStage(psm, PSM_CHROOT_OUT);

	if (rc) {
	    if (psm->failedFile)
		rpmError(RPMERR_CPIO,
			_("%s failed on file %s: %s\n"),
			psm->stepName, psm->failedFile, cpioStrerror(rc));
	    else
		rpmError(RPMERR_CPIO, _("%s failed: %s\n"),
			psm->stepName, cpioStrerror(rc));

	    /* XXX notify callback on error. */
	    psm->what = RPMCALLBACK_CPIO_ERROR;
	    psm->amount = 0;
	    psm->total = 0;
	    xx = psmStage(psm, PSM_NOTIFY);
	}

	if (fi->h && psm->goal == PSM_PKGERASE)
	    fi->h = headerFree(fi->h);
	psm->rpmio_flags = _free(psm->rpmio_flags);
	psm->failedFile = _free(psm->failedFile);

	fi->fgids = _free(fi->fgids);
	fi->fuids = _free(fi->fuids);
	fi->fgroup = hfd(fi->fgroup, -1);
	fi->fuser = hfd(fi->fuser, -1);
	fi->apath = _free(fi->apath);
	fi->fstates = _free(fi->fstates);
	break;

    case PSM_PKGINSTALL:
    case PSM_PKGERASE:
	psm->goal = stage;
	psm->rc = RPMRC_OK;
	psm->stepName = pkgStageString(stage);

	rc = psmStage(psm, PSM_INIT);
	if (!rc) rc = psmStage(psm, PSM_PRE);
	if (!rc) rc = psmStage(psm, PSM_PROCESS);
	if (!rc) rc = psmStage(psm, PSM_POST);
	xx = psmStage(psm, PSM_FINI);
	break;
    case PSM_PKGCOMMIT:
	break;

    case PSM_CREATE:
	break;
    case PSM_NOTIFY:
	if (ts && ts->notify) {
	    /*@-noeffectuncon @*/ /* FIX: check rc */
	    (void) ts->notify(fi->h, psm->what, psm->amount, psm->total,
		(fi->ap ? fi->ap->key : NULL), ts->notifyData);
	    /*@=noeffectuncon @*/
	}
	break;
    case PSM_DESTROY:
	break;
    case PSM_COMMIT:
	if (!(ts->transFlags & RPMTRANS_FLAG_PKGCOMMIT)) break;
	if (ts->transFlags & RPMTRANS_FLAG_APPLYONLY) break;

	rc = fsmSetup(fi->fsm, FSM_PKGCOMMIT, ts, fi,
			NULL, NULL, &psm->failedFile);
	xx = fsmTeardown(fi->fsm);
	break;

    case PSM_CHROOT_IN:
	/* Change root directory if requested and not already done. */
	if (ts->rootDir && !(ts->rootDir[0] == '/' && ts->rootDir[1] == '\0') &&
	    !ts->chrootDone && !psm->chrootDone) {
	    static int _loaded = 0;

	    /*
	     * This loads all of the name services libraries, in case we
	     * don't have access to them in the chroot().
	     */
	    if (!_loaded) {
		(void)getpwnam("root");
		endpwent();
		_loaded++;
	    }

	    xx = chdir("/");
	    /*@-unrecog -superuser @*/
	    rc = chroot(ts->rootDir);
	    /*@=unrecog =superuser @*/
	    psm->chrootDone = ts->chrootDone = 1;
	    if (ts->rpmdb != NULL) ts->rpmdb->db_chrootDone = 1;
#ifdef	DYING
	    /*@-onlytrans@*/
	    chroot_prefix = ts->rootDir;
	    /*@=onlytrans@*/
#endif
	}
	break;
    case PSM_CHROOT_OUT:
	/* Restore root directory if changed. */
	if (psm->chrootDone) {
	    /*@-superuser @*/
	    rc = chroot(".");
	    /*@=superuser @*/
	    psm->chrootDone = ts->chrootDone = 0;
	    if (ts->rpmdb != NULL) ts->rpmdb->db_chrootDone = 0;
#ifdef	DYING
	    chroot_prefix = NULL;
#endif
	    xx = chdir(ts->currDir);
	}
	break;
    case PSM_SCRIPT:
	rpmMessage(RPMMESS_DEBUG, _("%s: running %s script(s) (if any)\n"),
		psm->stepName, tag2sln(psm->scriptTag));
	rc = runInstScript(psm);
	break;
    case PSM_TRIGGERS:
	/* Run triggers in other package(s) this package sets off. */
	rc = runTriggers(psm);
	break;
    case PSM_IMMED_TRIGGERS:
	/* Run triggers in this package other package(s) set off. */
	rc = runImmedTriggers(psm);
	break;

    case PSM_RPMIO_FLAGS:
    {	const char * payload_compressor = NULL;
	char * t;

	if (!hge(fi->h, RPMTAG_PAYLOADCOMPRESSOR, NULL,
			    (void **) &payload_compressor, NULL))
	    payload_compressor = "gzip";
	psm->rpmio_flags = t = xmalloc(sizeof("r.gzdio"));
	*t = '\0';
	t = stpcpy(t, "r");
	if (!strcmp(payload_compressor, "gzip"))
	    t = stpcpy(t, ".gzdio");
#ifdef HAVE_BZLIB_H
	if (!strcmp(payload_compressor, "bzip2"))
	    t = stpcpy(t, ".bzdio");
#endif
	if (!strcmp(payload_compressor, "lzma"))
	    t = stpcpy(t, ".lzdio");
	if (!strcmp(payload_compressor, "xz"))
	    t = stpcpy(t, ".xzdio");
	rc = RPMRC_OK;
    }	break;

    case PSM_RPMDB_LOAD:
assert(psm->mi == NULL);
	psm->mi = rpmdbInitIterator(ts->rpmdb, RPMDBI_PACKAGES,
				&fi->record, sizeof(fi->record));

	fi->h = rpmdbNextIterator(psm->mi);
	if (fi->h)
	    fi->h = headerLink(fi->h);
else {
fprintf(stderr, "*** PSM_RDB_LOAD: header #%u not found\n", fi->record);
}
	psm->mi = rpmdbFreeIterator(psm->mi);
	rc = (fi->h ? RPMRC_OK : RPMRC_FAIL);
	break;
    case PSM_RPMDB_ADD:
	if (ts->transFlags & RPMTRANS_FLAG_TEST)	break;
	if (fi->h != NULL)	/* XXX can't happen */
	rc = rpmdbAdd(ts->rpmdb, ts->id, fi->h);
	fi_log(fi, "installed");
	break;
    case PSM_RPMDB_REMOVE:
	if (ts->transFlags & RPMTRANS_FLAG_TEST)	break;
	rc = rpmdbRemove(ts->rpmdb, ts->id, fi->record);
	fi_log(fi, "removed");
	break;

    default:
	break;
    }
    /*@=branchstate@*/

    /*@-nullstate@*/	/* FIX: psm->fi->h may be NULL. */
    return rc;
    /*@=nullstate@*/
}

static
void saveTriggerFiles(PSM_t psm)
{
    const rpmTransactionSet ts = psm->ts;
    if (ts->transFlags & RPMTRANS_FLAG_TEST)
	return;
    if (ts->transFlags & (_noTransScripts | _noTransTriggers))
	return;
    const TFI_t fi = psm->fi;
    if (fi->fc < 1)
	return;
    psmStage(psm, PSM_CHROOT_IN);
    const char *file = rpmGetPath(ts->rpmdb->db_home, "/files-awaiting-filetriggers", NULL);
    FILE *fp = fopen(file, "a");
    if (fp == NULL)
	rpmError(RPMERR_OPEN, "open of %s failed: %s\n", file, strerror(errno));
    else {
	int i;
	for (i = 0; i < fi->fc; i++) {
	    const char *d = fi->dnl[fi->dil[i]];
	    const char *b = fi->bnl[i];
	    if (strchr(d, '\n'))
		continue;
	    if (strchr(b, '\n'))
		continue;
	    fprintf(fp, "%s%s\n", d, b);
	}
	fclose(fp);
    }
    file = _free(file);
    psmStage(psm, PSM_CHROOT_OUT);
}

void psmTriggerAdded(PSM_t psm)
{
    saveTriggerFiles(psm);
}

void psmTriggerRemoved(PSM_t psm)
{
    saveTriggerFiles(psm);
}

static /* HACK */
int relock(rpmdb db_, short type)
{
    DB *db = db_->_dbi[0]->dbi_db;
    if (db == NULL)
	return 1;
    int fd = -1;
    if (db->fd(db, &fd) || fd < 0)
	return 1;
    struct flock l = { 0 };
    l.l_type = type;
    return fcntl(fd, F_SETLK, &l);
}

void psmTriggerPosttrans(PSM_t psm)
{
    const rpmTransactionSet ts = psm->ts;
    if (ts->transFlags & RPMTRANS_FLAG_TEST)
	return;
    if (ts->transFlags & (_noTransScripts | _noTransTriggers))
	return;
    if (!psm->fi)
	return;
    psmStage(psm, PSM_CHROOT_IN);
    if (relock(psm->ts->rpmdb, F_RDLCK))
	rpmMessage(RPMMESS_WARNING, "failed to downgrade database lock\n");
    const char *file = rpmGetPath(ts->rpmdb->db_home, "/files-awaiting-filetriggers", NULL);
    const char *script = RPMCONFIGDIR "/posttrans-filetriggers";
    const char *argv[] = { script, file, NULL };
    int verbose = RPMMESS_VERBOSE;
    /* HACK maybe increase verbosity */
    if (psm->ts->notify == rpmShowProgress) {
	int flags = (int) ((long)psm->ts->notifyData);
	if (flags & INSTALL_LABEL)
	    verbose = RPMMESS_NORMAL;
    }
    rpmMessage(verbose, _("Running %s\n"), script);
    int rc = runScript(psm, NULL, script, 2, argv, NULL, -1, -1);
    if (rc == 0)
	unlink(file);
    file = _free(file);
    if (relock(psm->ts->rpmdb, F_WRLCK))
	rpmMessage(RPMMESS_WARNING, "failed to restore database lock\n");
    psmStage(psm, PSM_CHROOT_OUT);
}
