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
 * $kbyanc: dyntrace/dyntrace/target_freebsd.c,v 1.2 2004/12/17 07:05:36 kbyanc Exp $
 */

#include <sys/types.h>

#include <inttypes.h>
#include <libgen.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include <machine/reg.h>

#include "dynprof.h"
#include "procfs.h"
#include "ptrace.h"


struct target_state {
	pid_t		 pid;		/* process identifier. */
	ptstate_t	 pts;		/* ptrace(2) state. */
	int		 pfs_map;	/* procfs map file descriptor. */
	region_list_t	 rlist;		/* memory regions in process VM. */
	char		*procname;
};



void
target_init(void)
{

	/*
	 * FreeBSD always includes ptrace(2) support so we use for as much as
	 * possible.  Currently, this includes process control, fetching
	 * registers, and reading the target process's address space.
	 */
	ptrace_init();

	/*
	 * However, ptrace(2) does not provide a means to describe the target
	 * process's address space.  For that, we use procfs.  However, there
	 * is no guarantee that procfs is available; in which case we need to
	 * warn the user that we cannot differentiate the type of the various
	 * regions of the address space.
	 * Note: The kvm(3) interface could be used instead as it is always
	 *	 available, but that runs the risk of breaking if the kernel
	 *       data structures change.
	 */
	if (!procfs_init())
		warn("procfs unavailable; region differentiation disabled");
}


void
target_done(void)
{
}


target_t
target_execvp(const char *path, char * const argv[])
{
	target_t targ;
	ptstate_t pts;
	pid_t pid;

	pts = ptrace_fork(&pid);
	if (pts == NULL) {
		/* Child process. */
		execvp(path, argv);
		fatal(EX_OSERR, "failed to execute \"%s\": %m", path);
	}

	targ = calloc(1, sizeof(*targ));
	if (targ == NULL)
		fatal(EX_OSERR, "malloc: %m");

	targ->pid = pid;
	targ->pts = pts;
	targ->pfs_map = procfs_map_open(pid);
	targ->rlist = region_list_new();
	targ->procname = strdup(basename(path));
	if (targ->procname == NULL)
		fatal(EX_OSERR, "malloc: %m");

	return targ;
}


target_t
target_attach(pid_t pid)
{
	target_t targ;
	ptstate_t pts;

	pts = ptrace_attach(pid);

	targ = calloc(1, sizeof(*targ));
	if (targ == NULL)
		fatal(EX_OSERR, "malloc: %m");

	targ->pid = pid;
	targ->pts = pts;
	targ->pfs_map = procfs_map_open(pid);
	targ->rlist = region_list_new();
//	targ->procname = XXX;

	if (targ->procname == NULL)
		asprintf(&targ->procname, "%u", pid);

	return targ;
}


void
target_detach(target_t *targp)
{
	target_t targ = *targp;

	*targp = NULL;

	ptrace_detach(targ->pts);
	ptrace_done(&targ->pts);
	procfs_map_close(&targ->pfs_map);
	region_list_done(&targ->rlist);

	free(targ);
}


bool
target_step(target_t targ)
{
	return ptrace_step(targ->pts);
}


size_t
target_read(target_t targ, vm_offset_t addr, void *dest, size_t len)
{
	return ptrace_read(targ->pts, addr, dest, len);
}


vm_offset_t
target_get_pc(target_t targ)
{
	struct reg regs;

	ptrace_getregs(targ->pts, &regs);
	return regs.r_eip;
}


const char *
target_get_name(target_t targ)
{
	return targ->procname;
}


region_t
target_get_region(target_t targ, vm_offset_t addr)
{
	return region_lookup(targ->rlist, addr);
}
