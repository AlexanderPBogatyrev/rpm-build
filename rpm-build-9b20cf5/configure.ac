dnl configure.in for RPM
dnl
dnl Don't add checks for things we can't work around, unless those checks
dnl failing causes the script to bomb out with a message. If we can't fix
dnl it, why check it?

AC_INIT(rpm, 4.0.4)
AC_CANONICAL_SYSTEM
AC_PREREQ(2.13)
AC_CONFIG_SRCDIR(rpmqv.c)
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([1.4 foreign])

dnl XXX AM_MAINTAINER_MODE

dnl Checks for programs.
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AS=${AS-as}
AC_SUBST(AS)
if test "$ac_cv_prog_gcc" = yes; then
	CFLAGS="$CFLAGS -D_GNU_SOURCE -D_REENTRANT -fno-strict-aliasing -Wall -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes -Wno-char-subscripts -Werror=trampolines"
fi
export CFLAGS

AC_PROG_CPP
AC_PROG_GCC_TRADITIONAL

AC_SYS_LARGEFILE

dnl Does this platform require array notation to assign to a va_list?
dnl If cross-compiling, we assume va_list is "normal".  If this breaks
dnl you, set ac_cv_valistisarray=true and maybe define HAVE_VA_LIST_AS_ARRAY
dnl also just to be sure.
AC_MSG_CHECKING(whether va_list assignments need array notation)
AC_CACHE_VAL(ac_cv_valistisarray,
	[AC_TRY_RUN([#include <stdlib.h>
		     #include <stdarg.h>
		     void foo(int i, ...) {
			va_list ap1, ap2;
			va_start(ap1, i);
			ap2 = ap1;
			if (va_arg(ap2, int) != 123 || va_arg(ap1, int) != 123)
			    { exit(1); }
			va_end(ap1); va_end(ap2);
		     }
		     int main() { foo(0, 123); return(0); }],
		    [ac_cv_valistisarray=false],
		    [ac_cv_valistisarray=true],
		    [ac_cv_valistisarray=false])])

if test "$ac_cv_valistisarray" = true ; then
	AC_DEFINE(HAVE_VA_LIST_AS_ARRAY, 1,
		[Define as 1 if your va_list type is an array])
	AC_MSG_RESULT(yes)
else
       AC_MSG_RESULT(no)
fi

RPMUSER=rpm
RPMGROUP=rpm
export RPMUSER RPMGROUP
AC_SUBST(RPMUSER)
AC_SUBST(RPMGROUP)

dnl
dnl This now uses libtool. Put
dnl	LDFLAGS_STATIC="-all"
dnl to attempt static executables using libtool. Otherwise
dnl	LDFLAGS_STATIC=""
dnl
AC_MSG_CHECKING(flag used by libtool to link rpm)
if test X"$GCC" = Xyes ; then
	case "$target" in
		*-*-solaris*)	LDFLAGS_STATIC="-static";;
		*-*-hpux*)	LDFLAGS_STATIC="-static";;
		*-*-sysv5uw*)   LDFLAGS_STATUS="-static";; # Unixware has no shared libthread.
		*-*-*)		LDFLAGS_STATIC="-all-static";;
	esac
elif test X"$CC" = Xcc ; then
	case "$target" in
		*-*-linux*)	LDFLAGS_STATIC="-all-static";;
		*-*-freebsd*)	LDFLAGS_STATIC="-all-static";;
		*-*-osf*)	LDFLAGS_STATIC="";; # OSF5 has no shared pthreads libs
		*-*-aix*)	LDFLAGS_STATIC="-static";;  # -Wl,-bnso doesn't seem to work...
		*-*-hpux*)	LDFLAGS_STATIC="-static";;
		*-*-solaris*)	LDFLAGS_STATIC="-static";;
		*-*-irix*)	LDFLAGS_STATIC="-static";;  #should be -non_shared, but can't
											# link because of crt1.o then.
		*-*-ultrix*)	LDFLAGS_STATIC="-all-static";;  #ultrix doesn't have shared libs.
		*-*-*)		LDFLAGS_STATIC="-static"
AC_MSG_WARN([

Unable to guess what option to pass to $CC to generate a static
executable.  You will need to set the LDFLAGS_STATIC macro in Makefile.inc to
the appropriate argument(s) if you want to build a static rpm executable.

])
		;;
	esac
else
	# just link it dynamically
	LDFLAGS_STATIC=""
fi
LDFLAGS_STATIC="${LDFLAGS} ${LDFLAGS_STATIC}"	# libtool format
AC_MSG_RESULT($LDFLAGS_STATIC)
AC_SUBST(LDFLAGS_STATIC)

dnl
dnl look for POSIX chmod attributes
dnl
AC_MSG_CHECKING(POSIX chmod)
touch foo.chmodtest
chmod 744 foo.chmodtest
chmod +X foo.chmodtest 2>/dev/null
a=`ls -l foo.chmodtest | awk '{print $1}'`
rm -f foo.chmodtest
if test "$a" = "-rwxr-xr-x"; then
    AC_MSG_RESULT(yes)
    FIXPERMS=a+rX,g-w,o-w 
else
    AC_MSG_RESULT(no (tell your OS vendor about GNU fileutils))
    FIXPERMS=a+r,g-w,o-w 
fi
AC_SUBST(FIXPERMS)

