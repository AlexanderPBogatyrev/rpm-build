/*@-branchstate@*/
/** \ingroup rpmrc rpmio
 * \file rpmio/macro.c
 */

#if 0
/*@unused@*/ static int _debug = 0;
#endif

#include "system.h"
#include <stdarg.h>
#if HAVE_SCHED_GETAFFINITY
#include <sched.h>
#endif

#if !defined(isblank)
#define	isblank(_c)	((_c) == ' ' || (_c) == '\t')
#endif
#define	iseol(_c)	((_c) == '\n' || (_c) == '\r')

#define	STREQ(_t, _f, _fn)	((_fn) == (sizeof(_t)-1) && !strncmp((_t), (_f), (_fn)))

#ifdef DEBUG_MACROS
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#define rpmError fprintf
#define RPMERR_BADSPEC stderr
#undef	_
#define	_(x)	x

#define	vmefail()		(exit(1), NULL)
#define	urlPath(_xr, _r)	*(_r) = (_xr)

typedef	FILE * FD_t;
#define Fopen(_path, _fmode)	fopen(_path, "r");
#define	Ferror			ferror
#define Fstrerror(_fd)		strerror(errno)
#define	Fread			fread
#define	Fclose			fclose

#define	fdGetFILE(_fd)		(_fd)

#else

#include "rpmio_internal.h"
#include "rpmmessages.h"
#include "rpmerr.h"

#endif

#include "rpmmacro.h"

#include "debug.h"

/*@access FD_t@*/		/* XXX compared with NULL */
/*@access MacroContext@*/
/*@access MacroEntry@*/

static struct MacroContext_s rpmGlobalMacroContext_s;
/*@-compmempass@*/
MacroContext rpmGlobalMacroContext = &rpmGlobalMacroContext_s;
/*@=compmempass@*/

static struct MacroContext_s rpmCLIMacroContext_s;
/*@-compmempass@*/
MacroContext rpmCLIMacroContext = &rpmCLIMacroContext_s;
/*@=compmempass@*/

/**
 * Macro expansion state.
 */
typedef /*@abstract@*/ struct MacroBuf_s {
/*@shared@*/ const char * s;	/*!< Text to expand. */
/*@shared@*/ char * t;		/*!< Expansion buffer. */
    size_t nb;			/*!< No. bytes remaining in expansion buffer. */
    int depth;			/*!< Current expansion depth. */
    int macro_trace;		/*!< Pre-print macro to expand? */
    int expand_trace;		/*!< Post-print macro expansion? */
/*@shared@*/ /*@null@*/ void * spec;	/*!< (future) %file expansion info?. */
/*@dependent@*/ MacroContext mc;
} * MacroBuf;

#define SAVECHAR(_mb, _c) { *(_mb)->t = (_c), (_mb)->t++, (_mb)->nb--; }

/*@-exportlocal -exportheadervar@*/

#define	MAX_MACRO_DEPTH	24
/*@unchecked@*/
int max_macro_depth = MAX_MACRO_DEPTH;

#ifdef	DEBUG_MACROS
/*@unchecked@*/
int print_macro_trace = 0;
/*@unchecked@*/
int print_expand_trace = 0;
#else
/*@unchecked@*/
int print_macro_trace = 0;
/*@unchecked@*/
int print_expand_trace = 0;
#endif
/*@=exportlocal =exportheadervar@*/

/* forward ref */
static int expandMacro(MacroBuf mb)
	/*@globals rpmGlobalMacroContext,
		print_macro_trace, print_expand_trace,
		fileSystem @*/
	/*@modifies mb, rpmGlobalMacroContext,
		print_macro_trace, print_expand_trace,
		fileSystem @*/;

/**
 * Wrapper to free(3), hides const compilation noise, permit NULL, return NULL.
 * @param p		memory to free
 * @retval		NULL always
 */
/*@unused@*/ static inline /*@null@*/ void *
_free(/*@only@*/ /*@null@*/ const void * p)
	/*@modifies p@*/
{
    if (p != NULL)	free((void *)p);
    return NULL;
}

/* =============================================================== */

/**
 * Compare macro entries by name (qsort/bsearch).
 * @param ap		1st macro entry
 * @param bp		2nd macro entry
 * @return		result of comparison
 */
static inline int
compareMacroName(const void * ap, const void * bp)
	/*@*/
{
    MacroEntry ame = *((MacroEntry *)ap);
    MacroEntry bme = *((MacroEntry *)bp);
    return strcmp(ame->name, bme->name);
}

void
rpmDumpMacroTable(MacroContext mc, FILE * fp)
{
    int nactive = 0;

    if (mc == NULL) mc = rpmGlobalMacroContext;
    if (fp == NULL) fp = stderr;
    
    fprintf(fp, "========================\n");
    if (mc->macroTable != NULL) {
	int i;
	for (i = 0; i < mc->macroTableSize; i++) {
	    MacroEntry me = mc->macroTable[i];
	    fprintf(fp, "%3d%c %s", me->level,
			(me->used > 0 ? '=' : ':'), me->name);
	    if (me->opts && *me->opts)
		    fprintf(fp, "(%s)", me->opts);
	    if (me->body && *me->body)
		    fprintf(fp, "\t%s", me->body);
	    fprintf(fp, "\n");
	    nactive++;
	}
    }
    fprintf(fp, _("======================== active %d empty %d\n"),
		nactive, 0);
}

#include "bsearch.h"

/**
 * Find entry in macro table.
 * @param mc		macro context
 * @param name		macro name
 * @param namelen	no. of byes
 * @return		address of slot in macro table with name (or NULL)
 */
/*@-mustmod@*/ /* LCL: segfault with modifies nothing annotation */
/*@dependent@*/ /*@null@*/ static MacroEntry *
findEntry(MacroContext mc, const char * name, size_t namelen)
	/*@globals rpmGlobalMacroContext @*/
	/*@modifies rpmGlobalMacroContext @*/
{
    MacroEntry key, *ret;
    struct MacroEntry_s keybuf;
    char namebuf[1024];

    if (mc == NULL) mc = rpmGlobalMacroContext;
    if (mc->macroTable == NULL || mc->macroTableSize == 0)
	return NULL;

    if (namelen > 0) {
	strncpy(namebuf, name, namelen);
	namebuf[namelen] = '\0';
	name = namebuf;
    }
    
    key = &keybuf;
    memset(key, 0, sizeof(*key));
    /*@-temptrans -assignexpose@*/
    key->name = (char *)name;
    /*@=temptrans =assignexpose@*/
    ret = BSEARCH(&key, mc->macroTable, mc->macroTableSize,
		sizeof(*(mc->macroTable)), compareMacroName);
    /* XXX TODO: find 1st empty slot and return that */
    return ret;
}
/*@=mustmod@*/

/* =============================================================== */

/**
 * fgets(3) analogue that reads \ continuations. Last newline always trimmed.
 * @param buf		input buffer
 * @param size		inbut buffer size (bytes)
 * @param fd		file handle
 * @return		number of lines read, or 0 on end-of-file
 */
