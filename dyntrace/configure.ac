#
# Process this file with autoconf to produce a configure script.
#
# $kbyanc: dyntrace/configure.ac,v 1.3 2005/04/27 02:53:46 kbyanc Exp $

AC_PREREQ(2.59)
AC_INIT(dyntrace, 0.1, kbyanc@posi.net)
AM_INIT_AUTOMAKE

AC_CONFIG_SRCDIR([dyntrace/main.c])
AC_CONFIG_HEADERS([dyntrace/config.h])

# Checks for programs.
AC_PROG_CC
AC_PATH_PROGS(PERL, perl perl5 perl5.8)
AC_PATH_PROGS(SH, sh)
AC_PATH_PROGS(XSLTPROC, xsltproc)

# Checks for libraries.
AM_PATH_XML2(2.6.13, , AC_MSG_ERROR(libxml2 must be installed))
AC_CHECK_LIB(pmc, pmc_attach)

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h limits.h paths.h stddef.h stdlib.h string.h sys/param.h sys/time.h unistd.h])
# XXX FreeBSD: sys/mount.h

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_C_VOLATILE

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STRFTIME
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([alarm atexit bzero gettimeofday memchr memset regcomp rmdir strchr strdup strerror strrchr strstr])

AC_CONFIG_FILES([Makefile data/Makefile dyntrace/Makefile tools/Makefile])
AC_OUTPUT
