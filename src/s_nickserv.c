/* src/s_nickserv.c
 *   Contains the code for the nickname services.
 *
 * Copyright (C) 2005-2007 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2005-2007 ircd-ratbox development team
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
 * $Id: s_nickserv.c 23596 2007-02-05 21:35:27Z leeh $
 */
#include "stdinc.h"

#ifdef ENABLE_NICKSERV
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
#include "s_userserv.h"
#include "s_nickserv.h"
#include "balloc.h"
#include "hook.h"
#include "watch.h"

static void init_s_nickserv(void);

static struct client *nickserv_p;
static BlockHeap *nick_reg_heap;

static dlink_list nick_reg_table[MAX_NAME_HASH];

static int o_nick_nickdrop(struct client *, struct lconn *, const char **, int);

static int s_nick_register(struct client *, struct lconn *, const char **, int);
static int s_nick_drop(struct client *, struct lconn *, const char **, int);
static int s_nick_release(struct client *, struct lconn *, const char **, int);
static int s_nick_regain(struct client *, struct lconn *, const char **, int);
static int s_nick_set(struct client *, struct lconn *, const char **, int);
static int s_nick_info(struct client *, struct lconn *, const char **, int);

static int h_nick_warn_client(void *target_p, void *unused);
static int h_nick_server_eob(void *client_p, void *unused);

static struct service_command nickserv_command[] =
{
	{ "NICKDROP",	&o_nick_nickdrop, 1, NULL, 1, 0L, 0, 0, CONF_OPER_NS_DROP },
	{ "REGISTER",	&s_nick_register, 0, NULL, 1, 0L, 0, 0, 0	},
	{ "DROP",	&s_nick_drop,     1, NULL, 1, 0L, 1, 0, 0	},
	{ "RELEASE",	&s_nick_release,  1, NULL, 1, 0L, 1, 0, 0	},
	{ "REGAIN",	&s_nick_regain,   1, NULL, 1, 0L, 1, 0, 0	},
	{ "SET",	&s_nick_set,	  2, NULL, 1, 0L, 1, 0, 0	},
	{ "INFO",	&s_nick_info,     1, NULL, 1, 0L, 1, 0, 0	}
};

static struct ucommand_handler nickserv_ucommand[] =
{
	{ "nickdrop", o_nick_nickdrop, 0, CONF_OPER_NS_DROP, 1, NULL },
	{ "\0", NULL, 0, 0, 0, NULL }
};

static struct service_handler nick_service = {
	"NICKSERV", "NICKSERV", "nickserv", "services.int",
	"Nickname Registration Service", 0, 0, 
	nickserv_command, sizeof(nickserv_command), nickserv_ucommand, init_s_nickserv, NULL
};

static int nick_db_callback(int, const char **);

void
preinit_s_nickserv(void)
{
	nickserv_p = add_service(&nick_service);
}

static void
init_s_nickserv(void)
{
	nick_reg_heap = BlockHeapCreate("Nick Reg", sizeof(struct nick_reg), HEAP_NICK_REG);

	rsdb_exec(nick_db_callback, 
			"SELECT nickname, username, reg_time, last_time, flags FROM nicks");

	hook_add(h_nick_warn_client, HOOK_NEW_CLIENT);
	hook_add(h_nick_warn_client, HOOK_NICKCHANGE);
	hook_add(h_nick_server_eob, HOOK_SERVER_EOB);
}

static void
add_nick_reg(struct nick_reg *nreg_p)
{
	unsigned int hashv = hash_name(nreg_p->name);
	dlink_add(nreg_p, &nreg_p->node, &nick_reg_table[hashv]);
}

void
free_nick_reg(struct nick_reg *nreg_p)
{
	unsigned int hashv = hash_name(nreg_p->name);

	rsdb_exec(NULL, "DELETE FROM nicks WHERE nickname = '%Q'",
			nreg_p->name);

	dlink_delete(&nreg_p->node, &nick_reg_table[hashv]);
	dlink_delete(&nreg_p->usernode, &nreg_p->user_reg->nicks);
	BlockHeapFree(nick_reg_heap, nreg_p);
}

