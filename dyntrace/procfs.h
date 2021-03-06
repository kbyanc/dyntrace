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
 * $kbyanc: dyntrace/dyntrace/procfs.h,v 1.3 2004/12/23 01:45:19 kbyanc Exp $
 */

#ifndef _INCLUDE_DYNTRACE_PROCFS_H
#define	_INCLUDE_DYNTRACE_PROCFS_H

#include <sys/cdefs.h>
#include <stdbool.h>


__BEGIN_DECLS

extern bool	 procfs_init(void);

extern int	 procfs_map_open(pid_t pid);
extern void	 procfs_map_close(int *pmapfdp);
extern void	 procfs_map_read(int pmapfd, void *destp, size_t *lenp);

extern int	 procfs_mem_open(pid_t pid);
extern void	 procfs_mem_close(int *pmemfdp);
extern size_t	 procfs_mem_read(int pmemfd, vm_offset_t addr,
				 void *dest, size_t len);

extern int	 procfs_generic_open(pid_t pid, const char *node);
extern void	 procfs_generic_close(int *fdp);

extern char	*procfs_get_procname(pid_t pid);

__END_DECLS

#endif
