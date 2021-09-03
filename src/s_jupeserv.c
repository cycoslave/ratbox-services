/* src/s_jupeserv.c
 *   Contains the code for the jupe service.
 *
 * Copyright (C) 2004-2007 Lee Hardy <leeh@leeh.co.uk>
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
 * $Id: s_jupeserv.c 23825 2007-04-11 22:40:55Z leeh $
 */
#include "stdinc.h"

#ifdef ENABLE_JUPESERV
#include "rsdb.h"
#include "rserv.h"
#include "langs.h"
#include "service.h"
#include "client.h"
#include "channel.h"
#include "io.h"
#include "c_init.h"
#include "log.h"
#include "conf.h"
#include "ucommand.h"
#include "newconf.h"
#include "hook.h"
#include "event.h"
#include "watch.h"

struct server_jupe
{
	char *name;
	char *reason;
	int points;
	int add;
	time_t expire;
	dlink_node node;
	dlink_list servers;
};

static dlink_list pending_jupes;
static dlink_list active_jupes;

static void init_s_jupeserv(void);

static struct client *jupeserv_p;

static int o_jupeserv_jupe(struct client *, struct lconn *, const char **, int);
static int o_jupeserv_unjupe(struct client *, struct lconn *, const char **, int);

static int s_jupeserv_calljupe(struct client *, struct lconn *, const char **, int);
static int s_jupeserv_callunjupe(struct client *, struct lconn *, const char **, int);
static int s_jupeserv_pending(struct client *, struct lconn *, const char **, int);

static struct service_command jupeserv_command[] =
{
	{ "JUPE",	&o_jupeserv_jupe,	2, NULL, 1, 0L, 0, 0, CONF_OPER_JS_JUPE	},
	{ "UNJUPE",	&o_jupeserv_unjupe,	1, NULL, 1, 0L, 0, 0, CONF_OPER_JS_JUPE	},
	{ "CALLJUPE",	&s_jupeserv_calljupe,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "CALLUNJUPE",	&s_jupeserv_callunjupe,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "PENDING",	&s_jupeserv_pending,	0, NULL, 1, 0L, 0, 1, 0 }
};

static struct ucommand_handler jupeserv_ucommand[] =
{
	{ "jupe",	o_jupeserv_jupe,	0, CONF_OPER_JS_JUPE, 2, NULL },
	{ "unjupe",	o_jupeserv_unjupe,	0, CONF_OPER_JS_JUPE, 1, NULL },
	{ "\0",	NULL, 0, 0, 0, NULL }
};

static struct service_handler jupe_service = {
	"JUPESERV", "jupeserv", "jupeserv", "services.int",
	"Jupe Services", 0, 0,
	jupeserv_command, sizeof(jupeserv_command), jupeserv_ucommand, init_s_jupeserv, NULL
};

static int jupe_db_callback(int argc, const char **argv);
static int h_jupeserv_squit(void *name, void *unused);
static int h_jupeserv_finburst(void *unused, void *unused2);
static void e_jupeserv_expire(void *unused);

void
preinit_s_jupeserv(void)
{
	jupeserv_p = add_service(&jupe_service);
}

static void
init_s_jupeserv(void)
{
	/* merge this service up into operserv if needed */
	if(config_file.js_merge_into_operserv)
	{
		struct client *service_p;

		if((service_p = merge_service(&jupe_service, "OPERSERV", 1)) != NULL)
		{
			dlink_delete(&jupeserv_p->listnode, &service_list);
			jupeserv_p = service_p;
		}
	}

	hook_add(h_jupeserv_squit, HOOK_SQUIT_UNKNOWN);
	hook_add(h_jupeserv_finburst, HOOK_FINISHED_BURSTING);
	eventAdd("e_jupeserv_expire", e_jupeserv_expire, NULL, 60);

	rsdb_exec(jupe_db_callback, "SELECT servername, reason FROM jupes");
}

static struct server_jupe *
make_jupe(const char *name)
{
	struct server_jupe *jupe_p = my_malloc(sizeof(struct server_jupe));
	jupe_p->name = my_strdup(name);
	dlink_add(jupe_p, &jupe_p->node, &pending_jupes);
	return jupe_p;
}

