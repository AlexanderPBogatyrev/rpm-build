/** \ingroup rpmbuild
 * \file build/parsePreamble.c
 *  Parse tags in global section from spec file.
 */

#include "system.h"

#include "rpmio_internal.h"
#include "rpmbuild.h"
#include "debug.h"

/*@access FD_t @*/	/* compared with NULL */

/**
 */
/*@observer@*/ /*@unchecked@*/
static rpmTag copyTagsDuringParse[] = {
    RPMTAG_EPOCH,
    RPMTAG_VERSION,
    RPMTAG_RELEASE,
    RPMTAG_LICENSE,
    RPMTAG_PACKAGER,
    RPMTAG_DISTRIBUTION,
    RPMTAG_DISTURL,
    RPMTAG_VENDOR,
    RPMTAG_ICON,
    RPMTAG_URL,
    RPMTAG_VCS,
    RPMTAG_CHANGELOGTIME,
    RPMTAG_CHANGELOGNAME,
    RPMTAG_CHANGELOGTEXT,
    RPMTAG_PREFIXES,
    RPMTAG_BUILDHOST,
    RPMTAG_DISTTAG,
    0
};

/**
 */
/*@observer@*/ /*@unchecked@*/
static rpmTag requiredTags[] = {
    RPMTAG_NAME,
    RPMTAG_VERSION,
    RPMTAG_RELEASE,
    RPMTAG_SUMMARY,
    RPMTAG_GROUP,
    RPMTAG_LICENSE,
    0
};

/**
 */
static void addOrAppendListEntry(Header h, int_32 tag, char * line)
	/*@modifies h @*/
{
    int xx;
    int argc;
    const char **argv;

    xx = poptParseArgvString(line, &argc, &argv);
    if (argc)
	xx = headerAddOrAppendEntry(h, tag, RPM_STRING_ARRAY_TYPE, argv, argc);
    argv = _free(argv);
}

/* Parse a simple part line that only take -n <pkg> or <pkg> */
/* <pkg> is return in name as a pointer into a static buffer */

/**
 */
static int parseSimplePart(char *line, /*@out@*/char **name, /*@out@*/int *flag)
	/*@globals internalState@*/
	/*@modifies *name, *flag, internalState @*/
{
    char *tok;
    char linebuf[BUFSIZ];
    static char buf[BUFSIZ];

    strcpy(linebuf, line);

    /* Throw away the first token (the %xxxx) */
    (void)strtok(linebuf, " \t\n");
    
    if (!(tok = strtok(NULL, " \t\n"))) {
	*name = NULL;
	return 1;
    }
    
    if (!strcmp(tok, "-n")) {
	if (!(tok = strtok(NULL, " \t\n")))
	    return 1;
	*flag = PART_NAME;
    } else {
	*flag = PART_SUBNAME;
    }
    strcpy(buf, tok);
    *name = buf;

    return (strtok(NULL, " \t\n")) ? 1 : 0;
}

/**
 */
static inline const char *parseReqProv(const char *s)
{
    if (!s ||
	!strcasecmp(s, "no") ||
	!strcasecmp(s, "false") ||
	!strcasecmp(s, "off") ||
	!strcmp(s, "0")) {
	return xstrdup("");
    }

    return xstrdup(s);
}

typedef struct tokenBits_s {
/*@observer@*/ /*@null@*/ const char * name;
    rpmsenseFlags bits;
} * tokenBits;

/**
 */
/*@observer@*/ /*@unchecked@*/
static struct tokenBits_s installScriptBits[] = {
    { "interp",		RPMSENSE_INTERP },
    { "preun",		RPMSENSE_SCRIPT_PREUN },
    { "pre",		RPMSENSE_SCRIPT_PRE },
    { "postun",		RPMSENSE_SCRIPT_POSTUN },
    { "post",		RPMSENSE_SCRIPT_POST },
    { "rpmlib",		RPMSENSE_RPMLIB },
    { "verify",		RPMSENSE_SCRIPT_VERIFY },
    { NULL, 0 }
};

/**
 */
/*@observer@*/ /*@unchecked@*/
static struct tokenBits_s buildScriptBits[] = {
    { "prep",		RPMSENSE_SCRIPT_PREP },
    { "build",		RPMSENSE_SCRIPT_BUILD },
    { "install",	RPMSENSE_SCRIPT_INSTALL },
    { "clean",		RPMSENSE_SCRIPT_CLEAN },
    { NULL, 0 }
};

/**
 */
