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
 * $kbyanc: dyntrace/dyntrace/procfs_freebsd.c,v 1.1 2004/12/15 04:29:52 kbyanc Exp $
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/mman.h>

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


#define	MAXMAPPINGS	32


struct procfs_state {
	pid_t	 pid;
	int	 fd_mem;
	int	 fd_map;

	uint	 map_count;
	void	*map_addr[MAXMAPPINGS];
	size_t	 map_len[MAXMAPPINGS];
};


static bool	 procfs_initialized = false;
static char	*procfs_path = NULL;

static bool	 procfs_init(void);
static bool	 procfs_isavailable(void);
static bool	 procfs_ismounted(char **mountpointp);
static bool	 procfs_isaccessable(const char *path);
static int	 procfs_opennode(const char *procfs, pid_t pid,
				 const char *node);
static bool	 procfs_mount(const char *path);
static void	 procfs_unmount(void);
static void	 procfs_rmtmpdir(void);


pfstate_t
procfs_open(pid_t pid)
{
	pfstate_t pfs;

	if (!procfs_initialized) {
		procfs_init();
		procfs_initialized = true;
	}

	if (procfs_path == NULL)
		return NULL;

	pfs = calloc(1, sizeof(*pfs));
	if (pfs == NULL)
		fatal(EX_OSERR, "malloc: %m");

	pfs->pid = pid;
	pfs->fd_mem = procfs_opennode(procfs_path, pid, "mem");
	pfs->fd_map = procfs_opennode(procfs_path, pid, "map");

	if (pfs->fd_mem < 0 || pfs->fd_map < 0)
		procfs_done(&pfs);

	return pfs;
}


void
procfs_done(pfstate_t *pfsp)
{
	pfstate_t pfs = *pfsp;
	uint i;

	*pfsp = NULL;

	for (i = 0; i < pfs->map_count; i++)
		munmap(pfs->map_addr[i], pfs->map_len[i]);

	close(pfs->fd_mem);
	close(pfs->fd_map);
}


size_t
procfs_read(pfstate_t pfs, vm_offset_t addr, void *dest, size_t len)
{
	ssize_t rv;

	assert(pfs != NULL);

	rv = pread(pfs->fd_mem, dest, len, addr);
	if (rv < 0)
		fatal(EX_OSERR, "read(procfs): %m");

	return rv;
}


void *
procfs_mmap(pfstate_t pfs, vm_offset_t addr, size_t len)
{
	void *ptr;
	uint i;

	assert(pfs != NULL);

	i = pfs->map_count;
	if (i == MAXMAPPINGS)
		return NULL;

	ptr = mmap(NULL, len, PROT_READ, MAP_NOCORE|MAP_SHARED,
		   pfs->fd_mem, addr);

	if (ptr == MAP_FAILED)
		return NULL;

	pfs->map_len[i] = len;
	pfs->map_addr[i] = ptr;
	pfs->map_count++;

	return ptr;
}


void
procfs_munmap(pfstate_t pfs, void *ptr, size_t len)
{
	uint nummaps;
	uint i;

	assert(pfs != NULL);

	nummaps = pfs->map_count;

	/*
	 * Verify that the given pointer corresponds to one of our mappings.
	 */
	for (i = 0; i < nummaps; i++) {
		if (pfs->map_addr[i] == ptr)
			break;
	}

	assert(i != pfs->map_count);
	assert(len == pfs->map_len[i]);

	/*
	 * Swap the last mapping into the removed mapping's array position.
	 */
	pfs->map_addr[i] = pfs->map_addr[nummaps];
	pfs->map_len[i] = pfs->map_len[nummaps];
	pfs->map_count--;

	if (munmap(ptr, len) < 0)
		fatal(EX_OSERR, "munmap: %m");
}


bool
procfs_init(void)
{

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
	struct vfsconf vfc;

	/*
	 * Check to see if the running kernel has support for procfs.
	 */
	if (getvfsbyname("procfs", &vfc) == 0)
		return true;

	/*
	 * The kernel does not support procfs; try to load it as a module.
	 * This can only succeed if the user has root privileges.
	 * XXX There is no vfsunload() to unload the module when we are done.
	 */
	if (!vfsisloadable("procfs") || vfsload("procfs") == 0) {
		warn("procfs is not available: %m");
		return false;
	}

	warn("loaded procfs");

	return true;
}


bool
procfs_ismounted(char **mountpointp)
{
	struct vfsconf vfc;
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

		if (fs->f_type == vfc.vfc_typenum &&
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