dnl
dnl see if we have a mkdir that supports `-p'.
dnl
AC_PATH_PROGS(MKDIR, mkdir, mkdir)
AC_MSG_CHECKING(if $MKDIR supports -p)
MKDIR_P="$MKDIR -p"
AC_MSG_RESULT(yes)

dnl
dnl substitute this into config.h, so the C source picks it up.
dnl
AC_DEFINE_UNQUOTED(MKDIR_P, "${MKDIR_P}",
[A full path to a program, possibly with arguments, that will create a
   directory and all necessary parent directories, ala `mkdir -p'])
AC_SUBST(MKDIR_P)

AC_ISC_POSIX

AC_PROG_AWK
AC_PROG_LN_S
AC_PROG_MAKE_SET

dnl AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)

AC_CHECK_TOOL(AR, ar, :)

dnl
dnl use defaults if cross-compiling, otherwise use the default path.
dnl
if test "$cross_compiling" = "yes"; then
    MYPATH=
else
    MYPATH="/usr/bin:/bin"
fi

dnl
dnl Find some common programs
dnl
AC_PATH_PROG(__CAT, cat, /bin/cat, $MYPATH)
AC_PATH_PROG(__CHGRP, chgrp, /bin/chgrp, $MYPATH)
AC_PATH_PROG(__CHMOD, chmod, /bin/chmod, $MYPATH)
AC_PATH_PROG(__CHOWN, chown, /bin/chown, $MYPATH)
AC_PATH_PROG(__CP, cp, /bin/cp, $MYPATH)
AC_PATH_PROG(__FILE, file4, /usr/bin/file4, $MYPATH)
AC_PATH_PROG(__GPG, gpg, /usr/bin/gpg, $MYPATH)
AC_PATH_PROG(__GREP, grep, /bin/grep, $MYPATH)
AC_PATH_PROG(__SUBST, subst, /usr/bin/subst, $MYPATH)

dnl Archiving and compression programs
AC_PATH_PROG(__TAR, tar, /bin/tar, $MYPATH)
AC_PATH_PROG(__CPIO, cpio, /bin/cpio, $MYPATH)
AC_PATH_PROG(__GZIP, gzip, /bin/gzip, $MYPATH)
AC_PATH_PROG(__BZIP2, bzip2, /bin/bzip2, $MYPATH)
AC_PATH_PROG(__UNZIP, unzip, /usr/bin/unzip, $MYPATH)
AC_PATH_PROG(__LZMA, lzms, /usr/bin/lzma, $MYPATH)
AC_PATH_PROG(__XZ, xz, /usr/bin/xz, $MYPATH)

AC_PATH_PROG(__ID, id, /usr/bin/id, $MYPATH)
AC_SUBST(__ID_U, "%{__id} -u")

AC_PATH_PROG(__INSTALL, install, /usr/bin/install, $MYPATH)
AC_PATH_PROG(__INSTALL_INFO, install-info, /sbin/install-info, $MYPATH)
AC_PATH_PROG(__MAKE, make, /usr/bin/make, $MYPATH)
AC_PATH_PROG(__MKDIR, mkdir, /bin/mkdir, $MYPATH)
AC_PATH_PROG(__MV, mv, /bin/mv, $MYPATH)
AC_PATH_PROG(__PATCH, patch, /usr/bin/patch, $MYPATH)

AC_PATH_PROG(__PERL, perl, /usr/bin/perl, $MYPATH)
AC_PATH_PROG(PGPBIN, pgp, /usr/bin/pgp, $MYPATH)
AC_PATH_PROG(__PYTHON, python, /usr/bin/python, $MYPATH)
AC_PATH_PROG(__RM, rm, /bin/rm, $MYPATH)
AC_PATH_PROG(__RSH, rsh, /usr/bin/rsh, $MYPATH)
AC_PATH_PROG(__SED, sed, /bin/sed, $MYPATH)
AC_PATH_PROG(__SSH, ssh, /usr/bin/ssh, $MYPATH)

AC_PATH_PROG(__LD, ld, /usr/bin/ld, $MYPATH)
AC_PATH_PROG(__NM, nm, /usr/bin/nm, $MYPATH)
AC_PATH_PROG(__OBJCOPY, objcopy, /usr/bin/objcopy, $MYPATH)
AC_PATH_PROG(__OBJDUMP, objdump, /usr/bin/objdump, $MYPATH)
AC_PATH_PROG(__STRIP, strip, /usr/bin/strip, $MYPATH)

WITH_ZLIB_SUBDIR=
WITH_ZLIB_INCLUDE=
WITH_ZLIB_LIB=
AC_SUBST(WITH_ZLIB_SUBDIR)
AC_SUBST(WITH_ZLIB_INCLUDE)
AC_SUBST(WITH_ZLIB_LIB)

dnl
dnl Check for features
dnl

dnl Checks for libraries.

AC_CHECK_FUNC(setreuid, [], [
    AC_CHECK_LIB(ucb, setreuid, [if echo $LIBS | grep -- -lucb >/dev/null ;then :; else LIBS="$LIBS -lc -lucb" USEUCB=y;fi])
])

AC_CHECK_FUNC(rand, [], [
    AC_CHECK_LIB(rand, rand)
])

AC_CHECK_FUNC(getdomainname, [], [
    AC_CHECK_LIB(nsl, getdomainname)
])
AC_CHECK_FUNC(socket, [], [
    AC_CHECK_LIB(socket, socket)
])

WITH_LIBELF_LIB=
AC_CHECK_LIB(elf, gelf_getehdr, [
    AC_DEFINE(HAVE_LIBELF, 1, [Define to 1 if you have the 'elf' library (-lelf).])
    WITH_LIBELF_LIB="-lelf"
])
AC_SUBST(WITH_LIBELF_LIB)

