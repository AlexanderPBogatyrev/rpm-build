#ifndef H_DEPENDS
#define H_DEPENDS

/** \ingroup rpmdep rpmtrans
 * \file lib/depends.h
 * Structures used for dependency checking.
 */

#include "header.h"

typedef /*@abstract@*/ struct transactionElement_s *	transactionElement;
typedef /*@abstract@*/ struct tsortInfo_s *		tsortInfo;

/** \ingroup rpmdep
 * Dependncy ordering information.
 */
struct tsortInfo_s {
    union {
	int	count;
	/*@kept@*/ /*@null@*/ struct availablePackage * suc;
    } tsi_u;
#define	tsi_count	tsi_u.count
#define	tsi_suc		tsi_u.suc
/*@owned@*/ /*@null@*/
    tsortInfo tsi_next;
/*@kept@*/ /*@null@*/
    struct availablePackage * tsi_pkg;
    int		tsi_reqx;
    int		tsi_qcnt;
} ;

/** \ingroup rpmdep
 * Info about a single package to be installed.
 */
struct availablePackage {
    Header h;				/*!< Package header. */
/*@dependent@*/ const char * name;	/*!< Header name. */
/*@dependent@*/ const char * version;	/*!< Header version. */
/*@dependent@*/ const char * release;	/*!< Header release. */
/*@owned@*/ const char ** provides;	/*!< Provides: name strings. */
/*@owned@*/ const char ** providesEVR;	/*!< Provides: [epoch:]version[-release] strings. */
/*@dependent@*/ int * provideFlags;	/*!< Provides: logical range qualifiers. */
/*@owned@*//*@null@*/ const char ** requires;	/*!< Requires: name strings. */
/*@owned@*//*@null@*/ const char ** requiresEVR;/*!< Requires: [epoch:]version[-release] strings. */
/*@dependent@*//*@null@*/ int * requireFlags;	/*!< Requires: logical range qualifiers. */
/*@dependent@*//*@null@*/ int_32 * epoch;	/*!< Header epoch (if any). */
    int providesCount;			/*!< No. of Provide:'s in header. */
    int requiresCount;			/*!< No. of Require:'s in header. */
    int filesCount;			/*!< No. of files in header. */

    struct availablePackage *  parent;	/*!< Parent package. */
    int degree;				/*!< No. of immediate children. */
    int depth;				/*!< Max. depth in dependency tree. */
    int npreds;				/*!< No. of predecessors. */
    int tree;				/*!< Tree index. */
    struct tsortInfo_s tsi;		/*!< Dependency tsort data. */

/*@kept@*//*@null@*/ const void * key;	/*!< Private data associated with a package (e.g. file name of package). */
/*@null@*/ rpmRelocation * relocs;
/*@null@*/ FD_t fd;

    int_32 * buildtime;
/*@dependent@*/ const char * disttag;	/*!< Header disttag (if any). */
} ;

/** \ingroup rpmdep
 * Set of available packages, items, and directories.
 */
typedef /*@abstract@*/ struct availableList_s {
/*@owned@*/ /*@null@*/ struct availablePackage * list;	/*!< Set of packages. */
    int size;				/*!< No. of pkgs in list. */
    struct alDirIndex *dirIndex;	/*!< Files index. */
    struct alProvIndex *provIndex;	/*!< Provides index. */
} * availableList;

/** \ingroup rpmdep
 * A single package instance to be installed/removed atomically.
 */
struct transactionElement_s {
    enum rpmTransactionType {
	TR_ADDED,	/*!< Package will be installed. */
	TR_REMOVED	/*!< Package will be removed. */
    } type;		/*!< Package disposition (installed/removed). */
    union { 
/*@unused@*/ int addedIndex;
/*@unused@*/ struct {
	    int dboffset;
	    int dependsOnIndex;
	    int erasedIndex;
	} removed;
    } u;
} ;

/** \ingroup rpmdep
 * The set of packages to be installed/removed atomically.
 */
struct rpmTransactionSet_s {
    rpmtransFlags transFlags;		/*!< Bit(s) to control operation. */
/*@null@*/ rpmCallbackFunction notify;	/*!< Callback function. */
/*@observer@*/ /*@null@*/ rpmCallbackData notifyData;
					/*!< Callback private data. */
/*@dependent@*/ rpmProblemSet probs;	/*!< Current problems in transaction. */
    rpmprobFilterFlags ignoreSet;	/*!< Bits to filter current problems. */
    int filesystemCount;		/*!< No. of mounted filesystems. */
/*@dependent@*/ const char ** filesystems; /*!< Mounted filesystem names. */
/*@only@*/ struct diskspaceInfo * di;	/*!< Per filesystem disk/inode usage. */
/*@kept@*/ /*@null@*/ rpmdb rpmdb;	/*!< Database handle. */
/*@only@*/ int * removedPackages;	/*!< Set of packages being removed. */
    int numRemovedPackages;		/*!< No. removed rpmdb instances. */
    struct availableList_s erasedPackages;
				/*!< Set of packages being removed. */
    struct availableList_s addedPackages;
				/*!< Set of packages being installed. */
/*@only@*/ transactionElement order;
				/*!< Packages sorted by dependencies. */
    int orderCount;		/*!< No. of transaction elements. */
/*@only@*/ TFI_t flList;	/*!< Transaction element(s) file info. */
    int flEntries;		/*!< No. of transaction elements. */
    int chrootDone;		/*!< Has chroot(2) been been done? */
/*@only@*/ const char * rootDir;/*!< Path to top of install tree. */
/*@only@*/ const char * currDir;/*!< Current working directory. */
/*@null@*/ FD_t scriptFd;	/*!< Scriptlet stdout/stderr. */
    int id;			/*!< Transaction id. */
    int selinuxEnabled;		/*!< Is SE linux enabled? */
} ;

/** \ingroup rpmdep
 * Problems encountered while checking dependencies.
 */
typedef /*@abstract@*/ struct problemsSet_s {
    rpmDependencyConflict problems;	/*!< Problems encountered. */
    int num;			/*!< No. of problems found. */
} * problemsSet;

#ifdef __cplusplus
extern "C" {
#endif

/** \ingroup rpmdep
 * Compare package name-version-release from header with dependency, looking
 * for overlap.
 * @deprecated Remove from API when obsoletes is correctly eliminated.
 * @param h		header
 * @param reqName	dependency name
 * @param reqEVR	dependency [epoch:]version[-release]
 * @param reqFlags	dependency logical range qualifiers
 * @return		1 if dependency overlaps, 0 otherwise
 */
int headerMatchesDepFlags(Header h,
	const char * reqName, const char * reqEVR, int reqFlags)
		/*@*/;

/**
 * Return formatted dependency string.
 * @param depend	type of dependency ("R" == Requires, "C" == Conflcts)
 * @param key		dependency name string
 * @param keyEVR	dependency [epoch:]version[-release] string
 * @param keyFlags	dependency logical range qualifiers
 * @return		formatted dependency (malloc'ed)
 */
/*@only@*/ char * printDepend(const char * depend, const char * key,
		const char * keyEVR, int keyFlags)
	/*@*/;

#ifdef __cplusplus
}
#endif

#endif	/* H_DEPENDS */
