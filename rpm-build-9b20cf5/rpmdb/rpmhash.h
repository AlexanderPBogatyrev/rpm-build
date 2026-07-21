#ifndef H_RPMHASH
#define H_RPMHASH

/**
 * \file rpmdb/rpmhash.h
 * Hash table implemenation.
 */

/**
 */
typedef /*@abstract@*/ struct hashTable_s * hashTable;

#ifdef __cplusplus
extern "C" {
#endif

/**
 */
typedef unsigned int (*hashFunctionType) (const void * string)
	/*@*/;

/**
 */
typedef int (*hashEqualityType) (const void * key1, const void * key2)
	/*@*/;

/**
 * Return hash value of a string
 * @param string	string on which to calculate hash value
 * @return		hash value
 */
unsigned int hashFunctionString(const void * string)
	/*@*/;

/**
 * Compare two hash table entries for equality.
 * @param key1          entry 1
 * @param key2          entry 2
 * @return		0 if entries are equal
 */
int hashEqualityString(const void * key1, const void * key2)
	/*@*/;

typedef void *(*hashFreeKeyType) (const void *key);
typedef void *(*hashFreeDataType) (const void *data);

/**
 * Create hash table.
 * @param size		number of elements expected
 * @param fn            function to generate hash value for key
 * @param eq            function to compare hash keys for equality
 * @return		pointer to initialized hash table
 */
hashTable htCreate(unsigned int size, hashFunctionType fn, hashEqualityType eq)
	/*@*/; 

/**
 * Destroy hash table.
 * @param ht            pointer to hash table
 * @return		NULL always
 */
/*@null@*/
hashTable htFree(hashTable ht, hashFreeKeyType freeKey, hashFreeDataType freeData)
	/*@modifies ht @*/;

/**
 * Add item to hash table.
 * @param ht            pointer to hash table
 * @param key           pointer to key
 * @param data          pointer to data value
 */
void htAddEntry(hashTable ht, /*@owned@*/ const void * key,
		/*@owned@*/ const void * data)
	/*@modifies ht */;

/**
 * Retrieve item from hash table.
 * @param ht            pointer to hash table
 * @param key           pointer to key value
 * @retval data         address to store data value from bucket
 * @retval dataCount    address to store data value size from bucket
 * @retval tableKey     address to store key value from bucket (may be NULL)
 * @return		0 on success, 1 if the item is not found.
 */
int htGetEntry(hashTable ht, const void * key,
		/*@null@*/ /*@out@*/ const void *** data,
		/*@null@*/ /*@out@*/ int * dataCount,
		/*@null@*/ /*@out@*/ const void ** tableKey)
	/*@modifies *data, *dataCount, *tableKey @*/;

/**
 * Check for key in hash table.
 * @param ht            pointer to hash table
 * @param key           pointer to key value
 * @return		1 if the key is present, 0 otherwise
 */
/*@unused@*/
int htHasEntry(hashTable ht, const void * key)
	/*@*/;

#ifdef __cplusplus
}
#endif

#endif