WITH_LIBDW_LIB=
AC_CHECK_LIB(dw, dwelf_strent_off, [
    AC_DEFINE(HAVE_LIBDW, 1, [Define to 1 if you have the 'dw' library (-ldw).])
    WITH_LIBDW_LIB="-ldw"
])
AC_SUBST(WITH_LIBDW_LIB)

WITH_LIBRPMIO_LIB=
AC_CHECK_LIB(rpmio, rpmDigestUpdate, [
    AC_DEFINE(HAVE_LIBRPMIO, 1, [Define to 1 if you have the 'rpmio' library (-lrpmio).])
    WITH_LIBRPMIO_LIB="-lrpmio"
])
AC_SUBST(WITH_LIBRPMIO_LIB)

AC_CHECK_HEADERS(thread.h pthread.h synch.h semaphore.h gelf.h)

dnl Better not use fchmod at all.
AC_CHECK_FUNC(fchmod)

AC_CHECK_FUNCS(vsnprintf snprintf)

dnl Temporary hack for MiNT.  Some functions (writev, snprintf) are
dnl not in the libc but in libport (for political reasons).  This check
dnl can hopefully be removed soon.  Please use the default action
dnl for this macro (not LIBS=...), otherwise the check for dbopen
dnl will fail.
AC_CHECK_LIB(port, writev)

dnl AmigaOS and IXEmul have a fork() dummy
  case "$target" in
    m68k-*-amigaos ) 
	echo "Building for AmigaOS: using vfork() instead of fork()"; 
	CFLAGS="$CFLAGS -Dfork=vfork" 
	;;
  esac

LIBZ=
if test -z "${WITH_ZLIB_LIB}" ; then
for zlib in z gz ; do
   AC_CHECK_LIB(${zlib}, gzread, 
	[LIBZ="-l$zlib"; break], 
	[if test ${zlib} = gz; then 
	    AC_MSG_ERROR([sorry rpm requires libz.a or libgz.a (from the zlib package)]) 
	 fi]
	       )
done
AC_SUBST(LIBZ)

dnl zlib-1.0.4 has not gzseek
AC_CHECK_LIB(${zlib}, gzseek, [AC_DEFINE(HAVE_GZSEEK, 1, [Define as 1 if your zlib has gzseek()])])
fi

LIBPOPT=
AC_CHECK_LIB(popt, poptGetContext, [LIBPOPT="-lpopt"])
AC_SUBST(LIBPOPT)

AC_CHECK_LIB(beecrypt, hashFunctionContextInit, [LIBBEECRYPT="-lbeecrypt"])
AC_SUBST(LIBBEECRYPT)

dnl Red Hat 4.x has not personality
AC_CHECK_FUNCS(personality)

AM_GNU_GETTEXT([external])

dnl Checks for header files we can live without.
AC_HEADER_STDC
AC_HEADER_MAJOR
AC_HEADER_DIRENT
AC_HEADER_TIME

AC_CHECK_HEADERS(fcntl.h getopt.h grp.h memory.h netdb.h pwd.h utime.h)

AC_CHECK_HEADERS(sys/ipc.h sys/socket.h sys/select.h)
AC_CHECK_HEADERS(sys/types.h sys/stdtypes.h)
AC_CHECK_HEADERS(sys/mman.h sys/resource.h sys/utsname.h sys/wait.h)

AC_CHECK_HEADERS(netinet/in_systm.h)
AC_CHECK_HEADERS(machine/types.h)
AC_CHECK_HEADERS(mntent.h sys/mnttab.h sys/systemcfg.h)
AC_CHECK_HEADERS(sys/mount.h sys/mntctl.h sys/param.h sys/vmount.h)
AC_CHECK_HEADERS(zlib.h)
AC_CHECK_HEADERS(err.h mcheck.h syslog.h)
AC_CHECK_HEADERS(locale.h)

dnl popt w float/double needs.
AC_CHECK_HEADERS(float.h)

AC_CHECK_HEADERS(glob.h)