static int parseBits(const char * s, const tokenBits tokbits,
		/*@out@*/ rpmsenseFlags * bp)
	/*@modifies *bp @*/
{
    tokenBits tb = NULL;
    rpmsenseFlags bits = RPMSENSE_ANY;
    int rc = 0;

    if (s) {
	for (;;) {
	    int c = 0;
	    while ((c = *s) && xisspace(c)) s++;

	    const char *se = s;
	    while ((c = *se) && xisalpha(c)) se++;
	    if (s == se) {
		if (c || tb)
		    rc = RPMERR_BADSPEC;
		break;
	    }

	    tb = NULL;
	    for (tokenBits t = tokbits; t->name; t++) {
		if (t->name && !strncasecmp(t->name, s, (se-s))) {
		    if (!t->name[se-s]) {
			tb = t;
			break;
		    }
		    if (tb) {
			tb = NULL;
			break;
		    }
		    tb = t;
		}
	    }
	    if (!tb) {
		rc = RPMERR_BADSPEC;
		break;
	    }
	    bits |= tb->bits;

	    while ((c = *se) && xisspace(c)) se++;
	    if (c != ',') {
		if (c)
		    rc = RPMERR_BADSPEC;
		break;
	    }
	    s = ++se;
	}
    }

    *bp |= bits;
    return rc;
}

/**
 */
static inline char * findLastChar(char * s)
	/*@*/
{
    char *res = s;

    while (*s != '\0') {
	if (! xisspace(*s))
	    res = s;
	s++;
    }

    /*@-temptrans -retalias@*/
    return res;
    /*@=temptrans =retalias@*/
}

/**
 */
static int isMemberInEntry(Header h, const char *name, rpmTag tag)
	/*@*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    const char ** names;
    rpmTagType type;
    int count;

    if (!hge(h, tag, &type, (void **)&names, &count))
	return -1;
    while (count--) {
	if (!xstrcasecmp(names[count], name))
	    break;
    }
    names = hfd(names, type);
    return (count >= 0 ? 1 : 0);
}

/**
 */
static int checkForValidArchitectures(Spec spec)
	/*@*/
{
#ifndef	DYING
    const char *arch = NULL;
    const char *os = NULL;

    rpmGetArchInfo(&arch, NULL);
    rpmGetOsInfo(&os, NULL);
#else
    const char *arch = rpmExpand("%{_target_cpu}", NULL);
    const char *os = rpmExpand("%{_target_os}", NULL);
#endif
    
    if (isMemberInEntry(spec->buildRestrictions,
			arch, RPMTAG_EXCLUDEARCH) == 1) {
	rpmError(RPMERR_BADSPEC, _("Architecture is excluded: %s\n"), arch);
	return RPMERR_BADSPEC;
    }
    if (isMemberInEntry(spec->buildRestrictions,
			arch, RPMTAG_EXCLUSIVEARCH) == 0) {
	rpmError(RPMERR_BADSPEC, _("Architecture is not included: %s\n"), arch);
	return RPMERR_BADSPEC;
    }
    if (isMemberInEntry(spec->buildRestrictions,
			os, RPMTAG_EXCLUDEOS) == 1) {
	rpmError(RPMERR_BADSPEC, _("OS is excluded: %s\n"), os);
	return RPMERR_BADSPEC;
    }
    if (isMemberInEntry(spec->buildRestrictions,
			os, RPMTAG_EXCLUSIVEOS) == 0) {
	rpmError(RPMERR_BADSPEC, _("OS is not included: %s\n"), os);
	return RPMERR_BADSPEC;
    }

    return 0;
}

/**
 * Check that required tags are present in header.
 * @param h		header
 * @param NVR		package name-version-release
 * @return		0 if OK
 */
static int checkForRequired(Header h, const char * NVR)
	/*@modifies h @*/ /* LCL: parse error here with modifies */
{
    int res = 0;
    rpmTag * p;

    for (p = requiredTags; *p != 0; p++) {
	if (!headerIsEntry(h, *p)) {
	    rpmError(RPMERR_BADSPEC,
			_("%s field must be present in package: %s\n"),
			tagName(*p), NVR);
	    res = 1;
	}
    }

    return res;
}

/**
 * Check that no duplicate tags are present in header.
 * @param h		header
 * @param NVR		package name-version-release
 * @return		0 if OK
 */
static int checkForDuplicates(Header h, const char * NVR)
	/*@modifies h @*/
{
    int res = 0;
    int lastTag, tag;
    HeaderIterator hi;
    
    for (hi = headerInitIterator(h), lastTag = 0;
	headerNextIterator(hi, &tag, NULL, NULL, NULL);
	lastTag = tag)
    {
	if (tag != lastTag)
	    continue;
	rpmError(RPMERR_BADSPEC, _("Duplicate %s entries in package: %s\n"),
		     tagName(tag), NVR);
	res = 1;
    }
    hi = headerFreeIterator(hi);

    return res;
}

