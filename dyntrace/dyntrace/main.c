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
 * $kbyanc: dyntrace/dyntrace/main.c,v 1.9 2004/12/14 06:02:26 kbyanc Exp $
 */

#include <sys/types.h>
#include <sys/time.h>

#include <inttypes.h>
#include <libgen.h>
#include <signal.h>
#include <stdlib.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include <machine/reg.h>

#include "dynprof.h"

#define	DEFAULT_CHECKPOINT	(15 * 60)	/* 15 minutes */


static void	 usage(const char *msg);
static struct procinfo *procinfo_new(ptstate_t pts);
static void	 profile(void);
static uint	 rounddiv(uint64_t a, uint64_t b);
static void	 setsighandler(int sig, void (*handler)(int));
static void	 sig_ignore(int sig);
static void	 sig_terminate(int sig);
static void	 sig_checkpoint(int sig);


static bool	 terminate	= false;
static bool	 checkpoint	= false;

       bool	 opt_debug	= false;
       bool	 opt_printzero	= false;
       int	 opt_checkpoint	= -1;
static pid_t	 opt_pid	= -1;
       char	*opt_outfile	= NULL;
       char	*opt_command	= NULL;

static struct procinfo *proc	= NULL;


void
usage(const char *msg)
{
	const char *progname;

	if (msg != NULL)
		warn("%s\n", msg);

	progname = getprogname();

	fatal(EX_USAGE,
"usage: %s [-vz] [-c seconds] [-f opcodefile] [-o outputfile] command\n"
"       %s [-vz] [-c seconds] [-f opcodefile] [-o outputfile] -p pid\n",
		progname, progname
	);
}


