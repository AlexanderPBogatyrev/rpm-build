/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Adapted from FreeBSD implementation of realpath(3), which has
 * copyright (c) 2003 Constantin S. Svintsoff <kostik@iclub.nsu.ru>
 *
 * This program "almost canonicalizes" a file path.
 * The algorithm is similar to realpath(3), with the following amendments:
 * - the last path component is ignored;
 * - if a path component is a symlink and it is present in a colon-separated
 *   list of paths, to be referred to as "ignore-list", the symlink is not
 *   followed.
 *
 * The source path is specified as the second CLI argument. The first CLI
 * argument is the ignore-list. This is why the tool is called remappath-2: it
 * expects exactly 2 arguments and accepts the ignore-list as a positional
 * argument, not as an option. (Also, we cannot use a hyphen in a program name
 * due to automake interface limitations.)
 *
 * If it ever gains a sophisticated CLI with options, or its functionality is
 * merged into an existing general-purpose tool, it will be installed under a
 * different name.
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char * ign_list;

static unsigned int
is_exception(const char * resolved, size_t resolved_len)
{
	/* Extension of the realpath algorithm: if this component
	 * is in the ignore-list, return true.
	 */
	ssize_t l, el;
	const char* p;
	const char* curexc = ign_list;
	el = strlen(ign_list);
	while (el > 0) {
		p = strchr(curexc, ':');
		if (!p)
			l = strlen(curexc);
		else
			l = p - curexc;
		if (l == resolved_len && !strncmp(resolved, curexc, resolved_len))
			return 1;
		if (!p)
			break;
		curexc += l + 1;
		el -= l + 1;
	}

	return 0;
}

/*
 * Find the real name of path, by removing all ".", ".." and symlink
 * components.  Returns @resolved on success, or NULL on failure,
 * in which case the path which caused trouble is left in resolved.
 */
