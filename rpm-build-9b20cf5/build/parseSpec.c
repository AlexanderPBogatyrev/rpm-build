/** \ingroup rpmbuild
 * \file build/parseSpec.c
 *  Top level dispatcher for spec file parsing.
 */

#include "system.h"

/*@unchecked@*/
static int _debug = 0;

extern	char	*_rpm_nosource, *_rpm_nopatch;

#include "rpmio_internal.h"
#include "rpmbuild.h"
#include "debug.h"

/*@access FD_t @*/	/* compared with NULL */

/**
 */
/*@unchecked@*/
static struct PartRec {
    int part;
    int len;
/*@observer@*/ /*@null@*/ const char * token;
} partList[] = {
    { PART_PREAMBLE,      0, "%package"},
    { PART_PREP,          0, "%prep"},
    { PART_CONF,          0, "%conf"},
    { PART_BUILD,         0, "%build"},
    { PART_INSTALL,       0, "%install"},
    { PART_CHECK,         0, "%check"},
    { PART_CLEAN,         0, "%clean"},
    { PART_PREUN,         0, "%preun"},
    { PART_POSTUN,        0, "%postun"},
    { PART_PRETRANS,      0, "%pretrans"},
    { PART_PRE,           0, "%pre"},
    { PART_POST,          0, "%post"},
    { PART_FILES,         0, "%files"},
    { PART_CHANGELOG,     0, "%changelog"},
    { PART_DESCRIPTION,   0, "%description"},
    { PART_TRIGGERPOSTUN, 0, "%triggerpostun"},
    { PART_TRIGGERUN,     0, "%triggerun"},
    { PART_TRIGGERIN,     0, "%triggerin"},
    { PART_TRIGGERIN,     0, "%trigger"},
    { PART_VERIFYSCRIPT,  0, "%verifyscript"},
    {0, 0, 0}
};

/**
 */
static inline void initParts(struct PartRec *p)
	/*@modifies p->len @*/
{
    for (; p->token != NULL; p++)
	p->len = strlen(p->token);
}

rpmParseState isPart(const char *line)
{
    struct PartRec *p;

    if (partList[0].len == 0)
	initParts(partList);
    
    for (p = partList; p->token != NULL; p++) {
	char c;
	if (xstrncasecmp(line, p->token, p->len))
	    continue;
	c = *(line + p->len);
	if (c == '\0' || xisspace(c))
	    break;
    }

    return (p->token ? p->part : PART_NONE);
}

/**
 */
static int matchTok(const char *token, const char *line)
	/*@*/
{
    const char *b, *be = line;
    size_t toklen = strlen(token);
    int rc = 0;

    while ( *(b = be) != '\0' ) {
	SKIPSPACE(b);
	be = b;
	SKIPNONSPACE(be);
	if (be == b)
	    break;
	if (toklen != (be-b) || xstrncasecmp(token, b, (be-b)))
	    continue;
	rc = 1;
	break;
    }

    return rc;
}

void handleComments(char *s)
{
    SKIPSPACE(s);
    if (*s == '#')
	*s = '\0';
}

static int isCommentLine(const char *s)
{
    SKIPSPACE(s);
    return (*s == '#');
}

/**
 */
static void forceIncludeFile(Spec spec, const char * fileName)
	/*@modifies spec->fileStack @*/
{
    OFI_t * ofi;

    ofi = newOpenFileInfo();
    ofi->fileName = xstrdup(fileName);
    ofi->next = spec->fileStack;
    spec->fileStack = ofi;
}

/**
 */
