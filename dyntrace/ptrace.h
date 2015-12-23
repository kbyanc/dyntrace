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
 * $kbyanc: dyntrace/dyntrace/ptrace.h,v 1.3 2004/12/23 01:45:19 kbyanc Exp $
 */

#ifndef _INCLUDE_DYNTRACE_PTRACE_H
#define	_INCLUDE_DYNTRACE_PTRACE_H

#include <sys/cdefs.h>
#include <stdbool.h>

struct reg;	/* Defined in <machine/reg.h> */

typedef struct ptrace_state *ptstate_t;


__BEGIN_DECLS

extern void	 ptrace_init(void);
extern ptstate_t ptrace_fork(pid_t *pidp);
extern ptstate_t ptrace_attach(pid_t pid);
extern void	 ptrace_detach(ptstate_t pts);
extern void	 ptrace_done(ptstate_t *ptsp);
extern void	 ptrace_step(ptstate_t pts);
extern void	 ptrace_continue(ptstate_t pts);
extern bool	 ptrace_wait(ptstate_t pts);
extern void	 ptrace_signal(ptstate_t pts, int signum);
extern void	 ptrace_getregs(ptstate_t pts, struct reg *regs);
extern void	 ptrace_setregs(ptstate_t pts, const struct reg *regs);
extern size_t	 ptrace_read(ptstate_t pts, vm_offset_t addr,
			     void *dest, size_t len);
extern void	 ptrace_write(ptstate_t pts, vm_offset_t addr,
			      const void *src, size_t len);

__END_DECLS

#endif
