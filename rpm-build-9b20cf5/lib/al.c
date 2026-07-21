#include "system.h"
#include "rpmlib.h"
#include "debug.h"

#include "depends.h"
#include "al.h"

struct alEntry {
    const char *name;
    unsigned int fasthash;
    /* entry-specific members */
};

struct alIndex {
    int sorted;
    int size;
    /* flexible array of entries */
};

static inline
unsigned int fasthash(const char *name)
{
    /* The "fast hash" is used below to avoid extra strcmp calls.  Initially it
     * was just a string length.  To improve the performance without resorting
     * to full-fledged hashing, we now combine string length with its middle
     * character. */
    unsigned int len = strlen(name);
    unsigned char c = name[len >> 1];
    return (len << 8) | c;
}

/**
 * Compare two available index entries by name (qsort/bsearch).
 * @param one		1st available prov entry
 * @param two		2nd available prov entry
 * @return		result of comparison
 */
static inline
int nameCmp(const void * one, const void * two)		/*@*/
{
    const struct alEntry *a = one, *b = two;
    if (a->fasthash > b->fasthash)
	return 1;
    if (a->fasthash < b->fasthash)
	return -1;
    return strcmp(a->name, b->name);
}

static
void *axSearch(void *index, int esize, const char *name, int *nfound)
{
    if (nfound)
	*nfound = 0;

    struct alIndex *ax = index;
    if (ax == NULL)
	return NULL;
    assert(ax->size > 0);

    char *entries = (char *)(ax + 1);
    struct alEntry needle = { name, fasthash(name) };
    if (ax->size == 1) {
	if (nameCmp(entries, &needle))
	    return NULL;
	if (nfound)
	    *nfound = 1;
	return entries;
    }
    if (!ax->sorted) {
	qsort(entries, ax->size, esize, nameCmp);
	ax->sorted = 1;
    }

    char *first, *last;
    first = last = bsearch(&needle, entries, ax->size, esize, nameCmp);
    if (first == NULL)
	return NULL;

    if (nfound) {
	*nfound = 1;

	/* rewind to the first match */
	while (first > entries) {
	    if (nameCmp(first - esize, &needle))
		break;
	    first -= esize;
	    (*nfound)++;
	}

	/* rewind to the last match */
	while (last + esize < entries + esize * ax->size) {
	    if (nameCmp(last + esize, &needle))
		break;
	    last += esize;
	    (*nfound)++;
	}
    }

    return first;
}

static
void *axGrow(void *index, int esize, int more)
{
    struct alIndex *ax = index;
    if (ax) {
	assert(ax->size > 0);
	ax = xrealloc(ax, sizeof(*ax) + esize * (ax->size + more));
    }
    else {
	ax = xmalloc(sizeof(*ax) + esize * more);
	ax->size = 0;
    }
    return ax;
}

/** \ingroup rpmdep
 * A single available item (e.g. a Provides: dependency).
 */
struct alProvEntry {
/*@dependent@*/ const char * name;	/*!< Provides name. */
    unsigned int fasthash;
    int pkgIx;				/*!< Containing package index. */
    int provIx;				/*!< Provides index in package. */
} ;

/** \ingroup rpmdep
 * Index of all available items.
 */
struct alProvIndex {
    int sorted;
    int size;				/*!< No. of available items. */
    struct alProvEntry prov[1];		/*!< Array of available items. */
} ;

static
void alIndexPkgProvides(availableList al, int pkgIx)
{
    const struct availablePackage *alp = &al->list[pkgIx];
    if (alp->providesCount == 0)
	return;

    struct alProvIndex *px = al->provIndex =
	    axGrow(al->provIndex, sizeof(*px->prov), alp->providesCount);

    int provIx;
    for (provIx = 0; provIx < alp->providesCount; provIx++) {
	struct alProvEntry *pe = &px->prov[px->size++];
	pe->name = alp->provides[provIx];
	pe->fasthash = fasthash(pe->name);
	pe->pkgIx = pkgIx;
	pe->provIx = provIx;
    }

    px->sorted = 0;
}

