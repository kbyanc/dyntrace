# $kbyanc: dyntrace/tools/Makefile.am,v 1.1 2005/03/02 05:15:01 kbyanc Exp $

dist_bin_SCRIPTS=	opfreqs		\
			opgroup		\
			oplist-to-html	\
			trace-to-html

CLEANFILES=		$(dist_bin_SCRIPTS)
SUFFIXES=		.sh .pl

do_subst=		sed -e 's,/usr/bin/perl,$(PERL),g'	\
			    -e 's,/bin/sh,$(SH),g'		\
			    -e 's,xsltproc,$(XSLTPROC),g'

.sh:
	$(do_subst) < $< > $@
	chmod +x $@

.pl:
	$(do_subst) < $< > $@
	chmod +x $@
