# $kbyanc: dyntrace/configure.ac,v 1.1 2005/03/02 05:14:58 kbyanc Exp $

AC_INIT(dyntrace, 0.1, kbyanc@posi.net)
AM_INIT_AUTOMAKE

AC_PROG_CC
AC_PATH_PROGS(PERL, perl perl5 perl5.8)
AC_PATH_PROGS(SH, sh)
AC_PATH_PROGS(XSLTPROC, xsltproc)

AM_PATH_XML2(2.6.13, , AC_MSG_ERROR(libxml2 must be installed))

AC_OUTPUT(Makefile data/Makefile tools/Makefile)
