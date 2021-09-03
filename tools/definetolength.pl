# definetolength.pl
# Reads include files, extracting the lengths from the selected defines.
#
# Copyright (C) 2006 Lee Hardy <lee -at- leeh.co.uk>
# Copyright (C) 2006 ircd-ratbox development team
#
# $Id: definetolength.pl 22896 2006-07-18 18:06:04Z leeh $

my %lengths = (
	"USERREGNAME_LEN" => 1,
	"PASSWDLEN" => 1,
	"EMAILLEN" => 1,
	"OPERNAMELEN" => 1,
	"NICKLEN" => 1,
	"USERLEN" => 1,
	"CHANNELLEN" => 1,
	"TOPICLEN" => 1,
	"HOSTLEN" => 1,
	"REALLEN" => 1,
	"REASONLEN" => 1,
	"SUSPENDREASONLEN" => 1,
	"URLLEN" => 1
);

my @srcs = ("setup.h", "rserv.h", "channel.h", "client.h");

sub parse_includes
{
	my $path = shift;

	foreach my $i (@srcs)
	{
		unless(open(INPUT, '<', "$path/$i"))
		{
			next;
		}

		while(<INPUT>)
		{
			chomp;

			if($_ =~ /^#define ([A-Z_]+)\s+\(?(\d+)/)
			{
				$key = $1;
				$value = $2;

				$lengths{"$key"} = $value
					if($lengths{"$key"});
			}
		}

		close(INPUT);
	}

	return %lengths;
}