static int
rdcl(char * buf, size_t size, FD_t fd)
{
    size_t nb = 0;
    int pc = 0, bc = 0;
    int nlines = 0;
    char *p = buf;
    char *q = buf;
    FILE *f = fdGetFILE(fd);

    if (f != NULL)
    do {
	*q = '\0';			/* terminate */
	if (fgets(q, size, f) == NULL)	/* read next line. */
	    break;
	nlines++;
	nb = strlen(q);
	for (q += nb; nb > 0 && iseol(q[-1]); q--)
	    nb--;
	if (*q == 0)
	    break;			/* no newline found, EOF */
	if (p == buf) {
            while (*p && xisblank(*p))
                p++;
            if (*p != '%') {		/* only parse actual macro */
                *q = '\0';		/* trim trailing \r, \n */
                break;
            }
        }
	for (; p < q; p++) {
	    switch (*p) {
		case '\\':
		    switch (*(p+1)) {
			case '\0': break;
			default: p++; break;
		    }
		    break;
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
	if ((nb == 0 || q[-1] != '\\') && !bc && !pc) {
	    *q = '\0';		/* trim trailing \r, \n */
	    break;
	}
	q++; nb++;			/* copy newline too */
	size -= nb;
	if (q[-1] == '\r')			/* XXX avoid \r madness */
	    q[-1] = '\n';
    } while (size > 0);
    return nlines;
}

/**
 * Return text between pl and matching pr characters.
 * @param p		start of text
 * @param pl		left char, i.e. '[', '(', '{', etc.
 * @param pr		right char, i.e. ']', ')', '}', etc.
 * @return		address of last char before pr (or NULL)
 */
static const char *
matchchar(const char * p, char pl, char pr)
	/*@*/
{
    int lvl = 0;
    char c;

    while ((c = *p++) != '\0') {
	if (c == '\\') {		/* Ignore escaped chars */
	    p++;
	    continue;
	}
	if (c == pr) {
	    if (--lvl <= 0)	return --p;
	} else if (c == pl)
	    lvl++;
    }
    return (const char *)NULL;
}

/**
 * Pre-print macro expression to be expanded.
 * @param mb		macro expansion state
 * @param s		current expansion string
 * @param se		end of string
 */
static void
printMacro(MacroBuf mb, const char * s, const char * se)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    const char *senl;
    const char *ellipsis;
    int choplen;

    if (s >= se) {	/* XXX just in case */
	fprintf(stderr, _("%3d>%*s(empty)"), mb->depth,
		(2 * mb->depth + 1), "");
	return;
    }

    if (s[-1] == '{')
	s--;

    /* Print only to first end-of-line (or end-of-string). */
    for (senl = se; *senl && !iseol(*senl); senl++)
	{};

    /* Limit trailing non-trace output */
    choplen = 61 - (2 * mb->depth);
    if ((senl - s) > choplen) {
	senl = s + choplen;
	ellipsis = "...";
    } else
	ellipsis = "";

    /* Substitute caret at end-of-macro position */
    fprintf(stderr, "%3d>%*s%%%.*s^", mb->depth,
	(2 * mb->depth + 1), "", (int)(se - s), s);
    if (se[1] != '\0' && (senl - (se+1)) > 0)
	fprintf(stderr, "%-.*s%s", (int)(senl - (se+1)), se+1, ellipsis);
    fprintf(stderr, "\n");
}

/**
 * Post-print expanded macro expression.
 * @param mb		macro expansion state
 * @param t		current expansion string result
 * @param te		end of string
 */
static void
printExpansion(MacroBuf mb, const char * t, const char * te)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    const char *ellipsis;
    int choplen;

    if (!(te > t)) {
	fprintf(stderr, _("%3d<%*s(empty)\n"), mb->depth, (2 * mb->depth + 1), "");
	return;
    }

    /* Shorten output which contains newlines */
    while (te > t && iseol(te[-1]))
	te--;
    ellipsis = "";
    if (mb->depth > 0) {
	const char *tenl;

	/* Skip to last line of expansion */
	while ((tenl = strchr(t, '\n')) && tenl < te)
	    t = ++tenl;

	/* Limit expand output */
	choplen = 61 - (2 * mb->depth);
	if ((te - t) > choplen) {
	    te = t + choplen;
	    ellipsis = "...";
	}
    }

    fprintf(stderr, "%3d<%*s", mb->depth, (2 * mb->depth + 1), "");
    if (te > t)
	fprintf(stderr, "%.*s%s", (int)(te - t), t, ellipsis);
    fprintf(stderr, "\n");
}

#define	SKIPBLANK(_s, _c)	\
	while (((_c) = *(_s)) && isblank(_c)) \
		(_s)++;

#define	SKIPNONBLANK(_s, _c)	\
	while (((_c) = *(_s)) && !(isblank(_c) || iseol(_c))) \
		(_s)++;

#define	COPYNAME(_ne, _s, _c)	\
    {	SKIPBLANK(_s,_c);	\
	while(((_c) = *(_s)) && (xisalnum(_c) || (_c) == '_')) \
		*(_ne)++ = *(_s)++; \
	*(_ne) = '\0';		\
    }

#define	COPYOPTS(_oe, _s, _c)	\
    {	while(((_c) = *(_s)) && (_c) != ')') \
		*(_oe)++ = *(_s)++; \
	*(_oe) = '\0';		\
    }

/**
 * Save source and expand field into target.
 * @param mb		macro expansion state
 * @param f		field
 * @param flen		no. bytes in field
 * @return		result of expansion
 */
static int
expandT(MacroBuf mb, const char * f, size_t flen)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies mb, rpmGlobalMacroContext, fileSystem @*/
{
    char *sbuf;
    const char *s = mb->s;
    int rc;

    sbuf = alloca(flen + 1);
    memset(sbuf, 0, (flen + 1));

    strncat(sbuf, f, flen);
    mb->s = sbuf;
    rc = expandMacro(mb);
    mb->s = s;
    return rc;
}

#if 0
/**
 * Save target and expand sbuf into target.
 * @param mb		macro expansion state
 * @param tbuf		target buffer
 * @param tbuflen	no. bytes in target buffer
 * @return		result of expansion
 */
static int
expandS(MacroBuf mb, char * tbuf, size_t tbuflen)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies mb, *tbuf, rpmGlobalMacroContext, fileSystem @*/
{
    const char *t = mb->t;
    size_t nb = mb->nb;
    int rc;

    mb->t = tbuf;
    mb->nb = tbuflen;
    rc = expandMacro(mb);
    mb->t = t;
    mb->nb = nb;
    return rc;
}
#endif

/**
 * Save source/target and expand macro in u.
 * @param mb		macro expansion state
 * @param u		input macro, output expansion
 * @param ulen		no. bytes in u buffer
 * @return		result of expansion
 */
static int
expandU(MacroBuf mb, char * u, size_t ulen)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies mb, *u, rpmGlobalMacroContext, fileSystem @*/
{
    const char *s = mb->s;
    char *t = mb->t;
    size_t nb = mb->nb;
    char *tbuf;
    int rc;

    tbuf = alloca(ulen + 1);
    memset(tbuf, 0, (ulen + 1));

    /*@-temptrans -assignexpose@*/
    mb->s = u;
    /*@=temptrans =assignexpose@*/
    mb->t = tbuf;
    mb->nb = ulen;
    rc = expandMacro(mb);

    tbuf[ulen] = '\0';	/* XXX just in case */
    if (ulen > mb->nb)
	strncpy(u, tbuf, (ulen - mb->nb + 1));

    mb->s = s;
    mb->t = t;
    mb->nb = nb;

    return rc;
}

/**
 * Skip to the end of line also eating EOL itself.
 */
static const char *
doDnl(MacroBuf mb, const char * se)
{
    const char *s = se;
    while (*s && !iseol(*s))
	s++;
    return (*s != '\0') ? s + 1 : s;
}

/**
 * Expand output of shell command into target buffer.
 * @param mb		macro expansion state
 * @param cmd		shell command
 * @param clen		no. bytes in shell command
 * @return		result of expansion
 */
static int
doShellEscape(MacroBuf mb, const char * cmd, size_t clen)
	/*@globals rpmGlobalMacroContext,
		fileSystem @*/
	/*@modifies mb, rpmGlobalMacroContext,
		fileSystem @*/
{
    char pcmd[BUFSIZ];
    FILE *shf;
    int rc;
    int c;

    strncpy(pcmd, cmd, clen);
    pcmd[clen] = '\0';
    rc = expandU(mb, pcmd, sizeof(pcmd));
    if (rc)
	return rc;

    if ((shf = popen(pcmd, "r")) == NULL)
	return 1;
    const char *start = mb->t;
    while(mb->nb > 0 && (c = fgetc(shf)) != EOF)
	SAVECHAR(mb, c);
    (void) pclose(shf);

    /* XXX delete trailing \r \n */
    while (mb->t > start && iseol(mb->t[-1])) {
	*(--mb->t) = '\0';
	mb->nb++;
    }
    return 0;
}

static unsigned int getncpus(void)
{
    unsigned int ncpus = 0;
#if HAVE_SCHED_GETAFFINITY
    cpu_set_t set;
    if (sched_getaffinity (0, sizeof(set), &set) == 0)
	ncpus = CPU_COUNT(&set);
#endif
    /* Fallback to sysconf() if the above isn't supported or didn't work */
    if (ncpus < 1)
	ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    /* If all else fails, there's always the one we're running on... */
    if (ncpus < 1)
	ncpus = 1;
    return ncpus;
}