static int copyNextLine(Spec spec, OFI_t *ofi, int strip)
	/*@globals rpmGlobalMacroContext,
		fileSystem @*/
	/*@modifies spec->nextline, spec->nextpeekc, spec->lbuf, spec->line,
		ofi->readPtr,
		rpmGlobalMacroContext, fileSystem @*/
{
    char *last;
    char ch;

    /* Restore 1st char in (possible) next line */
    if (spec->nextline != NULL && spec->nextpeekc != '\0') {
	*spec->nextline = spec->nextpeekc;
	spec->nextpeekc = '\0';
    }
    /* Expand next line from file into line buffer */
    if (!(spec->nextline && *spec->nextline)) {
	int pc = 0, bc = 0, nc = 0;
	char *from, *to, *p, *end;
	to = spec->lbufPtr ? spec->lbufPtr : spec->lbuf;
	end = spec->lbuf + spec->lbuf_len - 1;
	from = ofi->readPtr;
	ch = ' ';
	while (*from && ch != '\n' && to < end)
	    ch = *to++ = *from++;
	spec->lbufPtr = to;
	*to++ = '\0';
	ofi->readPtr = from;
	if (*from && ch != '\n') {
	    rpmError(RPMERR_BADSPEC, _("line %d: %s\n"),
		spec->lineNum, _("Target buffer overflow"));
	    return RPMERR_BADSPEC;
	}

	/* Check if we need another line before expanding the buffer. */
	for (p = spec->lbuf; *p; p++) {
	    switch (*p) {
		case '\\':
		    switch (*(p+1)) {
			case '\n': p++, nc = 1; break;
			case '\0': break;
			default: p++; break;
		    }
		    break;
		case '\n': nc = 0; break;
		case '%':
		    switch (*(p+1)) {
			case '{': p++, bc++; break;
			case '(': p++, pc++; break;
			case '%': p++; break;
		    }
		    break;
		case '{': if (bc > 0) bc++; break;
		case '}': if (bc > 0) bc--; break;
		case '(': if (pc > 0) pc++; break;
		case ')': if (pc > 0) pc--; break;
	    }
	}
	
	/* If it doesn't, ask for one more line. We need a better
	 * error code for this. */
	if (pc || bc || nc ) {
	    spec->nextline = "";
	    return RPMERR_UNMATCHEDIF;
	}
	spec->lbufPtr = spec->lbuf;

	/* Don't expand macros (eg. %define) in false branch of %if clause */
	if (spec->readStack->reading) {
	    int failed_ok;
	    int saved_lookup_failed;
	    int rc;

	    failed_ok = saved_lookup_failed = rpmSetBuiltinMacroLookupFailedOK(0);
	    if (!failed_ok)
	        failed_ok = (strip & STRIP_COMMENTS) && isCommentLine(spec->lbuf);
	    rpmSetBuiltinMacroLookupFailedOK(failed_ok);
	    rc = expandMacros(spec, spec->macros, spec->lbuf, spec->lbuf_len);
	    rpmSetBuiltinMacroLookupFailedOK(saved_lookup_failed);
	    if (rc) {
		rpmError(RPMERR_BADSPEC, _("line %d: %s\n"),
			spec->lineNum, spec->lbuf);
		return RPMERR_BADSPEC;
	    }
	}
	spec->nextline = spec->lbuf;
    }

    /* Find next line in expanded line buffer */
    spec->line = last = spec->nextline;
    ch = ' ';
    while (*spec->nextline && ch != '\n') {
	ch = *spec->nextline++;
	if (!xisspace(ch))
	    last = spec->nextline;
    }

    /* Save 1st char of next line in order to terminate current line. */
    if (*spec->nextline != '\0') {
	spec->nextpeekc = *spec->nextline;
	*spec->nextline = '\0';
    }
    
    if (strip & STRIP_COMMENTS)
	handleComments(spec->line);
    
    if (strip & STRIP_REDSPACE) {
	    /* If last character is LF strip trailing white spaces and
	     * terminate with lF, otherwise do nothing. */
	    if (spec->nextline > spec->line &&
		(last + 1) < spec->nextline &&
		*(spec->nextline - 1) == '\n') {
		    *last++ = '\n';
		    *last = '\0';
	    }
    }

    if (strip & STRIP_TRAILINGSPACE)
	*last = '\0';

    return 0;
}

static
FILE *tmpfp = NULL; /* temporary file for `rpmbuild -bE' preprocessor */