/**
 */
/*@observer@*/ /*@unchecked@*/
static struct optionalTag {
    rpmTag	ot_tag;
/*@observer@*/ /*@null@*/ const char * ot_mac;
} optionalTags[] = {
    { RPMTAG_VENDOR,		"%{?vendor}" },
    { RPMTAG_PACKAGER,		"%{?packager}" },
    { RPMTAG_DISTRIBUTION,	"%{?distribution}" },
    { RPMTAG_DISTURL,		"%{?disturl}" },
    { RPMTAG_BUILDHOST,		"%{?buildhost}" },
    { RPMTAG_DISTTAG,		"%{?disttag}"},
    { -1, NULL }
};

/**
 */
static void fillOutMainPackage(Header h)
	/*@globals rpmGlobalMacroContext @*/
	/*@modifies h, rpmGlobalMacroContext @*/
{
    struct optionalTag *ot;

    for (ot = optionalTags; ot->ot_mac != NULL; ot++) {
	if (!headerIsEntry(h, ot->ot_tag)) {
	    const char *val = rpmExpand(ot->ot_mac, NULL);
	    if (val && *val != '\0')
		(void) headerAddEntry(h, ot->ot_tag, RPM_STRING_TYPE, (void *)val, 1);
	    val = _free(val);
	}
    }
}

#ifdef ENABLE_ICON_TAG
/**
 */
static int readIcon(Header h, const char * file)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies h, rpmGlobalMacroContext, fileSystem @*/
{
    const char *fn = NULL;
    char *icon;
    FD_t fd;
    int rc = 0;
    off_t size;
    size_t nb, iconsize;

    /* XXX use rpmGenPath(rootdir, "%{_sourcedir}/", file) for icon path. */
    fn = rpmGetPath("%{_sourcedir}/", file, NULL);

    fd = Fopen(fn, "r.ufdio");
    if (fd == NULL || Ferror(fd)) {
	rpmError(RPMERR_BADSPEC, _("Unable to open icon %s: %s\n"),
		fn, Fstrerror(fd));
	rc = RPMERR_BADSPEC;
	goto exit;
    }
    size = fdSize(fd);
    iconsize = (size >= 0 ? size : (8 * BUFSIZ));
    if (iconsize == 0) {
	(void) Fclose(fd);
	rc = 0;
	goto exit;
    }

    icon = xmalloc(iconsize + 1);
    *icon = '\0';

    nb = Fread(icon, sizeof(icon[0]), iconsize, fd);
    if (Ferror(fd) || (size >= 0 && nb != size)) {
	rpmError(RPMERR_BADSPEC, _("Unable to read icon %s: %s\n"),
		fn, Fstrerror(fd));
	rc = RPMERR_BADSPEC;
    }
    (void) Fclose(fd);
    if (rc)
	goto exit;

    if (! strncmp(icon, "GIF", sizeof("GIF")-1)) {
	(void) headerAddEntry(h, RPMTAG_GIF, RPM_BIN_TYPE, icon, iconsize);
    } else if (! strncmp(icon, "/* XPM", sizeof("/* XPM")-1)) {
	(void) headerAddEntry(h, RPMTAG_XPM, RPM_BIN_TYPE, icon, iconsize);
    } else {
	rpmError(RPMERR_BADSPEC, _("Unknown icon type: %s\n"), file);
	rc = RPMERR_BADSPEC;
	goto exit;
    }
    icon = _free(icon);
    
exit:
    fn = _free(fn);
    return rc;
}
#endif /* ENABLE_ICON_TAG */

spectag stashSt(Spec spec, Header h, int tag, const char * lang)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    spectag t = NULL;

    if (spec->st) {
	spectags st = spec->st;
	if (st->st_ntags == st->st_nalloc) {
	    st->st_nalloc += 10;
	    st->st_t = xrealloc(st->st_t, st->st_nalloc * sizeof(*(st->st_t)));
	}
	t = st->st_t + st->st_ntags++;
	t->t_tag = tag;
	t->t_startx = spec->lineNum - 1;
	t->t_nlines = 1;
	t->t_lang = xstrdup(lang);
	t->t_msgid = NULL;
	if (!(t->t_lang && strcmp(t->t_lang, RPMBUILD_DEFAULT_LANG))) {
	    char *n;
	    if (hge(h, RPMTAG_NAME, NULL, (void **) &n, NULL)) {
		char buf[1024];
		sprintf(buf, "%s(%s)", n, tagName(tag));
		t->t_msgid = xstrdup(buf);
	    }
	}
    }
    /*@-usereleased -compdef@*/
    return t;
    /*@=usereleased =compdef@*/
}

