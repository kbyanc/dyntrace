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
 * $kbyanc: dyntrace/dyntrace/dyntrace.h,v 1.2 2004/11/28 10:37:56 kbyanc Exp $
 */

#ifndef _INCLUDE_DYNPROF_H
#define	_INCLUDE_DYNPROF_H

#include <sys/cdefs.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef __GNUC__
#define __attribute__()
#endif

#undef __DECONST
#define __DECONST(type, var)	((type)(uintptr_t)(const void *)(var))


#define	INSTRUCTION_MAXLEN	16


struct reg;	/* Defined in <machine/reg.h> */


typedef struct ptrace_state *ptstate_t;


extern bool	 opt_debug;

#define debug(fmt, ...) do {			\
	if (opt_debug) warn(fmt, __VA_ARGS__);	\
} while (0)


__BEGIN_DECLS
extern void	 warn(const char *fmt, ...)
			__attribute__ ((format (printf, 1, 2)));
extern void	 fatal(int eval, const char *fmt, ...)
			__attribute__ ((noreturn, format (printf, 2, 3)));

extern void	 optree_parsefile(const char *filepath);
extern void	 optree_update(const uint8_t *pc, size_t len, uint cycles);
extern void	 optree_output(FILE *f);


extern ptstate_t ptrace_fork(void);
extern ptstate_t ptrace_attach(pid_t pid);
extern void	 ptrace_detach(ptstate_t pts);
extern void	 ptrace_done(ptstate_t *ptsp);
extern bool	 ptrace_step(ptstate_t pts);
extern bool	 ptrace_continue(ptstate_t pts);
extern void	 ptrace_signal(ptstate_t pts, int signum);
extern void	 ptrace_getregs(ptstate_t pts, struct reg *regs);
extern void	 ptrace_setregs(ptstate_t pts, const struct reg *regs);
extern void	 ptrace_read(ptstate_t pts, vm_offset_t addr,
			     void *dest, size_t len);
extern void	 ptrace_write(ptstate_t pts, vm_offset_t addr,
			      const void *src, size_t len);

__END_DECLS

#endif