int readLine(Spec spec, int strip)
{
#ifdef	DYING
    const char *arch;
    const char *os;
#endif
    char  *s;
    int match;
    struct ReadLevelEntry *rl;
    OFI_t *ofi = spec->fileStack;
    int rc;

retry:
    /* Make sure the current file is open */
    /*@-branchstate@*/
    if (ofi->fd == NULL) {
	ofi->fd = Fopen(ofi->fileName, "r.fpio");
	if (ofi->fd == NULL || Ferror(ofi->fd)) {
	    /* XXX Fstrerror */
	    rpmError(RPMERR_BADSPEC, _("Unable to open %s: %s\n"),
		     ofi->fileName, Fstrerror(ofi->fd));
	    return RPMERR_BADSPEC;
	}
	spec->lineNum = ofi->lineNum = 0;
    }
    /*@=branchstate@*/

    /* Make sure we have something in the read buffer */
    if (!(ofi->readPtr && *(ofi->readPtr))) {
	/*@-type@*/ /* FIX: cast? */
	FILE * f = fdGetFp(ofi->fd);
	/*@=type@*/
	if (f == NULL || getline(&ofi->readBuf, &ofi->readBufSize, f) <= 0) {
	    /* EOF */
	    if (spec->readStack->next) {
		rpmError(RPMERR_UNMATCHEDIF, _("Unclosed %%if\n"));
	        return RPMERR_UNMATCHEDIF;
	    }

	    /* remove this file from the stack */
	    spec->fileStack = ofi->next;
	    ofi = freeOpenFileInfo(ofi);

	    /* only on last file do we signal EOF to caller */
	    ofi = spec->fileStack;
	    if (ofi == NULL)
		return 1;

	    /* otherwise, go back and try the read again. */
	    goto retry;
	}
	ofi->readPtr = ofi->readBuf;
	ofi->lineNum++;
	spec->lineNum = ofi->lineNum;
	if (spec->sl) {
	    speclines sl = spec->sl;
	    if (sl->sl_nlines == sl->sl_nalloc) {
		sl->sl_nalloc += 100;
		sl->sl_lines = (char **) xrealloc(sl->sl_lines, 
			sl->sl_nalloc * sizeof(*(sl->sl_lines)));
	    }
	    sl->sl_lines[sl->sl_nlines++] = xstrdup(ofi->readBuf);
	}
    }
    
#ifdef	DYING
    arch = NULL;
    rpmGetArchInfo(&arch, NULL);
    os = NULL;
    rpmGetOsInfo(&os, NULL);
#endif

    /* Copy next file line into the spec line buffer */
    if ((rc = copyNextLine(spec, ofi, strip)) != 0) {
	if (rc == RPMERR_UNMATCHEDIF)
	    goto retry;
	return rc;
    }

    s = spec->line;
    SKIPSPACE(s);

    match = -1;
    if (!spec->readStack->reading && !strncmp("%if", s, sizeof("%if")-1)) {
	match = 0;
    } else if (! strncmp("%ifarch", s, sizeof("%ifarch")-1)) {
	const char *arch = rpmExpand("%{_target_cpu}", NULL);
	s += 7;
	match = matchTok(arch, s);
	arch = _free(arch);
    } else if (! strncmp("%elifarch", s, sizeof("%elifarch")-1)) {
	s += 9;
	if (! spec->readStack->next) {
	    /* Got an %elifarch with no %if ! */
	    rpmError(RPMERR_UNMATCHEDIF,
		_("%s:%d: Got a %%elifarch with no %%if\n"),
		ofi->fileName, ofi->lineNum);
	    return RPMERR_UNMATCHEDIF;
	}
	if (spec->readStack->matched_once) {
	    spec->readStack->reading = 0;
	} else {
	    const char *arch = rpmExpand("%{_target_cpu}", NULL);
	    const char *cond = rpmExpand(s, NULL);
	    spec->readStack->matched_once = matchTok(arch, cond);
	    arch = _free(arch);
	    cond = _free(cond);
	    spec->readStack->reading =
		spec->readStack->next->reading && spec->readStack->matched_once;
	}
	spec->line[0] = '\0';
    } else if (! strncmp("%ifnarch", s, sizeof("%ifnarch")-1)) {
	const char *arch = rpmExpand("%{_target_cpu}", NULL);
	s += 8;
	match = !matchTok(arch, s);
	arch = _free(arch);
    } else if (! strncmp("%ifos", s, sizeof("%ifos")-1)) {
	const char *os = rpmExpand("%{_target_os}", NULL);
	s += 5;
	match = matchTok(os, s);
	os = _free(os);
    } else if (! strncmp("%ifnos", s, sizeof("%ifnos")-1)) {
	const char *os = rpmExpand("%{_target_os}", NULL);
	s += 6;
	match = !matchTok(os, s);
	os = _free(os);
    } else if (! strncmp("%if", s, sizeof("%if")-1)) {
	s += 3;
        match = parseExpressionBoolean(spec, s);
	if (match < 0) {
	    rpmError(RPMERR_UNMATCHEDIF,
			_("%s:%d: parseExpressionBoolean returns %d\n"),
			ofi->fileName, ofi->lineNum, match);
	    return RPMERR_BADSPEC;
	}
    } else if (! strncmp("%elif", s, sizeof("%elif")-1)) {
	s += 5;
	if (! spec->readStack->next) {
	    /* Got an %elif with no %if ! */
	    rpmError(RPMERR_UNMATCHEDIF,
			_("%s:%d: Got a %%elif with no %%if\n"),
			ofi->fileName, ofi->lineNum);
	    return RPMERR_UNMATCHEDIF;
	}
	if (spec->readStack->matched_once) {
		spec->readStack->reading = 0;
	} else {
	    /*
	     * Since we are in non-'reading' branch macros aren't expanded
	     * in copyNextLine, thus we need to expand them here. Same is
	     * true for %elifarch above.
	     */
	    const char *cond = rpmExpand(s, NULL);
	    spec->readStack->matched_once = parseExpressionBoolean(spec, cond);
	    cond = _free(cond);
	    if (spec->readStack->matched_once < 0) {
		rpmError(RPMERR_UNMATCHEDIF,
		    _("%s:%d: parseExpressionBoolean returns %d\n"),
		    ofi->fileName, ofi->lineNum, spec->readStack->matched_once);
		return RPMERR_BADSPEC;
	    }
	    spec->readStack->reading =
		spec->readStack->next->reading && spec->readStack->matched_once;
	}
	spec->line[0] = '\0';
    } else if (! strncmp("%else", s, sizeof("%else")-1)) {
	s += 5;
	if (! spec->readStack->next) {
	    /* Got an else with no %if ! */
	    rpmError(RPMERR_UNMATCHEDIF,
			_("%s:%d: Got a %%else with no %%if\n"),
			ofi->fileName, ofi->lineNum);
	    return RPMERR_UNMATCHEDIF;
	}
	while (*s && xisspace(*s)) s++;
	if (*s && *s != '#')
	    rpmError(RPMERR_BADSPEC, _("%s:%d: malformed %%else statement with garbage: '%s'\n"),
		ofi->fileName, ofi->lineNum, s);
	spec->readStack->matched_once = ! spec->readStack->matched_once;
	spec->readStack->reading =
	    spec->readStack->next->reading && spec->readStack->matched_once;
	spec->line[0] = '\0';
    } else if (! strncmp("%endif", s, sizeof("%endif")-1)) {
	s += 6;
	if (! spec->readStack->next) {
	    /* Got an end with no %if ! */
	    rpmError(RPMERR_UNMATCHEDIF,
			_("%s:%d: Got a %%endif with no %%if\n"),
			ofi->fileName, ofi->lineNum);
	    return RPMERR_UNMATCHEDIF;
	}
	while (*s && xisspace(*s)) s++;
	if (*s && *s != '#')
	    rpmError(RPMERR_BADSPEC, _("%s:%d: malformed %%endif statement with garbage: '%s'\n"),
		ofi->fileName, ofi->lineNum, s);
	rl = spec->readStack;
	spec->readStack = spec->readStack->next;
	free(rl);
	spec->line[0] = '\0';
    } else if (! strncmp("%include", s, sizeof("%include")-1)) {
	char *fileName, *endFileName, *p;

	s += 8;
	fileName = s;
	if (! xisspace(*fileName)) {
	    rpmError(RPMERR_BADSPEC, _("malformed %%include statement\n"));
	    return RPMERR_BADSPEC;
	}
	SKIPSPACE(fileName);
	endFileName = fileName;
	SKIPNONSPACE(endFileName);
	p = endFileName;
	SKIPSPACE(p);
	if (*p != '\0') {
	    rpmError(RPMERR_BADSPEC, _("malformed %%include statement\n"));
	    return RPMERR_BADSPEC;
	}
	*endFileName = '\0';

	forceIncludeFile(spec, fileName);

	ofi = spec->fileStack;
	goto retry;
    }

    if (match != -1) {
	rl = xmalloc(sizeof(*rl));
	rl->reading = spec->readStack->reading && match;
	rl->matched_once = match;
	rl->next = spec->readStack;
	spec->readStack = rl;
	spec->line[0] = '\0';
    }

    if (! spec->readStack->reading) {
	spec->line[0] = '\0';
    }

    if (spec->preprocess_mode) {
	size_t len = strlen(spec->line);
	if (len > 0 && spec->line[len-1] == '\n')
	    len--; /* chomp */
	fprintf(tmpfp, "%.*s\n", (int)len, spec->line);
    }

    /*@-compmempass@*/ /* FIX: spec->readStack->next should be dependent */
    return 0;
    /*@=compmempass@*/
}