AC_CHECK_TYPES([cookie_io_functions_t],,, [#include <stdio.h>])

dnl statfs portability fiddles.
dnl
dnl We should really emulate/steal sections of the statfs and struct statfs
dnl checks from GNU fileutils.
dnl
AC_MSG_CHECKING(for struct statfs)
dnl
dnl this is easier than nesting AC_TRY_COMPILEs...
dnl
found_struct_statfs=no

if test X$found_struct_statfs = Xno ; then
dnl Solaris 2.6+ wants to use statvfs
AC_TRY_COMPILE([
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#include <sys/statvfs.h> ],
	[struct statvfs sfs;],
	[AC_MSG_RESULT(in sys/statvfs.h)
	AC_DEFINE(STATFS_IN_SYS_STATVFS, 1,
		[statfs in <sys/statvfs.h> (for solaris 2.6+ systems)])
	found_struct_statfs=yes],
)
fi

if test X$found_struct_statfs = Xno ; then
dnl first try including sys/vfs.h
AC_TRY_COMPILE([
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#include <sys/vfs.h> ],
	[struct statfs sfs;],
	[AC_MSG_RESULT(in sys/vfs.h)
	AC_DEFINE(STATFS_IN_SYS_VFS, 1, [statfs in <sys/vfs.h> (for linux systems)])
	found_struct_statfs=yes],
)
fi

if test X$found_struct_statfs = Xno ; then
dnl ...next try including sys/mount.h
AC_TRY_COMPILE([
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <sys/mount.h> ],
	[struct statfs sfs;],
	[AC_MSG_RESULT(in sys/mount.h)
	AC_DEFINE(STATFS_IN_SYS_MOUNT, 1, [statfs in <sys/mount.h> (for Digital Unix 4.0D systems)])
	found_struct_statfs=yes],
)
fi

if test X$found_struct_statfs = Xno ; then
dnl ...still no joy.  Try sys/statfs.h
AC_TRY_COMPILE([
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#include <sys/statfs.h> ],
	[struct statfs sfs;],
	[AC_MSG_RESULT(in sys/statfs.h)
	AC_DEFINE(STATFS_IN_SYS_STATFS, 1, [statfs in <sys/statfs.h> (for Irix 6.4 systems)])
	found_struct_statfs=yes],
)
fi

if test X$found_struct_statfs = Xno ; then
dnl ...no luck.  Warn the user of impending doom.
AC_MSG_WARN(not found)
fi

dnl
dnl if we found the struct, see if it has the f_bavail member.  Some OSes
dnl don't, including IRIX 6.5+
dnl
if test X$found_struct_statfs = Xyes ; then
AC_MSG_CHECKING(for f_bavail member in struct statfs)
AC_TRY_COMPILE([
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if STATFS_IN_SYS_STATVFS
# include <sys/statvfs.h>
  typedef struct statvfs STATFS_t;
#else
  typedef struct statfs STATFS_t;
# if STATFS_IN_SYS_VFS
#  include <sys/vfs.h>
# elif STATFS_IN_SYS_MOUNT
#  include <sys/mount.h>
# elif STATFS_IN_SYS_STATFS
#  include <sys/statfs.h>
# endif
#endif ],
	[STATFS_t sfs;
	sfs.f_bavail = 0;],
	[AC_MSG_RESULT(yes)
	AC_DEFINE(STATFS_HAS_F_BAVAIL, 1, [Define if struct statfs has the f_bavail member])],
	[AC_MSG_RESULT(no)]
)
fi

if test X$found_struct_statfs = Xyes ; then
dnl
dnl now check to see if we have the 4-argument variant of statfs()
dnl this pretty much requires AC_TRY_RUN
dnl
AC_MSG_CHECKING([if statfs() requires 4 arguments])
AC_TRY_RUN([
#if STATFS_IN_SYS_STATVFS
#include <sys/statvfs.h>
#else
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef STATFS_IN_SYS_VFS
#include <sys/vfs.h>
#elif STATFS_IN_SYS_MOUNT
#include <sys/mount.h>
#elif STATFS_IN_SYS_STATFS
#include <sys/statfs.h>
#endif
#endif
int
main() {
	struct statfs sfs;
	return statfs(".", &sfs, sizeof(sfs), 0);
}
],
	[AC_MSG_RESULT(yes)
	AC_DEFINE(STAT_STATFS4, 1, [Define if the statfs() call takes 4 arguments])],
	[AC_MSG_RESULT(no)],
	[AC_MSG_RESULT(no)]
)
fi

AC_C_INLINE

dnl look for libc features
PROVIDES_ERRNO=no
AC_MSG_CHECKING(if <netdb.h> defines h_errno)
AC_TRY_LINK([#include <netdb.h>
	     #include <stdio.h>],printf("%d",h_errno),PROVIDES_ERRNO=yes)
AC_MSG_RESULT($PROVIDES_ERRNO)
if test $PROVIDES_ERRNO = yes; then
	AC_DEFINE(HAVE_HERRNO, 1, [ Define as 1 if <netdb.h> defines h_errno])
fi

dnl If a system doesn't have S_IFSOCK, define it as 0 which will
dnl make S_ISSOCK always return false (nice, eh?)
AC_MSG_CHECKING(if <sys/stat.h> defines S_IFSOCK)
AC_TRY_LINK([#include <sys/stat.h>
	     #include <stdio.h>],printf("%d", S_IFSOCK),
	HAS_S_IFSOCK=yes,HAS_S_IFSOCK=no)
AC_MSG_RESULT($HAS_S_IFSOCK)
if test $HAS_S_IFSOCK = yes; then
	AC_DEFINE(HAVE_S_IFSOCK, 1, [Define as 1 if <sys/stat.h> defines S_IFSOCK])
fi

dnl Some Unix's are missing S_ISLNK, S_ISSOCK
AC_MSG_CHECKING(if <sys/stat.h> defines S_ISLNK)
AC_TRY_LINK([#include <sys/stat.h>
	     #include <stdio.h>],printf("%d", S_ISLNK(0755)),
	HAS_S_ISLNK=yes,HAS_S_ISLNK=no)
AC_MSG_RESULT($HAS_S_ISLNK)
if test $HAS_S_ISLNK = yes; then
	AC_DEFINE(HAVE_S_ISLNK, 1, [Define as 1 if <sys/stat.h> defines S_ISLNK])
fi

AC_MSG_CHECKING(if <sys/stat.h> defines S_ISSOCK)
AC_TRY_LINK([#include <sys/stat.h>
	     #include <stdio.h>],printf("%d", S_ISSOCK(0755)),
	HAS_S_ISSOCK=yes,HAS_S_ISSOCK=no)
AC_MSG_RESULT($HAS_S_ISSOCK)
if test $HAS_S_ISSOCK = yes; then
	AC_DEFINE(HAVE_S_ISSOCK, 1, [Define as 1 if <sys/stat.h> defines S_ISSOCK])
fi

AC_MSG_CHECKING(if timezone is defined)
AC_TRY_LINK([#include <time.h>
	     #include <stdio.h>],printf("%ld", timezone),
      HAS_TIMEZONE=yes,HAS_TIMEZONE=no)
AC_MSG_RESULT($HAS_TIMEZONE)

dnl Check for missing typedefs
AC_TYPE_MODE_T
AC_TYPE_PID_T

dnl Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_VPRINTF

dnl XXX AC_FUNC_MEMCMP
dnl XXX AC_FUNC_MMAP
dnl XXX AC_TYPE_SIGNAL
dnl XXX AC_FUNC_STRCOLL
dnl XXX AC_FUNC_STRFTIME
dnl XXX AC_FUNC_UTIME_NULL
dnl XXX AC_FUNC_VFORK
dnl XXX AC_CHECK_FUNCS(gethostname mkdir mkfifo rmdir select uname)

AC_CHECK_FUNCS(basename getcwd getwd inet_aton mtrace putenv realpath setenv)
AC_CHECK_FUNCS(stpcpy stpncpy strcspn)
AC_CHECK_FUNCS(strdup strerror strtol strtoul strspn strstr)
AC_CHECK_FUNCS(sched_getaffinity, [], [], [#include <sched.h>])

AC_CHECK_FUNCS(regcomp)

AC_CHECK_FUNCS(ftok)

dnl
dnl XXX Glob *is* broken on linux with libc5, solaris and possibly aix when
dnl %files gets something like
dnl	/usr/*/locale/*/LC_MESSAGES/*.mo
dnl (Note: more than one asterisk in glob pattern.)
dnl
AC_ARG_WITH(glob, [  --with-glob             use the internal GNU glob ],
    [rpm_cv_glob=yes],
    [AC_CHECK_FUNCS(glob, rpm_cv_glob=no, rpm_cv_glob=yes)])

if test $rpm_cv_glob = no ; then
    AC_CACHE_CHECK([for GNU extensions to glob], rpm_cv_glob_ext, [
    rm -f t
    mkdir t
    mkdir t/p
    touch t/p/foo.8
    AC_TRY_RUN([
#include <unistd.h>
#include <sys/stat.h>
#include <glob.h>
int
main() {
  glob_t gl ;
  gl.gl_stat = stat;
  return (glob("t/*/*.8", GLOB_PERIOD, NULL, &gl) || gl.gl_pathc < 1);
} ],
    rpm_cv_glob_ext=yes, rpm_cv_glob_ext=no, rpm_cv_glob_ext=no)
    rm -r t
    ])
    if test "$rpm_cv_glob_ext" = no ; then
	echo "    --> using the included GNU glob instead." >&AC_FD_MSG
	rpm_cv_glob=yes
    fi
fi

if test "$rpm_cv_glob" = yes; then
    AC_DEFINE(USE_GNU_GLOB, 1, [Use the included glob.c?])
    AC_LIBOBJ(glob)
    AC_LIBOBJ(fnmatch)
fi

AC_PATH_PROG(__DOXYGEN, doxygen, no, $PATH)
dnl
dnl Auto-detect whether doxygen generated API docs should be included.
dnl
withval=auto
AC_ARG_WITH(apidocs, [  --with-apidocs          build rpm API docs ])

if test $withval = auto -a $__DOXYGEN != no ; then
  withval=yes
elif test $withval = yes -a $__DOXYGEN = no ; then
  AC_MSG_ERROR(--> rpm API docs needs doxygen in PATH)
fi

if test $withval = yes; then
  WITH_APIDOCS_TARGET=apidocs
  WITH_APIDOCS=1
else
  WITH_APIDOCS=0
fi
AC_SUBST(WITH_APIDOCS_TARGET)
AC_SUBST(WITH_APIDOCS)

withval=no
AC_ARG_WITH(dmalloc, [  --with-dmalloc          build with dmalloc debugging support ])
if test $withval = yes ; then
  AC_DEFINE(DMALLOC, 1, [Build with dmalloc support?])
  LIBS="$LIBS -ldmalloc"
fi

WITH_SELINUX_LIB=
AC_ARG_WITH(selinux, [AS_HELP_STRING([--with-selinux],[build with selinux support])],
[case "$with_selinux" in
yes|no) ;;
*) AC_MSG_ERROR([invalid argument to --with-selinux])
 ;;
esac],
[with_selinux=no])

AS_IF([test "$with_selinux" = yes],[
  AC_CHECK_HEADER([selinux/selinux.h],[
    AC_CHECK_LIB(selinux,[is_selinux_enabled],[with_selinux=yes],[
      AC_MSG_ERROR([--with-selinux given, but libselinux not found])])
  ],[
    AC_MSG_ERROR([--with-selinux given, but selinux/selinux.h not found])
  ])
])

AS_IF([test "$with_selinux" = yes],[
  AC_DEFINE(WITH_SELINUX, 1, [Build with selinux support?])
  WITH_SELINUX_LIB="-lselinux"
])
AC_SUBST(WITH_SELINUX_LIB)
AM_CONDITIONAL(SELINUX,[test "$with_selinux" = yes])

# libcap
WITH_CAP_LIB=
AC_ARG_WITH(cap, [  --with-cap              build with capability support ],
[case "$with_cap" in
yes|no) ;;
*) AC_MSG_ERROR([invalid argument to --with-cap])
  ;;
esac],
[with_cap=no])

AS_IF([test "$with_cap" = yes],[
  AC_CHECK_HEADER([sys/capability.h],[
    AC_CHECK_LIB(cap,[cap_get_file],[with_cap=yes],[
      AC_MSG_ERROR([--with-cap given, but libcap not found])])
  ],[
    AC_MSG_ERROR([--with-cap given, but sys/capability.h not found])
  ])
])

AS_IF([test "$with_cap" = yes],[
  AC_DEFINE(WITH_CAP, 1, [Build with capability support?])
  WITH_CAP_LIB="-lcap"
])
AC_SUBST(WITH_CAP_LIB)
AM_CONDITIONAL(CAP,[test "$with_cap" = yes])

AC_CHECK_FUNCS(setlocale)

dnl XXX Solaris <= 2.6 only permits 8 chars in password.
AC_CHECK_FUNCS(getpassphrase)

AC_CHECK_FUNC(getmntent, AC_DEFINE(HAVE_GETMNTENT, 1, [Define if you have the getmntent() function]), [
  AC_CHECK_FUNC(mntctl, AC_DEFINE(HAVE_MNTCTL, 1, [Define as 1 if you have mntctl() (only aix?)]),[
    AC_CHECK_FUNC(getmntinfo_r, AC_DEFINE(HAVE_GETMNTINFO_R, 1, [Define as 1 if you have getmntinfo_r() (only osf?)]), [
      AC_CHECK_LIB(c_r, getmntinfo_r, [LIBS="$LIBS -lc_r"; 
					AC_DEFINE(HAVE_GETMNTINFO_R, 1, [Define as 1 if you have getmntinfo_r() (only osf?)])], [
		 AC_DEFINE([USE_GETMNTENT], 1, [Defined if getmntent replacement is used])
                 AC_LIBOBJ(getmntent)])])])])

AC_CHECK_FUNC(lchown,
   [__CHOWN_RHF="%{__chown} -Rhf"
    __CHGRP_RHF="%{__chgrp} -Rhf"
    AC_DEFINE(HAVE_LCHOWN, 1, [Define as 1 if you have lchown()])],
   [__CHOWN_RHF="%{__chown} -Rf"
    __CHGRP_RHF="%{__chgrp} -Rf"
    dnl Does chown() follow symlinks? This should be a good enough test.
    AC_MSG_CHECKING(whether chown() follows symlinks)
    AC_ARG_ENABLE([broken-chown],
    [  --enable-broken-chown   this system's chown follows symbolic links], 
	    result=$enableval, result=unknown)
    if echo "$build" | grep -E "(aix)|(hpux)|(linux)" > /dev/null ; then
	result=yes
    elif echo "$build" | grep -E "(nextstep)" > /dev/null ; then
	result=no
    fi
    if test $result = unknown; then
	if test `${__ID} | cut -f2 -d\= | cut -f1 -d\(` = 0; then
	    rm -f foo bar
	    touch foo
	    ln -s foo bar
	    ${__CHOWN} 10 bar
	    if test `ls -l foo | awk '{print $3}'` != "root"; then
		result=yes
	    else
		result=no
	    fi
	    ${__RM} -f foo bar
	else
	    AC_MSG_CHECKING((cannot check by non-root user))
	    result=no
	fi
    fi

    AC_MSG_RESULT($result)
    if test $result = yes; then
	    AC_DEFINE(CHOWN_FOLLOWS_SYMLINK, 1, [Define as 1 if chown() follows symlinks and you don't have lchown()])
    fi])
AC_SUBST(__CHOWN_RHF)
AC_SUBST(__CHGRP_RHF)

dnl
dnl figure out what root's primary group is
dnl
AC_MSG_CHECKING(root's primary group)
AC_TRY_RUN([#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

int main()
{
	struct passwd *root = NULL;
	struct group *roots_group = NULL;
	FILE * tempfile = NULL;

	root = getpwuid( (uid_t) 0 );
	if (root != NULL) {
		roots_group = getgrgid(root->pw_gid);
		if (roots_group != NULL) {
			tempfile = fopen("conftest_rootg", "w");
			if (tempfile != NULL) {
				fprintf(tempfile, "%s\n", roots_group->gr_name);
				fclose(tempfile);
				return 0;
			}
		}
	}

	return 1;
}], ROOT_GROUP=`cat conftest_rootg`,
	ROOT_GROUP="root",ROOT_GROUP="root"
)
AC_MSG_RESULT($ROOT_GROUP)
AC_SUBST(ROOT_GROUP)

if test "x$varprefix" = "x"; then
    # For /usr and /usr/local, we want the 'var' directory to go
    # in /var and /var/local respectively. For everything else, 
    # just put the 'var' directory in prefix/var.
    case $prefix in
	/usr | /usr/local )
	    varprefix=`echo $prefix | sed 's/usr/var/'` ;;
	NONE)
 	    varprefix=`echo $ac_default_prefix | sed 's/usr/var/'` ;;
	*) 
	    varprefix=$prefix/var ;;
    esac	
fi
AC_SUBST(varprefix)

if test "x$tmpdir" = "x"; then
    if test -d $varprefix/tmp; then
	tmpdir=$varprefix/tmp
    else
	if test -d /var/tmp; then
	    tmpdir=/var/tmp
	else
	    tmpdir=/tmp
	fi
    fi
fi
AC_SUBST(tmpdir)

if echo "$build_os" | grep sco > /dev/null; then
	echo "hacking things up for sco"
	AC_DEFINE(NEED_STRINGS_H, 1, [Define as one if we need to include <strings.h> (along with <string.h>)])
	AC_DEFINE(HAVE_STRUCT_MNTTAB, 1,
	[Define as 1 if you have "struct mnttab" (only sco?)])
elif echo "$build_os" | grep sunos > /dev/null; then
	echo "hacking things up for sunos"
	CFLAGS="$CFLAGS -D__USE_FIXED_PROTOTYPES__"
	AC_DEFINE(NEED_STRINGS_H, 1, [Define as one if we need to include <strings.h> (along with <string.h>)])
	AC_DEFINE(NEED_MYREALLOC, 1, [Define as 1 if we need myrealloc])
	AC_LIBOBJ(myrealloc)
fi

RPMCANONGNU='-gnu'
RPMCANONGNUEABI='%nil'
#
# get rid of the 4-th tuple, if config.guess returned "linux-gnu" for build_os
#
case "$build_os" in
	*-gnulibc1)
		build_os="${build_os%-gnulibc1}"
		;;
	*-gnu)
		build_os="${build_os%-gnu}"
		;;
	*-gnueabi)
		build_os="${build_os%-gnueabi}"
		RPMCANONGNU='-gnueabi'
		RPMCANONGNUEABI='-gnueabi'
		;;
	*-gnuabin32)
		build_os="${build_os%-gnuabin32}"
		RPMCANONGNU='-gnuabin32'
		RPMCANONGNUEABI='-gnuabin32'
		;;
	*-gnuabi64)
		build_os="${build_os%-gnuabi64}"
		RPMCANONGNU='-gnuabi64'
		RPMCANONGNUEABI='-gnuabi64'
		;;
esac

changequote(<, >)
build_os_exact="${build_os}"
build_os_major=`echo "${build_os}" | sed 's/\..*$//'`
build_os_noversion=`echo "${build_os}" | sed 's/[0-9]*\..*$//'`
changequote([, ])

dnl Determine the canonical arch-vendor-os for the build machine
case "${build_cpu}" in
*86)		RPMCANONARCH=i386 ;;
alpha*)		RPMCANONARCH=alpha ;;
sparc*)		RPMCANONARCH=sparc ;;
ia64*)		RPMCANONARCH=ia64 ;;
s390*)		RPMCANONARCH=s390 ;;
powerpc*)	RPMCANONARCH=ppc ;;
armv3l*)        RPMCANONARCH=armv3l ;;
arm*b)		RPMCANONARCH=armeb ;;
arm*l)		RPMCANONARCH=arm ;;
mipsel*)	RPMCANONARCH=mipsel ;;
mips*)		RPMCANONARCH=mips ;;
m68k*)		RPMCANONARCH=m68k ;;
*)		RPMCANONARCH=unknown ;;
esac
case "${build_os_noversion}" in
mint)		RPMCANONARCH=m68kmint ;;
esac
RPMCANONVENDOR="$build_vendor"
case "${build_vendor}" in
unknown|pc|ibm|redhat|pld|mandrake|conectiva|lvr|alt)
	test -f /etc/redhat-release &&		RPMCANONVENDOR=redhat
	test -f /etc/pld-release &&		RPMCANONVENDOR=pld
	test -f /etc/mandrake-release &&	RPMCANONVENDOR=mandrake
	test -f /etc/conectiva-release &&	RPMCANONVENDOR=conectiva
	test -f /etc/lvr-release &&		RPMCANONVENDOR=lvr
	test -f /etc/altlinux-release &&	RPMCANONVENDOR=alt
	;;
