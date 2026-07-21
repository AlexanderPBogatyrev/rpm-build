#ifndef JHASH_H_
#define JHASH_H_

/*
 * Jenkins One-at-a-time hash.
 * http://burtleburtle.net/bob/hash/doobs.html
 * Also used in perl, see PERL_HASH in hv.h.
 */

static inline
unsigned int jhashStringAppend(const char *str, unsigned int hash)
{
    const char *p = str;
    while (*p) {
	hash += *p++;
	hash += (hash << 10);
	hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

static inline
unsigned int jhashDataAppend(const void *data, unsigned int len,
	unsigned int hash)
{
    const char *p = data;
    while (len--) {
	hash += *p++;
	hash += (hash << 10);
	hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

static inline
unsigned int jhashInit(void)
{
    /* The golden ratio really is an arbitrary value.
     * Its purpose is to avoid mapping all zeros to all zeros. */
    return 0x9e3779b9;
}

static inline
unsigned int jhashString(const char *str)
{
    return jhashStringAppend(str, jhashInit());
}

static inline
unsigned int jhashData(const void *data, unsigned int len)
{
    return jhashDataAppend(data, len, jhashInit());
}

/*
 * The best hash table sizes are powers of 2.
 */

static inline
unsigned int jhashSize(unsigned int size)
{
    if (size < 16)
	return 8;
    /* 640K ought to be enough for anybody. */
    if (size >= (1 << 20))
	return (1 << 20);
    /* Round down to proper power of 2.
     * See Perl_hv_ksplit in hv.c. */
    while ((size & (1 + ~size)) != size)
	size &= ~(size & (1 + ~size));
    return size;
}

#endif
