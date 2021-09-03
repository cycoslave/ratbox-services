/* src/rsdb_pgsql.c
 *   Contains the code for the postgresql database backend.
 *
 * Copyright (C) 2006-2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2006-2007 Aaron Sethman <androsyn@ratbox.org>
 * Copyright (C) 2006-2007 ircd-ratbox development team
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3.The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * $Id: rsdb_mysql.c 22247 2006-03-24 23:39:15Z leeh $
 */
#include "stdinc.h"
#include <libpq-fe.h>
#include "rsdb.h"
#include "rserv.h"
#include "conf.h"
#include "log.h"

#define RSDB_MAXCOLS			30
#define RSDB_MAX_RECONNECT_TIME		30

PGconn *rsdb_database;
int rsdb_doing_transaction;

static int rsdb_connect(int initial);

/* rsdb_init()
 */
void
rsdb_init(void)
{
	if(EmptyString(config_file.db_name) || EmptyString(config_file.db_host) ||
	   EmptyString(config_file.db_username) || EmptyString(config_file.db_password))
	{
		die(0, "Missing conf options in database {};");
	}

	rsdb_connect(1);
}

/* rsdb_connect()
 * attempts to connect to the postgresql database
 *
 * inputs	- initial, set if we're starting up
 * outputs	- 0 on success, > 0 on fatal error, < 0 on non-fatal error
 * side effects - connection to the postgresql database is attempted
 */
static int
rsdb_connect(int initial)
{

	rsdb_database = PQsetdbLogin(config_file.db_host, NULL, NULL, NULL, 
	                             config_file.db_name, config_file.db_username, 
	                             config_file.db_password);

	if(rsdb_database != NULL && PQstatus(rsdb_database) == CONNECTION_OK)
		return 0;

	/* all errors on startup are fatal */
	if(initial)
		die(0, "Unable to connect to postgresql database: %s",
			PQerrorMessage(rsdb_database));

	switch(PQstatus(rsdb_database))
	{
		case CONNECTION_BAD:
			return -1;

		default:
			return 1;
	}

	/* NOTREACHED */
	return 1;
}

void
rsdb_shutdown(void)
{
	PQfinish(rsdb_database);
}

static void
rsdb_try_reconnect(void)
{
	time_t expire_time = CURRENT_TIME + RSDB_MAX_RECONNECT_TIME;

	mlog("Warning: unable to connect to database, stopping all functions until we recover");

	while(CURRENT_TIME < expire_time)
	{
		PQfinish(rsdb_database);
		if(!rsdb_connect(0))
			return;

		my_sleep(1, 0);
		set_time();
	}

	die(0, "Unable to connect to postgresql database: %s", PQerrorMessage(rsdb_database));
}

/* rsdb_handle_connerror()
 * Handles a connection error from the database
 *
 * inputs	- result pointer, sql command to execute
 * outputs	-
 * side effects - will attempt to deal with non-fatal errors, and reexecute
 *		  the query if it can
 */
static void
rsdb_handle_connerror(PGresult **rsdb_result, const char *buf)
{
	if(rsdb_doing_transaction)
	{
		mlog("fatal error: problem with db file during transaction: %s",
			PQerrorMessage(rsdb_database));
		die(0, "problem with db file");
		return;
	}
	
	switch(PQstatus(rsdb_database))
	{
		case CONNECTION_BAD:
			PQreset(rsdb_database);

			if(PQstatus(rsdb_database) != CONNECTION_OK)
				rsdb_try_reconnect();

			break;

		default:
			mlog("fatal error: problem with db file: %s",
				PQerrorMessage(rsdb_database));
			die(0, "problem with db file");
			return;
	}

	/* connected back successfully */
	if(buf)
	{
		if((*rsdb_result = PQexec(rsdb_database, buf)) == NULL)
		{
			mlog("fatal error: problem with db file: %s",
				PQerrorMessage(rsdb_database));
			die(0, "problem with db file");
		}
	}
}

const char *
rsdb_quote(const char *src)
{
	static char buf[BUFSIZE*4];
	unsigned long length;

	length = strlen(src);

	if(length >= (sizeof(buf) / 2))
		die(0, "length problem compiling sql statement");

	PQescapeString(buf, src, length);
	return buf;
}