esac
RPMCANONOS="$build_os_noversion"
AC_SUBST(RPMCANONARCH)
AC_SUBST(RPMCANONVENDOR)
AC_SUBST(RPMCANONOS)
AC_SUBST(RPMCANONGNU)
AC_SUBST(RPMCANONGNUEABI)

if test X"$prefix" = XNONE ; then
    usrprefix="$ac_default_prefix"
else
    usrprefix=$prefix
fi
SYSUSERSDIR="`echo ${usrprefix}/lib/sysusers.d`"
AC_DEFINE_UNQUOTED(SYSUSERSDIR, "$SYSUSERSDIR",
	[Full path to sysusers.d(5) directory (usually /usr/lib/sysusers.d)])
AC_SUBST(SYSUSERSDIR)
LOCALEDIR="`echo ${usrprefix}/share/locale`"
AC_DEFINE_UNQUOTED(LOCALEDIR, "$LOCALEDIR",
	[Full path to rpm locale directory (usually /usr/share/locale)])
AC_SUBST(LOCALEDIR)
LIBDIR="`echo $libdir | sed 's-/lib$-/%{_lib}-'`"
AC_SUBST(LIBDIR)
RPMCONFIGDIR="`echo ${usrprefix}/lib/rpm`"
AC_DEFINE_UNQUOTED(RPMCONFIGDIR, "$RPMCONFIGDIR",
	[Full path to rpm global configuration directory (usually /usr/lib/rpm)])