/**
 * Parse (and execute) new macro definition.
 * @param mb		macro expansion state
 * @param se		macro definition to parse
 * @param level		macro recursion level
 * @param expandbody	should body be expanded?
 * @return		address to continue parsing
 */
/*@dependent@*/ static const char *
doDefine(MacroBuf mb, const char * se, int level, int expandbody)
	/*@globals rpmGlobalMacroContext @*/
	/*@modifies mb, rpmGlobalMacroContext @*/
{
    const char *s = se;
    char buf[BUFSIZ * 2], *n = buf, *ne = n;
    char *o = NULL, *oe;
    char *b, *be;
    int c;
    int oc = ')';

    /* Copy name */
/*@-globs@*/
    COPYNAME(ne, s, c);
/*@=globs@*/

    /* Copy opts (if present) */
    oe = ne + 1;
    if (*s == '(') {
	s++;	/* skip ( */
	o = oe;
	COPYOPTS(oe, s, oc);
	s++;	/* skip ) */
    }

    /* Copy body, skipping over escaped newlines */
    b = be = oe + 1;
/*@-globs@*/
    SKIPBLANK(s, c);
/*@=globs@*/
    if (c == '{') {	/* XXX permit silent {...} grouping */
	if ((se = matchchar(s, c, '}')) == NULL) {
	    rpmError(RPMERR_BADSPEC,
		_("Macro %%%s has unterminated body\n"), n);
	    se = s;	/* XXX W2DO? */
	    /*@-retalias@*/ return se; /*@=retalias@*/
	}
	s++;	/* XXX skip { */
	strncpy(b, s, (se - s));
	b[se - s] = '\0';
	be += strlen(b);
	se++;	/* XXX skip } */
	s = se;	/* move scan forward */
    } else {	/* otherwise free-field */
	int bc = 0, pc = 0;
	while (*s && (bc || pc || !iseol(*s))) {
	    switch (*s) {
		case '\\':
		    switch (*(s+1)) {
			case '\0': break;
			default: s++; break;
		    }
		    break;
		case '%':
		    switch (*(s+1)) {
			case '{': *be++ = *s++; bc++; break;
			case '(': *be++ = *s++; pc++; break;
			case '%': *be++ = *s++; break;
		    }
		    break;
		case '{': if (bc > 0) bc++; break;
		case '}': if (bc > 0) bc--; break;
		case '(': if (pc > 0) pc++; break;
		case ')': if (pc > 0) pc--; break;
	    }
	    *be++ = *s++;
	}
	*be = '\0';

	if (bc || pc) {
	    rpmError(RPMERR_BADSPEC, _("Macro %%%s has unterminated body\n"), n);
	    se = s;     /* XXX W2DO? */
	    return se;
	}

	/* Trim trailing blanks/newlines */
/*@-globs@*/
	while (--be >= b && (c = *be) && (isblank(c) || iseol(c)))
	    {};
/*@=globs@*/
	*(++be) = '\0';	/* one too far */
    }

    /* Move scan over body */
    while (iseol(*s))
	s++;
    se = s;

    /* Names must start with alphabetic or _ and be at least 3 chars */
    if (!((c = *n) && (xisalpha(c) || c == '_') && (ne - n) > 2)) {
	rpmError(RPMERR_BADSPEC,
		_("Macro %%%s has illegal name (%%define)\n"), n);
	/*@-retalias@*/ return se; /*@=retalias@*/
    }

    /* Options must be terminated with ')' */
    if (o && oc != ')') {
	rpmError(RPMERR_BADSPEC, _("Macro %%%s has unterminated opts\n"), n);
	/*@-retalias@*/ return se; /*@=retalias@*/
    }

    if ((be - b) < 1) {
	rpmError(RPMERR_BADSPEC, _("Macro %%%s has empty body\n"), n);
	/*@-retalias@*/ return se; /*@=retalias@*/
    }

/*@-modfilesys@*/
    if (expandbody && expandU(mb, b, (&buf[sizeof(buf)] - b))) {
	rpmError(RPMERR_BADSPEC, _("Macro %%%s failed to expand\n"), n);
	/*@-retalias@*/ return se; /*@=retalias@*/
    }
/*@=modfilesys@*/

    addMacro(mb->mc, n, o, b, (level - 1));

    /*@-retalias@*/ return se; /*@=retalias@*/
}

/**
 * Parse (and execute) macro undefinition.
 * @param mc		macro context
 * @param se		macro name to undefine
 * @return		address to continue parsing
 */
/*@dependent@*/ static const char *
doUndefine(MacroContext mc, const char * se)
	/*@globals rpmGlobalMacroContext @*/
	/*@modifies mc, rpmGlobalMacroContext @*/
{
    const char *s = se;
    char buf[BUFSIZ], *n = buf, *ne = n;
    int c;

/*@-globs@*/
    COPYNAME(ne, s, c);
/*@=globs@*/

    /* Move scan over body */
    while (iseol(*s))
	s++;
    se = s;

    /* Names must start with alphabetic or _ and be at least 3 chars */
    if (!((c = *n) && (xisalpha(c) || c == '_') && (ne - n) > 2)) {
	rpmError(RPMERR_BADSPEC,
		_("Macro %%%s has illegal name (%%undefine)\n"), n);
	/*@-retalias@*/ return se; /*@=retalias@*/
    }

    delMacro(mc, n);

    /*@-retalias@*/ return se; /*@=retalias@*/
}

#ifdef	DYING
static void
dumpME(const char * msg, MacroEntry me)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    if (msg)
	fprintf(stderr, "%s", msg);
    fprintf(stderr, "\tme %p", me);
    if (me)
	fprintf(stderr,"\tname %p(%s) prev %p",
		me->name, me->name, me->prev);
    fprintf(stderr, "\n");
}
#endif

/**
 * Push new macro definition onto macro entry stack.
 * @param mep		address of macro entry slot
 * @param n		macro name
 * @param o		macro parameters (NULL if none)
 * @param b		macro body (NULL becomes "")
 * @param level		macro recursion level
 */
static void
pushMacro(/*@out@*/ MacroEntry * mep,
		const char * n, /*@null@*/ const char * o,
		/*@null@*/ const char * b, int level)
	/*@modifies *mep @*/
{
    /*@-usedef@*/
    MacroEntry prev = (mep && *mep ? *mep : NULL);
    /*@=usedef@*/
    MacroEntry me = (MacroEntry) xmalloc(sizeof(*me));

    /*@-assignexpose@*/
    me->prev = prev;
    /*@=assignexpose@*/
    me->name = (prev ? prev->name : xstrdup(n));
    me->opts = (o ? xstrdup(o) : NULL);
    me->body = xstrdup(b ? b : "");
    me->used = 0;
    me->level = level;
    if (mep)
	*mep = me;
    else
	me = _free(me);
}

/**
 * Pop macro definition from macro entry stack.
 * @param mep		address of macro entry slot
 */
static void
popMacro(MacroEntry * mep)
	/*@modifies *mep @*/
{
	MacroEntry me = (*mep ? *mep : NULL);

	if (me) {
		/* XXX cast to workaround const */
		/*@-onlytrans@*/
		if ((*mep = me->prev) == NULL)
			me->name = _free(me->name);
		me->opts = _free(me->opts);
		me->body = _free(me->body);
		me = _free(me);
		/*@=onlytrans@*/
	}
}

/**
 * Free parsed arguments for parameterized macro.
 * @param mb		macro expansion state
 */
static void
freeArgs(MacroBuf mb)
	/*@modifies mb @*/
{
    MacroContext mc = mb->mc;
    int ndeleted = 0;
    int i;

    if (mc == NULL || mc->macroTable == NULL)
	return;

    /* Delete dynamic macro definitions */
    for (i = 0; i < mc->macroTableSize; i++) {
	MacroEntry *mep, me;
	int skiptest = 0;
	mep = &mc->macroTable[i];
	me = *mep;

	if (me->level < mb->depth)
	    goto skip;
	if (strlen(me->name) == 1 && strchr("#*0", *me->name)) {
	    if (*me->name == '*' && me->used > 0)
		skiptest = 1; /* XXX skip test for %# %* %0 */
	} else if (!skiptest && me->used <= 0) {
#if NOTYET
	    rpmError(RPMERR_BADSPEC,
			_("Macro %%%s (%s) was not used below level %d\n"),
			me->name, me->body, me->level);
#endif
	}
	popMacro(mep);
    skip:
	if (*mep == NULL)
	    ndeleted++;
	else if (ndeleted) {
	    mep[-ndeleted] = *mep;
	    *mep = NULL;
	}
    }
    mc->macroTableSize -= ndeleted;
}