#define SINGLE_TOKEN_ONLY \
if (multiToken) { \
    rpmError(RPMERR_BADSPEC, _("line %d: Tag takes single token only: %s\n"), \
	     spec->lineNum, spec->line); \
    return RPMERR_BADSPEC; \
}

/**
 * Check for inappropriate characters.
 * All alphanumerics are allowed.
 *
 * @param spec		spec (or NULL)
 * @param str		string to check
 * @param accept_bytes	string of permitted bytes
 * @param reject_substr	sequence of rejected bytes
 * @return		RPMRC_OK if OK
 */
static rpmRC
rpmCharCheck(Spec spec, const char *str,
	     const char *accept_bytes, const char *reject_substr)
{
	const char *err = 0;

	for (const char *p = str; *p; ++p) {
		const unsigned char c = *p;

		if (xisalnum(c))
			continue;

		/* the first byte must be alphanumeric */
		if (p != str && accept_bytes && strchr(accept_bytes, c))
			continue;

		err = isprint(c)
		      ? xasprintf("Invalid symbol '%c' (%#x)", c, c)
		      : xasprintf("Invalid symbol (%#x)", c);
	}

	if (!err && reject_substr && reject_substr[0]
	    && strstr(str, reject_substr))
		err = xasprintf("Invalid sequence \"%s\"", reject_substr);

	if (!err)
		return RPMRC_OK;

	if (spec) {
		rpmlog(RPMLOG_ERR, "line %d: %s in: %s\n",
		       spec->lineNum, err, spec->line);
	} else {
		rpmlog(RPMLOG_ERR, "%s in: %s\n", err, str);
	}

	err = _free(err);
	return RPMRC_FAIL;
}

/*@-redecl@*/
extern int noLang;
/*@=redecl@*/

/**
 */
