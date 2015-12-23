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
# $kbyanc: dyntrace/tools/opgroup.pl,v 1.1 2004/12/27 10:36:45 kbyanc Exp $
#

#
# Utility for grouping opcodes in trace files.
#
# Requires the textproc/p5-XML-Twig port and its dependencies to be installed.
#

use strict;

use IO::Handle;
use XML::Twig;					# textproc/p5-XML-Twig


# All implemented grouping methods and the routine to call per opcount tag
# to perform the grouping.
my %groupings = (
	'opcode'	=> \&GroupByOpcode,
	'mnemonic'	=> \&GroupByMnemonic
);


# List of all opcount attributes we can combine the values of and the routine
# used to do the combining.
my %combineMap = (
	'n'		=> \&CombineAdd,
	'cycles'	=> \&CombineAdd,
	'min'		=> \&CombineMin,
	'max'		=> \&CombineMax
);


# List of all groups as defined by the grouping routine.
# Reset for each region tag in the trace file.
my %groups = ();


sub CombineAdd($$) {
	return $_[0] unless $_[1];
	return $_[1] unless $_[0];
	return $_[0] + $_[1];
}


sub CombineMin($$) {
	return $_[0] unless ($_[1] and $_[1] < $_[0]);
	return $_[1];
}


sub CombineMax($$) {
	return $_[0] unless ($_[1] and $_[1] > $_[0]);
	return $_[1];
}


sub Combine($$) {
	my ($group, $opcode) = @_;
	my $val;

	# Iterate through all of the attributes listed in the combineMap
	# and call the appropriate combiner routine for updating the group's
	# counter attribute to include any value in the opcode.
	while (my ($attrname, $combiner) = each %combineMap) {
		$val = &$combiner($group->att($attrname),
				 $opcode->att($attrname));
		$group->set_att($attrname => $val) if $val;
	}
}


#
# Group all counters for the same opcode, regardless of any prefixes.
# Opcodes are identified by their unique bitmask.  The prefix attribute
# is lost in the grouping.
#
sub GroupByOpcode($$) {
	my ($twig, $opcount) = @_;

	my $bitmask = $opcount->att('bitmask');

	# If we have not seen an opcode with this bitmask before, add it to
	# the group hash as a new group.  Remove the prefixes attribute as it
	# won't be meaningful once we are done grouping.
	if (!exists($groups{$bitmask})) {
		$groups{$bitmask} = $opcount;
		$opcount->del_att('prefixes');
		return 1;
	}

	# If we have already have a group for this opcode, integrate the
	# opcode's counters into the group's counters and discard the opcode.
	Combine($groups{$bitmask}, $opcount);
	$opcount->delete();
	return 1;
}


#
# Group all counters for opcodes with the same mnemonic.
# The prefix, detail, and bitmask attributes are lost in this grouping.
#
sub GroupByMnemonic($$) {
	my ($twig, $opcount) = @_;

	my $mnemonic = $opcount->att('mnemonic');

	if (!exists($groups{$mnemonic})) {
		$groups{$mnemonic} = $opcount;
		$opcount->del_att('prefixes', 'detail', 'bitmask');
		return 1;
	}

	Combine($groups{$mnemonic}, $opcount);
	$opcount->delete();
	return 1;
}


sub usage {
	use FindBin qw($Script);

	print STDERR << "EOU" ;
usage: $Script group-method

$Script reads an XML trace file as input, groups the opcodes in the trace
per the method specified by the group-method argument, and writes the updated
trace file to output.

For example:
	$Script mnemonic < myprog.trace > myprog-grouped.trace

The supported group-method values are:
EOU

	foreach my $key (keys %groupings) {
		print STDERR "\t$key\n";
	}

	exit(1);
}


# --- main ---
{

	my $io = new IO::Handle;
	$io->fdopen(fileno(STDIN), 'r');

	usage() unless scalar(@ARGV) == 1;
	my $grouper = $groupings{$ARGV[0]};
	usage() unless $grouper;

	my $twig = XML::Twig->new(
		discard_spaces	=> 'true',
		pretty_print	=> 'indented',
		keep_atts_order	=> 'true',
		twig_handlers	=> {
			'prefix'	=> sub { $_->delete(); },
			'region'	=> sub { %groups = (); },
			'opcount'	=> $grouper
		}
	);

	$twig->parse($io);
	$twig->flush();
}
