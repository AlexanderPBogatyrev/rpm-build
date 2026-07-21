/** \ingroup rpmbuild
 * \file build/files.c
 *  The post-build, pre-packaging file tree walk to assemble the package
 *  manifest.
 */

#include "system.h"

#define	MYALLPERMS	07777

#include <stdlib.h>
#include <regex.h>
#include <signal.h>	/* getOutputFrom() */

#include "rpmio_internal.h"
#include "rpmbuild.h"

#include "buildio.h"

#include "debug.h"

/*@access Header @*/
/*@access TFI_t @*/
/*@access FD_t @*/
/*@access StringBuf @*/		/* compared with NULL */

#define	SKIPWHITE(_x)	{while(*(_x) && (xisspace(*_x) || *(_x) == ',')) (_x)++;}
#define	SKIPNONWHITE(_x){while(*(_x) &&!(xisspace(*_x) || *(_x) == ',')) (_x)++;}

#define MAXDOCDIR 1024

/**
 */
typedef enum specdFlags_e {
    SPECD_DEFFILEMODE	= (1 << 0),
    SPECD_DEFDIRMODE	= (1 << 1),
    SPECD_DEFUID	= (1 << 2),
    SPECD_DEFGID	= (1 << 3),
    SPECD_DEFVERIFY	= (1 << 4),

    SPECD_FILEMODE	= (1 << 8),
    SPECD_DIRMODE	= (1 << 9),
    SPECD_UID		= (1 << 10),
    SPECD_GID		= (1 << 11),
    SPECD_VERIFY	= (1 << 12)
} specdFlags;

/**
 */
typedef struct FileListRec_s {
    struct stat fl_st;
#define	fl_dev	fl_st.st_dev
#define	fl_ino	fl_st.st_ino
#define	fl_mode	fl_st.st_mode
#define	fl_nlink fl_st.st_nlink
#define	fl_uid	fl_st.st_uid
#define	fl_gid	fl_st.st_gid
#define	fl_rdev	fl_st.st_rdev
#define	fl_size	fl_st.st_size
#define	fl_mtime fl_st.st_mtime

/*@only@*/ const char * diskURL;	/* get file from here       */
/*@only@*/ const char * fileURL;	/* filename in cpio archive */
/*@observer@*/ const char * uname;
/*@observer@*/ const char * gname;
    unsigned	flags;
    specdFlags	specdFlags;	/* which attributes have been explicitly specified. */
    unsigned	verifyFlags;
/*@only@*/ const char *langs;	/* XXX locales separated with | */
    char *caps;
} * FileListRec;

/**
 */
typedef struct AttrRec_s {
    const char * ar_fmodestr;
    const char * ar_dmodestr;
    const char * ar_user;
    const char * ar_group;
    mode_t	ar_fmode;
    mode_t	ar_dmode;
} * AttrRec;

/**
 * Package file tree walk data.
 */
typedef struct FileList_s {
/*@only@*/ const char * buildRootURL;

    int fileCount;
    int processingFailed;

    int passedSpecialDoc;
    int isSpecialDoc;

    int noGlob;
    unsigned devtype;
    unsigned devmajor;
    int devminor;
    
    int isDir;
    int currentFlags;
    specdFlags currentSpecdFlags;
    int currentVerifyFlags;
    struct AttrRec_s cur_ar;
    struct AttrRec_s def_ar;
    specdFlags defSpecdFlags;
    int defVerifyFlags;
    int nLangs;
/*@only@*/ /*@null@*/ const char ** currentLangs;
    int haveCaps;
    char *currentCaps;

    /* Hard coded limit of MAXDOCDIR docdirs.         */
    /* If you break it you are doing something wrong. */
    const char * docDirs[MAXDOCDIR];
    int docDirCount;
    
/*@only@*/ FileListRec fileList;
    int fileListRecsAlloced;
    int fileListRecsUsed;
} * FileList;

/**
 */
static void nullAttrRec(/*@out@*/ AttrRec ar)	/*@modifies ar @*/
{
    ar->ar_fmodestr = NULL;
    ar->ar_dmodestr = NULL;
    ar->ar_user = NULL;
    ar->ar_group = NULL;
    ar->ar_fmode = 0;
    ar->ar_dmode = 0;
}

/**
 */
static void freeAttrRec(AttrRec ar)	/*@modifies ar @*/
{
    ar->ar_fmodestr = _free(ar->ar_fmodestr);
    ar->ar_dmodestr = _free(ar->ar_dmodestr);
    ar->ar_user = _free(ar->ar_user);
    ar->ar_group = _free(ar->ar_group);
    /* XXX doesn't free ar (yet) */
    /*@-nullstate@*/
    return;
    /*@=nullstate@*/
}

/**
 */
static void dupAttrRec(const AttrRec oar, /*@in@*/ /*@out@*/ AttrRec nar)
	/*@modifies nar @*/
{
    if (oar == nar)
	return;
    freeAttrRec(nar);
    nar->ar_fmodestr = (oar->ar_fmodestr ? xstrdup(oar->ar_fmodestr) : NULL);
    nar->ar_dmodestr = (oar->ar_dmodestr ? xstrdup(oar->ar_dmodestr) : NULL);
    nar->ar_user = (oar->ar_user ? xstrdup(oar->ar_user) : NULL);
    nar->ar_group = (oar->ar_group ? xstrdup(oar->ar_group) : NULL);
    nar->ar_fmode = oar->ar_fmode;
    nar->ar_dmode = oar->ar_dmode;
}

#if 0
/**
 */
static void dumpAttrRec(const char * msg, AttrRec ar)
	/*@globals fileSystem@*/
	/*@modifies fileSystem @*/
{
    if (msg)
	fprintf(stderr, "%s:\t", msg);
    fprintf(stderr, "(%s, %s, %s, %s)\n",
	ar->ar_fmodestr,
	ar->ar_user,
	ar->ar_group,
	ar->ar_dmodestr);
}
#endif

/* strtokWithQuotes() modified from glibc strtok() */
/* Copyright (C) 1991, 1996 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/**
 */
static char *strtokWithQuotes(char *s, char *delim)
	/*@modifies *s @*/
{
    static char *olds = NULL;
    char *token;

    if (s == NULL) {
	s = olds;
    }

    /* Skip leading delimiters */
    s += strspn(s, delim);
    if (*s == '\0') {
	return NULL;
    }

    /* Find the end of the token.  */
    token = s;
    if (*token == '"') {
	token++;
	/* Find next " char */
	s = strchr(token, '"');
    } else {
	s = strpbrk(token, delim);
    }

    /* Terminate it */
    if (s == NULL) {
	/* This token finishes the string */
	olds = strchr(token, '\0');
    } else {
	/* Terminate the token and make olds point past it */
	*s = '\0';
	olds = s+1;
    }

    /*@-retalias -temptrans @*/
    return token;
    /*@=retalias =temptrans @*/
}

/**
 */
static void timeCheck(int tc, Header h)
	/*@globals internalState @*/
	/*@modifies internalState @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    int * mtime;
    const char ** files;
    rpmTagType fnt;
    int count, x;
    time_t currentTime = time(NULL);

    x = hge(h, RPMTAG_OLDFILENAMES, &fnt, (void **) &files, &count);
    x = hge(h, RPMTAG_FILEMTIMES, NULL, (void **) &mtime, NULL);
    
    for (x = 0; x < count; x++) {
	if ((currentTime - mtime[x]) > tc)
	    rpmMessage(RPMMESS_WARNING, _("TIMECHECK failure: %s\n"), files[x]);
    }
    files = hfd(files, fnt);
}

/**
 */
typedef struct VFA {
/*@observer@*/ /*@null@*/ const char * attribute;
    int	flag;
} VFA_t;

/**
 */
/*@-exportlocal -exportheadervar@*/
/*@unchecked@*/
VFA_t verifyAttrs[] = {
    { "md5",	RPMVERIFY_MD5 },
    { "size",	RPMVERIFY_FILESIZE },
    { "link",	RPMVERIFY_LINKTO },
    { "user",	RPMVERIFY_USER },
    { "group",	RPMVERIFY_GROUP },
    { "mtime",	RPMVERIFY_MTIME },
    { "mode",	RPMVERIFY_MODE },
    { "rdev",	RPMVERIFY_RDEV },
    { NULL, 0 }
};
/*@=exportlocal =exportheadervar@*/

/**
 * @param buf
 * @param fl		package file tree walk data
 */