void
rsdb_exec(rsdb_callback cb, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	static const char *coldata[RSDB_MAXCOLS+1];
	PGresult *rsdb_result;
	va_list args;
	unsigned int field_count, row_count;
	int i;
	int cur_row;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem compiling sql statement: %s", buf);
		die(0, "length problem compiling sql statement");
	}

	if((rsdb_result = PQexec(rsdb_database, buf)) == NULL)
		rsdb_handle_connerror(&rsdb_result, buf);

	switch(PQresultStatus(rsdb_result))
	{
		case PGRES_FATAL_ERROR:
		case PGRES_BAD_RESPONSE:
		case PGRES_EMPTY_QUERY: /* i'm gonna guess this is bad for us too */
			mlog("fatal error: problem with db file: %s",
				PQresultErrorMessage(rsdb_result));
			die(0, "problem with db file");
			break;
		default:
			break;
	}

	field_count = PQnfields(rsdb_result);
	row_count = PQntuples(rsdb_result);

	if(field_count > RSDB_MAXCOLS)
		die(0, "too many columns in result set -- contact the ratbox team");

	if(!field_count || !row_count || !cb)
	{
		PQclear(rsdb_result);
		return;
	}		
	
	for(cur_row = 0; cur_row < row_count; cur_row++)
	{
		for(i = 0; i < field_count; i++)
		{
			coldata[i] = PQgetvalue(rsdb_result, cur_row, i);
		}
		coldata[i] = NULL;

		(cb)((int) field_count, coldata);
	}
	PQclear(rsdb_result);
}

void
rsdb_exec_insert(unsigned int *insert_id, const char *table_name, const char *field_name, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	static struct rsdb_table data;
	va_list args;
	int i;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem compiling sql statement: %s", buf);
		die(0, "length problem compiling sql statement");
	}

	rsdb_exec(NULL, "%s", buf);

	rsdb_exec_fetch(&data, "SELECT currval(pg_get_serial_sequence('%s', '%s'))",
			table_name, field_name);

	if(data.row_count == 0)
	{
		mlog("fatal error: SELECT currval(pg_get_serial_sequence('%s', '%s')) returned 0 rows",
			table_name, field_name);
		die(0, "problem with db file");
	}

	*insert_id = atoi(data.row[0][0]);

	rsdb_exec_fetch_end(&data);
}

void
rsdb_exec_fetch(struct rsdb_table *table, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	PGresult *rsdb_result;
	va_list args;
	int i, j;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem compiling sql statement: %s", buf);
		die(0, "length problem compiling sql statement");
	}

	if((rsdb_result = PQexec(rsdb_database, buf)) == NULL)
		rsdb_handle_connerror(&rsdb_result, buf);

	switch(PQresultStatus(rsdb_result))
	{
		case PGRES_FATAL_ERROR:
		case PGRES_BAD_RESPONSE:
		case PGRES_EMPTY_QUERY: /* i'm gonna guess this is bad for us too */
			mlog("fatal error: problem with db file: %s",
				PQresultErrorMessage(rsdb_result));
			die(0, "problem with db file");
		default:
			break;
	}

	
	table->row_count = PQntuples(rsdb_result);
	table->col_count = PQnfields(rsdb_result);
	table->arg = rsdb_result;

	if(!table->row_count || !table->col_count)
	{
		table->row = NULL;
		return;
	}

	table->row = my_malloc(sizeof(char **) * table->row_count);

	for(i = 0; i < table->row_count; i++)
	{
		table->row[i] = my_malloc(sizeof(char *) * table->col_count);

		for(j = 0; j < table->col_count; j++)
		{
			table->row[i][j] = PQgetvalue(rsdb_result, i, j);
		}
	}
}

void
rsdb_exec_fetch_end(struct rsdb_table *table)
{
	int i;

	for(i = 0; i < table->row_count; i++)
	{
		my_free(table->row[i]);
	}

	my_free(table->row);

	PQclear(table->arg);
}

void
rsdb_transaction(rsdb_transtype type)
{
	if(type == RSDB_TRANS_START)
	{
		rsdb_exec(NULL, "START TRANSACTION");
		rsdb_doing_transaction = 1;
	}
	else if(type == RSDB_TRANS_END)
	{
		rsdb_exec(NULL, "COMMIT");
		rsdb_doing_transaction = 0;
	}
}