static void
add_jupe(struct server_jupe *jupe_p)
{
	struct client *target_p;

	if((target_p = find_server(jupe_p->name)))
	{
		sendto_server("SQUIT %s :%s", jupe_p->name, jupe_p->reason);
		exit_client(target_p);
	}

	if(finished_bursting)
		sendto_server(":%s SERVER %s 1 :JUPED: %s",
				MYUID, jupe_p->name, jupe_p->reason);
	dlink_move_node(&jupe_p->node, &pending_jupes, &active_jupes);
}

static void
free_jupe(struct server_jupe *jupe_p)
{
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, jupe_p->servers.head)
	{
		my_free(ptr->data);
		dlink_destroy(ptr, &jupe_p->servers);
	}

	my_free(jupe_p->name);
	my_free(jupe_p->reason);
	my_free(jupe_p);
}

static struct server_jupe *
find_jupe(const char *name, dlink_list *list)
{
	struct server_jupe *jupe_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, list->head)
	{
		jupe_p = ptr->data;

		if(!irccmp(jupe_p->name, name))
			return jupe_p;
	}

	return NULL;
}

static void
e_jupeserv_expire(void *unused)
{
	struct server_jupe *jupe_p;
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, pending_jupes.head)
	{
		jupe_p = ptr->data;

		if(jupe_p->expire <= CURRENT_TIME)
		{
			dlink_delete(&jupe_p->node, &pending_jupes);
			free_jupe(jupe_p);
		}
	}
}	

static int
jupe_db_callback(int argc, const char **argv)
{
	struct server_jupe *jupe_p;

	jupe_p = make_jupe(argv[0]);
	jupe_p->reason = my_strdup(argv[1]);

	add_jupe(jupe_p);
	return 0;
}

static int
h_jupeserv_squit(void *name, void *unused)
{
	struct server_jupe *jupe_p;

	if((jupe_p = find_jupe(name, &active_jupes)))
	{
		sendto_server(":%s SERVER %s 2 :JUPED: %s",
				MYUID, jupe_p->name, jupe_p->reason);
		return -1;
	}

	return 0;
}

static int
h_jupeserv_finburst(void *unused, void *unused2)
{
	struct client *target_p;
	struct server_jupe *jupe_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, active_jupes.head)
	{
		jupe_p = ptr->data;

		if((target_p = find_server(jupe_p->name)))
		{
			sendto_server("SQUIT %s :%s", jupe_p->name, jupe_p->reason);
			exit_client(target_p);
		}

		sendto_server(":%s SERVER %s 2 :JUPED: %s",
				MYUID, jupe_p->name, jupe_p->reason);
	}

	return 0;
}

static int
valid_jupe(const char *servername)
{
	if(!valid_servername(servername) || strchr(servername, '*') ||
	   !irccmp(servername, MYNAME))
		return 0;

	if(strlen(servername) > HOSTLEN)
		return 0;

	/* cant jupe our uplink */
	if(server_p && !irccmp(servername, server_p->name))
		return 0;

	return 1;
}

static int
o_jupeserv_jupe(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct server_jupe *jupe_p;
	char *reason;

	if(!valid_jupe(parv[0]))
	{
		service_snd(jupeserv_p, client_p, conn_p, SVC_IRC_SERVERNAMEINVALID, parv[0]);
		return 0;
	}

	if((jupe_p = find_jupe(parv[0], &active_jupes)))
	{
		service_snd(jupeserv_p, client_p, conn_p, SVC_JUPE_ALREADYJUPED, jupe_p->name);
		return 0;
	}

	/* if theres a pending oper jupe, cancel it because we're gunna
	 * place a proper one.. --fl
	 */
	if((jupe_p = find_jupe(parv[0], &pending_jupes)))
	{
		dlink_delete(&jupe_p->node, &pending_jupes);
		free_jupe(jupe_p);
	}

	jupe_p = make_jupe(parv[0]);
	reason = rebuild_params(parv, parc, 1);

	if(strlen(reason) > REALLEN)
		reason[REALLEN] = '\0';

	zlog(jupeserv_p, 1, WATCH_JUPESERV, 1, client_p, conn_p,
		"JUPE %s %s", jupe_p->name, reason);

	if(EmptyString(reason))
		jupe_p->reason = my_strdup("No Reason");
	else
		jupe_p->reason = my_strdup(reason);

	rsdb_exec(NULL, "INSERT INTO jupes (servername, reason) VALUES('%Q', '%Q')",
			jupe_p->name, jupe_p->reason);

	if(client_p)
		sendto_server(":%s WALLOPS :JUPE set on %s by %s!%s@%s on %s [%s]",
				MYUID, jupe_p->name, client_p->name,
				client_p->user->username, client_p->user->host,
				client_p->user->servername, jupe_p->reason);
	else
		sendto_server(":%s WALLOPS :JUPE %s by %s [%s]",
				MYUID, jupe_p->name, conn_p->name, jupe_p->reason);

	add_jupe(jupe_p);
	return 0;
}

