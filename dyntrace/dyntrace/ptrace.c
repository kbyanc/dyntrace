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
 * $kbyanc: dyntrace/dyntrace/ptrace.c,v 1.3 2004/12/17 04:47:54 kbyanc Exp $
 */

#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include <machine/reg.h>

#include "dynprof.h"
#include "ptrace.h"


struct ptrace_state {
	enum { ATTACHED, DETACHED, TERMINATED } status;
	pid_t	 pid;
	int	 signum;
};

static bool	 ptrace_initialized = false;

static void	 ptrace_sig_ignore(int sig);
static ptstate_t ptrace_alloc(pid_t pid);
static bool	 ptrace_wait(struct ptrace_state *pts);



void
ptrace_init(void)
{

	/*
	 * The traced process receives a SIGTRAP each time it stops under the
	 * control of ptrace(2).  However, as the tracing process, we have
	 * the opportunity to intercept the (fatal) signal if we have a
	 * SIGCHLD handler other than the default SIG_IGN.  Since we wait
	 * for the child to stop with waitpid(2), we install our own SIGCHLD
	 * handler to ignore the signals.
	 */
	setsighandler(SIGCHLD, ptrace_sig_ignore);

	ptrace_initialized = true;
}


void
ptrace_sig_ignore(int sig __unused)
{
}


ptstate_t
ptrace_alloc(pid_t pid)
{
	ptstate_t pts;

	pts = malloc(sizeof(*pts));
	if (pts == NULL)
		fatal(EX_OSERR, "malloc: %m");

	pts->status = DETACHED;
	pts->pid = pid;
	pts->signum = 0;

	return pts;
}


ptstate_t
ptrace_fork(pid_t *pidp)
{
	ptstate_t pts;
	pid_t pid;

	if (!ptrace_initialized)
		ptrace_init();

	pid = fork();
	if (pid < 0)
		fatal(EX_OSERR, "fork: %m");
	if (pid == 0) {
		/*
		 * Child process.
		 * Set ourself up to be traced; a SIGTRAP will be raised on
		 * the first instruction after exec(3)'ing a new process image.
		 */
		if (ptrace(PT_TRACE_ME, 0, 0, 0) < 0)
			fatal(EX_OSERR, "ptrace(PT_TRACE_ME): %m");

		return NULL;
	}

	/*
	 * Parent process.
	 * Wait for the child process to stop (specifically stopped due to
	 * tracing as opposed to SIGSTOP), indicating it is ready to be traced.
	 */
	pts = ptrace_alloc(pid);
	pts->status = ATTACHED;

	if (!ptrace_wait(pts))
		exit(EX_UNAVAILABLE);

	if (pidp != NULL)
		*pidp = pid;
	return pts;
}


ptstate_t
ptrace_attach(pid_t pid)
{
	ptstate_t pts;

	if (!ptrace_initialized)
		ptrace_init();

	if (ptrace(PT_ATTACH, pid, 0, 0) < 0)
		fatal(EX_OSERR, "failed to attach to %u: %m", pid);

	pts = ptrace_alloc(pid);
	pts->status = ATTACHED;

	/* Wait for the traced process to stop. */
	if (!ptrace_wait(pts))
		exit(EX_UNAVAILABLE);

	return pts;
}


void
ptrace_detach(ptstate_t pts)
{

	assert(pts->status == ATTACHED);

	if (ptrace(PT_DETACH, pts->pid, 0, 0) < 0)
		warn("failed to detach from %u: %m", pts->pid);
	pts->status = DETACHED;
	pts->signum = 0;
}


void
ptrace_done(ptstate_t *ptsp)
{
	ptstate_t pts = *ptsp;

	*ptsp = NULL;
	free(pts);
}


bool
ptrace_step(ptstate_t pts)
{

	assert(pts->status == ATTACHED);

	if (pts->signum != 0)
		debug("sending signum %u to %u", pts->signum, pts->pid);

	if (ptrace(PT_STEP, pts->pid, (caddr_t)1, pts->signum) < 0)
		fatal(EX_OSERR, "ptrace(PT_STEP, %u): %m", pts->pid);
	return ptrace_wait(pts);
}