int
main(int argc, char *argv[])
{
	ptstate_t pts;
	int ch;

	if (argc == 1)
		usage(NULL);

	while ((ch = getopt(argc, argv, "c:f:o:p:vz")) != -1) {
		switch ((char)ch) {
		case 'c':
			opt_checkpoint = atoi(optarg);
			if (opt_checkpoint <= 0) {
				fatal(EX_USAGE, "invalid count for -c: \"%s\"",
				      optarg);
			}
			break;

		case 'f':
			optree_parsefile(optarg);
			break;

		case 'o':
			if (opt_outfile != NULL)
				usage("only one output file can be specified");
			opt_outfile = optarg;
			break;

		case 'p':
			if (opt_pid != -1)
				usage("only one process id can be specified");
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

	if (opt_checkpoint == -1)
		opt_checkpoint = DEFAULT_CHECKPOINT;

	/*
	 * The traced process receives a SIGTRAP each time it stops under the
	 * control of ptrace(2).  However, as the tracing process, we have
	 * the opportunity to intercept the (fatal) signal if we have a
	 * SIGCHLD handler other than the default SIG_IGN.  Since we wait
	 * for the child to stop with waitpid(2), we install our own SIGCHLD
	 * handler to ignore the signals.
	 */
	setsighandler(SIGCHLD, sig_ignore);

	if (opt_pid != -1) {
		if (argc != 0)
			usage("cannot specify both a process id and a command");
		pts = ptrace_attach(opt_pid);

		if (opt_outfile == NULL)
			asprintf(&opt_outfile, "%u.prof", opt_pid);
	}
	else {
		if (argc == 0)
			usage("command not specified");
		pts = ptrace_fork();
		if (pts == NULL) {
			/* Child process. */
			execvp(*argv, argv);
			fatal(EX_OSERR, "failed to executed \"%s\": %m", *argv);
		}

		if (opt_outfile == NULL)
			asprintf(&opt_outfile, "%s.prof", basename(*argv));
	}

	proc = procinfo_new(pts);
	region_insert(proc, 1, 0xffffffff, REGION_UNKNOWN, false);

	profile();

	/*
	 * If we attached to an already running process (i.e. -p pid command
	 * line option was used) and that process has not terminated, then
	 * detach from it so it can continue running like it was before we
	 * started tracing it.
	 *
	 * However, if the traced process is our child process, do not
	 * detach from it if it is still running so that it is killed when
	 * we exit.
	 */
	if (terminate && opt_pid > 0)
		ptrace_detach(pts);

	return 0;
}


struct procinfo *
procinfo_new(ptstate_t pts)
{
	struct procinfo *proc;

	proc = calloc(1, sizeof(*proc));
	if (proc == NULL)
		fatal(EX_OSERR, "malloc: %m");

//	proc->pid = pid;
	proc->pts = pts;

	return proc;
}


void
profile(void)
{
	struct timeval starttime, stoptime;
	char timestr[64];
	time_t seconds;
	uint64_t instructions;
	struct reg regs;

	optree_output_open();
	warn("recording results to %s", opt_outfile);

	/*
	 * Install signal handlers to ensure we dump the collected data
	 * before terminating.
	 */
	setsighandler(SIGHUP, sig_terminate);
	setsighandler(SIGINT, sig_terminate);
	setsighandler(SIGQUIT, sig_terminate);
	setsighandler(SIGTERM, sig_terminate);

	/*
	 * Install signal handlers to dump collected data on demand.  This
	 * is used to implement periodic checkpointing (via SIGALRM) and to
	 * allow external programs to request updates (via SIGINFO).
	 */
	setsighandler(SIGALRM, sig_checkpoint);
	setsighandler(SIGINFO, sig_checkpoint);

	if (opt_checkpoint == 0)
		warn("checkpoints disabled");
	else {
		alarm(opt_checkpoint);
		warn("checkpoints every %u seconds",
		     opt_checkpoint);
	}

	gettimeofday(&starttime, NULL);
	seconds = starttime.tv_sec;
	strftime(timestr, sizeof(timestr), "%c", localtime(&seconds));
	debug("profile started at %s", timestr);

	instructions = 0;
	while (!terminate) {
		ptrace_getregs(proc->pts, &regs);

		/* XXX MARK AS STACK(regs.r_esp) if regs.r_ss == regs.r_cs */

		optree_update(proc, regs.r_eip, 0);
		instructions++;

		/*
		 * Periodically record the instruction counters in case
		 * we get interrupted (e.g. power outage, etc) so at least
		 * we have something to show for our efforts.
		 */
		if (checkpoint) {
			warn("checkpoint");
			optree_output();
			optree_output_open();
			checkpoint = false;
		}

		if (terminate || !ptrace_step(proc->pts))
			break;
	}

	gettimeofday(&stoptime, NULL);
	seconds = stoptime.tv_sec;
	strftime(timestr, sizeof(timestr), "%c", localtime(&seconds));
	debug("profile stopped at %s", timestr);

	if (opt_debug) {
		uint ips;

		stoptime.tv_sec -= starttime.tv_sec;
		stoptime.tv_usec -= starttime.tv_usec;
		if (stoptime.tv_usec < 0) {
			stoptime.tv_usec += 1000000;
			stoptime.tv_sec--;
		}

		ips = rounddiv(instructions * 1000000,
			       (stoptime.tv_sec * 1000) +
			       rounddiv(stoptime.tv_usec, 1000));
		debug("%llu instructions profiled in "
		      "%0lu.%03u seconds (%0u.%03u/sec)",
		      (unsigned long long)instructions,
		      stoptime.tv_sec, rounddiv(stoptime.tv_usec, 1000),
		      ips / 1000, ips % 1000);
	}

	optree_output();
}


uint
rounddiv(uint64_t a, uint64_t b)
{
	return (a + (b / 2)) / b;
}


void
setsighandler(int sig, void (*handler)(int))
{
	struct sigaction act;

	act.sa_handler = handler;
	act.sa_flags = SA_RESTART;
	sigemptyset(&act.sa_mask);
	sigaction(sig, &act, NULL);
}


void
sig_ignore(int sig __unused)
{
}


void
sig_terminate(int sig __unused)
{
	terminate = true;
}


void
sig_checkpoint(int sig)
{
	checkpoint = true;
	if (sig == SIGALRM && opt_checkpoint > 0)
		alarm(opt_checkpoint);
}

