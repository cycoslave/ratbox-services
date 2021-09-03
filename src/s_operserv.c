/* src/s_operserv.c
 *   Contains the code for oper services.
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
 * $Id: s_operserv.c 26666 2009-09-17 18:49:37Z leeh $
 */
#include "stdinc.h"

#ifdef ENABLE_OPERSERV
#include "rsdb.h"
#include "rserv.h"
#include "langs.h"
#include "io.h"
#include "client.h"
#include "service.h"
#include "channel.h"
#include "c_init.h"
#include "conf.h"
#include "hook.h"
#include "ucommand.h"
#include "modebuild.h"
#include "log.h"
#include "watch.h"

static void init_s_operserv(void);

static struct client *operserv_p;

static int o_oper_takeover(struct client *, struct lconn *, const char **, int);
static int o_oper_osjoin(struct client *, struct lconn *, const char **, int);
static int o_oper_ospart(struct client *, struct lconn *, const char **, int);
static int o_oper_omode(struct client *, struct lconn *, const char **, int);
static int o_oper_dbsync(struct client *, struct lconn *, const char **, int);
static int o_oper_rehash(struct client *, struct lconn *, const char **, int);
static int o_oper_die(struct client *, struct lconn *, const char **, int);
static int o_oper_addignore(struct client *, struct lconn *, const char **, int);
static int o_oper_delignore(struct client *, struct lconn *, const char **, int);
static int o_oper_listignores(struct client *, struct lconn *, const char **, int);
static int o_oper_listopers(struct client *, struct lconn *, const char **, int);

static int h_operserv_sjoin_lowerts(void *chptr, void *unused);

static struct service_command operserv_command[] =
{
	{ "OSJOIN",	&o_oper_osjoin,		1, NULL, 1, 0L, 0, 0, CONF_OPER_OS_CHANNEL	},
	{ "OSPART",	&o_oper_ospart,		1, NULL, 1, 0L, 0, 0, 0				},
	{ "TAKEOVER",	&o_oper_takeover,	1, NULL, 1, 0L, 0, 0, CONF_OPER_OS_TAKEOVER	},
	{ "OMODE",	&o_oper_omode,		2, NULL, 1, 0L, 0, 0, CONF_OPER_OS_OMODE	},
	{ "DBSYNC",	&o_oper_dbsync,		0, NULL, 1, 0L, 0, 0, CONF_OPER_OS_MAINTAIN	},
	{ "REHASH",	&o_oper_rehash,		0, NULL, 1, 0L, 0, 0, CONF_OPER_OS_MAINTAIN	},
	{ "DIE",	&o_oper_die,		1, NULL, 1, 0L, 0, 0, CONF_OPER_OS_MAINTAIN	},
	{ "ADDIGNORE",	&o_oper_addignore,	2, NULL, 1, 0L, 0, 0, CONF_OPER_OS_IGNORE	},
	{ "DELIGNORE",	&o_oper_delignore,	1, NULL, 1, 0L, 0, 0, CONF_OPER_OS_IGNORE	},
	{ "LISTIGNORES",&o_oper_listignores,	0, NULL, 1, 0L, 0, 0, CONF_OPER_OS_IGNORE	},
	{ "LISTOPERS",	&o_oper_listopers,	0, NULL, 1, 0L, 0, 0, 0	}
};

static struct ucommand_handler operserv_ucommand[] =
{
	{ "osjoin",	o_oper_osjoin,		0, CONF_OPER_OS_CHANNEL,	1, NULL },
	{ "ospart",	o_oper_ospart,		0, CONF_OPER_OS_CHANNEL,	1, NULL },
	{ "takeover",	o_oper_takeover,	0, CONF_OPER_OS_TAKEOVER,	1, NULL },
	{ "omode",	o_oper_omode,		0, CONF_OPER_OS_OMODE,		2, NULL },
	{ "dbsync",	o_oper_dbsync,		CONF_OPER_ADMIN, 0,		0, NULL },
	{ "die",	o_oper_die,		CONF_OPER_ADMIN, 0,		1, NULL },
	{ "addignore",	o_oper_addignore,	0, CONF_OPER_OS_IGNORE, 	2, NULL },
	{ "delignore",	o_oper_delignore,	0, CONF_OPER_OS_IGNORE,		1, NULL },
	{ "listignores",o_oper_listignores,	0, CONF_OPER_OS_IGNORE,		1, NULL },
	{ "listopers",	o_oper_listopers,	0, 0, 0, NULL },
	{ "\0", NULL, 0, 0, 0, NULL }
};

