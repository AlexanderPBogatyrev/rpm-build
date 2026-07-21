
/* 
   Dump dynamic linker configuration.

   Copyright (C) 1999,2000,2001,2002,2003,2004 Free Software Foundation, Inc.
   This file is based on part of the GNU C Library.
   Contributed by Andreas Jaeger <aj@suse.de>, 1999.
   Modified by Dmitry V. Levin <ldv@altlinux.org>, 2004, 2006.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
#ifndef _FILE_OFFSET_BITS
# define _FILE_OFFSET_BITS 64
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <ctype.h>
#include <glob.h>
#include <sys/stat.h>

void   *
xmalloc(size_t size)
{
	void   *r = malloc(size);

	if (!r)
		error(EXIT_FAILURE, errno, "malloc");
	return r;
}

void   *
xrealloc(void *ptr, size_t size)
{
	void   *r = realloc(ptr, size);

	if (!r)
		error(EXIT_FAILURE, errno, "realloc");
	return r;
}

char   *
xstrdup(const char *s)
{
	size_t  len = strlen(s);
	char   *r = xmalloc(len + 1);

	memcpy(r, s, len + 1);
	return r;
}

/* List of directories to handle.  */
struct dir_entry
{
	struct dir_entry *next;
	const char *name;
};

static struct dir_entry *path_list_head, *path_list_tail;
static const char *prefix;

static void
path_list_add(const char *path)
{
	if (path[0] != '/')
		return;

	struct dir_entry *e;

#ifndef KEEP_DUPLICATES
	for (e = path_list_head; e; e = e->next)
		if (!strcmp(path, e->name))
			return;
#endif

	char   *newp = NULL;

	if (prefix)
	{
		newp = xmalloc(strlen(prefix) + strlen(path) + 1);
		strcpy(newp, prefix);
		strcat(newp, path);
		path = newp;
	}

	struct stat buf;

	if (stat(path, &buf))
	{
		free(newp);
		return;
	}

	e = xmalloc(sizeof(*e));

	e->next = 0;
	e->name = xstrdup(prefix ? (path + strlen(prefix)) : path);
	if (!path_list_head)
		path_list_head = e;
	if (path_list_tail)
		path_list_tail->next = e;
	path_list_tail = e;

	free(newp);
}

static void
parse_path_line(char *line)
{
	/* Skip leading space characters */
	while (isspace(*line))
		++line;

	/* Search for an '=' sign.  */
	char   *equal_sign = strchr(line, '=');

	if (equal_sign)
		*equal_sign = '\0';

	int     i = strlen(line) - 1;

	/* Remove trailing space characters */
	for (; i >= 0 && isspace(line[i]); --i)
		line[i] = '\0';

	/* Remove trailing slashes */
	for (; i > 0 && line[i] == '/'; --i)
		line[i] = '\0';

	if (line[0] == '/')
		path_list_add(line);
}

static void parse_file(const char *filename);

static void
parse_include_pattern(const char *filename, const char *pattern)
{
	char   *newp = NULL, *p;

	if (prefix)
	{
		if (pattern[0] == '/')
		{
			newp = xmalloc(strlen(prefix) + strlen(pattern) + 1);
			strcpy(newp, prefix);
			strcat(newp, pattern);
			pattern = newp;
		} else if ((p = strrchr(filename, '/')))
		{
			size_t  preflen = strlen(prefix);
			size_t  patlen = strlen(pattern) + 1;

			newp = xmalloc(p - filename + 1 + preflen + patlen);
			memcpy(newp, prefix, preflen);
			memcpy(newp + preflen, filename, p - filename + 1);
			memcpy(newp + (p - filename + 1 + preflen), pattern,
			       patlen);
			pattern = newp;
		}
	} else
	{
		if (pattern[0] != '/' && (p = strrchr(filename, '/')))
		{
			size_t  patlen = strlen(pattern) + 1;

			newp = xmalloc(p - filename + 1 + patlen);
			memcpy(newp, filename, p - filename + 1);
			memcpy(newp + (p - filename + 1), pattern, patlen);
			pattern = newp;
		}
	}

	glob_t  gl;

	if (glob(pattern, 0, NULL, &gl) == 0)
	{
		size_t  i;

		for (i = 0; i < gl.gl_pathc; ++i)
			parse_file(gl.gl_pathv[i]);
		globfree(&gl);
	}

	free(newp);
}

static void
parse_include_line(const char *filename, char *line)
{
	char   *arg;

	while ((arg = strsep(&line, " \t")) != NULL)
		if (arg[0] != '\0')
			parse_include_pattern(filename, arg);
}

static void
parse_file(const char *filename)
{
	FILE   *fp = fopen(filename, "r");

	if (fp == NULL)
		return;

	char   *line = 0;
	size_t  linesize = 0;

	while (getline(&line, &linesize, fp) > 0)
	{
		char   *p = strchr(line, '\n');

		if (p)
			*p = '\0';

		/* Because the file format does not know any form of quoting we
		   can search forward for the next '#' character and if found
		   make it terminating the line.  */
		p = strchr(line, '#');
		if (p)
			*p = '\0';

		/* Skip leading whitespace characters.  */
		for (p = line; isspace(*p); ++p)
			;

		/* If the line is blank it is ignored.  */
		if (p[0] == '\0')
			continue;

		if (!strncmp(p, "include", 7) && isblank(p[7]))
			parse_include_line(filename, p + 8);
		else
			parse_path_line(p);
	}
	free(line);
	fclose(fp);
}

int
main(int ac, char **av)
{
	const char *fname = "/etc/ld.so.conf";

	if (ac > 3)
		return 1;
	if (ac > 2)
	{
		int  len = strlen(av[2]) - 1;

		/* Remove trailing slashes */
		for (; len > 0 && av[2][len] == '/'; --len)
			av[2][len] = '\0';
		if (len < 1 || av[2][0] != '/')
			return 1;
		prefix = av[2];
	}
	if (ac > 1 && av[1][0])
		fname = av[1];

	parse_file(fname);

#if defined(SLIBDIR) && defined(LIBDIR)
	/* Always add the standard search paths.  */
	path_list_add(SLIBDIR);
	if (strcmp(SLIBDIR, LIBDIR))
		path_list_add(LIBDIR);
#endif

	struct dir_entry *e;

	for (e = path_list_head; e; e = e->next)
		puts(e->name);
	return 0;
}