void closeSpec(Spec spec)
{
    while (spec->fileStack) {
	OFI_t *ofi = spec->fileStack;
	spec->fileStack = ofi->next;
	ofi = freeOpenFileInfo(ofi);
    }
}

/*@-redecl@*/
/*@unchecked@*/
extern int noLang;		/* XXX FIXME: pass as arg */
/*@=redecl@*/

typedef struct tags_struct {
    int len;
    const char *token;
} tags_struct;

static int
comp_tags(const void *m1, const void *m2)
{
	tags_struct *p1 = (tags_struct *) m1;
	tags_struct *p2 = (tags_struct *) m2;
	int     len = (p1->len < p2->len) ? p1->len : p2->len;
	int     rc = strncasecmp(p1->token, p2->token, len);

	if (rc)
		return rc;
	return p1->len - p2->len;
}

#define nr_of_tags(list) (sizeof(list)/sizeof(list[0]))

static tags_struct tags_common_list[] = {
    {0, "build"},
    {0, "changelog"},
    {0, "check"},
    {0, "clean"},
    {0, "conf"},
    {0, "description"},
    {0, "elif"},
    {0, "elifarch"},
    {0, "else"},
    {0, "endif"},
    {0, "files"},
    {0, "if"},
    {0, "ifarch"},
    {0, "ifnarch"},
    {0, "ifnos"},
    {0, "ifos"},
    {0, "include"},
    {0, "install"},
    {0, "package"},
    {0, "post"},
    {0, "postun"},
    {0, "pretrans"},
    {0, "pre"},
    {0, "prep"},
    {0, "preun"},
    {0, "trigger"},
    {0, "triggerin"},
    {0, "triggerpostun"},
    {0, "triggerun"},
    {0, "verifyscript"}
};

