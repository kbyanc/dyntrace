# $kbyanc: dyntrace/dyntrace/Makefile,v 1.5 2004/12/22 09:03:17 kbyanc Exp $

PROG=		dynprof

PREFIX?=	/usr/local
BINDIR=		${PREFIX}/sbin
MANDIR?=	${PREFIX}/man/man

SRCS=		log.c \
		main.c \
		optree.c \
		procfs_freebsd.c \
		ptrace.c \
		radix.c \
		region.c \
		target_freebsd.c

MAN=		dynprof.1             

XML_CFLAGS!=	xml2-config --cflags
XML_LDFLAGS!=	xml2-config --libs

DEBUG_FLAGS+=	-O0 -g
CFLAGS+=	${XML_CFLAGS}
LDFLAGS+=	${XML_LDFLAGS}

NO_WERROR=	yes

WARNS?=		6

.include <bsd.prog.mk>

