/** \ingroup rpmbuild
 * \file build/parsePrep.c
 *  Parse %prep section from spec file.
 */

#include "system.h"

#include "rpmio_internal.h"
#include "rpmbuild.h"
#include "debug.h"

/*@access StringBuf @*/	/* compared with NULL */

/* These have to be global to make up for stupid compilers */
/*@unchecked@*/
    static int leaveDirs, skipDefaultAction;
/*@unchecked@*/
    static int createDir, quietly, verbosely;
/*@unchecked@*/
/*@observer@*/ /*@null@*/ static const char * dirName = NULL;
/*@unchecked@*/
/*@observer@*/ static struct poptOption optionsTable[] = {
	    { NULL, 'a', POPT_ARG_STRING, NULL, 'a',	NULL, NULL},
	    { NULL, 'b', POPT_ARG_STRING, NULL, 'b',	NULL, NULL},
	    { NULL, 'c', 0, &createDir, 0,		NULL, NULL},
	    { NULL, 'D', 0, &leaveDirs, 0,		NULL, NULL},
	    { NULL, 'n', POPT_ARG_STRING, &dirName, 0,	NULL, NULL},
	    { NULL, 'T', 0, &skipDefaultAction, 0,	NULL, NULL},
	    { NULL, 'q', 0, &quietly, 0,		NULL, NULL},
	    { NULL, 'v', 0, &verbosely, 0,		NULL, NULL},
	    { 0, 0, 0, 0, 0,	NULL, NULL}
    };

/**
 * Check that file owner and group are known.
 * @param urlfn		file url
 * @return		0 on success
 */
static int checkOwners(const char * urlfn)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    struct stat sb;

    if (Lstat(urlfn, &sb)) {
	rpmError(RPMERR_BADSPEC, _("Bad source: %s: %s\n"),
		urlfn, strerror(errno));
	return RPMERR_BADSPEC;
    }
    if (!getUname(sb.st_uid) || !getGname(sb.st_gid)) {
	rpmError(RPMERR_BADSPEC, _("Bad owner/group: %s\n"), urlfn);
	return RPMERR_BADSPEC;
    }

    return 0;
}

/**
 * Expand %patchN macro into %prep scriptlet.
 * @param spec		build info
 * @param c		patch index
 * @param strip		patch level (i.e. patch -p argument)
 * @param db		saved file suffix (i.e. patch --suffix argument)
 * @param reverse	include -R?
 * @param removeEmpties	include -E?
 * @param fuzz		fuzz factor, fuzz<0 means no fuzz set
 * @param dir		dir to change to (i.e. patch -d argument)
 * @return		expanded %patch macro (NULL on error)
 */
