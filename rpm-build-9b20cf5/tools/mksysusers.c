/*
 * This program accepts sysuser lines on its standard input.
 * For each non-empty line, it forms a sequence of fields separated by a
 * single space, pads it with zero bits to a multiple of 24-bit boundary and
 * outputs its base64 encoding.
 * Since we have to parse sysusers line syntax anyway, we cannot rely on
 * base64(1) from coreutils.
 */
#include <errno.h>
#include <error.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rpmbase64.h>

enum record_type {
	SUT_NONE,
	SUT_USER,
	SUT_GROUP,
	SUT_GROUPMEMBERSHIP,
	SUT_IDRANGE,
	SUT_END,
};

enum field {
	SU_NONE,
	SU_TYPE,
	SU_NAME,
	SU_UGID,
	SU_GECOS,
	SU_DIR,
	SU_SHELL,
	SU_END,
};

static int
split_type_name(const char* restrict in,
		enum record_type* type,
		char* restrict name)
{
	int r = 0;
	unsigned long l;
	enum field stage;
	for (stage = SU_TYPE; *in != '\0'; stage += (stage < SU_END)) {
		l = strcspn(in, " \t");
		switch (stage) {
		case SU_TYPE:
			switch (in[0]) {
			case 'u':
				*type = SUT_USER;
				break;
			case 'g':
				*type = SUT_GROUP;
				break;
			case 'm':
				*type = SUT_GROUPMEMBERSHIP;
				break;
			case 'r':
				*type = SUT_IDRANGE;
				break;
			default:
				return -ENODATA;
			}
			break;
		case SU_NAME:
			name = stpncpy(name, in, l);
			break;
		case SU_UGID:
			if (*type == SUT_GROUPMEMBERSHIP) {
				/* The ugid field is hijacked to store the group name. */
				*name++ = '/';
				name = stpncpy(name, in, l);
			}
			break;
		default:
			return r;
		}
		in += l;

		l = strspn(in, " \t");
		in += l;
	}
	return 0;
}

static int
split_onespace(const char* restrict in, char* restrict buf, size_t blen)
{
	char rtype;
	enum field stage;
	size_t brem = blen;
	unsigned long l;
	for (stage = SU_TYPE; *in != '\0'; stage += (stage < SU_END)) {
		if (stage == SU_TYPE)
			rtype = in[0];

		if (rtype == 'u' && stage == SU_GECOS) {
			/* This field shall be '-' ... */
			if (in[0] == '-') {
				l = 1;
				goto known_length;
			}
			/* ...or be enclosed in quotation marks. */
			if (in[0] != '"')
				return -EINVAL;
			/* XXX: we do not recognise escaped quotation marks in the middle */
			l = strcspn(in + 1, "\"");
			++l;
			if (in[l] != '"')
				return -EINVAL;
		} else {
			l = strcspn(in, " \t");
		}
known_length:
		if (brem < l)
			return -ENOBUFS;
		memcpy(buf, in, l);
		brem -= l;
		buf += l;
		in += l;

		/* Replace inter-field whitespace with a single 0x20. */
		l = strspn(in, " \t");
		if (l > 0) {
			brem--;
			*buf++ = ' ';
			in += l;
		}
	}
	return blen - brem;
}

int
main(int argc, char* argv[])
{
	char *line = NULL;
	size_t alloc_size = 0;
	ssize_t len;
	char* buf;
	size_t blen;
	const char* type;
	enum record_type type_e;
	char* name = NULL;
	char* encoded;
	uint8_t arg_prov = 0;

	if (argc < 1)
		error(EXIT_FAILURE, 0, "not enough arguments.");

	if (argc >= 3)
		error(EXIT_FAILURE, 0, "too many arguments.");

	if (argc >= 2 && !strcmp(argv[1], "--prov"))
		arg_prov = 1;

	while ((len = getline(&line, &alloc_size, stdin)) >= 0) {
		if (len > 0 && line[len-1] == '\n')
			line[--len] = '\0';
		if (len == 0)
			continue;
		/* Ignore comments. */
		if (line[0] == '#')
			continue;
		/* Reserve up to 3 bytes for '\0'-padding. */
		blen = (len + 1) + 3;
		buf = calloc(blen, sizeof(uint8_t));
		if (!buf)
			error(EXIT_FAILURE, ENOMEM, "%s", "calloc");
		int r;
		r = split_onespace(line, buf, blen);
		if (r < 0) {
			error(EXIT_SUCCESS, -r, "could not parse %s", line);
			goto no_prov;
		}
		/* Increase r by the length of the required '\0'-padding. */
		r = r + 2;
		r = r - (r % 3);
		encoded = rpmBase64Encode(buf, r, 0);
		if (!encoded) {
			error(EXIT_SUCCESS, ENOMEM, "%s %s", "encode", buf);
			goto no_prov;
		}

		if (arg_prov) {
			name = calloc(blen, sizeof(uint8_t));
			if (!buf)
				error(EXIT_FAILURE, ENOMEM, "%s", "calloc");
			r = split_type_name(buf, &type_e, name);
			if (r < 0)
				goto no_prov;
			switch (type_e) {
			case SUT_USER:
				type = "user";
				/* This will also define a group with the same name. */
				printf("%s%s(%s)%s", "", "group", name, "\n");
				break;
			case SUT_GROUP:
				type = "group";
				break;
			case SUT_GROUPMEMBERSHIP:
				type = "groupmember";
				break;
			case SUT_IDRANGE:
				goto no_prov;
			default:
				error(EXIT_SUCCESS, 0, "%s %s", "unknown type", buf);
				goto no_prov;
			}
			printf("%s%s(%s)%s", "", type, name, " = ");
			printf("%s\n", encoded);
		} else {
			printf("%s\n", encoded);
		}
		goto prov;

no_prov:
		printf("\n");
prov:
		free(name);
		name = NULL;
		free(buf);
		buf = NULL;
		free(line);
		line = NULL;
	}
	return 0;
}
