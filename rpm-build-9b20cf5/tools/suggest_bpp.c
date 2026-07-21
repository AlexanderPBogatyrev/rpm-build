/*
 * Copyright (C) 2010  Alexey Tourbin <at@altlinux.org>
 * Copyright (C) 2019  Dmitry V. Levin <ldv@altlinux.org>
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * For any given set of N different n-bit numbers the probability that
 * an arbitrary chosen n-bit number equals to one of these N numbers is
 * P = N / 2^n.
 *
 * Consequently,
 * log2(P) = log2(N / 2^n) = log2(N) - n,
 * n = log2(N) - log2(P).
 *
 * For the given P and the number of symbols N the bitness
 * of a good hash-function has to be
 * n = ceil(log2(N) - log2(P))
 *
 * For P == 2^(-10)
 * n = ceil(log2(N) + 10) = ceil(log2(N)) + 10
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int
main(int argc, const char **argv)
{
	assert(argc == 2);

	int n = atoi(argv[1]);
	assert(n >= 1);

	printf("%d\n", (int) ceil(log2((double) n)) + 10);

	return 0;
}
