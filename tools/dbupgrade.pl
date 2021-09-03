#!/usr/bin/perl -w
#
# dbupgrade.pl
# This script generates the SQL commands for the database alterations needed
# when upgrading versions of ratbox-services.
#
# It takes the version of ratbox-services as an argument, eg:
#   ./dbupgrade.pl ratbox-services-1.0.3
# You may leave off the "ratbox-services-" if you wish.  You should NOT
# however leave off extra version information like "rc1".
#
# Note, this script will only deal with actual releases, not svn copies.
# 
# Copyright (C) 2006 Lee Hardy <lee -at- leeh.co.uk>
# Copyright (C) 2006 ircd-ratbox development team
#
# $Id: dbupgrade.pl 26773 2010-02-04 18:48:27Z leeh $

use strict;

require "definetolength.pl";

unless($ARGV[0] && $ARGV[1])
{
	print "Usage: dbupgrade.pl <ratbox-services version> <sqlite|mysql|pgsql> [include_path]\n";
	print "Eg, ./dbupgrade.pl 1.0.3 sqlite\n";
	exit;
}

unless(-r "../include/setup.h")
{
	print("Unable to read ../include/setup.h, please run configure first\n");
	exit();
}

my %versionlist = (
	"1.0.0"		=> 1,
	"1.0.1"		=> 1,
	"1.0.2"		=> 1,
	"1.0.3"		=> 1,
	"1.1.0beta1"	=> 2,
	"1.1.0beta2"	=> 2,
	"1.1.0beta3"	=> 2,
	"1.1.0beta4"	=> 3,
	"1.1.0rc1"	=> 4,
	"1.1.0rc2"	=> 5,
	"1.1.0rc3"	=> 5,
	"1.1.0"		=> 5,
	"1.1.1"		=> 5,
	"1.1.2"		=> 5,
	"1.1.3"		=> 5,
	"1.2.0beta1"	=> 6,
	"1.2.0beta2"	=> 6,
	"1.2.0rc1"	=> 6,
	"1.2.0rc2"	=> 6,
	"1.2.0"		=> 6,
	"1.2.1"		=> 6
);

my $version = $ARGV[0];
my $dbtype = $ARGV[1];
my %vals;

$version =~ s/^ircd-ratbox-//;

my $currentver = $versionlist{"$version"};
my $upgraded = 0;

if(!$currentver)
{
	print "Unknown version $version\n";
	exit;
}

if($dbtype ne "sqlite" && $dbtype ne "mysql" && $dbtype ne "pgsql")
{
	print "Unknown database type $dbtype\n";
	exit;
}

if($ARGV[2])
{
	%vals = &parse_includes("$ARGV[2]");
}
else
{
	%vals = &parse_includes("../include");
}

while(my ($key, $value) = each(%vals))
{
	if($value == 1)
	{
		print "Unable to set $key -- include path must be wrong.\n";
		exit;
	}
}

if($currentver < 2)
{
	print "-- To version 1.1.0beta1\n";

	if($dbtype eq "sqlite")
	{
		print "CREATE TABLE users_resetpass (\n";
		print "    username TEXT, token TEXT, time INTEGER,\n";
		print "    PRIMARY KEY(username)\n";
		print ");\n";
		print "CREATE TABLE users_sync (\n";
		print "    id INTEGER PRIMARY KEY, hook TEXT, data TEXT\n";
		print ");\n";
		print "ALTER TABLE users ADD COLUMN verify_token TEXT;\n";
		print "ALTER TABLE users ADD COLUMN suspend_reason TEXT;\n";
		print "ALTER TABLE channels ADD COLUMN suspend_reason TEXT;\n";
	}
	elsif($dbtype eq "mysql")
	{
		print "CREATE TABLE users_sync (\n";
		print "    id INTEGER AUTO_INCREMENT, hook VARCHAR(50) NOT NULL, data TEXT,\n";
		print "    PRIMARY KEY(id)\n";
		print ");\n";
	}
	else
	{
		print "CREATE TABLE users_sync (\n";
		print "    id SERIAL, hook VARCHAR(50) NOT NULL, data TEXT,\n";
		print "    PRIMARY KEY(id)\n";
		print ");\n";
	}

	if($dbtype eq "mysql" || $dbtype eq "pgsql")
	{
		print "CREATE TABLE users_resetpass (\n";
		print "    username VARCHAR(".$vals{"USERREGNAME_LEN"}.") NOT NULL, token VARCHAR(10), time INTEGER,\n";
		print "    PRIMARY KEY(username)\n";
		print ");\n";
		print "ALTER TABLE users ADD COLUMN verify_token VARCHAR(8);\n";
		print "ALTER TABLE users ADD COLUMN suspend_reason VARCHAR(".$vals{"SUSPENDREASONLEN"}.");\n";
		print "ALTER TABLE channels ADD COLUMN suspend_reason VARCHAR(".$vals{"SUSPENDREASONLEN"}.");\n";
	}


	print "CREATE TABLE global_welcome (\n";
	print "    id INTEGER, text TEXT,\n";
	print "    PRIMARY KEY(id)\n";
	print ");\n";

	print "\n";

	$upgraded = 1;
}