bool
ptrace_continue(ptstate_t pts)
{

	assert(pts->status == ATTACHED);

	if (pts->signum != 0)
		debug("sending signum %u to %u", pts->signum, pts->pid);

	if (ptrace(PT_CONTINUE, pts->pid, (caddr_t)1, pts->signum) < 0)
		fatal(EX_OSERR, "ptrace(PT_CONTINUE, %u): %m", pts->pid);
	return ptrace_wait(pts);
}


bool
ptrace_wait(ptstate_t pts)
{
	int status;

	if (waitpid(pts->pid, &status, 0) < 0)
		fatal(EX_OSERR, "waitpid(%u): %m", pts->pid);

	/*
	 * The normal case is that the process is stopped.  If the process
	 * stopped due to a signal other than SIGTRAP then record that signal
	 * so we can send it to the process when we continue its execution.
	 * SIGTRAPs are generated due to our tracing of the process.
	 */
	if (WIFSTOPPED(status)) {
		pts->signum = WSTOPSIG(status);
		if (pts->signum == SIGTRAP)
			pts->signum = 0;
		return true;
	}

	if (WIFEXITED(status)) {
		warn("pid %u exited with status %u", pts->pid,
		     WEXITSTATUS(status));
		pts->status = TERMINATED;
		return false;
	}

	if (WIFSIGNALED(status)) {
		warn("pid %u exited on signal %u", pts->pid,
		     WTERMSIG(status));
		pts->status = TERMINATED;
		return false;
	}

	assert(0);
	/* NOTREACHED */
	return true;
}


void
ptrace_signal(ptstate_t pts, int signum)
{

	assert(pts->status == ATTACHED);

	pts->signum = signum;
}


void
ptrace_getregs(ptstate_t pts, struct reg *regs)
{

	assert(pts->status == ATTACHED);

	if (ptrace(PT_GETREGS, pts->pid, (caddr_t)regs, 0) < 0)
		fatal(EX_OSERR, "ptrace(PT_GETREGS, %u): %m", pts->pid);
}


void
ptrace_setregs(ptstate_t pts, const struct reg *regs)
{

	assert(pts->status == ATTACHED);

	if (ptrace(PT_SETREGS, pts->pid, __DECONST(caddr_t, regs), 0) < 0)
		fatal(EX_OSERR, "ptrace(PT_GETREGS, %u): %m", pts->pid);
}


size_t
ptrace_read(ptstate_t pts, vm_offset_t addr, void *dest, size_t len)
{
	struct ptrace_io_desc pio;

	assert(pts->status == ATTACHED);
	assert(sizeof(addr) >= sizeof(void *));

	pio.piod_op = PIOD_READ_I;
	pio.piod_offs = (void *)(uintptr_t)addr;
	pio.piod_addr = dest;
	pio.piod_len = len;

	if (ptrace(PT_IO, pts->pid, (caddr_t)&pio, 0) < 0) {
		fatal(EX_OSERR, "ptrace(PT_IO, %u, 0x%08x, %u): %m",
		      pts->pid, addr, len);
	}

	return pio.piod_len;
}


void
ptrace_write(ptstate_t pts, vm_offset_t addr, const void *src, size_t len)
{
	struct ptrace_io_desc pio;

	assert(pts->status == ATTACHED);
	assert(sizeof(addr) >= sizeof(void *));

	while (len > 0) {
		pio.piod_op = PIOD_WRITE_I;
		pio.piod_offs = (void *)(uintptr_t)addr;
		pio.piod_addr = __DECONST(void *, src);
		pio.piod_len = len;

		if (ptrace(PT_IO, pts->pid, (caddr_t)&pio, 0) < 0) {
			fatal(EX_OSERR, "ptrace(PT_IO, %u, 0x%08x, %u): %m",
			      pts->pid, addr, len);
		}

		src = ((const uint8_t *)src) + pio.piod_len;
		addr += pio.piod_len;
		len -= pio.piod_len;
	}
}