static
struct alProvEntry *alSearchProv(availableList al, const char *name, int *n)
{
    /* first time lookup, create provIndex */
    if (al->provIndex == NULL) {
	int i;
	for (i = 0; i < al->size; i++)
	    alIndexPkgProvides(al, i);
    }
    return axSearch(al->provIndex, sizeof(*al->provIndex->prov), name, n);
}

static
void alFreeProvIndex(availableList al)
{
    al->provIndex = _free(al->provIndex);
}

/** \ingroup rpmdep
 * A file to be installed/removed.
 */
struct alFileEntry {
    const char *basename;		/*!< File basename. */
    unsigned int fasthash;
    int pkgIx;				/*!< Containing package number. */
};

struct alFileIndex {
    int sorted;
    int size;
    struct alFileEntry files[1];
};

/** \ingroup rpmdep
 * A directory which contains some files.
 */
struct alDirEntry {
    const char *dirname;		/*!< Directory path (+ trailing '/'). */
    unsigned int fasthash;
    struct alFileIndex *fx;		/*!< Files index this directory. */
};

struct alDirIndex {
    int sorted;
    int size;
    struct alDirEntry dirs[1];
};

static
void alIndexPkgFiles(availableList al, int pkgIx)
{
    const struct availablePackage *alp = &al->list[pkgIx];
    if (alp->filesCount == 0)
	return;

    const HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    const HFD_t hfd = headerFreeData;
    const char **bn = NULL, **dn = NULL;
    const int *di = NULL;
    rpmTagType bnt = 0, dnt = 0, dit = 0;
    int bnc = 0, dnc = 0, dic = 0;
    if (!hge(alp->h, RPMTAG_BASENAMES, &bnt, (void**)&bn, &bnc))
	goto exit;
    if (!hge(alp->h, RPMTAG_DIRNAMES, &dnt, (void**)&dn, &dnc))
	goto exit;
    if (!hge(alp->h, RPMTAG_DIRINDEXES, &dit, (void**)&di, &dic))
	goto exit;
    if (bnc != dic)
	goto exit;

    /* XXX FIXME: We ought to relocate the directory list here */

    struct alDirIndex *dx = al->dirIndex =
	    axGrow(al->dirIndex, sizeof(*dx->dirs), dnc);

    int i = 0;
    while (i < bnc) {
	/* maybe a few files under the same dir */
	int j = i;
	while (j + 1 < bnc) {
	    if (di[i] != di[j + 1])
		break;
	    j++;
	}
	/* find or create dir entry */
	const char *d = dn[di[i]];
	struct alDirEntry *de = (dx->size == 0) ? NULL :
		axSearch(dx, sizeof(*dx->dirs), d, NULL);
	if (de == NULL) {
	    de = &dx->dirs[dx->size++];
	    de->dirname = d;
	    de->fasthash = fasthash(d);
	    de->fx = NULL;
	    dx->sorted = 0;
	}
	struct alFileIndex *fx = de->fx =
		axGrow(de->fx, sizeof(*fx->files), j - i + 1);
	while (i <= j) {
	    /* add file entries */
	    const char *b = bn[i++];
	    struct alFileEntry *fe = &fx->files[fx->size++];
	    fe->basename = b;
	    fe->fasthash = fasthash(b);
	    fe->pkgIx = pkgIx;
	}
	fx->sorted = 0;
    }

exit:
    /* XXX strings point to header memory */
    bn = hfd(bn, bnt);
    dn = hfd(dn, dnt);
    di = hfd(di, dit);
}

static
struct alFileEntry *alSearchFile(availableList al, const char *fname, int *n)
{
    /* first time lookup, create dirIndex */
    if (al->dirIndex == NULL) {
	int i;
	for (i = 0; i < al->size; i++)
	    alIndexPkgFiles(al, i);
    }

