/*
 * interdep.c - inter-package analysis and optimizations based on
 * strict dependencies between subpackages (Requires: N = [E:]V-R[:D]).
 *
 * Written by Alexey Tourbin <at@altlinux.org>.
 * License: GPLv2+.
 */

#include "system.h"
#include "psm.h" // TFI_t
#include "rpmbuild.h"
#include "interdep.h"

static
const char *pkgName(Package pkg)
{
    TFI_t fi = pkg->cpioList;
    if (fi)
	return fi->name;
    const char *name;
    headerName(pkg->header, &name);
    return name;
}

static
const char *skipPrefixDash(const char *str, const char *prefix)
{
    int len = strlen(prefix);
    if (strncmp(str, prefix, len))
	return NULL;
    if (str[len] != '-')
	return NULL;
    return str + len + 1;
}

struct Req {
    int c;
    struct Pair {
	Package pkg1;
	Package pkg2;
    } *v;
};

static
int Requires(struct Req *r, Package pkg1, Package pkg2)
{
    int i;
    for (i = 0; i < r->c; i++)
	if (pkg1 == r->v[i].pkg1 && pkg2 == r->v[i].pkg2)
	    return 1;
    return 0;
}

static
int addRequires(struct Req *r, Package pkg1, Package pkg2)
{
    if (pkg1 == pkg2)
	return 0;
    if (Requires(r, pkg1, pkg2))
	return 0;
    AUTO_REALLOC(r->v, r->c, 8);
    r->v[r->c++] = (struct Pair) { pkg1, pkg2 };
    return 1;
}

static void
propagateRequires(struct Req *r)
{
    unsigned propagated;
    do {
	propagated = 0;
	int i1, i2;
	for (i1 = 0; i1 < r->c; i1++)
	    for (i2 = i1; i2 < r->c; i2++) {
		struct Pair r1 = r->v[i1];
		struct Pair r2 = r->v[i2];
		if (r1.pkg2 == r2.pkg1 && addRequires(r, r1.pkg1, r2.pkg2))
		    propagated = 1;
		if (r2.pkg2 == r1.pkg1 && addRequires(r, r2.pkg1, r1.pkg2))
		    propagated = 1;
	    }
    }
    while (propagated);
}

static void
addDeps1(struct Req *r, Package pkg1, Package pkg2)
{
    const char *name = pkgName(pkg2);
    const char *evrd = headerSprintf(pkg2->header,
	    "%|epoch?{%{epoch}:}|%{version}-%{release}%|disttag?{:%{disttag}}|",
	    rpmTagTable, rpmHeaderFormats, NULL);
    assert(evrd);
    int flags = RPMSENSE_EQUAL | RPMSENSE_FIND_REQUIRES;
    int added = !addReqProv(NULL, pkg1->header, flags, name, evrd, 0);
    if (added) {
        if (addRequires(r, pkg1, pkg2))
            propagateRequires(r);
	rpmMessage(RPMMESS_NORMAL, "Adding to %s a strict dependency on %s\n",
		   pkgName(pkg1), pkgName(pkg2));
    }
    evrd = _free(evrd);
}

