/*
 * Copyright (c) 2004 Kelly Yancey
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $kbyanc: dyntrace/dyntrace/dyntrace.h,v 1.6 2004/12/15 18:06:42 kbyanc Exp $
 */

#ifndef _INCLUDE_DYNPROF_H
#define	_INCLUDE_DYNPROF_H

#include <sys/cdefs.h>
#include <sys/queue.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef __GNUC__
#define __attribute__()
#endif

#undef __DECONST
#define __DECONST(type, var)	((type)(uintptr_t)(const void *)(var))


struct reg;	/* Defined in <machine/reg.h> */


typedef struct ptrace_state *ptstate_t;
typedef struct procfs_state *pfstate_t;

typedef enum {
	REGION_UNKNOWN		= 0,
	REGION_TEXT_UNKNOWN	= 1,
	REGION_TEXT_PROGRAM	= 2,
	REGION_TEXT_LIBRARY	= 3,
	REGION_NONTEXT_UNKNOWN	= 4,
	REGION_DATA		= 5,
	REGION_STACK		= 6
} region_type_t;

#define	REGION_IS_TEXT(rt)	((rt) < REGION_NONTEXT_UNKNOWN)


typedef struct region_info *region_t;
typedef LIST_HEAD(region_list, region_info) region_list_t;


struct procinfo {
	pid_t		 pid;		/* process identifier. */
	ptstate_t	 pts;		/* ptrace(2) state. */
	pfstate_t	 pfs;		/* procfs state. */
	region_list_t	 region_list;
};


extern bool	 opt_debug;
extern bool	 opt_printzero;
extern char	*opt_outfile;

#define debug(fmt, ...) do {			\
	if (opt_debug) warn(fmt, __VA_ARGS__);	\
} while (0)


__BEGIN_DECLS
extern void	 warn(const char *fmt, ...)
			__attribute__ ((format (printf, 1, 2)));
extern void	 fatal(int eval, const char *fmt, ...)
			__attribute__ ((noreturn, format (printf, 2, 3)));


extern region_t	 region_lookup(struct procinfo *proc, vm_offset_t offset);
extern void	 region_insert(struct procinfo *proc,
			       vm_offset_t start, vm_offset_t end,
			       region_type_t type, bool readonly);
extern size_t	 region_read(struct procinfo *proc, region_t region,
			     vm_offset_t offset, void *dest, size_t len);


extern void	 optree_parsefile(const char *filepath);
extern void	 optree_update(struct procinfo *proc, vm_offset_t pc,
			       uint cycles);
extern void	 optree_output_open(void);
extern void	 optree_output(void);


extern ptstate_t ptrace_fork(void);
extern ptstate_t ptrace_attach(pid_t pid);
extern void	 ptrace_detach(ptstate_t pts);
extern void	 ptrace_done(ptstate_t *ptsp);
extern bool	 ptrace_step(ptstate_t pts);
extern bool	 ptrace_continue(ptstate_t pts);
extern void	 ptrace_signal(ptstate_t pts, int signum);
extern void	 ptrace_getregs(ptstate_t pts, struct reg *regs);
extern void	 ptrace_setregs(ptstate_t pts, const struct reg *regs);
extern size_t	 ptrace_read(ptstate_t pts, vm_offset_t addr,
			     void *dest, size_t len);
extern void	 ptrace_write(ptstate_t pts, vm_offset_t addr,
			      const void *src, size_t len);


extern pfstate_t procfs_open(pid_t pid);
extern void	 procfs_done(pfstate_t *pfsp);
extern size_t	 procfs_read(pfstate_t pfs, vm_offset_t addr,
			     void *dest, size_t len);
#if 0
extern char	*procfs_get_progname(pfstate_t pfs);
#endif
			     

extern void	 procfs_map_load(struct procinfo *proc);

__END_DECLS

#endif
