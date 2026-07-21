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

#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

extern const char *__progname;

static void
my_error_print_progname (void)
{
	fflush (stdout);
	fprintf (stderr, "%s: ", __progname);
}

__attribute__ ((__noreturn__))
static void usage (void)
{
	fprintf (stderr, "usage: %s <pid> <command>\n", __progname);
	exit (EXIT_FAILURE);
}

static void
wait_for_pdeath (pid_t pid)
{
	for (;;)
	{
		if (kill (pid, 0) < 0)
		{
			if (ESRCH == errno)
				break;
			else
				error (EXIT_FAILURE, errno, "kill: %u", pid);
		}
		usleep (100000);
	}
}

int
main (int ac, char *const *av)
{
	pid_t   pid;

	error_print_progname = my_error_print_progname;

	if (ac < 3)
		usage ();

	pid = atoi (av[1]);

	/*
	 *  Check arguments.
	 */

	if (pid <= 1)
		usage ();

	if (kill (pid, 0) < 0)
		error (EXIT_FAILURE, errno, "kill: %u", pid);

	/* Lets parent go on. */
	if (daemon (1, 1) < 0)
		error (EXIT_FAILURE, errno, "daemon");

	/* Wait for parent completion. */
	wait_for_pdeath (pid);

	execvp (av[2], av + 2);
	error (EXIT_FAILURE, errno, "execv: %s", av[2]);
	return EXIT_FAILURE;
}
