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
 * $kbyanc: dyntrace/dyntrace/region.c,v 1.8 2004/12/23 01:45:19 kbyanc Exp $
 */

#include <sys/types.h>
#include <sys/queue.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "dyntrace.h"


/*
 * Minimum and maximum number of bytes to cache per region of the target
 * process's address space.
 */
#define	REGION_BUFFER_MINSIZE	32
#define	REGION_BUFFER_MAXSIZE	1024*1024


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


struct region_list {
	LIST_HEAD(, region_info) head;	/* List of regions. */
};


const char *region_type_name[NUMREGIONTYPES] = {
	"unknown",
	"text",
	"text:program",
	"text:library",
	"non-text",
	"data",
	"stack"
};

static region_t	 region_find(region_list_t rlist, vm_offset_t addr);
static void	 region_remove(region_t *regionp);


region_list_t
region_list_new(void)
{
	region_list_t rlist;

	rlist = malloc(sizeof(*rlist));
	if (rlist == NULL)
		fatal(EX_OSERR, "malloc: %m");

	LIST_INIT(&rlist->head);
	return rlist;
}


void
region_list_done(region_list_t *rlistp)
{
	region_list_t rlist = *rlistp;
	region_t region;

	*rlistp = NULL;

	while (!LIST_EMPTY(&rlist->head)) {
		region = LIST_FIRST(&rlist->head);
		region_remove(&region);
	}

	free(rlist);
}


region_t
region_find(region_list_t rlist, vm_offset_t addr)
{
	region_t region;

	LIST_FOREACH(region, &rlist->head, link) {		
		if (region->start <= addr && region->end > addr)
			return region;
	}

	return NULL;
}


region_t
region_lookup(region_list_t rlist, vm_offset_t addr)
{
	region_t region;

	region = region_find(rlist, addr);

	if (region == LIST_FIRST(&rlist->head) || region == NULL)
		return region;

	/*
	 * Move the matched region to the head of the list to take advantage of
	 * the locality of reference in the traced code.
	 */
	LIST_REMOVE(region, link);
	LIST_INSERT_HEAD(&rlist->head, region, link);

	return region;
}


void
region_remove(region_t *regionp)
{
	region_t region = *regionp;

	*regionp = NULL;
	LIST_REMOVE(region, link);
	if (region->buffer != NULL)
		free(region->buffer);
	free(region);
}


void
region_update(region_list_t rlist, vm_offset_t start, vm_offset_t end,
	      region_type_t type, bool readonly)
{
	region_t region;

	assert(end > start);

	/*
	 * Lookup any existing regions which contain the new regions' start
	 * address.  This will find overlapping regions, but not proper
	 * sub-regions.  The latter is OK as the new region will be ahead
	 * of the old region in the list so it will effectively "block" it.
	 * This isn't ideal, but works as a time versus memory tradeoff.
	 */
	while ((region = region_find(rlist, start)) != NULL) {

		/*
		 * If the new region exactly matches or is an extension of an
		 * existing region, then we simply update the existing region
		 * and return.  This is the most common case.
		 */
		if (region->start == start && region->end <= end &&
		    region->type == type && region->readonly == readonly) {
			region->end = end;
			return;
		}

		/*
		 * Remove any regions that overlap the start address.
		 */
		region_remove(&region);
	}

	assert(region == NULL);

	/*
	 * Create a new region record and add it to the head of the list.
	 */

	region = calloc(1, sizeof(*region));
	if (region == NULL)
		fatal(EX_OSERR, "malloc: %m");

	LIST_INSERT_HEAD(&rlist->head, region, link);

	region->start = start;
	region->end = end;
	region->type = type;
	region->readonly = readonly;

	if (!readonly)
		return;

	/*
	 * The region is read-only so we can cache the memory contents to
	 * save a call to the kernel for every instruction.  We cache the
	 * minimum amount unless the region is a text segment, in which case
	 * it is highly probable for code to be executed there so we cache
	 * more.
	 */
	region->bufsize = REGION_BUFFER_MINSIZE;

	if (REGION_IS_TEXT(region->type))
		region->bufsize = REGION_BUFFER_MAXSIZE;

	if (region->bufsize > end - start)
		region->bufsize = end - start;

	/*
	 * Allocate buffer to cache the region's contents.  If the allocation
	 * fails, just pretend the region isn't read-only.  This will likely
	 * reduce throughput of the tracer, but will allow it to continue
	 * to run without impacting the results.
	 */
	region->buffer = malloc(region->bufsize);
	if (region->buffer == NULL) {
		warn("malloc: %m (non-fatal)");
		region->readonly = false;
	}
}


size_t
region_read(target_t targ, region_t region, vm_offset_t addr,
	    void *dest, size_t len)
{
	vm_offset_t start;
	ssize_t offset;

	assert(len > 0);
	assert(addr + len <= region->end);

	/*
	 * If the region is not readonly, we cannot cache the memory contents
	 * as they may change (e.g. self-modifying code).  So we have to ask
	 * the kernel to supply the memory contents every time.
	 */
	if (!region->readonly)
		return target_read(targ, addr, dest, len);

	assert(region->buffer != NULL);

	offset = addr - region->bufaddr;

	/*
	 * Satisfy the request from the region's cache if we can.
	 */
	if (offset >= 0 && offset + len <= region->buflen) {
		memcpy(dest, region->buffer + offset, len);
		return len;
	}

	/*
	 * Reload the region's cache.
	 * We start the region cache slightly before the requested addr
	 * so that simple loops do not cause spurious cache misses.
	 */
	start = region->start;
	if (start + region->bufsize <= addr)
		start = region->end - region->bufsize;
	if (start > addr)
		start = addr + len - (region->bufsize / 2);

	region->buflen = region->end - start;
	if (region->buflen > region->bufsize)
		region->buflen = region->bufsize;

#if 0
	debug("XXX region cache miss, buflen = %u", region->buflen);
#endif

	region->buflen = target_read(targ, start, region->buffer,
				     region->buflen);
	region->bufaddr = start;

	offset = addr - start;
	assert(offset >= 0);

	memcpy(dest, region->buffer + offset, len);
	return len;
}


region_type_t
region_get_type(region_t region)
{
	return region->type;
}


size_t
region_get_range(region_t region, vm_offset_t *startp, vm_offset_t *endp)
{

	assert(region != NULL);

	if (startp != NULL)
		*startp = region->start;
	if (endp != NULL)
		*endp = region->end;

	return (region->end - region->start);
}
