/* src/s_global.c
 *   Contains the code for the netwide messaging service.
 *
 * Copyright (C) 2004-2007 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2004-2007 ircd-ratbox development team
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
 * $Id: s_global.c 23535 2007-01-27 16:04:04Z leeh $
 */
#include "stdinc.h"

#ifdef ENABLE_GLOBAL
#include "rsdb.h"
#include "rserv.h"
#include "langs.h"
#include "io.h"
#include "service.h"
#include "client.h"
#include "c_init.h"
#include "conf.h"
#include "ucommand.h"
#include "log.h"
#include "hook.h"
#include "watch.h"

/* maximum length of a welcome message */
#define WELCOME_MAGIC	400

/* maximum number of welcomes */
#define WELCOME_MAX	6

static struct client *global_p;

static void init_s_global(void);

static char *global_welcome_list[WELCOME_MAX];

static int o_global_netmsg(struct client *, struct lconn *, const char **, int);
static int o_global_addwelcome(struct client *, struct lconn *, const char **, int);
static int o_global_delwelcome(struct client *, struct lconn *, const char **, int);
static int o_global_listwelcome(struct client *, struct lconn *, const char **, int);

static int h_global_send_welcome(void *target_p, void *unused);

static struct service_command global_command[] =
{
	{ "NETMSG",	&o_global_netmsg,	1, NULL, 1, 0L, 0, 0, CONF_OPER_GLOB_NETMSG	},
	{ "ADDWELCOME",	&o_global_addwelcome,	2, NULL, 1, 0L, 0, 0, CONF_OPER_GLOB_WELCOME	},
	{ "DELWELCOME", &o_global_delwelcome,	1, NULL, 1, 0L, 0, 0, CONF_OPER_GLOB_WELCOME	},
	{ "LISTWELCOME",&o_global_listwelcome,	0, NULL, 1, 0L, 0, 0, CONF_OPER_GLOB_WELCOME	}
};

static struct ucommand_handler global_ucommand[] =
{
	{ "netmsg",	o_global_netmsg,	0, CONF_OPER_GLOB_NETMSG, 1, NULL },
	{ "addwelcome",	o_global_addwelcome,	0, CONF_OPER_GLOB_WELCOME, 2, NULL },
	{ "delwelcome",	o_global_delwelcome,	0, CONF_OPER_GLOB_WELCOME, 1, NULL },
	{ "listwelcome",o_global_listwelcome,	0, CONF_OPER_GLOB_WELCOME, 0, NULL },
	{ "\0", NULL, 0, 0, 0, NULL }
};

static struct service_handler global_service = {
	"GLOBAL", "GLOBAL", "global", "services.int",
	"Network Message Service", 0, 0, 
	global_command, sizeof(global_command), global_ucommand, init_s_global, NULL
};

void
preinit_s_global(void)
{
	global_p = add_service(&global_service);

	/* global service has to be opered otherwise it
	 * wont work. --anfl
	 */
	global_p->service->flags |= SERVICE_OPERED;
}

static void
init_s_global(void)
{
	struct rsdb_table *data;
	unsigned int pos;
	int i;

	/* we will only ever use this once, so malloc() it */
	data = my_malloc(sizeof(struct rsdb_table));

	rsdb_exec_fetch(data, "SELECT id, text FROM global_welcome");

	for(i = 0; i < data->row_count; i++)
	{
		pos = atoi(data->row[i][0]);

		if(pos >= WELCOME_MAX)
			continue;

		global_welcome_list[pos] = my_strdup(data->row[i][1]);
	}

	rsdb_exec_fetch_end(data);
	my_free(data);

	hook_add(h_global_send_welcome, HOOK_NEW_CLIENT);
}

static int
o_global_netmsg(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct client *target_p;
	dlink_node *ptr;
	const char *data;

	data = rebuild_params(parv, parc, 0);

	DLINK_FOREACH(ptr, server_list.head)
	{
		target_p = ptr->data;

		sendto_server(":%s NOTICE $$%s :[NETWORK MESSAGE] %s",
				SVC_UID(global_p), target_p->name, data);
	}

	zlog(global_p, 1, WATCH_GLOBAL, 1, client_p, conn_p,
		"NETMSG %s", data);

	return 0;
}

static int
h_global_send_welcome(void *target_p, void *unused)
{
	unsigned int i;

	for(i = 0; i < WELCOME_MAX; i++)
	{
		if(!EmptyString(global_welcome_list[i]))
			service_error(global_p, target_p, "%s", global_welcome_list[i]);
	}

	return 0;
}

static int
o_global_addwelcome(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *data;
	unsigned int id;

	id = atoi(parv[0]);
	data = rebuild_params(parv, parc, 1);

	if(strlen(data) > WELCOME_MAGIC)
	{
		service_snd(global_p, client_p, conn_p, SVC_GLOBAL_WELCOMETOOLONG,
				(unsigned int) strlen(data), WELCOME_MAGIC);
		return 0;
	}

	if(id >= WELCOME_MAX)
	{
		service_snd(global_p, client_p, conn_p, SVC_GLOBAL_WELCOMEINVALID,
				id, WELCOME_MAX);
		return 0;
	}

	if(global_welcome_list[id])
	{
		my_free(global_welcome_list[id]);

		rsdb_exec(NULL, "DELETE FROM global_welcome WHERE id='%u'", id);
	}

	global_welcome_list[id] = my_strdup(data);

	rsdb_exec(NULL, "INSERT INTO global_welcome (id, text) VALUES('%u', '%Q')",
			id, data);

	service_snd(global_p, client_p, conn_p, SVC_GLOBAL_WELCOMESET, id);

	zlog(global_p, 1, WATCH_GLOBAL, 1, client_p, conn_p,
		"ADDWELCOME %u %s", id, data);

	return 0;
}

static int
o_global_delwelcome(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	unsigned int id;

	id = atoi(parv[0]);

	if(global_welcome_list[id] == NULL)
	{
		service_snd(global_p, client_p, conn_p, SVC_GLOBAL_WELCOMENOTSET, id);
		return 0;
	}

	rsdb_exec(NULL, "DELETE FROM global_welcome WHERE id='%u'", id);

	my_free(global_welcome_list[id]);
	global_welcome_list[id] = NULL;

	service_snd(global_p, client_p, conn_p, SVC_GLOBAL_WELCOMEDELETED, id);

	zlog(global_p, 1, WATCH_GLOBAL, 1, client_p, conn_p,
		"DELWELCOME %u", id);

	return 0;
}

static int
o_global_listwelcome(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	unsigned int i;

	service_snd(global_p, client_p, conn_p, SVC_GLOBAL_WELCOMELIST);

	for(i = 0; i < WELCOME_MAX; i++)
	{
		service_send(global_p, client_p, conn_p,
				"    %u: %s",
				i, EmptyString(global_welcome_list[i]) ? "" : global_welcome_list[i]);
	}

	service_snd(global_p, client_p, conn_p, SVC_ENDOFLIST);

	zlog(global_p, 2, WATCH_GLOBAL, 1, client_p, conn_p, "LISTWELCOME");

	return 0;
}

#endif