static tags_struct tags_files_list[] = {
    {0, "attr"},
    {0, "caps"},
    {0, "config"},
    {0, "defattr"},
    {0, "defverify"},
    {0, "dev"},
    {0, "dir"},
    {0, "doc"},
    {0, "docdir"},
    {0, "exclude"},
    {0, "ghost"},
    {0, "lang"},
    {0, "license"},
    {0, "readme"},
    {0, "verify"},
};

static const char *
is_builtin_tag(const char *line, int len, tags_struct *tlist, size_t nlist)
{
	int     i;
	tags_struct key;

	if (tlist[0].len == 0)
	{
		for (i = 0; i < nlist; ++i)
			tlist[i].len = strlen(tlist[i].token);
		qsort(tlist, nlist, sizeof(tags_struct), comp_tags);
	}

	for (i = 0; i < len; ++i)
	{
		if (line[i] == '\0' || line[i] == '(' || xisspace(line[i]))
		{
			len = i;
			break;
		}
	}

	key.token = line;
	key.len = len;
	if (bsearch
	    (&key, tlist, nlist, sizeof(tags_struct), comp_tags))
		return line;

	return NULL;
}

static const char *
is_builtin_preamble_tag(const char *line, int len)
{
	return is_builtin_tag(line, len, tags_common_list, nr_of_tags(tags_common_list));
}

