/*
  Copyright (C) 2002-2004  Dmitry V. Levin <ldv@altlinux.org>

  rpm EVRs comparator.

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
#include <error.h>
#include <string.h>
#include <stdlib.h>
#include <rpmlib.h>

static  Header
newHeaderEVR(const char *e, const char *v, const char *r)
{
	Header  h;

	if (!(h = headerNew()))
		return h;

	if (e)
	{
		int     i = atoi(e);

		headerAddEntry(h, RPMTAG_EPOCH, RPM_INT32_TYPE, &i, 1);
	}
	if (v)
		headerAddEntry(h, RPMTAG_VERSION, RPM_STRING_TYPE, v, 1);
	if (r)
		headerAddEntry(h, RPMTAG_RELEASE, RPM_STRING_TYPE, r, 1);
	return h;
}

int
main(int ac, const char *av[])
{
	const char *e1 = 0, *v1 = 0, *r1 = 0;
	const char *e2 = 0, *v2 = 0, *r2 = 0;
	char   *arg1, *arg2;
	Header  h1, h2;

	if (ac != 3)
	{
		fprintf(stderr,
			"%s - compare EVRs.\n"
			"Usage: %s <EVR1> <EVR2>\n"
			"\nReport bugs to http://bugs.altlinux.ru/\n\n",
			program_invocation_short_name,
			program_invocation_short_name);
		return EXIT_FAILURE;
	}

	arg1 = strdup(av[1]);
	arg2 = strdup(av[2]);
	if (!arg1 || !arg2)
		error(EXIT_FAILURE, errno, "strdup");

	parseEVR(arg1, &e1, &v1, &r1);
	parseEVR(arg2, &e2, &v2, &r2);
	h1 = newHeaderEVR(e1, v1, r1);
	h2 = newHeaderEVR(e2, v2, r2);
	if (!h1 || !h2)
		error(EXIT_FAILURE, errno, "headerNew");

	printf("%d\n", rpmVersionCompare(h1, h2));

	h2 = headerFree(h2);
	h1 = headerFree(h1);
	free(arg2);
	free(arg1);

	return EXIT_SUCCESS;
}