/*@observer@*/ static char *doPatch(Spec spec, int c, const char *strip, const char *db,
		     int reverse, int removeEmpties, int fuzz, const char *dir, int silent)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies rpmGlobalMacroContext, fileSystem @*/
{
    const char *arg_patch_flags = rpmExpand("%{?_default_patch_flags}", NULL);
    struct Source *sp;
    for (sp = spec->sources; sp != NULL; sp = sp->next) {
	if ((sp->flags & RPMBUILD_ISPATCH) && (sp->num == c)) {
	    break;
	}
    }
    if (sp == NULL) {
	rpmError(RPMERR_BADSPEC, _("No patch number %d\n"), c);
	return NULL;
    }

    const char *urlfn = rpmGetPath("%{_sourcedir}/", sp->source, NULL);

    char args[BUFSIZ];
    sprintf(args, "%s%s-p%s",
	arg_patch_flags, *arg_patch_flags ? " ": "", strip ?: "0");
    if (silent)
	strcat(args, " -s");
    if (db) {
	strcat(args, " -b --suffix ");
	strcat(args, db);
    }
    if (reverse)
	strcat(args, " -R");
    if (removeEmpties)
	strcat(args, " -E");
    if (dir) {
	strcat(args, " -d ");
	strcat(args, dir);
    }
    if (fuzz >= 0) {
	char str_fuzz[sizeof(int) * 3 + 1];
	sprintf(str_fuzz, "%d", fuzz);
	strcat(args, " -F ");
	strcat(args, str_fuzz);
    };

    rpmCompressedMagic compressed = COMPRESSED_NOT;
    /* XXX On non-build parse's, file cannot be stat'd or read */
    if (!spec->force && (isCompressed(urlfn, &compressed) || checkOwners(urlfn))) {
	urlfn = _free(urlfn);
	return NULL;
    }

    const char *fn = NULL;
    int urltype = urlPath(urlfn, &fn);
    switch (urltype) {
    case URL_IS_HTTP:	/* XXX WRONG WRONG WRONG */
    case URL_IS_FTP:	/* XXX WRONG WRONG WRONG */
    case URL_IS_PATH:
    case URL_IS_UNKNOWN:
	break;
    case URL_IS_DASH:
	urlfn = _free(urlfn);
	return NULL;
	/*@notreached@*/ break;
    }

    const char *t;
    const char *patcher = rpmGetPath("%{__patch}", NULL);
    char cmd[BUFSIZ];
    if (compressed != COMPRESSED_NOT) {
	switch (compressed) {
	default:	/* XXX can't happen */
	case COMPRESSED_OTHER:
	    t = "%{__gzip} -dc";
	    break;
	case COMPRESSED_BZIP2:
	    t = "%{__bzip2} -dc";
	    break;
	case COMPRESSED_ZIP:
	    t = "%{__unzip} -qq -p";
	    break;
	case COMPRESSED_LZMA:
	    t = "%{__lzma} -dc";
	    break;
	case COMPRESSED_XZ:
	    t = "%{__xz} -dc";
	    break;
	}
	const char *zipper = rpmGetPath(t, NULL);
	sprintf(cmd, "%s '%s' | %s %s\n", zipper, fn, patcher, args);
	zipper = _free(zipper);
    } else {
	sprintf(cmd, "%s %s < '%s'\n", patcher, args, fn);
    }
    patcher = _free(patcher);

    static char buf[BUFSIZ];
    sprintf(buf, "echo 'Patch #%d (%s):'\n", c, basename(fn));
    strcat(buf, cmd);

    urlfn = _free(urlfn);
    return buf;
}

/**
 * Expand %setup macro into %prep scriptlet.
 * @param spec		build info
 * @param c		source index
 * @param quietly	should -vv be omitted from tar?
 * @return		expanded %setup macro (NULL on error)
 */
/*@observer@*/ static const char *doUntar(Spec spec, int c, int quietly)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies rpmGlobalMacroContext, fileSystem @*/
{
    struct Source *sp;
    for (sp = spec->sources; sp != NULL; sp = sp->next) {
	if ((sp->flags & RPMBUILD_ISSOURCE) && (sp->num == c)) {
	    break;
	}
    }
    if (sp == NULL) {
	rpmError(RPMERR_BADSPEC, _("No source number %d\n"), c);
	return NULL;
    }

    const char *urlfn = rpmGetPath("%{_sourcedir}/", sp->source, NULL);

#ifdef AUTOFETCH_NOT	/* XXX don't expect this code to be enabled */
    /* XXX
     * XXX If nosource file doesn't exist, try to fetch from url.
     * XXX TODO: add a "--fetch" enabler.
     */
    if (sp->flags & RPMTAG_NOSOURCE && autofetchnosource) {
	struct stat st;
	int rc;
	if (Lstat(urlfn, &st) != 0 && errno == ENOENT &&
	    urlIsUrl(sp->fullSource) != URL_IS_UNKNOWN) {
	    if ((rc = urlGetFile(sp->fullSource, urlfn)) != 0) {
		rpmError(RPMERR_BADFILENAME,
			_("Couldn't download nosource %s: %s\n"),
			sp->fullSource, ftpStrerror(rc));
		return NULL;
	    }
	}
    }
#endif

    rpmCompressedMagic compressed = COMPRESSED_NOT;
    /* XXX On non-build parse's, file cannot be stat'd or read */
    if (!spec->force && (isCompressed(urlfn, &compressed) || checkOwners(urlfn))) {
	urlfn = _free(urlfn);
	return NULL;
    }

    const char *fn = NULL;
    int urltype = urlPath(urlfn, &fn);
    switch (urltype) {
    case URL_IS_HTTP:	/* XXX WRONG WRONG WRONG */
    case URL_IS_FTP:	/* XXX WRONG WRONG WRONG */
    case URL_IS_PATH:
    case URL_IS_UNKNOWN:
	break;
    case URL_IS_DASH:
	urlfn = _free(urlfn);
	return NULL;
	/*@notreached@*/ break;
    }

    const char *t;
    if (rpmIsVerbose() && !quietly)
	t = "%{__tar} -xvvf";
    else
	t = "%{__tar} -xf";
    const char *tar = rpmGetPath(t, NULL);
    char cmd[BUFSIZ];
    if (compressed != COMPRESSED_NOT) {
	switch (compressed) {
	default:	/* XXX can't happen */
	case COMPRESSED_OTHER:
	    t = "%{__gzip} -dc";
	    break;
	case COMPRESSED_BZIP2:
	    t = "%{__bzip2} -dc";
	    break;
	case COMPRESSED_ZIP:
	    if (rpmIsVerbose() && !quietly)
		t = "%{__unzip}";
	    else
		t = "%{__unzip} -qq";
	    break;
	case COMPRESSED_LZMA:
	    t = "%{__lzma} -dc";
	    break;
	case COMPRESSED_XZ:
	    t = "%{__xz} -dc";
	    break;
	}
	const char *zipper = rpmGetPath(t, NULL);
	if (compressed == COMPRESSED_ZIP)
	    sprintf(cmd, "%s '%s'\n", zipper, fn);
	else
	    sprintf(cmd, "%s '%s' | %s -\n", zipper, fn, tar);
	zipper = _free(zipper);
    } else {
	sprintf(cmd, "%s '%s'\n", tar, fn);
    }
    tar = _free(tar);

    static char buf[BUFSIZ];
    sprintf(buf, "echo 'Source #%d (%s):'\n", c, basename(fn));
    strcat(buf, cmd);

    urlfn = _free(urlfn);
    return buf;
}