static const char *
is_builtin_prep_tag(const char *line, int len)
{
	int     i;
	const char *rc;

	for (i = 0; i < len; ++i)
	{
		if (line[i] == '\0' || xisspace(line[i]))
		{
			len = i;
			break;
		}
	}

	if ((rc = is_builtin_tag(line, len, tags_common_list, nr_of_tags(tags_common_list))))
		return rc;

	if (len == 5 && !strncasecmp(line, "setup", 5))
		return line;

	if (len >= 5 && !strncasecmp(line, "patch", 5))
	{
		for (i = 5; i < len; ++i)
			if (!xisdigit(line[i]))
				break;
		if (i >= len)
			return line;
	}

	if (len >= sizeof("autopatch") - 1 &&
		  !strncasecmp(line, "autopatch", sizeof("autopatch") - 1))
	    return line;

	return 0;
}

static const char *
is_builtin_build_tag(const char *line, int len)
{
	return is_builtin_tag(line, len, tags_common_list, nr_of_tags(tags_common_list));
}

static const char *
is_builtin_changelog_tag(const char *line, int len)
{
	return is_builtin_tag(line, len, tags_common_list, nr_of_tags(tags_common_list));
}
static const char *
is_builtin_description_tag(const char *line, int len)
{
	return is_builtin_tag(line, len, tags_common_list, nr_of_tags(tags_common_list));
}

static const char *
is_builtin_script_tag(const char *line, int len)
{
	return is_builtin_tag(line, len, tags_common_list, nr_of_tags(tags_common_list));
}

static const char *
is_builtin_files_tag(const char *line, int len)
{
	const char *rc;

	if ((rc = is_builtin_tag(line, len, tags_common_list, nr_of_tags(tags_common_list))))
		return rc;
	return is_builtin_tag(line, len, tags_files_list, nr_of_tags(tags_files_list));
}

