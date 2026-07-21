/*
  Copyright (C) 2002, 2003  Dmitry V. Levin <ldv@altlinux.org>

  rpm versions comparator.

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
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <stdio.h>
#include <errno.h>
#include <rpmlib.h>

int
main(int ac, const char *av[])
{
	if (ac != 3)
	{
		fprintf(stderr,
			"%s - compare versions.\n"
			"Usage: %s <version1> <version2>\n"
			"\nReport bugs to http://bugs.altlinux.ru/\n\n",
			program_invocation_short_name,
			program_invocation_short_name);
		return 1;
	}

	printf("%d\n", rpmvercmp(av[1], av[2]));

	return 0;
}
