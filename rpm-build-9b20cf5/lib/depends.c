/** \ingroup rpmdep
 * \file lib/depends.c
 */

#include "system.h"

#include "rpmlib.h"
#include "rpmmacro.h"		/* XXX for rpmExpand() */

#include "depends.h"
#include "al.h"
#include "rpmhash.h"

#include "debug.h"

/*@access Header@*/		/* XXX compared with NULL */
/*@access FD_t@*/		/* XXX compared with NULL */
/*@access rpmdb@*/		/* XXX compared with NULL */
/*@access rpmdbMatchIterator@*/		/* XXX compared with NULL */
/*@access rpmTransactionSet@*/
/*@access rpmDependencyConflict@*/
/*@access availableList@*/

/*@only@*/ char * printDepend(const char * depend, const char * key,
		const char * keyEVR, int keyFlags)
	/*@*/
{
    char * tbuf, * t;
    size_t nb;

    nb = 0;
    if (depend)	nb += strlen(depend) + 1;
    if (key)	nb += strlen(key);
    if (keyFlags & RPMSENSE_SENSEMASK) {
	if (nb)	nb++;
	if (keyFlags & RPMSENSE_LESS)	nb++;
	if (keyFlags & RPMSENSE_GREATER) nb++;
	if (keyFlags & RPMSENSE_EQUAL)	nb++;
    }
    if (keyEVR && *keyEVR) {
	if (nb)	nb++;
	nb += strlen(keyEVR);
    }

    t = tbuf = xmalloc(nb + 1);
    if (depend) {
	while(*depend != '\0')	*t++ = *depend++;
	*t++ = ' ';
    }
    if (key)
	while(*key != '\0')	*t++ = *key++;
    if (keyFlags & RPMSENSE_SENSEMASK) {
	if (t != tbuf)	*t++ = ' ';
	if (keyFlags & RPMSENSE_LESS)	*t++ = '<';
	if (keyFlags & RPMSENSE_GREATER) *t++ = '>';
	if (keyFlags & RPMSENSE_EQUAL)	*t++ = '=';
    }
    if (keyEVR && *keyEVR) {
	if (t != tbuf)	*t++ = ' ';
	while(*keyEVR != '\0')	*t++ = *keyEVR++;
    }
    *t = '\0';
    return tbuf;
}

/* parseEVR() moved to rpmvercmp.c */

const char *rpmNAME = PACKAGE;
const char *rpmEVR = VERSION;
int rpmFLAGS = RPMSENSE_EQUAL;

#include "set.h"