/**
 * Parse %setup macro.
 * @todo FIXME: Option -q broken when not immediately after %setup.
 * @param spec		build info
 * @param line		current line from spec file
 * @return		0 on success
 */
static int doSetupMacro(Spec spec, char *line)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies spec->buildSubdir, spec->macros, spec->prep,
		rpmGlobalMacroContext, fileSystem @*/
{
    char buf[BUFSIZ];
    StringBuf before;
    StringBuf after;
    poptContext optCon;
    int argc;
    const char ** argv;
    int arg;
    const char * optArg;
    int rc;
    int num;

    /*@-mods@*/
    leaveDirs = skipDefaultAction = 0;
    createDir = verbosely = 0;
    quietly = 1;
    dirName = NULL;
    /*@=mods@*/

    if ((rc = poptParseArgvString(line, &argc, &argv))) {
	rpmError(RPMERR_BADSPEC, _("Error parsing %%setup: %s\n"),
			poptStrerror(rc));
	return RPMERR_BADSPEC;
    }

    before = newStringBuf();
    after = newStringBuf();

    optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
    while ((arg = poptGetNextOpt(optCon)) > 0) {
	optArg = poptGetOptArg(optCon);

	/* We only parse -a and -b here */

	if (parseNum(optArg, &num)) {
	    rpmError(RPMERR_BADSPEC, _("line %d: Bad arg to %%setup: %s\n"),
		     spec->lineNum, (optArg ? optArg : "???"));
	    before = freeStringBuf(before);
	    after = freeStringBuf(after);
	    optCon = poptFreeContext(optCon);
	    argv = _free(argv);
	    return RPMERR_BADSPEC;
	}

	{   const char *chptr = doUntar(spec, num, verbosely ? 0 : quietly);
	    if (chptr == NULL)
		return RPMERR_BADSPEC;

	    appendLineStringBuf((arg == 'a' ? after : before), chptr);
	}
    }

    if (arg < -1) {
	rpmError(RPMERR_BADSPEC, _("line %d: Bad %%setup option %s: %s\n"),
		 spec->lineNum,
		 poptBadOption(optCon, POPT_BADOPTION_NOALIAS), 
		 poptStrerror(arg));
	before = freeStringBuf(before);
	after = freeStringBuf(after);
	optCon = poptFreeContext(optCon);
	argv = _free(argv);
	return RPMERR_BADSPEC;
    }

    if (dirName) {
	spec->buildSubdir = xstrdup(dirName);
    } else {
	const char *name, *version;
	(void) headerNameVersion(spec->packages->header, &name, &version);
	snprintf(buf, sizeof(buf), "%s-%s", name, version);
	spec->buildSubdir = xstrdup(buf);
    }
    addMacro(spec->macros, "buildsubdir", NULL, spec->buildSubdir, RMIL_SPEC);
    
    optCon = poptFreeContext(optCon);
    argv = _free(argv);

    /* cd to the build dir */
    {	const char * buildDirURL = rpmGenPath(spec->rootURL, "%{_builddir}", "");
	const char *buildDir;

	(void) urlPath(buildDirURL, &buildDir);
	snprintf(buf, sizeof(buf), "cd '%s'", buildDir);
	appendLineStringBuf(spec->prep, buf);
	buildDirURL = _free(buildDirURL);
    }
    
    /* delete any old sources */
    if (!leaveDirs) {
	snprintf(buf, sizeof(buf), "rm -rf '%s'", spec->buildSubdir);
	appendLineStringBuf(spec->prep, buf);
    }

    /* if necessary, create and cd into the proper dir */
    if (createDir) {
	snprintf(buf, sizeof(buf), MKDIR_P " '%s'\ncd '%s'",
		spec->buildSubdir, spec->buildSubdir);
	appendLineStringBuf(spec->prep, buf);
    }

    /* do the default action */
   if (!createDir && !skipDefaultAction) {
	const char *chptr = doUntar(spec, 0, verbosely ? 0 : quietly);
	if (!chptr)
	    return RPMERR_BADSPEC;
	appendLineStringBuf(spec->prep, chptr);
    }

    appendStringBuf(spec->prep, getStringBuf(before));
    before = freeStringBuf(before);

    if (!createDir) {
	snprintf(buf, sizeof(buf), "cd '%s'", spec->buildSubdir);
	appendLineStringBuf(spec->prep, buf);
    }

    if (createDir && !skipDefaultAction) {
	const char * chptr = doUntar(spec, 0, verbosely ? 0 : quietly);
	if (chptr == NULL)
	    return RPMERR_BADSPEC;
	appendLineStringBuf(spec->prep, chptr);
    }
    
    appendStringBuf(spec->prep, getStringBuf(after));
    after = freeStringBuf(after);

    /* XXX FIXME: owner & group fixes were conditioned on !geteuid() */
    /* Fix the owner, group, and permissions of the setup build tree */
    {	/*@observer@*/ static const char *fixmacs[] =
		{ "%{?_fixowner}", "%{?_fixgroup}", "%{?_fixperms}", NULL };
	const char ** fm;

	for (fm = fixmacs; *fm; fm++) {
	    const char *fix;
	    /*@-nullpass@*/
	    fix = rpmExpand(*fm, " .", NULL);
	    /*@=nullpass@*/
	    if (fix && *fix != ' ')
		appendLineStringBuf(spec->prep, fix);
	    fix = _free(fix);
	}
    }
    
    return 0;
}

