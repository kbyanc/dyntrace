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
 * $kbyanc: dyntrace/dyntrace/target_freebsd.c,v 1.4 2004/12/17 21:45:38 kbyanc Exp $
 */

#include <sys/types.h>

#include <assert.h>
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


static void	 target_region_refresh(target_t targ);
static void	 freebsd_map_parseline(target_t targ, char *line, uint linenum);


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

	/*
	 * XXX should be updated whenever the pc is in an unknown region or
	 *     the traced process calls exec().
	 */
	target_region_refresh(targ);

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
	targ->procname = procfs_get_procname(pid);

	/*
	 * If we couldn't get the process name via procfs, fall back to using
	 * the pid as the process name.
	 */
	if (targ->procname == NULL)
		asprintf(&targ->procname, "%u", pid);

	if (targ->procname == NULL)
		fatal(EX_OSERR, "malloc: %m");

	/* XXX should be updated in realtime */
	target_region_refresh(targ);

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

	free(targ->procname);
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

	start = strtoll(args[0], NULL, 16);
	end = strtoll(args[1], NULL, 16);

	readonly = (strchr(args[5], 'w') == NULL);

	type= REGION_NONTEXT_UNKNOWN;
	if (strcmp(args[11], "vnode") == 0) {
		if (linenum == 0)
			type = REGION_TEXT_PROGRAM;
		else if (strcmp(args[5], "r-x") == 0)
			type = REGION_TEXT_LIBRARY;
	}

	region_update(targ->rlist, start, end, type, readonly);
}

