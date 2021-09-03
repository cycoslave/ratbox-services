/* src/dbhook.c
 *   Contains code for hooking into database tables.
 *
 * Copyright (C) 2006-2007 Lee Hardy <leeh@leeh.co.uk>
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
 * $Id: dbhook.c 23877 2007-04-24 20:17:01Z leeh $
 */
#include "stdinc.h"
#include "rserv.h"
#include "tools.h"
#include "rsdb.h"
#include "dbhook.h"
#include "log.h"
#include "event.h"

static dlink_list rsdb_hook_list;
static dlink_list dbh_schedule_list;

static void rsdb_hook_call(void *dbh);
static void rsdb_hook_schedule_execute(void);

struct rsdb_hook *
rsdb_hook_add(const char *table, const char *hook_value,
		int frequency, dbh_callback callback)
{
	struct rsdb_hook *dbh;

	if(EmptyString(table) || EmptyString(hook_value))
		return NULL;

	dbh = my_malloc(sizeof(struct rsdb_hook));

	dbh->table = my_strdup(table);
	dbh->hook_value = my_strdup(hook_value);
	dbh->callback = callback;

	dlink_add(dbh, &dbh->ptr, &rsdb_hook_list);

	eventAdd(hook_value, rsdb_hook_call, dbh, frequency);

	return dbh;
}

void
rsdb_hook_delete(dbh_callback callback)
{
	struct rsdb_hook *dbh;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, rsdb_hook_list.head)
	{
		dbh = ptr->data;

		if(dbh->callback == callback)
			break;

		dbh = NULL;
	}

	if(dbh == NULL)
		return;

	dlink_delete(&dbh->ptr, &rsdb_hook_list);

	eventDelete(rsdb_hook_call, dbh);

	my_free(dbh->table);
	my_free(dbh->hook_value);

	my_free(dbh);
}

static void
rsdb_hook_call(void *v_dbh)
{
	struct rsdb_table data;
	struct rsdb_hook *dbh = v_dbh;
	unsigned int *delid;
	unsigned int count;
	int i, retval;

	/* first, find how many rows we're expecting so we can allocate
	 * memory for the ids of those to delete
	 */
	rsdb_exec_fetch(&data, "SELECT COUNT(id) FROM %s WHERE hook = '%Q'",
			dbh->table, dbh->hook_value);

	if(data.row_count == 0)
	{
		mlog("fatal error: SELECT COUNT() returned 0 rows in rsdb_hook_call()");
		die(0, "problem with db file");
	}

	count = atoi(data.row[0][0]);
	rsdb_exec_fetch_end(&data);

	if(!count)
		return;

	delid = my_malloc(sizeof(unsigned int) * count);

	/* limit our result set to count entries, as we've only allocated
	 * memory for that many..
	 */
	rsdb_exec_fetch(&data, "SELECT id, data FROM %s WHERE hook = '%Q' LIMIT %u",
			dbh->table, dbh->hook_value, count);

	if(data.row_count)
	{
		for(i = 0; i < data.row_count; i++)
		{
			retval = (dbh->callback)(dbh, data.row[i][1]);

			if(retval)
				delid[i] = atoi(data.row[i][0]);
		}
	}

	rsdb_exec_fetch_end(&data);

	rsdb_transaction(RSDB_TRANS_START);

	for(i = 0; i < count; i++)
	{
		if(delid[i])
			rsdb_exec(NULL, "DELETE FROM %s WHERE id='%u'",
					dbh->table, delid[i]);
	}

	/* execute anything scheduled whilst this hook was running */
	rsdb_hook_schedule_execute();

	rsdb_transaction(RSDB_TRANS_END);

	my_free(delid);
}

void
rsdb_hook_schedule(dbh_schedule_callback callback, void *arg, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	struct dbh_schedule *sched_p;
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

	sched_p = my_malloc(sizeof(struct dbh_schedule));
	sched_p->callback = callback;
	sched_p->arg = arg;
	sched_p->sql = my_strdup(buf);

	dlink_add(sched_p, &sched_p->ptr, &dbh_schedule_list);
}

static void
rsdb_hook_schedule_execute(void)
{
	struct dbh_schedule *sched_p;
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, dbh_schedule_list.head)
	{
		sched_p = ptr->data;

		rsdb_exec(NULL, "%s", sched_p->sql);

		if(sched_p->callback)
			(sched_p->callback)(sched_p->arg);

		dlink_delete(&sched_p->ptr, &dbh_schedule_list);
		my_free(sched_p->sql);
		my_free(sched_p);
	}
}