int rpmRangesOverlap(const char * AName, const char * AEVR, int AFlags,
	const char * BName, const char * BEVR, int BFlags)
{
    const char *aDepend = NULL;
    const char *bDepend = NULL;
    int result;
    int sense;

    /* Different names don't overlap. */
    if (AName != BName && strcmp(AName, BName)) {
	result = 0;
	goto exit;
    }

    /* Same name. If either A or B is an existence test, always overlap. */
    if (!((AFlags & RPMSENSE_SENSEMASK) && (BFlags & RPMSENSE_SENSEMASK))) {
	result = 1;
	goto exit;
    }

    if (!AEVR) AEVR = "";
    if (!BEVR) BEVR = "";

    if (*AEVR && *BEVR) {
	/* equal version strings => equal versions */
	if ((AFlags & RPMSENSE_SENSEMASK) == RPMSENSE_EQUAL &&
	    (BFlags & RPMSENSE_SENSEMASK) == RPMSENSE_EQUAL &&
	    strcmp(AEVR, BEVR) == 0)
	{
	    result = 1;
	    goto exit;
	}
    }
    /* something beats nothing */
    else if (*AEVR) {
	sense = 1;
	goto sense_result;
    }
    else if (*BEVR) {
	sense = -1 ;
	goto sense_result;
    }
    else {
	/* both EVRs are non-existent or empty, always overlap */
	result = 1;
	goto exit;
    }

    int aset = strncmp(AEVR, "set:", 4) == 0;
    int bset = strncmp(BEVR, "set:", 4) == 0;
    if (aset && bset) {
	sense = rpmsetcmp(AEVR, BEVR);
	if (sense < -1) {
	    if (sense == -3)
		rpmMessage(RPMMESS_WARNING, _("failed to decode %s\n"), AEVR);
	    if (sense == -4)
		rpmMessage(RPMMESS_WARNING, _("failed to decode %s\n"), BEVR);
	    /* neither is subset of each other */
	    result = 0;
	    goto exit;
	}
    }
    else if (aset || bset) {
	/* no overlap between a set and non-set */
	result = 0;
	goto exit;
    }
    else {
        const char *aE, *aV, *aR, *aD, *bE, *bV, *bR, *bD;
	/* Both AEVR and BEVR exist. */
	parseEVRD(strdupa(AEVR), &aE, &aV, &aR, &aD);
	parseEVRD(strdupa(BEVR), &bE, &bV, &bR, &bD);
	/* rpmEVRcmp() is also shared; the code moved to rpmvercmp.c */
	if (rpmIsDebug()) {
	    aDepend = printDepend(NULL, AName, AEVR, AFlags);
	    bDepend = printDepend(NULL, BName, BEVR, BFlags);
	}
	sense = rpmEVRcmp(aE, aV, aR, aDepend, bE, bV, bR, bDepend);
        /* We can't merge the DistTag comparison into rpmEVRcmp(), because
           rpmEVRcmp() doesn't have a return code for incomparable things.
           That's similar to set comparison which is done in this function.
        */
	if (sense == 0) {
            if (bD && *bD) {
                /* (Remember: we are in the case when the EVR components
                   are equal.) We might detect equal DistTags here.
                   If not, since DistTags have no order, there is
                   no possibility here that one thing will be less or greater
                   than the other; the result is "incomparable".
                */
                if (aD && *aD && strcmp(aD, bD) == 0)
                    sense = 0;
                else {
                    result = 0;
                    goto exit;
                }
            } else if (aD && *aD) {
                /* Support for underspecification on the side of Requires/Conflicts */
                rpmMessage(RPMMESS_DEBUG, _("the \"B\" dependency doesn't specify a disttag, letting it match any in \"A\"\n\tA %s\tB %s\n"),
                           aDepend, bDepend);
                sense = 0;
            }
        }
    }

sense_result:
    /* Detect overlap of {A,B} range. */
    result = 0;
    if (sense < 0 && ((AFlags & RPMSENSE_GREATER) || (BFlags & RPMSENSE_LESS))) {
	result = 1;
    } else if (sense > 0 && ((AFlags & RPMSENSE_LESS) || (BFlags & RPMSENSE_GREATER))) {
	result = 1;
    } else if (sense == 0 &&
	(((AFlags & RPMSENSE_EQUAL) && (BFlags & RPMSENSE_EQUAL)) ||
	 ((AFlags & RPMSENSE_LESS) && (BFlags & RPMSENSE_LESS)) ||
	 ((AFlags & RPMSENSE_GREATER) && (BFlags & RPMSENSE_GREATER)))) {
	result = 1;
    }

exit:
    if (rpmIsDebug()) {
	if (!aDepend)
	    aDepend = printDepend(NULL, AName, AEVR, AFlags);
	if (!bDepend)
	    bDepend = printDepend(NULL, BName, BEVR, BFlags);
	rpmMessage(RPMMESS_DEBUG, _("  %s    A %s\tB %s\n"),
	    (result ? _("YES") : _("NO ")), aDepend, bDepend);
	aDepend = _free(aDepend);
	bDepend = _free(bDepend);
    }
    return result;
}

/*@-typeuse@*/
typedef int (*dbrecMatch_t) (Header h, const char *reqName, const char * reqEVR, int reqFlags);
/*@=typeuse@*/

static int rangeMatchesDepFlags (Header h,
		const char * reqName, const char * reqEVR, int reqFlags)
	/*@*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    rpmTagType pnt, pvt;
    const char ** provides;
    const char ** providesEVR;
    int_32 * provideFlags;
    int providesCount;
    int result;
    int i;

    if (!(reqFlags & RPMSENSE_SENSEMASK) || !reqEVR || !strlen(reqEVR))
	return 1;

    /* Get provides information from header */
    /*
     * Rpm prior to 3.0.3 does not have versioned provides.
     * If no provides version info is available, match any requires.
     */
    if (!hge(h, RPMTAG_PROVIDEVERSION, &pvt,
		(void **) &providesEVR, &providesCount))
	return 1;

    (void) hge(h, RPMTAG_PROVIDEFLAGS, NULL, (void **) &provideFlags, NULL);

    if (!hge(h, RPMTAG_PROVIDENAME, &pnt, (void **) &provides, &providesCount))
    {
	providesEVR = hfd(providesEVR, pvt);
	return 0;	/* XXX should never happen */
    }

    result = 0;
    for (i = 0; i < providesCount; i++) {

	/* Filter out provides that came along for the ride. */
	if (strcmp(provides[i], reqName))
	    continue;

	if (!(provideFlags[i] & RPMSENSE_SENSEMASK))
	    provideFlags[i] |= RPMSENSE_EQUAL; /* ALT21-139-g6cb9a9a */
	result = rpmRangesOverlap(provides[i], providesEVR[i], provideFlags[i],
			reqName, reqEVR, reqFlags);

	/* If this provide matches the require, we're done. */
	if (result)
	    break;
    }

    provides = hfd(provides, pnt);
    providesEVR = hfd(providesEVR, pvt);

    return result;
}

