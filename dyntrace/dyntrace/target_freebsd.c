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
 * $kbyanc: dyntrace/dyntrace/target_freebsd.c,v 1.8 2004/12/23 01:45:19 kbyanc Exp $
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/event.h>
#include <sys/sysctl.h>

#include <assert.h>
#include <inttypes.h>
#include <libgen.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#if HAVE_PMC
#include <pmc.h>
#if __i386__
#include <machine/cpufunc.h>		/* for rdpmc() */
#endif
#endif

#include <machine/reg.h>

#include "dyntrace.h"
#include "procfs.h"
#include "ptrace.h"


struct target_state {
	pid_t		 pid;		/* process identifier. */
	int		 pfs_map;	/* procfs map file descriptor. */
	ptstate_t	 pts;		/* ptrace(2) state. */
	region_list_t	 rlist;		/* memory regions in process VM. */

#if HAVE_PMC
	pmc_id_t	 pmc;		/* handle for PMC for cycle counts. */
	uint32_t	 pmc_regnum;	/* x86 MSR number. */
	pmc_value_t	 cycles;	/* cycle counter. */
#endif

	char		*procname;
};


static bool	 pmc_avail = false;
static const char *pmc_eventname = NULL;

static vm_offset_t stack_top;
static int	 kq;

/* Currently, we only support tracing a single process. */
static target_t	 tracedproc = NULL;

static target_t	 target_new(pid_t pid, ptstate_t pts, char *procname);
static void	 target_region_refresh(target_t targ);
static void	 freebsd_map_parseline(target_t targ, char *line, uint linenum);


void
target_init(void)
{
	size_t len;
	int mib[2];

	kq = kqueue();
	if (kq < 0)
		fatal(EX_OSERR, "kqueue: %m");

#if HAVE_PMC
	pmc_avail = (pmc_init() >= 0);
	if (pmc_avail) {
		const struct pmc_op_getcpuinfo *cpuinfo;

		if (pmc_cpuinfo(&cpuinfo) < 0)
			fatal(EX_OSERR, "pmc_cpuinfo: %m");

		switch (cpuinfo->pm_cputype) {
		case PMC_CPU_INTEL_PIV:
			pmc_eventname = "p4-global-power-events,usr";
			break;

		case PMC_CPU_INTEL_PPRO:
			pmc_eventname = "ppro-cpu-clk-unhalted,usr";
			break;

		default:
			pmc_avail = false;
		}
	}
#endif
	if (!pmc_avail)
		warn("pmc unavailable; instruction timing disabled");
	
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

	/*
	 * Query the top-of-stack address.  We can use this information to
	 * identify the main process stack in the process's region list.
	 */
	mib[0] = CTL_KERN;
	mib[1] = KERN_USRSTACK;
	len = sizeof(stack_top);
	sysctl(mib, 2, &stack_top, &len, NULL, 0);
}


void
target_done(void)
{
}


target_t
target_new(pid_t pid, ptstate_t pts, char *procname)
{
	struct kevent kev;
	target_t targ;

	targ = calloc(1, sizeof(*targ));
	if (targ == NULL)
		fatal(EX_OSERR, "malloc: %m");

	targ->pid = pid;
	targ->pts = pts;
	targ->pfs_map = procfs_map_open(pid);
	targ->rlist = region_list_new();
	targ->procname = procname;

	assert(tracedproc == NULL);
	tracedproc = targ;

	target_region_refresh(targ);

	/*
	 * Request notification whenever the process executes a new image.
	 * This is necessary so we can flush the region cache.
	 */
	EV_SET(&kev, pid, EVFILT_PROC, EV_ADD, NOTE_EXEC, 0, targ);
	if (kevent(kq, &kev, 1, NULL, 0, NULL) < 0)
		fatal(EX_OSERR, "kevent: %m");

#if HAVE_PMC
	if (pmc_avail) {
		if (pmc_allocate(strdup(pmc_eventname),
				 PMC_MODE_TC, 0, PMC_CPU_ANY, &targ->pmc) < 0)
			fatal(EX_OSERR, "pmc_allocate: %m");
		if (pmc_attach(targ->pmc, pid) < 0)
			fatal(EX_OSERR, "pmc_attach: %m");
		if (pmc_rw(targ->pmc, 0, &targ->cycles) < 0)
			fatal(EX_OSERR, "pmc_rw: %m");
#if __i386__
		/*
		 * On the x86 line of chips (Pentium and later) we can read
		 * the performance counter from userland using the rdpmc
		 * instruction, eliminating a context switch.  We just need
		 * to know which register our performance counter is in...
		 */
		if (pmc_i386_get_msr(targ->pmc, &targ->pmc_regnum) < 0)
			fatal(EX_OSERR, "pmc_i386_get_msr: %m");
#endif
		if (pmc_start(targ->pmc) < 0)
			fatal(EX_OSERR, "pmc_start: %m");
	}
#endif

	return targ;
}


target_t
target_execvp(const char *path, char * const argv[])
{
	char *procname;
	ptstate_t pts;
	pid_t pid;

	pts = ptrace_fork(&pid);
	if (pts == NULL) {
		/* Child process. */
		execvp(path, argv);
		fatal(EX_OSERR, "failed to execute \"%s\": %m", path);
	}

	procname = strdup(basename(path));
	if (procname == NULL)
		fatal(EX_OSERR, "malloc: %m");

	return target_new(pid, pts, procname);
}


