/** \ingroup rpmdb
 * \file rpmdb/hdrNVR.c
 */

#include "system.h"
#include "rpmlib.h"
#include "debug.h"

int headerNVRD(Header h, const char **np, const char **vp, const char **rp,
	       const char **dp)
{
    int type;
    int count;

/*@-boundswrite@*/
    if (np) {
	if (!(headerGetEntry(h, RPMTAG_NAME, &type, (void **) np, &count)
	    && type == RPM_STRING_TYPE && count == 1))
		*np = NULL;
    }
    if (vp) {
	if (!(headerGetEntry(h, RPMTAG_VERSION, &type, (void **) vp, &count)
	    && type == RPM_STRING_TYPE && count == 1))
		*vp = NULL;
    }
    if (rp) {
	if (!(headerGetEntry(h, RPMTAG_RELEASE, &type, (void **) rp, &count)
	    && type == RPM_STRING_TYPE && count == 1))
		*rp = NULL;
    }
    if (dp) {
	if (!(headerGetEntry(h, RPMTAG_DISTTAG, &type, (void **) dp, &count)
	      && type == RPM_STRING_TYPE && count == 1))
		*dp = NULL;
    }
/*@=boundswrite@*/
    return 0;
}

int headerNVR(Header h, const char **np, const char **vp, const char **rp)
{
    return headerNVRD(h, np, vp, rp, NULL);
}

int headerNEVRA(Header h, const char **np,
		/*@unused@*/ const char **ep, const char **vp, const char **rp,
		const char **ap)
{
    int type;
    int count;

/*@-boundswrite@*/
    if (np) {
	if (!(headerGetEntry(h, RPMTAG_NAME, &type, (void **) np, &count)
	    && type == RPM_STRING_TYPE && count == 1))
		*np = NULL;
    }
    if (vp) {
	if (!(headerGetEntry(h, RPMTAG_VERSION, &type, (void **) vp, &count)
	    && type == RPM_STRING_TYPE && count == 1))
		*vp = NULL;
    }
    if (rp) {
	if (!(headerGetEntry(h, RPMTAG_RELEASE, &type, (void **) rp, &count)
	    && type == RPM_STRING_TYPE && count == 1))
		*rp = NULL;
    }
    if (ap) {
	if (!(headerGetEntry(h, RPMTAG_ARCH, &type, (void **) ap, &count)
	    && type == RPM_STRING_TYPE && count == 1))
		*ap = NULL;
    }
/*@=boundswrite@*/
    return 0;
}