/**
 * Parse arguments (to next new line) for parameterized macro.
 * @todo Use popt rather than getopt to parse args.
 * @param mb		macro expansion state
 * @param me		macro entry slot
 * @param se		arguments to parse
 * @param lastc		stop parsing at lastc
 * @return		address to continue parsing
 */
/*@dependent@*/ static const char *
grabArgs(MacroBuf mb, const MacroEntry me, const char * se, char lastc)
	/*@globals rpmGlobalMacroContext @*/
	/*@modifies mb, rpmGlobalMacroContext @*/
{
    char buf[BUFSIZ], *b, *be;
    char aname[16];
    const char *opts, *o;
    int argc = 0;
    const char **argv;
    int c;

    /* Copy macro name as argv[0], save beginning of args.  */
    buf[0] = '\0';
    b = be = stpcpy(buf, me->name);

    addMacro(mb->mc, "0", NULL, buf, mb->depth);

    argc = 1;	/* XXX count argv[0] */

    /* Copy args into buf until lastc */
    *be++ = ' ';
    while ((c = *se++) != '\0' && c != lastc) {
/*@-globs@*/
	if (!isblank(c)) {
	    *be++ = c;
	    continue;
	}
/*@=globs@*/
	/* c is blank */
	if (be[-1] == ' ')
	    continue;
	/* a word has ended */
	*be++ = ' ';
	argc++;
    }
    if (c == '\0' || c == '\n') se--;	/* one too far */
    if (be[-1] != ' ')
	argc++, be++;		/* last word has not trailing ' ' */
    be[-1] = '\0';
    if (*b == ' ') b++;		/* skip the leading ' ' */

/*
 * The macro %* analoguous to the shell's $* means "Pass all non-macro
 * parameters." Consequently, there needs to be a macro that means "Pass all
 * (including macro parameters) options". This is useful for verifying
 * parameters during expansion and yet transparently passing all parameters
 * through for higher level processing (e.g. %description and/or %setup).
 * This is the (potential) justification for %{**} ...
 */
    /* Add unexpanded args as macro */
    addMacro(mb->mc, "**", NULL, b, mb->depth);

#ifdef NOTYET
    /* XXX if macros can be passed as args ... */
    expandU(mb, buf, sizeof(buf));
#endif

    /* Build argv array */
    argv = (const char **) alloca((argc + 1) * sizeof(*argv));
    be[-1] = ' '; /* assert((be - 1) == (b + strlen(b) == buf + strlen(buf))) */
    be[0] = '\0';
    b = buf;
    for (c = 0; c < argc; c++) {
	argv[c] = b;
	b = strchr(b, ' ');
	*b++ = '\0';
    }
    /* assert(b == be);  */
    argv[argc] = NULL;

    /* Citation from glibc/posix/getopt.c:
     *    Index in ARGV of the next element to be scanned.
     *    This is used for communication to and from the caller
     *    and for communication between successive calls to `getopt'.
     *
     *    On entry to `getopt', zero means this is the first call; initialize.
     *
     *    When `getopt' returns -1, this is the index of the first of the
     *    non-option elements that the caller should itself scan.
     *
     *    Otherwise, `optind' communicates from one call to the next
     *    how much of ARGV has been scanned so far.
     */
    /* 1003.2 says this must be 1 before any call.  */

    /*
     * glibc getopt(3) (re)initializes it's internal structure on first call
     * or if optind == 0.
     */

#ifdef __GLIBC__
    optind = 0;
#else
    optind = 1;
#endif

    opts = me->opts;

    /* Define option macros. */
    while((c = getopt(argc, (char **)argv, opts)) != -1) {
	if (c == '?' || (o = strchr(opts, c)) == NULL) {
	    rpmError(RPMERR_BADSPEC, _("Unknown option %c in %s(%s)\n"),
			(char)c, me->name, opts);
	    /*@-retalias@*/ return se; /*@=retalias@*/
	}
	*be++ = '-';
	*be++ = c;
	/*@-usedef@*/
	if (o[1] == ':') {
	/*@=usedef@*/
	    *be++ = ' ';
	    be = stpcpy(be, optarg);
	}
	*be++ = '\0';
	aname[0] = '-'; aname[1] = c; aname[2] = '\0';
	addMacro(mb->mc, aname, NULL, b, mb->depth);
	if (o[1] == ':') {
	    aname[0] = '-'; aname[1] = c; aname[2] = '*'; aname[3] = '\0';
	    addMacro(mb->mc, aname, NULL, optarg, mb->depth);
	}
	be = b; /* reuse the space */
    }

    /* Add arg count as macro. */
    sprintf(aname, "%d", (argc - optind));
    addMacro(mb->mc, "#", NULL, aname, mb->depth);

    /* Add macro for each arg. Concatenate args for %*. */
    if (be) {
	*be = '\0';
	for (c = optind; c < argc; c++) {
	    sprintf(aname, "%d", (c - optind + 1));
	    addMacro(mb->mc, aname, NULL, argv[c], mb->depth);
	    if (be != b) *be++ = ' '; /* Add space between args */
	    be = stpcpy(be, argv[c]);
	}
    }

    /* Add unexpanded args as macro. */
    addMacro(mb->mc, "*", NULL, b, mb->depth);

    /*@-retalias@*/ return se; /*@=retalias@*/
}

/**
 * Perform macro message output
 * @param mb		macro expansion state
 * @param waserror	use rpmError()?
 * @param msg		message to ouput
 * @param msglen	no. of bytes in message
 */
static void
doOutput(MacroBuf mb, int waserror, const char * msg, size_t msglen)
	/*@globals rpmGlobalMacroContext,
		fileSystem @*/
	/*@modifies mb, rpmGlobalMacroContext,
		fileSystem @*/
{
    char buf[BUFSIZ];

    strncpy(buf, msg, msglen);
    buf[msglen] = '\0';
    (void) expandU(mb, buf, sizeof(buf));
    if (waserror)
	rpmError(RPMERR_BADSPEC, "%s\n", buf);
    else
	fprintf(stderr, "%s", buf);
}

/**
 * Execute macro primitives.
 * @param mb		macro expansion state
 * @param negate	should logic be inverted?
 * @param f		beginning of field f
 * @param fn		length of field f
 * @param g		beginning of field g
 * @param gn		length of field g
 */
