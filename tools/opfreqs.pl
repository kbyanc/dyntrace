#!/usr/bin/perl -w
#
# Copyright (c) 2004 Kelly Yancey
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $kbyanc: dyntrace/tools/opfreqs.pl,v 1.1 2004/12/27 12:24:23 kbyanc Exp $
#

#
# Utility for annotating a trace file with the relative frequency and relative
# time of each of the opcodes.
#
# Requires the textproc/p5-XML-Twig port and its dependencies to be installed.
#

use strict;

use IO::Handle;
use XML::Twig;					# textproc/p5-XML-Twig

my %resolutions = (
	# Name		   Tag in trace XML
	'region'	=> 'region',
	'program'	=> 'program',
	'trace'		=> 'dyntrace'
);

my @nodes = ();
my $total_n = 0;
my $total_cycles = 0;


sub UpdateNodes($$) {
	while (my $opcount = pop @nodes) {
		my $n = $opcount->att('n');
		$opcount->set_att('relfreq', sprintf("%0.8f", $n / $total_n))
		    if ($total_n);

		my $cycles = $opcount->att('cycles');
		$cycles = 0 unless $cycles;
		$opcount->set_att('reltime',
				  sprintf("%0.8f", $cycles / $total_cycles))
		    if ($total_cycles);
	}

	$total_n = 0;
	$total_cycles = 0;
}


sub usage {
	use FindBin qw($Script);

	print STDERR << "EOU" ;
usage: $Script

$Script reads an XML trace file as input, annotates the opcounts with their
relative frequencies and relative timings, and writes the updated trace file
to output.

For example:
	$Script program < myprog.trace > myprog-withfreqs.trace

The level of trace detail used in calculating relative values is determined
by the resolution parameter.  If the resolution is set for 'region', then
the sum of relative values in each region will be 1, meaning the sum for
the entire trace may be larger.  If the resolution is set for 'process',
then the sub of all relative values in the entire trace will be 1.

The supported resolutions are:
EOU

	foreach my $resolution (sort keys %resolutions) {
		print STDERR "\t$resolution\n";
	}

	exit(1);
}


# --- main ---
{

	my $io = new IO::Handle;
	$io->fdopen(fileno(STDIN), 'r');

	usage() unless scalar(@ARGV) == 1;
	my $parentTag = $resolutions{$ARGV[0]};
	usage() unless $parentTag;

	my $twig = XML::Twig->new(
		discard_spaces	=> 'true',
		pretty_print	=> 'indented',
		keep_atts_order	=> 'true',
		twig_handlers	=> {
			$parentTag	=> \&UpdateNodes,
			'opcount'	=> sub {
				my $opcount = $_;
				my $n = $opcount->att('n');
				my $cycles = $opcount->att('cycles');

				push @nodes, $opcount;
				$total_n += $n if $n;
				$total_cycles += $cycles if $cycles;
			}
		}
	);

	$twig->parse($io);
	$twig->flush();
}