int headerMatchesDepFlags(Header h,
		const char * reqName, const char * reqEVR, int reqFlags)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    const char *name, *version, *release, *disttag;
    int_32 * epoch;
    const char *pkgEVR;
    char *p;
    int pkgFlags = RPMSENSE_EQUAL;

    if (!((reqFlags & RPMSENSE_SENSEMASK) && reqEVR && *reqEVR))
	return 1;

    /* Get package information from header */
    (void) headerNVRD(h, &name, &version, &release, &disttag);

    pkgEVR = p = alloca(21 + strlen(version) + 1 + strlen(release) + 1
                        + (disttag ? strlen(disttag) + 1 : 0));
    *p = '\0';
    if (hge(h, RPMTAG_EPOCH, NULL, (void **) &epoch, NULL)) {
	sprintf(p, "%d:", *epoch);
	while (*p != '\0')
	    p++;
    }
    p = stpcpy( stpcpy( stpcpy(p, version) , "-") , release);
    if (disttag)
        (void) stpcpy( stpcpy(p , ":") , disttag);

    return rpmRangesOverlap(name, pkgEVR, pkgFlags, reqName, reqEVR, reqFlags);
}

rpmTransactionSet rpmtransCreateSet(rpmdb rpmdb, const char * rootDir)
{
    rpmTransactionSet ts;
    int rootLen;

    if (!rootDir) rootDir = "";

    ts = xcalloc(1, sizeof(*ts));
    ts->filesystemCount = 0;
    ts->filesystems = NULL;
    ts->di = NULL;
    /*@-assignexpose@*/
    ts->rpmdb = rpmdb;
    /*@=assignexpose@*/
    ts->scriptFd = NULL;
    ts->id = 0;

    ts->numRemovedPackages = 0;
    ts->removedPackages = NULL;

    /* This canonicalizes the root */
    rootLen = strlen(rootDir);
    if (!(rootLen && rootDir[rootLen - 1] == '/')) {
	char * t;

	t = alloca(rootLen + 2);
	*t = '\0';
	(void) stpcpy( stpcpy(t, rootDir), "/");
	rootDir = t;
    }

    ts->rootDir = xstrdup(rootDir);
    ts->currDir = NULL;
    ts->chrootDone = 0;

    alCreate(&ts->addedPackages);
    alCreate(&ts->erasedPackages);

    ts->orderCount = 0;
    ts->order = NULL;

    ts->selinuxEnabled = is_selinux_enabled() > 0;

    return ts;
}

/**
 * Compare removed package instances (qsort/bsearch).
 * @param a		1st instance address
 * @param b		2nd instance address
 * @return		result of comparison
 */
static int intcmp(const void * a, const void * b)	/*@*/
{
    const int * aptr = a;
    const int * bptr = b;
    int rc = (*aptr - *bptr);
    return rc;
}

/**
 * Add removed package instance to ordered transaction set.
 * @param ts		transaction set
 * @param dboffset	rpm database instance
 * @param depends	installed package of pair (or -1 on erase)
 * @return		0 on success
 */
