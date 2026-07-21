/** \ingroup rpmbuild
 * \file build/reqprov.c
 *  Add dependency tags to package header(s).
 */

#include "system.h"

#include "rpmbuild.h"
#include "debug.h"

static int
deps_opt_enabled (void)
{
	static int enabled = -1;

	if (enabled == -1)
		enabled = rpmExpandNumeric("%{?_deps_optimization}%{?!_deps_optimization:2}") >= 2;

	return enabled;
}

static int
tag_is_reqprov (rpmTag tag)
{
	switch (tag) {
		case RPMTAG_REQUIREFLAGS:
		case RPMTAG_PROVIDEFLAGS:
			return 1;
		default:
			return 0;
	}
}

static dep_compare_t
compare_sense_flags (rpmTag tag, int cmp, int wcmp, rpmsenseFlags a, rpmsenseFlags b)
{
	if (cmp > 0) {
		/* Aevr > Bevr */
		return -compare_sense_flags (tag, -cmp, -wcmp, b, a);
	} else if (cmp == 0) {
		/* Aevr == Bevr */
		if (a == b) {
			if (wcmp == 0)
				return DEP_EQ;
			else if (wcmp < 0)
				return tag_is_reqprov(tag) ? DEP_ST : DEP_WK;
			else
				return tag_is_reqprov(tag) ? DEP_WK : DEP_ST;
		}
		if (a && ((a & b) == a)) {
			/* b contains a */
			/* LT,LE || EQ,LE || EQ,GE || GT,GE */
			if (wcmp <= 0)
				return (tag == RPMTAG_REQUIREFLAGS) ? DEP_ST : DEP_WK;
			if (a == RPMSENSE_EQUAL)
				return DEP_UN;
			return (tag == RPMTAG_REQUIREFLAGS) ? DEP_ST : DEP_WK;
		}
		if (b && ((a & b) == b)) /* a contains b */
			return -compare_sense_flags (tag, -cmp, -wcmp, b, a);
		return DEP_UN;
	}
	/* cmp < 0 => Aevr < Bevr */
	if (a == 0) {
		/* a == 0 && cmp < 0 means b != 0 */
		return tag_is_reqprov(tag) ? DEP_WK : DEP_ST;
	}
	if (tag == RPMTAG_REQUIREFLAGS) {
		/* EQ || LE || LT is stronger than LE || LT */
		if (!(a & RPMSENSE_GREATER) && (b & RPMSENSE_LESS))
			return DEP_ST;
		/* GE || GT is weaker than EQ || GE || GT */
		if ((a & RPMSENSE_GREATER) && !(b & RPMSENSE_LESS))
			return DEP_WK;
		return DEP_UN;
	} else {
		/* GE || GT is stronger than EQ || GE || GT */
		if ((a & RPMSENSE_GREATER) && !(b & RPMSENSE_LESS))
			return DEP_ST;
		/* EQ || LE || LT is weaker than LE || LT */
		if (!(a & RPMSENSE_GREATER) && (b & RPMSENSE_LESS))
			return DEP_WK;
		return DEP_UN;
	}
}

#include "set.h"

