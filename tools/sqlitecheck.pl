#!/usr/bin/perl -w
#
# sqlitecheck.pl
# Outputs SQL commands for checking an sqlite database will adhere to
# length limits on strings when imported into a database that actually
# cares about lengths.
#
# Takes the schema for the new database as its argument, outputs commands
# for use with the old database.
#
# Copyright (C) 2006 Lee Hardy <lee -at- leeh.co.uk>
# Copyright (C) 2006 ircd-ratbox development team
#
# $Id$

use strict;

unless($ARGV[0])
{
	print "Usage: ./sqlitecheck.pl <schema>\n";
	exit;
}

unless(open(INPUT, "<$ARGV[0]"))
{
	print "Unable to open $ARGV[0]\n";
	exit;
}

my $table = "";

while(<INPUT>)
{
	chomp;

	if($_ =~ /^CREATE TABLE (.*) \(/)
	{
		$table = $1;
		next;
	}
	elsif($_ =~ /\s*(.*) VARCHAR\((\d+)\)/)
	{
		print "SELECT * FROM $table WHERE LENGTH($1) > $2;\n";
	}
}