static int handlePreambleTag(Spec spec, Package pkg, int tag, const char *macro,
			     const char *lang)
	/*@globals rpmGlobalMacroContext,
		fileSystem @*/
	/*@modifies spec->macros, spec->st, spec->buildRootURL,
		spec->sources, spec->numSources, spec->noSource,
		spec->buildRestrictions, spec->BANames, spec->BACount,
		spec->line, spec->gotBuildRootURL,
		pkg->header, pkg->autoProv, pkg->autoReq, pkg->icon,
		rpmGlobalMacroContext, fileSystem @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    char * field = spec->line;
    char * end;
    char ** array;
    int multiToken = 0;
    rpmsenseFlags tagflags = RPMSENSE_ANY;
    rpmTagType type;
    int len;
    int num;
    int rc;
    int xx;
    
    if (field == NULL) return RPMERR_BADSPEC;	/* XXX can't happen */
    /* Find the start of the "field" and strip trailing space */
    while ((*field) && (*field != ':'))
	field++;
    if (*field != ':') {
	rpmError(RPMERR_BADSPEC, _("line %d: Malformed tag: %s\n"),
		 spec->lineNum, spec->line);
	return RPMERR_BADSPEC;
    }
    field++;
    SKIPSPACE(field);
    if (!*field) {
	/* Empty field */
	rpmError(RPMERR_BADSPEC, _("line %d: Empty tag: %s\n"),
		 spec->lineNum, spec->line);
	return RPMERR_BADSPEC;
    }
    end = findLastChar(field);
    *(end+1) = '\0';

    /* See if this is multi-token */
    end = field;
    SKIPNONSPACE(end);
    if (*end != '\0')
	multiToken = 1;

    switch (tag) {
      case RPMTAG_NAME:
	SINGLE_TOKEN_ONLY;
	if (rpmCharCheck(spec, field, "-._+", ".."))
	    return RPMERR_BADSPEC;
	(void) headerAddEntry(pkg->header, tag, RPM_STRING_TYPE, field, 1);
	break;
      case RPMTAG_VERSION:
	SINGLE_TOKEN_ONLY;
	if (rpmCharCheck(spec, field, "._+~", ".."))
	    return RPMERR_BADSPEC;
	/* This macro is for backward compatibility */
	addMacro(spec->macros, "PACKAGE_VERSION", NULL, field, RMIL_OLDSPEC);
	(void) headerAddEntry(pkg->header, tag, RPM_STRING_TYPE, field, 1);
	break;
      case RPMTAG_RELEASE:
	if (rpmCharCheck(spec, field, "._+~", ".."))
	    return RPMERR_BADSPEC;
	/* This macro is for backward compatibility */
	addMacro(spec->macros, "PACKAGE_RELEASE", NULL, field, RMIL_OLDSPEC-1);
	(void) headerAddEntry(pkg->header, tag, RPM_STRING_TYPE, field, 1);
	break;
      case RPMTAG_DISTTAG:
	SINGLE_TOKEN_ONLY;
	if (rpmCharCheck(spec, field, "._+", ".."))
	    return RPMERR_BADSPEC;
	(void) headerAddEntry(pkg->header, tag, RPM_STRING_TYPE, field, 1);
	break;
      case RPMTAG_URL:
      case RPMTAG_VCS:
	SINGLE_TOKEN_ONLY;
	(void) headerAddEntry(pkg->header, tag, RPM_STRING_TYPE, field, 1);
	break;
      case RPMTAG_GROUP:
      case RPMTAG_SUMMARY:
	(void) stashSt(spec, pkg->header, tag, lang);
	/*@fallthrough@*/
      case RPMTAG_DISTRIBUTION:
      case RPMTAG_VENDOR:
      case RPMTAG_LICENSE:
      case RPMTAG_PACKAGER:
      case RPMTAG_BUILDHOST:
	if (!*lang)
	    (void) headerAddEntry(pkg->header, tag, RPM_STRING_TYPE, field, 1);
	else if (!(noLang && strcmp(lang, RPMBUILD_DEFAULT_LANG)))
	    (void) headerAddI18NString(pkg->header, tag, field, lang);
	break;
      case RPMTAG_BUILDROOT:
	SINGLE_TOKEN_ONLY;
	/* Just ignore legacy tag. */
	return 0;
      case RPMTAG_PREFIXES:
	addOrAppendListEntry(pkg->header, tag, field);
	xx = hge(pkg->header, tag, &type, (void **)&array, &num);
	while (num--) {
	    len = strlen(array[num]);
	    if (array[num][len - 1] == '/' && len > 1) {
		rpmError(RPMERR_BADSPEC,
			 _("line %d: Prefixes must not end with \"/\": %s\n"),
			 spec->lineNum, spec->line);
		array = hfd(array, type);
		return RPMERR_BADSPEC;
	    }
	}
	array = hfd(array, type);
	break;
      case RPMTAG_DOCDIR:
	SINGLE_TOKEN_ONLY;
	if (field[0] != '/') {
	    rpmError(RPMERR_BADSPEC,
		     _("line %d: Docdir must begin with '/': %s\n"),
		     spec->lineNum, spec->line);
	    return RPMERR_BADSPEC;
	}
	macro = NULL;
	delMacro(NULL, "_docdir");
	addMacro(NULL, "_docdir", NULL, field, RMIL_SPEC);
	break;
      case RPMTAG_EPOCH:
	SINGLE_TOKEN_ONLY;
	if (parseNum(field, &num)) {
	    rpmError(RPMERR_BADSPEC,
		     _("line %d: Epoch/Serial field must be a number: %s\n"),
		     spec->lineNum, spec->line);
	    return RPMERR_BADSPEC;
	}
	xx = headerAddEntry(pkg->header, tag, RPM_INT32_TYPE, &num, 1);
	break;
      case RPMTAG_AUTOREQPROV:
	pkg->autoReq = parseReqProv(field);
	pkg->autoProv = xstrdup(pkg->autoReq);
	break;
      case RPMTAG_AUTOREQ:
	pkg->autoReq = parseReqProv(field);
	break;
      case RPMTAG_AUTOPROV:
	pkg->autoProv = parseReqProv(field);
	break;
      case RPMTAG_SOURCE:
      case RPMTAG_PATCH:
	SINGLE_TOKEN_ONLY;
	macro = NULL;
	if ((rc = addSource(spec, pkg, field, tag)))
	    return rc;
	break;
      case RPMTAG_ICON:
	SINGLE_TOKEN_ONLY;
	if ((rc = addSource(spec, pkg, field, tag)))
	    return rc;
#ifdef ENABLE_ICON_TAG
	if(!spec->preprocess_mode) {
	    if ((rc = readIcon(pkg->header, field)))
		return RPMERR_BADSPEC;
	}
#endif /* ENABLE_ICON_TAG */
	break;
      case RPMTAG_NOSOURCE:
      case RPMTAG_NOPATCH:
	spec->noSource = 1;
	if ((rc = parseNoSource(spec, field, tag)))
	    return rc;
	break;
      case RPMTAG_BUILDREQUIRES:
	if ((rc = parseBits(lang, buildScriptBits, &tagflags))) {
	    rpmError(RPMERR_BADSPEC,
		     _("line %d: Bad %s: qualifiers: %s\n"),
		     spec->lineNum, tagName(tag), spec->line);
	    return rc;
	}
	if ((rc = parseRCPOT(spec, pkg, field, tag, 0, tagflags)))
	    return rc;
	break;
      case RPMTAG_REQUIREFLAGS:
      case RPMTAG_PREREQ:
	if ((rc = parseBits(lang, installScriptBits, &tagflags))) {
	    rpmError(RPMERR_BADSPEC,
		     _("line %d: Bad %s: qualifiers: %s\n"),
		     spec->lineNum, tagName(tag), spec->line);
	    return rc;
	}
	if ((rc = parseRCPOT(spec, pkg, field, tag, 0, tagflags)))
	    return rc;
	break;
      case RPMTAG_BUILDPREREQ:
      case RPMTAG_BUILDCONFLICTS:
      case RPMTAG_CONFLICTFLAGS:
      case RPMTAG_OBSOLETEFLAGS:
      case RPMTAG_PROVIDEFLAGS:
	if ((rc = parseRCPOT(spec, pkg, field, tag, 0, tagflags)))
	    return rc;
	break;
      case RPMTAG_EXCLUDEARCH:
      case RPMTAG_EXCLUSIVEARCH:
      case RPMTAG_EXCLUDEOS:
      case RPMTAG_EXCLUSIVEOS:
	addOrAppendListEntry(spec->buildRestrictions, tag, field);
	break;
      case RPMTAG_BUILDARCHS:
      {
	const char **BANames = NULL;
	int BACount = 0;
	if ((rc = poptParseArgvString(field, &BACount, &BANames))) {
	    rpmError(RPMERR_BADSPEC,
		     _("line %d: Bad BuildArchitecture format: %s\n"),
		     spec->lineNum, spec->line);
	    return RPMERR_BADSPEC;
	}
	if (pkg == spec->packages) {
	    /* toplevel */
	    if (BACount > 0 && BANames != NULL) {
		spec->BACount = BACount;
		spec->BANames = BANames;
		BANames = NULL; /* don't free */
	    }
	}
	else {
	    /* subpackage */
	    if (BACount != 1 || strcmp(BANames[0], "noarch")) {
		rpmError(RPMERR_BADSPEC,
			 _("line %d: Only \"noarch\" sub-packages are supported: %s\n"),
			 spec->lineNum, spec->line);
		BANames = _free(BANames);
		return RPMERR_BADSPEC;
	    }
	    headerAddEntry(pkg->header, RPMTAG_ARCH, RPM_STRING_TYPE, "noarch", 1);
	}
	BANames = _free(BANames);
	break;
      }
      case RPMTAG_REMOVEPATHPOSTFIXES:
	if (strchr(field, ':')) {
	    rpmError(RPMERR_BADSPEC,
		     _("line %d: Multiple RemovePathPostfixes are not supported: %s\n"),
		     spec->lineNum, spec->line);
	    return RPMERR_BADSPEC;
	}
	pkg->removePostfixes = xstrdup(field);
	break;
      default:
	rpmError(RPMERR_INTERNAL, _("Internal error: Bogus tag %d\n"), tag);
	return RPMERR_INTERNAL;
    }

    if (macro)
	addMacro(spec->macros, macro, NULL, field, RMIL_SPEC);
    
    return 0;
}