static int cmppn(const void *p1, const void *p2)
{
    unsigned int i1 = *(unsigned int *)p1, i2 = *(unsigned int *)p2;
    return (i1 < i2) ? -1 :
		       ((i1 > i2) ? 1 : 0);
}

/**
 * Parse %autopatch line.
 * @param spec		build info
 * @param line		current line from spec file
 * @return		0 on success
 */
static int doAutopatchMacro(Spec spec, const char *line)
{
    int argc = 0, c, rc;
    const char **argv = NULL;

    char *opt_p = NULL;
    int opt_F = rpmExpandNumeric("%{?_default_patch_fuzz}%{?!_default_patch_fuzz:-1}");
    struct poptOption const autopatchOpts[] = {
	{ NULL, 'p', POPT_ARG_STRING, &opt_p, 'p', NULL, NULL },
	{ NULL, 'F', POPT_ARG_INT, &opt_F, 'F', NULL, NULL },
	{ NULL, 0, 0, NULL, 0, NULL, NULL }
    };

    if ((rc = poptParseArgvString(line, &argc, &argv))) {
	rpmError(RPMERR_BADSPEC, _("Error parsing %%autopatch: %s\n"),
		 poptStrerror(rc));
	return RPMERR_BADSPEC;
    }

    poptContext optCon = poptGetContext(NULL, argc, argv, autopatchOpts, 0);
    while ((c = poptGetNextOpt(optCon)) > 0)
	;

    if (c < -1) {
	rpmError(RPMERR_BADSPEC, _("%s: %s: %s\n"), poptStrerror(c),
	       poptBadOption(optCon, POPT_BADOPTION_NOALIAS), line);
	optCon = poptFreeContext(optCon);
	_free(argv);
	return RPMERR_BADSPEC;
    }

    if (opt_p && strcmp(opt_p, "g") && parseNum(opt_p, NULL)) {
	rpmError(RPMERR_BADSPEC,
		_("line %d: Bad arg to %%autopatch -p: %s\n"),
	        spec->lineNum, spec->line);
	return RPMERR_BADSPEC;
    }

    optCon = poptFreeContext(optCon);
    _free(argv);

    unsigned int patch_num = 0;
    for (struct Source *sp = spec->sources; sp != NULL; sp = sp->next) {
	if (sp->flags & RPMBUILD_ISPATCH)
	    patch_num++;
    }

    if (!patch_num)
	return 0;

    unsigned int *patches = calloc(patch_num, sizeof(*patches)), i = 0;
    for (struct Source *sp = spec->sources; sp != NULL; sp = sp->next) {
	if (sp->flags & RPMBUILD_ISPATCH)
	    patches[i++] = sp->num;
    }

    qsort(patches, patch_num, sizeof(*patches), cmppn);

    for (i = 0; i < patch_num; i++) {
	const char *s = doPatch(spec, patches[i], opt_p, NULL, 0, 0, opt_F, NULL, 0);
	if (s == NULL) {
	    _free(patches);
	    return RPMERR_BADSPEC;
	}
	appendLineStringBuf(spec->prep, s);
    }

    _free(patches);

    return 0;
}