static
void makeReq1(struct Req *r, Package pkg1, Package pkg2, int warn)
{
    int c = 0;
    const char **reqNv = NULL;
    const char **reqVv = NULL;
    const int *reqFv = NULL;
    const HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    int ok =
       hge(pkg1->header, RPMTAG_REQUIRENAME, NULL, (void **) &reqNv, &c) &&
       hge(pkg1->header, RPMTAG_REQUIREVERSION, NULL, (void **) &reqVv, NULL) &&
       hge(pkg1->header, RPMTAG_REQUIREFLAGS, NULL, (void **) &reqFv, NULL);
    if (!ok)
	return;
    const char *provN, *provV, *provR, *provD;
    headerNVRD(pkg2->header, &provN, &provV, &provR, &provD);
    int i;
    for (i = 0; i < c; i++) {
	if (strcmp(reqNv[i], provN))
	    continue;
	if ((reqFv[i] & RPMSENSE_SENSEMASK) != RPMSENSE_EQUAL)
	    continue;
	const char *reqVR = reqVv[i];
	if (*reqVR == '\0')
	    continue;
	const char *colon = NULL;
	const char *reqR = skipPrefixDash(reqVR, provV);
	if (reqR == NULL && xisdigit(*reqVR)) {
	    colon = strchr(reqVR, ':');
	    if (colon) {
		/* reqVR contains an epoch */
		reqR = skipPrefixDash(colon + 1, provV);
	    }
	}
	if (reqR == NULL)
	    continue;
	const char *reqD = NULL;
	if (strcmp(reqR, provR)) {
	    /* Check whether reqR contains a disttag. */
	    reqD = strchr(reqR, ':');
	    if (!reqD)
		continue;
	    if (strncmp(provR, reqR, reqD - reqR)
		|| provR[reqD - reqR])
		continue;
	    reqD++;
	    if (provD && strcmp(provD, reqD))
		continue;
	}
	if (warn && colon == NULL && headerIsEntry(pkg2->header, RPMTAG_EPOCH)) {
	    rpmMessage(RPMMESS_WARNING, "%s: dependency on %s needs Epoch\n",
		       pkgName(pkg1), pkgName(pkg2));
	    addDeps1(r, pkg1, pkg2);
	} else if (warn && provD && !reqD) {
	    rpmMessage(RPMMESS_DEBUG, "%s: dependency on %s needs disttag\n",
		       pkgName(pkg1), pkgName(pkg2));
	    addDeps1(r, pkg1, pkg2);
	} else {
	    addRequires(r, pkg1, pkg2);
	}
	break;
    }
    const HFD_t hfd = (HFD_t) headerFreeData;
    reqNv = hfd(reqNv, RPM_STRING_ARRAY_TYPE);
    reqVv = hfd(reqVv, RPM_STRING_ARRAY_TYPE);
}

#include "al.h"

// check if pkg1 has (possibly non-strict) dependency on pkg2
static
int depRequires(Package pkg1, Package pkg2)
{
    if (pkg1 == pkg2)
	return 0;
    int reqc = 0;
    const char **reqNv = NULL;
    const char **reqVv = NULL;
    const int *reqFv = NULL;
    const HGE_t hge = (HGE_t) headerGetEntryMinMemory;
    int ok =
       hge(pkg1->header, RPMTAG_REQUIRENAME, NULL, (void **) &reqNv, &reqc) &&
       hge(pkg1->header, RPMTAG_REQUIREVERSION, NULL, (void **) &reqVv, NULL) &&
       hge(pkg1->header, RPMTAG_REQUIREFLAGS, NULL, (void **) &reqFv, NULL);
    if (!ok)
	return 0;
    struct availableList_s proval;
    alCreate(&proval);
    alAddPackage(&proval, pkg2->header, NULL, NULL, NULL);
    int i;
    struct availablePackage *ap = NULL;
    for (i = 0; i < reqc; i++) {
	ap = alSatisfiesDepend(&proval, reqNv[i], reqVv[i], reqFv[i]);
	if (ap)
	    break;
    }
    const HFD_t hfd = (HFD_t) headerFreeData;
    reqNv = hfd(reqNv, RPM_STRING_ARRAY_TYPE);
    reqVv = hfd(reqVv, RPM_STRING_ARRAY_TYPE);
    alFree(&proval);
    return ap ? 1 : 0;
}

static
struct Req *freeRequires(struct Req *r)
{
    r->v = _free(r->v);
    return _free(r);
}

/*
 * For every subpackage pkg2 in the set, pkg1 will get a strict dep
 * on pkg2 if at least one of these conditions is met:
 * - pkg1 has a RPMSENSE_FIND_REQUIRES dep on pkg2;
 * - pkg1 has a manual dep on pkg2 and
 *   this dep has no ~RPMSENSE_EQUAL sense flags;
 * - pkg1 has a RPMSENSE_FIND_REQUIRES dep on X, and
 *   pkg2 is the only subpackage in the set that satisfies X.
 */