static struct service_handler operserv_service = {
	"OPERSERV", "OPERSERV", "operserv", "services.int", "Oper Services",
	0, 0, operserv_command, sizeof(operserv_command), operserv_ucommand, init_s_operserv, NULL
};

static int operserv_db_callback(int, const char **);

void
preinit_s_operserv(void)
{
	operserv_p = add_service(&operserv_service);
}

static void
init_s_operserv(void)
{
	rsdb_exec(operserv_db_callback, 
			"SELECT chname, tsinfo FROM operserv");

	hook_add(h_operserv_sjoin_lowerts, HOOK_SJOIN_LOWERTS);
}

static int
operserv_db_callback(int argc, const char **argv)
{
	join_service(operserv_p, argv[0], atol(argv[1]), NULL, 0);
	return 0;
}

static int
h_operserv_sjoin_lowerts(void *v_chptr, void *unused)
{
	struct channel *chptr = v_chptr;

	if (dlink_find(operserv_p, &chptr->services) == NULL)
		return 0;

	/* Save the new TS for later -- jilles */
	rsdb_exec(NULL, "UPDATE operserv SET tsinfo = '%lu' WHERE chname = LOWER('%Q')",
			chptr->tsinfo, chptr->name);
	return 0;
}

/* preconditions: TS >= 2 and there is at least one user in the channel */
static void
otakeover(struct channel *chptr, int invite)
{
	dlink_node *ptr;

	part_service(operserv_p, chptr->name);

	remove_our_modes(chptr);

	if (EmptyString(server_p->sid))
	{
		modebuild_start(operserv_p, chptr);

		DLINK_FOREACH(ptr, chptr->bans.head)
		{
			modebuild_add(DIR_DEL, "b", ptr->data);
		}
		DLINK_FOREACH(ptr, chptr->excepts.head)
		{
			modebuild_add(DIR_DEL, "e", ptr->data);
		}
		DLINK_FOREACH(ptr, chptr->invites.head)
		{
			modebuild_add(DIR_DEL, "I", ptr->data);
		}
	}

	remove_bans(chptr);

	if(invite)
		chptr->mode.mode = MODE_TOPIC|MODE_NOEXTERNAL|MODE_INVITEONLY;
	else
		chptr->mode.mode = MODE_TOPIC|MODE_NOEXTERNAL;

	chptr->tsinfo--;

	join_service(operserv_p, chptr->name, chptr->tsinfo, NULL, 0);

	/* apply the -beI if needed, after the join */
	if(EmptyString(server_p->sid))
		modebuild_finish();

	/* need to reop some services */
	if(dlink_list_length(&chptr->services) > 1)
	{
		struct client *target_p;

		modebuild_start(operserv_p, chptr);

		DLINK_FOREACH(ptr, chptr->services.head)
		{
			target_p = ptr->data;

			if(target_p != operserv_p)
				modebuild_add(DIR_ADD, "o", target_p->name);
		}

		modebuild_finish();
	}
}

static void
otakeover_clear(struct channel *chptr, int remove_opers)
{
	struct chmember *msptr;
	dlink_node *ptr, *next_ptr;

	kickbuild_start();

	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->users.head)
	{
		msptr = ptr->data;

		if(msptr->client_p->user->oper ||
		   (!remove_opers && is_oper(msptr->client_p)))
			continue;

		kickbuild_add(UID(msptr->client_p), "Takeover Requested");
		del_chmember(msptr);
	}

	kickbuild_finish(operserv_p, chptr);
}

