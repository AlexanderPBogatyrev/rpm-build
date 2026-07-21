// gcc tools/test_rpmvercmp.c -lrpm
#include <stdio.h>
#include <errno.h>
#ifdef HAVE_CONFIG_H
# include <rpmlib.h>
#else /* librpm-devel */
# include <rpm/rpmlib.h>
# include <rpm/rpmvercmp.h>
#endif

/* All following versions are greater than previous unless prefixed with '='. */
const char *list[] = {
	"0",
	"1",
	"1.0~p",
	"1.0", "=1_0",
	"1.0.00", "=1.0.0", "=1.0.000",
	"1.0.0a",
	"1.0.0a0",
	"1.0.0a1",
	"1.0.0aa",
	"1.1~p~p",
	"1.1~p",
	"1.01", "=1.1", "=1.0001",
	"2",
};

int main(int argc, char **argv)
{
	int ret = 0;
	const size_t tests = sizeof(list) / sizeof(list[0]);

	printf("TAP version 13\n1..%zu\n", tests);
	for (size_t i = 0; i < tests; i++) {
		const char *a = i > 0 ? list[i - 1] : "";
		const char *b = list[i];
		int exp = -1;
		if (*b == '=') {
			b++;
			exp = 0;
		}
		int cmp = rpmvercmp(a, b);
		if (cmp == exp) {
			printf("ok");
		} else {
			printf("not ok");
			ret = 1;
		}
		printf(" %zu - cmp '%s' '%s' results %d expected %d\n", i + 1,
		       a, b, cmp, exp);

	}
	printf("# rpmvercmp test %s\n", ret ? "FAIL" : "PASS");
	return ret;
}