AC_SUBST(RPMCONFIGDIR)

SYSCONFIGDIR="`echo /etc/rpm`"
AC_DEFINE_UNQUOTED(SYSCONFIGDIR, "$SYSCONFIGDIR",
	[Full path to rpm system configuration directory (usually /etc/rpm)])
AC_SUBST(SYSCONFIGDIR)

MACROFILES="${RPMCONFIGDIR}/buildmacros:${RPMCONFIGDIR}/%{_target}/macros:${RPMCONFIGDIR}/macros.d/*:${SYSCONFIGDIR}/macros.specspo:${SYSCONFIGDIR}/macros.cdb:${SYSCONFIGDIR}/macros:${SYSCONFIGDIR}/%{_target}/macros:${SYSCONFIGDIR}/macros.d/*:~/.rpmmacros"
AC_DEFINE_UNQUOTED(MACROFILES, "$MACROFILES",
	[Colon separated paths of macro files to read.])
AC_SUBST(MACROFILES)

LIBRPMRC_FILENAME="${RPMCONFIGDIR}/rpmbuildrc"
AC_DEFINE_UNQUOTED(LIBRPMRC_FILENAME, "$LIBRPMRC_FILENAME",
	[Full path to rpmrc configuration file (usually /usr/lib/rpm/rpmrc)])