/* This table has to be in a peculiar order.  If one tag is the */
/* same as another, plus a few letters, it must come first.     */

/**
 */
typedef const struct PreambleRec_s {
    rpmTag tag;
    unsigned type;
    unsigned len;
/*@observer@*/ /*@null@*/ const char * token;
} * PreambleRec;

#define LEN_AND_STR(_tag) (sizeof(_tag)-1), _tag

/*@unchecked@*/
static struct PreambleRec_s const preambleList[] = {
    {RPMTAG_NAME,		0, LEN_AND_STR("name")},
    {RPMTAG_VERSION,		0, LEN_AND_STR("version")},
    {RPMTAG_RELEASE,		0, LEN_AND_STR("release")},
    {RPMTAG_EPOCH,		0, LEN_AND_STR("epoch")},
    {RPMTAG_EPOCH,		0, LEN_AND_STR("serial")},
    {RPMTAG_SUMMARY,		1, LEN_AND_STR("summary")},
    {RPMTAG_LICENSE,		0, LEN_AND_STR("copyright")},
    {RPMTAG_LICENSE,		0, LEN_AND_STR("license")},
    {RPMTAG_DISTRIBUTION,	0, LEN_AND_STR("distribution")},
    {RPMTAG_DISTURL,		0, LEN_AND_STR("disturl")},
    {RPMTAG_BUILDHOST,		0, LEN_AND_STR("buildhost")},
    {RPMTAG_VENDOR,		0, LEN_AND_STR("vendor")},
    {RPMTAG_GROUP,		1, LEN_AND_STR("group")},
    {RPMTAG_PACKAGER,		0, LEN_AND_STR("packager")},
    {RPMTAG_URL,		0, LEN_AND_STR("url")},
    {RPMTAG_VCS,		0, LEN_AND_STR("vcs")},
    {RPMTAG_SOURCE,		0, LEN_AND_STR("source")},
    {RPMTAG_PATCH,		0, LEN_AND_STR("patch")},
    {RPMTAG_NOSOURCE,		0, LEN_AND_STR("nosource")},
    {RPMTAG_NOPATCH,		0, LEN_AND_STR("nopatch")},
    {RPMTAG_EXCLUDEARCH,	0, LEN_AND_STR("excludearch")},
    {RPMTAG_EXCLUSIVEARCH,	0, LEN_AND_STR("exclusivearch")},
    {RPMTAG_EXCLUDEOS,		0, LEN_AND_STR("excludeos")},
    {RPMTAG_EXCLUSIVEOS,	0, LEN_AND_STR("exclusiveos")},
    {RPMTAG_ICON,		0, LEN_AND_STR("icon")},
    {RPMTAG_PROVIDEFLAGS,	0, LEN_AND_STR("provides")},
    {RPMTAG_REQUIREFLAGS,	2, LEN_AND_STR("requires")},
    {RPMTAG_PREREQ,		2, LEN_AND_STR("prereq")},
    {RPMTAG_CONFLICTFLAGS,	0, LEN_AND_STR("conflicts")},
    {RPMTAG_OBSOLETEFLAGS,	0, LEN_AND_STR("obsoletes")},
    {RPMTAG_PREFIXES,		0, LEN_AND_STR("prefixes")},
    {RPMTAG_PREFIXES,		0, LEN_AND_STR("prefix")},
    {RPMTAG_BUILDROOT,		0, LEN_AND_STR("buildroot")},
    {RPMTAG_BUILDARCHS,		0, LEN_AND_STR("buildarchitectures")},
    {RPMTAG_BUILDARCHS,		0, LEN_AND_STR("buildarch")},
    {RPMTAG_BUILDCONFLICTS,	0, LEN_AND_STR("buildconflicts")},
    {RPMTAG_BUILDPREREQ,	0, LEN_AND_STR("buildprereq")},
    {RPMTAG_BUILDREQUIRES,	2, LEN_AND_STR("buildrequires")},
    {RPMTAG_AUTOREQPROV,	0, LEN_AND_STR("autoreqprov")},
    {RPMTAG_AUTOREQ,		0, LEN_AND_STR("autoreq")},
    {RPMTAG_AUTOPROV,		0, LEN_AND_STR("autoprov")},
    {RPMTAG_DOCDIR,		0, LEN_AND_STR("docdir")},
    {RPMTAG_DISTTAG,		0, LEN_AND_STR("disttag")},
    {RPMTAG_REMOVEPATHPOSTFIXES,0, LEN_AND_STR("removepathpostfixes")},
    /*@-nullassign@*/	/* LCL: can't add null annotation */
    {0, 0, 0, 0}
    /*@=nullassign@*/
};