static int parseForVerify(char * buf, FileList fl)
	/*@modifies buf, fl->processingFailed,
		fl->currentVerifyFlags, fl->defVerifyFlags,
		fl->currentSpecdFlags, fl->defSpecdFlags @*/
{
    char *p, *pe, *q;
    const char *name;
    int *resultVerify;
    int negated;
    int verifyFlags;
    specdFlags * specdFlags;

    if ((p = strstr(buf, (name = "%verify"))) != NULL) {
	resultVerify = &(fl->currentVerifyFlags);
	specdFlags = &fl->currentSpecdFlags;
    } else if ((p = strstr(buf, (name = "%defverify"))) != NULL) {
	resultVerify = &(fl->defVerifyFlags);
	specdFlags = &fl->defSpecdFlags;
    } else
	return 0;

    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';

    SKIPSPACE(pe);

    if (*pe != '(') {
	rpmError(RPMERR_BADSPEC, _("Missing '(' in %s %s\n"), name, pe);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Bracket %*verify args */
    *pe++ = ' ';
    for (p = pe; *pe && *pe != ')'; pe++)
	{};

    if (*pe == '\0') {
	rpmError(RPMERR_BADSPEC, _("Missing ')' in %s(%s\n"), name, p);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Localize. Erase parsed string */
    q = alloca((pe-p) + 1);
    strncpy(q, p, pe-p);
    q[pe-p] = '\0';
    while (p <= pe)
	*p++ = ' ';

    negated = 0;
    verifyFlags = RPMVERIFY_NONE;

    for (p = q; *p != '\0'; p = pe) {
	SKIPWHITE(p);
	if (*p == '\0')
	    break;
	pe = p;
	SKIPNONWHITE(pe);
	if (*pe != '\0')
	    *pe++ = '\0';

	{   VFA_t *vfa;
	    for (vfa = verifyAttrs; vfa->attribute != NULL; vfa++) {
		if (strcmp(p, vfa->attribute))
		    /*@innercontinue@*/ continue;
		verifyFlags |= vfa->flag;
		/*@innerbreak@*/ break;
	    }
	    if (vfa->attribute)
		continue;
	}

	if (!strcmp(p, "not")) {
	    negated ^= 1;
	} else {
	    rpmError(RPMERR_BADSPEC, _("Invalid %s token: %s\n"), name, p);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
    }

    *resultVerify = negated ? ~(verifyFlags) : verifyFlags;
    *specdFlags |= SPECD_VERIFY;

    return 0;
}

#define	isAttrDefault(_ars)	((_ars)[0] == '-' && (_ars)[1] == '\0')

/**
 * Parse %dev from file manifest.
 * @param buf
 * @param fl		package file tree walk data
 * @return		0 on success
 */
static int parseForDev(char * buf, FileList fl)
	/*@modifies buf, fl->processingFailed,
		fl->noGlob, fl->devtype, fl->devmajor, fl->devminor @*/
{
    const char * name;
    const char * errstr = NULL;
    char *p, *pe, *q;
    int rc = RPMERR_BADSPEC;	/* assume error */

    if ((p = strstr(buf, (name = "%dev"))) == NULL)
	return 0;

    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';
    SKIPSPACE(pe);

    if (*pe != '(') {
	errstr = "'('";
	goto exit;
    }

    /* Bracket %dev args */
    *pe++ = ' ';
    for (p = pe; *pe && *pe != ')'; pe++)
	{};
    if (*pe != ')') {
	errstr = "')'";
	goto exit;
    }

    /* Localize. Erase parsed string */
    q = alloca((pe-p) + 1);
    strncpy(q, p, pe-p);
    q[pe-p] = '\0';
    while (p <= pe)
	*p++ = ' ';

    p = q; SKIPWHITE(p);
    pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
    if (*p == 'b')
	fl->devtype = 'b';
    else if (*p == 'c')
	fl->devtype = 'c';
    else {
	errstr = "devtype";
	goto exit;
    }

    p = pe; SKIPWHITE(p);
    pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
    for (pe = p; *pe && xisdigit(*pe); pe++)
	{} ;
    if (*pe == '\0') {
	fl->devmajor = atoi(p);
	/*@-unsignedcompare @*/	/* LCL: ge is ok */
	if (!(fl->devmajor >= 0 && fl->devmajor < 256)) {
	    errstr = "devmajor";
	    goto exit;
	}
	/*@=unsignedcompare @*/
	pe++;
    } else {
	errstr = "devmajor";
	goto exit;
    }

    p = pe; SKIPWHITE(p);
    pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
    for (pe = p; *pe && xisdigit(*pe); pe++)
	{} ;
    if (*pe == '\0') {
	fl->devminor = atoi(p);
	if (!(fl->devminor >= 0 && fl->devminor < 256)) {
	    errstr = "devminor";
	    goto exit;
	}
	pe++;
    } else {
	errstr = "devminor";
	goto exit;
    }

    fl->noGlob = 1;

    rc = 0;

exit:
    if (rc) {
	rpmError(RPMERR_BADSPEC, _("Missing %s in %s %s\n"), errstr, name, p);
	fl->processingFailed = 1;
    }
    return rc;
}

/**
 * Parse %attr and %defattr from file manifest.
 * @param buf
 * @param fl		package file tree walk data
 * @return		0 on success
 */
static int parseForAttr(char * buf, FileList fl)
	/*@modifies buf, fl->processingFailed,
		fl->cur_ar, fl->def_ar,
		fl->currentSpecdFlags, fl->defSpecdFlags @*/
{
    const char *name;
    char *p, *pe, *q;
    int x;
    struct AttrRec_s arbuf;
    AttrRec ar = &arbuf, ret_ar;
    specdFlags * specdFlags;

    if ( !buf || !fl )
	return 0;
    
    if ((p = strstr(buf, (name = "%attr"))) != NULL) {
	ret_ar = &(fl->cur_ar);
	specdFlags = &fl->currentSpecdFlags;
    } else if ((p = strstr(buf, (name = "%defattr"))) != NULL) {
	ret_ar = &(fl->def_ar);
	specdFlags = &fl->defSpecdFlags;
    } else
	return 0;

    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';

    SKIPSPACE(pe);

    if (*pe != '(') {
	rpmError(RPMERR_BADSPEC, _("Missing '(' in %s %s\n"), name, pe);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Bracket %*attr args */
    *pe++ = ' ';
    for (p = pe; *pe && *pe != ')'; pe++)
	{};

    if (ret_ar == &(fl->def_ar)) {	/* %defattr */
	q = pe;
	q++;
	SKIPSPACE(q);
	if (*q != '\0') {
	    rpmError(RPMERR_BADSPEC,
		     _("Non-white space follows %s(): %s\n"), name, q);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
    }

    /* Localize. Erase parsed string */
    q = alloca((pe-p) + 1);
    strncpy(q, p, pe-p);
    q[pe-p] = '\0';
    while (p <= pe)
	*p++ = ' ';

    nullAttrRec(ar);

    p = q; SKIPWHITE(p);
    if (*p != '\0') {
	pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
	ar->ar_fmodestr = p;
	p = pe; SKIPWHITE(p);
    }
    if (*p != '\0') {
	pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
	ar->ar_user = p;
	p = pe; SKIPWHITE(p);
    }
    if (*p != '\0') {
	pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
	ar->ar_group = p;
	p = pe; SKIPWHITE(p);
    }
    if (*p != '\0' && ret_ar == &(fl->def_ar)) {	/* %defattr */
	pe = p; SKIPNONWHITE(pe); if (*pe != '\0') *pe++ = '\0';
	ar->ar_dmodestr = p;
	p = pe; SKIPWHITE(p);
    }

    if (!(ar->ar_fmodestr && ar->ar_user && ar->ar_group) || *p != '\0') {
	rpmError(RPMERR_BADSPEC, _("Bad syntax: %s(%s)\n"), name, q);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Do a quick test on the mode argument and adjust for "-" */
    if (ar->ar_fmodestr && !isAttrDefault(ar->ar_fmodestr)) {
	unsigned int ui;
	x = sscanf(ar->ar_fmodestr, "%o", &ui);
	if ((x == 0) || (ar->ar_fmode & ~MYALLPERMS)) {
	    rpmError(RPMERR_BADSPEC, _("Bad mode spec: %s(%s)\n"), name, q);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
	ar->ar_fmode = ui;
    } else
	ar->ar_fmodestr = NULL;

    if (ar->ar_dmodestr && !isAttrDefault(ar->ar_dmodestr)) {
	unsigned int ui;
	x = sscanf(ar->ar_dmodestr, "%o", &ui);
	if ((x == 0) || (ar->ar_dmode & ~MYALLPERMS)) {
	    rpmError(RPMERR_BADSPEC, _("Bad dirmode spec: %s(%s)\n"), name, q);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
	ar->ar_dmode = ui;
    } else
	ar->ar_dmodestr = NULL;

    if (!(ar->ar_user && !isAttrDefault(ar->ar_user)))
	ar->ar_user = NULL;

    if (!(ar->ar_group && !isAttrDefault(ar->ar_group)))
	ar->ar_group = NULL;

    dupAttrRec(ar, ret_ar);

    /* XXX fix all this */
    *specdFlags |= SPECD_UID | SPECD_GID | SPECD_FILEMODE | SPECD_DIRMODE;
    
    return 0;
}

/**
 * Parse %config from file manifest.
 * @param buf
 * @param fl		package file tree walk data
 * @return		0 on success
 */
static int parseForConfig(char * buf, FileList fl)
	/*@modifies buf, fl->processingFailed,
		fl->currentFlags @*/
{
    char *p, *pe, *q;
    const char *name;

    if ((p = strstr(buf, (name = "%config"))) == NULL)
	return 0;

    fl->currentFlags = RPMFILE_CONFIG;

    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';
    SKIPSPACE(pe);
    if (*pe != '(')
	return 0;

    /* Bracket %config args */
    *pe++ = ' ';
    for (p = pe; *pe && *pe != ')'; pe++)
	{};

    if (*pe == '\0') {
	rpmError(RPMERR_BADSPEC, _("Missing ')' in %s(%s\n"), name, p);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Localize. Erase parsed string */
    q = alloca((pe-p) + 1);
    strncpy(q, p, pe-p);
    q[pe-p] = '\0';
    while (p <= pe)
	*p++ = ' ';

    for (p = q; *p != '\0'; p = pe) {
	SKIPWHITE(p);
	if (*p == '\0')
	    break;
	pe = p;
	SKIPNONWHITE(pe);
	if (*pe != '\0')
	    *pe++ = '\0';
	if (!strcmp(p, "missingok")) {
	    fl->currentFlags |= RPMFILE_MISSINGOK;
	} else if (!strcmp(p, "noreplace")) {
	    fl->currentFlags |= RPMFILE_NOREPLACE;
	} else {
	    rpmError(RPMERR_BADSPEC, _("Invalid %s token: %s\n"), name, p);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}
    }

    return 0;
}

/**
 */
static int langCmp(const void * ap, const void * bp)	/*@*/
{
    return strcmp(*(const char **)ap, *(const char **)bp);
}

/**
 * Parse %lang from file manifest.
 * @param buf
 * @param fl		package file tree walk data
 * @return		0 on success
 */
static int parseForLang(char * buf, FileList fl)
	/*@modifies buf, fl->processingFailed,
		fl->currentLangs, fl->nLangs @*/
{
    char *p, *pe, *q;
    const char *name;

  while ((p = strstr(buf, (name = "%lang"))) != NULL) {

    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';
    SKIPSPACE(pe);

    if (*pe != '(') {
	rpmError(RPMERR_BADSPEC, _("Missing '(' in %s %s\n"), name, pe);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Bracket %lang args */
    *pe++ = ' ';
    for (pe = p; *pe && *pe != ')'; pe++)
	{};

    if (*pe == '\0') {
	rpmError(RPMERR_BADSPEC, _("Missing ')' in %s(%s\n"), name, p);
	fl->processingFailed = 1;
	return RPMERR_BADSPEC;
    }

    /* Localize. Erase parsed string */
    q = alloca((pe-p) + 1);
    strncpy(q, p, pe-p);
    q[pe-p] = '\0';
    while (p <= pe)
	*p++ = ' ';

    /* Parse multiple arguments from %lang */
    for (p = q; *p != '\0'; p = pe) {
	char *newp;
	size_t np;
	int i;

	SKIPWHITE(p);
	pe = p;
	SKIPNONWHITE(pe);

	np = pe - p;
	
	/* Sanity check on locale lengths */
	if (np < 1 || (np == 1 && *p != 'C') || np >= 32) {
	    rpmError(RPMERR_BADSPEC,
		_("Unusual locale length: \"%.*s\" in %%lang(%s)\n"),
		(int)np, p, q);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}

	/* Check for duplicate locales */
	if (fl->currentLangs != NULL)
	for (i = 0; i < fl->nLangs; i++) {
	    if (strncmp(fl->currentLangs[i], p, np))
		/*@innercontinue@*/ continue;
	    rpmError(RPMERR_BADSPEC, _("Duplicate locale %.*s in %%lang(%s)\n"),
		(int)np, p, q);
	    fl->processingFailed = 1;
	    return RPMERR_BADSPEC;
	}

	/* Add new locale */
	fl->currentLangs = xrealloc(fl->currentLangs,
				(fl->nLangs + 1) * sizeof(*fl->currentLangs));
	newp = xmalloc( np+1 );
	strncpy(newp, p, np);
	newp[np] = '\0';
	fl->currentLangs[fl->nLangs++] = newp;
	if (*pe == ',') pe++;	/* skip , if present */
    }
  }

    /* Insure that locales are sorted. */
    if (fl->currentLangs)
	qsort(fl->currentLangs, fl->nLangs, sizeof(*fl->currentLangs), langCmp);

    return 0;
}

/**
 * Parse %caps from file manifest.
 * @param buf		current spec file line
 * @param fl		package file tree walk data
 * @return		RPMRC_OK on success
 */
static rpmRC parseForCaps(const char * buf, FileList fl)
{
    char *p, *pe, *q = NULL;
    const char *name;
    rpmRC rc = RPMRC_FAIL;

    if ((p = strstr(buf, (name = "%caps"))) == NULL)
	return RPMRC_OK;

    /* Erase "%caps" token. */
    for (pe = p; (pe-p) < strlen(name); pe++)
	*pe = ' ';
    SKIPSPACE(pe);
    if (*pe != '(')
	return RPMRC_OK;

    /* Bracket %caps args */
    *pe++ = ' ';
    for (p = pe; *pe && *pe != ')'; pe++)
	{};

    if (*pe == '\0') {
	rpmlog(RPMLOG_ERR, _("Missing ')' in %s(%s\n"), name, p);
	goto exit;
    }

    /* Localize. Erase parsed string. */
    q = xmalloc((pe-p) + 1);
    strlcpy(q, p, (pe-p) + 1);
    while (p <= pe)
	*p++ = ' ';

#if WITH_CAP
    {
	char *captxt = NULL;
	cap_t fcaps = cap_from_text(q);
	if (fcaps == NULL) {
	    rpmlog(RPMLOG_ERR, _("Invalid capability: %s\n"), q);
	    goto exit;
	}
	/* run our string through cap_to_text() to get libcap presentation */
	captxt = cap_to_text(fcaps, NULL);
	fl->currentCaps = xstrdup(captxt);
	fl->haveCaps = 1;
	cap_free(captxt);
	cap_free(fcaps);
    }
#else
	rpmlog(RPMLOG_ERR, _("File capability support not built in\n"));
	goto exit;
#endif

    rc = RPMRC_OK;
    
exit:
    free(q);
    if (rc != RPMRC_OK) {
	fl->processingFailed = 1;
    }

    return rc;
}
/**
 */
/*@-exportlocal -exportheadervar@*/
/*@unchecked@*/
VFA_t virtualFileAttributes[] = {
	{ "%dir",	0 },	/* XXX why not RPMFILE_DIR? */
	{ "%doc",	RPMFILE_DOC },
	{ "%ghost",	RPMFILE_GHOST },
	{ "%exclude",	RPMFILE_EXCLUDE },
	{ "%readme",	RPMFILE_README },
	{ "%license",	RPMFILE_LICENSE },

#if WHY_NOT
	{ "%spec",	RPMFILE_SPEC },
	{ "%config",	RPMFILE_CONFIG },
	{ "%donotuse",	RPMFILE_DONOTUSE },	/* XXX WTFO? */
	{ "%missingok",	RPMFILE_CONFIG|RPMFILE_MISSINGOK },
	{ "%noreplace",	RPMFILE_CONFIG|RPMFILE_NOREPLACE },
#endif

	{ NULL, 0 }
};
/*@=exportlocal =exportheadervar@*/

/**
 * Parse simple attributes (e.g. %dir) from file manifest.
 * @param spec
 * @param pkg
 * @param buf
 * @param fl		package file tree walk data
 * @retval fileName
 * @return		0 on success
 */
static int parseForSimple(/*@unused@*/Spec spec, Package pkg, char * buf,
			  FileList fl, /*@out@*/ const char ** fileName)
	/*@globals rpmGlobalMacroContext @*/
	/*@modifies buf, fl->processingFailed, *fileName,
		fl->currentFlags,
		fl->docDirs, fl->docDirCount, fl->isDir,
		fl->passedSpecialDoc, fl->isSpecialDoc,
		pkg->specialDoc, rpmGlobalMacroContext @*/
{
    char *s, *t;
    int res, specialDoc = 0;
    char specialDocBuf[BUFSIZ];

    specialDocBuf[0] = '\0';
    *fileName = NULL;
    res = 0;

    t = buf;
    while ((s = strtokWithQuotes(t, " \t\n")) != NULL) {
	t = NULL;
	if (!strcmp(s, "%docdir")) {
	    s = strtokWithQuotes(NULL, " \t\n");
	    if (!s || strtokWithQuotes(NULL, " \t\n")) {
		rpmError(RPMERR_INTERNAL, _("Only one arg for %%docdir\n"));
		fl->processingFailed = 1;
		res = 1;
	    } else if (fl->docDirCount == MAXDOCDIR) {
		rpmError(RPMERR_INTERNAL, _("Hit limit for %%docdir\n"));
		fl->processingFailed = 1;
		res = 1;
	    } else
		fl->docDirs[fl->docDirCount++] = xstrdup(s);
	    break;
	}

    /* Set flags for virtual file attributes */
    {	VFA_t *vfa;
	for (vfa = virtualFileAttributes; vfa->attribute != NULL; vfa++) {
	    if (strcmp(s, vfa->attribute))
		/*@innercontinue@*/ continue;
	    if (!vfa->flag) {
		if (!strcmp(s, "%dir"))
		    fl->isDir = 1;	/* XXX why not RPMFILE_DIR? */
	    } else
		fl->currentFlags |= vfa->flag;
	    /*@innerbreak@*/ break;
	}
	/* if we got an attribute, continue with next token */
	if (vfa->attribute != NULL)
	    continue;
    }

	if (*fileName) {
	    /* We already got a file -- error */
	    rpmError(RPMERR_BADSPEC, _("Two files on one line: %s\n"),
		*fileName);
	    fl->processingFailed = 1;
	    res = 1;
	}

	/*@-branchstate@*/
	if (*s != '/') {
	    if (fl->currentFlags & RPMFILE_DOC) {
		specialDoc = 1;
		strcat(specialDocBuf, " ");
		strcat(specialDocBuf, s);
	    } else {
		/* not in %doc, does not begin with / -- error */
		rpmError(RPMERR_BADSPEC,
		    _("File must begin with \"/\": %s\n"), s);
		fl->processingFailed = 1;
		res = 1;
	    }
	} else {
	    *fileName = s;
	}
	/*@=branchstate@*/
    }

    if (specialDoc) {
	if (*fileName || (fl->currentFlags & ~(RPMFILE_DOC))) {
	    rpmError(RPMERR_BADSPEC,
		     _("Can't mix special %%doc with other forms: %s\n"),
		     (*fileName ? *fileName : ""));
	    fl->processingFailed = 1;
	    res = 1;
	} else {
	/* XXX WATCHOUT: buf is an arg */
	    int custom = 0;

	    {
		const char *ddir = rpmExpand("%{?_customdocdir}", NULL);
		if (ddir && *ddir) {
		    custom = 1;
		} else {
		    const char *n, *v;
		    (void) headerNameVersion(pkg->header, &n, &v);
		    ddir = rpmGetPath("%{_docdir}/", n, "-", v, NULL);
		}
		strcpy(buf, ddir);
		ddir = _free(ddir);
	    }

	/* XXX FIXME: this is easy to do as macro expansion */

	    if (! fl->passedSpecialDoc) {
		pkg->specialDoc = newStringBuf();
		appendStringBuf(pkg->specialDoc, "DOCDIR=$RPM_BUILD_ROOT");
		appendLineStringBuf(pkg->specialDoc, buf);
		appendLineStringBuf(pkg->specialDoc, "export DOCDIR");
		if (!custom)
		    appendLineStringBuf(pkg->specialDoc, "rm -rf \"$DOCDIR\"");
		appendLineStringBuf(pkg->specialDoc, MKDIR_P " \"$DOCDIR\"");

		/*@-temptrans@*/
		*fileName = buf;
		/*@=temptrans@*/
		fl->passedSpecialDoc = 1;
		fl->isSpecialDoc = 1;
	    }

	    appendStringBuf(pkg->specialDoc, "cp -prL ");
	    appendStringBuf(pkg->specialDoc, specialDocBuf);
	    appendLineStringBuf(pkg->specialDoc, " \"$DOCDIR\"");
	    appendLineStringBuf(pkg->specialDoc, "chmod -R go-w \"$DOCDIR\"");
	    appendLineStringBuf(pkg->specialDoc, "chmod -R a+rX \"$DOCDIR\"");
	}
    }

    return res;
}

/**
 */
static int compareFileListRecs(const void * ap, const void * bp)	/*@*/
{
    const char *a = ((FileListRec)ap)->fileURL;
    const char *b = ((FileListRec)bp)->fileURL;
    return strcmp(a, b);
}

/**
 * Test if file is located in a %docdir.
 * @param fl		package file tree walk data
 * @param fileName	file path
 * @return		1 if doc file, 0 if not
 */
static int isDoc(FileList fl, const char * fileName)	/*@*/
{
    int i;
    for (i = 0; i < fl->docDirCount; i++) {
	const char *docdir = fl->docDirs[i];
	size_t len = strlen(docdir);
	if (strncmp(fileName, docdir, len) != 0)
	    continue;
	if (docdir[len-1] == '/')
	    return 1;
	if (fileName[len] == '/')
	    return 1;
    }
    return 0;
}

#define IS_DIR_SEPARATOR(c) ((c) == '/')
#define IS_NUL_OR_DIR_SEPARATOR(c) ((c) == '\0' || (c) == '/')
#define SKIP_DIR_SEPARATOR(s_) while (IS_DIR_SEPARATOR (*(s_))) (s_)++

static void
canonicalize_path (const char *s, char *d)
{
  if (*s == '\0')
    {
      *d = '\0';
      return;
    }

  const char *const orig_d = d;

  if (IS_DIR_SEPARATOR (*s))
    {
      *d++ = *s++;
      SKIP_DIR_SEPARATOR(s);
    }

  const char *const droot = d;

  while (*s)
    {
      /* At this point, we're always at the beginning of a path segment.  */
      if (s[0] == '.' && IS_NUL_OR_DIR_SEPARATOR (s[1]))
	{
	  s++;
	  SKIP_DIR_SEPARATOR(s);
	}

      else if (s[0] == '.' && s[1] == '.' && IS_NUL_OR_DIR_SEPARATOR (s[2]))
	{
	  char *pre = d - 1; /* includes slash */
	  while (droot < pre && IS_DIR_SEPARATOR (*pre))
	    pre--;
	  if (droot <= pre && ! IS_DIR_SEPARATOR (*pre))
	    {
	      while (droot < pre && ! IS_DIR_SEPARATOR (*pre))
		pre--;
	      /* If droot < pre, then pre points to the slash.  */
	      if (droot < pre)
		pre++;
	      if (pre < droot ||
		  (pre + 3 == d && pre[0] == '.' && pre[1] == '.'))
		{
		  /* Append ".." segment.  */
		  *d++ = *s++;
		  *d++ = *s++;
		}
	      else
		{
		  d = pre;
		  s += 2;
		  SKIP_DIR_SEPARATOR(s);
		}
	    }
	  else
	    {
	      /* Append ".." segment.  */
	      *d++ = *s++;
	      *d++ = *s++;
	    }
	}
      else
	{
	  while (*s && ! IS_DIR_SEPARATOR (*s))
	    *d++ = *s++;
	}

      if (IS_DIR_SEPARATOR (*s))
	{
	  *d++ = *s++;
	  SKIP_DIR_SEPARATOR(s);
	}
    }

  while (droot < d && IS_DIR_SEPARATOR (d[-1]))
    --d;

  if (d == orig_d)
    *d++ = '.';

  *d = '\0';
}

static int
canonicalize_symlinks_enabled (void)
{
	static int enabled = -1;

	if (enabled == -1) {
		static const char expr[] =
			"%{?_canonicalize_symlinks}%{?!_canonicalize_symlinks:1}";
		enabled = rpmExpandNumeric(expr) > 0;
	}

	return enabled;
}

/**
 * Add file entries to header.
 * @todo Should directories have %doc/%config attributes? (#14531)
 * @todo Remove RPMTAG_OLDFILENAMES, add dirname/basename instead.
 * @param fl		package file tree walk data
 * @param cpioList
 * @param h
 * @param isSrc
 */
static void genCpioListAndHeader(Spec spec, /*@partial@*/ FileList fl,
		TFI_t * cpioList, Header h, Package pkg, int isSrc)
	/*@globals rpmGlobalMacroContext,
		fileSystem @*/
	/*@modifies h, *cpioList, fl->processingFailed, fl->fileList,
		rpmGlobalMacroContext, fileSystem @*/
{
    int _addDotSlash = !isSrc;
    int apathlen = 0;
    int dpathlen = 0;
    int skipLen = 0;
    FileListRec flp;
    char buf[BUFSIZ];
    int i;
    int override_date = 0;
    time_t source_date_epoch;
    const char *srcdate = getenv("SOURCE_DATE_EPOCH");

    /* Limit the maximum date to SOURCE_DATE_EPOCH if defined
     * similar to the tar --clamp-mtime option
     * https://reproducible-builds.org/specs/source-date-epoch/
     */
    if (srcdate && *srcdate) {
	char *endptr;
	errno = 0;
	source_date_epoch = strtol(srcdate, &endptr, 10);
	if (srcdate == endptr || *endptr
	    || ((source_date_epoch == LONG_MIN || source_date_epoch == LONG_MAX)
		&& errno != 0)) {
	    rpmlog(RPMLOG_ERR, _("unable to parse %s=%s\n"), "SOURCE_DATE_EPOCH", srcdate);
	} else {
	    override_date = 1;
	}
    }

    /* Adjust cpio paths if needed */
    if (!isSrc && pkg->removePostfixes) {
	for (i = 1, flp = fl->fileList; i <= fl->fileListRecsUsed; i++, flp++) {
	    char * cpiopath = (char *)flp->fileURL;
	    const char * postfix_p = pkg->removePostfixes;
	    int len = strlen(postfix_p);
	    int plen = strlen(cpiopath);
	    if (len <= plen && !strncmp(cpiopath+plen-len, postfix_p, len)) {
		cpiopath[plen-len] = '\0';
		if (plen-len > 0 && cpiopath[plen-len-1] == '/') {
		    cpiopath[plen-len-1] = '\0';
		}
	    }
	}
    }

    /* Sort the big list */
    qsort(fl->fileList, fl->fileListRecsUsed,
	  sizeof(*(fl->fileList)), compareFileListRecs);
    
    /* Generate the header. */
    if (! isSrc) {
	skipLen = 1;
    }

    for (i = 1, flp = fl->fileList; i <= fl->fileListRecsUsed; i++, flp++) {
	char *s;

 	/* Merge duplicate entries. */
	while (i < fl->fileListRecsUsed &&
	    !strcmp(flp->fileURL, flp[1].fileURL)) {

	    /* Two entries for the same file found, merge the entries. */
	    /* Note that an %exclude is a duplication of a file reference */

	    /* file flags */
	    flp[1].flags |= flp->flags;	

	    if (!(flp[1].flags & RPMFILE_EXCLUDE))
		rpmMessage(RPMMESS_WARNING, _("File listed twice: %s\n"),
			flp->fileURL);
   
	    /* file mode */
	    if (S_ISDIR(flp->fl_mode)) {
		if ((flp[1].specdFlags & (SPECD_DIRMODE | SPECD_DEFDIRMODE)) <
		    (flp->specdFlags & (SPECD_DIRMODE | SPECD_DEFDIRMODE)))
			flp[1].fl_mode = flp->fl_mode;
	    } else {
		if ((flp[1].specdFlags & (SPECD_FILEMODE | SPECD_DEFFILEMODE)) <
		    (flp->specdFlags & (SPECD_FILEMODE | SPECD_DEFFILEMODE)))
			flp[1].fl_mode = flp->fl_mode;
	    }

	    /* uid */
	    if ((flp[1].specdFlags & (SPECD_UID | SPECD_DEFUID)) <
		(flp->specdFlags & (SPECD_UID | SPECD_DEFUID)))
	    {
		flp[1].fl_uid = flp->fl_uid;
		flp[1].uname = flp->uname;
	    }

	    /* gid */
	    if ((flp[1].specdFlags & (SPECD_GID | SPECD_DEFGID)) <
		(flp->specdFlags & (SPECD_GID | SPECD_DEFGID)))
	    {
		flp[1].fl_gid = flp->fl_gid;
		flp[1].gname = flp->gname;
	    }

	    /* verify flags */
	    if ((flp[1].specdFlags & (SPECD_VERIFY | SPECD_DEFVERIFY)) <
		(flp->specdFlags & (SPECD_VERIFY | SPECD_DEFVERIFY)))
		    flp[1].verifyFlags = flp->verifyFlags;

	    /* XXX to-do: language */

	    flp++; i++;
	}

	/* Skip files that were marked with %exclude. */
	if (flp->flags & RPMFILE_EXCLUDE) {
	    AUTO_REALLOC(spec->exclude, spec->excludeCount, 8);
	    spec->exclude[spec->excludeCount++] = xstrdup(flp->fileURL);
	    continue;
	}

	if (override_date && flp->fl_mtime > source_date_epoch) {
	    flp->fl_mtime = source_date_epoch;
	}

	/* Omit '/' and/or URL prefix, leave room for "./" prefix */
	apathlen += (strlen(flp->fileURL) - skipLen + (_addDotSlash ? 3 : 1));

	/* Leave room for both dirname and basename NUL's */
	dpathlen += (strlen(flp->diskURL) + 2);

	/*
	 * Make the header, the OLDFILENAMES will get converted to a 
	 * compressed file list write before we write the actual package to
	 * disk.
	 */
	(void) headerAddOrAppendEntry(h, RPMTAG_OLDFILENAMES, RPM_STRING_ARRAY_TYPE,
			       &(flp->fileURL), 1);

/*@-sizeoftype@*/
      if (sizeof(flp->fl_size) != sizeof(uint_32)) {
	uint_32 psize = (uint_32)flp->fl_size;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILESIZES, RPM_INT32_TYPE,
			       &(psize), 1);
      } else {
	(void) headerAddOrAppendEntry(h, RPMTAG_FILESIZES, RPM_INT32_TYPE,
			       &(flp->fl_size), 1);
      }
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEUSERNAME, RPM_STRING_ARRAY_TYPE,
			       &(flp->uname), 1);
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEGROUPNAME, RPM_STRING_ARRAY_TYPE,
			       &(flp->gname), 1);
      if (sizeof(flp->fl_mtime) != sizeof(uint_32)) {
	uint_32 mtime = (uint_32)flp->fl_mtime;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEMTIMES, RPM_INT32_TYPE,
			       &(mtime), 1);
      } else {
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEMTIMES, RPM_INT32_TYPE,
			       &(flp->fl_mtime), 1);
      }
      if (sizeof(flp->fl_mode) != sizeof(uint_16)) {
	uint_16 pmode = (uint_16)flp->fl_mode;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEMODES, RPM_INT16_TYPE,
			       &(pmode), 1);
      } else {
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEMODES, RPM_INT16_TYPE,
			       &(flp->fl_mode), 1);
      }
      if (sizeof(flp->fl_rdev) != sizeof(uint_16)) {
	uint_16 prdev = (uint_16)flp->fl_rdev;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILERDEVS, RPM_INT16_TYPE,
			       &(prdev), 1);
      } else {
	(void) headerAddOrAppendEntry(h, RPMTAG_FILERDEVS, RPM_INT16_TYPE,
			       &(flp->fl_rdev), 1);
      }

      uint_32 pdev = !!flp->fl_dev;
      (void) headerAddOrAppendEntry(h, RPMTAG_FILEDEVICES, RPM_INT32_TYPE,
				    &pdev, 1);

      uint_32 ino = i;
      if (S_ISREG(flp->fl_mode) && flp->fl_nlink != 1) {
	int j;
	FileListRec tmp;
	for (j = 1, tmp = fl->fileList; j < i; j++, tmp++) {
	  if (flp->fl_ino == tmp->fl_ino && flp->fl_dev == tmp->fl_dev) {
	    ino = j;
	    break;
	  }
	}
      }
      (void) headerAddOrAppendEntry(h, RPMTAG_FILEINODES, RPM_INT32_TYPE,
				    &ino, 1);
/*@=sizeoftype@*/

	(void) headerAddOrAppendEntry(h, RPMTAG_FILELANGS, RPM_STRING_ARRAY_TYPE,
			       &(flp->langs),  1);
	
	if (fl->haveCaps) {
	    (void) headerAddOrAppendEntry(h, RPMTAG_FILECAPS, RPM_STRING_ARRAY_TYPE,
				   &(flp->caps), 1);
	}

	/* We used to add these, but they should not be needed */
	/* (void) headerAddOrAppendEntry(h, RPMTAG_FILEUIDS,
	 *		   RPM_INT32_TYPE, &(flp->fl_uid), 1);
	 * (void) headerAddOrAppendEntry(h, RPMTAG_FILEGIDS,
	 *		   RPM_INT32_TYPE, &(flp->fl_gid), 1);
	 */
	
	buf[0] = '\0';
	if (S_ISREG(flp->fl_mode) && !(flp->flags & RPMFILE_GHOST))
	    (void) domd5(flp->diskURL, buf, 1);
	s = buf;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEMD5S, RPM_STRING_ARRAY_TYPE,
			       &s, 1);
	
	buf[0] = '\0';
	if (S_ISLNK(flp->fl_mode)) {
	    ssize_t llen = readlink(flp->diskURL, buf, BUFSIZ-1);
	    if (llen < 0) {
		rpmError(RPMERR_BADFILENAME,
			 _("reading symlink %s failed: %s\n"),
			 flp->diskURL, strerror(errno));
		fl->processingFailed = 1;
	    } else {
		buf[llen] = '\0';
		if (buf[0] == '/') {
		    rpmlog(RPMLOG_WARNING, _("absolute symlink: %s -> %s\n"),
			   flp->diskURL, buf);
		}
		if (fl->buildRootURL) {
		    const char * buildRoot;
		    (void) urlPath(fl->buildRootURL, &buildRoot);

		    if (buf[0] == '/' && strcmp(buildRoot, "/") &&
			!strncmp(buf, buildRoot, strlen(buildRoot))) {
			 rpmError(RPMERR_BADSPEC,
				    _("Symlink points to BuildRoot: %s -> %s\n"),
				    flp->fileURL, buf);
			fl->processingFailed = 1;
		    }
		}
		char dest[sizeof(buf)];
		canonicalize_path(buf, dest);
		if (strcmp(buf, dest) != 0) {
		    if (canonicalize_symlinks_enabled()) {
			rpmlog(RPMLOG_WARNING,
			       _("Symlink %s destination changed from %s to %s\n"),
			       flp->diskURL, buf, dest);
			strcpy(buf, dest);
		    } else {
			rpmlog(RPMLOG_WARNING,
			       _("Symlink %s destination should be changed from %s to %s\n"),
			       flp->diskURL, buf, dest);
		    }
		}
		if (!isSrc && pkg->removePostfixes) {
		    const char * postfix_p = pkg->removePostfixes;
		    int len = strlen(postfix_p);
		    int plen = strlen(buf);
		    if (len <= plen && !strncmp(buf+plen-len, postfix_p, len))
			buf[plen-len] = '\0';
		}
	    }
	}
	s = buf;
	(void) headerAddOrAppendEntry(h, RPMTAG_FILELINKTOS, RPM_STRING_ARRAY_TYPE,
			       &s, 1);
	
	if (flp->flags & RPMFILE_GHOST) {
	    flp->verifyFlags &= ~(RPMVERIFY_MD5 | RPMVERIFY_FILESIZE |
				RPMVERIFY_LINKTO | RPMVERIFY_MTIME);
	}
	(void) headerAddOrAppendEntry(h, RPMTAG_FILEVERIFYFLAGS, RPM_INT32_TYPE,
			       &(flp->verifyFlags), 1);
	
	if (!isSrc && isDoc(fl, flp->fileURL))
	    flp->flags |= RPMFILE_DOC;
	/* XXX Should directories have %doc/%config attributes? (#14531) */
	if (S_ISDIR(flp->fl_mode))
	    flp->flags &= ~(RPMFILE_CONFIG|RPMFILE_DOC);

	(void) headerAddOrAppendEntry(h, RPMTAG_FILEFLAGS, RPM_INT32_TYPE,
			       &(flp->flags), 1);

    }

    compressFilelist(h);

    if (fl->haveCaps) {
	rpmlibNeedsFeature(h, "FileCaps", "4.6.1-1");
    }

  { TFI_t fi = xcalloc(1, sizeof(*fi));
    char * a, * d;

    fi->type = TR_ADDED;
    loadFi(h, fi);
    fi->dnl = _free(fi->dnl);
    fi->bnl = _free(fi->bnl);

    fi->dnl = xmalloc(fi->fc * sizeof(*fi->dnl) + dpathlen);
    d = (char *)(fi->dnl + fi->fc);
    *d = '\0';

    fi->bnl = xmalloc(fi->fc * (sizeof(*fi->bnl) + sizeof(*fi->dil)));
    /*@-dependenttrans@*/ /* FIX: artifact of spoofing headerGetEntry */
    fi->dil = (int *)(fi->bnl + fi->fc);
    /*@=dependenttrans@*/

    fi->apath = xmalloc(fi->fc * sizeof(*fi->apath) + apathlen);
    a = (char *)(fi->apath + fi->fc);
    *a = '\0';

    fi->actions = xcalloc(sizeof(*fi->actions), fi->fc);
    fi->fmapflags = xcalloc(sizeof(*fi->fmapflags), fi->fc);
    fi->astriplen = 0;
    if (fl->buildRootURL)
	fi->astriplen = strlen(fl->buildRootURL);
    fi->striplen = 0;
    fi->fuser = NULL;
    fi->fuids = xcalloc(sizeof(*fi->fuids), fi->fc);
    fi->fgroup = NULL;
    fi->fgids = xcalloc(sizeof(*fi->fgids), fi->fc);
    fi->fsts = xcalloc(sizeof(*fi->fsts), fi->fc);

    /* Make the cpio list */
    for (i = 0, flp = fl->fileList; i < fi->fc; i++, flp++) {
	char * b;

	/* Skip (possible) duplicate file entries, use last entry info. */
	while (((flp - fl->fileList) < (fl->fileListRecsUsed - 1)) &&
		!strcmp(flp->fileURL, flp[1].fileURL))
	    flp++;

	if (flp->flags & RPMFILE_EXCLUDE) {
	    i--;
	    continue;
	}

	/* Create disk directory and base name. */
	fi->dil[i] = i;
	/*@-dependenttrans@*/ /* FIX: artifact of spoofing headerGetEntry */
	fi->dnl[fi->dil[i]] = d;
	/*@=dependenttrans@*/
#ifdef IA64_SUCKS_ROCKS
	(void) stpcpy(d, flp->diskURL);
	d += strlen(d);
#else
	d = stpcpy(d, flp->diskURL);
#endif

	/* Make room for the dirName NUL, find start of baseName. */
	for (b = d; b > fi->dnl[fi->dil[i]] && *b != '/'; b--)
	    b[1] = b[0];
	b++;		/* dirname's end in '/' */
	*b++ = '\0';	/* terminate dirname, b points to basename */
	fi->bnl[i] = b;
	d += 2;		/* skip both dirname and basename NUL's */

	/* Create archive path, normally adding "./" */
	/*@-dependenttrans@*/	/* FIX: xstrdup? nah ... */
	fi->apath[i] = a;
 	/*@=dependenttrans@*/
	if (_addDotSlash) {
#ifdef IA64_SUCKS_ROCKS
	    (void) stpcpy(a, "./");
	    a += strlen(a);
#else
	    a = stpcpy(a, "./");
#endif
	}
#ifdef IA64_SUCKS_ROCKS
	(void) stpcpy(a, (flp->fileURL + skipLen));
	a += strlen(a);
#else
	a = stpcpy(a, (flp->fileURL + skipLen));
#endif
	a++;		/* skip apath NUL */

	if (flp->flags & RPMFILE_GHOST) {
	    fi->actions[i] = FA_SKIP;
	    continue;
	}
	fi->actions[i] = FA_COPYOUT;
	fi->fuids[i] = getUidS(flp->uname);
	fi->fgids[i] = getGidS(flp->gname);
	if (fi->fuids[i] == (uid_t)-1) fi->fuids[i] = 0;
	if (fi->fgids[i] == (gid_t)-1) fi->fgids[i] = 0;
	fi->fmapflags[i] = CPIO_MAP_PATH |
		CPIO_MAP_TYPE | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID;
	if (isSrc)
	    fi->fmapflags[i] |= CPIO_FOLLOW_SYMLINKS;
	fi->fsts[i] = flp->fl_st;
    }
    /*@-branchstate@*/
    if (cpioList)
	*cpioList = fi;
    else
	fi = _free(fi);
    /*@=branchstate@*/
  }
}

/**
 */
static /*@null@*/ FileListRec freeFileList(/*@only@*/ FileListRec fileList,
			int count)
	/*@*/
{
    while (count--) {
	fileList[count].diskURL = _free(fileList[count].diskURL);
	fileList[count].fileURL = _free(fileList[count].fileURL);
	fileList[count].langs = _free(fileList[count].langs);
	fileList[count].caps = _free(fileList[count].caps);
    }
    fileList = _free(fileList);
    return NULL;
}

/* Written by Alexey Tourbin! */
static int pathIsCanonical(const char *path)
{
    enum {
	ST_NONE,
	ST_SLASH,
	ST_SLASHDOT,
	ST_SLASHDOTDOT
    } state = ST_NONE;
    const char *p = path;
    while (1) {
	int c = *p;
	switch (c) {
	case '/':
	    switch (state) {
	    case ST_SLASH:
	    case ST_SLASHDOT:
	    case ST_SLASHDOTDOT:
		return 0;
	    default:
		state = ST_SLASH;
		break;
	    }
	    break;
	case '.':
	    switch (state) {
	    case ST_SLASH:
		state = ST_SLASHDOT;
		break;
	    case ST_SLASHDOT:
		state = ST_SLASHDOTDOT;
		break;
	    default:
		state = ST_NONE;
		break;
	    }
	    break;
	case '\0':
	    switch (state) {
	    case ST_SLASHDOT:
	    case ST_SLASHDOTDOT:
		return 0;
	    case ST_SLASH:
		if (p > path + 1)
		    return 0;
		return 1;
	    default:
		return 1;
	    }
	    break;
	default:
	    state = ST_NONE;
	    break;
	}
	p++;
    }
    /* not reachable */
    return 0;
}

/* forward ref */
static rpmRC addFile1(FileList fl, const char * diskPath, struct stat * statp);
static rpmRC recurseDir(FileList fl, const char * diskPath);

/**
 * Add a file to the package manifest.
 * @param fl		package file tree walk data
 * @param diskPath	path to file
 * @return		RPMRC_OK on success
 */
static rpmRC addFile(FileList fl, const char * diskPath)
{
    const char *cpioPath = diskPath;
    
    /* Path may have prepended buildRootURL, so locate the original filename. */
    if (fl->buildRootURL && strcmp(fl->buildRootURL, "/")) {
	size_t br_len = strlen(fl->buildRootURL);
	if (strncmp(fl->buildRootURL, cpioPath, br_len) == 0
	&& (cpioPath[br_len] == '/' || cpioPath[br_len] == '\0'))
	    cpioPath += br_len;
	else {
	    rpmlog(RPMLOG_ERR, _("File doesn't match buildroot (%s): %s\n"),
		    fl->buildRootURL, cpioPath);
	    fl->processingFailed = 1;
	    return RPMRC_FAIL;
	}
    }

    /* XXX make sure '/' can be packaged also */
    if (*cpioPath == '\0')
	cpioPath = "/";

    /* cannot happen?! */
    if (*cpioPath != '/') {
	rpmlog(RPMLOG_ERR, _("File must begin with \"/\": %s\n"), cpioPath);
	fl->processingFailed = 1;
	return RPMRC_FAIL;
    }

    if (!pathIsCanonical(cpioPath)) {
	rpmlog(RPMLOG_ERR, _("File path must be canonical: %s\n"), cpioPath);
	fl->processingFailed = 1;
	return RPMRC_FAIL;
    }

    struct stat statbuf, *statp = &statbuf;
    {
	memset(statp, 0, sizeof(*statp));
	if (fl->devtype) {
	    time_t now = time(NULL);

	    /* XXX hack up a stat structure for a %dev(...) directive. */
	    statp->st_nlink = 1;
	    statp->st_rdev =
		((fl->devmajor & 0xff) << 8) | (fl->devminor & 0xff);
	    statp->st_dev = statp->st_rdev;
	    statp->st_mode = (fl->devtype == 'b' ? S_IFBLK : S_IFCHR);
	    statp->st_mode |= (fl->cur_ar.ar_fmode & 0777);
	    statp->st_atime = now;
	    statp->st_mtime = now;
	    statp->st_ctime = now;
	} else if (lstat(diskPath, statp)) {
	    rpmlog(RPMLOG_ERR, "%m: %s\n", diskPath);
	    fl->processingFailed = 1;
	    return RPMRC_FAIL;
	}
    }

    /* intermediate path component must be directories, not symlinks */
    {
	struct stat st;
	size_t dp_len = strlen(diskPath);
	char *dp = alloca(dp_len + 1);
	char *p = dp + dp_len - strlen(cpioPath);
	strcpy(dp, diskPath);
	while ((p = strchr(p + 1, '/'))) {
	    *p = '\0';
	    if (lstat(dp, &st)) {
		rpmlog(RPMLOG_ERR, "%m: %s\n", diskPath);
		fl->processingFailed = 1;
		return RPMRC_FAIL;
	    }
	    if (!S_ISDIR(st.st_mode)) {
		rpmlog(RPMLOG_ERR,
			_("File path component must be directory (%s): %s\n"),
			dp, diskPath);
		fl->processingFailed = 1;
		return RPMRC_FAIL;
	    }
	    *p = '/';
	}
    }

    if ((! fl->isDir) && S_ISDIR(statp->st_mode))
	return recurseDir(fl, diskPath);
    else
	return addFile1(fl, diskPath, statp);
}

/* implementation - no expensive tests */
static rpmRC addFile1(FileList fl, const char * diskPath, struct stat * statp)
{
    const char *cpioPath = diskPath;
    if (fl->buildRootURL && strcmp(fl->buildRootURL, "/"))
	cpioPath += strlen(fl->buildRootURL);
    if (*cpioPath == '\0')
	cpioPath = "/";
    assert(*cpioPath == '/');

    mode_t fileMode = statp->st_mode;
    uid_t fileUid = statp->st_uid;
    gid_t fileGid = statp->st_gid;
    const char *fileUname;
    const char *fileGname;

    if (S_ISDIR(fileMode) && fl->cur_ar.ar_dmodestr) {
	fileMode &= S_IFMT;
	fileMode |= fl->cur_ar.ar_dmode;
    } else if (fl->cur_ar.ar_fmodestr != NULL) {
	fileMode &= S_IFMT;
	fileMode |= fl->cur_ar.ar_fmode;
    }
    if (fl->cur_ar.ar_user) {
	fileUname = getUnameS(fl->cur_ar.ar_user);
    } else {
	fileUname = getUname(fileUid);
    }
    if (fl->cur_ar.ar_group) {
	fileGname = getGnameS(fl->cur_ar.ar_group);
    } else {
	fileGname = getGname(fileGid);
    }
	
    /* Default user/group to builder's user/group */
    if (fileUname == NULL)
	fileUname = getUname(getuid());
    if (fileGname == NULL)
	fileGname = getGname(getgid());
    
    /* Add to the file list */
    if (fl->fileListRecsUsed == fl->fileListRecsAlloced) {
	fl->fileListRecsAlloced += 128;
	fl->fileList = xrealloc(fl->fileList,
			fl->fileListRecsAlloced * sizeof(*(fl->fileList)));
    }
	    
    {	FileListRec flp = &fl->fileList[fl->fileListRecsUsed];
	int i;

	flp->fl_st = *statp;	/* structure assignment */
	flp->fl_mode = fileMode;
	flp->fl_uid = fileUid;
	flp->fl_gid = fileGid;
	if (S_ISDIR(fileMode))
	    flp->fl_size = 0;

	flp->fileURL = xstrdup(cpioPath);
	flp->diskURL = xstrdup(diskPath);
	flp->uname = fileUname;
	flp->gname = fileGname;

	if (fl->currentLangs && fl->nLangs > 0) {
	    char * ncl;
	    size_t nl = 0;
	    
	    for (i = 0; i < fl->nLangs; i++)
		nl += strlen(fl->currentLangs[i]) + 1;

	    flp->langs = ncl = xmalloc(nl);
	    for (i = 0; i < fl->nLangs; i++) {
	        const char *ocl;
		if (i)	*ncl++ = '|';
		for (ocl = fl->currentLangs[i]; *ocl != '\0'; ocl++)
			*ncl++ = *ocl;
		*ncl = '\0';
	    }
	} else {
	    flp->langs = xstrdup("");
	}

	if (fl->currentCaps) {
	    flp->caps = fl->currentCaps;
	} else {
	    flp->caps = xstrdup("");
	}

	flp->flags = fl->currentFlags;
	flp->specdFlags = fl->currentSpecdFlags;
	flp->verifyFlags = fl->currentVerifyFlags;

    }

    fl->fileListRecsUsed++;
    fl->fileCount++;

    return RPMRC_OK;
}

#include <fts.h>

/**
 * Add directory (and all of its files) to the package manifest.
 * @param fl		package file tree walk data
 * @param diskPath	path to file
 * @return		RPMRC_OK on success
 */
static rpmRC recurseDir(FileList fl, const char * diskPath)
{
    char * ftsSet[2];
    FTS * ftsp;
    FTSENT * fts;
    int myFtsOpts = (FTS_COMFOLLOW | FTS_NOCHDIR | FTS_PHYSICAL);
    rpmRC rc = RPMRC_FAIL;

    ftsSet[0] = (char *) diskPath;
    ftsSet[1] = NULL;
    ftsp = fts_open(ftsSet, myFtsOpts, NULL);
    while ((fts = fts_read(ftsp)) != NULL) {
	switch (fts->fts_info) {
	case FTS_D:		/* preorder directory */
	case FTS_F:		/* regular file */
	case FTS_SL:		/* symbolic link */
	case FTS_SLNONE:	/* symbolic link without target */
	case FTS_DEFAULT:	/* none of the above */
	    rc = addFile1(fl, fts->fts_accpath, fts->fts_statp);
	    break;
	case FTS_DOT:		/* dot or dot-dot */
	case FTS_DP:		/* postorder directory */
	    rc = RPMRC_OK;
	    break;
	case FTS_NS:		/* stat(2) failed */
	case FTS_DNR:		/* unreadable directory */
	case FTS_ERR:		/* error; errno is set */
	case FTS_DC:		/* directory that causes cycles */
	case FTS_NSOK:		/* no stat(2) requested */
	case FTS_INIT:		/* initialized only */
	case FTS_W:		/* whiteout object */
	default:
	    rpmlog(RPMLOG_WARNING, "%s: fts error\n", fts->fts_path);
	    rc = RPMRC_FAIL;
	    break;
	}
	if (rc)
	    break;
    }
    (void) fts_close(ftsp);

    return rc;
}

/**
 * Add a file to a binary package.
 * @param pkg
 * @param fl		package file tree walk data
 * @param fileURL
 * return		0 on success
 */
static int processBinaryFile(/*@unused@*/ Package pkg, FileList fl,
		const char * fileURL)
	/*@globals rpmGlobalMacroContext,
		fileSystem@*/
	/*@modifies *fl, fl->processingFailed,
		fl->fileList, fl->fileListRecsAlloced, fl->fileListRecsUsed,
		fl->fileCount, fl->isDir,
		rpmGlobalMacroContext, fileSystem @*/
{
    int doGlob;
    const char *diskURL = NULL;
    int rc = 0;
    
    doGlob = myGlobPatternP(fileURL);

    /* Check that file starts with leading "/" */
    {	const char * fileName;
	(void) urlPath(fileURL, &fileName);
	if (*fileName != '/') {
	    rpmError(RPMERR_BADSPEC, _("File needs leading \"/\": %s\n"),
			fileName);
	    rc = 1;
	    goto exit;
	}
    }
    
    /* Copy file name or glob pattern removing multiple "/" chars. */
    /*
     * Note: rpmGetPath should guarantee a "canonical" path. That means
     * that the following pathologies should be weeded out:
     *		//bin//sh
     *		//usr//bin/
     *		/.././../usr/../bin//./sh
     */
    diskURL = rpmGenPath(fl->buildRootURL, NULL, fileURL);

    if (doGlob) {
	const char ** argv = NULL;
	int argc = 0;
	int i;

	if (fl->noGlob) {
	    rpmError(RPMERR_BADSPEC, _("Glob not permitted: %s\n"),
			diskURL);
	    rc = 1;
	    goto exit;
	}

	/*@-branchstate@*/
	rc = rpmGlob(diskURL, &argc, &argv);
	if (rc == 0 && argc >= 1 && !myGlobPatternP(argv[0])) {
	    for (i = 0; i < argc; i++) {
		rc = addFile(fl, argv[i]);
		argv[i] = _free(argv[i]);
	    }
	    argv = _free(argv);
	} else {
	    rpmError(RPMERR_BADSPEC, _("File not found by glob: %s\n"),
			diskURL);
	    rc = 1;
	}
	/*@=branchstate@*/
    } else {
	rc = addFile(fl, diskURL);
    }

exit:
    diskURL = _free(diskURL);
    if (rc)
	fl->processingFailed = 1;
    return rc;
}

/**
 */
static int processPackageFiles(Spec spec, Package pkg,
			       int installSpecialDoc, int test)
	/*@globals rpmGlobalMacroContext,
		fileSystem, internalState@*/
	/*@modifies spec->macros,
		pkg->cpioList, pkg->fileList, pkg->specialDoc, pkg->header,
		rpmGlobalMacroContext, fileSystem, internalState @*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    struct FileList_s fl;
    char *s, **files, **fp;
    const char *fileName;
    char buf[BUFSIZ];
    struct AttrRec_s arbuf;
    AttrRec specialDocAttrRec = &arbuf;
    char *specialDoc = NULL;

    nullAttrRec(specialDocAttrRec);
    pkg->cpioList = NULL;

    if (pkg->fileFile) {
	const char *ffn;
	FILE * f;
	FD_t fd;

	/* XXX W2DO? urlPath might be useful here. */
	if (*pkg->fileFile == '/') {
	    ffn = rpmGetPath(pkg->fileFile, NULL);
	} else {
	    /* XXX FIXME: add %{_buildsubdir} */
	    ffn = rpmGetPath("%{_builddir}/",
		(spec->buildSubdir ? spec->buildSubdir : "") ,
		"/", pkg->fileFile, NULL);
	}
	fd = Fopen(ffn, "r.fpio");

	if (fd == NULL || Ferror(fd)) {
	    rpmError(RPMERR_BADFILENAME,
		_("Could not open %%files file %s: %s\n"),
		ffn, Fstrerror(fd));
	    return RPMERR_BADFILENAME;
	}
	ffn = _free(ffn);

	/*@+voidabstract@*/ f = fdGetFp(fd); /*@=voidabstract@*/
	if (f != NULL)
	while (fgets(buf, sizeof(buf), f)) {
	    handleComments(buf);
	    if (expandMacros(spec, spec->macros, buf, sizeof(buf))) {
		rpmError(RPMERR_BADSPEC, _("line: %s\n"), buf);
		return RPMERR_BADSPEC;
	    }
	    appendStringBuf(pkg->fileList, buf);
	}
	(void) Fclose(fd);
    }
    
    /* Init the file list structure */
    memset(&fl, 0, sizeof(fl));

    /* XXX spec->buildRootURL == NULL, then xstrdup("") is returned */
    fl.buildRootURL = rpmGenPath(spec->rootURL, spec->buildRootURL, NULL);

    fl.fileCount = 0;
    fl.processingFailed = 0;

    fl.passedSpecialDoc = 0;
    fl.isSpecialDoc = 0;

    fl.isDir = 0;
    fl.currentFlags = 0;
    fl.currentVerifyFlags = 0;
    
    fl.noGlob = 0;
    fl.devtype = 0;
    fl.devmajor = 0;
    fl.devminor = 0;

    nullAttrRec(&fl.cur_ar);
    nullAttrRec(&fl.def_ar);

    fl.defVerifyFlags = RPMVERIFY_ALL;
    fl.nLangs = 0;
    fl.currentLangs = NULL;
    fl.haveCaps = 0;
    fl.currentCaps = NULL;

    fl.currentSpecdFlags = 0;
    fl.defSpecdFlags = 0;

    fl.docDirCount = 0;
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/doc");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/man");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/info");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/X11R6/man");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/share/doc");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/share/man");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/share/info");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/lib/perl5/man");
    fl.docDirs[fl.docDirCount++] = xstrdup("/usr/share/gtk-doc/html");
    fl.docDirs[fl.docDirCount++] = rpmGetPath("%{_docdir}", NULL);
    fl.docDirs[fl.docDirCount++] = rpmGetPath("%{_mandir}", NULL);
    fl.docDirs[fl.docDirCount++] = rpmGetPath("%{_infodir}", NULL);
    
    fl.fileList = NULL;
    fl.fileListRecsAlloced = 0;
    fl.fileListRecsUsed = 0;

    s = getStringBuf(pkg->fileList);
    files = splitString(s, strlen(s), '\n');

    parseForAttr(rpmExpand("%{?_defattr}", NULL), &fl);

    for (fp = files; *fp != NULL; fp++) {
	s = *fp;
	SKIPSPACE(s);
	if (*s == '\0')
	    continue;
	fileName = NULL;
	/*@-nullpass@*/	/* LCL: buf is NULL ?!? */
	strcpy(buf, s);
	/*@=nullpass@*/
	
	/* Reset for a new line in %files */
	fl.isDir = 0;
	fl.currentFlags = 0;
	/* turn explicit flags into %def'd ones (gosh this is hacky...) */
	fl.currentSpecdFlags = ((unsigned)fl.defSpecdFlags) >> 8;
	fl.currentVerifyFlags = fl.defVerifyFlags;
	fl.isSpecialDoc = 0;

	fl.noGlob = 0;
 	fl.devtype = 0;
 	fl.devmajor = 0;
 	fl.devminor = 0;

	/* XXX should reset to %deflang value */
	if (fl.currentLangs) {
	    int i;
	    for (i = 0; i < fl.nLangs; i++)
		/*@-unqualifiedtrans@*/
		fl.currentLangs[i] = _free(fl.currentLangs[i]);
		/*@=unqualifiedtrans@*/
	    fl.currentLangs = _free(fl.currentLangs);
	}
  	fl.nLangs = 0;
	fl.currentCaps = NULL;

	dupAttrRec(&fl.def_ar, &fl.cur_ar);

	/*@-nullpass@*/	/* LCL: buf is NULL ?!? */
	if (parseForVerify(buf, &fl))
	    continue;
	if (parseForAttr(buf, &fl))
	    continue;
	if (parseForDev(buf, &fl))
	    continue;
	if (parseForConfig(buf, &fl))
	    continue;
	if (parseForLang(buf, &fl))
	    continue;
	if (parseForCaps(buf, &fl))
	    continue;
	/*@-nullstate@*/	/* FIX: pkg->fileFile might be NULL */
	if (parseForSimple(spec, pkg, buf, &fl, &fileName))
	/*@=nullstate@*/
	    continue;
	/*@=nullpass@*/
	if (fileName == NULL)
	    continue;

	/*@-branchstate@*/
	if (fl.isSpecialDoc) {
	    /* Save this stuff for last */
	    specialDoc = _free(specialDoc);
	    specialDoc = xstrdup(fileName);
	    dupAttrRec(&fl.cur_ar, specialDocAttrRec);
	} else {
	    /*@-nullstate@*/	/* FIX: pkg->fileFile might be NULL */
	    (void) processBinaryFile(pkg, &fl, fileName);
	    /*@=nullstate@*/
	}
	/*@=branchstate@*/
    }

    /* Now process special doc, if there is one */
    if (specialDoc) {
	if (installSpecialDoc) {
	    int rc = doScript(spec, RPMBUILD_STRINGBUF, "%doc", pkg->specialDoc, test);
	    if (rc) fl.processingFailed = 1;
	}

	/* Reset for %doc */
	fl.isDir = 0;
	fl.currentFlags = 0;
	fl.currentVerifyFlags = 0;

	fl.noGlob = 0;
 	fl.devtype = 0;
 	fl.devmajor = 0;
 	fl.devminor = 0;

	/* XXX should reset to %deflang value */
	if (fl.currentLangs) {
	    int i;
	    for (i = 0; i < fl.nLangs; i++)
		/*@-unqualifiedtrans@*/
		fl.currentLangs[i] = _free(fl.currentLangs[i]);
		/*@=unqualifiedtrans@*/
	    fl.currentLangs = _free(fl.currentLangs);
	}
  	fl.nLangs = 0;

	dupAttrRec(specialDocAttrRec, &fl.cur_ar);
	freeAttrRec(specialDocAttrRec);

	/*@-nullstate@*/	/* FIX: pkg->fileFile might be NULL */
	(void) processBinaryFile(pkg, &fl, specialDoc);
	/*@=nullstate@*/

	specialDoc = _free(specialDoc);
    }
    
    freeSplitString(files);

    if (fl.processingFailed)
	goto exit;

    genCpioListAndHeader(spec, &fl, (TFI_t *)&pkg->cpioList, pkg->header, pkg, 0);

    if (spec->timeCheck)
	timeCheck(spec->timeCheck, pkg->header);
    