static int removePackage(rpmTransactionSet ts, int dboffset, int depends)
	/*@modifies ts @*/
{

    /* Filter out duplicate erasures. */
    if (ts->numRemovedPackages > 0 && ts->removedPackages != NULL) {
	if (bsearch(&dboffset, ts->removedPackages, ts->numRemovedPackages,
			sizeof(int), intcmp) != NULL)
	    return 0;
    }

    /* Fetch header. */
    rpmdbMatchIterator mi = rpmdbInitIterator(ts->rpmdb,
	    RPMDBI_PACKAGES, &dboffset, sizeof(dboffset));
    Header h = rpmdbNextIterator(mi);
    if (h)
	h = headerLink(h);
    mi = rpmdbFreeIterator(mi);
    if (h == NULL)
	return 1;

    struct availablePackage *alp =
	    alAddPackage(&ts->erasedPackages, h, NULL, NULL, NULL);
    int alNum = alp - ts->erasedPackages.list;

    AUTO_REALLOC(ts->removedPackages, ts->numRemovedPackages, 8);
    ts->removedPackages[ts->numRemovedPackages++] = dboffset;
    qsort(ts->removedPackages, ts->numRemovedPackages, sizeof(int), intcmp);

    AUTO_REALLOC(ts->order, ts->orderCount, 8);
    transactionElement te = &ts->order[ts->orderCount++];
    te->type = TR_REMOVED;
    te->u.removed.dboffset = dboffset;
    te->u.removed.dependsOnIndex = depends;
    te->u.removed.erasedIndex = alNum;

    return 0;
}

static int rpmDigestCompare(Header first, Header second)
{
    const char * one, * two;

    if (!headerGetEntry(first, RPMTAG_SHA1HEADER, NULL, (void **) &one, NULL))
	one = NULL;
    if (!headerGetEntry(second, RPMTAG_SHA1HEADER, NULL, (void **) &two, NULL))
	two = NULL;

    if (one && two)
	return strcmp(one, two);
    if (one && !two)
	return 1;
    if (!one && two)
	return -1;
    return 0;
}

int rpmtransAddPackage(rpmTransactionSet ts, Header h, FD_t fd,
			const void * key, int upgrade, rpmRelocation * relocs)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    rpmTagType ont, ovt;
    /* this is an install followed by uninstalls */
    const char * name;
    int count;
    const char ** obsoletes;

    /*
     * FIXME: handling upgrades like this is *almost* okay. It doesn't
     * check to make sure we're upgrading to a newer version, and it
     * makes it difficult to generate a return code based on the number of
     * packages which failed.
     */
    struct availablePackage *alp =
	    alAddPackage(&ts->addedPackages, h, key, fd, relocs);
    int alNum = alp - ts->addedPackages.list;
    AUTO_REALLOC(ts->order, ts->orderCount, 8);
    ts->order[ts->orderCount].type = TR_ADDED;
    ts->order[ts->orderCount++].u.addedIndex = alNum;

    if (!upgrade || ts->rpmdb == NULL)
	return 0;

    /* XXX binary rpms always have RPMTAG_SOURCERPM, source rpms do not */
    if (headerIsEntry(h, RPMTAG_SOURCEPACKAGE))
	return 0;

    (void) headerName(h, &name);

    {	rpmdbMatchIterator mi;
	Header h2;

	mi = rpmdbInitIterator(ts->rpmdb, RPMTAG_NAME, name, 0);
	while((h2 = rpmdbNextIterator(mi)) != NULL) {
	    if (rpmDigestCompare(h, h2) || rpmVersionCompare(h, h2))
		(void) removePackage(ts, rpmdbGetIteratorOffset(mi), alNum);
	}
	mi = rpmdbFreeIterator(mi);
    }

    if (hge(h, RPMTAG_OBSOLETENAME, &ont, (void **) &obsoletes, &count)) {
	const char ** obsoletesEVR;
	int_32 * obsoletesFlags;
	int j;

	(void) hge(h, RPMTAG_OBSOLETEVERSION, &ovt, (void **) &obsoletesEVR,
			NULL);
	(void) hge(h, RPMTAG_OBSOLETEFLAGS, NULL, (void **) &obsoletesFlags,
			NULL);

	for (j = 0; j < count; j++) {

	    /* XXX avoid self-obsoleting packages. */
	    if (!strcmp(name, obsoletes[j]))
		continue;

	  { rpmdbMatchIterator mi;
	    Header h2;

	    mi = rpmdbInitIterator(ts->rpmdb, RPMTAG_NAME, obsoletes[j], 0);

	    (void) rpmdbPruneIterator(mi,
		ts->removedPackages, ts->numRemovedPackages, 1);

	    while((h2 = rpmdbNextIterator(mi)) != NULL) {
		/*
		 * Rpm prior to 3.0.3 does not have versioned obsoletes.
		 * If no obsoletes version info is available, match all names.
		 */
		if (obsoletesEVR == NULL ||
		    headerMatchesDepFlags(h2,
			obsoletes[j], obsoletesEVR[j], obsoletesFlags[j]))
		{
		    (void) removePackage(ts, rpmdbGetIteratorOffset(mi), alNum);
		}
	    }
	    mi = rpmdbFreeIterator(mi);
	  }
	}

	obsoletesEVR = hfd(obsoletesEVR, ovt);
	obsoletes = hfd(obsoletes, ont);
    }

    return 0;
}