static void
doFoo(MacroBuf mb, int negate, const char * f, size_t fn,
		const char * g, size_t gn)
	/*@globals rpmGlobalMacroContext,
		fileSystem, internalState @*/
	/*@modifies mb, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    char buf[BUFSIZ], *b = NULL, *be;
    int c;

    buf[0] = '\0';
    if (g) {
	strncpy(buf, g, gn);
	buf[gn] = '\0';
	(void) expandU(mb, buf, sizeof(buf));
    }
    if (STREQ("basename", f, fn)) {
	if ((b = strrchr(buf, '/')) == NULL)
	    b = buf;
	else
	    ++b;
#if NOTYET
    /* XXX watchout for conflict with %dir */
    } else if (STREQ("dirname", f, fn)) {
	if ((b = strrchr(buf, '/')) != NULL)
	    *b = '\0';
	b = buf;
#endif
    } else if (STREQ("shrink", f, fn)) {
	/*
	 * shrink body by removing all leading and trailing whitespaces and
	 * reducing intermediate whitespaces to a single space character.
	 */
	size_t i = 0, j = 0;
	size_t buflen = strlen(buf);
	int was_space = 0;
	while (i < buflen) {
	    if (xisspace(buf[i])) {
		was_space = 1;
		i++;
		continue;
	    } else if (was_space) {
		was_space = 0;
		if (j > 0) /* remove leading blanks at all */
		    buf[j++] = ' ';
	    }
	    buf[j++] = buf[i++];
	}
	buf[j] = '\0';
	b = buf;
    } else if (STREQ("suffix", f, fn)) {
	if ((b = strrchr(buf, '.')) != NULL)
	    b++;
    } else if (STREQ("expand", f, fn)) {
	b = buf;
    } else if (STREQ("verbose", f, fn)) {
	if (negate)
	    b = (rpmIsVerbose() ? NULL : buf);
	else
	    b = (rpmIsVerbose() ? buf : NULL);
    } else if (STREQ("url2path", f, fn) || STREQ("u2p", f, fn)) {
	(void)urlPath(buf, (const char **)&b);
	if (*b == '\0') b = "/";
    } else if (STREQ("uncompress", f, fn)) {
	rpmCompressedMagic compressed = COMPRESSED_OTHER;
/*@-globs@*/
	for (b = buf; (c = *b) && isblank(c);)
	    b++;
	for (be = b; (c = *be) && !isblank(c);)
	    be++;
/*@=globs@*/
	*be++ = '\0';
#ifndef	DEBUG_MACROS
	(void) isCompressed(b, &compressed);
#endif
	switch(compressed) {
	default:
	case COMPRESSED_NOT:
	    sprintf(be, "%%__cat '%s'", b);
	    break;
	case COMPRESSED_OTHER:
	    sprintf(be, "%%__gzip -dc '%s'", b);
	    break;
	case COMPRESSED_BZIP2:
	    sprintf(be, "%%__bzip2 -dc '%s'", b);
	    break;
	case COMPRESSED_ZIP:
	    sprintf(be, "%%__unzip -qq -p '%s'", b);
	    break;
	case COMPRESSED_LZMA:
	    sprintf(be, "%%__lzma -dc '%s'", b);
	    break;
	case COMPRESSED_XZ:
	    sprintf(be, "%%__xz -dc '%s'", b);
	    break;
	}
	b = be;
    } else if (STREQ("getncpus", f, fn)) {
	sprintf(buf, "%u", getncpus());
	b = buf;
    } else if (STREQ("S", f, fn)) {
	for (b = buf; (c = *b) && xisdigit(c);)
	    b++;
	if (!c) {	/* digit index */
	    b++;
	    sprintf(b, "%%SOURCE%s", buf);
	} else
	    b = buf;
    } else if (STREQ("P", f, fn)) {
	for (b = buf; (c = *b) && xisdigit(c);)
	    b++;
	if (!c) {	/* digit index */
	    b++;
	    sprintf(b, "%%PATCH%s", buf);
	} else
			b = buf;
    } else if (STREQ("getenv", f, fn)) {
	b = getenv(buf);
    } else if (STREQ("_tmpdir", f, fn)) {
	b = getenv("TMPDIR");
	if (!b || *b != '/' || access(b, X_OK | W_OK))
	    b = "/tmp";
    } else if (STREQ("homedir", f, fn)) {
	struct passwd *pw = 0;

	if (buf[0])
	    pw = getpwnam (buf);
	else
	{
	    static struct passwd pw_buf, *result;
	    static char buffer[BUFSIZ];
	    static uid_t uid = -1;

	    uid_t new_uid = geteuid();
	    if (result && (uid == new_uid))
		pw = result;
	    else {
	    	uid = new_uid;
		result = 0;
		if ( !getpwuid_r (uid, &pw_buf, buffer, sizeof buffer, &result))
		    pw = result;
	    }
	}

	buf[0] = '\0';
	if (pw && pw->pw_dir) {
	    strncat (buf, pw->pw_dir, sizeof(buf) - 1);
	    b = buf;
	}
    }

    if (b) {
	(void) expandT(mb, b, strlen(b));
    }
}

static rpmBuiltinMacroLookup current_rpmBuiltinMacroLookup;
static int current_rpmBuiltinMacroLookupFailedOK;

rpmBuiltinMacroLookup
rpmSetBuiltinMacroLookup(rpmBuiltinMacroLookup f)
{
	rpmBuiltinMacroLookup old = current_rpmBuiltinMacroLookup;

	current_rpmBuiltinMacroLookup = f;
	return old;
}

int
rpmSetBuiltinMacroLookupFailedOK(int val)
{
	int old = current_rpmBuiltinMacroLookupFailedOK;

	current_rpmBuiltinMacroLookupFailedOK = val;
	return old;
}

/**
 * The main macro recursion loop.
 * @todo Dynamically reallocate target buffer.
 * @param mb		macro expansion state
 * @return		0 on success, 1 on failure
 */