exit:
    fl.buildRootURL = _free(fl.buildRootURL);

    freeAttrRec(&fl.cur_ar);
    freeAttrRec(&fl.def_ar);

    if (fl.currentLangs) {
	int i;
	for (i = 0; i < fl.nLangs; i++)
	    /*@-unqualifiedtrans@*/
	    fl.currentLangs[i] = _free(fl.currentLangs[i]);
	    /*@=unqualifiedtrans@*/
	fl.currentLangs = _free(fl.currentLangs);
    }

    fl.fileList = freeFileList(fl.fileList, fl.fileListRecsUsed);
    while (fl.docDirCount--)
	fl.docDirs[fl.docDirCount] = _free(fl.docDirs[fl.docDirCount]);
    return fl.processingFailed;
}

void initSourceHeader(Spec spec)
{
    HeaderIterator hi;
    int_32 tag, type, count;
    const void * ptr;

    spec->sourceHeader = headerNew();
    /* Only specific tags are added to the source package header */
    /*@-branchstate@*/
    for (hi = headerInitIterator(spec->packages->header);
	headerNextIterator(hi, &tag, &type, &ptr, &count);
	ptr = headerFreeData(ptr, type))
    {
	switch (tag) {
	case RPMTAG_NAME:
	case RPMTAG_VERSION:
	case RPMTAG_RELEASE:
	case RPMTAG_EPOCH:
	case RPMTAG_SUMMARY:
	case RPMTAG_DESCRIPTION:
	case RPMTAG_PACKAGER:
	case RPMTAG_DISTRIBUTION:
	case RPMTAG_DISTURL:
	case RPMTAG_VENDOR:
	case RPMTAG_LICENSE:
	case RPMTAG_GROUP:
	case RPMTAG_OS:
	case RPMTAG_ARCH:
	case RPMTAG_CHANGELOGTIME:
	case RPMTAG_CHANGELOGNAME:
	case RPMTAG_CHANGELOGTEXT:
	case RPMTAG_URL:
	case RPMTAG_VCS:
	case RPMTAG_BUILDHOST:
	case HEADER_I18NTABLE:
	    if (ptr)
		(void)headerAddEntry(spec->sourceHeader, tag, type, ptr, count);
	    /*@switchbreak@*/ break;
	default:
	    /* do not copy */
	    /*@switchbreak@*/ break;
	}
    }
    hi = headerFreeIterator(hi);
    /*@=branchstate@*/

    /* Add the build restrictions */
    /*@-branchstate@*/
    for (hi = headerInitIterator(spec->buildRestrictions);
	headerNextIterator(hi, &tag, &type, &ptr, &count);
	ptr = headerFreeData(ptr, type))
    {
	if (ptr)
	    (void) headerAddEntry(spec->sourceHeader, tag, type, ptr, count);
    }
    hi = headerFreeIterator(hi);
    /*@=branchstate@*/

    if (spec->BANames && spec->BACount > 0) {
	(void) headerAddEntry(spec->sourceHeader, RPMTAG_BUILDARCHS,
		       RPM_STRING_ARRAY_TYPE,
		       spec->BANames, spec->BACount);
    }
}

