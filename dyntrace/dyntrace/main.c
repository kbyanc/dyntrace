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
 * $kbyanc: dyntrace/dyntrace/main.c,v 1.3 2004/11/29 02:13:44 kbyanc Exp $
 */

#include <sys/types.h>

#include <signal.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include <machine/reg.h>

#include "dynprof.h"


static void usage(const char *msg);
static void sigchild(int sig);


       bool	opt_debug	= false;
static bool	opt_printzero	= false;
static pid_t	opt_pid		= -1;


int
main(int argc, char *argv[])
{
	uint8_t codebuf[16];
	struct reg regs;
	struct sigaction act;
	ptstate_t pts;
	int ch;

	act.sa_handler = sigchild;
	act.sa_flags = SA_RESTART;
	sigemptyset(&act.sa_mask);
	sigaction(SIGCHLD, &act, NULL);

	if (argc == 1)
		usage(NULL);

	while ((ch = getopt(argc, argv, "f:p:vz")) != -1) {
		switch ((char)ch) {
		case 'f':
			optree_parsefile(optarg);
			break;

		case 'p':
			if (opt_pid != -1)
				usage("-p can only be specified once");
			opt_pid = atoi(optarg);
			if (opt_pid <= 0) {
				fatal(EX_USAGE,
				      "expected process id, got \"%s\"",
				      optarg);
			}
			break;

		case 'v':
			opt_debug = true;
			break;

		case 'z':
			opt_printzero = true;
			break;

		case '?':
		default:
			usage(NULL);
		}
	}

	argv += optind;
	argc -= optind;

	if (opt_pid != -1) {
		if (argc != 0)
			usage("cannot specify both a process id and a command");
		pts = ptrace_attach(opt_pid);

	}
	else {
		if (argc == 0)
			usage("command not specified");
		pts = ptrace_fork();
		if (pts == NULL) {
			/* Child process. */
			debug("running %s...", *argv);
			if (execvp(*argv, argv) < 0) {
				fatal(EX_OSERR, "failed to executed \"%s\": %m",
				      *argv);
			}
			/* NOTREACHED */
		}

	}

	do {
		ptrace_getregs(pts, &regs);
		/* XXX MARK AS STACK(regs.r_esp) if regs.r_ss == regs.r_cs */
		ptrace_read(pts, regs.r_eip, codebuf, sizeof(codebuf));
		optree_update(codebuf, sizeof(codebuf), 0);
	} while (ptrace_step(pts));

	optree_output(stdout);

	return 0;
}


void
usage(const char *msg)
{
	const char *progname;

	if (msg != NULL)
		warn("%s\n", msg);

	progname = getprogname();

	fatal(EX_USAGE,
		"usage: %s [-vz] [-f opcodefile] command\n"
		"       %s [-vz] [-f opcodefile] -p pid\n",
		progname, progname
	);
}


void
sigchild(int sig __unused)
{
}