int rpmtransRemovePackage(rpmTransactionSet ts, int dboffset)
{
    return removePackage(ts, dboffset, -1);
}

rpmTransactionSet rpmtransFree(rpmTransactionSet ts)
{
    if (ts) {
	alFree(&ts->addedPackages);
	alFree(&ts->erasedPackages);
	ts->di = _free(ts->di);
	ts->removedPackages = _free(ts->removedPackages);
	ts->order = _free(ts->order);
	if (ts->scriptFd != NULL)
	    ts->scriptFd =
		fdFree(ts->scriptFd, "rpmtransSetScriptFd (rpmtransFree");
	ts->rootDir = _free(ts->rootDir);
	ts->currDir = _free(ts->currDir);

	ts = _free(ts);
    }
    return NULL;
}

rpmDependencyConflict rpmdepFreeConflicts(rpmDependencyConflict conflicts,
		int numConflicts)
{
    int i;

    if (conflicts)
    for (i = 0; i < numConflicts; i++) {
	conflicts[i].byHeader = headerFree(conflicts[i].byHeader);
	conflicts[i].byName = _free(conflicts[i].byName);
	conflicts[i].byVersion = _free(conflicts[i].byVersion);
	conflicts[i].byRelease = _free(conflicts[i].byRelease);
	conflicts[i].needsName = _free(conflicts[i].needsName);
	conflicts[i].needsVersion = _free(conflicts[i].needsVersion);
    }

    return (conflicts = _free(conflicts));
}

static __thread
hashTable dbProvCache;

/* Cached rpmdb provide lookup, returns 0 if satisfied, 1 otherwise */
static
int dbSatisfiesDepend(rpmTransactionSet ts,
		const char * keyName, const char * keyEVR, int keyFlags)
{
    rpmdbMatchIterator mi;
    Header h;
    int rc = 1;

    /* Lookup dbProvCache by keyName. */
    const void ** cacheData = NULL;
    if (htGetEntry(dbProvCache, keyName, &cacheData, NULL, NULL) == 0) {
	if (*cacheData == NULL)
	    /* cache value is NULL (for "no"), the dependency is not satisfied */
	    return 1;
	if ((keyFlags & RPMSENSE_SENSEMASK) == 0)
	    /* cache value is "yes", unversioned dependency is satisfied */
	    return 0;
    }

    if (*keyName == '/' && (keyFlags & RPMSENSE_SENSEMASK) == 0) {
	mi = rpmdbInitIterator(ts->rpmdb, RPMTAG_BASENAMES, keyName, 0);
	rpmdbPruneIterator(mi, ts->removedPackages, ts->numRemovedPackages, 1);
	while ((h = rpmdbNextIterator(mi)) != NULL) {
	    rc = 0;
	    break;
	}
	mi = rpmdbFreeIterator(mi);
    }

    if (rc == 1) {
	mi = rpmdbInitIterator(ts->rpmdb, RPMTAG_PROVIDENAME, keyName, 0);
	rpmdbPruneIterator(mi, ts->removedPackages, ts->numRemovedPackages, 1);
	while ((h = rpmdbNextIterator(mi)) != NULL) {
	    if (rangeMatchesDepFlags(h, keyName, keyEVR, keyFlags)) {
		rc = 0;
		break;
	    }
	    else {
		/* version did not match */
		rc = -1;
	    }
	}
	mi = rpmdbFreeIterator(mi);
    }

    /* Update dbProvCache.
     * When versions did not match, it is still okay to say "yes" for the name. */
    if (cacheData == NULL)
	/* XXX keyName points to header memory, no need for strdup */
	htAddEntry(dbProvCache, keyName, rc < 1 ? "yes" : NULL);

    return rc ? 1 : 0;
}

