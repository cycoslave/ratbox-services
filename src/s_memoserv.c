/* src/s_memoserv.c
 *   Contains the code for memo services
 *
 * Copyright (C) 2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2007 ircd-ratbox development team
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
 * $Id: s_alis.c 23596 2007-02-05 21:35:27Z leeh $
 */
#include "stdinc.h"

#ifdef ENABLE_MEMOSERV
#include "rsdb.h"
#include "rserv.h"
#include "langs.h"
#include "service.h"
#include "io.h"
#include "client.h"
#include "channel.h"
#include "c_init.h"
#include "log.h"
#include "conf.h"
#include "hook.h"
#include "s_userserv.h"

#define MS_FLAGS_READ			0x0001

static void init_s_memoserv(void);

static struct client *memoserv_p;

static int s_memo_list(struct client *, struct lconn *, const char **, int);
static int s_memo_read(struct client *, struct lconn *, const char **, int);
static int s_memo_send(struct client *, struct lconn *, const char **, int);
static int s_memo_delete(struct client *, struct lconn *, const char **, int);

static struct service_command memoserv_command[] =
{
	{ "LIST",	&s_memo_list,	0, NULL, 1, 0L, 1, 0, 0 },
	{ "READ",	&s_memo_read,	1, NULL, 1, 0L, 1, 0, 0 },
	{ "SEND",	&s_memo_send,	2, NULL, 1, 0L, 1, 0, 0 },
	{ "DELETE",	&s_memo_delete,	1, NULL, 1, 0L, 1, 0, 0 },
};

static struct service_handler memoserv_service = {
	"MEMOSERV", "MEMOSERV", "memoserv", "services.int", "Memo Services",
        0, 0, memoserv_command, sizeof(memoserv_command), NULL, init_s_memoserv, NULL
};

static int h_memoserv_user_login(void *client, void *unused);

void
preinit_s_memoserv(void)
{
	memoserv_p = add_service(&memoserv_service);
}

static void
init_s_memoserv(void)
{
	hook_add(h_memoserv_user_login, HOOK_USER_LOGIN);
}

static int
h_memoserv_user_login(void *v_client_p, void *unused)
{
	struct client *client_p;
	struct user_reg *ureg_p;
	struct rsdb_table data;
	unsigned int memocount;

	client_p = (struct client *) v_client_p;
	ureg_p = client_p->user->user_reg;

	rsdb_exec_fetch(&data, "SELECT COUNT(*) FROM memos WHERE user_id='%d' AND (flags & %u) = 0",
			ureg_p->id, MS_FLAGS_READ);

	if(data.row_count == 0)
	{
		mlog("fatal error: SELECT COUNT() returned 0 rows in s_memo_send()");
		die(0, "problem with db file");
	}

	memocount = atoi(data.row[0][0]);

	if(memocount > 0)
		service_err(memoserv_p, client_p, SVC_MEMO_UNREAD_COUNT, memocount);

	return 0;
}

static int
s_memo_list(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct rsdb_table data;
	int i;
	unsigned int read_count = 0;
	unsigned int unread_count = 0;

	/* if they have no memos, we wont reach the bottom of this function */
	zlog(memoserv_p, 3, 0, 0, client_p, NULL, "LIST");

	rsdb_exec_fetch(&data, "SELECT id, source, timestamp, flags FROM memos WHERE user_id='%d'",
			client_p->user->user_reg->id);

	for(i = 0; i < data.row_count; i++)
	{
		if((atoi(data.row[i][3]) & MS_FLAGS_READ) == 0)
			unread_count++;
		else
			read_count++;
	}

	service_err(memoserv_p, client_p, SVC_MEMO_LIST, unread_count, read_count);

	if(unread_count == 0 && read_count == 0)
		return 1;

	service_err(memoserv_p, client_p, SVC_MEMO_LISTSTART);

	for(i = 0; i < data.row_count; i++)
	{
		service_error(memoserv_p, client_p, "   %c %9d %s %s",
				(atoi(data.row[i][3]) & MS_FLAGS_READ) ? ' ' : '*',
				atoi(data.row[i][0]), get_time(atoi(data.row[i][2]), 0),
				data.row[i][1]);
	}

	service_err(memoserv_p, client_p, SVC_ENDOFLIST);

	return 2;
}

