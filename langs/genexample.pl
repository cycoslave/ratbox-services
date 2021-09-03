#!/usr/bin/perl -w

open(INPUT, "<../src/messages.c");
open(OUTPUT, ">example.lang");

print OUTPUT "# example.lang\n";
print OUTPUT "#   Contains a base translation file to rework into other languages\n";
print OUTPUT "#\n";
print OUTPUT "# Copyright (C) 2007-2008 Lee Hardy <leeh\@leeh.co.uk>\n";
print OUTPUT "# Copyright (C) 2007-2008 ircd-ratbox development team\n";
print OUTPUT "\n";
print OUTPUT "# The 'code' of the language\n";
print OUTPUT "set LANG_CODE\t\t\"en\"\n";
print OUTPUT "\n";
print OUTPUT "# The description of the language, which appears in the help\n";
print OUTPUT "set LANG_DESCRIPTION\t\"English (Example)\"\n";
print OUTPUT "\n";

my $parsing = 0;

while($parsing == 0)
{
	$_ = <INPUT>;
	chomp;

	$parsing = 1 if($_ =~ /START_GENEXAMPLE_PARSING/);
}

while(<INPUT>)
{
	chomp;

	exit if($_ =~ /STOP_GENEXAMPLE_PARSING/);

	if($_ =~ /^$/)
	{
		print OUTPUT "\n";
		next;
	}
	elsif($_ =~ /{ ([A-Z_]+,)\s+(\".*?\")\s+},/)
	{
		my $t = "\t" . (length($1) < 16 ? "\t" : "") . (length($1) < 24 ? "\t" : "");

		print OUTPUT "$1$t$2\n";
	}
	elsif($_ =~ m%/\* (.*) \*\/%)
	{
		print OUTPUT "# $1\n";
	}
	elsif($_ =~ m%/\*%)
	{
		while(<INPUT>)
		{
			chomp;
			last if($_ =~ m%\*/%);
			
			if($_ =~ /\* (.*)/)
			{
				print OUTPUT "# $1\n";
			}
			else
			{
				print "Unknown line: $_\n";
			}
		}
	}
	else
	{
		print "Unknown line: $_\n";
	}
}

