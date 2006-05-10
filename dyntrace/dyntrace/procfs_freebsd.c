/*
 * Copyright (c) 2004,2006 Kelly Yancey
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
 * $kbyanc: dyntrace/dyntrace/procfs_freebsd.c,v 1.8 2006/05/10 03:25:26 kbyanc Exp $
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/mount.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <regex.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "dyntrace.h"
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


/*!
 * procfs_init() - Initialize data structures for the procfs interface routines.
 *
 *	@return	boolean true if procfs is available and initialized, boolean
 *		false otherwise.
 */
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
	procfs_path = strdup(_PATH_TMP "dyntrace.XXXXXX");
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


/*!
 * procfs_isavailable() - Determine if the procfs filesystem is supported by
 *			  the kernel.
 *
 *	If procfs is not available, tries to load the kernel module for
 *	procfs to make it available.
 *
 *	@return	boolean true if the kernel supports the procfs filesystem,
 *		boolean false otherwise.
 */
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

	/* Deserves a warning as the system administrator may be concerned. */
	warn("loaded procfs");
#endif

	return true;
}


/*!
 * procfs_ismounted() - Determine if the procfs filesystem is mounted.
 *
 *	If procfs is not mounted, tried to mount it on a temporary directory.
 *
 *	@param	mountpointp	Pointer to populate with the address of a
 *				newly-allocated string holding the path procfs
 *				is mounted on.
 *
 *	@return	boolean true and sets \a *mountpointp if procfs is mounted,
 *		boolean false otherwise.
 */
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


/*!
 * procfs_isaccessable() - Determine if the current process has permissions
 *			   to access the procfs filesystem mounted at the given
 *			   path.
 *
 *	@param	path	The path where procfs is mounted.
 *
 *	@return	boolean true if the current process can read procfs nodes
 *		at the given path.
 */
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


/*!
 * procfs_opennode() - Internal routine to open a procfs node for the given
 *		       process identifier.
 *
 *	@param	procfs	Path where procfs is mounted.
 *
 *	@param	pid	The process identifier whose node we are to open.
 *
 *	@param	node	The name of the procfs node (e.g. "mem", "map", etc).
 *
 *	@return	file descriptor for reading from the given node.
 *
 *	The FreeBSD target only requires read access to procfs nodes, so all
 *	nodes are open by this routine read-only.
 */
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


/*!
 * procfs_mount() - Internal routine to mount procfs at a given mount point.
 *
 *	@param	path	Path to mount procfs on.
 *
 *	@return	boolean true of procfs was successfully mounted at the
 *		specified mount point path, boolean false otherwise.
 */
bool
procfs_mount(const char *path)
{
	int bogus;

	/*
	 * FreeBSD 6.0 and 6.1 require the fourth argument to the mount(2)
	 * system call, data, to be non-NULL even if the mounted filesystem
	 * does not use the argument.  All other versions do not impose this
	 * restriction (nor is the restriction documented in the man page
	 * on the versions that do).  Workaround the bug by passing a
	 * bogus non-NULL pointer as the data argument.
	 */
	if (mount("procfs", path, MNT_RDONLY|MNT_NOEXEC|MNT_NOSUID,
		  &bogus) < 0) {
		warn("unable to mount procfs on %s: %m", path);
		return false;
	}

	return true;
}


/*!
 * procfs_unmount() - atexit(3) handler for unmounting a temporary procfs mount.
 */
void
procfs_unmount(void)
{

	if (procfs_path == NULL)
		return;

	if (unmount(procfs_path, 0) < 0)
		warn("failed to unmount procfs from %s: %m", procfs_path);
}


/*!
 * procfs_rmtmpdir() - atexit(3) handle for removing the temporary procfs
 *		       mount point path.
 *
 *	Must be called after procfs_unmount().
 */
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


/*
 * ========================================================================
 * What follows are the implementations of the generic routines declared in
 * "procfs.h".
 * ========================================================================
 */


/*!
 * procfs_generic_open() - Open a process' procfs node.
 *
 *	@param	pid	The process identifier whose procfs node is to be
 *			opened.
 *
 *	@param	node	Name of the procfs node to open.
 *
 *	@return	file descriptor for reading from the specified node.
 *
 *	Names for procfs nodes vary from system to system; the
 *	procfs_generic_open() and procfs_generic_close() routines should only
 *	be called from system-specific code that has knowledge of the given
 *	system's node names.
 */
int
procfs_generic_open(pid_t pid, const char *node)
{

	assert(pid >= 0);

	if (!procfs_initialized)
		procfs_init();

	if (procfs_path == NULL)
		return -1;

	return procfs_opennode(procfs_path, pid, node);
}


/*!
 * procfs_generic_close() - Close a file descriptor.
 *
 *	@param	fdp	Pointer to file descriptor to close.
 *
 *	@post	The file descriptor pointed to by \a fdp is set to -1.
 */
void
procfs_generic_close(int *fdp)
{
	int fd = *fdp;

	*fdp = -1;
	if (fd >= 0)
		close(fd);
}


/*!
 * procfs_map_open() - Open process' memory-map procfs node for reading.
 *
 *	The memory-map procfs node allows the description of the given
 *	process' memory map to be read.  The exact format of the memory map
 *	is operating-system dependent.
 *
 *	@param	pid	The process identifier who memory-map node to open.
 *
 *	@return	file descriptor for reading the process' memory map.
 */
int
procfs_map_open(pid_t pid)
{
	return procfs_generic_open(pid, "map");
}


