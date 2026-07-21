#ifndef RPM_AL_H
#define RPM_AL_H

/**
 * Initialize available packckages, items, and directories list.
 * @param al		available list
 */
static inline
void alCreate(availableList al)
	/*@modifies al @*/
{
    al->list = NULL;
    al->size = 0;
    al->dirIndex = NULL;
    al->provIndex = NULL;
}

/**
 * Free available packages, items, and directories members.
 * @param al		available list
 */
void alFree(availableList al)
	/*@modifies al @*/;

/**
 * Add package to available list.
 * @param al		available list
 * @param h		package header
 * @param key		package private data
 * @param fd		package file handle
 * @param relocs	package file relocations
 * @return		available package pointer
 */
/*@exposed@*/ struct availablePackage *
alAddPackage(availableList al,
		Header h, /*@null@*/ /*@dependent@*/ const void * key,
		/*@null@*/ FD_t fd, /*@null@*/ rpmRelocation * relocs)
	/*@modifies al, h @*/;

/**
 * Check added package file lists for package(s) that have a provide.
 * @param al		available list
 * @param keyName	dependency name string
 * @param keyEVR	dependency [epoch:]version[-release] string
 * @param keyFlags	dependency logical range qualifiers
 * @return		available package pointer
 */
/*@only@*/ /*@null@*/ struct availablePackage **
alAllSatisfiesDepend(const availableList al,
		const char * keyName, const char * keyEVR, int keyFlags)
	/*@*/;

/**
 * Check added package file lists for first package that has a provide.
 * @todo Eliminate.
 * @param al		available list
 * @param keyName	dependency name string
 * @param keyEVR	dependency [epoch:]version[-release] string
 * @param keyFlags	dependency logical range qualifiers
 * @return		available package pointer
 */
static inline /*@only@*/ /*@null@*/ struct availablePackage *
alSatisfiesDepend(const availableList al,
		const char * keyName, const char * keyEVR, int keyFlags)
	/*@*/
{
    struct availablePackage * ret;
    struct availablePackage ** tmp =
	alAllSatisfiesDepend(al, keyName, keyEVR, keyFlags);

    if (tmp) {
	ret = tmp[0];
	tmp = _free(tmp);
	return ret;
    }
    return NULL;
}

#endif
