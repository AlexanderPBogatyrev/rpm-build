/*
  Copyright (C) 2001  Dmitry V. Levin <ldv@altlinux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef _GNU_SOURCE
 # define _GNU_SOURCE
#endif
#ifndef _FILE_OFFSET_BITS
# define _FILE_OFFSET_BITS 64
#endif
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

extern const char *__progname;

__attribute__ ((__noreturn__))
static void usage( void )
{
	fprintf( stderr, "usage: %s filename [blocksize]\n"
		"\tfilename   - stat this file\n"
		"\tblocksize  - set size of block to this value, overriding default\n",
		__progname );
	exit( EXIT_FAILURE );
}

int	main( int ac, const char *av[] )
{
	const char *fname;
	unsigned long	bsize = 0;
	struct stat stb;

	if ( (ac < 2) || (ac > 3) )
		usage();

	fname = av[1];
	if ( ac > 2 )
	{
		bsize = atol( av[2] );
		if ( bsize < 1 )
			usage();
	}

	if ( stat( fname, &stb ) < 0 )
	{
		fprintf( stderr, "%s: %s: %s\n", __progname, fname, strerror(errno) );
		exit( EXIT_FAILURE );
	}

	if ( !bsize )
		bsize = stb.st_blksize;

	printf( "%lu\n", (bsize < 2) ? stb.st_size : ((bsize - 1 + stb.st_size)/bsize) );

	return 0;
}