/**
 * Check key for an unsatisfied dependency.
 * @todo Eliminate rpmrc provides.
 * @param ts		transaction set
 * @param h		header the dependency comes from
 * @param tag		RPMTAG_REQUIRENAME, PROVIDENAME or OBSOLETENAME
 * @param keyDepend	dependency string representation
 * @param keyName	dependency name string
 * @param keyEVR	dependency [epoch:]version[-release] string
 * @param keyFlags	dependency logical range qualifiers
 * @return		0 if satisfied, 1 if not satisfied, 2 if error
 */
static int tsSatisfiesDepend(rpmTransactionSet ts,
		Header h, rpmTag tag, const char * keyDepend,
		const char * keyName, const char * keyEVR, int keyFlags)
	/*@modifies ts @*/
{
    const char *keyType;
    switch (tag) {
    case RPMTAG_REQUIRENAME:
	keyType = " Requires";
	break;
    case RPMTAG_CONFLICTNAME:
	keyType = " Conflicts";
	break;
    default:
	assert(tag == RPMTAG_OBSOLETENAME);
	keyType = " Obsoletes";
	break;
    }

    /*
     * New features in rpm packaging implicitly add versioned dependencies
     * on rpmlib provides. The dependencies look like "rpmlib(YaddaYadda)".
     * Check those dependencies now.
     */
    if (!strncmp(keyName, "rpmlib(", sizeof("rpmlib(")-1)) {
	if (rpmCheckRpmlibProvides(keyName, keyEVR, keyFlags)) {
	    rpmMessage(RPMMESS_DEBUG, _("%s: %-45s YES (rpmlib provides)\n"),
			keyType, keyDepend+2);
	    return 0;
	}
	goto unsatisfied;
    }

    struct availablePackage **all =
	    alAllSatisfiesDepend(&ts->addedPackages, keyName, keyEVR, keyFlags);
    if (all) {
	int ret = 1;
	if (tag == RPMTAG_REQUIRENAME)
	    ret = 0;
	else {
	    struct availablePackage **alpp;
	    for (alpp = all; *alpp; alpp++) {
		// Conflicts are Obsoletes do not self match.
		if ((*alpp)->h == h)
		    continue;
		// Obsoletes match only against packags names, not Provides.
		if (tag == RPMTAG_OBSOLETENAME && strcmp((*alpp)->name, keyName))
		    continue;
		ret = 0;
		break;
	    }
	}
	all = _free(all);
	if (ret == 0) {
	    rpmMessage(RPMMESS_DEBUG, _("%s: %-45s YES (added provides)\n"),
			    keyType, keyDepend+2);
	    return 0;
	}
    }

    if (dbSatisfiesDepend(ts, keyName, keyEVR, keyFlags) == 0) {
	rpmMessage(RPMMESS_DEBUG, _("%s: %-45s YES (rpmdb provides)\n"),
			keyType, keyDepend+2);
	return 0;
    }

unsatisfied:
    rpmMessage(RPMMESS_DEBUG, _("%s: %-45s NO\n"), keyType, keyDepend+2);
    return 1;
}

/**
 * Check header requires/conflicts against against installed+added packages.
 * @param ts		transaction set
 * @param psp		dependency problems
 * @param h		header to check
 * @param keyName	dependency name
 * @return		0 no problems found
 */