static int
o_oper_takeover(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_snd(operserv_p, client_p, conn_p, SVC_IRC_NOSUCHCHANNEL, parv[0]);
		return 0;
	}

	if(chptr->tsinfo < 2)
	{
		/* isnt really worth translating */
		service_send(operserv_p, client_p, conn_p,
				"Channel %s TS too low for takeover", parv[0]);
		return 0;
	}

	if(dlink_list_length(&chptr->users) == 0)
	{
		/* Taking over a channel without users would lead to segfaults
		 * and is pointless anyway -- jilles */
		service_snd(operserv_p, client_p, conn_p, SVC_IRC_CHANNELNOUSERS, parv[0]);
		return 0;
	}

	if(parc > 1 && !EmptyString(parv[1]))
	{
		if(!irccmp(parv[1], "-clearall"))
		{
			otakeover(chptr, 1);
			otakeover_clear(chptr, 1);
		}
		else if(!irccmp(parv[1], "-clear"))
		{
			otakeover(chptr, 1);
			otakeover_clear(chptr, 0);
		}
		/* DEPRECATED */
		else if(!irccmp(parv[1], "-full"))
		{
			otakeover(chptr, 0);
		}
	}
	else
		otakeover(chptr, 0);

	zlog(operserv_p, 1, WATCH_OPERSERV, 1, client_p, conn_p,
		"TAKEOVER %s", parv[0]);

	service_snd(operserv_p, client_p, conn_p, SVC_SUCCESSFULON,
			operserv_p->name, "TAKEOVER", parv[0]);
	return 0;
}