static char *
remappath1(const char * path, char * resolved)
{
	struct stat sb;
	char *p, *q;
	size_t left_len, next_token_len, resolved_len;
	unsigned symlinks;
	ssize_t slen;
	char left[PATH_MAX], next_token[PATH_MAX], symlink[PATH_MAX];

	symlinks = 0;
	if (path[0] == '/') {
		resolved[0] = '/';
		resolved[1] = '\0';
		if (path[1] == '\0')
			return resolved;
		resolved_len = 1;
		left_len = strlcpy(left, path + 1, sizeof(left));
	} else {
		if (getcwd(resolved, PATH_MAX) == NULL) {
			resolved[0] = '.';
			resolved[1] = '\0';
			return NULL;
		}
		resolved_len = strlen(resolved);
		left_len = strlcpy(left, path, sizeof(left));
	}
	if (left_len >= sizeof(left) || resolved_len >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	/*
	 * Iterate over path components in `left'.
	 */
	while (left_len != 0) {
		/*
		 * Extract the next path component and adjust `left'
		 * and its length.
		 */
		p = strchr(left, '/');

		next_token_len = p != NULL ? (size_t)(p - left) : left_len;
		memcpy(next_token, left, next_token_len);
		next_token[next_token_len] = '\0';

		if (p != NULL) {
			left_len -= next_token_len + 1;
			memmove(left, p + 1, left_len + 1);
		} else {
			left[0] = '\0';
			left_len = 0;
		}

		if (resolved[resolved_len - 1] != '/') {
			if (resolved_len + 1 >= PATH_MAX) {
				errno = ENAMETOOLONG;
				return NULL;
			}
			resolved[resolved_len++] = '/';
			resolved[resolved_len] = '\0';
		}
		if (next_token[0] == '\0') {
			/* Handle consequential slashes. */
			continue;
		} else if (strcmp(next_token, ".") == 0) {
			continue;
		} else if (strcmp(next_token, "..") == 0) {
			/*
			 * Strip the last path component except when we have
			 * single "/"
			 */
			if (resolved_len > 1) {
				resolved[resolved_len - 1] = '\0';
				q = strrchr(resolved, '/') + 1;
				*q = '\0';
				resolved_len = q - resolved;
			}
			continue;
		}

		/*
		 * Append the next path component and lstat() it.
		 */
		resolved_len = strlcat(resolved, next_token, PATH_MAX);
		if (resolved_len >= PATH_MAX) {
			errno = ENAMETOOLONG;
			return NULL;
		}
		if (p == NULL)
			/* Difference from realpath(3):
			 * like `readlink -m`, ignore last component.
			 */
			break;
		if (lstat(resolved, &sb) != 0) {
			/* Difference from realpath(3):
			 * allow any component to be missing.
			 */
			if (errno == ENOENT)
				continue;
			return NULL;
		}
		if (S_ISLNK(sb.st_mode) && is_exception(resolved, resolved_len)) {
			/* Difference from realpath(3):
			 * Do not follow the link; do not check
			 * if non-final component is a directory.
			 */
		} else if (S_ISLNK(sb.st_mode)) {
			if (symlinks++ > MAXSYMLINKS) {
				errno = ELOOP;
				return NULL;
			}
			slen = readlink(resolved, symlink, sizeof(symlink));
			if (slen <= 0 || slen >= (ssize_t)sizeof(symlink)) {
				if (slen < 0)
					; /* keep errno from readlink(2) call */
				else if (slen == 0)
					errno = ENOENT;
				else
					errno = ENAMETOOLONG;
				return NULL;
			}
			symlink[slen] = '\0';
			if (symlink[0] == '/') {
				resolved[1] = '\0';
				resolved_len = 1;
			} else {
				/* Strip the last path component. */
				q = strrchr(resolved, '/') + 1;
				*q = '\0';
				resolved_len = q - resolved;
			}

			/*
			 * If there are any path components left, then
			 * append them to symlink. The result is placed
			 * in `left'.
			 */
			if (p != NULL) {
				if (symlink[slen - 1] != '/') {
					if (slen + 1 >= (ssize_t)sizeof(symlink)) {
						errno = ENAMETOOLONG;
						return NULL;
					}
					symlink[slen] = '/';
					symlink[slen + 1] = 0;
				}
				left_len = strlcat(symlink, left,
				    sizeof(symlink));
				if (left_len >= sizeof(symlink)) {
					errno = ENAMETOOLONG;
					return NULL;
				}
			}
			left_len = strlcpy(left, symlink, sizeof(left));
		} else if (!S_ISDIR(sb.st_mode) && p != NULL) {
			errno = ENOTDIR;
			return NULL;
		}
	}

	/*
	 * Remove trailing slash except when the resolved pathname
	 * is a single "/".
	 */
	if (resolved_len > 1 && resolved[resolved_len - 1] == '/')
		resolved[resolved_len - 1] = '\0';
	return resolved;
}

static char *
remappath(const char * restrict path, char * restrict resolved)
{
	char *m, *res;

	if (path == NULL) {
		errno = EINVAL;
		return NULL;
	}
	if (path[0] == '\0') {
		errno = ENOENT;
		return NULL;
	}
	if (resolved != NULL) {
		m = NULL;
	} else {
		m = resolved = malloc(PATH_MAX);
		if (resolved == NULL)
			return NULL;
	}
	res = remappath1(path, resolved);
	if (res == NULL)
		free(m);
	return res;
}

int
main(int argc, char * argv[])
{
	if (argc != 3)
		error(EXIT_FAILURE, 0, "Exactly 2 arguments expected. Usage: $0 <ignore-list> <path>");

	ign_list = argv[1];
	const char * source = argv[2];

	char* res = remappath(source, NULL);
	if (!res)
		error(EXIT_FAILURE, errno, "failed to remap \"%s\"", source);

	printf("%s\n", res);
	free(res);
	return 0;
}