static void
fix_weak_deps(struct Req *r, Package pkg1, Package packages)
{
    int reqc = 0;
    const char **reqNv = NULL;
    const char **reqVv = NULL;
    const int *reqFv = NULL;
    const HGE_t hge = (HGE_t) headerGetEntryMinMemory;
    int ok =
       hge(pkg1->header, RPMTAG_REQUIRENAME, NULL, (void **) &reqNv, &reqc) &&
       hge(pkg1->header, RPMTAG_REQUIREVERSION, NULL, (void **) &reqVv, NULL) &&
       hge(pkg1->header, RPMTAG_REQUIREFLAGS, NULL, (void **) &reqFv, NULL);
    if (!ok)
	return;
    int i;
    Package pkg2;
    for (i = 0, pkg2 = packages; pkg2; ++i, pkg2 = pkg2->next)
	    ;;
    Package *provs = xcalloc(i, sizeof(*provs));
    availableList proval = xcalloc(i, sizeof(*proval));
    for (i = 0, pkg2 = packages; pkg2; ++i, pkg2 = pkg2->next) {
	if (pkg1 != pkg2 && pkg2->cpioList) {
	    alCreate(&proval[i]);
	    alAddPackage(&proval[i], pkg2->header, NULL, NULL, NULL);
	}
    }
    for (i = 0; i < reqc; ++i) {
	if (reqFv[i] & RPMSENSE_SENSEMASK & ~RPMSENSE_EQUAL &&
	    !(reqFv[i] & RPMSENSE_FIND_REQUIRES))
	    continue;
	int j;
	for (j = 0, pkg2 = packages; pkg2; ++j, pkg2 = pkg2->next) {
	    if (pkg1 != pkg2 && pkg2->cpioList && !strcmp(reqNv[i], pkgName(pkg2))) {
		provs[j] = pkg2;
		break;
	    }
	}
	int k;
	Package prov = NULL;
	for (j = 0, pkg2 = packages; pkg2; ++j, pkg2 = pkg2->next) {
	    if (pkg1 != pkg2 && pkg2->cpioList) {
		if (alSatisfiesDepend(&proval[j], reqNv[i], reqVv[i], reqFv[i])) {
		    if (prov) {
			prov = NULL;
			break;
		    }
		    prov = pkg2;
		    k = j;
		}
	    }
	}
	if (prov && reqFv[i] & RPMSENSE_FIND_REQUIRES)
	    provs[k] = prov;
    }
    for (i = 0, pkg2 = packages; pkg2; ++i, pkg2 = pkg2->next) {
	if (pkg1 != pkg2 && pkg2->cpioList)
	    alFree(&proval[i]);
    }
    proval = _free(proval);
    const HFD_t hfd = (HFD_t) headerFreeData;
    reqNv = hfd(reqNv, RPM_STRING_ARRAY_TYPE);
    reqVv = hfd(reqVv, RPM_STRING_ARRAY_TYPE);
    for (i = 0, pkg2 = packages; pkg2; ++i, pkg2 = pkg2->next) {
	if (provs[i])
	    addDeps1(r, pkg1, provs[i]);
    }
    provs = _free(provs);
}

static
struct Req *makeRequires(Spec spec, int warn)
{
    struct Req *r = xcalloc(1, sizeof *r);
    Package pkg1, pkg2;
    for (pkg1 = spec->packages; pkg1; pkg1 = pkg1->next) {
	if (!pkg1->cpioList)
	    continue;
	for (pkg2 = pkg1->next; pkg2; pkg2 = pkg2->next) {
	    if (!pkg2->cpioList)
		continue;
	    makeReq1(r, pkg1, pkg2, warn & 1);
	    makeReq1(r, pkg2, pkg1, warn & 1);
	}
    }
    propagateRequires(r);
    for (pkg1 = spec->packages; pkg1; pkg1 = pkg1->next) {
	if (!pkg1->cpioList)
	    continue;
	fix_weak_deps(r, pkg1, spec->packages);
    }
    if ((warn & 2) == 0)
	return r;

