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
 * $kbyanc: dyntrace/dyntrace/dyntrace.h,v 1.12 2004/12/22 09:24:49 kbyanc Exp $
 */

#ifndef _INCLUDE_DYNPROF_H
#define	_INCLUDE_DYNPROF_H

#include <sys/cdefs.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef __GNUC__
#define __attribute__()
#endif

#undef __DECONST
#define __DECONST(type, var)	((type)(uintptr_t)(const void *)(var))


typedef	struct target_state *target_t;


typedef enum {
	REGION_UNKNOWN		= 0,
	REGION_TEXT_UNKNOWN	= 1,
	REGION_TEXT_PROGRAM	= 2,
	REGION_TEXT_LIBRARY	= 3,
	REGION_NONTEXT_UNKNOWN	= 4,
	REGION_DATA		= 5,
	REGION_STACK		= 6
} region_type_t;

#define	NUMREGIONTYPES		  7
#define	REGION_IS_TEXT(rt)	((rt) < REGION_NONTEXT_UNKNOWN)

typedef struct region_info *region_t;
typedef struct region_list *region_list_t;

extern const char *region_type_name[NUMREGIONTYPES];


extern bool	 opt_debug;
extern bool	 opt_printzero;
extern char	*opt_outfile;

#define debug(fmt, ...) do {			\
	if (opt_debug) warn(fmt, __VA_ARGS__);	\
} while (0)


__BEGIN_DECLS
extern void	 warn(const char *fmt, ...)
			__attribute__ ((format (printf, 1, 2)));
extern void	 fatal(int eval, const char *fmt, ...)
			__attribute__ ((noreturn, format (printf, 2, 3)));


extern void	 setsighandler(int sig, void (*handler)(int));


extern region_list_t
		 region_list_new(void);
extern void	 region_list_done(region_list_t *rlistp);

extern region_t	 region_lookup(region_list_t rlist, vm_offset_t addr);
extern void	 region_update(region_list_t rlist,
			       vm_offset_t start, vm_offset_t end,
			       region_type_t type, bool readonly);
extern size_t	 region_read(target_t targ, region_t region,
			     vm_offset_t offset, void *dest, size_t len);
extern region_type_t
		 region_get_type(region_t region);
extern size_t	 region_get_range(region_t region,
				  vm_offset_t *startp, vm_offset_t *endp);


extern void	 optree_parsefile(const char *filepath);
extern void	 optree_update(target_t targ, region_t region,
			       vm_offset_t pc, uint cycles);
extern void	 optree_output_open(void);
extern void	 optree_output(void);


extern void	 target_init(void);
extern void	 target_done(void);

extern target_t	 target_execvp(const char *path, char * const argv[]);
extern target_t	 target_attach(pid_t pid);
extern void	 target_detach(target_t *targp);

extern target_t	 target_wait(void);
extern void	 target_step(target_t targ);

extern size_t	 target_read(target_t targ, vm_offset_t addr,
			     void *dest, size_t len);

extern vm_offset_t target_get_pc(target_t targ);
extern uint	 target_get_cycles(target_t targ);
extern const char *target_get_name(target_t targ);
extern region_t	 target_get_region(target_t targ, vm_offset_t offset);

__END_DECLS

#endif
