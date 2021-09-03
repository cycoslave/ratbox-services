/* src/rsdb_sqlite.h
 *   Contains the code for the sqlite database backend.
 *
 * Copyright (C) 2003-2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2007 ircd-ratbox development team
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
 * $Id: rsdb_sqlite3.c 23783 2007-04-01 17:49:20Z jilles $
 */
#include "stdinc.h"
#include "rsdb.h"
#include "rserv.h"
#include "log.h"

/* build sqlite, so use local version */
#ifdef SQLITE_BUILD
#include "sqlite3.h"
#else
#include <sqlite3.h>
#endif

struct sqlite3 *rserv_db;

/* rsdb_init()
 */
void
rsdb_init(void)
{
	if(sqlite3_open(DB_PATH, &rserv_db))
	{
		die(0, "Failed to open db file: %s", sqlite3_errmsg(rserv_db));
	}
}

void
rsdb_shutdown(void)
{
	if(rserv_db)
		sqlite3_close(rserv_db);
}

const char *
rsdb_quote(const char *src)
{
	static char buf[BUFSIZE*4];
	char *p = buf;

	/* cheap and dirty length check.. */
	if(strlen(src) >= (sizeof(buf) / 2))
		return NULL;

	while(*src)
	{
		if(*src == '\'')
			*p++ = '\'';

		*p++ = *src++;
	}

	*p = '\0';
	return buf;
}

static int
rsdb_callback_func(void *cbfunc, int argc, char **argv, char **colnames)
{
	rsdb_callback cb = cbfunc;
	(cb)(argc, (const char **) argv);
	return 0;
}

void
rsdb_exec(rsdb_callback cb, const char *format, ...)
{
	static char errmsg_busy[] = "Database file locked";
	static char buf[BUFSIZE*4];
	va_list args;
	char *errmsg;
	int errcount = 0;
	int i;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem with compiling sql");
		die(0, "problem with compiling sql statement");
	}

tryexec:
	if((i = sqlite3_exec(rserv_db, buf, (cb ? rsdb_callback_func : NULL), cb, &errmsg)))
	{
		switch(i)
		{
			case SQLITE_BUSY:
				/* sleep for upto 5 seconds in 10 iterations
				 * to try and get through..
				 */
				errcount++;

				if(errcount <= 10)
				{
					my_sleep(0, 500000);
					goto tryexec;
				}

				errmsg = errmsg_busy;					
				/* otherwise fall through */

			default:
				mlog("fatal error: problem with db file: %s", errmsg);
				die(0, "problem with db file");
				break;
		}
	}
}

void
rsdb_exec_insert(unsigned int *insert_id, const char *table_name, const char *field_name, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	va_list args;
	int i;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem with compiling sql");
		die(0, "problem with compiling sql statement");
	}

	rsdb_exec(NULL, "%s", buf);

	*insert_id = (unsigned int) sqlite3_last_insert_rowid(rserv_db);
}

void
rsdb_exec_fetch(struct rsdb_table *table, const char *format, ...)
{
	static char errmsg_busy[] = "Database file locked";
	static char buf[BUFSIZE*4];
	va_list args;
	char *errmsg;
	char **data;
	int pos;
	int errcount = 0;
	int i, j;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
		mlog("fatal error: length problem with compiling sql");
		die(0, "problem with compiling sql statement");
	}

tryexec:
	if((i = sqlite3_get_table(rserv_db, buf, &data, &table->row_count, &table->col_count, &errmsg)))
	{
		switch(i)
		{
			case SQLITE_BUSY:
				/* sleep for upto 5 seconds in 10 iterations
				 * to try and get through..
				 */
				errcount++;

				if(errcount <= 10)
				{
					my_sleep(0, 500000);
					goto tryexec;
				}
					
				errmsg = errmsg_busy;					
				/* otherwise fall through */

			default:
				mlog("fatal error: problem with db file: %s", errmsg);
				die(0, "problem with db file");
				break;
		}
	}

	/* we need to be able to free data afterward */
	table->arg = data;

	if(table->row_count == 0)
	{
		table->row = NULL;
		return;
	}

	/* sqlite puts the column names as the first row */
	pos = table->col_count;
	table->row = my_malloc(sizeof(char **) * table->row_count);
	for(i = 0; i < table->row_count; i++)
	{
		table->row[i] = my_malloc(sizeof(char *) * table->col_count);

		for(j = 0; j < table->col_count; j++)
		{
			table->row[i][j] = data[pos++];
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

	sqlite3_free_table((char **) table->arg);
}

void
rsdb_transaction(rsdb_transtype type)
{
	if(type == RSDB_TRANS_START)
		rsdb_exec(NULL, "BEGIN TRANSACTION");
	else if(type == RSDB_TRANS_END)
		rsdb_exec(NULL, "COMMIT TRANSACTION");
}

