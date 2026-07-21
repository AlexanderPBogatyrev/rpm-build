/**
 * \file rpmdb/rpmhash.c
 * Hash table implemenation
 */

#include "system.h"
#include "rpmlib.h"
#include "rpmhash.h"
#include "debug.h"

#include "jhash.h"

typedef	struct hashBucket_s * hashBucket;

/**
 */
struct hashBucket_s {
    hashBucket next;			/*!< pointer to next item in bucket */
    const void *key;			/*!< hash key */
    unsigned int dataCount;		/*!< data entries */
    const void *data[1];		/*!< data - grows by resizing whole bucket */
};

/**
 */
struct hashTable_s {
    hashFunctionType fn;		/*!< generate hash value for key */
    hashEqualityType eq;		/*!< compare hash keys for equality */
    unsigned int numBuckets;		/*!< number of hash buckets */
    hashBucket buckets[1];		/*!< hash bucket array */
};

/**
 * Find entry in hash table.
 * @param ht            pointer to hash table
 * @param key           pointer to key value
 * @return pointer to hash bucket of key (or NULL)
 */
static inline /*@shared@*/ /*@null@*/
hashBucket findBucket(hashTable ht, const void * key)
	/*@*/
{
    unsigned int hash = ht->fn(key) & (ht->numBuckets - 1);
    hashBucket b = ht->buckets[hash];

    while (b && b->key && ht->eq(b->key, key))
	b = b->next;

    return b;
}

int hashEqualityString(const void * key1, const void * key2)
{
    return strcmp(key1, key2);
}

unsigned int hashFunctionString(const void *str)
{
    return jhashString(str);
}

hashTable htCreate(unsigned int size, hashFunctionType fn, hashEqualityType eq)
{
    hashTable ht;
    unsigned int numBuckets = jhashSize(size);
    size_t numBytes = sizeof(*ht) + sizeof(ht->buckets[1]) * (numBuckets - 1);
    ht = xcalloc(numBytes, 1);
    ht->numBuckets = numBuckets;
    /*@-assignexpose@*/
    ht->fn = fn;
    ht->eq = eq;
    /*@=assignexpose@*/
    return ht;
}

void htAddEntry(hashTable ht, const void * key, const void * data)
{
    unsigned int hash = ht->fn(key) & (ht->numBuckets - 1);
    hashBucket b = ht->buckets[hash];
    hashBucket *b_addr = ht->buckets + hash;

    while (b && b->key && ht->eq(b->key, key)) {
	b_addr = &(b->next);
	b = b->next;
    }

    if (b == NULL) {
	b = xmalloc(sizeof(*b));
	b->key = key;
	b->dataCount = 1;
	b->data[0] = data;
	b->next = ht->buckets[hash];
	ht->buckets[hash] = b;
    }
    else {
	// Bucket_s already contains space for one dataset
	b = *b_addr = xrealloc(b, sizeof(*b) + sizeof(b->data[0]) * b->dataCount);
	// though increasing dataCount after the resize
	b->data[b->dataCount++] = data;
    }
}

hashTable htFree(hashTable ht, hashFreeKeyType freeKey, hashFreeDataType freeData)
{
    hashBucket b, n;
    unsigned int i, j;

    for (i = 0; i < ht->numBuckets; i++) {
	b = ht->buckets[i];
	if (b == NULL)
	    continue;
	ht->buckets[i] = NULL;
	do {
	    n = b->next;
	    if (freeKey)
		b->key = freeKey(b->key);
	    if (freeData)
		for (j = 0; j < b->dataCount; j++)
		    b->data[j] = freeData(b->data[j]);
	    b = _free(b);
	} while ((b = n) != NULL);
    }

    ht = _free(ht);
    return NULL;
}

int htHasEntry(hashTable ht, const void * key)
{
    hashBucket b = findBucket(ht, key);
    if (b == NULL)
	return 0;
    return 1;
}

int htGetEntry(hashTable ht, const void * key, const void *** data,
	       int * dataCount, const void ** tableKey)
{
    hashBucket b = findBucket(ht, key);
    if (b == NULL)
	return 1;
    if (data)
	*data = b->data;
    if (dataCount)
	*dataCount = b->dataCount;
    if (tableKey)
	*tableKey = b->key;
    return 0;
}