    /* need to preserve trailing slahs in d */
    const char *b = strrchr(fname, '/') + 1;
    int dlen = b - fname;
    char *d = alloca(dlen + 1);
    memcpy(d, fname, dlen);
    d[dlen] = '\0';

    struct alDirEntry *de = axSearch(al->dirIndex, sizeof(*de), d, NULL);
    if (de == NULL) {
	*n = 0;
	return NULL;
    }
    assert(de->fx);
    return axSearch(de->fx, sizeof(*de->fx->files), b, n);
}

static
void alFreeDirIndex(availableList al)
{
    struct alDirIndex *dx = al->dirIndex;
    if (dx) {
	int i;
	for (i = 0; i < dx->size; i++) {
	    struct alDirEntry *de = &dx->dirs[i];
	    de->fx = _free(de->fx);
	}
	al->dirIndex = _free(al->dirIndex);
    }
}

struct availablePackage **
alAllSatisfiesDepend(const availableList al,
		const char * keyName, const char * keyEVR, int keyFlags)
{
    struct availablePackage ** ret = NULL;
    int found = 0;
    int i, n;

    if (*keyName == '/' && (keyFlags & RPMSENSE_SENSEMASK) == 0) {
	const struct alFileEntry *fe = alSearchFile(al, keyName, &n);
	for (i = 0; fe && i < n; i++, fe++) {
	    struct availablePackage *alp = &al->list[fe->pkgIx];
	    int j, already = 0;
	    for (j = 0; j < found; j++)
		if (ret[j] == alp) {
		    already = 1;
		    break;
		}
	    if (already)
		continue;
	    ret = xrealloc(ret, (found + 2) * sizeof(*ret));
	    ret[found++] = alp;
	}
    }

    const struct alProvEntry *pe = alSearchProv(al, keyName, &n);
    for (i = 0; pe && i < n; i++, pe++) {
	struct availablePackage *alp = &al->list[pe->pkgIx];
	int j, already = 0;
	for (j = 0; j < found; j++)
	    if (ret[j] == alp) {
		already = 1;
		break;
	    }
	if (already)
	    continue;
	if ((keyFlags & RPMSENSE_SENSEMASK)) {
	    const char *provName = pe->name;
	    const char *provEVR = alp->providesEVR ?
		    alp->providesEVR[pe->provIx] : NULL;
	    int provFlags = alp->provideFlags ?
		    alp->provideFlags[pe->provIx] : 0;
	    if (!(provFlags & RPMSENSE_SENSEMASK))
		provFlags |= RPMSENSE_EQUAL; /* ALT21-139-g6cb9a9a */
	    int rc = rpmRangesOverlap(provName, provEVR, provFlags,
				    keyName, keyEVR, keyFlags);
	    if (rc == 0)
		continue;
	}
	ret = xrealloc(ret, (found + 2) * sizeof(*ret));
	ret[found++] = alp;
    }

    if (ret)
	ret[found] = NULL;
    return ret;
}

