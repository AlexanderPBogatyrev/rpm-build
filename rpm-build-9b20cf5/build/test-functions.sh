#!/bin/bash
# shellcheck disable=SC2120

set -efu

if [[ "${1-}" == @(-v|--verbose) ]]; then
	export TEST_VERBOSE=y
	shift
fi

RPMB=$(PATH=.:..:$PATH type -p rpmb || type -p rpmbuild)
printf '# Testing %s\n' "$RPMB"

if type -p valgrind >/dev/null && [ -x ../.libs/lt-rpmb ]; then
	# Additional testing for manual runs.
	RPMB="valgrind -q --error-exitcode=99 --track-origins=yes ../.libs/lt-rpmb"
fi

global_failure=0
atexit() {
	rm -rf "$t"
	printf '1..%d\n' "$count"
	exit $global_failure
}
t=$(mktemp -d) && trap atexit 0
readonly t

if [ -e ../macros ]; then
	# We test in the build tree for some reason; cannot depend on a buildroot.
	# Put the global macrofile in $t.
	cp -- ../macros "$t/buildmacros"
	# Also, produce the per-target macrofile in $t.
	# Path to output file: "$t/$ARCH-$VENDOR-$OS/macros".
	( cd ..
	  pkglibdir=$t ./installplatform )
	( cd "$t"
	  set +f
	  for dn in *-alt-*; do
		n=${dn/-alt/}
		[ -e "$n" ] || ln -s "$dn" "$n"
	  done )
	sys_macrofiles="$t/buildmacros:$t/%{_target}/macros"
	sys_macrofiles="$sys_macrofiles:$t/build-tree-tools"
	printf '# Using built macrofiles\n'
else
	sys_macrofiles="/usr/lib/rpm/buildmacros:/usr/lib/rpm/%{_target}/macros"
	printf '# Using system macrofiles\n'
fi

# We don't support --macros option for some reason.
macrosfile=$t/macros
cat > "$t/testrc" <<-EOF
	macrofiles: $sys_macrofiles:$macrosfile
EOF
cat > "$macrosfile" <<-EOF
	%_topdir $t/RPM
	%_tmppath $t/tmp
EOF
mkdir "$t/tmp"
cat >> "$t/build-tree-tools" <<-EOF
	%__find_provides $(pwd)/../autodeps/find-provides
	%__find_requires $(pwd)/../autodeps/find-requires
	%__spec_autodep_custom_pre\\
	export RPMB_LIB_DIR="$(pwd)/../scripts"\\
	export RPMB_AUTODEPS_DIR="$(pwd)/../autodeps"\\
	export RPMB_TOOLS_DIR="$(pwd)/../tools"\\
	%nil
EOF

declare -i count=0
run() {
	local expect=0 msg re vre macros spec callback trimspec=1 binary=rpmbuild eval "$@"
	count+=1
	[ -s "$macrosfile" ] && macros="--rcfile /usr/lib/rpm/rpmbuildrc:$t/testrc"
	set +e
	# shellcheck disable=SC2086
	(
		set -x
		exec -a "$binary" $RPMB ${macros-} ${args-} ${eval:+--eval="$eval"} ${spec-}
	) >"$t/output" 2>&1
	local ret=$?
	set -e
	if [[ -z "${msg-}" ]]; then
		msg=$(tail -1 "$t/output")
	fi
	msg="[${topic?}] $msg"
	if (( expect == 0 )); then
		msg="xpass: $msg"
	else
		msg="xfail: $msg"
	fi
	local ok=1 e err=
	if (( expect != ret )); then
		ok=0
		printf -v e '\t! unexpected exit code %d != %d\n' "$ret" "$expect"
		err+="$e"
	fi
	re_output="$t/output"
	if [ -v callback ]; then
		$callback 2>>"$t/output" || ret=$?
	fi
	if [ -n "${re-}" ] && ! grep -Pzq -e "$re" "$re_output"; then
		printf -v e '\t! positive grep pattern "%s" does not match output\n' "$re"
		err+="$e"
		ok=0
	fi
	if [ -n "${vre-}" ] && grep -Pzq -e "$vre" "$re_output"; then
		printf -v e '\t! negative grep pattern "%s" matches output\n' "$vre"
		err+="$e"
		ok=0
	fi

	if ((ok)); then
		printf 'ok %d - %s\n' "$count" "$msg"
	else
		printf 'not ok %d - %s\n' "$count" "$msg"
		printf '%s' "$err"
		global_failure=1
	fi
	if [ -v TEST_VERBOSE ] || (( ok == 0 )); then
		if [[ -v spec ]]; then
			printf '\t== %s (%s) ==\n' "$(basename "$spec")" "$trimspec"
			sed -n "$trimspec,\$s/^/\t/p" < "$spec"
		fi
		if [[ -s "$macrosfile" ]]; then
			printf '\t== %s ==\n' "$(basename "$macrosfile")"
			sed -n 's/^/\t/p' < "$macrosfile"
		fi
		printf '\t== output ==\n'
		sed 's/^/\t/' < "$t/output"
		printf '\t== exit code %d (expected %d) ==\n' "$ret" "$expect"
		if [[ $re_output != "$t/output" ]]; then
			printf '\t== %s ==\n' "$re_output"
			cat -nA "$re_output" | sed 's/^/\t/'
			echo
		fi
	fi
}

check() {
	cat <<-EOF >"$t/test.spec"
		Name: test
		Version: 0
		Release: 0
		Summary: Test
		Group: Other
		License: CC0
		%description
		%summary.
		%check
	EOF
	cat >>"$t/test.spec"
	run \
		args="--short-circuit -bt" \
		spec="$t/test.spec" \
		trimspec="/^%check/" \
		"$@"
}

pass() { check "$@"; }
xfail() { check expect=1 "$@"; }