static struct nick_reg *
find_nick_reg(struct client *client_p, const char *name)
{
	struct nick_reg *nreg_p;
	dlink_node *ptr;
	unsigned int hashv = hash_name(name);

	DLINK_FOREACH(ptr, nick_reg_table[hashv].head)
	{
		nreg_p = ptr->data;
		if(!irccmp(nreg_p->name, name))
			return nreg_p;
	}

	if(client_p)
		service_err(nickserv_p, client_p, SVC_NICK_NOTREG, name);

	return NULL;
}

static int
nick_db_callback(int argc, const char **argv)
{
	struct nick_reg *nreg_p;
	struct user_reg *ureg_p;

	if(EmptyString(argv[0]) || EmptyString(argv[1]))
		return 0;

	if((ureg_p = find_user_reg(NULL, argv[1])) == NULL)
		return 0;

	nreg_p = BlockHeapAlloc(nick_reg_heap);
	strlcpy(nreg_p->name, argv[0], sizeof(nreg_p->name));
	nreg_p->reg_time = atol(argv[2]);
	nreg_p->last_time = atol(argv[3]);
	nreg_p->flags = atol(argv[4]);

	add_nick_reg(nreg_p);
	dlink_add(nreg_p, &nreg_p->usernode, &ureg_p->nicks);
	nreg_p->user_reg = ureg_p;

	return 0;
}

static int
h_nick_warn_client(void *vclient_p, void *unused)
{
	struct nick_reg *nreg_p;
	struct client *client_p = vclient_p;

	if(!config_file.nallow_set_warn || EmptyString(config_file.nwarn_string))
		return 0;

	if((nreg_p = find_nick_reg(NULL, client_p->name)) == NULL)
		return 0;

	if((nreg_p->flags & NS_FLAGS_WARN) == 0)
		return 0;

	/* here for nick change */
	if(nreg_p->user_reg == client_p->user->user_reg)
		return 0;

	service_error(nickserv_p, client_p, "%s", config_file.nwarn_string);
	return 0;
}

static int
h_nick_server_eob(void *vclient_p, void *unused)
{
	struct nick_reg *nreg_p;
	struct client *client_p = vclient_p;
	struct client *target_p;
	dlink_node *ptr;

	if(!config_file.nallow_set_warn || EmptyString(config_file.nwarn_string))
		return 0;

	DLINK_FOREACH(ptr, client_p->server->users.head)
	{
		target_p = ptr->data;

		if((nreg_p = find_nick_reg(NULL, target_p->name)) == NULL)
			continue;

		if((nreg_p->flags & NS_FLAGS_WARN) == 0)
			continue;

		if(nreg_p->user_reg == target_p->user->user_reg)
			continue;

		service_error(nickserv_p, target_p, "%s", config_file.nwarn_string);
	}

	return 0;
}

static int
o_nick_nickdrop(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct nick_reg *nreg_p;

	if((nreg_p = find_nick_reg(NULL, parv[0])) == NULL)
	{
		service_snd(nickserv_p, client_p, conn_p, SVC_NICK_NOTREG, parv[0]);
		return 0;
	}

	service_snd(nickserv_p, client_p, conn_p, SVC_SUCCESSFULON, 
			nickserv_p->name, "NICKDROP", parv[0]);

	zlog(nickserv_p, 1, WATCH_NSADMIN, 1, client_p, conn_p,
		"NICKDROP %s", nreg_p->name);

	free_nick_reg(nreg_p);
	return 0;
}

