# $kbyanc: dyntrace/dyntrace/Makefile,v 1.3 2004/12/15 18:06:42 kbyanc Exp $

PROG=		dynprof

PREFIX?=	/usr/local
BINDIR=		${PREFIX}/sbin

SRCS=		log.c \
		main.c \
		optree.c \
		procfs_freebsd.c \
		ptrace.c \
		radix.c \
		region.c

NOMAN=		noman

XML_CFLAGS!=	xml2-config --cflags
XML_LDFLAGS!=	xml2-config --libs

DEBUG_FLAGS+=	-O0 -g
CFLAGS+=	${XML_CFLAGS}
LDFLAGS+=	${XML_LDFLAGS}

NO_WERROR=	yes

WARNS?=		6

.include <bsd.prog.mk>