if($currentver < 3)
{
	print "-- To version 1.1.0beta4\n";

	if($dbtype eq "mysql" || $dbtype eq "pgsql")
	{
		print "CREATE TABLE channels_dropowner (\n";
		print "    chname VARCHAR(".$vals{"CHANNELLEN"}.") NOT NULL, token VARCHAR(10), time INTEGER,\n";
		print "    PRIMARY KEY(chname)\n";
		print ");\n";
		print "CREATE TABLE users_resetemail (\n";
		print "    username VARCHAR(".$vals{"USERREGNAME_LEN"}.") NOT NULL, token VARCHAR(10),\n";
		print "    email VARCHAR(".$vals{"EMAILLEN"}.") DEFAULT NULL, time INTEGER,\n";
		print "    PRIMARY KEY (username)\n";
		print ");\n";
	}
	else
	{
		print "CREATE TABLE channels_dropowner (\n";
		print "    chname TEXT, token TEXT, time INTEGER,\n";
		print "    PRIMARY KEY(chname)\n";
		print ");\n";
		print "CREATE TABLE users_resetemail (\n";
		print "    username TEXT, token TEXT, email TEXT DEFAULT NULL, time INTEGER,\n";
		print "    PRIMARY KEY(username)\n";
		print ");\n";
	}

	if($dbtype eq "mysql")
	{
		print "ALTER TABLE channels_dropowner ADD INDEX (time);\n";
		print "ALTER TABLE users_resetpass ADD INDEX (time);\n";
		print "ALTER TABLE users_resetemail ADD INDEX (time);\n";
	}
	elsif($dbtype eq "pgsql")
	{
		print "CREATE INDEX dropowner_time_idx ON channels_dropowner (time);\n";
		print "CREATE INDEX resetpass_time_idx ON users_resetpass (time);\n";
		print "CREATE INDEX resetemail_time_idx ON users_resetemail (time);\n";
	}

	print "UPDATE operserv SET chname=LOWER(chname);\n";
	print "UPDATE operbot SET chname=LOWER(chname);\n";
	print "UPDATE operbans SET mask=LOWER(mask);\n";
	print "\n";

	$upgraded = 1;
}

if($currentver < 4)
{
	print "-- To version 1.1.0rc1\n";

	if($dbtype eq "mysql" || $dbtype eq "pgsql")
	{
		print "CREATE TABLE email_banned_domain (\n";
		print "    domain VARCHAR(255) NOT NULL,\n";
		print "    PRIMARY KEY(domain)\n";
		print ");\n";
	}
	else
	{
		print "CREATE TABLE email_banned_domain (\n";
		print "    domain TEXT NOT NULL,\n";
		print "    PRIMARY KEY(domain)\n";
		print ");\n";
	}

	print "\n";

	$upgraded = 1;
}

if($currentver < 5)
{
	print "-- To version 1.1.0rc2\n";

	if($dbtype eq "mysql")
	{
		print "ALTER TABLE members ADD PRIMARY KEY(chname, username);\n";
		print "ALTER TABLE members ADD INDEX (chname);\n";
		print "ALTER TABLE members ADD INDEX (username);\n";
		print "ALTER TABLE bans ADD PRIMARY KEY(chname, mask);\n";
		print "ALTER TABLE bans ADD INDEX (chname);\n";
		print "UPDATE channels SET url=SUBSTR(url,1,".$vals{"URLLEN"}.");\n";
		print "ALTER TABLE channels CHANGE url url VARCHAR(".$vals{"URLLEN"}.");\n";
	}
	elsif($dbtype eq "pgsql")
	{
		print "ALTER TABLE members ADD PRIMARY KEY(chname, username);\n";
		print "CREATE INDEX members_chname_idx ON members (chname);\n";
		print "CREATE INDEX members_username_idx ON members (username);\n";
		print "ALTER TABLE bans ADD PRIMARY KEY(chname, mask);\n";
		print "CREATE INDEX bans_chname_idx ON bans (chname);\n";
		print "UPDATE channels SET url=SUBSTR(url,1,".$vals{"URLLEN"}.");\n";
		print "ALTER TABLE channels ALTER COLUMN url TYPE VARCHAR(".$vals{"URLLEN"}.");\n";
	}

	print "\n";

	$upgraded = 1;
}