AC_SUBST(LIBRPMRC_FILENAME)

VENDORRPMRC_FILENAME="${RPMCONFIGDIR}/${RPMCANONVENDOR}/rpmrc"
AC_DEFINE_UNQUOTED(VENDORRPMRC_FILENAME, "$VENDORRPMRC_FILENAME",
	[Full path to vendor rpmrc configuration file (usually /usr/lib/rpm/vendor/rpmrc)])
AC_SUBST(VENDORRPMRC_FILENAME)

LIBRPMALIAS_FILENAME="${RPMCONFIGDIR}/rpmpopt-${VERSION}"
AC_DEFINE_UNQUOTED(LIBRPMALIAS_FILENAME, "$LIBRPMALIAS_FILENAME",
	[Full path to rpmpopt configuration file (usually /usr/lib/rpm/rpmpopt)])
AC_SUBST(LIBRPMALIAS_FILENAME)
FINDREQUIRES="${RPMCONFIGDIR}/find-requires"
AC_DEFINE_UNQUOTED(FINDREQUIRES, "$FINDREQUIRES",
	[Full path to find-requires script (usually /usr/lib/rpm/find-requires)])
AC_SUBST(FINDREQUIRES)
FINDPROVIDES="${RPMCONFIGDIR}/find-provides"
AC_DEFINE_UNQUOTED(FINDPROVIDES, "$FINDPROVIDES",
	[Full path to find-provides script (usually /usr/lib/rpm/find-provides)])
