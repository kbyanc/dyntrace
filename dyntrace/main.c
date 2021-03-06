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
 * $kbyanc: dyntrace/dyntrace/main.c,v 1.16 2004/12/23 01:45:19 kbyanc Exp $
 */

#include <sys/types.h>
#include <sys/time.h>

#include <assert.h>
#include <inttypes.h>
#include <libgen.h>
#include <signal.h>
#include <stdlib.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include "dyntrace.h"

#define	DEFAULT_CHECKPOINT	(15 * 60)	/* 15 minutes */
#define	DEFAULT_OPFILE		"/usr/local/share/dyntrace/oplist-x86.xml"


static void	 usage(const char *msg);
static void	 trace(target_t targ);
static void	 time_record(const char *msg, struct timeval *tvp);
static void	 epilogue(void);
static uint	 rounddiv(uint64_t a, uint64_t b);
static void	 sig_terminate(int sig);
static void	 sig_checkpoint(int sig);


static struct timeval starttime, stoptime;
static uint64_t	 instructions	= 0;

static volatile sig_atomic_t terminate	= false;
static volatile sig_atomic_t checkpoint	= false;

       bool	 opt_debug	= false;
       bool	 opt_printzero	= false;
       int	 opt_checkpoint	= -1;
static pid_t	 opt_pid	= -1;
       char	*opt_outfile	= NULL;
       char	*opt_command	= NULL;


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
	bool opsloaded = false;
	target_t targ;
	int ch;

	if (argc == 1)
		usage(NULL);

	while ((ch = getopt(argc, argv, "c:f:o:p:vz")) != -1) {
		switch ((char)ch) {
		case 'c':
			opt_checkpoint = atoi(optarg);
			if (opt_checkpoint < 0) {
				fatal(EX_USAGE, "invalid count for -c: \"%s\"",
				      optarg);
			}
			break;

		case 'f':
			optree_parsefile(optarg);
			opsloaded = true;
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
	if (!opsloaded) {
		optree_parsefile(DEFAULT_OPFILE);
		opsloaded = true;
	}

	target_init();

	if (opt_pid != -1) {
		if (argc != 0)
			usage("cannot specify both a process id and a command");

		targ = target_attach(opt_pid);
	}
	else {
		if (argc == 0)
			usage("command not specified");

		targ = target_execvp(*argv, argv);
	}

	if (opt_outfile == NULL)
		asprintf(&opt_outfile, "%s.trace", target_get_name(targ));

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
	 * allow external programs to request updates (via SIGUSR1 or SIGINFO).
	 */
	setsighandler(SIGALRM, sig_checkpoint);
	setsighandler(SIGUSR1, sig_checkpoint);
#ifdef SIGINFO
	setsighandler(SIGINFO, sig_checkpoint);
#endif

	if (opt_checkpoint == 0)
		warn("checkpoints disabled");
	else {
		alarm(opt_checkpoint);
		warn("checkpoints every %u seconds",
		     opt_checkpoint);
	}

	time_record("trace started at", &starttime);

	trace(targ);

	time_record("trace stopped at", &stoptime);
	epilogue();

	optree_output();

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
		target_detach(&targ);

	target_done();

	return 0;
}


void
trace(target_t targ)
{

	while (!terminate) {
		vm_offset_t pc = target_get_pc(targ);
		region_t region = target_get_region(targ, pc);
		uint cycles = target_get_cycles(targ);

		optree_update(targ, region, pc, cycles);
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

		if (terminate)
			break;

		target_step(targ);
		targ = target_wait();
		if (targ == NULL)
			break;
	}
}


void
time_record(const char *msg, struct timeval *tvp)
{
	char timestr[64];
	time_t seconds;

	gettimeofday(tvp, NULL);
	seconds = tvp->tv_sec;

	if (opt_debug) {
		strftime(timestr, sizeof(timestr), "%c", localtime(&seconds));
		debug("=== %s %s ===", msg, timestr);
	}
}


void
epilogue(void)
{
	uint ips;

	if (!opt_debug)
		return;

	stoptime.tv_sec -= starttime.tv_sec;
	stoptime.tv_usec -= starttime.tv_usec;
	if (stoptime.tv_usec < 0) {
		stoptime.tv_usec += 1000000;
		stoptime.tv_sec--;
	}

	ips = rounddiv(instructions * 1000000,
		       (stoptime.tv_sec * 1000) +
		       rounddiv(stoptime.tv_usec, 1000));
	debug("%llu instructions traced in "
	      "%0lu.%03u seconds (%0u.%03u/sec)",
	      (unsigned long long)instructions,
	      stoptime.tv_sec, rounddiv(stoptime.tv_usec, 1000),
	      ips / 1000, ips % 1000);
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