/**
 */
static int findPreambleTag(Spec spec, /*@out@*/int * tag,
		/*@null@*/ /*@out@*/ const char ** macro, /*@out@*/ char * lang)
	/*@modifies *tag, *macro, *lang @*/
{
    PreambleRec p;
    char *s;

    for (p = preambleList; p->token != NULL; p++) {
	if (p->token && !xstrncasecmp(spec->line, p->token, p->len))
	    break;
    }
    if (p->token == NULL)
	return 1;

    s = spec->line + p->len;
    SKIPSPACE(s);

    switch (p->type) {
    default:
    case 0:
	/* Unless this is a source or a patch, a ':' better be next */
	if (p->tag != RPMTAG_SOURCE && p->tag != RPMTAG_PATCH) {
	    if (*s != ':') return 1;
	}
	*lang = '\0';
	break;
    case 1:	/* Parse optional ( <token> ). */
    case 2:
	if (*s == ':') {
	    /* Type 1 is multilang, 2 is qualifiers with no defaults */
	    strcpy(lang, (p->type == 1) ? RPMBUILD_DEFAULT_LANG : "");
	    break;
	}
	if (*s != '(') return 1;
	s++;
	SKIPSPACE(s);
	while (!xisspace(*s) && *s != ')')
	    *lang++ = *s++;
	*lang = '\0';
	SKIPSPACE(s);
	if (*s != ')') return 1;
	s++;
	SKIPSPACE(s);
	if (*s != ':') return 1;
	break;
    }

    *tag = p->tag;
    if (macro)
	/*@-onlytrans -observertrans -dependenttrans@*/	/* FIX: double indirection. */
	*macro = p->token;
	/*@=onlytrans =observertrans =dependenttrans@*/
    return 0;
}

