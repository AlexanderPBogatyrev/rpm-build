#include "system.h"
#include "rpmlib.h"
#include "debug.h"

#include "rpmmacro.h"		/* XXX for rpmExpand() */

#include "depends.h"
#include "al.h"

/**
 * Recursively mark all nodes with their predecessors.
 * @param tsi		successor chain
 * @param q		predecessor
 */
static void markLoop(/*@special@*/ tsortInfo tsi,
		struct availablePackage * q)
	/*@uses tsi @*/
	/*@modifies internalState @*/
{
    struct availablePackage * p;

    while (tsi != NULL && (p = tsi->tsi_suc) != NULL) {
	tsi = tsi->tsi_next;
	if (p->tsi.tsi_pkg != NULL)
	    continue;
	p->tsi.tsi_pkg = q;
	if (p->tsi.tsi_next != NULL)
	    markLoop(p->tsi.tsi_next, p);
    }
}

static inline /*@observer@*/ const char * identifyDepend(int_32 f)
{
    if (isLegacyPreReq(f))
	return "PreReq:";
    f = _notpre(f);
    if (f & RPMSENSE_SCRIPT_PRE)
	return "Requires(pre):";
    if (f & RPMSENSE_SCRIPT_POST)
	return "Requires(post):";
    if (f & RPMSENSE_SCRIPT_PREUN)
	return "Requires(preun):";
    if (f & RPMSENSE_SCRIPT_POSTUN)
	return "Requires(postun):";
    if (f & RPMSENSE_SCRIPT_VERIFY)
	return "Requires(verify):";
    return "Requires:";
}

/**
 * Find (and eliminate co-requisites) "q <- p" relation in dependency loop.
 * Search all successors of q for instance of p. Format the specific relation,
 * (e.g. p contains "Requires: q"). Unlink and free co-requisite (i.e.
 * pure Requires: dependencies) successor node(s).
 * @param q		sucessor (i.e. package required by p)
 * @param p		predecessor (i.e. package that "Requires: q")
 * @param zap		max. no. of co-requisites to remove (-1 is all)?
 * @retval nzaps	address of no. of relations removed
 * @return		(possibly NULL) formatted "q <- p" releation (malloc'ed)
 */
static /*@owned@*/ /*@null@*/ const char *
zapRelation(struct availablePackage * q, struct availablePackage * p,
		int zap, /*@in@*/ /*@out@*/ int * nzaps)
	/*@modifies q, p, *nzaps @*/
{
    tsortInfo tsi_prev;
    tsortInfo tsi;
    const char *dp = NULL;

    for (tsi_prev = &q->tsi, tsi = q->tsi.tsi_next;
	 tsi != NULL;
	/* XXX Note: the loop traverses "not found", break on "found". */
	/*@-nullderef@*/
	 tsi_prev = tsi, tsi = tsi->tsi_next)
	/*@=nullderef@*/
    {
	int j;

	if (tsi->tsi_suc != p)
	    continue;
	if (p->requires == NULL) continue;	/* XXX can't happen */
	if (p->requireFlags == NULL) continue;	/* XXX can't happen */
	if (p->requiresEVR == NULL) continue;	/* XXX can't happen */

	j = tsi->tsi_reqx;
	dp = printDepend( identifyDepend(p->requireFlags[j]),
		p->requires[j], p->requiresEVR[j], p->requireFlags[j]);

	/*
	 * Attempt to unravel a dependency loop by eliminating Requires's.
	 */
	if (zap && !(p->requireFlags[j] & RPMSENSE_PREREQ)) {
	    /* The above RPMSENSE_PREREQ test works properly only because,
	     * on one hand, rpmbuild marks all scriptlet-like dependencies
	     * with RPMSENSE_PREREQ (see rpmsenseFlags_e in rpmlib.h);
	     * and, on the other, since only %pre/%post dependencies are added
	     * for installed packages, but not %preun/%postun (see below). */
	    rpmMessage(RPMMESS_DEBUG,
			_("removing %s-%s-%s \"%s\" from tsort relations.\n"),
			p->name, p->version, p->release, dp);
	    p->tsi.tsi_count--;
	    if (tsi_prev) tsi_prev->tsi_next = tsi->tsi_next;
	    tsi->tsi_next = NULL;
	    tsi->tsi_suc = NULL;
	    tsi = _free(tsi);
	    if (nzaps)
		(*nzaps)++;
	    if (zap)
		zap--;
	}
	/* XXX Note: the loop traverses "not found", get out now! */
	break;
    }
    return dp;
}

/**
 * Record next "q <- p" relation (i.e. "p" requires "q").
 * @param ts		transaction set
 * @param p		predecessor (i.e. package that "Requires: q")
 * @param selected	boolean package selected array
 * @param j		relation index
 * @return		0 always
 */
