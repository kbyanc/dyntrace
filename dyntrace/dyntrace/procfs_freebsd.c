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
 * $kbyanc: dyntrace/dyntrace/procfs_freebsd.c,v 1.3 2004/12/17 07:05:36 kbyanc Exp $
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/mount.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "dynprof.h"
#include "procfs.h"


/*
 * On FreeBSD 4, the vfsconf structure exported to userland applications is
 * the same as the structure that the kernel uses.  All later versions export
 * a distinct xvfsconf to userland.
 */
#if __FreeBSD__ == 4
#define xvfsconf vfsconf
#endif


static bool	 procfs_initialized = false;
static char	*procfs_path = NULL;

static bool	 procfs_isavailable(void);
static bool	 procfs_ismounted(char **mountpointp);
static bool	 procfs_isaccessable(const char *path);
static int	 procfs_opennode(const char *procfs, pid_t pid,
				 const char *node);
static bool	 procfs_mount(const char *path);
static void	 procfs_unmount(void);
static void	 procfs_rmtmpdir(void);

static int	 procfs_common_open(pid_t pid, const char *node);
static void	 procfs_common_close(int *fdp);



bool
procfs_init(void)
{

	/*
	 * If procfs_init() has already been called, then procfs_path will be
	 * non-null (the path where we can access procfs) if procfs is
	 * available.
	 */
	if (procfs_initialized)
		return procfs_path != NULL;
	procfs_initialized = true;

	/*
	 * Check to see if the kernel supports procfs and if it is mounted
	 * somewhere accessable.
	 */
	if (!procfs_isavailable())
		return false;

	if (procfs_ismounted(&procfs_path))
		return true;

	/*
	 * Procfs is available, but not already mounted.  Create a temporary
	 * directory and try to mount procfs there.
	 */
	procfs_path = strdup(_PATH_TMP "dynprof.XXXXXX");
	if (mkdtemp(procfs_path) == NULL) {
		warn("failed to create directory %s to mount procfs: %m",
		     procfs_path);
		free(procfs_path);
		procfs_path = NULL;
		return false;
	}

	if (!procfs_mount(procfs_path)) {
		procfs_rmtmpdir();
		free(procfs_path);
		procfs_path = NULL;
		return false;
	}

	warn("procfs temporarily mounted on %s", procfs_path);

	/*
	 * Make sure we clean up after ourselves when we are done.
	 * Note that atexit() handlers are called in reverse order.
	 */
	atexit(procfs_rmtmpdir);
	atexit(procfs_unmount);

	return true;
}


bool
procfs_isavailable(void)
{
	struct xvfsconf vfc;

	/*
	 * Check to see if the running kernel has support for procfs.
	 */
	if (getvfsbyname("procfs", &vfc) == 0)
		return true;

#if __FreeBSD__ == 4
	/*
	 * The kernel does not support procfs; try to load it as a module.
	 * Only necessary for FreeBSD 4 as FreeBSD 5 and later kernels will
	 * load filesystem modules automatically when they are mounted.
	 * This can only succeed if the user has root privileges.
	 * XXX There is no vfsunload() to unload the module when we are done.
	 */
	if (!vfsisloadable("procfs") || vfsload("procfs") == 0) {
		warn("procfs is not available: %m");
		return false;
	}

	warn("loaded procfs");
#endif

	return true;
}


bool
procfs_ismounted(char **mountpointp)
{
	struct xvfsconf vfc;
	struct statfs *fsinfo;
	size_t bufsize;
	int nummounts;
	int i;

	*mountpointp = NULL;

	/*
	 * Ensure the procfs filesystem is really available.
	 */
	if (getvfsbyname("procfs", &vfc) != 0)
		fatal(EX_OSERR, "getvfsbyname(\"procfs\"): %m");

	/*
	 * First, call getfsstat() to get the number of mounted filesystems.
	 */
	nummounts = getfsstat(NULL, 0, MNT_NOWAIT);
	if (nummounts < 0)
		fatal(EX_OSERR, "getfsstat: %m");

	if (nummounts == 0)
		return false;

	/*
	 * Fetch all of the mounted filesystems.  We allocate the buffer
	 * one entry larger than getfsstat() said we needed just in case.
	 */
	bufsize = (nummounts + 1) * sizeof(struct statfs);
	fsinfo = malloc(bufsize);
	if (fsinfo == NULL)
		fatal(EX_OSERR, "malloc: %m");

	nummounts = getfsstat(fsinfo, bufsize, MNT_NOWAIT);
	if (nummounts < 0)
		fatal(EX_OSERR, "getfsstat: %m");

	/*
	 * Scan the list of mounted filesystems for a procfs filesystem we
	 * have access to.  We verify access by trying to open the 'mem'
	 * node corresponding to our own pid.
	 */
	for (i = 0; i < nummounts; i++) {
		const struct statfs *fs = &fsinfo[i];

		if ((int)fs->f_type == vfc.vfc_typenum &&
		    procfs_isaccessable(fs->f_mntonname)) {
			*mountpointp = strdup(fs->f_mntonname);
			break;
		}
	}

	free(fsinfo);

	return (*mountpointp != NULL);
}


