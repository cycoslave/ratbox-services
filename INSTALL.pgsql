pgsql database backend
----------------------

**************************************************************
* EVEN THOUGH YOU ARE USING PGSQL, DO NOT ALTER ANY DATABASE *
* TABLES WITHOUT FIRST READING doc/database_mod.txt          *
**************************************************************

You must first, as a user with the appropriate access, create the database
ratbox-services will use, and the user it will connect as.

Add the user via:
	CREATE USER rserv WITH UNENCRYPTED PASSWORD 'password' NOCREATEDB NOCREATEROLE NOCREATEUSER;
The password here should just be random.

Create the database via:
	CREATE DATABASE ratbox_services WITH OWNER=rserv;

The schema must then be generated as it depends on length values set at
compile time:
	cd /path/to/source/tools/
	./generate-schema.pl
	
Then initialise the database:
	psql -W ratbox_services rserv < /path/to/source/tools/schema-pgsql.txt

The username (default: rserv), database name (default: ratbox_services) and
password must be set in the config for ratbox-services to work.
