# $kbyanc: dyntrace/dyntrace/Makefile,v 1.7 2004/12/23 01:45:19 kbyanc Exp $

PROG=		dyntrace

PREFIX?=	/usr/local
BINDIR=		${PREFIX}/bin
MANDIR?=	${PREFIX}/man/man

SRCS=		log.c \
		main.c \
		optree.c \
		procfs_freebsd.c \
		ptrace.c \
		radix.c \
		region.c \
		target_freebsd.c

MAN=		dyntrace.1             

XML_CFLAGS!=	xml2-config --cflags
XML_LDFLAGS!=	xml2-config --libs

DEBUG_FLAGS+=	-O0 -g
CFLAGS+=	${XML_CFLAGS}
LDFLAGS+=	${XML_LDFLAGS}

# Flags for enabling PMC support 
#CFLAGS+=	-DHAVE_PMC=1
#LDADD+=	-lpmc

NO_WERROR=	yes

WARNS?=		6

.include <bsd.prog.mk>