static inline int addRelation( const rpmTransactionSet ts,
		struct availablePackage * p, unsigned char * selected, int j)
	/*@modifies p->tsi.tsi_u.count, p->depth, *selected @*/
{
    struct availablePackage * q;
    tsortInfo tsi;
    int matchNum;

    if (!p->requires || !p->requiresEVR || !p->requireFlags)
	return 0;

    q = alSatisfiesDepend(&ts->addedPackages,
		p->requires[j], p->requiresEVR[j], p->requireFlags[j]);

    /* Ordering depends only on added package relations. */
    if (q == NULL)
	return 0;

    /* Avoid rpmlib feature dependencies. */
    if (!strncmp(p->requires[j], "rpmlib(", sizeof("rpmlib(")-1))
	return 0;

    /* Avoid redundant relations. */
    /* XXX TODO: add control bit. */
    matchNum = q - ts->addedPackages.list;
    if (selected[matchNum] != 0)
	return 0;
    selected[matchNum] = 1;

    /* T3. Record next "q <- p" relation (i.e. "p" requires "q"). */
    p->tsi.tsi_count++;			/* bump p predecessor count */
    if (p->depth <= q->depth)		/* Save max. depth in dependency tree */
	p->depth = q->depth + 1;

    tsi = xmalloc(sizeof(*tsi));
    tsi->tsi_suc = p;
    tsi->tsi_reqx = j;
    tsi->tsi_next = q->tsi.tsi_next;
    q->tsi.tsi_next = tsi;
    q->tsi.tsi_qcnt++;			/* bump q successor count */
    return 0;
}

/**
 * Add element to list sorting by initial successor count.
 * @param p		new element
 * @retval qp		address of first element
 * @retval rp		address of last element
 */
static void addQ(struct availablePackage * p,
		/*@in@*/ /*@out@*/ struct availablePackage ** qp,
		/*@in@*/ /*@out@*/ struct availablePackage ** rp)
	/*@modifies p->tsi, *qp, *rp @*/
{
    struct availablePackage *q, *qprev;

    /* Mark the package as queued. */
    p->tsi.tsi_reqx = 1;

    if ((*rp) == NULL) {	/* 1st element */
	(*rp) = (*qp) = p;
	return;
    }
    for (qprev = NULL, q = (*qp); q != NULL; qprev = q, q = q->tsi.tsi_suc) {
	if (q->tsi.tsi_qcnt <= p->tsi.tsi_qcnt)
	    break;
    }
    if (qprev == NULL) {	/* insert at beginning of list */
	p->tsi.tsi_suc = q;
	(*qp) = p;		/* new head */
    } else if (q == NULL) {	/* insert at end of list */
	qprev->tsi.tsi_suc = p;
	(*rp) = p;		/* new tail */
    } else {			/* insert between qprev and q */
	p->tsi.tsi_suc = q;
	qprev->tsi.tsi_suc = p;
    }
}

struct orderListIndex {
    int alIndex;
    int orIndex;
};

/**
 * Compare ordered list entries by index (qsort/bsearch).
 * @param one		1st ordered list entry
 * @param two		2nd ordered list entry
 * @return		result of comparison
 */
static int orderListIndexCmp(const void * one, const void * two)	/*@*/
{
    int a = ((const struct orderListIndex *)one)->alIndex;
    int b = ((const struct orderListIndex *)two)->alIndex;
    return (a - b);
}

