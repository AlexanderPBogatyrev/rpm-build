/*
 * bsearch.h - drop-in bsearch(3) replacement implemented with a macro.
 * Written by Alexey Tourbin <at@altlinux.org>.
 * License: GPLv2+ or LGPL, see RPM COPYING.
 *
 * There are two reasons for using BSEARCH macro:
 * 1. Callback function can be inlined.  Since gcc sucks at inlining
 *    callbacks, BSEARCH has to be implemented with a macro.
 * 2. When BSEARCH returns NULL (no matching element), BSEARCH_IDX will
 *    indicate insertion position for the element.  To perform actual
 *    insertion, the array should be resized as necessary, and elements
 *    starting at BSEARCH_IDX moved one place to the right.
 */

#include <stddef.h>

static __thread
size_t bsearch_idx_;
#define BSEARCH_IDX bsearch_idx_

// Based on glibc/stdlib/bsearch.c.
// Copyright (C) 1991,92,97,2000,02 Free Software Foundation, Inc.
#define BSEARCH(key, base, nmemb, size, compar)		\
({							\
    size_t l_ = 0;					\
    size_t u_ = (nmemb);				\
    size_t idx_ = 0;					\
    int cmp_ = -1;					\
    void *elem_ = NULL;					\
    while (l_ < u_) {					\
	idx_ = (l_ + u_) / 2;				\
	elem_ = ((char *)(base)) + idx_ * (size);	\
	cmp_ = (compar)((key), elem_);			\
	if (cmp_ < 0)					\
	    u_ = idx_;					\
	else if (cmp_ > 0)				\
	    l_ = idx_ + 1;				\
	else						\
	    break;					\
    }							\
    bsearch_idx_ = (cmp_ > 0) ? idx_ + 1 : idx_;	\
    (cmp_ == 0) ? elem_ : NULL;				\
})