bool
procfs_isaccessable(const char *path)
{
	int fd;

	/*
	 * Test whether we can read nodes in the given procfs filesystem by
	 * trying to read our own node.  This should always succeed unless
	 * the filesystem is mounted on a directory with restrictive
	 * permissions.
	 */
	fd = procfs_opennode(path, getpid(), "mem");
	if (fd < 0)
		return false;

	close(fd);
	return true;
}


int
procfs_opennode(const char *procfs, pid_t pid, const char *node)
{
	char filename[PATH_MAX];
	int fd;

	/*
	 * Construct the file path to the desired procfs node.
	 * Ensure that the path is nul-terminated.
	 */
	snprintf(filename, sizeof(filename),
		 "%s/%u/%s", procfs, pid, node);
	filename[sizeof(filename) - 1] = '\0';

	/*
	 * Open the procfs node and return the file descriptor.
	 */
	fd = open(filename, O_RDONLY);
	if (fd < 0)
		fatal(EX_OSERR, "cannot open %s: %m", filename);

	return fd;
}


bool
procfs_mount(const char *path)
{

	if (mount("procfs", path, MNT_RDONLY|MNT_NOEXEC|MNT_NOSUID, NULL) < 0) {
		warn("unable to mount procfs on %s: %m", path);
		return false;
	}

	return true;
}


void
procfs_unmount(void)
{

	if (procfs_path == NULL)
		return;

	if (unmount(procfs_path, 0) < 0)
		warn("failed to unmount procfs from %s: %m", procfs_path);
}


void
procfs_rmtmpdir(void)
{

	if (procfs_path == NULL)
		return;

	if (rmdir(procfs_path) < 0)
		warn("failed to remove %s: %m", procfs_path);

	free(procfs_path);
	procfs_path = NULL;
}




int
procfs_common_open(pid_t pid, const char *node)
{

	assert(pid >= 0);

	if (!procfs_initialized)
		procfs_init();

	if (procfs_path == NULL)
		return -1;

	return procfs_opennode(procfs_path, pid, node);
}


void
procfs_common_close(int *fdp)
{
	int fd = *fdp;

	*fdp = -1;
	if (fd >= 0)
		close(fd);
}


int
procfs_map_open(pid_t pid)
{
	return procfs_common_open(pid, "map");
}


void
procfs_map_close(int *pmapfdp)
{
	procfs_common_close(pmapfdp);
}


void
procfs_map_read(int pmapfd, void *destp, size_t *lenp)
{
	static uint8_t *buffer = NULL;
	static size_t buflen = 4096;
	uint8_t **dest = (uint8_t **)destp;
	ssize_t rv;

	assert(pmapfd >= 0);

	if (buffer == NULL) {
		buffer = malloc(buflen);
		if (buffer == NULL)
			fatal(EX_OSERR, "malloc: %m");
	}

	/*
	 * The procfs map must be read atomically.  The only way to do that
	 * is to allocate a buffer large enough to read the entire text
	 * at once.  Luckily, if we try to read too little, procfs fails with
	 * EFBIG so we know we need to try a larger buffer.
	 * XXX There should probably be a limit on how much memory we
	 *     allocate.
	 */
	for (;;) {
		rv = pread(pmapfd, buffer, buflen, 0);

		if (rv >= 0)
			break;				/* Successful read. */

		if (rv != EFBIG)
			fatal(EX_OSERR, "read: %m");	/* Unexpected error. */

		buflen <<= 1;
		buffer = realloc(buffer, buflen);
		if (buffer == NULL)
			fatal(EX_OSERR, "realloc: %m");
	}

	*dest = buffer;
	*lenp = rv;
}


int
procfs_mem_open(pid_t pid)
{
	return procfs_common_open(pid, "mem");
}


void
procfs_mem_close(int *pmemfdp)
{
	procfs_common_close(pmemfdp);
}


size_t
procfs_mem_read(int pmemfd, vm_offset_t addr, void *dest, size_t len)
{
	ssize_t rv;

	assert(pmemfd >= 0);

	rv = pread(pmemfd, dest, len, addr);
	if (rv < 0)
		fatal(EX_OSERR, "read(procfs): %m");

	return rv;
}