int rpmdepOrder(rpmTransactionSet ts)
{
    int npkgs = ts->addedPackages.size;
    int anaconda = 0;
    struct availablePackage * p;
    struct availablePackage * q;
    struct availablePackage * r;
    tsortInfo tsi;
    tsortInfo tsi_next;
    int * ordering = alloca(sizeof(*ordering) * (npkgs + 1));
    int orderingCount = 0;
    unsigned char * selected = alloca(sizeof(*selected) * (npkgs + 1));
    int loopcheck;
    transactionElement newOrder;
    int newOrderCount = 0;
    struct orderListIndex * orderList;
    int _printed = 0;
    int treex;
    int depth;
    int qlen;
    int i, j;

    /* T1. Initialize. */
    loopcheck = npkgs;

    /* Record all relations. */
    rpmMessage(RPMMESS_DEBUG, _("========== recording tsort relations\n"));
    if ((p = ts->addedPackages.list) != NULL)
    for (i = 0; i < npkgs; i++, p++) {
	int matchNum;

	if (p->requiresCount <= 0)
	    continue;

	memset(selected, 0, sizeof(*selected) * npkgs);

	/* Avoid narcisstic relations. */
	matchNum = p - ts->addedPackages.list;
	selected[matchNum] = 1;

	/* T2. Next "q <- p" relation. */

	/* First, do pre-requisites.
	 * This is required for selected[] optimization to work properly. */
	for (j = 0; j < p->requiresCount; j++) {

	    if (p->requireFlags == NULL) continue;	/* XXX can't happen */

	    /* Skip if not %pre/%post requires or legacy prereq. */

	    if (!( isInstallPreReq(p->requireFlags[j]) ||
		   isLegacyPreReq(p->requireFlags[j]) ))
		continue;

	    /* T3. Record next "q <- p" relation (i.e. "p" requires "q"). */
	    (void) addRelation(ts, p, selected, j);

	}

	/* Then do co-requisites. */
	for (j = 0; j < p->requiresCount; j++) {

	    if (p->requireFlags == NULL) continue;	/* XXX can't happen */

	    /* Skip if %pre/%post/%preun/%postun requires or legacy prereq. */

	    if (isErasePreReq(p->requireFlags[j]) ||
		 ( isInstallPreReq(p->requireFlags[j]) ||
		   isLegacyPreReq(p->requireFlags[j]) ))
		continue;

	    /* T3. Record next "q <- p" relation (i.e. "p" requires "q"). */
	    (void) addRelation(ts, p, selected, j);

	}
    }

    /* Save predecessor count and mark tree roots. */
    treex = 0;
    if ((p = ts->addedPackages.list) != NULL)
    for (i = 0; i < npkgs; i++, p++) {
	p->npreds = p->tsi.tsi_count;
	p->tree = (p->npreds == 0 ? treex++ : -1);
    }

    /* T4. Scan for zeroes. */
    rpmMessage(RPMMESS_DEBUG, _("========== tsorting packages (order, #predecessors, #succesors, tree, depth)\n"));

rescan:
    q = r = NULL;
    qlen = 0;
    if ((p = ts->addedPackages.list) != NULL)
    for (i = 0; i < npkgs; i++, p++) {

	/* Prefer packages in chainsaw or anaconda presentation order. */
	if (anaconda)
	    p->tsi.tsi_qcnt = (npkgs - i);

	if (p->tsi.tsi_count != 0)
	    continue;
	p->tsi.tsi_suc = NULL;
	addQ(p, &q, &r);
	qlen++;
    }

    /* T5. Output front of queue (T7. Remove from queue.) */
    for (; q != NULL; q = q->tsi.tsi_suc) {

	/* Mark the package as unqueued. */
	q->tsi.tsi_reqx = 0;

	rpmMessage(RPMMESS_DEBUG, "%5d%5d%5d%5d%5d %*s %s-%s-%s\n",
			orderingCount, q->npreds, q->tsi.tsi_qcnt,
			q->tree, q->depth,
			2*q->depth, "",
			q->name, q->version, q->release);

	treex = q->tree;
	depth = q->depth;
	q->degree = 0;

	ordering[orderingCount++] = q - ts->addedPackages.list;
	qlen--;
	loopcheck--;

	/* T6. Erase relations. */
	tsi_next = q->tsi.tsi_next;
	q->tsi.tsi_next = NULL;
	while ((tsi = tsi_next) != NULL) {
	    tsi_next = tsi->tsi_next;
	    tsi->tsi_next = NULL;
	    p = tsi->tsi_suc;
	    if (p && (--p->tsi.tsi_count) <= 0) {

		p->tree = treex;
		p->depth = depth + 1;
		p->parent = q;
		q->degree++;

		/* XXX TODO: add control bit. */
		p->tsi.tsi_suc = NULL;
		/*@-nullstate@*/	/* FIX: q->tsi.tsi_u.suc may be NULL */
		addQ(p, &q->tsi.tsi_suc, &r);
		/*@=nullstate@*/
		qlen++;
	    }
	    tsi = _free(tsi);
	}
	if (!_printed && loopcheck == qlen && q->tsi.tsi_suc != NULL) {
	    _printed++;
	    rpmMessage(RPMMESS_DEBUG,
		_("========== successors only (presentation order)\n"));

	    /* Relink the queue in presentation order. */
	    tsi = &q->tsi;
	    if ((p = ts->addedPackages.list) != NULL)
	    for (i = 0; i < npkgs; i++, p++) {
		/* Is this element in the queue? */
		if (p->tsi.tsi_reqx == 0)
		    /*@innercontinue@*/ continue;
		tsi->tsi_suc = p;
		tsi = &p->tsi;
	    }
	    tsi->tsi_suc = NULL;
	}
    }

    /* T8. End of process. Check for loops. */
    if (loopcheck != 0) {
	int nzaps;

	/* T9. Initialize predecessor chain. */
	nzaps = 0;
	if ((q = ts->addedPackages.list) != NULL)
	for (i = 0; i < npkgs; i++, q++) {
	    q->tsi.tsi_pkg = NULL;
	    q->tsi.tsi_reqx = 0;
	    /* Mark packages already sorted. */
	    if (q->tsi.tsi_count == 0)
		q->tsi.tsi_count = -1;
	}

	/* T10. Mark all packages with their predecessors. */
	if ((q = ts->addedPackages.list) != NULL)
	for (i = 0; i < npkgs; i++, q++) {
	    if ((tsi = q->tsi.tsi_next) == NULL)
		continue;
	    q->tsi.tsi_next = NULL;
	    markLoop(tsi, q);
	    q->tsi.tsi_next = tsi;
	}

	/* T11. Print all dependency loops. */
	if ((r = ts->addedPackages.list) != NULL)
	for (i = 0; i < npkgs; i++, r++) {
	    int printed;

	    printed = 0;

	    /* T12. Mark predecessor chain, looking for start of loop. */
	    for (q = r->tsi.tsi_pkg; q != NULL; q = q->tsi.tsi_pkg) {
		if (q->tsi.tsi_reqx)
		    /*@innerbreak@*/ break;
		q->tsi.tsi_reqx = 1;
	    }

	    /* T13. Print predecessor chain from start of loop. */
	    while ((p = q) != NULL && (q = p->tsi.tsi_pkg) != NULL) {
		const char * dp;
		char buf[4096];

		/* Unchain predecessor loop. */
		p->tsi.tsi_pkg = NULL;

		if (!printed) {
		    rpmMessage(RPMMESS_DEBUG, _("LOOP:\n"));
		    printed = 1;
		}

		/* Find (and destroy if co-requisite) "q <- p" relation. */
		dp = zapRelation(q, p, 1, &nzaps);

		/* Print next member of loop. */
		sprintf(buf, "%s-%s-%s", p->name, p->version, p->release);
		rpmMessage(RPMMESS_DEBUG, "    %-40s %s\n", buf,
			(dp ? dp : "not found!?!"));

		dp = _free(dp);
	    }

	    /* Walk (and erase) linear part of predecessor chain as well. */
	    for (p = r, q = r->tsi.tsi_pkg;
		 q != NULL;
		 p = q, q = q->tsi.tsi_pkg)
	    {
		/* Unchain linear part of predecessor loop. */
		p->tsi.tsi_pkg = NULL;
		p->tsi.tsi_reqx = 0;
	    }
	}

	/* If a relation was eliminated, then continue sorting. */
	/* XXX TODO: add control bit. */
	if (nzaps) {
	    rpmMessage(RPMMESS_DEBUG, _("========== continuing tsort ...\n"));
	    goto rescan;
	}

	rpmMessage(RPMMESS_ERROR, _("ordering failed, %d elements remain\n"), loopcheck);
	return 1;
    }

    /*
     * The order ends up as installed packages followed by removed packages,
     * with removes for upgrades immediately following the installation of
     * the new package. This would be easier if we could sort the
     * addedPackages array, but we store indexes into it in various places.
     */
    orderList = xmalloc(npkgs * sizeof(*orderList));
    for (i = 0, j = 0; i < ts->orderCount; i++) {
	if (ts->order[i].type == TR_ADDED) {
	    orderList[j].alIndex = ts->order[i].u.addedIndex;
	    orderList[j].orIndex = i;
	    j++;
	}
    }
    assert(j <= npkgs);

    qsort(orderList, npkgs, sizeof(*orderList), orderListIndexCmp);

    newOrder = xmalloc(ts->orderCount * sizeof(*newOrder));
    for (i = 0, newOrderCount = 0; i < orderingCount; i++) {
	struct orderListIndex * needle, key;

	key.alIndex = ordering[i];
	needle = bsearch(&key, orderList, npkgs, sizeof(key),orderListIndexCmp);
	/* bsearch should never, ever fail */
	if (needle == NULL) continue;

	newOrder[newOrderCount++] = ts->order[needle->orIndex];
	for (j = needle->orIndex + 1; j < ts->orderCount; j++) {
	    if (ts->order[j].type == TR_REMOVED &&
		ts->order[j].u.removed.dependsOnIndex == needle->alIndex) {
		newOrder[newOrderCount++] = ts->order[j];
	    } else
		/*@innerbreak@*/ break;
	}
    }

    for (i = 0; i < ts->orderCount; i++) {
	if (ts->order[i].type == TR_REMOVED &&
	    ts->order[i].u.removed.dependsOnIndex == -1)  {
	    newOrder[newOrderCount++] = ts->order[i];
	}
    }
    assert(newOrderCount == ts->orderCount);

    ts->order = _free(ts->order);
    ts->order = newOrder;
    orderList = _free(orderList);

    return 0;
}