static int checkPackageDeps(rpmTransactionSet ts, problemsSet psp,
		Header h, const char * keyName)
	/*@modifies ts, h, psp */
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    rpmTagType rnt, rvt;
    rpmTagType cnt, cvt;
    const char * name, * version, * release;
    const char ** requires;
    const char ** requiresEVR = NULL;
    int_32 * requireFlags = NULL;
    int requiresCount = 0;
    const char ** conflicts;
    const char ** conflictsEVR = NULL;
    int_32 * conflictFlags = NULL;
    int conflictsCount = 0;
    rpmTagType type;
    int i, rc;
    int ourrc = 0;

    /* FIXME: There is no psp->problems->byDisttag. We don't need it for now. */
    (void) headerNVRD(h, &name, &version, &release, NULL);

    if (!hge(h, RPMTAG_REQUIRENAME, &rnt, (void **) &requires, &requiresCount))
    {
	requiresCount = 0;
	rvt = RPM_STRING_ARRAY_TYPE;
    } else {
	(void)hge(h, RPMTAG_REQUIREFLAGS, NULL, (void **) &requireFlags, NULL);
	(void)hge(h, RPMTAG_REQUIREVERSION, &rvt, (void **) &requiresEVR, NULL);
    }

    for (i = 0; i < requiresCount && !ourrc; i++) {
	const char * keyDepend;

	/* Filter out requires that came along for the ride. */
	if (keyName && strcmp(keyName, requires[i]))
	    continue;

	keyDepend = printDepend("R",
		requires[i], requiresEVR[i], requireFlags[i]);

	rc = tsSatisfiesDepend(ts, h, RPMTAG_REQUIRENAME, keyDepend,
		keyName ?: requires[i], // points to added/erased header memory
		requiresEVR[i], requireFlags[i]);

	switch (rc) {
	case 0:		/* requirements are satisfied. */
	    break;
	case 1:		/* requirements are not satisfied. */
	    rpmMessage(RPMMESS_DEBUG, _("package %s-%s-%s require not satisfied: %s\n"),
		    name, version, release, keyDepend+2);

	    AUTO_REALLOC(psp->problems, psp->num, 8);

	    {	rpmDependencyConflict pp = psp->problems + psp->num;
		pp->byHeader = headerLink(h);
		pp->byName = xstrdup(name);
		pp->byVersion = xstrdup(version);
		pp->byRelease = xstrdup(release);
		pp->needsName = xstrdup(requires[i]);
		pp->needsVersion = xstrdup(requiresEVR[i]);
		pp->needsFlags = requireFlags[i];
		pp->sense = RPMDEP_SENSE_REQUIRES;
	    }

	    psp->num++;
	    break;
	case 2:		/* something went wrong! */
	default:
	    ourrc = 1;
	    break;
	}
	keyDepend = _free(keyDepend);
    }

    if (requiresCount) {
	requiresEVR = hfd(requiresEVR, rvt);
	requires = hfd(requires, rnt);
    }

    if (!hge(h, RPMTAG_CONFLICTNAME, &cnt, (void **)&conflicts, &conflictsCount))
    {
	conflictsCount = 0;
	cvt = RPM_STRING_ARRAY_TYPE;
    } else {
	(void) hge(h, RPMTAG_CONFLICTFLAGS, &type,
		(void **) &conflictFlags, &conflictsCount);
	(void) hge(h, RPMTAG_CONFLICTVERSION, &cvt,
		(void **) &conflictsEVR, &conflictsCount);
    }

    for (i = 0; i < conflictsCount && !ourrc; i++) {
	const char * keyDepend;

	/* Filter out conflicts that came along for the ride. */
	if (keyName && strcmp(keyName, conflicts[i]))
	    continue;

	keyDepend = printDepend("C", conflicts[i], conflictsEVR[i], conflictFlags[i]);

	rc = tsSatisfiesDepend(ts, h, RPMTAG_CONFLICTNAME, keyDepend,
		keyName ?: conflicts[i], // points to added/erased header memory
		conflictsEVR[i], conflictFlags[i]);

	/* 1 == unsatisfied, 0 == satsisfied */
	switch (rc) {
	case 0:		/* conflicts exist. */
	    rpmMessage(RPMMESS_DEBUG, _("package %s conflicts: %s\n"),
		    name, keyDepend+2);

	    AUTO_REALLOC(psp->problems, psp->num, 8);

	    {	rpmDependencyConflict pp = psp->problems + psp->num;
		pp->byHeader = headerLink(h);
		pp->byName = xstrdup(name);
		pp->byVersion = xstrdup(version);
		pp->byRelease = xstrdup(release);
		pp->needsName = xstrdup(conflicts[i]);
		pp->needsVersion = xstrdup(conflictsEVR[i]);
		pp->needsFlags = conflictFlags[i];
		pp->sense = RPMDEP_SENSE_CONFLICTS;
	    }

	    psp->num++;
	    break;
	case 1:		/* conflicts don't exist. */
	    break;
	case 2:		/* something went wrong! */
	default:
	    ourrc = 1;
	    break;
	}
	keyDepend = _free(keyDepend);
    }

    if (conflictsCount) {
	conflictsEVR = hfd(conflictsEVR, cvt);
	conflicts = hfd(conflicts, cnt);
    }

    return ourrc;
}

/**
 * Erasing: check provides key against tag (requires or conflicts) matches.
 * @param ts		transaction set
 * @param psp		dependency problems
 * @param tag		RPMTAG_REQUIRENAME or RPMTAG_CONFLICTNAME
 * @param key		requires name
 * @return		0 no problems found
 */
