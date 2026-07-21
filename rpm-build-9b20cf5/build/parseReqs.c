/** \ingroup rpmbuild
 * \file build/parseReqs.c
 *  Parse dependency tag from spec file or from auto-dependency generator.
 */

#include "system.h"

#include "rpmbuild.h"
#include "debug.h"

/**
 */
/*@observer@*/ /*@unchecked@*/
static struct ReqComp {
/*@observer@*/ /*@null@*/ const char * token;
    rpmsenseFlags sense;
} ReqComparisons[] = {
    { "<=", RPMSENSE_LESS | RPMSENSE_EQUAL},
    { "=<", RPMSENSE_LESS | RPMSENSE_EQUAL},
    { "<", RPMSENSE_LESS},

    { "==", RPMSENSE_EQUAL},
    { "=", RPMSENSE_EQUAL},
    
    { ">=", RPMSENSE_GREATER | RPMSENSE_EQUAL},
    { "=>", RPMSENSE_GREATER | RPMSENSE_EQUAL},
    { ">", RPMSENSE_GREATER},

    { NULL, 0 },
};

#define	SKIPWHITE(_x)	{while(*(_x) && (xisspace(*_x) || *(_x) == ',')) (_x)++;}
#define	SKIPNONWHITE(_x){while(*(_x) &&!(xisspace(*_x) || *(_x) == ',')) (_x)++;}