static int finalizeSize(TFI_t fi)
{
    if (fi == NULL)
	return 0;
    uint32_t totalFileSize = 0;
    int partialHardlinkSets = 0;
    for (int i = 0; i < fi->fc; i++) {
	if (fi->actions[i] == FA_SKIP) // %ghost
	    continue;
	if (!S_ISREG(fi->fsts[i].st_mode))
	    continue;
	if (fi->fsts[i].st_nlink == 1) {
	    totalFileSize += fi->fsts[i].st_size;
	    continue;
	}
	assert(fi->fsts[i].st_nlink > 1);
	int found = 0;
	// Look backwards, more likely in the same dir.
	for (int j = i - 1; j >= 0; j--) {
	    if (fi->actions[j] == FA_SKIP)
		continue;
	    if (fi->fsts[i].st_dev != fi->fsts[j].st_dev)
		continue;
	    if (fi->fsts[i].st_ino != fi->fsts[j].st_ino)
		continue;
	    found = 1;
	    break;
	}
	if (found)
	    continue;
	// first hardlink occurrence
	totalFileSize += fi->fsts[i].st_size;
	int nlink = 1;
	for (int j = i + 1; j < fi->fc; j++) {
	    if (fi->actions[j] == FA_SKIP)
		continue;
	    if (fi->fsts[i].st_dev != fi->fsts[j].st_dev)
		continue;
	    if (fi->fsts[i].st_ino != fi->fsts[j].st_ino)
		continue;
	    // XXX check for identical locale coloring?
	    nlink++;
	}
	assert(nlink <= fi->fsts[i].st_nlink);
	if (nlink < fi->fsts[i].st_nlink)
	    partialHardlinkSets = 1;
    }
    headerAddEntry(fi->h, RPMTAG_SIZE, RPM_INT32_TYPE, &totalFileSize, 1);
    // XXX handle PartialHardlinkSets?
    return 0;
}