static int
expandMacro(MacroBuf mb)
	/*@globals rpmGlobalMacroContext,
		print_macro_trace, print_expand_trace,
		fileSystem @*/
	/*@modifies mb, rpmGlobalMacroContext,
		print_macro_trace, print_expand_trace,
		fileSystem @*/
{
    MacroEntry *mep;
    MacroEntry me;
    const char *s = mb->s, *se;
    const char *f, *fe;
    const char *g, *ge;
    size_t fn, gn;
    char *t = mb->t;	/* save expansion pointer for printExpand */
    int c;
    int rc = 0, rc2 = 0;
    int negate;
    char grab;
    int chkexist;

    if (++mb->depth > max_macro_depth) {
	rpmError(RPMERR_BADSPEC,
		_("Recursion depth(%d) greater than max(%d)\n"),
		mb->depth, max_macro_depth);
	mb->depth--;
	mb->expand_trace = 1;
	return 1;
    }

    while (rc == 0 && mb->nb > 0 && (c = *s) != '\0') {
	const char *s_orig = s;
	size_t nb_orig = mb->nb;
	s++;
	/* Copy text until next macro */
	switch(c) {
	case '%':
		if (*s != '%')
			/*@switchbreak@*/ break;
		s++;	/* skip first % in %% */
		/*@fallthrough@*/
	default:
		SAVECHAR(mb, c);
		continue;
		/*@notreached@*/
	}

	/* Expand next macro */
	f = fe = NULL;
	g = ge = NULL;
	if (mb->depth > 1)	/* XXX full expansion for outermost level */
		t = mb->t;	/* save expansion pointer for printExpand */
	negate = 0;
	grab = '\0';
	chkexist = 0;
	switch ((c = *s)) {
	default:		/* %name substitution */
		while (strchr("!?", *s) != NULL) {
			switch(*s++) {
			case '!':
				negate = ((negate + 1) % 2);
				/*@switchbreak@*/ break;
			case '?':
				chkexist++;
				/*@switchbreak@*/ break;
			}
		}
		f = se = s;
		if (*se == '-')
			se++;
		while((c = *se) && (xisalnum(c) || c == '_'))
			se++;
		/* Recognize non-alnum macros too */
		switch (*se) {
		case '*':
			se++;
			if (*se == '*') se++;
			/*@innerbreak@*/ break;
		case '#':
			se++;
			/*@innerbreak@*/ break;
		default:
			/*@innerbreak@*/ break;
		}
		fe = se;
		/* For "%name " macros ... */
/*@-globs@*/
		if ((c = *fe) && isblank(c))
			grab = '\n';
/*@=globs@*/
		/*@switchbreak@*/ break;
	case '(':		/* %(...) shell escape */
		if ((se = matchchar(s, c, ')')) == NULL) {
			rpmError(RPMERR_BADSPEC,
				_("Unterminated %c: %s\n"), (char)c, s);
			rc = 1;
			continue;
		}
		if (mb->macro_trace)
			printMacro(mb, s, se+1);

		s++;	/* skip ( */
		rc = doShellEscape(mb, s, (se - s));
		se++;	/* skip ) */

		s = se;
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	case '{':		/* %{...}/%{...:...} substitution */
		if ((se = matchchar(s, c, '}')) == NULL) {
			rpmError(RPMERR_BADSPEC,
				_("Unterminated %c: %s\n"), (char)c, s);
			rc = 1;
			continue;
		}
		f = s+1;/* skip { */
		se++;	/* skip } */
		while (strchr("!?", *f) != NULL) {
			switch(*f++) {
			case '!':
				negate = ((negate + 1) % 2);
				/*@switchbreak@*/ break;
			case '?':
				chkexist++;
				/*@switchbreak@*/ break;
			}
		}
		for (fe = f; (c = *fe) && !strchr(" :}", c);)
			fe++;
		switch (c) {
		case ':':
			g = fe + 1;
			ge = se - 1;
			/*@innerbreak@*/ break;
		case ' ':
			grab = se[-1];
			/*@innerbreak@*/ break;
		default:
			/*@innerbreak@*/ break;
		}
		/*@switchbreak@*/ break;
	case '\0':
		SAVECHAR(mb, '%');
		continue;
		/*@notreached@*/
	}

	/* XXX Everything below expects fe > f */
	fn = (fe - f);
	gn = (ge - g);
	if ((fe - f) <= 0) {
/* XXX Process % in unknown context */
		c = '%';	/* XXX only need to save % */
		SAVECHAR(mb, c);
		rpmError(_wm(118),
			_("Unparseable macro: %.*s\n"), nb_orig, s_orig);
		s = se;
		continue;
	}

	if (mb->macro_trace)
		printMacro(mb, s, se);

	/* Expand builtin macros */
	if (STREQ("global", f, fn)) {
		s = doDefine(mb, se, RMIL_GLOBAL, 1);
		continue;
	}
	if (STREQ("define", f, fn)) {
		s = doDefine(mb, se, mb->depth, 0);
		continue;
	}
	if (STREQ("undefine", f, fn)) {
		s = doUndefine(mb->mc, se);
		continue;
	}
	if (STREQ("dnl", f, fn)) {
		s = doDnl(mb, s);
		continue;
	}

	if (STREQ("echo", f, fn) ||
	    STREQ("warn", f, fn) ||
	    STREQ("error", f, fn)) {
		int waserror = STREQ("error", f, fn) ? RPMERR_BADSPEC : 0;
		if (g < ge)
			doOutput(mb, waserror, g, gn);
		else
			doOutput(mb, waserror, f, fn);
		s = se;
		if ( waserror )
			return waserror;
		continue;
	}

	if (STREQ("trace", f, fn)) {
		/* XXX TODO restore expand_trace/macro_trace to 0 on return */
		mb->expand_trace = mb->macro_trace = (negate ? 0 : mb->depth);
		if (mb->depth == 1) {
			print_macro_trace = mb->macro_trace;
			print_expand_trace = mb->expand_trace;
		}
		s = se;
		continue;
	}

	if (STREQ("dump", f, fn)) {
		rpmDumpMacroTable(mb->mc, NULL);
		while (iseol(*se))
			se++;
		s = se;
		continue;
	}

	/* XXX necessary but clunky */
	if (STREQ("basename", f, fn) ||
	    STREQ("shrink", f, fn) ||
	    STREQ("suffix", f, fn) ||
	    STREQ("expand", f, fn) ||
	    STREQ("verbose", f, fn) ||
	    STREQ("uncompress", f, fn) ||
	    STREQ("url2path", f, fn) ||
	    STREQ("u2p", f, fn) ||
	    STREQ("getenv", f, fn) ||
	    STREQ("_tmpdir", f, fn) ||
	    STREQ("homedir", f, fn) ||
	    STREQ("getncpus", f, fn) ||
	    STREQ("S", f, fn) ||
	    STREQ("P", f, fn) ||
	    STREQ("F", f, fn)) {
		/*@-internalglobs@*/ /* FIX: verbose may be set */
		doFoo(mb, negate, f, fn, g, gn);
		/*@=internalglobs@*/
		s = se;
		continue;
	}

	/* Expand defined macros */
	mep = findEntry(mb->mc, f, fn);
	me = (mep ? *mep : NULL);

	/* XXX Special processing for flags */
	if (*f == '-') {
		if (me)
			me->used++;	/* Mark macro as used */
		if ((me == NULL && !negate) ||	/* Without -f, skip %{-f...} */
		    (me != NULL && negate)) {	/* With -f, skip %{!-f...} */
			s = se;
			continue;
		}

		if (g && g < ge) {		/* Expand X in %{-f:X} */
			rc = expandT(mb, g, gn);
		} else
		if (me && me->body && *me->body) {/* Expand %{-f}/%{-f*} */
			rc = expandT(mb, me->body, strlen(me->body));
		}
		s = se;
		continue;
	}

	/* XXX Special processing for macro existence */
	if (chkexist) {
		if ((me == NULL && !negate) ||	/* Without -f, skip %{?f...} */
		    (me != NULL && negate)) {	/* With -f, skip %{!?f...} */
			s = se;
			continue;
		}
		if (g && g < ge) {		/* Expand X in %{?f:X} */
			rc = expandT(mb, g, gn);
		} else
		if (me && me->body && *me->body) { /* Expand %{?f}/%{?f*} */
			rc = expandT(mb, me->body, strlen(me->body));
		}
		s = se;
		continue;
	}
	
	if (me == NULL) {	/* leave unknown %... as is */
		/* XXX hack to permit non-overloaded %foo to be passed */
		c = '%';	/* XXX only need to save % */
		SAVECHAR(mb, c);
		if (current_rpmBuiltinMacroLookup &&
		    !current_rpmBuiltinMacroLookup(f, fn)) {
			if (current_rpmBuiltinMacroLookupFailedOK || fn <= 2)
				rpmError(_wm(118),
					_("Macro %%%.*s not found\n"), fn, f);
			else {
				rpmError(RPMERR_BADSPEC,
					_("Macro %%%.*s not found\n"), fn, f);
				rc2 = 1;
			}
		}
		continue;
	}

	/* Setup args for "%name " macros with opts */
	if (me && me->opts != NULL) {
		if (grab != '\0') {
			se = grabArgs(mb, me, fe, grab);
		} else {
			addMacro(mb->mc, "**", NULL, "", mb->depth);
			addMacro(mb->mc, "*", NULL, "", mb->depth);
			addMacro(mb->mc, "#", NULL, "0", mb->depth);
			addMacro(mb->mc, "0", NULL, me->name, mb->depth);
		}
	}

	/* Recursively expand body of macro */
	if (me->body && *me->body) {
		/*@-onlytrans@*/
		mb->s = me->body;
		/*@=onlytrans@*/
		rc = expandMacro(mb);
		if (rc == 0)
			me->used++;	/* Mark macro as used */
	}

	/* Free args for "%name " macros with opts */
	if (me->opts != NULL)
		freeArgs(mb);

	s = se;
    }

    *mb->t = '\0';
    mb->s = s;
    mb->depth--;
    if (rc > 0 || mb->expand_trace)
	printExpansion(mb, t, mb->t);
    return rc ? rc : rc2;
}

/* =============================================================== */

int
expandMacros(void * spec, MacroContext mc, char * sbuf, size_t slen)
{
    MacroBuf mb = alloca(sizeof(*mb));
    char *tbuf;
    int rc;

    if (sbuf == NULL || slen == 0)
	return 0;
    if (mc == NULL) mc = rpmGlobalMacroContext;

    tbuf = alloca(slen + 1);
    memset(tbuf, 0, (slen + 1));

    /*@-temptrans -assignexpose@*/
    mb->s = sbuf;
    /*@=temptrans =assignexpose@*/
    mb->t = tbuf;
    mb->nb = slen;
    mb->depth = 0;
    mb->macro_trace = print_macro_trace;
    mb->expand_trace = print_expand_trace;

    /*@-temptrans -assignexpose@*/
    mb->spec = spec;	/* (future) %file expansion info */
    mb->mc = mc;
    /*@=temptrans =assignexpose@*/

    rc = expandMacro(mb);

    if (mb->nb == 0)
	rpmError(RPMERR_BADSPEC, "%s\n", _("Target buffer overflow"));

    tbuf[slen] = '\0';	/* XXX just in case */
    strncpy(sbuf, tbuf, (slen - mb->nb + 1));

    return rc;
}

void
addMacro(MacroContext mc,
	const char * n, const char * o, const char * b, int level)
{

    if (mc == NULL) mc = rpmGlobalMacroContext;

    struct MacroEntry_s keybuf = { .name = n };
    MacroEntry key = &keybuf;
    MacroEntry *mep = BSEARCH(&key, mc->macroTable, mc->macroTableSize,
			sizeof(*mc->macroTable), compareMacroName);
    if (mep == NULL) {
	AUTO_REALLOC(mc->macroTable, mc->macroTableSize, 128);
	mep = mc->macroTable + BSEARCH_IDX;
	/* move the remaining element to the right */
	MacroEntry *destp = mc->macroTable + mc->macroTableSize++;
	for (; destp > mep; destp--)
	    *destp = destp[-1];
	*mep = NULL;
    }
    /* Push macro over previous definition */
    pushMacro(mep, n, o, b, level);
}

