# $kbyanc: dyntrace/dyntrace/Makefile.am,v 1.1 2005/03/04 04:47:03 kbyanc Exp $

bin_PROGRAMS=		dyntrace		

dyntrace_SOURCES=	log.c \
			main.c \
			optree.c \
			procfs_freebsd.c \
			ptrace.c \
			radix.c \
			region.c \
			target_freebsd.c

dyntrace_CPPFLAGS=	$(XML_CPPFLAGS)
dyntrace_LDFLAGS=	$(XML_LIBS)

man1_MANS=		dyntrace.1             
