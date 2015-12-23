
OVERVIEW
==========

  Dyntrace is an instruction-level dynamic trace tool primarily intended
to aid in workload characterization of processor-bound programs.

DOCUMENTATION
===============

  Primary documentation is located in the doc/ subdirectory.

  - doc/Report.doc - Detailed report of the function and operation of
                     the dyntrace tool.
  - doc/User's Manual.pdf - Pre-rendered man page for the dyntrace tool.

DEPENDENCIES
==============

  The following packages are required to build/use dyntrace and its
associated tools:

	Package		Minimum Version		FreeBSD Port
	----------------------------------------------------
	libxml2		2.6.19			textproc/libxml2
	libxslt		1.1.14			textproc/libxslt
	perl5		5.8.0			lang/perl5.8

  The following additional packages are required to build distfiles:

	Package		Minimum Version		FreeBSD Port
	----------------------------------------------------
	autoconf	2.59			devel/gnu-autoconf
	automake	1.85			devel/gnu-automake

  To build a distfile, run the make-distfile script located under package
subdirectory.  It will checkout the latest sources from CVS, build the
configure script and Makefiles, and tar them all up into a
ready-to-distribute tar-ball in the current directory.

