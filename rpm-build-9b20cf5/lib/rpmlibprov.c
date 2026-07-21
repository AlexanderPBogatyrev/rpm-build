/** \ingroup rpmdep
 * \file lib/rpmlibprov.c
 */

#include "system.h"

#include "rpmlib.h"
#include "debug.h"

static struct rpmlibProvides_s {
/*@observer@*/ /*@null@*/ const char * featureName;
/*@observer@*/ /*@null@*/ const char * featureEVR;
    int featureFlags;
/*@observer@*/ /*@null@*/ const char * featureDescription;
} rpmlibProvides[] = {
    { "rpmlib(VersionedDependencies)",	"3.0.3-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("PreReq:, Provides:, and Obsoletes: dependencies support versions.") },
    { "rpmlib(CompressedFileNames)",	"3.0.4-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("file name(s) are stored as (dirName,baseName,dirIndex) tuple, not as path.")},
#ifdef HAVE_BZLIB_H
    { "rpmlib(PayloadIsBzip2)",		"3.0.5-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("package payload is compressed using bzip2.") },
#endif
    { "rpmlib(PayloadIsLzma)",         "4.4.2-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("package payload can be compressed using lzma.") },
    { "rpmlib(PayloadIsXz)",		"5.2-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("package payload can be compressed using xz.") },
    { "rpmlib(PayloadFilesHavePrefix)",	"4.0-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("package payload file(s) have \"./\" prefix.") },
    { "rpmlib(ExplicitPackageProvide)",	"4.0-1",
	(RPMSENSE_RPMLIB|RPMSENSE_EQUAL),
    N_("package name-version-release is not implicitly provided.") },
    { "rpmlib(HeaderLoadSortsTags)",    "4.0.1-1",
	(                RPMSENSE_EQUAL),
    N_("header tags are always sorted after being loaded.") },
    { "rpmlib(ScriptletInterpreterArgs)",    "4.0.3-1",
	(                RPMSENSE_EQUAL),
    N_("the scriptlet interpreter can use arguments from header.") },
    { "rpmlib(PartialHardlinkSets)",    "4.0.4-1",
	(                RPMSENSE_EQUAL),
    N_("a hardlink file set may be installed without being complete.") },
    { "rpmlib(PosttransFiletriggers)",  "4.0.4",
	(                RPMSENSE_EQUAL),
    N_("package installs post-transaction filetrigger.") },
    { "rpmlib(SetVersions)",  "4.0.4-alt98",
	(                RPMSENSE_EQUAL),
    N_("dependencies support set/subset versions.") },
    { "rpmlib(TildeInVersions)",    "4.10.0-1",
	(                RPMSENSE_EQUAL),
    N_("dependency comparison supports versions with tilde.") },
#ifdef WITH_CAP
    { "rpmlib(FileCaps)",               "4.6.1-1",
        (                RPMSENSE_EQUAL),
    N_("support for POSIX.1e file capabilities") },
#endif
    { NULL,				NULL, 0,	NULL }
};

void rpmShowRpmlibProvides(FILE * fp)
{
    const struct rpmlibProvides_s * rlp;

    for (rlp = rpmlibProvides; rlp->featureName != NULL; rlp++) {
	fprintf(fp, "    %s", rlp->featureName);
	if (rlp->featureEVR && rlp->featureFlags)
	    printDepFlags(fp, rlp->featureEVR, rlp->featureFlags);
	fprintf(fp, "\n");
	if (rlp->featureDescription)
	    fprintf(fp, "\t%s\n", rlp->featureDescription);
    }
}

int rpmCheckRpmlibProvides(const char * keyName, const char * keyEVR,
	int keyFlags)
{
    const struct rpmlibProvides_s * rlp;
    int rc = 0;

    for (rlp = rpmlibProvides; rlp->featureName != NULL; rlp++) {
	if (rlp->featureEVR && rlp->featureFlags)
	    rc = rpmRangesOverlap(keyName, keyEVR, keyFlags,
		rlp->featureName, rlp->featureEVR, rlp->featureFlags);
	if (rc)
	    break;
    }
    return rc;
}

int rpmGetRpmlibProvides(const char *** provNames, int ** provFlags,
                         const char *** provVersions)
{
    const char ** names, ** versions;
    int * flags;
    int n = 0;
    
    while (rpmlibProvides[n].featureName != NULL)
        n++;

    names = xcalloc((n+1), sizeof(*names));
    versions = xcalloc((n+1), sizeof(*versions));
    flags = xcalloc((n+1), sizeof(*flags));
    
    for (n = 0; rpmlibProvides[n].featureName != NULL; n++) {
        names[n] = rpmlibProvides[n].featureName;
        flags[n] = rpmlibProvides[n].featureFlags;
        versions[n] = rpmlibProvides[n].featureEVR;
    }
    
    /*@-branchstate@*/
    if (provNames)
	*provNames = names;
    else
	names = _free(names);
    /*@=branchstate@*/

    /*@-branchstate@*/
    if (provFlags)
	*provFlags = flags;
    else
	flags = _free(flags);
    /*@=branchstate@*/

    /*@-branchstate@*/
    if (provVersions)
	*provVersions = versions;
    else
	versions = _free(versions);
    /*@=branchstate@*/

    /*@-compmempass@*/ /* FIX: rpmlibProvides[] reachable */
    return n;
    /*@=compmempass@*/
}