static int checkDependent(rpmTransactionSet ts, problemsSet psp,
		rpmTag tag, const char * key)
	/*@modifies ts, psp @*/
{
    rpmdbMatchIterator mi = rpmdbInitIterator(ts->rpmdb, tag, key, 0);
    rpmdbPruneIterator(mi, ts->removedPackages, ts->numRemovedPackages, 1);

    Header h;
    int rc = 0;
    while ((h = rpmdbNextIterator(mi)) != NULL) {
	if (checkPackageDeps(ts, psp, h, key)) {
	    rc = 1;
	    break;
	}
    }
    mi = rpmdbFreeIterator(mi);
    return rc;
}

int rpmdepCheck(rpmTransactionSet ts,
		rpmDependencyConflict * conflicts, int * numConflicts)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    struct availablePackage * p;
    problemsSet ps;
    int i, j;
    int rc = 0;

    ps = xcalloc(1, sizeof(*ps));
    ps->num = 0;
    ps->problems = NULL;

    *conflicts = NULL;
    *numConflicts = 0;

    /* XXX figure some kind of heuristic for the cache size */
    dbProvCache = htCreate(1024, hashFunctionString, hashEqualityString);

    /*
     * Look at all of the added packages and make sure their dependencies
     * are satisfied.
     */
    if ((p = ts->addedPackages.list) != NULL)
    for (i = 0; i < ts->addedPackages.size; i++, p++)
    {
        rpmMessage(RPMMESS_DEBUG,  "========== +++ %s-%s-%s\n" ,
		p->name, p->version, p->release);
	rc = checkPackageDeps(ts, ps, p->h, NULL);
	if (rc)
	    goto exit;
	for (j = 0; j < p->providesCount; j++) {
	    /* Adding: check provides key against conflicts matches. */
	    if (!checkDependent(ts, ps, RPMTAG_CONFLICTNAME, p->provides[j]))
		continue;
	    rc = 1;
	    /*@innerbreak@*/ break;
	}
	if (rc)
	    goto exit;
    }

    /*
     * Look at the removed packages and make sure they aren't critical.
     */
    if ((p = ts->erasedPackages.list) != NULL)
    for (i = 0; i < ts->erasedPackages.size; i++, p++)
    {
	rpmMessage(RPMMESS_DEBUG,  "========== --- %s-%s-%s\n" ,
		p->name, p->version, p->release);
	for (j = 0; j < p->providesCount; j++) {
	    /* Erasing: check provides against requiredby matches. */
	    if (!checkDependent(ts, ps, RPMTAG_REQUIRENAME, p->provides[j]))
		continue;
	    rc = 1;
	    /*@innerbreak@*/ break;
	}

	{   const char ** baseNames, ** dirNames;
	    int_32 * dirIndexes;
	    rpmTagType dnt, bnt;
	    int fileCount;
	    char * fileName = NULL;
	    int fileAlloced = 0;
	    int len;
	    Header h = p->h;

	    if (hge(h, RPMTAG_BASENAMES, &bnt, (void **) &baseNames, &fileCount))
	    {
		(void) hge(h, RPMTAG_DIRNAMES, &dnt, (void **) &dirNames, NULL);
		(void) hge(h, RPMTAG_DIRINDEXES, NULL, (void **) &dirIndexes,
				NULL);
		for (j = 0; j < fileCount; j++) {
		    len = strlen(baseNames[j]) + 1 + 
			  strlen(dirNames[dirIndexes[j]]);
		    if (len > fileAlloced) {
			fileAlloced = len * 2;
			fileName = xrealloc(fileName, fileAlloced);
		    }
		    *fileName = '\0';
		    (void) stpcpy( stpcpy(fileName, dirNames[dirIndexes[j]]) , baseNames[j]);
		    /* Erasing: check filename against requiredby matches. */
		    if (!checkDependent(ts, ps, RPMTAG_REQUIRENAME, fileName))
			continue;
		    rc = 1;
		    /*@innerbreak@*/ break;
		}

		fileName = _free(fileName);
		baseNames = hfd(baseNames, bnt);
		dirNames = hfd(dirNames, dnt);
		if (rc)
		    goto exit;
	    }
	}
    }

    if (ps->num) {
	*conflicts = ps->problems;
	ps->problems = NULL;
	*numConflicts = ps->num;
    }
    rc = 0;

exit:
    ps->problems = _free(ps->problems);
    ps = _free(ps);
    dbProvCache = htFree(dbProvCache, NULL, NULL);
    return rc;
}