static int
s_memo_read(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct rsdb_table data;
	char *endptr;
	unsigned int id;

	id = strtol(parv[0], &endptr, 10);

	if(!strcasecmp(parv[0], "ALL"))
	{
		int i;

		rsdb_exec_fetch(&data, "SELECT id, source, timestamp, text FROM memos WHERE user_id='%u'",
				client_p->user->user_reg->id);

		for(i = 0; i < data.row_count; i++)
		{
			service_err(memoserv_p, client_p, SVC_MEMO_READ,
					atoi(data.row[i][0]), get_time(atoi(data.row[i][2]), 0),
					data.row[i][1], data.row[i][3]);
		}

		rsdb_exec_fetch_end(&data);

		rsdb_exec(NULL, "UPDATE memos SET flags = (flags|%u) WHERE user_id='%u'",
				MS_FLAGS_READ, client_p->user->user_reg->id);

		service_err(memoserv_p, client_p, SVC_ENDOFLIST);
		return 2;
	}
	else if(EmptyString(endptr) && id > 0)
	{
		rsdb_exec_fetch(&data, "SELECT id, source, timestamp, text FROM memos WHERE user_id='%u' AND id='%u'",
				client_p->user->user_reg->id, id);

		if(data.row_count < 1)
		{
			service_err(memoserv_p, client_p, SVC_MEMO_INVALID, parv[0]);
			rsdb_exec_fetch_end(&data);
			return 1;
		}

		service_err(memoserv_p, client_p, SVC_MEMO_READ,
				atoi(data.row[0][0]), get_time(atoi(data.row[0][2]), 0),
				data.row[0][1], data.row[0][3]);

		rsdb_exec_fetch_end(&data);

		rsdb_exec(NULL, "UPDATE memos SET flags = (flags|%u) WHERE id='%u'",
				MS_FLAGS_READ, id);

		return 1;
	}
	else
	{
		service_err(memoserv_p, client_p, SVC_MEMO_INVALID, parv[0]);
		return 1;
	}

	zlog(memoserv_p, 3, 0, 0, client_p, NULL, "READ");

	return 1;
}


static int
s_memo_send(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *msg;
	struct user_reg *ureg_p;
	struct rsdb_table data;
	unsigned int memo_id;
	dlink_node *ptr;

	/* check their username has been registered long enough */
	if(config_file.ms_memo_regtime_duration &&
	   (CURRENT_TIME - client_p->user->user_reg->reg_time) < config_file.ms_memo_regtime_duration)
	{
		service_err(memoserv_p, client_p, SVC_USER_DURATIONTOOSHORT,
				client_p->user->user_reg->name, memoserv_p->name, "SEND");
		return 1;
	}

	if((ureg_p = find_user_reg_nick(client_p, parv[0])) == NULL)
		return 1;

	/* this user cannot receive memos */
	if(ureg_p->flags & US_FLAGS_NOMEMOS)
	{
		service_err(memoserv_p, client_p, SVC_USER_QUERYOPTION,
				ureg_p->name, "NOMEMOS", "ON");
		return 1;
	}

	rsdb_exec_fetch(&data, "SELECT COUNT(*) FROM memos WHERE user_id='%u'",
			ureg_p->id);

	if(data.row_count == 0)
	{
		mlog("fatal error: SELECT COUNT() returned 0 rows in s_memo_send()");
		die(0, "problem with db file");
	}

	if(atoi(data.row[0][0]) >= config_file.ms_max_memos)
	{
		service_err(memoserv_p, client_p, SVC_MEMO_TOOMANYMEMOS,
				ureg_p->name);
		rsdb_exec_fetch_end(&data);
		return 1;
	}

	rsdb_exec_fetch_end(&data);

	msg = rebuild_params(parv, parc, 1);

	rsdb_exec_insert(&memo_id, "memos", "id",
			"INSERT INTO memos (user_id, source, source_id, timestamp, flags, text)"
			"VALUES('%u', '%Q', '%u', '%ld', '0', '%Q')",
			ureg_p->id, client_p->user->user_reg->name,
			client_p->user->user_reg->id, CURRENT_TIME, msg);

	service_err(memoserv_p, client_p, SVC_MEMO_SENT, ureg_p->name);

	DLINK_FOREACH(ptr, ureg_p->users.head)
	{
		service_err(memoserv_p, ptr->data, SVC_MEMO_RECEIVED,
				memo_id, client_p->name);
	}

	zlog(memoserv_p, 2, 0, 0, client_p, NULL, "SEND %s",
		client_p->user->user_reg->name);

	return 0;
}


static int
s_memo_delete(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	char *endptr;
	const char *id_str;
	unsigned int id;

	id_str = parv[0];

	if(*id_str == '#')
		id_str++;

	id = strtol(id_str, &endptr, 10);

	if(!strcasecmp(id_str, "ALL"))
	{
		rsdb_exec(NULL, "DELETE FROM memos WHERE user_id='%u'",
			client_p->user->user_reg->id);

		service_err(memoserv_p, client_p, SVC_MEMO_DELETEDALL);
	}
	else if(EmptyString(endptr) && id > 0)
	{
		struct rsdb_table data;
		unsigned int user_id;

		rsdb_exec_fetch(&data, "SELECT user_id FROM memos WHERE id='%u'", id);

		if(data.row_count == 0)
		{
			service_err(memoserv_p, client_p, SVC_MEMO_INVALID, parv[0]);
			rsdb_exec_fetch_end(&data);
			return 1;
		}

		user_id = atoi(data.row[0][0]);
		rsdb_exec_fetch_end(&data);

		if(user_id == client_p->user->user_reg->id)
		{
			rsdb_exec(NULL, "DELETE FROM memos WHERE id='%u'", id);
			service_err(memoserv_p, client_p, SVC_MEMO_DELETED, id);
		}
		else
			service_err(memoserv_p, client_p, SVC_MEMO_INVALID, parv[0]);
	}
	else
	{
		service_err(memoserv_p, client_p, SVC_MEMO_INVALID, parv[0]);
	}

	zlog(memoserv_p, 1, 0, 0, client_p, NULL, "DELETE");

	return 0;
}


#endif