static
int finalizePkg(Package pkg)
{
    return finalizeSize(pkg->cpioList);
}

static
int finalizeSrc(Spec spec)
{
    return finalizeSize(spec->sourceCpioList);
}

int processSourceFiles(Spec spec)
{
    struct Source *srcPtr;
    StringBuf sourceFiles;
    int x, isSpec = 1;
    struct FileList_s fl;
    char *s, **files, **fp;
    Package pkg;

    sourceFiles = newStringBuf();

    /* XXX
     * XXX This is where the source header for noarch packages needs
     * XXX to be initialized.
     */
    if (spec->sourceHeader == NULL)
	initSourceHeader(spec);

    /* Construct the file list and source entries */
    appendLineStringBuf(sourceFiles, spec->specFile);
    if (spec->sourceHeader != NULL)
    for (srcPtr = spec->sources; srcPtr != NULL; srcPtr = srcPtr->next) {
	if (srcPtr->flags & RPMBUILD_ISSOURCE) {
	    (void) headerAddOrAppendEntry(spec->sourceHeader, RPMTAG_SOURCE,
				   RPM_STRING_ARRAY_TYPE, &srcPtr->source, 1);
	    if (srcPtr->flags & RPMBUILD_ISNO) {
		(void) headerAddOrAppendEntry(spec->sourceHeader, RPMTAG_NOSOURCE,
				       RPM_INT32_TYPE, &srcPtr->num, 1);
	    }
	}
	if (srcPtr->flags & RPMBUILD_ISPATCH) {
	    (void) headerAddOrAppendEntry(spec->sourceHeader, RPMTAG_PATCH,
				   RPM_STRING_ARRAY_TYPE, &srcPtr->source, 1);
	    if (srcPtr->flags & RPMBUILD_ISNO) {
		(void) headerAddOrAppendEntry(spec->sourceHeader, RPMTAG_NOPATCH,
				       RPM_INT32_TYPE, &srcPtr->num, 1);
	    }
	}

      {	const char * sfn;
	sfn = rpmGetPath( ((srcPtr->flags & RPMBUILD_ISNO) ? "!" : ""),
		"%{_sourcedir}/", srcPtr->source, NULL);
	appendLineStringBuf(sourceFiles, sfn);
	sfn = _free(sfn);
      }
    }

    for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
	for (srcPtr = pkg->icon; srcPtr != NULL; srcPtr = srcPtr->next) {
	    const char * sfn;
	    sfn = rpmGetPath( ((srcPtr->flags & RPMBUILD_ISNO) ? "!" : ""),
		"%{_sourcedir}/", srcPtr->source, NULL);
	    appendLineStringBuf(sourceFiles, sfn);
	    sfn = _free(sfn);
	}
    }

    spec->sourceCpioList = NULL;

    fl.fileList = xcalloc((spec->numSources + 1), sizeof(*fl.fileList));
    fl.processingFailed = 0;
    fl.fileListRecsUsed = 0;
    fl.buildRootURL = NULL;

    s = getStringBuf(sourceFiles);
    files = splitString(s, strlen(s), '\n');

    /* The first source file is the spec file */
    x = 0;
    for (fp = files; *fp != NULL; fp++) {
	const char * diskURL, *diskPath;
	FileListRec flp;

	diskURL = *fp;
	SKIPSPACE(diskURL);
	if (! *diskURL)
	    continue;

	flp = &fl.fileList[x];

	flp->flags = isSpec ? RPMFILE_SPECFILE : 0;
	/* files with leading ! are no source files */
	if (*diskURL == '!') {
	    flp->flags |= RPMFILE_GHOST;
	    diskURL++;
	}

	(void) urlPath(diskURL, &diskPath);

	flp->diskURL = xstrdup(diskURL);
	diskPath = strrchr(diskPath, '/');
	if (diskPath)
	    diskPath++;
	else
	    diskPath = diskURL;

	flp->fileURL = xstrdup(diskPath);
	flp->verifyFlags = RPMVERIFY_ALL;

	if (Stat(diskURL, &flp->fl_st)) {
	    rpmError(RPMERR_BADSPEC, _("Bad file: %s: %s\n"),
		diskURL, strerror(errno));
	    fl.processingFailed = 1;
	}

	flp->uname = getUname(flp->fl_uid);
	flp->gname = getGname(flp->fl_gid);
	flp->langs = xstrdup("");
	flp->caps = xstrdup("");
	
	if (! (flp->uname && flp->gname)) {
	    rpmError(RPMERR_BADSPEC, _("Bad owner/group: %s\n"), diskURL);
	    fl.processingFailed = 1;
	}

	isSpec = 0;
	x++;
    }
    fl.fileListRecsUsed = x;
    freeSplitString(files);

    if (! fl.processingFailed) {
	if (spec->sourceHeader != NULL)
	    genCpioListAndHeader(spec, &fl, (TFI_t *)&spec->sourceCpioList,
			spec->sourceHeader, NULL, 1);
	finalizeSrc(spec);
    }

    sourceFiles = freeStringBuf(sourceFiles);
    fl.fileList = freeFileList(fl.fileList, fl.fileListRecsUsed);
    return fl.processingFailed;
}

/**
 */
static StringBuf getOutputFrom(const char **argv,
			const char * writePtr, int writeBytesLeft)
	/*@globals fileSystem, internalState@*/
	/*@modifies fileSystem, internalState@*/
{
    int progPID;
    int toProg[2];
    int fromProg[2];
    int status;
    StringBuf readBuff;
    int done;

    toProg[0] = toProg[1] = 0;
    (void) pipe(toProg);
    fromProg[0] = fromProg[1] = 0;
    (void) pipe(fromProg);
    
    if (!(progPID = fork())) {
	(void) close(toProg[1]);
	(void) close(fromProg[0]);
	
	(void) dup2(toProg[0], STDIN_FILENO);   /* Make stdin the in pipe */
	(void) dup2(fromProg[1], STDOUT_FILENO); /* Make stdout the out pipe */

	(void) close(toProg[0]);
	(void) close(fromProg[1]);

	if ( rpm_close_all() ) {
		perror( "rpm_close_all" );
		_exit( -1 );
	}

	(void) execvp(argv[0], (char *const *)argv);
	/* XXX this error message is probably not seen. */
	rpmError(RPMERR_EXEC, _("Couldn't exec %s: %s\n"),
		argv[0], strerror(errno));
	_exit(RPMERR_EXEC);
    }
    if (progPID < 0) {
	rpmError(RPMERR_FORK, _("Couldn't fork %s: %s\n"),
		argv[0], strerror(errno));
	return NULL;
    }

    (void) close(toProg[0]);
    (void) close(fromProg[1]);

    sighandler_t oldhandler = signal(SIGPIPE, SIG_IGN);

    /* Do not block reading or writing from/to prog. */
    (void) fcntl(fromProg[0], F_SETFL, O_NONBLOCK);
    (void) fcntl(toProg[1], F_SETFL, O_NONBLOCK);
    
    readBuff = newStringBuf();

    do {
	fd_set ibits, obits;
	struct timeval tv;
	int nfd, nbw, nbr;
	int rc;

	done = 0;
top:
	/* XXX the select is mainly a timer since all I/O is non-blocking */
	FD_ZERO(&ibits);
	FD_ZERO(&obits);
	if (fromProg[0] >= 0) {
	    FD_SET(fromProg[0], &ibits);
	}
	if (toProg[1] >= 0) {
	    FD_SET(toProg[1], &obits);
	}
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	nfd = ((fromProg[0] > toProg[1]) ? fromProg[0] : toProg[1]);
	if ((rc = select(nfd, &ibits, &obits, NULL, &tv)) < 0) {
	    if (errno == EINTR)
		goto top;
	    break;
	}

	/* Write any data to program */
	if (toProg[1] >= 0 && FD_ISSET(toProg[1], &obits)) {
          if (writeBytesLeft) {
	    if ((nbw = write(toProg[1], writePtr,
		    (1024<writeBytesLeft) ? 1024 : writeBytesLeft)) < 0) {
	        if (errno != EAGAIN) {
		    perror("getOutputFrom()");
	            exit(EXIT_FAILURE);
		}
	        nbw = 0;
	    }
	    writeBytesLeft -= nbw;
	    writePtr += nbw;
	  } else if (toProg[1] >= 0) {	/* close write fd */
	    (void) close(toProg[1]);
	    toProg[1] = -1;
	  }
	}
	
	/* Read any data from prog */
	{   char buf[BUFSIZ+1];
	    while ((nbr = read(fromProg[0], buf, sizeof(buf)-1)) > 0) {
		buf[nbr] = '\0';
		appendStringBuf(readBuff, buf);
	    }
	}

	/* terminate on (non-blocking) EOF or error */
	done = (nbr == 0 || (nbr < 0 && errno != EAGAIN));

    } while (!done);

    /* Clean up */
    if (toProg[1] >= 0)
    	(void) close(toProg[1]);
    if (fromProg[0] >= 0)
	(void) close(fromProg[0]);
    signal(SIGPIPE, oldhandler);

    /* Collect status from prog */
    (void)waitpid(progPID, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
	rpmError(RPMERR_EXEC, _("%s failed\n"), argv[0]);
	return NULL;
    }
    if (writeBytesLeft) {
	rpmError(RPMERR_EXEC, _("failed to write all data to %s\n"), argv[0]);
	return NULL;
    }
    return readBuff;
}

static
StringBuf runPkgScript(Package pkg, const char *body,
	const char *fileList, int fileListLen)
{
    const char *name = "+autodep";
    const char *tmpdir = getenv("TMPDIR");
    if (!(tmpdir && *tmpdir))
	tmpdir = "/tmp";
    char script[strlen(tmpdir) + sizeof("/rpmb-autodep.XXXXXX")];
    sprintf(script, "%s/rpmb-autodep.XXXXXX", tmpdir);
    int fd = mkstemp(script);
    assert(fd >= 0);
    FILE *fp = fdopen(fd, "w");
    assert(fp);
    // script header
    char *s;
    int isVerbose = rpmIsVerbose();
    if (isVerbose)
	rpmDecreaseVerbosity();
    s = rpmExpand("%{?__spec_autodep_template}\n", NULL);
    if (isVerbose)
	rpmIncreaseVerbosity();
    fputs(s, fp);
    free(s);
    // pkg info
    const char *N, *V, *R, *A;
    headerNEVRA(pkg->header, &N, NULL, &V, &R, &A);
    fprintf(fp, "export RPM_SUBPACKAGE_NAME='%s'\n", N);
    fprintf(fp, "export RPM_SUBPACKAGE_VERSION='%s'\n", V);
    fprintf(fp, "export RPM_SUBPACKAGE_RELEASE='%s'\n", R);
    fprintf(fp, "export RPM_SUBPACKAGE_ARCH='%s'\n", A);
    // script body
    fputs(body, fp);
    fputc('\n', fp);
    // script footer
    s = rpmExpand("%{?__spec_autodep_post}\n", NULL);
    fputs(s, fp);
    free(s);
    fclose(fp);
    // run script
    char *cmd = rpmExpand("%{?___build_cmd}", " ", script, NULL);
    rpmMessage(RPMMESS_NORMAL, _("Executing(%s): %s\n"), name, cmd);
    const char **argv = NULL;
    poptParseArgvString(cmd, NULL, &argv);
    assert(argv);
    StringBuf out = getOutputFrom(argv, fileList, fileListLen);
    if (out)
	unlink(script);
    free(argv);
    return out;
}

/**
 */
typedef struct {
/*@observer@*/ /*@null@*/ const char * msg;
/*@observer@*/ const char *argv[3];
    rpmTag ntag;
    rpmTag vtag;
    rpmTag ftag;
    int mask;
    int xor;
} DepMsg_t;

/**
 */
/*@-exportlocal -exportheadervar@*/
/*@unchecked@*/
DepMsg_t depMsgs[] = {
  { "Provides",		{ "%{?__find_provides}", 0 },
	RPMTAG_PROVIDENAME, RPMTAG_PROVIDEVERSION, RPMTAG_PROVIDEFLAGS,
	0, -1 },
  { "Requires",		{ "%{?__find_requires}", 0 },
	RPMTAG_REQUIRENAME, RPMTAG_REQUIREVERSION, RPMTAG_REQUIREFLAGS,
	RPMSENSE_PREREQ, RPMSENSE_PREREQ },
  { "Requires(interp)",	{ 0, "interp", 0 },
	-1, -1, RPMTAG_REQUIREFLAGS,
	_notpre(RPMSENSE_INTERP), 0 },
  { "Requires(rpmlib)",	{ 0, "rpmlib", 0 },
	-1, -1, RPMTAG_REQUIREFLAGS,
	_notpre(RPMSENSE_RPMLIB), 0 },
  { "Requires(verify)",	{ 0, "verify", 0 },
	-1, -1, RPMTAG_REQUIREFLAGS,
	RPMSENSE_SCRIPT_VERIFY, 0 },
  { "Requires(pre)",	{ 0, "pre", 0 },
	-1, -1, RPMTAG_REQUIREFLAGS,
	_notpre(RPMSENSE_SCRIPT_PRE), 0 },
  { "Requires(post)",	{ 0, "post", 0 },
	-1, -1, RPMTAG_REQUIREFLAGS,
	_notpre(RPMSENSE_SCRIPT_POST), 0 },
  { "Requires(preun)",	{ 0, "preun", 0 },
	-1, -1, RPMTAG_REQUIREFLAGS,
	_notpre(RPMSENSE_SCRIPT_PREUN), 0 },
  { "Requires(postun)",	{ 0, "postun", 0 },
	-1, -1, RPMTAG_REQUIREFLAGS,
	_notpre(RPMSENSE_SCRIPT_POSTUN), 0 },
  { "Conflicts",	{ "%{?__find_conflicts}", 0 },
	RPMTAG_CONFLICTNAME, RPMTAG_CONFLICTVERSION, RPMTAG_CONFLICTFLAGS,
	0, -1 },
  { "Obsoletes",	{ "%{?__find_obsoletes}", 0 },
	RPMTAG_OBSOLETENAME, RPMTAG_OBSOLETEVERSION, RPMTAG_OBSOLETEFLAGS,
	0, -1 },
  { 0,		{ 0 },	0, 0, 0, 0, 0 }
};
/*@=exportlocal =exportheadervar@*/

typedef struct {
    char *scriptname;
    int progTag;
    int scriptTag;
} ScriptDep_t;

ScriptDep_t scriptDeps[] = {
    { "pre",	RPMTAG_PREINPROG, RPMTAG_PREIN },
    { "preun",	RPMTAG_PREUNPROG, RPMTAG_PREUN },
    { "post",	RPMTAG_POSTINPROG, RPMTAG_POSTIN },
    { "postun",	RPMTAG_POSTUNPROG, RPMTAG_POSTUN },
    { NULL,	0, 0 }
};

/* Save script conents in a file under buildroot.  */
static
const char *saveInstScript(Spec spec, Package pkg, const char *scriptname)
{
    ScriptDep_t *sd;
    int progTag = 0, scriptTag = 0;

    for (sd = scriptDeps; sd->scriptname; sd++)
	if (strcmp(scriptname, sd->scriptname) == 0) {
	    progTag = sd->progTag;
	    scriptTag = sd->scriptTag;
	    break;
	}
    if (!scriptTag)
	return NULL;

    /* similar to runInstScript() */
    rpmTagType stt;
    const char *script = NULL;
    HGE_t hge = (HGE_t)headerGetEntry;
    HFD_t hfd = headerFreeData;

    hge(pkg->header, scriptTag, &stt, (void**)&script, NULL);
    if (!script)
	return NULL;

    TFI_t fi = pkg->cpioList;
    if (!(fi && fi->fc > 0)) /* possibly a packaging bug */
	rpmMessage(RPMMESS_WARNING, _("package with no files has %%%s-script\n"), scriptname);

    rpmTagType ptt;
    int argc;
    const char **hdrArgv = NULL;
    const char **argv = NULL;
    const char *argv1[1];

    hge(pkg->header, progTag, &ptt, (void**)&hdrArgv, &argc);
    if (hdrArgv && ptt == RPM_STRING_TYPE) {
	*argv1 = (const char *) hdrArgv;
	argv = argv1;
    } else if (argv) {
	argv = hdrArgv;
    } else {
	*argv1 = "/bin/sh";
	argv = argv1;
	argc = 1;
    }

    const char *buildroot = NULL;
    urlPath(spec->buildRootURL, &buildroot);
    assert(buildroot);

    const char *N = NULL;
    headerName(pkg->header, &N);
    assert(N);

    const char *path =
	    xasprintf("%s/.%s:%s", buildroot, scriptname, N);
    FILE *fp = fopen(path, "w");
    if (!fp) {
	rpmMessage(RPMMESS_ERROR, _("cannot write %s\n"), path);
	path = _free(path);
	goto done;
    }

    fchmod(fileno(fp), 0755);

    fprintf(fp, "#!%s", argv[0]);
    while (--argc > 0)
	fprintf(fp, " %s", *++argv);
    fprintf(fp, "\n%s\n", script);
    fclose(fp);

done:
    hfd(script, stt);
    hfd(hdrArgv, ptt);

    return path;
}

/**
 */
static int generateDepends(Spec spec, Package pkg, TFI_t cpioList)
	/*@globals rpmGlobalMacroContext,
		fileSystem, internalState @*/
	/*@modifies cpioList, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    TFI_t fi = cpioList;
    StringBuf readBuf;
    DepMsg_t *dm;
    int rc = 0;
    int i;

    if ( *pkg->autoProv )
	addMacro(spec->macros, "_findprov_method", NULL, pkg->autoProv, RMIL_SPEC);

    if ( *pkg->autoReq )
	addMacro(spec->macros, "_findreq_method", NULL, pkg->autoReq, RMIL_SPEC);

    StringBuf fileListBuf = NULL;
    int fileListBytes = 0;
    if (fi && fi->fc > 0) {
	fileListBuf = newStringBuf();
	for (i = 0, fileListBytes = 0; i < fi->fc; i++) {
	    appendStringBuf(fileListBuf, fi->dnl[fi->dil[i]]);
	    fileListBytes += strlen(fi->dnl[fi->dil[i]]);
	    appendLineStringBuf(fileListBuf, fi->bnl[i]);
	    fileListBytes += strlen(fi->bnl[i]) + 1;
	}
    }

    for (dm = depMsgs; dm->msg != NULL; dm++) {
	int tag = (dm->ftag > 0) ? dm->ftag : dm->ntag, tagflags = 0;
	char *runBody = 0;

	/* This will indicate whether we're doing a scriptlet or a filelist */
	const char *instScript = NULL;

	if (!dm->argv)
	    continue;
	if (dm->argv[0]) {
	    /* filelist slot */
	    if (!fileListBuf)
		continue;
	} else if (dm->argv[1]) {
	    /* scriptlet slot */
	    instScript = saveInstScript(spec, pkg, dm->argv[1]);
	    if (!instScript)
		continue;
	} else {
	    /* neither-nor */
	    continue;
	}

	switch(tag) {
	case RPMTAG_PROVIDEFLAGS:
	    if (!*pkg->autoProv)
		continue;
	    tagflags = RPMSENSE_FIND_PROVIDES;
	    /*@switchbreak@*/ break;
	case RPMTAG_CONFLICTFLAGS:
	    tagflags = RPMSENSE_ANY;
	    /*@switchbreak@*/ break;
	case RPMTAG_OBSOLETEFLAGS:
	    tagflags = RPMSENSE_ANY;
	    /*@switchbreak@*/ break;
	case RPMTAG_REQUIREFLAGS:
	    if (!*pkg->autoReq && !instScript)
		continue;
	    tagflags = RPMSENSE_FIND_REQUIRES;
	    if (instScript) /* XXX this just works */
		tagflags |= dm->mask | RPMSENSE_PREREQ;
	    /*@switchbreak@*/ break;
	default:
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	}

	if (instScript)
	    runBody = rpmExpand("%{?__find_scriptlet_requires}", NULL);
	else
	    runBody = rpmExpand( dm->argv[0], NULL );

	if ( !runBody || !*runBody || '%' == *runBody )
	{
		runBody = _free(runBody);
		continue;
	}

	if (!instScript) {
		const char **av;
		for ( av = dm->argv + 1; av[0]; ++av )
		{
			char *p = xasprintf("%s %s", runBody, av[0]);
			_free(runBody);
			runBody = p;
		}
	}

	rpmMessage(RPMMESS_NORMAL, _("Finding %s (using %s)\n"), dm->msg, runBody);

	if (instScript) {
	    /* pass extra argument, the script filename */
	    char *p = xasprintf("%s %s", runBody, instScript);
	    _free(runBody);
	    runBody = p;
	}

	readBuf = runPkgScript(pkg, runBody,
			instScript ? NULL : getStringBuf(fileListBuf),
			instScript ? 0 : fileListBytes);

	if (readBuf == NULL) {
	    rc = RPMERR_EXEC;
	    rpmError(rc, _("Failed to find %s\n"), dm->msg);
	    break;
	}

	/* Parse dependencies into header */
	rc = parseRCPOT(spec, pkg, getStringBuf(readBuf), tag, 0, tagflags);
	readBuf = freeStringBuf(readBuf);

	if (rc) {
	    rpmError(rc, _("Failed to find %s\n"), dm->msg);
	    break;
	}

	if (instScript) {
	    /* unlink(instScript); */
	    instScript = _free(instScript);
	}
    }

    fileListBuf = freeStringBuf(fileListBuf);
    return rc;
}

static int
checkProvides(Spec spec, Package pkg)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    const char **names = NULL;
    rpmTagType dnt;
    int i, len = 0;

    if (!hge(pkg->header, RPMTAG_PROVIDENAME, &dnt, (void **) &names, &len))
	return 0;

    Package p;
    for (p = spec->packages; p; p = p->next) {
	if (p == pkg)
	    continue;
	const char *n = NULL;
	headerName(p->header, &n);
	for (i = 0; i < len; ++i) {
	    if (!strcmp(n, names[i])) {
		headerName(pkg->header, &n);
		rpmMessage(RPMMESS_WARNING,
			   "%s provides another subpackage: %s\n", n, names[i]);
		break;
	    }
	}
    }

    names = hfd(names, dnt);
    return 0;
}

static int makeDebugInfo(Spec spec, Package pkg)
{
    TFI_t fi = pkg->cpioList;
    if (fi == NULL || fi->fc == 0)
	return 0;
    const char *N = NULL, *A = NULL;
    headerNEVRA(pkg->header, &N, NULL, NULL, NULL, &A);
    assert(N && A);
    if (strcmp(A, "noarch") == 0)
	return 0;
    const char *dash = strrchr(N, '-');
    if (dash && strcmp(dash, "-debuginfo") == 0)
	return 0;

    char *cmd = rpmExpand("%{?__find_debuginfo_files}", NULL);
    if (!(cmd && *cmd))
	return 0;

    StringBuf fileList = newStringBuf();
    int i;
    int fileListLen = 0;
    for (i = 0, fileListLen = 0; i < fi->fc; i++) {
	appendStringBuf(fileList, fi->dnl[fi->dil[i]]);
	fileListLen += strlen(fi->dnl[fi->dil[i]]);
	appendLineStringBuf(fileList, fi->bnl[i]);
	fileListLen += strlen(fi->bnl[i]) + 1;
    }

    rpmMessage(RPMMESS_NORMAL, _("Finding debuginfo files (using %s)\n"), cmd);

    int rc = 0;
    StringBuf out = runPkgScript(pkg, cmd, getStringBuf(fileList), fileListLen);
    if (!out) {
	rc = RPMERR_EXEC;
	rpmError(rc, _("Failed to find debuginfo files\n"));
	goto exit;
    }

    if (*getStringBuf(out) == '\0')
	goto exit;

    rpmMessage(RPMMESS_NORMAL, _("Creating %s-debuginfo package\n"), N);

    /* simulate %include */
    const char include_fmt[] =
	"%%package -n %{name}-debuginfo\n"
	"Version: %{version}\n"
	"Release: %{release}\n"
	"%|epoch?{Epoch: %{epoch}\n}|"
	"Summary: %{summary} (debug files)\n"
	"Group: Development/Debug\n"
	"Requires: %{name} = %|epoch?{%{epoch}:}|%{version}-%{release}\n"
	"AutoReqProv: no, debuginfo\n"
	"%%description -n %{name}-debuginfo\n"
	"This package provides debug information for package %{name}.\n"
	"%%files -n %{name}-debuginfo\n";
    const char *include = headerSprintf(pkg->header, include_fmt,
	    rpmTagTable, rpmHeaderFormats, NULL);
    assert(include);

    const char *path =
	    xasprintf("%s/.include:%s-debuginfo", spec->buildRootURL, N);
    FILE *fp = fopen(path, "w");
    assert(fp);
    fputs(include, fp);
    fputs(getStringBuf(out), fp);
    fclose(fp);

    OFI_t *ofi = newOpenFileInfo();
    ofi->fileName = path;
    ofi->next = spec->fileStack;
    spec->fileStack = ofi;

    rc = readLine(spec, 0);
    assert(rc == 0);
    rc = parsePreamble(spec, 0);
    assert(rc == PART_DESCRIPTION);
    rc = parseDescription(spec);
    assert(rc == PART_FILES);
    rc = parseFiles(spec);
    assert(rc == PART_NONE);

    Package dpkg = pkg;
    while (dpkg->next)
	dpkg = dpkg->next;
    int tags[] = { RPMTAG_OS, RPMTAG_ARCH, 0 };
    headerCopyTags(pkg->header, dpkg->header, tags);
    if (pkg->removePostfixes)
	dpkg->removePostfixes = xstrdup(pkg->removePostfixes);

exit:
    freeStringBuf(fileList);
    freeStringBuf(out);
    free(cmd);
    return rc;
}

/**
 */
static void printDepMsg(const DepMsg_t * dm, int count, const char * const * names,
		const char *const * versions, const int *flags)
	/*@*/
{
    int hasVersions = (versions != NULL);
    int hasFlags = (flags != NULL);
    int bingo = 0;
    int i;

    for (i = 0; i < count; i++, names++, versions++, flags++) {
	if (hasFlags && !((*flags & dm->mask) ^ dm->xor))
	    continue;
	/* workaround for legacy PreReq */
	if (dm->mask == RPMSENSE_PREREQ && dm->xor == 0 && !isLegacyPreReq (*flags))
	    continue;
	if (bingo == 0) {
	    rpmMessage(RPMMESS_NORMAL, "%s:", (dm->msg ? dm->msg : ""));
	    bingo = 1;
	}
	/* print comma where appropriate */
	if (bingo > 1)
	    rpmMessage(RPMMESS_NORMAL, ",");
	else
	    bingo = 2;
	rpmMessage(RPMMESS_NORMAL, " %s", *names);

	if (hasVersions && !(*versions != NULL && **versions != '\0'))
	    continue;
	if (!(hasFlags && (*flags && RPMSENSE_SENSEMASK)))
	    continue;

	rpmMessage(RPMMESS_NORMAL, " ");
	if (*flags & RPMSENSE_LESS)
	    rpmMessage(RPMMESS_NORMAL, "<");
	if (*flags & RPMSENSE_GREATER)
	    rpmMessage(RPMMESS_NORMAL, ">");
	if (*flags & RPMSENSE_EQUAL)
	    rpmMessage(RPMMESS_NORMAL, "=");

	rpmMessage(RPMMESS_NORMAL, " %s", *versions);
    }
    if (bingo)
	rpmMessage(RPMMESS_NORMAL, "\n");
}

/**
 */
static void printDeps(Header h)
	/*@*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    const char ** names = NULL;
    rpmTagType dnt = -1;
    const char ** versions = NULL;
    rpmTagType dvt = -1;
    int * flags = NULL;
    DepMsg_t * dm;
    int count, xx;

    for (dm = depMsgs; dm->msg != NULL; dm++) {
	switch (dm->ntag) {
	case 0:
	    names = hfd(names, dnt);
	    /*@switchbreak@*/ break;
	case -1:
	    /*@switchbreak@*/ break;
	default:
	    names = hfd(names, dnt);
	    if (!hge(h, dm->ntag, &dnt, (void **) &names, &count))
		continue;
	    /*@switchbreak@*/ break;
	}
	switch (dm->vtag) {
	case 0:
	    versions = hfd(versions, dvt);
	    /*@switchbreak@*/ break;
	case -1:
	    /*@switchbreak@*/ break;
	default:
	    versions = hfd(versions, dvt);
	    xx = hge(h, dm->vtag, &dvt, (void **) &versions, NULL);
	    /*@switchbreak@*/ break;
	}
	switch (dm->ftag) {
	case 0:
	    flags = NULL;
	    /*@switchbreak@*/ break;
	case -1:
	    /*@switchbreak@*/ break;
	default:
	    xx = hge(h, dm->ftag, NULL, (void **) &flags, NULL);
	    /*@switchbreak@*/ break;
	}
	/*@-noeffect@*/
	printDepMsg(dm, count, names, versions, flags);
	/*@=noeffect@*/
    }
    names = hfd(names, dnt);
    versions = hfd(versions, dvt);
}


/*
 * Rewrite the upper/lower bounds in deps into equivalent ones with a
 * different or absent disttag.
 *
 * (n < E:V-R:D) and (n < E:V-R) are equivalent constraints when interpreted
 * by a disttag-aware rpm. (They "overlap" with the same packages.)
 * Similarly for >.
 *
 * One form or another can be better for compatibility with
 * disttag-unaware tools.
 */
static
int fillInsignificantDisttagInDep(const Package pkg,
                                  const DepMsg_t * const dm,
                                  const int senseMask,
                                  const char * const newDisttag,
                                  const int overwrite)
{
    /* The change or removal of the disttag is an equivalent rewrite if
       the rewritten constraint is a lower/upper bound (after a > or < sign).
    */
    assert ( !(senseMask & ~(RPMSENSE_LESS | RPMSENSE_GREATER)) );

    int rc = RPMRC_OK;

    const char * const *depNv = NULL;
    const char **depVv = NULL;
    const int *depFv = NULL;
    int depc;

    HGE_t hge = (HGE_t) headerGetEntryMinMemory; /* pointers reference header memory */
    HME_t hme = (HME_t) headerModifyEntry;
    HFD_t hfd = (HFD_t) headerFreeData;

    const int ok =
      hge(pkg->header, dm->ntag, NULL, (void **) &depNv, &depc) &&
      hge(pkg->header, dm->vtag, NULL, (void **) &depVv, NULL) &&
      hge(pkg->header, dm->ftag, NULL, (void *) &depFv, NULL);
    if (!ok) {
	return RPMRC_OK;
    }

    for (int i = 0; i < depc; i++) {
        const char *e, *v, *r, *d, *evr;

        /* Skip if there are no bits from senseMask in the flags,
           or if there are other sense bits in the flags (not from senseMask).
        */
	if ( !(depFv[i] & senseMask)
             || ((depFv[i] & RPMSENSE_SENSEMASK) & ~senseMask) )
	    continue;

	/* Skip set dependencies. */
        if (strncmp(depVv[i], "set:", 4) == 0)
            continue;

	parseEVRD(strdupa(depVv[i]), &e, &v, &r, &d);

        /* Skip dependencies with underspecified release.
           We assert that a release must be present if there is a disttag.
         */
        if (!r || !*r)
            continue;

        if (d && *d) {
            if (!overwrite)
                continue;
        } else {
            if (!newDisttag || !*newDisttag)
                continue;
        }

        if (e && *e)
            evr = xasprintf("%s:%s-%s", e, v, r);
        else
            evr = xasprintf("%s-%s", v, r);

        if (!newDisttag || !*newDisttag) {
            rpmMessage(RPMMESS_NORMAL,
                       "Discarding an insignificant disttag from ");
            printDepMsg(dm, 1, &depNv[i], &depVv[i], &depFv[i]);
            // Not freeing old depVv[i] because we have got it from hge().
            depVv[i] = evr;
        } else {
            rpmMessage(RPMMESS_NORMAL,
                       (d && *d)
                       ? "Overwriting the insignificant disttag (with %s) in "
                       : "Adding an insignificant disttag (%s) to ",
                       newDisttag);
            printDepMsg(dm, 1, &depNv[i], &depVv[i], &depFv[i]);
            // Not freeing old depVv[i] because we have got it from hge().
            depVv[i] = xasprintf("%s:%s", evr, newDisttag);
            evr = _free(evr);
        }
        // I suspect that the new allocated value depVv[i] might not be freed.
        // But it would be too much hassle to care about this.
    }

    hme(pkg->header, dm->vtag, RPM_STRING_ARRAY_TYPE, depVv, depc);

 exit:
    depNv = hfd(depNv, RPM_STRING_ARRAY_TYPE);
    depVv = hfd(depVv, RPM_STRING_ARRAY_TYPE);
    // depFv (as an array of int) seems not to need to be freed with hfd().
    // At least, it's not done in timeCheck() or printDeps() in this file.
    return rc;
}


#include "interdep.h"
#include "checkFiles.h"

int processBinaryFiles(Spec spec, int installSpecialDoc, int test)
{
    Package pkg;
    int rc = 0;

    for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
	if (pkg->fileList == NULL)
	    continue;

        {
            const char *n, *v, *r;
            (void) headerNVRD(pkg->header, &n, &v, &r, NULL);
            rpmMessage(RPMMESS_NORMAL, _("Processing files: %s-%s-%s\n"), n, v, r);
        }

	rc = processPackageFiles(spec, pkg, installSpecialDoc, test);
	if (rc) break;

	rc = generateDepends(spec, pkg, pkg->cpioList);
	if (rc) break;

	rc = checkProvides(spec, pkg);
	if (rc) break;

        /* Rewriting "< E:V-R:D" dependencies for compatibility with older
           disttag-unaware tools, so that such a dependency means in a
           disttag-unaware tool the same thing as in the disttag-aware rpm.

           In the disttag-aware rpm tool,
           "x < E:V-R:D" iff "x < E:V-R"
           (i.e., the disttag is not significant).

           For example, "E1:V1-R1:D1 < E:V-R:D" iff "E1:V1-R1 < E:V-R".

           "x < E:V-R" in the disttag-aware rpm tool
           iff
           "x < E:V-R" in a disttag-unaware tool
           (i.e., this condition has the same meaning
           in disttag-aware and -unaware tools).

           For example, in a disttag-unaware tool,
           "E:V-R1:D1 < E:V-R" iff R1:D1<R iff R1<R,
           the same condition as in the disttag-aware rpm tool.

           But in a disttag-unaware tool,
           the conditions "x < E:V-R:D" and "x < E:V-R" mean different things.
           ("R:D" is understood as the release.)

           For example, "E:V-R < E:V-R:D" is true in a disttag-unaware tool,
           although--from the point fo view of the disttag-aware rpm tool--it shouldn't.

           Therefore, for compatibility with older disttag-unaware tools
           we discard the insignificant disttag in such dependencies.
         */

        static const DepMsg_t
            conflicts_depMsg = { "Conflicts", { 0 }, RPMTAG_CONFLICTNAME, RPMTAG_CONFLICTVERSION, RPMTAG_CONFLICTFLAGS, 0, -1 },
            obsoletes_depMsg = { "Obsoletes", { 0 }, RPMTAG_OBSOLETENAME, RPMTAG_OBSOLETEVERSION, RPMTAG_OBSOLETEFLAGS, 0, -1 },
            requires_depMsg = { "Requires(..)", { 0 }, RPMTAG_REQUIRENAME, RPMTAG_REQUIREVERSION, RPMTAG_REQUIREFLAGS, 0, -1 };

	rc = fillInsignificantDisttagInDep(pkg,
                                           &conflicts_depMsg,
                                           RPMSENSE_LESS,
                                           NULL,
                                           1 /* overwrite */);
	if (rc) break;
	rc = fillInsignificantDisttagInDep(pkg,
                                           &obsoletes_depMsg,
                                           RPMSENSE_LESS,
                                           NULL,
                                           1 /* overwrite */);
	if (rc) break;
        /* Not touching unrealistic Requires: x < E:V-R[:D].
           No one writes like this; there are no such packages in Sisyphus.
        */

        /* Rewriting "> E:V-R[:D]" dependencies for compatibility with older
           disttag-unaware tools, so that such a dependency means in a
           disttag-unaware tool ALMOST the same thing as in the disttag-aware rpm.

           In the disttag-aware rpm tool,
           "x > E:V-R:D" iff "x > E:V-R"
           (i.e., the disttag is not significant).

           For example, "E1:V1-R1:D1 > E:V-R:D" iff "E1:V1-R1 > E:V-R".

           But in a disttag-unaware tool,
           "E1:V1-R1:D1 > E:V-R" would be true also if "E1:V1-R1 = E:V-R" and D1 is non-empty;
           "E1:V1-R1:D1 > E:V-R:D" would be true also if "E1:V1-R1 = E:V-R" and D1>D.

           To make this impossible, we would like to rewrite "x > E:V-R[:D]"
           in the form "x > E:V-R:MAX",
           where MAX>=D1 for any other possible disttag D1.

           In a disttag-unaware tool,
           "E1:V1-R1:D1 > E:V-R:MAX" iff "E1:V1-R1 > E:V-R";
           this is also true in the disttag-aware rpm tool.
           So, the meaning of "x > E:V-R:MAX" would be the same
           both in the disttag-aware rpm tool and in a disttag-unaware tool,
           and--from the point fo view of the disttag-aware rpm tool--it is
           equivalent to "x > E:V-R[:D]" with any D.

           Therefore, for compatibility with older disttag-unaware tools,
           we rewrite such dependencies; as an approximation to MAX,
           we take "z", and fill the insignificant disttag with it.
         */
        static const char * const maximal_disttag = "z";
	rc = fillInsignificantDisttagInDep(pkg,
                                           &conflicts_depMsg,
                                           RPMSENSE_GREATER,
                                           maximal_disttag,
                                           1 /* overwrite */);
	if (rc) break;
        /* Not touching unrealistic Obsoletes: x > E:V-R[:D].
           No one writes like this; there are no such packages in Sisyphus.
        */
	rc = fillInsignificantDisttagInDep(pkg,
                                           &requires_depMsg,
                                           RPMSENSE_GREATER,
                                           maximal_disttag,
                                           0 /* don't overwrite */);
        /* Not rewriting unrealistic Requires: x > E:V-R:D (with a disttag).
           Normally, no one would write like this, because this makes no sense,
           and because one wouldn't know a valid disttag of another package.
        */
	if (rc) break;

	/*@-noeffect@*/
	printDeps(pkg->header);
	/*@=noeffect@*/

	rc = makeDebugInfo(spec, pkg);
	if (rc) break;
    }

    if (rc == 0)
	rc = processInterdep(spec);

    if (rc == 0)
	rc = checkFiles(spec);

    if (rc)
	return rc;

    for (pkg = spec->packages; pkg != NULL; pkg = pkg->next) {
	if (pkg->fileList == NULL)
	    continue;
	rc = finalizePkg(pkg);
	if (rc) break;
    }

    return rc;
}