int parsePreamble(Spec spec, int initialPackage)
{
    int nextPart;
    int tag, rc, xx;
    char *name, *linep;
    int flag;
    Package pkg;
    char NVR[BUFSIZ];
    char lang[BUFSIZ];

    strcpy(NVR, "(main package)");

    pkg = newPackage(spec);
	
    if (! initialPackage) {
	/* There is one option to %package: <pkg> or -n <pkg> */
	if (parseSimplePart(spec->line, &name, &flag)) {
	    rpmError(RPMERR_BADSPEC, _("Bad package specification: %s\n"),
			spec->line);
	    return RPMERR_BADSPEC;
	}
	
	if (!lookupPackage(spec, name, flag, NULL)) {
	    rpmError(RPMERR_BADSPEC, _("Package already exists: %s\n"),
			spec->line);
	    return RPMERR_BADSPEC;
	}
	
	/* Construct the package */
	if (flag == PART_SUBNAME) {
	    const char * mainName;
	    xx = headerName(spec->packages->header, &mainName);
	    sprintf(NVR, "%s-%s", mainName, name);
	} else
	    strcpy(NVR, name);
	xx = headerAddEntry(pkg->header, RPMTAG_NAME, RPM_STRING_TYPE, NVR, 1);
    }

    if ((rc = readLine(spec, STRIP_TRAILINGSPACE | STRIP_COMMENTS)) == 1) {
	nextPart = PART_NONE;
    } else {
	if (rc)
	    return rc;
	while (! (nextPart = isPart(spec->line))) {
	    const char * macro;
	    /* Skip blank lines */
	    linep = spec->line;
	    SKIPSPACE(linep);
	    if (*linep != '\0') {
		if (findPreambleTag(spec, &tag, &macro, lang)) {
		    rpmError(RPMERR_BADSPEC, _("line %d: Unknown tag: %s\n"),
				spec->lineNum, spec->line);
		    return RPMERR_BADSPEC;
		}
		if (handlePreambleTag(spec, pkg, tag, macro, lang))
		    return RPMERR_BADSPEC;
		if (spec->BANames && !spec->recursing)
		    return PART_BUILDARCHITECTURES;
	    }
	    if ((rc =
		 readLine(spec, STRIP_TRAILINGSPACE | STRIP_COMMENTS)) == 1) {
		nextPart = PART_NONE;
		break;
	    }
	    if (rc)
		return rc;
	}
    }

    /* Do some final processing on the header */
    
    if (!spec->gotBuildRootURL && spec->buildRootURL) {
	rpmError(RPMERR_BADSPEC, _("Spec file can't use BuildRoot\n"));
	return RPMERR_BADSPEC;
    }

    if (!spec->buildRootURL) {
	spec->buildRootURL = rpmGenPath(NULL, "%{?buildroot:%{buildroot}}", NULL);
	if (strcmp(spec->buildRootURL, "/"))
	    spec->gotBuildRootURL = 1;
	else
	{
	    spec->buildRootURL = NULL;
	    rpmError(RPMERR_BADSPEC, _("Neither spec file nor macros define BuildRoot"));
	    return RPMERR_BADSPEC;
	}
    }

    /* XXX Skip valid arch check if not building binary package */
    if (!spec->anyarch && checkForValidArchitectures(spec))
	return RPMERR_BADSPEC;

    if (pkg == spec->packages)
	fillOutMainPackage(pkg->header);

    if (checkForDuplicates(pkg->header, NVR))
	return RPMERR_BADSPEC;

    if (pkg != spec->packages)
	headerCopyTags(spec->packages->header, pkg->header,
			(int_32 *)copyTagsDuringParse);

    if (checkForRequired(pkg->header, NVR))
	return RPMERR_BADSPEC;

    return nextPart;
}