/**
 * Parse %patch line.
 * @param spec		build info
 * @param line		current line from spec file
 * @return		0 on success
 */
static int doPatchMacro(Spec spec, char *line)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies spec->prep, rpmGlobalMacroContext, fileSystem @*/
{
    char *opt_b, *opt_d, *opt_p;
    int opt_P, opt_R, opt_E, opt_F, opt_s;
    char *s;
    char buf[BUFSIZ], *bp;
    int patch_nums[1024];  /* XXX - we can only handle 1024 patches! */
    int patch_index, x;

    memset(patch_nums, 0, sizeof(patch_nums));
    opt_P = opt_R = opt_E = opt_s = 0;
    opt_F = rpmExpandNumeric("%{?_default_patch_fuzz}%{?!_default_patch_fuzz:-1}");
    opt_p = opt_b = opt_d = NULL;
    patch_index = 0;

    if (! strchr(" \t\n", line[6])) {
	/* %patchN */
	snprintf(buf, sizeof(buf), "%%patch -P %s", line + 6);
    } else {
	strcpy(buf, line);
    }
    
    /*@-internalglobs@*/	/* FIX: strtok has state */
    for (bp = buf; (s = strtok(bp, " \t\n")) != NULL;) {
	if (bp) {	/* remove 1st token (%patch) */
	    bp = NULL;
	    continue;
	}
	if (!strcmp(s, "-P")) {
	    opt_P = 1;
	} else if (!strcmp(s, "-R")) {
	    opt_R = 1;
	} else if (!strcmp(s, "-E")) {
	    opt_E = 1;
	} else if (!strcmp(s, "-s")) {
	    opt_s = 1;
	} else if (!strcmp(s, "-b")) {
	    /* orig suffix */
	    opt_b = strtok(NULL, " \t\n");
	    if (! opt_b) {
		rpmError(RPMERR_BADSPEC,
			_("line %d: Need arg to %%patch -b: %s\n"),
			spec->lineNum, spec->line);
		return RPMERR_BADSPEC;
	    }
	} else if (!strcmp(s, "-d")) {
	    /* dest dir */
	    opt_d = strtok(NULL, " \t\n");
	    if (! opt_d) {
		rpmError(RPMERR_BADSPEC,
			_("line %d: Need arg to %%patch -d: %s\n"),
			spec->lineNum, spec->line);
		return RPMERR_BADSPEC;
	    }
	} else if (!strcmp(s, "-z")) {
	    /* orig suffix */
	    opt_b = strtok(NULL, " \t\n");
	    if (! opt_b) {
		rpmError(RPMERR_BADSPEC,
			_("line %d: Need arg to %%patch -z: %s\n"),
			spec->lineNum, spec->line);
		return RPMERR_BADSPEC;
	    }
	} else if (!strncmp(s, "-p", sizeof("-p")-1)) {
	    /* unfortunately, we must support -pX */
	    if (! strchr(" \t\n", s[2])) {
		opt_p = s + 2;
	    } else {
		opt_p = strtok(NULL, " \t\n");
		if (!opt_p) {
		    rpmError(RPMERR_BADSPEC,
			     _("line %d: Need arg to %%patch -p: %s\n"),
			     spec->lineNum, spec->line);
		    return RPMERR_BADSPEC;
		}
	    }
	    if (strcmp(opt_p, "g") && parseNum(opt_p, NULL)) {
		rpmError(RPMERR_BADSPEC,
			_("line %d: Bad arg to %%patch -p: %s\n"),
			spec->lineNum, spec->line);
		return RPMERR_BADSPEC;
	    }
	} else if (!strncmp(s, "-F", sizeof("-F")-1)) {
	    /* unfortunately, we must support -FX */
	    if (! strchr(" \t\n", s[2])) {
		s = s + 2;
	    } else {
		s = strtok(NULL, " \t\n");
		if (s == NULL) {
		    rpmError(RPMERR_BADSPEC,
			     _("line %d: Need arg to %%patch -F: %s\n"),
			     spec->lineNum, spec->line);
		    return RPMERR_BADSPEC;
		}
	    }
	    if (parseNum(s, &opt_F)) {
		rpmError(RPMERR_BADSPEC,
			_("line %d: Bad arg to %%patch -F: %s\n"),
			spec->lineNum, spec->line);
		return RPMERR_BADSPEC;
	    }
	} else {
	    /* Must be a patch num */
	    if (patch_index == 1024) {
		rpmError(RPMERR_BADSPEC, _("Too many patches!\n"));
		return RPMERR_BADSPEC;
	    }
	    if (parseNum(s, &(patch_nums[patch_index]))) {
		rpmError(RPMERR_BADSPEC, _("line %d: Bad arg to %%patch: %s\n"),
			 spec->lineNum, spec->line);
		return RPMERR_BADSPEC;
	    }
	    patch_index++;
	}
    }
    /*@=internalglobs@*/

    /* All args processed */

    if (! opt_P) {
	s = doPatch(spec, 0, opt_p, opt_b, opt_R, opt_E, opt_F, opt_d, opt_s);
	if (s == NULL)
	    return RPMERR_BADSPEC;
	appendLineStringBuf(spec->prep, s);
    }

    for (x = 0; x < patch_index; x++) {
	s = doPatch(spec, patch_nums[x], opt_p, opt_b, opt_R, opt_E, opt_F, opt_d, opt_s);
	if (s == NULL)
	    return RPMERR_BADSPEC;
	appendLineStringBuf(spec->prep, s);
    }
    
    return 0;
}