struct availablePackage *
alAddPackage(availableList al,
		Header h, /*@null@*/ /*@dependent@*/ const void * key,
		/*@null@*/ FD_t fd, /*@null@*/ rpmRelocation * relocs)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    struct availablePackage * p;
    rpmRelocation * r;
    int i;
    int pkgNum;

    AUTO_REALLOC(al->list, al->size, 8);
    pkgNum = al->size++;
    p = al->list + pkgNum;
    p->h = headerLink(h);	/* XXX reference held by transaction set */
    p->depth = p->npreds = 0;
    memset(&p->tsi, 0, sizeof(p->tsi));

    (void) headerNVRD(p->h, &p->name, &p->version, &p->release, &p->disttag);

    if (!hge(h, RPMTAG_EPOCH, NULL, (void **) &p->epoch, NULL))
	p->epoch = NULL;

    if (!hge(h, RPMTAG_BUILDTIME, NULL, (void **) &p->buildtime, NULL))
	p->buildtime = NULL;

    if (!hge(h, RPMTAG_PROVIDENAME, NULL, (void **) &p->provides,
	&p->providesCount)) {
	p->providesCount = 0;
	p->provides = NULL;
	p->providesEVR = NULL;
	p->provideFlags = NULL;
    } else {
	if (!hge(h, RPMTAG_PROVIDEVERSION,
			NULL, (void **) &p->providesEVR, NULL))
	    p->providesEVR = NULL;
	if (!hge(h, RPMTAG_PROVIDEFLAGS,
			NULL, (void **) &p->provideFlags, NULL))
	    p->provideFlags = NULL;
    }

    if (!hge(h, RPMTAG_REQUIRENAME, NULL, (void **) &p->requires,
	&p->requiresCount)) {
	p->requiresCount = 0;
	p->requires = NULL;
	p->requiresEVR = NULL;
	p->requireFlags = NULL;
    } else {
	if (!hge(h, RPMTAG_REQUIREVERSION,
			NULL, (void **) &p->requiresEVR, NULL))
	    p->requiresEVR = NULL;
	if (!hge(h, RPMTAG_REQUIREFLAGS,
			NULL, (void **) &p->requireFlags, NULL))
	    p->requireFlags = NULL;
    }

    if (!hge(h, RPMTAG_BASENAMES, NULL, NULL, &p->filesCount))
	p->filesCount = 0;

    p->key = key;
    p->fd = (fd != NULL ? fdLink(fd, "alAddPackage") : NULL);

    if (relocs) {
	for (i = 0, r = relocs; r->oldPath || r->newPath; i++, r++)
	    {};
	p->relocs = xmalloc((i + 1) * sizeof(*p->relocs));

	for (i = 0, r = relocs; r->oldPath || r->newPath; i++, r++) {
	    p->relocs[i].oldPath = r->oldPath ? xstrdup(r->oldPath) : NULL;
	    p->relocs[i].newPath = r->newPath ? xstrdup(r->newPath) : NULL;
	}
	p->relocs[i].oldPath = NULL;
	p->relocs[i].newPath = NULL;
    } else {
	p->relocs = NULL;
    }

    /* Only update the index if it is already created.
     * Otherwise, the index will be constructed upon the first time lookup. */
    if (al->provIndex)
	alIndexPkgProvides(al, pkgNum);
    if (al->dirIndex)
	alIndexPkgFiles(al, pkgNum);

    return p;
}

void alFree(availableList al)
{
    HFD_t hfd = headerFreeData;
    struct availablePackage * p;
    rpmRelocation * r;
    int i;

    if ((p = al->list) != NULL)
    for (i = 0; i < al->size; i++, p++) {

	{   tsortInfo tsi;
	    while ((tsi = p->tsi.tsi_next) != NULL) {
		p->tsi.tsi_next = tsi->tsi_next;
		tsi->tsi_next = NULL;
		tsi = _free(tsi);
	    }
	}

	p->provides = hfd(p->provides, -1);
	p->providesEVR = hfd(p->providesEVR, -1);
	p->requires = hfd(p->requires, -1);
	p->requiresEVR = hfd(p->requiresEVR, -1);
	p->h = headerFree(p->h);

	if (p->relocs) {
	    for (r = p->relocs; (r->oldPath || r->newPath); r++) {
		r->oldPath = _free(r->oldPath);
		r->newPath = _free(r->newPath);
	    }
	    p->relocs = _free(p->relocs);
	}
	if (p->fd != NULL)
	    p->fd = fdFree(p->fd, "alAddPackage (alFree)");
    }

    al->list = _free(al->list);
    alFreeProvIndex(al);
    alFreeDirIndex(al);
}