void
delMacro(MacroContext mc, const char * n)
{
    MacroEntry * mep;

    if (mc == NULL) mc = rpmGlobalMacroContext;
    /* If name exists, pop entry */
    if ((mep = findEntry(mc, n, 0)) != NULL) {
	popMacro(mep);
	if (*mep == NULL) {
	    /* move the remaining element to the left */
	    MacroEntry *destp = mep;
	    MacroEntry *endp = mc->macroTable + --mc->macroTableSize;
	    for (; destp < endp; destp++)
		*destp = destp[1];
	    *destp = NULL;
	}
    }
}

/*@-mustmod@*/ /* LCL: mc is modified through mb->mc, mb is abstract */
int
rpmDefineMacro(MacroContext mc, const char * macro, int level)
{
    MacroBuf mb = alloca(sizeof(*mb));

    memset(mb, 0, sizeof(*mb));
    /* XXX just enough to get by */
    /*@-temptrans -assignexpose@*/
    mb->mc = (mc ? mc : rpmGlobalMacroContext);
    /*@=temptrans =assignexpose@*/
    (void) doDefine(mb, macro, level, 0);
    return 0;
}
/*@=mustmod@*/

void
rpmLoadMacros(MacroContext mc, int level)
{

    if (mc == NULL || mc == rpmGlobalMacroContext)
	return;

    int i;
    for (i = 0; i < mc->macroTableSize; i++) {
	MacroEntry me = mc->macroTable[i];
	addMacro(NULL, me->name, me->opts, me->body, (level - 1));
    }
}

static void
rpmInitMacrofile (const char *macrofile)
{
	char buf[BUFSIZ * 2];
	FD_t fd = Fopen(macrofile, "r.fpio");

	if (fd == NULL || Ferror(fd)) {
	    if (fd) (void) Fclose(fd);
	    return;
	}

	/* XXX Assume new fangled macro expansion */
	/*@-mods@*/
	max_macro_depth = 24;
	/*@=mods@*/

	buf[0] = '\0';
	while(rdcl(buf, sizeof(buf), fd) > 0) {
	    char c, *n;

	    n = buf;
/*@-globs@*/
	    SKIPBLANK(n, c);
/*@=globs@*/

	    if (c != '%')
		/*@innercontinue@*/ continue;
	    n++;	/* skip % */
	    (void) rpmDefineMacro(NULL, n, RMIL_MACROFILES);
	}
	(void) Fclose(fd);
}

static void
rpmInitMacrofileGlob (const char *macrofile)
{
	int is_local = strchr (macrofile, '~') ? 1 : 0;

	if (is_local || strchr (macrofile, '*'))
	{
		glob_t	gl;

		memset (&gl, 0, sizeof(gl));
		if (!glob(macrofile, GLOB_ERR | GLOB_NOESCAPE | GLOB_TILDE | GLOB_TILDE_CHECK, 0, &gl))
		{
			unsigned int i;

			for (i = 0; i < gl.gl_pathc; ++i)
				if (is_local)
					rpmInitMacrofile (gl.gl_pathv[i]);
				else
				{
					const char *p = strrchr (gl.gl_pathv[i], '/');

					if (!p)
						continue;

					if (!*++p)
						continue;

					for (; *p; ++p)
						if (!xisalnum (*p)) {
							if (('_' != *p) && ('-' != *p))
								break;
							if (p[1] == '\0')
								break;
						}
					if (!*p)
						rpmInitMacrofile (gl.gl_pathv[i]);
				}
		}
		globfree (&gl);
	}
	else
		rpmInitMacrofile (macrofile);
}

void
rpmInitMacros(/*@unused@*/ MacroContext mc, const char *macrofiles)
{
    char *m, *mfile, *me;

    if (macrofiles == NULL)
	return;
#ifdef	DYING
    if (mc == NULL) mc = rpmGlobalMacroContext;
#endif

    for (mfile = m = xstrdup(macrofiles); mfile && *mfile != '\0'; mfile = me) {
	for (me = mfile; (me = strchr(me, ':')) != NULL; me++) {
	    if (!(me[1] == '/' && me[2] == '/'))
		/*@innerbreak@*/ break;
	}

	if (me && *me == ':')
	    *me++ = '\0';
	else
	    me = mfile + strlen(mfile);

	rpmInitMacrofileGlob (mfile);
    }
    m = _free(m);

    /* Reload cmdline macros */
    /*@-mods@*/
    rpmLoadMacros(rpmCLIMacroContext, RMIL_CMDLINE);
    /*@=mods@*/
}

/*@-globstate@*/
void
rpmFreeMacros(MacroContext mc)
{
    
    if (mc == NULL) mc = rpmGlobalMacroContext;

    if (mc->macroTable != NULL) {
	int i;
	for (i = 0; i < mc->macroTableSize; i++) {
	    MacroEntry me;
	    while ((me = mc->macroTable[i]) != NULL) {
		/* XXX cast to workaround const */
		/*@-onlytrans@*/
		if ((mc->macroTable[i] = me->prev) == NULL)
		    me->name = _free(me->name);
		/*@=onlytrans@*/
		me->opts = _free(me->opts);
		me->body = _free(me->body);
		me = _free(me);
	    }
	}
	mc->macroTable = _free(mc->macroTable);
    }
    memset(mc, 0, sizeof(*mc));
}
/*@=globstate@*/