static int
o_oper_osjoin(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	time_t tsinfo;

	if(!valid_chname(parv[0]))
	{
		service_snd(operserv_p, client_p, conn_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}

	if((chptr = find_channel(parv[0])) &&
	   dlink_find(operserv_p, &chptr->services))
	{
		service_snd(operserv_p, client_p, conn_p, SVC_IRC_ALREADYONCHANNEL,
				operserv_p->name, parv[0]);
		return 0;
	}

	zlog(operserv_p, 1, WATCH_OPERSERV, 1, client_p, conn_p,
		"OSJOIN %s", parv[0]);

	tsinfo = chptr != NULL ? chptr->tsinfo : CURRENT_TIME;

	rsdb_exec(NULL, "INSERT INTO operserv (chname, tsinfo, oper) VALUES(LOWER('%Q'), '%lu', '%Q')",
			parv[0], tsinfo, OPER_NAME(client_p, conn_p));

	join_service(operserv_p, parv[0], tsinfo, NULL, 0);

	service_snd(operserv_p, client_p, conn_p, SVC_SUCCESSFULON,
			operserv_p->name, "OSJOIN", parv[0]);
	return 0;
}

static int
o_oper_ospart(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct rsdb_table data;
	struct channel *chptr;
	int osjoin = 0;

	if(client_p && !client_p->user->oper)
	{
		service_err(operserv_p, client_p, SVC_NOACCESS,
				operserv_p->name, "OSPART");
		return 1;
	}

	if((chptr = find_channel(parv[0])) == NULL || dlink_find(operserv_p, &chptr->services) == NULL)
	{
		service_snd(operserv_p, client_p, conn_p, SVC_IRC_NOTINCHANNEL,
				operserv_p->name, parv[0]);
		return 0;
	}

	/* test privs ourself here, OSPART for channels done through OSJOIN
	 * is the 'maintain' priv, but through channels joined through
	 * TAKEOVER, its the 'takeover' priv.
	 */
	rsdb_exec_fetch(&data, "SELECT COUNT(chname) FROM operserv WHERE chname=LOWER('%Q')",
			chptr->name);

	if(data.row_count == 0)
	{
		mlog("fatal error: SELECT COUNT() returned 0 rows in o_oper_ospart()");
		die(0, "problem with db file");
	}

	osjoin = atoi(data.row[0][0]);

	rsdb_exec_fetch_end(&data);

	/* done through OSJOIN */
	if(osjoin)
	{
		if((client_p && (client_p->user->oper->sflags & CONF_OPER_OS_CHANNEL) == 0) ||
		   (conn_p && (conn_p->sprivs & CONF_OPER_OS_CHANNEL) == 0))
		{
			service_snd(operserv_p, client_p, conn_p, SVC_OPER_OSPARTACCESS,
					operserv_p->name, "OSJOIN");
			return 0;
		}
	}
	/* through TAKEOVER */
	else if((client_p && (client_p->user->oper->sflags & CONF_OPER_OS_TAKEOVER) == 0) ||
		(conn_p && (conn_p->sprivs & CONF_OPER_OS_TAKEOVER) == 0))
	{
		service_snd(operserv_p, client_p, conn_p, SVC_OPER_OSPARTACCESS,
				operserv_p->name, "TAKEOVER");
		return 0;
	}

	part_service(operserv_p, parv[0]);
	chptr = NULL;	/* part_service() may have destroyed the channel */

	zlog(operserv_p, 1, WATCH_OPERSERV, 1, client_p, conn_p,
		"OSPART %s", parv[0]);

	if(osjoin)
		rsdb_exec(NULL, "DELETE FROM operserv WHERE chname=LOWER('%Q')", parv[0]);

	service_snd(operserv_p, client_p, conn_p, SVC_SUCCESSFULON,
			operserv_p->name, "OSPART", parv[0]);

	return 0;
}

static int
o_oper_omode(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;

	if((chptr = find_channel(parv[0])) == NULL)
	{
		sendto_one(conn_p, "Channel %s does not exist",	parv[0]);
		return 0;
	}

	parse_full_mode(chptr, operserv_p, parv, parc, 1, config_file.allow_sslonly);

	zlog(operserv_p, 1, WATCH_OPERSERV, 1, client_p, conn_p,
		"OMODE %s %s", chptr->name, rebuild_params(parv, parc, 1));

	service_snd(operserv_p, client_p, conn_p, SVC_ISSUED,
			operserv_p->name, "OMODE");
	return 0;
}

static int
o_oper_dbsync(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	hook_call(HOOK_DBSYNC, NULL, NULL);

	zlog(operserv_p, 2, WATCH_OPERSERV, 1, client_p, conn_p, "DBSYNC");

	service_snd(operserv_p, client_p, conn_p, SVC_SUCCESSFUL,
			operserv_p->name, "DBSYNC");
	return 0;
}

static int
o_oper_rehash(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	if(parc > 0 && !irccmp(parv[0], "help"))
	{
		mlog("services rehashing: %s reloading help/translations", OPER_NAME(client_p, conn_p));
		sendto_all("services rehashing: %s reloading help/translations",
				OPER_NAME(client_p, conn_p));

		rehash_help();
		lang_clear_trans();
		lang_load_trans();
		return 0;
	}

	mlog("services rehashing: %s reloading config file", OPER_NAME(client_p, conn_p));
	sendto_all("services rehashing: %s reloading config file", OPER_NAME(client_p, conn_p));

	rehash(0);
	return 0;
}

static int
o_oper_die(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	if(parc < 1 || strcasecmp(MYNAME, parv[0]))
	{
		service_snd(operserv_p, client_p, conn_p, SVC_OPER_SERVERNAMEMISMATCH);
		return 0;
	}

	if(client_p && !config_file.os_allow_die)
	{
		service_snd(operserv_p, client_p, conn_p, SVC_ISDISABLED,
				operserv_p->name, "DIE");
		return 0;
	}

	/* this gives us the operwall if one is needed */
	zlog(operserv_p, 1, WATCH_OPERSERV, 1, client_p, conn_p, "DIE");

	sendto_all("Services terminated by %s", OPER_NAME(client_p, conn_p));
	mlog("ratbox-services terminated by %s", OPER_NAME(client_p, conn_p));

	die(1, "Services terminated");
	return 0;
}

static int
o_oper_addignore(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct service_ignore *ignore_p;
	dlink_node *ptr;

	if(!valid_ban(parv[0]))
	{
		service_snd(operserv_p, client_p, conn_p, SVC_INVALIDMASK, parv[0]);
		return 0;
	}

	DLINK_FOREACH(ptr, ignore_list.head)
	{
		if(match(ptr->data, parv[0]))
		{
			service_snd(operserv_p, client_p, conn_p, SVC_OPER_IGNOREALREADY,
					parv[0], ptr->data);
			return 0;
		}
	}

	ignore_p = my_malloc(sizeof(struct service_ignore));
	ignore_p->mask = my_strdup(parv[0]);
	collapse(ignore_p->mask);
	ignore_p->reason = my_strdup(rebuild_params(parv, parc, 1));
	ignore_p->oper = my_strdup(OPER_NAME(client_p, conn_p));

	dlink_add(ignore_p, &ignore_p->ptr, &ignore_list);

	rsdb_exec(NULL, "INSERT INTO ignore_hosts (hostname, oper, reason) VALUES('%Q', '%Q', '%Q')",
			ignore_p->mask, ignore_p->oper, ignore_p->reason);

	service_snd(operserv_p, client_p, conn_p, SVC_SUCCESSFUL,
			operserv_p->name, "ADDIGNORE");
	return 0;
}

static int
o_oper_delignore(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct service_ignore *ignore_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, ignore_list.head)
	{
		ignore_p = ptr->data;

		if(!irccmp(ignore_p->mask, parv[0]))
		{
			dlink_delete(&ignore_p->ptr, &ignore_list);

			my_free(ignore_p->mask);
			my_free(ignore_p->oper);
			my_free(ignore_p->reason);
			my_free(ignore_p);

			rsdb_exec(NULL, "DELETE FROM ignore_hosts WHERE hostname='%Q'", parv[0]);

			service_snd(operserv_p, client_p, conn_p, SVC_SUCCESSFUL,
					operserv_p->name, "DELIGNORE");
			return 0;
		}
	}

	service_snd(operserv_p, client_p, conn_p, SVC_OPER_IGNORENOTFOUND, parv[0]);
	return 0;
}