if($currentver < 6)
{
	print "-- To version 1.2.0beta1\n";

	print "ALTER TABLE users ADD COLUMN language VARCHAR(255) DEFAULT '';\n";

	if($dbtype eq "mysql")
	{
		print "ALTER TABLE users ADD COLUMN suspend_time INT UNSIGNED DEFAULT '0';\n";
		print "ALTER TABLE channels ADD COLUMN suspend_time INT UNSIGNED DEFAULT '0';\n";
		print "CREATE TABLE ignore_hosts (\n";
		print "    hostname VARCHAR(255) NOT NULL,\n";
		print "    oper VARCHAR(" . $vals{"OPERNAMELEN"} . ") NOT NULL,\n";
		print "    reason VARCHAR(255) NOT NULL,\n";
		print "    PRIMARY KEY(hostname)\n";
		print ");\n";
		print "CREATE TABLE operbans_regexp (\n";
		print "    id INTEGER AUTO_INCREMENT,\n";
		print "    regex VARCHAR(255) NOT NULL,\n";
		print "    reason VARCHAR(" . $vals{"REASONLEN"} . ") NOT NULL,\n";
		print "    hold INTEGER,\n";
		print "    create_time INTEGER,\n";
		print "    oper VARCHAR(" . $vals{"OPERNAMELEN"} . ") NOT NULL,\n";
		print "    PRIMARY KEY(id)\n";
		print ");\n";
		print "CREATE TABLE operbans_regexp_neg (\n";
		print "    id INTEGER AUTO_INCREMENT,\n";
		print "    parent_id INTEGER NOT NULL,\n";
		print "    regex VARCHAR(255) NOT NULL,\n";
		print "    oper VARCHAR(" . $vals{"OPERNAMELEN"} . ") NOT NULL,\n";
		print "    PRIMARY KEY(id)\n";
		print ");\n";
		print "ALTER TABLE users DROP PRIMARY KEY;\n";
		print "ALTER TABLE users ADD COLUMN id INTEGER AUTO_INCREMENT PRIMARY KEY FIRST;\n";
		print "ALTER TABLE users ADD UNIQUE(username);\n";
		print "CREATE TABLE memos(\n";
		print "    id INTEGER AUTO_INCREMENT,\n";
		print "    user_id INTEGER NOT NULL,\n";
		print "    source_id INTEGER NOT NULL,\n";
		print "    source VARCHAR(" . $vals{"USERREGNAME_LEN"} . ") NOT NULL,\n";
		print "    timestamp INTEGER UNSIGNED DEFAULT '0',\n";
		print "    flags INTEGER UNSIGNED DEFAULT '0',\n";
		print "    text TEXT,\n";
		print "    PRIMARY KEY(id)\n";
		print ");\n";
	}
	elsif($dbtype eq "pgsql")
	{
		print "ALTER TABLE users ADD COLUMN suspend_time INTEGER DEFAULT '0';\n";
		print "ALTER TABLE channels ADD COLUMN suspend_time INTEGER DEFAULT '0';\n";
		print "CREATE TABLE ignore_hosts (\n";
		print "    hostname VARCHAR(255) NOT NULL,\n";
		print "    oper VARCHAR(" . $vals{"OPERNAMELEN"} . ") NOT NULL,\n";
		print "    reason VARCHAR(255) NOT NULL,\n";
		print "    PRIMARY KEY(hostname)\n";
		print ");\n";
		print "CREATE TABLE operbans_regexp (\n";
		print "    id SERIAL,\n";
		print "    regex VARCHAR(255) NOT NULL,\n";
		print "    reason VARCHAR(" . $vals{"REASONLEN"} . ") NOT NULL,\n";
		print "    hold INTEGER,\n";
		print "    create_time INTEGER,\n";
		print "    oper VARCHAR(" . $vals{"OPERNAMELEN"} . ") NOT NULL,\n";
		print "    PRIMARY KEY(id)\n";
		print ");\n";
		print "CREATE TABLE operbans_regexp_neg (\n";
		print "    id SERIAL,\n";
		print "    parent_id BIGINT NOT NULL,\n";
		print "    regex VARCHAR(255) NOT NULL,\n";
		print "    oper VARCHAR(" . $vals{"OPERNAMELEN"} . ") NOT NULL,\n";
		print "    PRIMARY KEY(id)\n";
		print ");\n";
		print "ALTER TABLE users DROP CONSTRAINT users_pkey CASCADE;\n";
		print "ALTER TABLE users ADD UNIQUE(username);\n";
		print "ALTER TABLE members ADD FOREIGN KEY (username) REFERENCES users (username) MATCH FULL;\n";
		print "ALTER TABLE nicks ADD FOREIGN KEY (username) REFERENCES users (username) MATCH FULL;\n";
		print "ALTER TABLE users ADD COLUMN id SERIAL PRIMARY KEY;\n";
		print "CREATE TABLE memos (\n";
		print "    id SERIAL,\n";
		print "    user_id BIGINT NOT NULL,\n";
		print "    source_id BIGINT NOT NULL,\n";
		print "    source VARCHAR(" . $vals{"USERREGNAME_LEN"} . ") NOT NULL,\n";
		print "    timestamp INTEGER DEFAULT '0',\n";
		print "    flags INTEGER DEFAULT '0',\n";
		print "    text TEXT,\n";
		print "    PRIMARY KEY(id),\n";
		print "    FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE\n";
		print ");\n";
	}
	else
	{
		print "ALTER TABLE users ADD COLUMN suspend_time INTEGER DEFAULT '0';\n";
		print "ALTER TABLE channels ADD COLUMN suspend_time INTEGER DEFAULT '0';\n";
		print "CREATE TABLE ignore_hosts (\n";
		print "    hostname TEXT NOT NULL,\n";
		print "    oper TEXT NOT NULL,\n";
		print "    reason TEXT NOT NULL,\n";
		print "    PRIMARY KEY(hostname)\n";
		print ");\n";
		print "CREATE TABLE operbans_regexp (\n";
		print "    id INTEGER PRIMARY KEY,\n";
		print "    regex TEXT NOT NULL,\n";
		print "    reason TEXT NOT NULL,\n";
		print "    hold INTEGER,\n";
		print "    create_time INTEGER,\n";
		print "    oper TEXT NOT NULL\n";
		print ");\n";
		print "CREATE TABLE operbans_regexp_neg (\n";
		print "    id INTEGER PRIMARY KEY,\n";
		print "    parent_id INTEGER NOT NULL,\n";
		print "    regex TEXT NOT NULL,\n";
		print "    oper TEXT NOT NULL\n";
		print ");\n";
		print "CREATE TABLE users_tmpmerge (\n";
		print "    id INTEGER PRIMARY KEY, username TEXT, password TEXT, email TEXT, suspender TEXT,\n";
		print "    suspend_reason TEXT, suspend_time INTEGER DEFAULT '0', reg_time INTEGER,\n";
		print "    last_time INTEGER, flags INTEGER, verify_token TEXT, language TEXT DEFAULT ''\n";
		print ");\n";
		print "INSERT INTO users_tmpmerge (username, password, email, suspender, suspend_reason,\n";
		print "suspend_time, reg_time, last_time, flags, verify_token, language)\n";
		print "SELECT username, password, email, suspender, suspend_reason,\n";
		print "suspend_time, reg_time, last_time, flags, verify_token, language FROM users;\n";
		print "ALTER TABLE users RENAME TO users_pre_dbupgrade;\n";
		print "ALTER TABLE users_tmpmerge RENAME TO users;\n";
		print "CREATE UNIQUE INDEX users_username_unique ON users (username);\n";
		print "CREATE TABLE memos (\n";
		print "    id INTEGER PRIMARY KEY,\n";
		print "    user_id INTEGER NOT NULL,\n";
		print "    source_id INTEGER NOT NULL,\n";
		print "    source TEXT NOT NULL,\n";
		print "    timestamp INTEGER DEFAULT '0',\n";
		print "    flags INTEGER DEFAULT '0',\n";
		print "    text TEXT\n";
		print ");\n";
	}

	print "\n";

	$upgraded = 1;
}

if($upgraded == 0)
{
	print "No database modification required.\n";
}

exit;