int parsePrep(Spec spec)
{
    int nextPart, res, rc;
    StringBuf sb;
    char **lines, **saveLines;

    if (spec->prep != NULL) {
	rpmError(RPMERR_BADSPEC, _("line %d: second %%prep\n"), spec->lineNum);
	return RPMERR_BADSPEC;
    }

    spec->prep = newStringBuf();

    /* There are no options to %prep */
    if ((rc = readLine(spec, STRIP_NOTHING)) == 1) {
	return PART_NONE;
    }
    if (rc)
	return rc;
    
    sb = newStringBuf();
    
    while (! (nextPart = isPart(spec->line))) {
	/* Need to expand the macros inline.  That way we  */
	/* can give good line number information on error. */
	appendStringBuf(sb, spec->line);
	if ((rc = readLine(spec, STRIP_NOTHING)) == 1) {
	    nextPart = PART_NONE;
	    break;
	}
	if (rc)
	    return rc;
    }

    if (!spec->preprocess_mode) {
    saveLines = splitString(getStringBuf(sb), strlen(getStringBuf(sb)), '\n');
    /*@-usereleased@*/
    for (lines = saveLines; *lines; lines++) {
	res = 0;
	if (! strncmp(*lines, "%setup", sizeof("%setup")-1)) {
	    res = doSetupMacro(spec, *lines);
	} else if (! strncmp(*lines, "%patch", sizeof("%patch")-1)) {
	    res = doPatchMacro(spec, *lines);
	} else if (! strncmp(*lines, "%autopatch", sizeof("%autopatch")-1)) {
	    res = doAutopatchMacro(spec, *lines);
	} else {
	    appendLineStringBuf(spec->prep, *lines);
	}
	if (res && !spec->force) {
	    freeSplitString(saveLines);
	    sb = freeStringBuf(sb);
	    return res;
	}
    }
    /*@=usereleased@*/

    freeSplitString(saveLines);
    sb = freeStringBuf(sb);
    }

    return nextPart;
}