int parseRCPOT(Spec spec, Package pkg, const char *field, int tag,
	       int index, rpmsenseFlags tagflags)
{
    const char *r, *re, *v, *ve;
    char * req, * version;
    Header h;
    rpmsenseFlags flags;

    switch (tag) {
    case RPMTAG_PROVIDEFLAGS:
	tagflags |= RPMSENSE_PROVIDES;
	h = pkg->header;
	break;
    case RPMTAG_OBSOLETEFLAGS:
	tagflags |= RPMSENSE_OBSOLETES;
	h = pkg->header;
	break;
    case RPMTAG_CONFLICTFLAGS:
	tagflags |= RPMSENSE_CONFLICTS;
	h = pkg->header;
	break;
    case RPMTAG_BUILDCONFLICTS:
	tagflags |= RPMSENSE_CONFLICTS;
	h = spec->buildRestrictions;
	break;
    case RPMTAG_PREREQ:
	tag = RPMTAG_REQUIREFLAGS;
	if (tagflags == RPMSENSE_ANY) {
		tagflags = RPMSENSE_SCRIPT_PRE | RPMSENSE_SCRIPT_POSTUN;
		rpmlog(RPMLOG_WARNING,
		       _("line %d: Deprecated PreReq converted"
			 " to Requires(pre,postun): %s\n"),
		       spec->lineNum, spec->line);
	} else {
		rpmlog(RPMLOG_WARNING,
		       _("line %d: Deprecated PreReq converted"
			 " to Requires: %s\n"),
		       spec->lineNum, spec->line);
	}
	h = pkg->header;
	break;
    case RPMTAG_BUILDPREREQ:
	tagflags |= RPMSENSE_PREREQ;
	h = spec->buildRestrictions;
	break;
    case RPMTAG_TRIGGERIN:
	tagflags |= RPMSENSE_TRIGGERIN;
	h = pkg->header;
	break;
    case RPMTAG_TRIGGERPOSTUN:
	tagflags |= RPMSENSE_TRIGGERPOSTUN;
	h = pkg->header;
	break;
    case RPMTAG_TRIGGERUN:
	tagflags |= RPMSENSE_TRIGGERUN;
	h = pkg->header;
	break;
    case RPMTAG_BUILDREQUIRES:
	tagflags |= RPMSENSE_ANY;
	h = spec->buildRestrictions;
	break;
    default:
    case RPMTAG_REQUIREFLAGS:
	tagflags |= RPMSENSE_ANY;
	h = pkg->header;
	break;
    }

    for (r = field; *r != '\0'; r = re) {
	SKIPWHITE(r);
	if (*r == '\0')
	    break;

	flags = (tagflags & ~RPMSENSE_SENSEMASK);

	/* Tokens must begin with alphanumeric, _, or / */
	if (!(xisalnum(r[0]) || r[0] == '_' || r[0] == '/')) {
	    rpmError(RPMERR_BADSPEC,
		     _("line %d: Dependency tokens must begin with alpha-numeric, '_' or '/': %s\n"),
		     spec->lineNum, (spec->fileStack ? spec->line : field));
	    return RPMERR_BADSPEC;
	}

	/* Don't permit file names as args for certain tags */
	switch (tag) {
	case RPMTAG_OBSOLETEFLAGS:
	case RPMTAG_CONFLICTFLAGS:
	case RPMTAG_BUILDCONFLICTS:
	    if (r[0] == '/') {
		rpmError(RPMERR_BADSPEC,_("line %d: File name not permitted: %s\n"),
			 spec->lineNum, (spec->fileStack ? spec->line : field));
		return RPMERR_BADSPEC;
	    }
	    /*@switchbreak@*/ break;
	default:
	    /*@switchbreak@*/ break;
	}

	re = r;
	SKIPNONWHITE(re);
	req = xmalloc((re-r) + 1);
	strncpy(req, r, (re-r));
	req[re-r] = '\0';

	/* Names must not contain '%<=>' symbols - it's common error */
	if (strpbrk (req, "%<=>")) {
	    rpmError(RPMERR_BADSPEC,
		     _("line %d: Dependency tokens must not contain '%%<=>' symbols: %s\n"),
		     spec->lineNum, (spec->fileStack ? spec->line : field));
	    return RPMERR_BADSPEC;
	}

	/* Parse version */
	v = re;
	SKIPWHITE(v);
	ve = v;
	SKIPNONWHITE(ve);

	re = v;	/* ==> next token (if no version found) starts here */

	/* Check for possible logical operator */
	if (ve > v) {
	  struct ReqComp *rc;
	  for (rc = ReqComparisons; rc->token != NULL; rc++) {
	    if ((ve-v) != strlen(rc->token) || strncmp(v, rc->token, (ve-v)))
		/*@innercontinue@*/ continue;

	    flags |= rc->sense;

	    /* now parse version */
	    v = ve;
	    SKIPWHITE(v);
	    ve = v;
	    SKIPNONWHITE(ve);
	    /*@innerbreak@*/ break;
	  }
	}

	/*@-branchstate@*/
	if (flags & RPMSENSE_SENSEMASK) {
	    if (*v == '\0' || ve == v) {
		rpmError(RPMERR_BADSPEC, _("line %d: Version required: %s\n"),
			spec->lineNum, (spec->fileStack ? spec->line : field));
		return RPMERR_BADSPEC;
	    }
	    if ((tag == RPMTAG_PROVIDEFLAGS) &&
		(flags & (RPMSENSE_LESS | RPMSENSE_GREATER))) {
		rpmError(RPMERR_BADSPEC, _("line %d: Invalid logical operator in %s\n"),
			spec->lineNum, (spec->fileStack ? spec->line : field));
		return RPMERR_BADSPEC;
	    }
	    version = xmalloc((ve-v) + 1);
	    strncpy(version, v, (ve-v));
	    version[ve-v] = '\0';
	    re = ve;	/* ==> next token after version string starts here */
	} else
	    version = NULL;
	/*@=branchstate@*/

	/* Versions must not contain '%<=>' symbols - it's common error */
	if (version && strpbrk (version, "%<=>")) {
	    rpmError(RPMERR_BADSPEC,
		     _("line %d: Dependency tokens must not contain '%%<=>' symbols: %s\n"),
		     spec->lineNum, (spec->fileStack ? spec->line : field));
	    req = _free(req);
	    version = _free(version);
	    return RPMERR_BADSPEC;
	}

	if (addReqProv(spec, h, flags, req, version, index) == 0) {
	    // Set-versions need their rpmlib feature.
	    if (version && strncmp(version, "set:", 4) == 0)
		// Except for provides, to facilitate bootstrap.
		if (tag != RPMTAG_PROVIDEFLAGS)
		    rpmlibNeedsFeature(h, "SetVersions", NULL);
	}

	req = _free(req);
	version = _free(version);

    }

    return 0;
}
