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
 * $kbyanc: dyntrace/dyntrace/ptrace.c,v 1.8 2004/12/27 04:31:54 kbyanc Exp $
 */

#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include <machine/reg.h>

#include "dyntrace.h"
#include "ptrace.h"


struct ptrace_state {
	enum { ATTACHED, DETACHED, TERMINATED } status;
	pid_t	 pid;
	int	 signum;
};

static bool	 ptrace_initialized = false;

static const char *ptrace_signal_name(int sig);
static void	 ptrace_sig_ignore(int sig);
static ptstate_t ptrace_alloc(pid_t pid);


/*!
 * ptrace_signal_name() - Map signal numbers to signal names.
 *
 *	@param	sig	The signal number to get the name of.
 *
 *	@return pointer to static storage holding the signal name string.
 *
 *	The caller must not try to free() the returned pointer.
 */
const char *
ptrace_signal_name(int sig)
{
	static char buffer[20];
	char *pos;

	buffer[sizeof(buffer) - 1] = '\0';

	if (sig >= 0 && sig < NSIG) {
		snprintf(buffer, sizeof(buffer) - 1, "sig%s", sys_signame[sig]);
		for (pos = buffer; *pos != '\0'; pos++)
			*pos = toupper(*pos);
	} else
		snprintf(buffer, sizeof(buffer) - 1, "signal #%d", sig);

	return buffer;
}


/*!
 * ptrace_init() - Initialize ptrace interface API.
 *
 *	Initializes the local data structures used for interfacing with
 *	the ptrace API.  Installs a SIGCHLD signal handler.
 */
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


/*!
 * ptrace_sig_ignore() - Stub signal handler for ignoring SIGCHLD signals.
 *
 *	Installing a stub no-op signal handler is different than using SIG_IGN
 *	as the action of SIGCHLD.  The former causes the child to stop or
 *	exit such that we can retreive the child's status with the wait(2)
 *	system call whereas the latter prevents us from learning the child's
 *	status altogether.
 */
void
ptrace_sig_ignore(int sig __unused)
{
}


/*!
 * ptrace_alloc() - Internal routine to allocate and initialize a ptrace
 *		    state handle.
 *
 *	@param	pid	The process identifer of the process to be traced.
 *
 *	@return newly-allocated ptrace state handle.
 */
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


/*!
 * ptrace_fork() - 
 *
 *	Wraps the fork(2) system call with additional logic for attaching to
 *	the child process for tracing.  As with fork(2), both the parent
 *	and the child process return; the calling code can distinguish which
 *	process it the child because it will return NULL whereas the parent
 *	will return a non-NULL ptrace handle.
 *
 *	@param	pidp	Pointer to pid_t to populate with the process
 *			identifier of the child process.
 *
 *	@return	NULL to the child process or ptrace state handle for tracing
 *		the child process to the parent process.
 */
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


/*!
 * ptrace_attach() - Attach to an existing process for tracing.
 *
 *	@param	pid	The process identifier to attach to.
 *
 *	@return	ptrace handle for tracing the given process.
 *
 *	If the current process does not have sufficient permissions to trace
 *	the specified target process, an error is logged and the program will
 *	terminate.
 */
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


/*!
 * ptrace_detach() - Stop tracing a process, allowing it to continue running
 *		     as usual.
 *
 *	@param	pts	The ptrace handle for the process to stop tracing.
 *
 *	Detaching from a child process may cause it to terminate on some
 *	platforms.
 */
void
ptrace_detach(ptstate_t pts)
{

	assert(pts->status == ATTACHED);

	if (ptrace(PT_DETACH, pts->pid, (caddr_t)1, pts->signum) < 0)
		warn("failed to detach from %u: %m", pts->pid);
	pts->status = DETACHED;
	pts->signum = 0;
}


/*!
 * ptrace_done() - Free memory allocated to ptrace state handle.
 *
 *	@param	ptsp	Pointer to the ptrace state handle to free.
 *
 *	@post	The value is ptsp points to is invalidated so it cannot be
 *		passed to any ptrace_* routine.
 */
void
ptrace_done(ptstate_t *ptsp)
{
	ptstate_t pts = *ptsp;

	*ptsp = NULL;
	free(pts);
}


/*!
 * ptrace_step() - Single-step the given process by a single instruction.
 *
 *	Allows the process controlled by the given ptrace state handle to
 *	execute a single instruction before stopping.
 *
 *	@param	pts	The ptrace state handle for the process to single-step.
 *
 *	@post	The ptrace_wait() routine should be called to wait for the
 *		process to stop again after executing the instruction.
 */