/* =============================================================== */
int isCompressed(const char * file, rpmCompressedMagic * compressed)
{
    *compressed = COMPRESSED_NOT;

    FD_t fd = Fopen(file, "r.ufdio");
    if (fd == NULL || Ferror(fd)) {
	/* XXX Fstrerror */
	rpmError(RPMERR_BADSPEC, _("File %s: %s\n"), file, Fstrerror(fd));
	if (fd) (void) Fclose(fd);
	return 1;
    }

    int rc = -1;
    unsigned char magic[8];
    ssize_t nb = Fread(magic, sizeof(magic[0]), sizeof(magic), fd);
    if (nb < 0) {
	rpmError(RPMERR_BADSPEC, _("File %s: %s\n"), file, Fstrerror(fd));
	rc = 1;
    } else if (nb < sizeof(magic)) {
	rpmMessage(RPMMESS_WARNING, _("File %s is smaller than %u bytes\n"),
		file, (unsigned)sizeof(magic));
	rc = 0;
    }
    (void) Fclose(fd);
    if (rc >= 0)
	return rc;

#define BT(s,t) \
	if (memcmp(s,magic,sizeof(s)-1)==0) \
	{ *compressed = COMPRESSED_##t; return 0; }
    BT("BZh", BZIP2);
    BT("PK\003\004", ZIP);
    BT("\037\213", OTHER); // gizp
    BT("\037\236", OTHER); // old gzip
    BT("\037\036", OTHER); // pack
    BT("\037\240", OTHER); // SCO lzh
    BT("\037\235", OTHER); // compress
    BT("\3757zXZ\0", XZ);
#undef BT

    /* LZMA has no magic */
    if (magic[0] < 0xE1 && magic[1] == 0) {
	const char *ext = strrchr(file, '.');
	if (ext)
	    if (strcmp(ext, ".lzma") == 0 || strcmp(ext, ".tlz") == 0)
		*compressed = COMPRESSED_LZMA;
    }

    return 0;
}

/* =============================================================== */

/*@-modfilesys@*/
char * 
rpmExpand(const char *arg, ...)
{
    char buf[BUFSIZ], *p, *pe;
    const char *s;
    va_list ap;

    if (arg == NULL)
	return xstrdup("");

    buf[0] = '\0';
    p = buf;
    pe = stpcpy(p, arg);

    va_start(ap, arg);
    while ((s = va_arg(ap, const char *)) != NULL)
	pe = stpcpy(pe, s);
    va_end(ap);
    (void) expandMacros(NULL, NULL, buf, sizeof(buf));
    return xstrdup(buf);
}
/*@=modfilesys@*/

int
rpmExpandNumeric(const char *arg)
{
    const char *val;
    int rc;

    if (arg == NULL)
	return 0;

    val = rpmExpand(arg, NULL);
    if (!(val && *val != '%'))
	rc = 0;
    else if (*val == 'Y' || *val == 'y')
	rc = 1;
    else if (*val == 'N' || *val == 'n')
	rc = 0;
    else {
	char *end;
	rc = strtol(val, &end, 0);
	if (!(end && *end == '\0'))
	    rc = 0;
    }
    val = _free(val);

    return rc;
}

/* @todo "../sbin/./../bin/" not correct. */
char *rpmCleanPath(char * path)
{
    const char *s;
    char *se, *t, *te;
    int begin = 1;

    if (path == NULL)
	return NULL;

/*fprintf(stderr, "*** RCP %s ->\n", path); */
    s = t = te = path;
    while (*s != '\0') {
/*fprintf(stderr, "*** got \"%.*s\"\trest \"%s\"\n", (t-path), path, s); */
	switch(*s) {
	case ':':			/* handle url's */
	    if (s[1] == '/' && s[2] == '/') {
		*t++ = *s++;
		*t++ = *s++;
		/*@switchbreak@*/ break;
	    }
	    begin=1;
	    /*@switchbreak@*/ break;
	case '/':
	    /* Move parent dir forward */
	    for (se = te + 1; se < t && *se != '/'; se++)
		{};
	    if (se < t && *se == '/') {
		te = se;
/*fprintf(stderr, "*** next pdir \"%.*s\"\n", (te-path), path); */
	    }
	    while (s[1] == '/')
		s++;
	    while (t > path && t[-1] == '/')
		t--;
	    /*@switchbreak@*/ break;
	case '.':
	    /* Leading .. is special */
 	    /* Check that it is ../, so that we don't interpret */
	    /* ..?(i.e. "...") or ..* (i.e. "..bogus") as "..". */
	    /* in the case of "...", this ends up being processed*/
	    /* as "../.", and the last '.' is stripped.  This   */
	    /* would not be correct processing.                 */
	    if (begin && s[1] == '.' && (s[2] == '/' || s[2] == '\0')) {
/*fprintf(stderr, "    leading \"..\"\n"); */
		*t++ = *s++;
		/*@switchbreak@*/ break;
	    }
	    /* Single . is special */
	    if (begin && s[1] == '\0') {
		/*@switchbreak@*/ break;
	    }
	    /* Trim embedded ./ , trailing /. */
	    if ((t[-1] == '/' && s[1] == '\0') || (t > path && t[-1] == '/' && s[1] == '/')) {
		s++;
		continue;
	    }
	    /* Trim embedded /../ and trailing /.. */
	    if (!begin && t > path && t[-1] == '/' && s[1] == '.' && (s[2] == '/' || s[2] == '\0')) {
		t = te;
		/* Move parent dir forward */
		if (te > path)
		    for (--te; te > path && *te != '/'; te--)
			{};
/*fprintf(stderr, "*** prev pdir \"%.*s\"\n", (te-path), path); */
		s++;
		s++;
		continue;
	    }
	    /*@switchbreak@*/ break;
	default:
	    begin = 0;
	    /*@switchbreak@*/ break;
	}
	*t++ = *s++;
    }

    /* Trim trailing / (but leave single / alone) */
    if (t > &path[1] && t[-1] == '/')
	t--;
    *t = '\0';

/*fprintf(stderr, "\t%s\n", path); */
    /*@-temptrans -retalias@*/ return path; /*@=temptrans =retalias@*/
}

/* Return concatenated and expanded canonical path. */

const char *
rpmGetPath(const char *path, ...)
{
    char buf[BUFSIZ];
    const char * s;
    char * t, * te;
    va_list ap;

    if (path == NULL)
	return xstrdup("");

    buf[0] = '\0';
    t = buf;
    te = stpcpy(t, path);
    *te = '\0';

    va_start(ap, path);
    while ((s = va_arg(ap, const char *)) != NULL) {
	te = stpcpy(te, s);
	*te = '\0';
    }
    va_end(ap);
/*@-modfilesys@*/
    (void) expandMacros(NULL, NULL, buf, sizeof(buf));
/*@=modfilesys@*/

    (void) rpmCleanPath(buf);
    return xstrdup(buf);	/* XXX xstrdup has side effects. */
}

/* Merge 3 args into path, any or all of which may be a url. */

const char * rpmGenPath(const char * urlroot, const char * urlmdir,
		const char *urlfile)
{
/*@owned@*/ const char * xroot = rpmGetPath(urlroot, NULL);
/*@dependent@*/ const char * root = xroot;
/*@owned@*/ const char * xmdir = rpmGetPath(urlmdir, NULL);
/*@dependent@*/ const char * mdir = xmdir;
/*@owned@*/ const char * xfile = rpmGetPath(urlfile, NULL);
/*@dependent@*/ const char * file = xfile;
    const char * result;
    const char * url = NULL;
    int nurl = 0;
    int ut;

#if 0
if (_debug) fprintf(stderr, "*** RGP xroot %s xmdir %s xfile %s\n", xroot, xmdir, xfile);
#endif
    ut = urlPath(xroot, &root);
    if (url == NULL && ut > URL_IS_DASH) {
	url = xroot;
	nurl = root - xroot;
#if 0
if (_debug) fprintf(stderr, "*** RGP ut %d root %s nurl %d\n", ut, root, nurl);
#endif
    }
    if (root == NULL || *root == '\0') root = "/";

    ut = urlPath(xmdir, &mdir);
    if (url == NULL && ut > URL_IS_DASH) {
	url = xmdir;
	nurl = mdir - xmdir;
#if 0
if (_debug) fprintf(stderr, "*** RGP ut %d mdir %s nurl %d\n", ut, mdir, nurl);
#endif
    }
    if (mdir == NULL || *mdir == '\0') mdir = "/";

    ut = urlPath(xfile, &file);
    if (url == NULL && ut > URL_IS_DASH) {
	url = xfile;
	nurl = file - xfile;
#if 0
if (_debug) fprintf(stderr, "*** RGP ut %d file %s nurl %d\n", ut, file, nurl);
#endif
    }

    if (url && nurl > 0) {
	char *t = strncpy(alloca(nurl+1), url, nurl);
	t[nurl] = '\0';
	url = t;
    } else
	url = "";

    result = rpmGetPath(url, root, "/", mdir, "/", file, NULL);

    xroot = _free(xroot);
    xmdir = _free(xmdir);
    xfile = _free(xfile);
#if 0
if (_debug) fprintf(stderr, "*** RGP result %s\n", result);
#endif
    return result;
}

/* =============================================================== */

#if defined(DEBUG_MACROS)

#if defined(EVAL_MACROS)

char *macrofiles = "/usr/lib/rpm/macros:/etc/rpm/macros:~/.rpmmacros";

int
main(int argc, char *argv[])
{
    int c;
    int errflg = 0;
    extern char *optarg;
    extern int optind;

    while ((c = getopt(argc, argv, "f:")) != EOF ) {
	switch (c) {
	case 'f':
	    macrofiles = optarg;
	    break;
	case '?':
	default:
	    errflg++;
	    break;
	}
    }
    if (errflg || optind >= argc) {
	fprintf(stderr, "Usage: %s [-f macropath ] macro ...\n", argv[0]);
	exit(1);
    }

    rpmInitMacros(NULL, macrofiles);
    for ( ; optind < argc; optind++) {
	const char *val;

	val = rpmGetPath(argv[optind], NULL);
	if (val) {
	    fprintf(stdout, "%s:\t%s\n", argv[optind], val);
	    val = _free(val);
	}
    }
    rpmFreeMacros(NULL);
    return 0;
}

#else	/* !EVAL_MACROS */

char *macrofiles = "../macros:./testmacros";
char *testfile = "./test";

int
main(int argc, char *argv[])
{
    char buf[BUFSIZ];
    FILE *fp;
    int x;

    rpmInitMacros(NULL, macrofiles);
    rpmDumpMacroTable(NULL, NULL);

    if ((fp = fopen(testfile, "r")) != NULL) {
	while(rdcl(buf, sizeof(buf), fp)) {
	    x = expandMacros(NULL, NULL, buf, sizeof(buf));
	    fprintf(stderr, "%d->%s\n", x, buf);
	    memset(buf, 0, sizeof(buf));
	}
	fclose(fp);
    }

    while(rdcl(buf, sizeof(buf), stdin)) {
	x = expandMacros(NULL, NULL, buf, sizeof(buf));
	fprintf(stderr, "%d->%s\n <-\n", x, buf);
	memset(buf, 0, sizeof(buf));
    }
    rpmFreeMacros(NULL);

    return 0;
}
#endif	/* EVAL_MACROS */
#endif	/* DEBUG_MACROS */
/*@=branchstate@*/