static int
o_jupeserv_unjupe(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct server_jupe *ajupe_p, *jupe_p;

	if((jupe_p = find_jupe(parv[0], &active_jupes)) == NULL)
	{
		service_snd(jupeserv_p, client_p, conn_p, SVC_JUPE_NOTJUPED, parv[0]);
		return 0;
	}

	if((ajupe_p = find_jupe(parv[0], &pending_jupes)))
	{
		dlink_delete(&ajupe_p->node, &pending_jupes);
		free_jupe(ajupe_p);
	}

	zlog(jupeserv_p, 1, WATCH_JUPESERV, 1, client_p, conn_p,
		"UNJUPE %s", jupe_p->name);

	rsdb_exec(NULL, "DELETE FROM jupes WHERE servername = '%Q'",
			jupe_p->name);

	if(client_p)
		sendto_server(":%s WALLOPS :UNJUPE set on %s by %s!%s@%s",
				MYUID, jupe_p->name, client_p->name,
				client_p->user->username, client_p->user->host);
	else
		sendto_server(":%s WALLOPS :UNJUPE %s by %s",
				MYUID, jupe_p->name, conn_p->name);

	sendto_server("SQUIT %s :Unjuped", jupe_p->name);
	dlink_delete(&jupe_p->node, &active_jupes);
	free_jupe(jupe_p);
	return 0;
}

static int
s_jupeserv_calljupe(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct server_jupe *jupe_p;
	dlink_node *ptr;

	if(!valid_jupe(parv[0]))
	{
		service_err(jupeserv_p, client_p, SVC_IRC_SERVERNAMEINVALID, parv[0]);
		return 0;
	}

	if(!config_file.jupe_score || !config_file.oper_score)
	{
		service_err(jupeserv_p, client_p, SVC_ISDISABLED,
				jupeserv_p->name, "CALLJUPE");
		return 0;
	}

	if((jupe_p = find_jupe(parv[0], &active_jupes)))
	{
		service_err(jupeserv_p, client_p, SVC_JUPE_ALREADYJUPED, jupe_p->name);
		return 0;
	}

	if((jupe_p = find_jupe(parv[0], &pending_jupes)) == NULL)
	{
		char *reason;

		jupe_p = make_jupe(parv[0]);
		jupe_p->add = 1;

		reason = rebuild_params(parv, parc, 1);

		if(strlen(reason) > REASONLEN)
			reason[REASONLEN] = '\0';

		if(EmptyString(reason))
			jupe_p->reason = my_strdup("No Reason");
		else
			jupe_p->reason = my_strdup(reason);
	}

	DLINK_FOREACH(ptr, jupe_p->servers.head)
	{
		if(!irccmp((const char *) ptr->data, client_p->user->servername))
		{
			service_err(jupeserv_p, client_p, SVC_JUPE_ALREADYREQUESTED,
					jupeserv_p->name, "CALLJUPE", jupe_p->name);
			return 0;
		}
	}

	zlog(jupeserv_p, 1, WATCH_JUPESERV, 1, client_p, conn_p,
		"CALLJUPE %s %s", parv[0], jupe_p->reason);

	jupe_p->expire = CURRENT_TIME + config_file.pending_time;
	jupe_p->points += config_file.oper_score;
	dlink_add_alloc(my_strdup(client_p->user->servername), &jupe_p->servers);

	if(jupe_p->points >= config_file.jupe_score)
	{
		zlog(jupeserv_p, 1, WATCH_JUPESERV, 1, client_p, conn_p,
			"TRIGGERJUPE %s", parv[0]);

		sendto_server(":%s WALLOPS :JUPE triggered on %s by %s!%s@%s on %s [%s]",
				MYUID, jupe_p->name, client_p->name,
				client_p->user->username, client_p->user->host, 
				client_p->user->servername, jupe_p->reason);

		rsdb_exec(NULL, "INSERT INTO jupes (servername, reason) VALUES('%Q', '%Q')",
				jupe_p->name, jupe_p->reason);

		add_jupe(jupe_p);
	}
	else
		sendto_server(":%s WALLOPS :JUPE requested on %s by %s!%s@%s on %s [%s]",
				MYUID, jupe_p->name, client_p->name,
				client_p->user->username, client_p->user->host, 
				client_p->user->servername, jupe_p->reason);

	return 0;
}