    for (pkg1 = spec->packages; pkg1; pkg1 = pkg1->next) {
	if (!pkg1->cpioList)
	    continue;
	for (pkg2 = pkg1->next; pkg2; pkg2 = pkg2->next) {
	    if (!pkg2->cpioList)
		continue;
	    if (!Requires(r, pkg1, pkg2) && depRequires(pkg1, pkg2))
		rpmMessage(RPMMESS_WARNING, "%s: non-strict dependency on %s\n",
			   pkgName(pkg1), pkgName(pkg2));
	    if (!Requires(r, pkg2, pkg1) && depRequires(pkg2, pkg1))
		rpmMessage(RPMMESS_WARNING, "%s: non-strict dependency on %s\n",
			   pkgName(pkg2), pkgName(pkg1));
	}
    }
    return r;
}

#include "checkFiles.h" // fiIntersect

static
void fiPrune(TFI_t fi, char pruned[])
{
    int *dil_;
    const char **bnl, **dnl;
    int bnc, dnc, dic;
    int ok =
       fi->hge(fi->h, RPMTAG_BASENAMES, NULL, (void **) &bnl, &bnc) &&
       fi->hge(fi->h, RPMTAG_DIRNAMES, NULL, (void **) &dnl, &dnc) &&
       fi->hge(fi->h, RPMTAG_DIRINDEXES, NULL, (void **) &dil_, &dic);
    assert(ok);
    assert(fi->fc == bnc);
    assert(bnc == dic);
    int i, j, k;
    // dil must be copied, cf. relocateFileList
    int dil[dic];
    for (i = 0; i < dic; i++)
	dil[i] = dil_[i];
    // mark used dirnames
    int dirused[dnc];
    bzero(dirused, dnc * sizeof *dirused);
    for (i = 0; i < bnc; i++)
	if (!pruned[i])
	    dirused[dil[i]]++;
    int propagated;
    do {
	propagated = 0;
	// for each unused dirname
	for (i = 0; i < dnc; i++) {
	    if (dirused[i])
		continue;
	    // find its corresponding parent_dir+name entry
	    for (j = 0; j < bnc; j++) {
		if (pruned[j])
		    continue;
		const char *D = dnl[i];
		const char *d = dnl[dil[j]];
		int dlen = strlen(d);
		if (strncmp(D, d, dlen))
		    continue;
		const char *b = bnl[j];
		int blen = strlen(b);
		if (strncmp(D + dlen, b, blen))
		    continue;
		if (strncmp(D + dlen + blen, "/", 2))
		    continue;
		// makr parent_dir+name for removal
		rpmMessage(RPMMESS_NORMAL, "also prunning dir %s%s\n", d, b);
		pruned[j] = 1;
		// decrement parent_dir usage
		if (--dirused[dil[j]] == 0)
		    propagated++;
	    }
	}
    }
    while (propagated);
    // new count for bnc-like values
    int oldc = bnc;
    int newc = 0;
    for (i = 0; i < oldc; i++)
	if (!pruned[i])
	    newc++;
    // establish new dirnames and dirindexes
    for (i = 0, j = 0; i < dnc; i++) {
	if (!dirused[i])
	    continue;
	if (i == j)
	    goto skip;
	dnl[j] = dnl[i];
	for (k = 0; k < dic; k++)
	    if (dil[k] == i)
		dil[k] = j;
    skip:
	j++;
    }
    dnc = j;
    // handle bnl, dnl and dil
#define PruneV(v) \
    for (i = 0, j = 0; i < oldc; i++) \
	if (!pruned[i]) \
	    v[j++] = v[i]
    PruneV(bnl);
    PruneV(dil);
    PruneV(fi->bnl);
    PruneV(fi->dil);
    fi->hme(fi->h, RPMTAG_BASENAMES, RPM_STRING_ARRAY_TYPE, bnl, newc);
    fi->hme(fi->h, RPMTAG_DIRNAMES, RPM_STRING_ARRAY_TYPE, dnl, dnc);
    fi->hme(fi->h, RPMTAG_DIRINDEXES, RPM_INT32_TYPE, dil, newc);
    bnl = fi->hfd(bnl, RPM_STRING_ARRAY_TYPE);
    dnl = fi->hfd(dnl, RPM_STRING_ARRAY_TYPE);
    // prune header tags
    rpmTagType tagt;
    int tagc;
    const char **strv;
#define PruneStrTag(tag) \
    if (fi->hge(fi->h, tag, &tagt, (void **) &strv, &tagc)) { \
	assert(tagt == RPM_STRING_ARRAY_TYPE); \
	assert(tagc == oldc); \
	PruneV(strv); \
	fi->hme(fi->h, tag, RPM_STRING_ARRAY_TYPE, strv, newc); \
	fi->hfd(strv, RPM_STRING_ARRAY_TYPE); \
    }
    short *INT16p, INT16v[oldc];
    int *INT32p, INT32v[oldc];
#define PruneIntTag(INT, tag) \
    if (fi->hge(fi->h, tag, &tagt, (void **) &INT ## p, &tagc)) { \
	assert(tagt == RPM_ ## INT ## _TYPE); \
	assert(tagc == oldc); \
	for (i = 0; i < oldc; i++) \
	    INT ## v[i] = INT ## p[i]; \
	PruneV(INT ## v); \
	fi->hme(fi->h, tag, RPM_ ## INT ##_TYPE, INT ## v, newc); \
    }
#define PruneI16Tag(tag) PruneIntTag(INT16, tag)
#define PruneI32Tag(tag) PruneIntTag(INT32, tag)
    PruneI32Tag(RPMTAG_FILESIZES);
    PruneStrTag(RPMTAG_FILEUSERNAME);
    PruneStrTag(RPMTAG_FILEGROUPNAME);
    PruneI32Tag(RPMTAG_FILEMTIMES);
    PruneI16Tag(RPMTAG_FILEMODES);
    PruneI16Tag(RPMTAG_FILERDEVS);
    PruneI32Tag(RPMTAG_FILEDEVICES);
    PruneI32Tag(RPMTAG_FILEINODES);
    PruneStrTag(RPMTAG_FILELANGS);
    PruneStrTag(RPMTAG_FILEMD5S);
    PruneStrTag(RPMTAG_FILELINKTOS);
    PruneI32Tag(RPMTAG_FILEVERIFYFLAGS);
    PruneI32Tag(RPMTAG_FILEFLAGS);
    // update fi, cf. genCpioListAndHeader
    PruneV(fi->apath);
    PruneV(fi->actions);
    PruneV(fi->fmapflags);
    PruneV(fi->fuids);
    PruneV(fi->fgids);
    PruneV(fi->fsts);
    struct transactionFileInfo_s save_fi;
#define MV(a) save_fi.a = fi->a; fi->a = NULL
    MV(bnl); MV(dnl); MV(dil);
    MV(apath); MV(actions); MV(fmapflags); MV(fuids); MV(fgids); MV(fsts);
    save_fi.h = fi->h;
    save_fi.astriplen = fi->astriplen;
    freeFi(fi);
    bzero(fi, sizeof *fi);
    fi->type = TR_ADDED;
    loadFi(save_fi.h, fi);
    assert(fi->fc == newc);
    fi->dnl = _free(fi->dnl);
    fi->bnl = _free(fi->bnl);
#undef MV
#define MV(a) fi->a = save_fi.a
    MV(bnl); MV(dnl); MV(dil);
    MV(apath); MV(actions); MV(fmapflags); MV(fuids); MV(fgids); MV(fsts);
    fi->astriplen = save_fi.astriplen;
}

static
void fiIntersect_cb(const TFI_t fi1, const TFI_t fi2, const char *f,
		    int i1, int i2, void *data)
{
	(void) fi2;
	(void) i2;
	if (S_ISDIR(fi1->fmodes[i1]))
	    return;
	const char src[] = "/usr/src/debug/";
	if (strncmp(f, src, sizeof(src) - 1))
	    return;
	struct {
		unsigned int n;
		char list[fi1->fc];
	} *pruned = data;
	pruned->list[i1] = 1;
	pruned->n++;
    }

// prune src dups from pkg1 and add dependency on pkg2
static
void pruneSrc1(struct Req *r, Package pkg1, Package pkg2)
{
    TFI_t fi1 = pkg1->cpioList;
    const TFI_t fi2 = pkg2->cpioList;
    if (!fi1 || !fi2) return;
    struct {
	    unsigned int n;
	    char list[fi1->fc];
    } pruned;
    bzero(&pruned, sizeof(pruned));
    fiIntersect(fi1, fi2, fiIntersect_cb, &pruned);
    if (pruned.n == 0)
	return;
    addDeps1(r, pkg1, pkg2);
    rpmMessage(RPMMESS_NORMAL, "Removing from %s %d sources provided by %s\n",
	    pkgName(pkg1), pruned.n, pkgName(pkg2));
    fiPrune(fi1, pruned.list);
}

static void
processDependentDebuginfo(struct Req *r, Spec spec,
    void (*cb)(struct Req *r, Package pkg1, Package pkg2), int mutual)
{
    int i1, i2;
    struct Pair r1, r2;
    const char *Nd, *Np;
    const char *suf;
    // r1 = { pkg1-debuginfo, pkg1 }
    for (i1 = 0; i1 < r->c; i1++) {
	r1 = r->v[i1];
	Nd = pkgName(r1.pkg1);
	Np = pkgName(r1.pkg2);
	suf = skipPrefixDash(Nd, Np);
	if (suf == NULL || strcmp(suf, "debuginfo"))
	    continue;
	// r2 = { pkg2-debuginfo, pkg2 }
	for (i2 = i1; i2 < r->c; i2++) {
	    r2 = r->v[i2];
	    Nd = pkgName(r2.pkg1);
	    Np = pkgName(r2.pkg2);
	    suf = skipPrefixDash(Nd, Np);
	    if (suf == NULL || strcmp(suf, "debuginfo"))
		continue;
	    // (pkg1 <-> pkg2) => (pkg1-debuginfo <-> pkg2-debuginfo)
	    if (Requires(r, r1.pkg2, r2.pkg2)) {
		cb(r, r1.pkg1, r2.pkg1);
		if (!mutual)
		    continue;
	    }
	    if (Requires(r, r2.pkg2, r1.pkg2))
		cb(r, r2.pkg1, r1.pkg1);
	}
    }
}

static
void pruneDebuginfoSrc(struct Req *r, Spec spec)
{
    processDependentDebuginfo(r, spec, pruneSrc1, 0);
}

// if pkg1 implicitly requires pkg2, add strict dependency
static void
liftDeps1(struct Req *r, Package pkg1, Package pkg2)
{
    if (!Requires(r, pkg1, pkg2) && depRequires(pkg1, pkg2))
	addDeps1(r, pkg1, pkg2);
}
static
void liftDebuginfoDeps(struct Req *r, Spec spec)
{
    processDependentDebuginfo(r, spec, liftDeps1, 1);
}

// assuming pkg1 has strict dependency on pkg2, prune deps provided by pkg2
static
void pruneDeps1(Package pkg1, Package pkg2)
{
    TFI_t fi = pkg1->cpioList;
    if (fi == NULL)
	return;
    int reqc = 0;
    const char **reqNv = NULL;
    const char **reqVv = NULL;
    const int *reqFv_ = NULL;
    int ok =
       fi->hge(pkg1->header, RPMTAG_REQUIRENAME, NULL, (void **) &reqNv, &reqc) &&
       fi->hge(pkg1->header, RPMTAG_REQUIREVERSION, NULL, (void **) &reqVv, NULL) &&
       fi->hge(pkg1->header, RPMTAG_REQUIREFLAGS, NULL, (void **) &reqFv_, NULL);
    if (!ok)
	return;
    assert(reqc > 0);
    int i, j;
    int reqFv[reqc];
    for (i = 0; i < reqc; i++)
	reqFv[i] = reqFv_[i];
    struct availableList_s proval;
    alCreate(&proval);
    alAddPackage(&proval, pkg2->header, NULL, NULL, NULL);
    int flags = 0, *flagsp = NULL;
    struct availablePackage *ap = NULL;
    int rpmlibSetVersions = -1;
    int hasSetVersions = 0;
    int npruned = 0;
    char pruned[reqc];
    bzero(pruned, reqc);
    for (i = 0; i < reqc; i++) {
	if ((reqFv[i] & RPMSENSE_SENSEMASK) == RPMSENSE_EQUAL &&
	    strcmp(reqNv[i], pkgName(pkg2)) == 0)
	{
	    if (flagsp == NULL)
		flagsp = &reqFv[i];
	    continue;
	}
	if ((reqFv[i] & RPMSENSE_RPMLIB) &&
	    strcmp(reqNv[i], "rpmlib(SetVersions)") == 0)
	{
	    if (rpmlibSetVersions == -1)
		rpmlibSetVersions = i;
	    continue;
	}
	ap = alSatisfiesDepend(&proval, reqNv[i], reqVv[i], reqFv[i]);
	if (ap == NULL) {
	    if ((reqFv[i] & RPMSENSE_SENSEMASK))
		if (strncmp(reqVv[i], "set:", 4) == 0)
		    hasSetVersions++;
	    continue;
	}
	pruned[i] = 1;
	npruned++;
	flags |= reqFv[i];
    }
    alFree(&proval);
    if (npruned == 0) {
	reqNv = fi->hfd(reqNv, RPM_STRING_ARRAY_TYPE);
	reqVv = fi->hfd(reqVv, RPM_STRING_ARRAY_TYPE);
	return;
    }
    if (hasSetVersions == 0 && rpmlibSetVersions >= 0) {
	pruned[rpmlibSetVersions] = 1;
	npruned++;
    }
    rpmMessage(RPMMESS_NORMAL,
	       "Removing %d extra deps from %s due to dependency on %s\n",
	       npruned, pkgName(pkg1), pkgName(pkg2));
    if (flagsp)
	*flagsp |= (flags & RPMSENSE_PREREQ);
    for (i = 0, j = 0; i < reqc; i++) {
	if (pruned[i])
	    continue;
	if (i == j)
	    goto skip;
	reqNv[j] = reqNv[i];
	reqVv[j] = reqVv[i];
	reqFv[j] = reqFv[i];
    skip:
	j++;
    }
    reqc = j;
    fi->hme(fi->h, RPMTAG_REQUIRENAME, RPM_STRING_ARRAY_TYPE, reqNv, reqc);
    fi->hme(fi->h, RPMTAG_REQUIREVERSION, RPM_STRING_ARRAY_TYPE, reqVv, reqc);
    fi->hme(fi->h, RPMTAG_REQUIREFLAGS, RPM_INT32_TYPE, reqFv, reqc);
    reqNv = fi->hfd(reqNv, RPM_STRING_ARRAY_TYPE);
    reqVv = fi->hfd(reqVv, RPM_STRING_ARRAY_TYPE);
}

static
void pruneExtraDeps(struct Req *r, Spec spec)
{
    int i;
    for (i = 0; i < r->c; i++)
	pruneDeps1(r->v[i].pkg1, r->v[i].pkg2);
}

// assuming pkg1 has strict dependency on pkg2, prune deps required by pkg2
static
void pruneRDeps1(struct Req *r, Spec spec, Package pkg1, Package pkg2)
{
    TFI_t fi = pkg1->cpioList;
    if (fi == NULL)
	return;
    int reqc = 0;
    const char **reqNv = NULL;
    const char **reqVv = NULL;
    const int *reqFv_ = NULL;
    int ok =
       fi->hge(fi->h, RPMTAG_REQUIRENAME, NULL, (void **) &reqNv, &reqc) &&
       fi->hge(fi->h, RPMTAG_REQUIREVERSION, NULL, (void **) &reqVv, NULL) &&
       fi->hge(fi->h, RPMTAG_REQUIREFLAGS, NULL, (void **) &reqFv_, NULL);
    if (!ok)
	return;
    int i, j;
    int reqFv[reqc];
    for (i = 0; i < reqc; i++)
	reqFv[i] = reqFv_[i];
    int provc = 0;
    const char **provNv = NULL;
    const char **provVv = NULL;
    const int *provFv = NULL;
    const HGE_t hge = (HGE_t) headerGetEntryMinMemory;
    const HFD_t hfd = (HFD_t) headerFreeData;
    ok =
       hge(pkg2->header, RPMTAG_REQUIRENAME, NULL, (void **) &provNv, &provc) &&
       hge(pkg2->header, RPMTAG_REQUIREVERSION, NULL, (void **) &provVv, NULL) &&
       hge(pkg2->header, RPMTAG_REQUIREFLAGS, NULL, (void **) &provFv, NULL);
    if (!ok) {
	reqNv = fi->hfd(reqNv, RPM_STRING_ARRAY_TYPE);
	reqVv = fi->hfd(reqVv, RPM_STRING_ARRAY_TYPE);
	return;
    }
    int npruned = 0;
    char pruned[reqc];
    bzero(pruned, reqc);
    // check if pkg2 is part of a cycle
    int cycle = 0;
    Package pkg3;
    for (pkg3 = spec->packages; pkg3; pkg3 = pkg3->next) {
	if (pkg3 == pkg1 || pkg3 == pkg2)
	    continue;
	if (Requires(r, pkg2, pkg3) && Requires(r, pkg3, pkg2)) {
	    cycle = 1;
	    break;
	}
    }
    for (i = 0; i < reqc; i++)
	for (j = 0; j < provc; j++) {
	    if (strcmp(reqNv[i], provNv[j]))
		continue;
	    if (cycle && (reqFv[i] & RPMSENSE_SENSEMASK) == RPMSENSE_EQUAL)
		continue;
	    dep_compare_t cmp = compare_deps(RPMTAG_REQUIREFLAGS,
				provVv[j], provFv[j], reqVv[i], reqFv[i]);
	    if (!(cmp == DEP_ST || cmp == DEP_EQ))
		continue;
	    pruned[i] = 1;
	    npruned++;
	}
    if (npruned == 0)
	goto free;
    rpmMessage(RPMMESS_NORMAL,
	       "Removing %d extra deps from %s due to repentancy on %s\n",
	       npruned, pkgName(pkg1), pkgName(pkg2));
    for (i = 0, j = 0; i < reqc; i++) {
	if (pruned[i])
	    continue;
	if (i == j)
	    goto skip;
	reqNv[j] = reqNv[i];
	reqVv[j] = reqVv[i];
	reqFv[j] = reqFv[i];
    skip:
	j++;
    }
    reqc = j;
    fi->hme(fi->h, RPMTAG_REQUIRENAME, RPM_STRING_ARRAY_TYPE, reqNv, reqc);
    fi->hme(fi->h, RPMTAG_REQUIREVERSION, RPM_STRING_ARRAY_TYPE, reqVv, reqc);
    fi->hme(fi->h, RPMTAG_REQUIREFLAGS, RPM_INT32_TYPE, reqFv, reqc);
  free:
    reqNv = fi->hfd(reqNv, RPM_STRING_ARRAY_TYPE);
    reqVv = fi->hfd(reqVv, RPM_STRING_ARRAY_TYPE);
    provNv = hfd(provNv, RPM_STRING_ARRAY_TYPE);
    provVv = hfd(provVv, RPM_STRING_ARRAY_TYPE);
}

static
void pruneExtraRDeps(struct Req *r, Spec spec)
{
    Package pkg1, pkg2;
    for (pkg1 = spec->packages; pkg1; pkg1 = pkg1->next) {
	if (!pkg1->cpioList)
	    continue;
	for (pkg2 = pkg1->next; pkg2; pkg2 = pkg2->next) {
	    if (!pkg2->cpioList)
		continue;
	    if (Requires(r, pkg1, pkg2))
		pruneRDeps1(r, spec, pkg1, pkg2);
	    // "else" guards against mutual deletions
	    else if (Requires(r, pkg2, pkg1))
		pruneRDeps1(r, spec, pkg2, pkg1);
	}
    }
}

int processInterdep(Spec spec)
{
    struct Req *r;
    int optlevel = rpmExpandNumeric("%{?_deps_optimization}%{?!_deps_optimization:3}");
    if (optlevel >= 1) {
	r = makeRequires(spec, 1);
	pruneDebuginfoSrc(r, spec);
	liftDebuginfoDeps(r, spec);
	r = freeRequires(r);
    }
    if (optlevel >= 3) {
	r = makeRequires(spec, 2);
	pruneExtraDeps(r, spec);
	pruneExtraRDeps(r, spec);
	r = freeRequires(r);
    }
    return 0;
}

// ex: set ts=8 sts=4 sw=4 noet:
