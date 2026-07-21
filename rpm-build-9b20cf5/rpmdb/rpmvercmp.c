/** \ingroup rpmtrans
 * \file lib/rpmvercmp.c
 */

#include "system.h"

#include "rpmlib.h"

#include "debug.h"

/* compare alpha and numeric segments of two versions */
/* return 1: a is newer than b */
/*        0: a and b are the same version */
/*       -1: b is newer than a */
int rpmvercmp(const char * a, const char * b)
{
    char oldch1, oldch2;
    char * str1, * str2;
    char * one, * two;
    int rc;
    int isnum;

    /* easy comparison to see if versions are identical */
    if (!strcmp(a, b)) return 0;

    str1 = alloca(strlen(a) + 1);
    str2 = alloca(strlen(b) + 1);

    strcpy(str1, a);
    strcpy(str2, b);

    one = str1;
    two = str2;

    /* loop through each version segment of str1 and str2 and compare them */
    while (*one || *two) {
	while (*one && !xisalnum(*one) && *one != '~') one++;
	while (*two && !xisalnum(*two) && *two != '~') two++;

	/* handle the tilde separator, it sorts before everything else */
	if (*one == '~' || *two == '~') {
	    if (*one != '~') return 1;
	    if (*two != '~') return -1;
	    one++;
	    two++;
	    continue;
	}

	if ( !*one && !*two )
		return 0;

	str1 = one;
	str2 = two;

	/* grab first completely alpha or completely numeric segment */
	/* leave one and two pointing to the start of the alpha or numeric */
	/* segment and walk str1 and str2 to end of segment */
	/* Also take care of the case where the two version segments are */
	/* different types: one numeric and one alpha */
	if (xisdigit(*str1)) {
	    if ( xisalpha(*str2) ) return -1;
	    while (*str1 && xisdigit(*str1)) str1++;
	    while (*str2 && xisdigit(*str2)) str2++;
	    isnum = 1;
	} else {
	    while (*str1 && xisalpha(*str1)) str1++;
	    while (*str2 && xisalpha(*str2)) str2++;
	    isnum = 0;
	}

	/* Again, take care of the case where the two version segments are */
	/* different types: one numeric and one alpha */
	if (one == str1) return -1;
	if (two == str2) return 1;

	/* save character at the end of the alpha or numeric segment */
	/* so that they can be restored after the comparison */
	oldch1 = *str1;
	*str1 = '\0';
	oldch2 = *str2;
	*str2 = '\0';

	if (isnum) {
	    /* this used to be done by converting the digit segments */
	    /* to ints using atoi() - it's changed because long  */
	    /* digit segments can overflow an int - this should fix that. */

	    /* throw away any leading zeros - it's a number, right? */
	    while (*one == '0') one++;
	    while (*two == '0') two++;

	    /* whichever number has more digits wins */
	    if (strlen(one) > strlen(two)) return 1;
	    if (strlen(two) > strlen(one)) return -1;
	}

	/* strcmp will return which one is greater - even if the two */
	/* segments are alpha or if they are numeric.  don't return  */
	/* if they are equal because there might be more segments to */
	/* compare */
	rc = strcmp(one, two);
	if (rc) return (rc < 1 ? -1 : 1);

	/* restore character that was replaced by null above */
	*str1 = oldch1;
	one = str1;
	*str2 = oldch2;
	two = str2;
    }

    /* this catches the case where all numeric and alpha segments have */
    /* compared identically but the segment sepparating characters were */
    /* different */
    if ((!*one) && (!*two)) return 0;

    /* whichever version still has characters left over wins */
    if (!*one) return -1; else return 1;
}

/* Moved from depends.c, because we use it in other places, too. */
/**
 * Split EVRD into epoch, version, release and disttag components.
 * @param evrd		[epoch:]version[-release[:disttag]] string
 * @retval *ep		pointer to epoch
 * @retval *vp		pointer to version
 * @retval *rp		pointer to release
 * @retval *dp		pointer to disttag
 */
void parseEVRD(char * evrd,
		const char ** ep,
		const char ** vp,
		const char ** rp,
		const char ** dp)
{
    const char *epoch = NULL;
    const char *version = NULL;		/* assume only version is present */
    const char *release = NULL;
    const char *disttag = NULL;
    char *s, *se;

    s = evrd;
    while (*s && xisdigit(*s)) s++;	/* s points to epoch terminator */
    se = strrchr(s, '-');		/* se points to version terminator */

    if (*s == ':') {
	epoch = evrd;
	*s++ = '\0';
	version = s;
	if (*epoch == '\0') epoch = "0";
    } else {
	version = evrd;
    }
    if (se) {
	*se++ = '\0';
	release = se;
	se = strchr(se, ':');
	if (se) {
	    *se++ = '\0';
	    disttag = se;
	}
    }

    if (ep) *ep = epoch;
    if (vp) *vp = version;
    if (rp) *rp = release;
    if (dp) *dp = disttag;
}

/* compat function */
void parseEVR(char *evr,
	      const char ** ep,
	      const char ** vp,
	      const char ** rp)
{
    parseEVRD(evr, ep, vp, rp, NULL);
}

/* Compare {A,B} [epoch:]version[-release] */
int 
rpmEVRcmp(const char * const aE, const char * const aV, const char * const aR,
	  const char * const aDepend,
	  const char * const bE, const char * const bV, const char * const bR,
	  const char * const bDepend)
{
    int sense = 0;

    rpmMessage(RPMMESS_DEBUG, "cmp e=%s, v=%s, r=%s\n and e=%s, v=%s, r=%s\n ",
	       aE, aV, aR, bE, bV, bR);


    if (aE && *aE && bE && *bE)
	sense = rpmvercmp(aE, bE);
    else if (aE && *aE && atol(aE) > 0) {
	/* XXX legacy epoch-less requires/conflicts compatibility */
	rpmMessage(RPMMESS_DEBUG, _("the \"B\" dependency needs an epoch (assuming same as \"A\")\n\tA %s\tB %s\n"),
		aDepend, bDepend);
	sense = 0;
    } else if (bE && *bE && atol(bE) > 0)
	sense = -1;

    if (sense == 0) {
	sense = rpmvercmp(aV, bV);
	if (sense == 0) {
            if (aR && *aR && bR && *bR)
                sense = rpmvercmp(aR, bR);
            else if (aR && *aR) {
                /* Support for underspecification on the side of Requires/Conflicts */
                rpmMessage(RPMMESS_DEBUG, _("the \"B\" dependency doesn't specify a release, letting it match any in \"A\"\n\tA %s\tB %s\n"),
                           aDepend, bDepend);
                sense = 0;
            } else if (bR && *bR)
                sense = -1;
	}
    }

    return sense;
}

int isChangeNameMoreFresh(const char * const head, 
			  const char * const tail[3]) 
{
  int result;
  const char * evr[3];
  const char * wordAfterEmail;
  char * copy;

  rpmMessage(RPMMESS_DEBUG, "test: is '%s' more fresh than e=%s, v=%s, r=%s?\n",
	     head, tail[0], tail[1], tail[2]);
  /* find the next to <email> word begin */
  if ( (wordAfterEmail = strrchr(head, '>')) )
    ++wordAfterEmail;
  else
    wordAfterEmail = head;
  while ( *wordAfterEmail && xisspace(*wordAfterEmail) )
    ++wordAfterEmail; 
  /* found. */
  copy = xstrdup(wordAfterEmail);
  parseEVR(copy, &evr[0],  &evr[1],  &evr[2]);
  /* The order of two argument groups is important: 
     if evr[] (passed as B on the second place) has no epoch, 
     rpmEVRcmp() assumes the same as in tail[];
     This fits our needs: the epoch may be omitted in a changelog entry (evr[])
     but there are no problems in specifying it in the format (tail[]). */
  result = rpmEVRcmp(tail[0], tail[1], tail[2], "",
		     evr[0], evr[1], evr[2], "") < 0;
  _free(copy);
  return result;
}