static int
s_jupeserv_callunjupe(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct server_jupe *ajupe_p, *jupe_p;
	dlink_node *ptr;

	if(!config_file.unjupe_score || !config_file.oper_score)
	{
		service_err(jupeserv_p, client_p, SVC_ISDISABLED,
				jupeserv_p->name, "CALLUNJUPE");
		return 0;
	}

	if((ajupe_p = find_jupe(parv[0], &active_jupes)) == NULL)
	{
		service_err(jupeserv_p, client_p, SVC_JUPE_NOTJUPED, parv[0]);
		return 0;
	}

	if((jupe_p = find_jupe(parv[0], &pending_jupes)) == NULL)
	{
		jupe_p = make_jupe(ajupe_p->name);
		jupe_p->points = config_file.unjupe_score;
	}

	DLINK_FOREACH(ptr, jupe_p->servers.head)
	{
		if(!irccmp((const char *) ptr->data, client_p->user->servername))
		{
			service_err(jupeserv_p, client_p, SVC_JUPE_ALREADYREQUESTED,
					jupeserv_p->name, "CALLUNJUPE", jupe_p->name);
			return 0;
		}
	}

	zlog(jupeserv_p, 1, WATCH_JUPESERV, 1, client_p, conn_p,
		"CALLUNJUPE %s", parv[0]);

	jupe_p->expire = CURRENT_TIME + config_file.pending_time;
	jupe_p->points -= config_file.oper_score;
	dlink_add_alloc(my_strdup(client_p->user->servername), &jupe_p->servers);

	if(jupe_p->points <= 0)
	{
		zlog(jupeserv_p, 1, WATCH_JUPESERV, 1, client_p, conn_p,
			"TRIGGERUNJUPE %s", parv[0]);

		sendto_server(":%s WALLOPS :UNJUPE triggered on %s by %s!%s@%s on %s",
				MYUID, jupe_p->name, client_p->name,
				client_p->user->username, client_p->user->host, 
				client_p->user->servername);

		rsdb_exec(NULL, "DELETE FROM jupes WHERE servername = '%Q'",
				jupe_p->name);

		sendto_server("SQUIT %s :Unjuped", jupe_p->name);
		dlink_delete(&jupe_p->node, &pending_jupes);
		dlink_delete(&ajupe_p->node, &active_jupes);
		free_jupe(jupe_p);
		free_jupe(ajupe_p);
	}
	else
		sendto_server(":%s WALLOPS :UNJUPE requested on %s by %s!%s@%s on %s",
				MYUID, jupe_p->name, client_p->name,
				client_p->user->username, client_p->user->host, 
				client_p->user->servername);

	return 0;
}

static int
s_jupeserv_pending(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct server_jupe *jupe_p;
	dlink_node *ptr;

	if(!config_file.oper_score)
	{
		service_err(jupeserv_p, client_p, SVC_ISDISABLED,
				jupeserv_p->name, "PENDING");
		return 0;
	}

	if(dlink_list_length(&pending_jupes))
		zlog(jupeserv_p, 3, WATCH_JUPESERV, 1, client_p, conn_p, "PENDING");

	service_err(jupeserv_p, client_p, SVC_JUPE_PENDINGLIST);

	DLINK_FOREACH(ptr, pending_jupes.head)
	{
		jupe_p = ptr->data;

		service_error(jupeserv_p, client_p, "  %s %s %d/%d points (%s)",
				jupe_p->add ? "JUPE" : "UNJUPE",
				jupe_p->name, jupe_p->points,
				jupe_p->add ? config_file.jupe_score : config_file.unjupe_score,
				jupe_p->reason);
	}

	service_err(jupeserv_p, client_p, SVC_ENDOFLIST);

	return 0;
}

#endif