void
ptrace_step(ptstate_t pts)
{

	assert(pts->status == ATTACHED);

	if (pts->signum != 0) {
		debug("sending %s to %u",
		      ptrace_signal_name(pts->signum), pts->pid);
	}

	if (ptrace(PT_STEP, pts->pid, (caddr_t)1, pts->signum) < 0)
		fatal(EX_OSERR, "ptrace(PT_STEP, %u): %m", pts->pid);
}


/*!
 * ptrace_continue() - Continue the given process' execution.
 *
 *	Allows the process controlled by the given ptrace state handle to
 *	continue execution.  Execution continues until the process receives
 *	a signal or encounters a breakpoint.
 *
 *	@param	pts	The ptrace state handle for the process to unstop.
 */
void
ptrace_continue(ptstate_t pts)
{

	assert(pts->status == ATTACHED);

	if (pts->signum != 0) {
		debug("sending %s to %u",
		      ptrace_signal_name(pts->signum), pts->pid);
	}

	if (ptrace(PT_CONTINUE, pts->pid, (caddr_t)1, pts->signum) < 0)
		fatal(EX_OSERR, "ptrace(PT_CONTINUE, %u): %m", pts->pid);
}


/*!
 * ptrace_wait() - Wait for a process to stop.
 *
 *	Waits for the process controlled by the given state handle to stop.
 *
 *	@param	pts	The ptrace state handle for the process to wait for.
 *
 *	@return	boolean true if the process has stopped; boolean false if the
 *		the process has terminated.
 */
bool
ptrace_wait(ptstate_t pts)
{
	int status;

	while (waitpid(pts->pid, &status, 0) < 0) {
		if (errno != EINTR)
			fatal(EX_OSERR, "waitpid(%u): %m", pts->pid);
	}

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
		warn("pid %u exited on %s", pts->pid,
		     ptrace_signal_name(WTERMSIG(status)));
		pts->status = TERMINATED;
		return false;
	}

	assert(0);
	/* NOTREACHED */
	return true;
}


/*!
 * ptrace_signal() - Send a signal to a process.
 *
 *	@param	pts	The state handle of the process to signal.
 *
 *	@param	signum	The signal number to send to the process.
 *
 *	The specified signal is sent to the process when it resumes execution
 *	either by ptrace_step(), ptrace_continue(), or ptrace_detach().
 */
void
ptrace_signal(ptstate_t pts, int signum)
{

	assert(pts->status == ATTACHED);

	if (signum != SIGTRAP)
		pts->signum = signum;
}


/*!
 * ptrace_getregs() - Get the values in the CPU registers for a process.
 *
 *	@param	pts	The state handle of the process to read the register
 *			values from.
 *
 *	@param	regs	Machine-dependent structure to populate with the
 *			target process' register values.
 *
 *	@pre	The process controlled by the given state handle must be
 *		stopped.
 */
void
ptrace_getregs(ptstate_t pts, struct reg *regs)
{

	assert(pts->status == ATTACHED);

	if (ptrace(PT_GETREGS, pts->pid, (caddr_t)regs, 0) < 0)
		fatal(EX_OSERR, "ptrace(PT_GETREGS, %u): %m", pts->pid);
}


/*!
 * ptrace_setregs() - Set the values of the CPU registers for a process.
 *
 *	@param	pts	The state handle of the process to write the register
 *			values for.
 *
 *	@param	regs	Machine-dependent structure to load the target process'
 *			register values from.
 *
 *	@pre	The process controlled by the given state handle must be
 *		stopped.
 */
void
ptrace_setregs(ptstate_t pts, const struct reg *regs)
{

	assert(pts->status == ATTACHED);

	if (ptrace(PT_SETREGS, pts->pid, __DECONST(caddr_t, regs), 0) < 0)
		fatal(EX_OSERR, "ptrace(PT_GETREGS, %u): %m", pts->pid);
}


/*!
 * ptrace_read() - Read the contents of a process' virtual memory.
 *
 *	@param	pts	The state handle of the process to read from.
 *
 *	@param	addr	The address in the given process' virtual memory to
 *			read.
 *
 *	@param	dest	Pointer to buffer in the current process to read the
 *			memory contents into.
 *
 *	@param	len	The number of bytes to read.
 *
 *	@return	the actual number of bytes read.
 */
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


/*!
 * ptrace_write() - Write the contents of a process' virtual memory.
 *
 *	@param	pts	The state handle of the process to write to.
 *
 *	@param	addr	The address in the process' virtual memory to write to.
 *
 *	@param	src	Pointer to buffer in the current process containing
 *			the data to write.
 *
 *	@param	len	The number of bytes to write.
 */
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