static int
s_nick_register(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct nick_reg *nreg_p;
	struct user_reg *ureg_p = client_p->user->user_reg;
	struct client *userserv_p;

	/* Hack: give better error message -- jilles */
	if (ureg_p == NULL)
	{
		userserv_p = find_service_id("USERSERV");
		service_err(nickserv_p, client_p, SVC_NICK_LOGINFIRST,
				userserv_p != NULL ? userserv_p->name : "???");
		return 1;
	}
	else
		ureg_p->last_time = CURRENT_TIME;

	if(dlink_list_length(&ureg_p->nicks) >= config_file.nmax_nicks)
	{
		service_err(nickserv_p, client_p, SVC_NICK_TOOMANYREG,
				config_file.nmax_nicks);
		return 1;
	}

	if(IsDigit(client_p->name[0]))
	{
		service_err(nickserv_p, client_p, SVC_NICK_CANTREGUID);
		return 1;
	}

	if(find_nick_reg(NULL, client_p->name))
	{
		service_err(nickserv_p, client_p, SVC_NICK_ALREADYREG,
				client_p->name);
		return 1;
	}

	zlog(nickserv_p, 2, WATCH_NSREGISTER, 0, client_p, NULL,
		"REGISTER %s", client_p->name);

	nreg_p = BlockHeapAlloc(nick_reg_heap);

	strlcpy(nreg_p->name, client_p->name, sizeof(nreg_p->name));
	nreg_p->reg_time = nreg_p->last_time = CURRENT_TIME;

	if(config_file.nallow_set_warn)
		nreg_p->flags |= NS_FLAGS_WARN;

	add_nick_reg(nreg_p);
	dlink_add(nreg_p, &nreg_p->usernode, &ureg_p->nicks);
	nreg_p->user_reg = ureg_p;

	rsdb_exec(NULL, 
			"INSERT INTO nicks (nickname, username, reg_time, last_time, flags) "
			"VALUES('%Q', '%Q', '%lu', '%lu', '%u')",
			nreg_p->name, ureg_p->name, nreg_p->reg_time, 
			nreg_p->last_time, nreg_p->flags);

	service_err(nickserv_p, client_p, SVC_NICK_NOWREG, client_p->name);
	return 1;
}

static int
s_nick_drop(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct nick_reg *nreg_p;

	if((nreg_p = find_nick_reg(client_p, parv[0])) == NULL)
		return 1;

	if(nreg_p->user_reg != client_p->user->user_reg)
	{
		service_err(nickserv_p, client_p, SVC_NICK_REGGEDOTHER,
				nreg_p->name);
		return 1;
	}

	service_err(nickserv_p, client_p, SVC_SUCCESSFULON,
			nickserv_p->name, "DROP", parv[0]);

	zlog(nickserv_p, 3, 0, 0, client_p, NULL, "DROP %s", parv[0]);

	free_nick_reg(nreg_p);
	return 1;
}

static int
s_nick_release(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct nick_reg *nreg_p;
	struct client *target_p;

	if((nreg_p = find_nick_reg(client_p, parv[0])) == NULL)
		return 1;

	if(nreg_p->user_reg != client_p->user->user_reg)
	{
		service_err(nickserv_p, client_p, SVC_NICK_REGGEDOTHER,
				nreg_p->name);
		return 1;
	}

	if((target_p = find_user(parv[0], 0)) == NULL)
	{
		service_err(nickserv_p, client_p, SVC_NICK_NOTONLINE, nreg_p->name);
		return 1;
	}

	if(target_p == client_p)
	{
		service_err(nickserv_p, client_p, SVC_NICK_USING,
				nreg_p->name);
		return 1;
	}

	sendto_server("KILL %s :%s (%s: RELEASE by %s)",
			UID(target_p), MYNAME, 
			nickserv_p->name, client_p->name);
	exit_client(target_p);

	zlog(nickserv_p, 4, 0, 0, client_p, NULL, "RELEASE %s", parv[0]);

	return 1;
}