AC_SUBST(FINDPROVIDES)

testdir="`pwd`/tests"
dnl AC_DEFINE_UNQUOTED(testdir, "$testdir")
AC_SUBST(testdir)

AC_SUBST(INCPATH)

AC_SUBST(RPM)

AC_SUBST(OBJDUMP)

dnl XXX this causes popt to depend on zlib et al
dnl # XXX Propagate -lucb to popt ...
dnl export LIBS INCPATH CONFIG_SITE

AC_OUTPUT([ Doxyfile Makefile rpmrc macros platform rpmpopt
	rpmio/Makefile rpmdb/Makefile lib/Makefile build/Makefile
	autodeps/Makefile
	autodeps/autodep-functions
	autodeps/find-provides
	autodeps/find-requires
	autodeps/find-scriptlet-requires
	tools/Makefile
	scripts/Makefile
	scripts/brp-adjust_libraries
	scripts/brp-alt
	scripts/brp-check_contents
	scripts/brp-cleanup
	scripts/brp-compress
	scripts/brp-debuginfo
	scripts/brp-sign-kmodules
	scripts/brp-fix-perms
	scripts/brp-fixup
	scripts/brp-strip-lto
	scripts/brp-dupe-bin
	scripts/brp-verify_elf
	scripts/brp-verify-info
	scripts/brp-verify-unit
	scripts/compress_files
	autodeps/cpp.req
	autodeps/debuginfo.prov
	autodeps/debuginfo.req
	autodeps/files.req
	autodeps/files.req.files
	scripts/find-lang
	scripts/rpmb-find-package
	scripts/rpmb-functions
	scripts/find-debuginfo-files
	scripts/process-debuginfo
	scripts/process-lto
	scripts/fixup-binconfig
	scripts/fixup-libraries
	scripts/fixup-libtool
	scripts/fixup-pkgconfig
	scripts/fixup-desktop
	scripts/ldd
	autodeps/lib.prov
	autodeps/lib.req
	autodeps/pam.prov
	autodeps/pam.req
	scripts/percolate
	autodeps/pkgconfig.req
	autodeps/pkgconfiglib.req
	autodeps/rpmlib.req
	autodeps/rpmlib.req.files
	autodeps/shebang.req
	autodeps/shell.prov
	autodeps/shell.req
	autodeps/static.req
	autodeps/symlinks.req
	autodeps/systemd-services.req
	autodeps/sysusers.prov
	scripts/verify-elf
	scripts/posttrans-filetriggers
	doc/Makefile doc/manual/Makefile
	doc/ja/Makefile doc/pl/Makefile doc/ru/Makefile doc/sk/Makefile],
  [	echo timestamp > stamp-h.in
  ]
)

chmod +x autodeps/find-provides
chmod +x autodeps/find-requires
chmod +x autodeps/find-scriptlet-requires
chmod +x autodeps/*.prov{,.files}
chmod +x autodeps/*.req{,.files}
chmod +x scripts/brp-adjust_libraries
chmod +x scripts/brp-check_contents
chmod +x scripts/brp-cleanup
chmod +x scripts/brp-compress
chmod +x scripts/brp-debuginfo
chmod +x scripts/brp-sign-kmodules
chmod +x scripts/brp-fix-perms
chmod +x scripts/brp-fixup
chmod +x scripts/brp-strip-lto
chmod +x scripts/brp-dupe-bin
chmod +x scripts/brp-verify_elf
chmod +x scripts/brp-verify-info
chmod +x scripts/brp-verify-unit
chmod +x scripts/fixup-binconfig
chmod +x scripts/fixup-libraries
chmod +x scripts/fixup-libtool
chmod +x scripts/fixup-pkgconfig
chmod +x scripts/fixup-desktop
chmod +x scripts/percolate
rm -f find-provides find-requires
ln -s ${srcdir}/autodeps/find-provides ./find-provides
ln -s ${srcdir}/autodeps/find-requires ./find-requires