static int
o_oper_listignores(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct service_ignore *ignore_p;
	dlink_node *ptr;

	service_snd(operserv_p, client_p, conn_p, SVC_OPER_IGNORELIST);

	DLINK_FOREACH(ptr, ignore_list.head)
	{
		ignore_p = ptr->data;

		service_send(operserv_p, client_p, conn_p, "  %-40s oper:%s [%s]",
				ignore_p->mask, ignore_p->oper, ignore_p->reason);
	}

	service_snd(operserv_p, client_p, conn_p, SVC_ENDOFLIST);
	return 0;
}

static int
o_oper_listopers(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct client *target_p;
	struct lconn *dcc_p;
	dlink_node *ptr;

	if(client_p && !is_oper(client_p) && !client_p->user->oper)
	{
		if(ServiceStealth(operserv_p))
			return 1;

		service_snd(operserv_p, client_p, conn_p, SVC_NOACCESS,
				operserv_p->name, "LISTOPERS");
		return 1;
	}

	if(dlink_list_length(&connection_list))
	{
		service_snd(operserv_p, client_p, conn_p, SVC_OPER_CONNECTIONSSTART, "DCC");

		DLINK_FOREACH(ptr, connection_list.head)
		{
			dcc_p = ptr->data;

			service_send(operserv_p, client_p, conn_p, "  %s - %s %s",
					dcc_p->name, conf_oper_flags(dcc_p->privs),
					conf_service_flags(dcc_p->sprivs));
		}
	}

	if(dlink_list_length(&oper_list))
	{
		service_snd(operserv_p, client_p, conn_p, SVC_OPER_CONNECTIONSSTART, "IRC");

		DLINK_FOREACH(ptr, oper_list.head)
		{
			target_p = ptr->data;

			service_send(operserv_p, client_p, conn_p, "  %s %s %s %s",
					target_p->user->oper->name, target_p->user->mask,
					conf_oper_flags(target_p->user->oper->flags),
					conf_service_flags(target_p->user->oper->sflags));
		}
	}

	service_snd(operserv_p, client_p, conn_p, SVC_OPER_CONNECTIONSEND);

	zlog(operserv_p, 2, WATCH_OPERSERV, 1, client_p, conn_p, "LISTOPERS");

	return 0;
}

#endif