/*@todo Skip parse recursion if os is not compatible. @*/
int parseSpec(Spec *specp, const char *specFile, const char *rootURL,
		const char *buildRootURL, int recursing, const char *passPhrase,
		char *cookie, int anyarch, int force, int preprocess)
{
    rpmParseState parsePart = PART_PREAMBLE;
    int initialPackage = 1;
#ifdef	DYING
    const char *saveArch;
#endif
    Package pkg;
    Spec spec;
    
    /* Set up a new Spec structure with no packages. */
    spec = newSpec();

    /*
     * Note: rpmGetPath should guarantee a "canonical" path. That means
     * that the following pathologies should be weeded out:
     *          //bin//sh
     *          //usr//bin/
     *          /.././../usr/../bin//./sh (XXX FIXME: dots not handled yet)
     */
    spec->specFile = rpmGetPath(specFile, NULL);
    spec->fileStack = newOpenFileInfo();
    spec->fileStack->fileName = xstrdup(spec->specFile);
    spec->preprocess_mode = preprocess;

    if (spec->preprocess_mode) {
	if (!recursing){
	    assert(tmpfp == NULL);
	    tmpfp = tmpfile();
	    if (!tmpfp) {
		rpmError(RPMERR_CREATE,
			 _("Cannot create temporary file: %s"),
			 strerror(errno));
		return RPMERR_CREATE;
	    }
	}
	else {
	    assert(tmpfp);
	    fseek(tmpfp, 0, SEEK_SET);
	    ftruncate(fileno(tmpfp), 0);
	}
    }

    if (buildRootURL) {
	const char * buildRoot;
	(void) urlPath(buildRootURL, &buildRoot);
	/*@-branchstate@*/
	if (*buildRoot == '\0') buildRoot = "/";
	/*@=branchstate@*/
	if (!strcmp(buildRoot, "/")) {
            rpmError(RPMERR_BADSPEC,
                     _("BuildRoot can not be \"/\": %s\n"), buildRootURL);
            return RPMERR_BADSPEC;
        }
	spec->gotBuildRootURL = 1;
	spec->buildRootURL = xstrdup(buildRootURL);
	addMacro(spec->macros, "buildroot", NULL, buildRoot, RMIL_SPEC);
if (_debug)
fprintf(stderr, "*** PS buildRootURL(%s) %p macro set to %s\n", spec->buildRootURL, spec->buildRootURL, buildRoot);
    }
    addMacro(NULL, "_docdir", NULL, "%{_defaultdocdir}", RMIL_SPEC);
    spec->recursing = recursing;
    spec->anyarch = anyarch;
    spec->force = force;

    if (rootURL)
	spec->rootURL = xstrdup(rootURL);
    if (passPhrase)
	spec->passPhrase = xstrdup(passPhrase);
    if (cookie)
	spec->cookie = xstrdup(cookie);

    spec->timeCheck = rpmExpandNumeric("%{?_timecheck}");

    /* All the parse*() functions expect to have a line pre-read */
    /* in the spec's line buffer.  Except for parsePreamble(),   */
    /* which handles the initial entry into a spec file.         */
    
    /*@-infloops@*/	/* LCL: parsePart is modified @*/
    while (parsePart < PART_LAST && parsePart != PART_NONE) {
	rpmBuiltinMacroLookup saved_lookup = rpmSetBuiltinMacroLookup(NULL);
	int saved_lookup_failed =
		rpmSetBuiltinMacroLookupFailedOK(preprocess ||
			rpmExpandNumeric("%{?_allow_undefined_macros}"));
	switch (parsePart) {
	case PART_PREAMBLE:
	    rpmSetBuiltinMacroLookup(is_builtin_preamble_tag);
	    parsePart = parsePreamble(spec, initialPackage);
	    initialPackage = 0;
	    /*@switchbreak@*/ break;
	case PART_PREP:
	    rpmSetBuiltinMacroLookup(is_builtin_prep_tag);
	    rpmSetBuiltinMacroLookupFailedOK(1);
	    parsePart = parsePrep(spec);
	    /*@switchbreak@*/ break;
	case PART_CONF:
	case PART_BUILD:
	case PART_INSTALL:
	case PART_CHECK:
	case PART_CLEAN:
	    rpmSetBuiltinMacroLookup(is_builtin_build_tag);
	    rpmSetBuiltinMacroLookupFailedOK(1);
	    parsePart = parseBuildInstallClean(spec, parsePart);
	    /*@switchbreak@*/ break;
	case PART_CHANGELOG:
	    rpmSetBuiltinMacroLookup(is_builtin_changelog_tag);
	    rpmSetBuiltinMacroLookupFailedOK(1);
	    parsePart = parseChangelog(spec);
	    /*@switchbreak@*/ break;
	case PART_DESCRIPTION:
	    rpmSetBuiltinMacroLookup(is_builtin_description_tag);
	    parsePart = parseDescription(spec);
	    /*@switchbreak@*/ break;

	case PART_PRE:
	case PART_POST:
	case PART_PREUN:
	case PART_POSTUN:
	case PART_PRETRANS:
	case PART_VERIFYSCRIPT:
	case PART_TRIGGERIN:
	case PART_TRIGGERUN:
	case PART_TRIGGERPOSTUN:
	    rpmSetBuiltinMacroLookup(is_builtin_script_tag);
	    parsePart = parseScript(spec, parsePart);
	    /*@switchbreak@*/ break;

	case PART_FILES:
	    rpmSetBuiltinMacroLookup(is_builtin_files_tag);
	    parsePart = parseFiles(spec);
	    /*@switchbreak@*/ break;

	case PART_NONE:		/* XXX avoid gcc whining */
	case PART_LAST:
	case PART_BUILDARCHITECTURES:
	    /*@switchbreak@*/ break;
	}
	rpmSetBuiltinMacroLookup(saved_lookup);
	rpmSetBuiltinMacroLookupFailedOK(saved_lookup_failed);

	if (parsePart >= PART_LAST) {
	    spec = freeSpec(spec);
	    return parsePart;
	}

	if (parsePart == PART_BUILDARCHITECTURES) {
	    int index;
	    int x;

	    closeSpec(spec);

	    /* LCL: sizeof(spec->BASpecs[0]) -nullderef whine here */
	    spec->BASpecs = xcalloc(spec->BACount, sizeof(*spec->BASpecs));
	    index = 0;
	    if (spec->BANames != NULL)
	    for (x = 0; x < spec->BACount; x++) {

		/* Skip if not arch is not compatible. */
		if (!rpmMachineScore(RPM_MACHTABLE_BUILDARCH, spec->BANames[x]))
		    /*@innercontinue@*/ continue;
#ifdef	DYING
		rpmGetMachine(&saveArch, NULL);
		saveArch = xstrdup(saveArch);
		rpmSetMachine(spec->BANames[x], NULL);
#else
		addMacro(NULL, "_target_cpu", NULL, spec->BANames[x], RMIL_RPMRC);
#endif
		spec->BASpecs[index] = NULL;
		if (parseSpec(&(spec->BASpecs[index]),
				  specFile, spec->rootURL, buildRootURL, 1,
				  passPhrase, cookie, anyarch, force, preprocess))
		{
			spec->BACount = index;
			spec = freeSpec(spec);
			return RPMERR_BADSPEC;
		}
#if 0
#ifdef	DYING
		rpmSetMachine(saveArch, NULL);
		saveArch = _free(saveArch);
#else
		delMacro(NULL, "_target_cpu");
#endif
#endif
		index++;
	    }

	    spec->BACount = index;
	    if (! index) {
		spec = freeSpec(spec);
		rpmError(RPMERR_BADSPEC,
			_("No compatible architectures found for build\n"));
		return RPMERR_BADSPEC;
	    }

	    /*
	     * Return the 1st child's fully parsed Spec structure.
	     * The restart of the parse when encountering BuildArch
	     * causes problems for "rpm -q --specfile". This is
	     * still a hack because there may be more than 1 arch
	     * specified (unlikely but possible.) There's also the
	     * further problem that the macro context, particularly
	     * %{_target_cpu}, disagrees with the info in the header.
	     */
	    /*@-branchstate@*/
	    if (spec->BACount >= 1) {
		Spec nspec = spec->BASpecs[0];
		spec->BASpecs = _free(spec->BASpecs);
		spec = freeSpec(spec);
		spec = nspec;
	    }
	    /*@=branchstate@*/

	    *specp = spec;
	    return 0;
	}
    }
    /*@=infloops@*/	/* LCL: parsePart is modified @*/

    /* Check for description in each package and add arch and os */
  {
#ifdef	DYING
    const char *arch = NULL;
    const char *os = NULL;
    char *myos = NULL;

    rpmGetArchInfo(&arch, NULL);
    rpmGetOsInfo(&os, NULL);
    /*
     * XXX Capitalizing the 'L' is needed to insure that old
     * XXX os-from-uname (e.g. "Linux") is compatible with the new
     * XXX os-from-platform (e.g "linux" from "sparc-*-linux").
     * XXX A copy of this string is embedded in headers.
     */
    if (!strcmp(os, "linux")) {
	myos = xstrdup(os);
	*myos = 'L';
	os = myos;
    }
#else
    const char *arch = rpmExpand("%{_target_cpu}", NULL);
    const char *os = rpmExpand("%{_target_os}", NULL);
#endif

    for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
	if (!headerIsEntry(pkg->header, RPMTAG_DESCRIPTION)) {
	    const char * name;
	    (void) headerName(pkg->header, &name);
	    rpmError(RPMERR_BADSPEC, _("Package has no %%description: %s\n"),
			name);
	    spec = freeSpec(spec);
	    return RPMERR_BADSPEC;
	}

	(void) headerAddEntry(pkg->header, RPMTAG_OS, RPM_STRING_TYPE, os, 1);
	if (!headerIsEntry(pkg->header, RPMTAG_ARCH))
	    headerAddEntry(pkg->header, RPMTAG_ARCH, RPM_STRING_TYPE, arch, 1);
	else
	    assert(pkg != spec->packages); /* noarch subpackage */
    }

#ifdef	DYING
    myos = _free(myos);
#else
    arch = _free(arch);
    os = _free(os);
#endif
  }

    if ( _rpm_nosource || _rpm_nopatch )
    {
	spec->noSource = 1;
	if ( _rpm_nosource ) parseNoSource( spec, _rpm_nosource, RPMTAG_NOSOURCE );
	if( _rpm_nopatch ) parseNoSource( spec, _rpm_nopatch, RPMTAG_NOPATCH );
    }

    closeSpec(spec);
    *specp = spec;

    if (spec->preprocess_mode) {
	char buf[BUFSIZ];
	size_t n;
	assert(tmpfp);
	fseek(tmpfp, 0, SEEK_SET);
	while ((n = fread(buf, 1, sizeof(buf), tmpfp)))
	    fwrite(buf, 1, n, stdout);
	fclose(tmpfp);
	tmpfp = NULL;
    }

    return 0;
}