/*!
 * procfs_map_close() - Close file handle for reading process' memory map.
 *
 *	@param	pmapfdp	Pointer to the file descriptor to close.
 *
 *	@post	Sets the file descriptor pointed to by \a pmapfdp to -1.
 */
void
procfs_map_close(int *pmapfdp)
{
	procfs_generic_close(pmapfdp);
}


/*!
 * procfs_map_read() - Read a process's memory map.
 *
 *	@param	pmapfd	File descriptor returned by procfs_map_open() to read.
 *
 *	@param	destp	Pointer to a pointer to be populated with the address
 *			of the memory map buffer.
 *
 *	@param	lenp	Pointer to a size_t to be populated with the number of
 *			bytes in the memory map buffer.
 *
 *	The memory map buffer pointed to by \a destp on return is static
 *	storage and should not be freed by the caller.
 */
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
		rv = pread(pmapfd, buffer, buflen - 1, 0);

		if (rv >= 0)
			break;				/* Successful read. */

		if (errno != EFBIG)
			fatal(EX_OSERR, "read: %m");	/* Unexpected error. */

		buflen <<= 1;
		buffer = realloc(buffer, buflen);
		if (buffer == NULL)
			fatal(EX_OSERR, "realloc: %m");
	}

	buffer[rv] = '\0';
	*dest = buffer;
	*lenp = rv;
}


/*!
 * procfs_mem_open() - Open process' memory-access procfs node for reading.
 *
 *	The memory-access procfs node allows the entire virtual memory of
 *	the given process to be readable using procfs_mem_read().
 *
 *	@param	pid	Process identifier whose memory to read.
 *
 *	@return	file descriptor for reading the process' memory.
 */
int
procfs_mem_open(pid_t pid)
{
	return procfs_generic_open(pid, "mem");
}


/*!
 * procfs_mem_close() - Close file descriptor for reading process' memory.
 *
 *	@param	pmemfdp	Pointer to file descriptor to close.
 *
 *	@post	Sets the file descriptor pointed to by \a *pmemfdp to -1.
 */
void
procfs_mem_close(int *pmemfdp)
{
	procfs_generic_close(pmemfdp);
}


/*!
 * procfs_mem_read() - Read process' memory.
 *
 *	@param	pmemfd	The file descriptor returned by procfs_mem_open() for
 *			reading from the process' memory.
 *
 *	@param	addr	The address in the process' virtual memory to read.
 *
 *	@param	dest	Pointer to a buffer to read the memory contents into.
 *
 *	@param	len	The number of bytes to read.
 *
 *	@return	the number of bytes read.
 */
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


/*!
 * procfs_get_procname() - Get the name of the process with the given pid.
 *
 *	@param	pid	The process identifier to get the name of.
 *
 *	@returns a newly-allocated string containing the name of the process
 *		 or NULL if the name could not be determined.
 *
 *	It is the caller's responsibility to free the returned string when
 *	it is done with it.
 */
char *
procfs_get_procname(pid_t pid)
{
	char buffer[NAME_MAX + 45];
	regex_t re_postname;
	regmatch_t re_match;
	int re_error;
	ssize_t len;
	char *pos;
	int fd;

	/*
	 * Only /proc/XXX/status has the original process name, unfortunately
	 * it is difficult to parse correctly.  The /proc/XXX/cmdline file
	 * seems to be ideal, except that it maybe be altered by the
	 * process and hence may have non-sensical values (e.g. sendmail which
	 * changes its name for status reporting).
	 */
	fd = procfs_generic_open(pid, "status");
	if (fd < 0)
		return NULL;

	/*
	 * The /proc/XXX/status file is only a single line.  Of that, we only
	 * need to read the process name (maximum NAME_MAX chars) plus some
	 * trailing text to identify where the process name ends.
	 */
	len = read(fd, buffer, sizeof(buffer) - 1);
	if (len < 0) {
		procfs_generic_close(&fd);
		return NULL;
	}

	procfs_generic_close(&fd);

	/*
	 * Now for the trick of parsing the status line.  The format is a
	 * space-separated list of various fields.  The issue is how to
	 * accurately identify the process name which itself may have spaces
	 * embedded in it.  The solution: don't try to find the process name
	 * but rather the text immediately following the process name.  Once
	 * we have found that, we know everything before that is the process
	 * name (spaces and all).
	 */
	memset(&re_postname, 0, sizeof(re_postname));
	re_error = regcomp(&re_postname,
			   /* my cat 83162 82755 83162 82755 5,8 ctty ... */
			   /*        ^--------------------------^         */
			    "( [[:digit:]]{1,5}){4} [[:digit:]]+,[[:digit:]]+ ",
			    REG_EXTENDED);
	if (re_error) {
		regerror(re_error, &re_postname, buffer, sizeof(buffer));
		fatal(EX_SOFTWARE, "failed to compile regex: %s", buffer);
	}

	buffer[len] = '\0';
	re_error = regexec(&re_postname, buffer, 1, &re_match, 0);
	if (re_error) {
		regerror(re_error, &re_postname, buffer, sizeof(buffer));
		fatal(EX_SOFTWARE, "regex match failed: %s", buffer);
	}

	regfree(&re_postname);

	/*
	 * Replace the first character matched with a nul-terminator.
	 * Everything before that is the actual process name.
	 */
	pos = buffer + re_match.rm_so;
	*pos = '\0';

	/*
	 * Make a copy of the process name to return to the caller.  We don't
	 * have to check for strdup() returning NULL because if it does it
	 * just tells our caller we couldn't get the process name.
	 */
	return strdup(buffer);
}