dep_compare_t
compare_deps (rpmTag tag, const char *Aevr, rpmsenseFlags Aflags,
			  const char *Bevr, rpmsenseFlags Bflags)
{
	dep_compare_t rc = DEP_UN, cmp_rc;
	rpmsenseFlags Asense, Bsense;
	int sense, wcmp = 0;
	char *aEVR = NULL, *bEVR = NULL;
	const char *aE, *aV, *aR, *aD, *bE, *bV, *bR, *bD;

	/* 1. filter out noise */
	Aflags &= ~(RPMSENSE_FIND_REQUIRES | RPMSENSE_FIND_PROVIDES);
	Bflags &= ~(RPMSENSE_FIND_REQUIRES | RPMSENSE_FIND_PROVIDES);

	/* 2. identical? */
	if (Aflags == Bflags && !strcmp (Aevr, Bevr))
		return DEP_EQ;

	/* 3. whether dependency optimization is enabled? */
	if (!deps_opt_enabled ())
		return DEP_UN;

	Asense = Aflags & RPMSENSE_SENSEMASK;
	Bsense = Bflags & RPMSENSE_SENSEMASK;

	/* 4. check for supported tags. */
	switch (tag) {
		case RPMTAG_PROVIDEFLAGS:
		case RPMTAG_OBSOLETEFLAGS:
		case RPMTAG_CONFLICTFLAGS:
		case RPMTAG_REQUIREFLAGS:
			break;
		default:
			/* no way to optimize this case. */
			return DEP_UN;
	}

	/* 5. sanity checks */
	if (
	    ((Asense & RPMSENSE_LESS) && (Asense & RPMSENSE_GREATER)) ||
	    ((Bsense & RPMSENSE_LESS) && (Bsense & RPMSENSE_GREATER)) ||
	    ((Asense == 0) ^ (Aevr[0] == 0)) ||
	    ((Bsense == 0) ^ (Bevr[0] == 0))
	   )
		return DEP_UN;

	/* 5. filter out essentially different versions. */
	if (
	    ((Asense & RPMSENSE_LESS) && (Bsense & RPMSENSE_GREATER)) ||
	    ((Bsense & RPMSENSE_LESS) && (Asense & RPMSENSE_GREATER))
	   )
		return DEP_UN;

	/* 7. filter out essentially different flags. */
	if ((Aflags & ~RPMSENSE_SENSEMASK) != (Bflags & ~RPMSENSE_SENSEMASK))
	{
		rpmsenseFlags Areq, Breq;

		/* 7a. no way to optimize different non-REQUIREFLAGS */
		if (tag != RPMTAG_REQUIREFLAGS)
			return DEP_UN;

		/* 7b. filter out essentially different requires. */
		if ((Aflags & ~RPMSENSE_SENSEMASK & ~_ALL_REQUIRES_MASK) !=
		    (Bflags & ~RPMSENSE_SENSEMASK & ~_ALL_REQUIRES_MASK))
			return DEP_UN;

		Areq = Aflags & _ALL_REQUIRES_MASK;
		Breq = Bflags & _ALL_REQUIRES_MASK;
		/* it is established fact that Areq != Breq */

		/* 7c. Aflags contains Bflags? */
		if (Breq && (Areq & Breq) == Breq)
			rc = DEP_ST;

		/* 7d. Bflags contains Aflags? */
		else if (Areq && (Areq & Breq) == Areq)
			rc = DEP_WK;

		else
			return DEP_UN;
	}

	/* 8. compare versions. */
	int aset = strncmp(Aevr, "set:", 4) == 0;
	int bset = strncmp(Bevr, "set:", 4) == 0;
	if (aset && bset) {
	    sense = rpmsetcmp(Aevr, Bevr);
	    if (sense < -1)
		return DEP_UN;
	}
	else if (aset) {
	    if (*Bevr)
		return DEP_UN;
	    sense = 1;
	}
	else if (bset) {
	    if (*Aevr)
		return DEP_UN;
	    sense = -1;
	}
	else {
	    aEVR = xstrdup(Aevr);
	    parseEVRD(aEVR, &aE, &aV, &aR, &aD);
	    bEVR = xstrdup(Bevr);
	    parseEVRD(bEVR, &bE, &bV, &bR, &bD);

	    /*
	     * Promote Epoch by giving it special treatment:
	     * if one of deps has Epoch and another one hasn't,
	     * we first compare them without Epoch, and if it happens
	     * that they are equal, then the dep that has Epoch wins.
	     */
	    const char *ae = aE, *be = bE;
	    if ((!(aE && *aE) || !(bE && *bE))) {
		ae = NULL; be = NULL;
	    }

		if ((aR && *aR) && !(bR && *bR))
			wcmp = -1;
		else if ((bR && *bR) && !(aR && *aR))
			wcmp = 1;

	    sense = rpmEVRcmp(ae, aV, aR, Aevr, be, bV, bR, Bevr);
	}

	/* 9. detect overlaps. */
	cmp_rc = compare_sense_flags (tag, sense, wcmp, Asense, Bsense);

	/* 10. EVRs with Epoch are stronger. */
	if (cmp_rc == DEP_EQ)
	{
		if ((aE && *aE) && !(bE && *bE))
			cmp_rc = DEP_ST;
		else if ((bE && *bE) && !(aE && *aE))
			cmp_rc = DEP_WK;
	}
	/* 10b. EVRs with DistTag are stronger. */
	if (cmp_rc == DEP_EQ)
	{
	    if ((aD && *aD) && !(bD && *bD))
		cmp_rc = DEP_ST;
	    else if ((bD && *bD) && !(aD && *aD))
		cmp_rc = DEP_WK;
	}

	aEVR = _free(aEVR);
	bEVR = _free(bEVR);

#if 0
	fprintf(stderr, "D: compare_sense_flags=%d: tag=%d, sense=%d, wcmp=%d, Asense=%#x, Bsense=%#x\n",
		cmp_rc, tag, sense, wcmp, Asense, Bsense);
#endif

	/* 11. compare expected with received. */
	if (cmp_rc == DEP_UN || rc == DEP_UN)
		return cmp_rc;

	if (cmp_rc != rc && cmp_rc != DEP_EQ)
		return DEP_UN;

	return rc;
}

