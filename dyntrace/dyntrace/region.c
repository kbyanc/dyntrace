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
 * $kbyanc: dyntrace/dyntrace/region.c,v 1.1 2004/12/14 06:02:26 kbyanc Exp $
 */

#include <sys/types.h>

#include <assert.h>
#include <stdlib.h>
#include <sysexits.h>

#include "dynprof.h"


struct region_info {
	LIST_ENTRY(region_info) link;

	vm_offset_t	 start;
	vm_offset_t	 end;

	region_type_t	 type;
	bool		 readonly;

	vm_offset_t	 bufaddr;	/* First address cached. */
	size_t		 buflen;	/* Bytes in cache buffer. */
	uint8_t		*buffer;
	size_t		 bufsize;	/* Memory allocated to buffer. */
};


static void	 region_remove(region_t *regionp);



region_t
region_lookup(struct procinfo *proc, vm_offset_t offset)
{
	region_t region;

	LIST_FOREACH(region, &proc->region_list, link) {		
		if (region->start <= offset && region->end >= offset)
			break;
	}

	if (region == LIST_FIRST(&proc->region_list) || region == NULL)
		return region;

	LIST_REMOVE(region, link);
	LIST_INSERT_HEAD(&proc->region_list, region, link);

	return region;
}


void
region_remove(region_t *regionp)
{
	region_t region = *regionp;

	*regionp = NULL;
	LIST_REMOVE(region, link);
	/* XXX buffer */
	free(region);
}


void
region_insert(struct procinfo *proc, vm_offset_t start, vm_offset_t end,
	      region_type_t type, bool readonly)
{
	region_t region;

	while ((region = region_lookup(proc, start)) != NULL) {

		if (region->start == start && region->end <= end &&
		    region->type == type && region->readonly == readonly) {
			region->end = end;
			return;
		}

		region_remove(&region);
	}

	assert(region == NULL);

	region = calloc(1, sizeof(*region));
	if (region == NULL)
		fatal(EX_OSERR, "malloc: %m");

	LIST_INSERT_HEAD(&proc->region_list, region, link);

	region->start = start;
	region->end = end;
	region->readonly = readonly;

	/* XXX Should use 64k if text and using procfs or have PIOD_READ_I. */
	region->bufsize = 65536;		/* Must be at least 4 */
	region->buffer = malloc(region->bufsize);
	if (region->buffer == NULL)
		fatal(EX_OSERR, "malloc: %m");
}


size_t
region_read(struct procinfo *proc, region_t region, vm_offset_t addr,
	    void *dest, size_t len)
{
	ssize_t offset;

	assert(len > 0);

	/*
	 * If the region is not readonly, we cannot cache the memory contents
	 * as they may change (e.g. self-modifying code).  So we have to ask
	 * the kernel to supply the memory contents every time.
	 */
	if (!region->readonly) {
		return ptrace_read(proc->pts, addr, dest, len);
	}

	assert(region->buffer != NULL);

	/*
	 * Satisfy the request from the region's cache if we can.
	 */
	offset = addr - region->bufaddr;
	if (offset >= 0 && offset + len <= region->buflen) {
		memcpy(dest, region->buffer + offset, len);
		return len;
	}

	/*
	 * Reload the region's cache.
	 */
	region->buflen = ptrace_read(proc->pts, addr, region->buffer,
				     region->bufsize);
	region->bufaddr = addr;

	/* We cannot read more than what fits in our cache. */
	if (len > region->buflen)
		len = region->buflen;

	memcpy(dest, region->buffer, len);
	return len;
}