target_t
target_attach(pid_t pid)
{
	char *procname;
	ptstate_t pts;

	pts = ptrace_attach(pid);

	/*
	 * Try to use procfs to get the process name.  Failing that, fall back
	 * to using the pid as the process name.
	 */
	procname = procfs_get_procname(pid);
	if (procname == NULL)
		asprintf(&procname, "%u", pid);
	if (procname == NULL)
		fatal(EX_OSERR, "malloc: %m");

	return target_new(pid, pts, procname);
}


void
target_detach(target_t *targp)
{
	struct kevent kev;
	target_t targ = *targp;

	*targp = NULL;

#if HAVE_PMC
	if (pmc_avail) {
		pmc_stop(targ->pmc);
		pmc_release(targ->pmc);
	}
#endif

	EV_SET(&kev, targ->pid, EVFILT_PROC, EV_DELETE, NOTE_EXEC, 0, NULL);
	kevent(kq, &kev, 1, NULL, 0, NULL);	/* Not fatal if fails. */

	ptrace_detach(targ->pts);
	ptrace_done(&targ->pts);
	procfs_map_close(&targ->pfs_map);
	region_list_done(&targ->rlist);

	free(targ->procname);
	free(targ);

	tracedproc = NULL;
}


target_t
target_wait(void)
{
	static struct kevent events[1];
	static int nevents = 0;
	static struct kevent *kevp;
	static const struct timespec timeout = {0, 0};
	target_t targ;

	if (nevents == 0) {
		nevents = kevent(kq, NULL, 0, events, 1, &timeout);
		if (nevents < 0)
			fatal(EX_OSERR, "kevent: %m");
		kevp = events;
	}

	if (nevents > 0) {
		assert(kevp->filter == EVFILT_PROC);

		targ = kevp->udata;
		assert(targ == tracedproc);

		if ((kevp->fflags & NOTE_EXEC) != 0) {
			/*
			 * The traced process loaded a new process image so
			 * we need to invalidate the cache of the old image.
			 * Note that it is critical that we completely free
			 * the old region list and build a fresh one; just
			 * calling target_region_refresh() is not enough.
			 */
			region_list_done(&targ->rlist);
			targ->rlist = region_list_new();
			target_region_refresh(targ);
		}

		kevp++;
		nevents--;
	}

	return ptrace_wait(tracedproc->pts) ? tracedproc : NULL;
}


void
target_step(target_t targ)
{
	ptrace_step(targ->pts);
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


uint
target_get_cycles(target_t targ)
{

#ifdef HAVE_PMC
	if (pmc_avail) {
		pmc_value_t cycles_prev = targ->cycles;

#if __i386__
		targ->cycles = rdpmc(targ->pmc_regnum);
#else
		if (pmc_read(targ->pmc, &targ->cycles) < 0)
			fatal(EX_OSERR, "pmc_read: %m");
#endif
		assert(targ->cycles >= cycles_prev);
		return (targ->cycles - cycles_prev);
	}
#endif

	(void)targ;	/* Silence gcc warning when !HAVE_PMC. */
	return 0;
}


const char *
target_get_name(target_t targ)
{
	return targ->procname;
}


region_t
target_get_region(target_t targ, vm_offset_t addr)
{
	region_t region;

	region = region_lookup(targ->rlist, addr);
	if (region != NULL)
		return region;

	debug("refreshing region list; addr = 0x%08x", addr);

	target_region_refresh(targ);
	region = region_lookup(targ->rlist, addr);
	assert(region != NULL);
	return region;
}


void
target_region_refresh(target_t targ)
{
	char *pos, *endl;
	char *mapbuf;
	size_t maplen;
	uint linenum;

	if (targ->pfs_map < 0) {
		region_update(targ->rlist, 0, -1, REGION_UNKNOWN, false);
		return;
	}

	procfs_map_read(targ->pfs_map, &mapbuf, &maplen);
	assert(mapbuf != NULL);
	assert(mapbuf[maplen - 1] == '\n');

	linenum = 0;
	pos = mapbuf;
	while (maplen > 0) {
		endl = memchr(pos, '\n', maplen);
		if (endl == NULL)
			break;
		*endl = '\0';

		freebsd_map_parseline(targ, pos, linenum);

		/* Advance to next line in map output. */
		linenum++;
		endl++;
		maplen -= endl - pos;
		pos = endl;
	}
}


void
freebsd_map_parseline(target_t targ, char *line, uint linenum)
{
	char *args[20];
	vm_offset_t start, end;
	region_type_t type;
	bool readonly;
	int i;

	memset(args, 0, sizeof(args));

	i = 0;
	while (i < 20 && (args[i] = strsep(&line, " \t")) != NULL) {
		if (*args[i] != '\0')
			i++;
	}

	/* start = args[0]; */
	/* end   = args[1]; */
	/* perms = args[5]; (eg. rwx, r-x, ...) */
	/* type  = args[11]; (e.g. vnode) */
	/* path  = args[12]; */

	/* We aren't interested in regions that are not executable. */
	if (strchr(args[5], 'x') == NULL)
		return;

	readonly = (strchr(args[5], 'w') == NULL);

	start = strtoll(args[0], NULL, 16);
	end = strtoll(args[1], NULL, 16);

	type= REGION_NONTEXT_UNKNOWN;
	if (strcmp(args[11], "vnode") == 0) {
		if (linenum == 0)
			type = REGION_TEXT_PROGRAM;
		else if (strcmp(args[5], "r-x") == 0)
			type = REGION_TEXT_LIBRARY;
	}
	else if (end == stack_top)
		type = REGION_STACK;

	region_update(targ->rlist, start, end, type, readonly);
}