int addReqProv(/*@unused@*/ Spec spec, Header h,
	       rpmsenseFlags depFlags, const char *depName, const char *depEVR,
		int index)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    const char ** names;
    rpmTagType dnt;
    rpmTag nametag = 0;
    rpmTag versiontag = 0;
    rpmTag flagtag = 0;
    rpmTag indextag = 0;
    int len;
    rpmsenseFlags extra = RPMSENSE_ANY;
    int xx;
    
    /*@-branchstate@*/
    if (depEVR == NULL)
	depEVR = "";
    /*@=branchstate@*/

    if (depFlags & RPMSENSE_PROVIDES) {
	const char *name = NULL;

	headerName(h, &name);
	if (name != NULL && strcmp(name, depName) == 0) {
	    char dep_flags_str[4];
	    unsigned int i = 0;

	    if (depFlags & RPMSENSE_LESS)
		dep_flags_str[i++] = '<';
	    if (depFlags & RPMSENSE_GREATER)
		dep_flags_str[i++] = '>';
	    if (depFlags & RPMSENSE_EQUAL)
		dep_flags_str[i++] = '=';
	    dep_flags_str[i] = '\0';

	    rpmMessage(RPMMESS_WARNING, "ignored self-provides: %s %s %s\n",
		       depName, dep_flags_str, depEVR);
	    return 0;
	}

	nametag = RPMTAG_PROVIDENAME;
	versiontag = RPMTAG_PROVIDEVERSION;
	flagtag = RPMTAG_PROVIDEFLAGS;
	extra = depFlags & RPMSENSE_FIND_PROVIDES;
    } else if (depFlags & RPMSENSE_OBSOLETES) {
	nametag = RPMTAG_OBSOLETENAME;
	versiontag = RPMTAG_OBSOLETEVERSION;
	flagtag = RPMTAG_OBSOLETEFLAGS;
    } else if (depFlags & RPMSENSE_CONFLICTS) {
	nametag = RPMTAG_CONFLICTNAME;
	versiontag = RPMTAG_CONFLICTVERSION;
	flagtag = RPMTAG_CONFLICTFLAGS;
    } else if (depFlags & RPMSENSE_PREREQ) {
	nametag = RPMTAG_REQUIRENAME;
	versiontag = RPMTAG_REQUIREVERSION;
	flagtag = RPMTAG_REQUIREFLAGS;
	extra = depFlags & _ALL_REQUIRES_MASK;
    } else if (depFlags & RPMSENSE_TRIGGER) {
	nametag = RPMTAG_TRIGGERNAME;
	versiontag = RPMTAG_TRIGGERVERSION;
	flagtag = RPMTAG_TRIGGERFLAGS;
	indextag = RPMTAG_TRIGGERINDEX;
	extra = depFlags & RPMSENSE_TRIGGER;
    } else {
	nametag = RPMTAG_REQUIRENAME;
	versiontag = RPMTAG_REQUIREVERSION;
	flagtag = RPMTAG_REQUIREFLAGS;
	extra = depFlags & _ALL_REQUIRES_MASK;
    }

    depFlags = (depFlags & (RPMSENSE_SENSEMASK)) | extra;

    /* Check for duplicate dependencies. */
    if (hge(h, nametag, &dnt, (void **) &names, &len) && len > 0) {
	const char ** versions = NULL;
	rpmTagType dvt = RPM_STRING_ARRAY_TYPE;
	int *flags = NULL;
	int *indexes = NULL;
	int i, o_cnt = 0, duplicate = 0;
	char obsolete[len];

	memset (obsolete, 0, sizeof obsolete);

	if (flagtag) {
	    xx = hge(h, versiontag, &dvt, (void **) &versions, NULL);
	    xx = hge(h, flagtag, NULL, (void **) &flags, NULL);
	}
	if (indextag)
	    xx = hge(h, indextag, NULL, (void **) &indexes, NULL);

	for (i = len - 1; i >= 0; --i) {
	    if (indextag && indexes && indexes[i] != index)
		continue;

	    if (strcmp(names[i], depName))
		continue;

	    if (flagtag && flags && versions) {
		dep_compare_t rc = compare_deps(flagtag, versions[i], flags[i],
						depEVR, depFlags);

#if 0
		fprintf (stderr, "D: name=%s, compare_deps=%d: tag=%d, AEVR=%s, Aflags=%#x, BEVR=%s, Bflags=%#x\n",
			depName, rc, flagtag, versions[i], flags[i], depEVR, depFlags);
#endif
		if (rc == DEP_UN && flagtag == RPMTAG_REQUIREFLAGS) {
			const rpmsenseFlags mergedFlags =
				(flags[i] & _ALL_REQUIRES_MASK) |
				(depFlags & ~RPMSENSE_FIND_REQUIRES);
			if (mergedFlags != depFlags &&
			    compare_deps(flagtag, versions[i], flags[i],
					 depEVR, mergedFlags) == DEP_WK) {
				rpmMessage(RPMMESS_DEBUG,
					"new dep \"%s\" flags %#x upgraded to %#x\n",
					depName, depFlags, mergedFlags);
				depFlags = mergedFlags;
				rc = DEP_WK;
			}
		}
		switch (rc) {
			case DEP_EQ:
				rpmMessage (RPMMESS_DEBUG,
					"new dep \"%s\" already exists, optimized out\n",
					depName);
				break;
			case DEP_ST:
				rpmMessage (RPMMESS_DEBUG,
					"new dep \"%s\" is weaker, optimized out\n",
					depName);
				break;
			case DEP_WK:
				++o_cnt;
				obsolete[i] = 1;
			default:
				continue;
		}
	    }

	    /* This is a duplicate dependency. */
	    duplicate = 1;

	    if (o_cnt) {
		rpmMessage(RPMMESS_WARNING, "%d obsolete deps left", o_cnt);
		o_cnt = 0;
	    }

	    break;
	} /* end of main loop */

	if (o_cnt)
	{
		int     j, new_len = len - o_cnt;
		const char *new_names[new_len];
		const char *new_versions[new_len];
		int     new_flags[new_len];

		rpmMessage (RPMMESS_DEBUG, "%d old deps to be optimized out\n", o_cnt);
		for (i = 0, j = 0; i < len; ++i)
		{
			char   *p;

			if (obsolete[i])
			{
				rpmMessage (RPMMESS_DEBUG, "old dep \"%s\" optimized out\n", names[i]);
				continue;
			}

			p = alloca (1 + strlen (names[i]));
			strcpy (p, names[i]);
			new_names[j] = p;

			p = alloca (1 + strlen (versions[i]));
			strcpy (p, versions[i]);
			new_versions[j] = p;

			new_flags[j] = flags[i];
			++j;
		}

		if (   !headerModifyEntry (h, nametag, RPM_STRING_ARRAY_TYPE, new_names, new_len)
		    || !headerModifyEntry (h, versiontag, RPM_STRING_ARRAY_TYPE, new_versions, new_len)
		    || !headerModifyEntry (h, flagtag, RPM_INT32_TYPE, new_flags, new_len))
			rpmError (RPMERR_BADHEADER, "addReqProv: error modifying entry for dep %s\n", depName);
		rpmMessage (RPMMESS_DEBUG, "%d old deps optimized out, %d left\n", o_cnt, new_len);
	}

	versions = hfd(versions, dvt);
	names = hfd(names, dnt);
	if (duplicate)
	    return 1;
    }

	/* Do not add NEW provided requires. */
	if (   deps_opt_enabled ()
	    && (nametag == RPMTAG_REQUIRENAME)
	    && !(depFlags & _notpre (RPMSENSE_RPMLIB | RPMSENSE_KEYRING |
				     RPMSENSE_SCRIPT_PRE | RPMSENSE_SCRIPT_POSTUN)))
	{

		int     skip = 0;
		int    *flags = 0;
		const char **versions = 0;
		rpmTagType dvt = RPM_STRING_ARRAY_TYPE;

		names = NULL;
		hge (h, RPMTAG_PROVIDENAME, &dnt, (void **) &names, &len);
		hge (h, RPMTAG_PROVIDEVERSION, &dvt, (void **) &versions, NULL);
		hge (h, RPMTAG_PROVIDEFLAGS, NULL, (void **) &flags, NULL);

		while (names && flags && versions && (len > 0))
		{
			--len;

			if (strcmp (depName, names[len]))
				continue;
			if (!(depFlags & RPMSENSE_SENSEMASK))
			{
				skip = 1;
				break;
			}
			if (!(flags[len] & RPMSENSE_SENSEMASK))
				continue;
			if (rpmRangesOverlap ("", versions[len], flags[len],
					      "", depEVR, depFlags))
			{
				rpmMessage (RPMMESS_DEBUG,
					    "new dep \"%s\" already provided, optimized out\n",
					    depName);
				skip = 1;
				break;
			}
		}

		versions = hfd (versions, dvt);
		names = hfd (names, dnt);
		if (skip)
			return 1;

		if (*depName == '/' && !(depFlags & RPMSENSE_SENSEMASK))
		{
			const char **bn = NULL, **dn = NULL;
			const int_32 *di = NULL;
			rpmTagType bnt = 0, dnt = 0, dit = 0;
			int_32 bnc = 0;
			int i;
			(void) (hge(h, RPMTAG_DIRNAMES, &dnt, (void**)&dn, NULL) &&
				hge(h, RPMTAG_DIRINDEXES, &dit, (void**)&di, NULL) &&
				hge(h, RPMTAG_BASENAMES, &bnt, (void**)&bn, &bnc));
			for (i = 0; i < bnc; i++) {
				const char *d = dn[di[i]], *b = bn[i];
				size_t dl = strlen(d);
				if (strncmp(depName, d, dl) ||
				    strcmp(depName + dl, b))
					continue;
				rpmMessage (RPMMESS_DEBUG,
					    "new dep \"%s\" is packaged file, optimized out\n",
					    depName);
				skip = 1;
				break;
			}
			hfd(dn, dnt);
			hfd(di, dit);
			hfd(bn, bnt);
		}
		else {
			const char *N = NULL, *V = NULL, *R = NULL, *D = NULL;
			headerNVRD(h, &N, &V, &R, &D);
			if (N && strcmp(depName, N) == 0) {
				if (!(depFlags & RPMSENSE_SENSEMASK))
					skip = 1;
				else if (V && R) {
					int_32 *E = NULL;
					const char *EVR, *EVRD = NULL;
					hge(h, RPMTAG_EPOCH, NULL, (void**) &E, NULL);
					if (E)
						EVR = xasprintf("%d:%s-%s", *E, V, R);
					else
						EVR = xasprintf("%s-%s", V, R);
					if (D)
						EVRD = xasprintf("%s:%s", EVR, D);
					if (rpmRangesOverlap("", EVRD ? : EVR, RPMSENSE_EQUAL,
							     "", depEVR, depFlags))
						skip = 1;
					EVRD = _free(EVRD);
					EVR = _free(EVR);
				}
				else
					skip = 1;
				if (skip)
				rpmMessage (RPMMESS_DEBUG,
					    "new dep \"%s\" is the package name, optimized out\n",
					    depName);
			}
		}

		if (skip)
			return 1;
	}

	/* Remove OLD provided requires. */
	if (   deps_opt_enabled ()
	    && (nametag == RPMTAG_PROVIDENAME)
	    && hge (h, RPMTAG_REQUIRENAME, &dnt, (void **) &names, &len))
	{

		int    *flags = 0;
		const char **versions = 0;
		rpmTagType dvt = RPM_STRING_ARRAY_TYPE;
		int i, o_cnt = 0;
		char obsolete[len];

		memset (obsolete, 0, sizeof obsolete);

		hge (h, RPMTAG_REQUIREVERSION, &dvt, (void **) &versions, NULL);
		hge (h, RPMTAG_REQUIREFLAGS, NULL, (void **) &flags, NULL);

		for (i = len - 1; flags && versions && (i >= 0); --i)
		{
			rpmsenseFlags f = flags[i];

			if ((f & _notpre (RPMSENSE_RPMLIB | RPMSENSE_KEYRING |
					  RPMSENSE_SCRIPT_PRE | RPMSENSE_SCRIPT_POSTUN)))
				continue;
			if (strcmp (depName, names[i]))
				continue;
			if (!(f & RPMSENSE_SENSEMASK))
			{
				++o_cnt;
				obsolete[i] = 1;
				continue;
			}
			if (!(depFlags & RPMSENSE_SENSEMASK))
				continue;
			if (rpmRangesOverlap ("", depEVR, depFlags,
					      "", versions[i], f))
			{
				++o_cnt;
				obsolete[i] = 1;
				continue;
			}
		}

		if (o_cnt)
		{
			int j, new_len = len - o_cnt;
			const char *new_names[new_len];
			const char *new_versions[new_len];
			int new_flags[new_len];

			rpmMessage (RPMMESS_DEBUG, "%d old deps to be optimized out\n", o_cnt);
			for (i = 0, j = 0; i < len; ++i)
			{
				char *p;

				if (obsolete[i])
				{
					rpmMessage (RPMMESS_DEBUG, "old dep \"%s\" optimized out\n", names[i]);
					continue;
				}

				p = alloca (1 + strlen (names[i]));
				strcpy (p, names[i]);
				new_names[j] = p;

				p = alloca (1 + strlen (versions[i]));
				strcpy (p, versions[i]);
				new_versions[j] = p;

				new_flags[j] = flags[i];
				++j;
			}

			if (!headerModifyEntry (h, RPMTAG_REQUIRENAME, RPM_STRING_ARRAY_TYPE, new_names, new_len) ||
			    !headerModifyEntry (h, RPMTAG_REQUIREVERSION, RPM_STRING_ARRAY_TYPE, new_versions, new_len) ||
			    !headerModifyEntry (h, RPMTAG_REQUIREFLAGS, RPM_INT32_TYPE, new_flags, new_len))
			    rpmError (RPMERR_BADHEADER, "addReqProv: error modifying entry for dep %s\n", depName);
			rpmMessage (RPMMESS_DEBUG, "%d old deps optimized out, %d left\n", o_cnt, new_len);
		}

		versions = hfd (versions, dvt);
		names = hfd (names, dnt);
	}

    /* Add this dependency. */
    xx = headerAddOrAppendEntry(h, nametag, RPM_STRING_ARRAY_TYPE, &depName, 1);
    if (flagtag) {
	xx = headerAddOrAppendEntry(h, versiontag,
			       RPM_STRING_ARRAY_TYPE, &depEVR, 1);
	xx = headerAddOrAppendEntry(h, flagtag,
			       RPM_INT32_TYPE, &depFlags, 1);
    }
    if (indextag)
	xx = headerAddOrAppendEntry(h, indextag, RPM_INT32_TYPE, &index, 1);

    return 0;
}

int rpmlibNeedsFeature(Header h, const char * feature, const char * featureEVR)
{
    char * reqname = alloca(sizeof("rpmlib()") + strlen(feature));
    (void) stpcpy( stpcpy( stpcpy(reqname, "rpmlib("), feature), ")");

    rpmsenseFlags depFlags = RPMSENSE_RPMLIB;
    if (featureEVR)
	depFlags |= RPMSENSE_LESS|RPMSENSE_EQUAL;

    /* XXX 1st arg is unused */
    return addReqProv(NULL, h, depFlags, reqname, featureEVR, 0);
}