static int
s_nick_regain(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct nick_reg *nreg_p;
	struct client *target_p;

	if((nreg_p = find_nick_reg(client_p, parv[0])) == NULL)
		return 1;

	if(nreg_p->user_reg != client_p->user->user_reg)
	{
		service_err(nickserv_p, client_p, SVC_NICK_REGGEDOTHER,
				nreg_p->name);
		return 1;
	}

	if((target_p = find_user(parv[0], 0)) == NULL)
	{
		service_err(nickserv_p, client_p, SVC_NICK_NOTONLINE,
				nreg_p->name);
		return 1;
	}

	if(target_p == client_p)
	{
		service_err(nickserv_p, client_p, SVC_NICK_USING,
				nreg_p->name);
		return 1;
	}

	if((client_p->uplink->flags & FLAGS_RSFNC) == 0)
	{
		service_err(nickserv_p, client_p, SVC_NOTSUPPORTED,
				nickserv_p->name, "REGAIN");
		return 1;
	}

	sendto_server("KILL %s :%s (%s: REGAIN by %s)",
			UID(target_p), MYNAME, 
			nickserv_p->name, client_p->name);

	/* send out a forced nick change for the client to their new
	 * nickname, at a TS of 60 seconds ago to prevent collisions.
	 */
	sendto_server("ENCAP %s RSFNC %s %s %lu %lu",
			client_p->user->servername, UID(client_p),
			nreg_p->name, (unsigned long)(CURRENT_TIME - 60),
			(unsigned long)client_p->user->tsinfo);

	exit_client(target_p);

	zlog(nickserv_p, 4, 0, 0, client_p, NULL, "REGAIN %s", parv[0]);

	return 1;
}


static int
s_nick_set_flag(struct client *client_p, struct nick_reg *nreg_p,
		const char *name, const char *arg, int flag)
{
	if(!strcasecmp(arg, "ON"))
	{
		service_err(nickserv_p, client_p, SVC_NICK_CHANGEDOPTION,
				nreg_p->name, name, "ON");

		if(nreg_p->flags & flag)
			return 0;

		nreg_p->flags |= flag;

		rsdb_exec(NULL, "UPDATE nicks SET flags='%d' WHERE nickname='%Q'",
				nreg_p->flags, nreg_p->name);

		return 1;
	}
	else if(!strcasecmp(arg, "OFF"))
	{
		service_err(nickserv_p, client_p, SVC_NICK_CHANGEDOPTION,
				nreg_p->name, name, "OFF");

		if((nreg_p->flags & flag) == 0)
			return 0;

		nreg_p->flags &= ~flag;

		rsdb_exec(NULL, "UPDATE nicks SET flags='%d' WHERE nickname='%Q'",
				nreg_p->flags, nreg_p->name);

		return -1;
	}

	service_err(nickserv_p, client_p, SVC_NICK_QUERYOPTION,
			nreg_p->name, name, (nreg_p->flags & flag) ? "ON" : "OFF");
	return 0;
}

static int
s_nick_set(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static const char dummy[] = "\0";
	struct nick_reg *nreg_p;
	const char *arg;

	if((nreg_p = find_nick_reg(client_p, parv[0])) == NULL)
		return 1;

	if(nreg_p->user_reg != client_p->user->user_reg)
	{
		service_err(nickserv_p, client_p, SVC_NICK_REGGEDOTHER,
				nreg_p->name);
		return 1;
	}

	arg = EmptyString(parv[2]) ? dummy : parv[2];

	if(!strcasecmp(parv[1], "WARN"))
	{
		if(!config_file.nallow_set_warn)
		{
			service_err(nickserv_p, client_p, SVC_ISDISABLED,	
					nickserv_p->name, "SET::WARN");
			return 1;
		}

		s_nick_set_flag(client_p, nreg_p, parv[1], arg, NS_FLAGS_WARN);
		return 1;
	}

	service_err(nickserv_p, client_p, SVC_OPTIONINVALID,
			nickserv_p->name, "SET");
	return 1;
}	

static int
s_nick_info(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct nick_reg *nreg_p;

	if((nreg_p = find_nick_reg(client_p, parv[0])) == NULL)
		return 1;

	service_err(nickserv_p, client_p, SVC_INFO_REGDURATIONNICK,
			nreg_p->name, nreg_p->user_reg->name,
			get_duration((time_t) (CURRENT_TIME - nreg_p->reg_time)));

	zlog(nickserv_p, 5, 0, 0, client_p, NULL, "INFO %s", parv[0]);

	return 1;
}

#endif
