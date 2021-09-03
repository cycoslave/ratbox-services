/* src/s_operbot.c
 *   Contains the code for the operbot service.
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
 * $Id: s_operbot.c 23596 2007-02-05 21:35:27Z leeh $
 */
#include "stdinc.h"

#ifdef ENABLE_OPERBOT
#include "rsdb.h"
#include "rserv.h"
#include "langs.h"
#include "io.h"
#include "service.h"
#include "client.h"
#include "channel.h"
#include "c_init.h"
#include "log.h"
#include "conf.h"
#include "hook.h"
#include "ucommand.h"
#include "newconf.h"
#include "watch.h"

static void init_s_operbot(void);

static struct client *operbot_p;

static int o_operbot_objoin(struct client *, struct lconn *, const char **, int);
static int o_operbot_obpart(struct client *, struct lconn *, const char **, int);

static int s_operbot_invite(struct client *, struct lconn *, const char **, int);
static int s_operbot_op(struct client *, struct lconn *, const char **, int);

static int h_operbot_sjoin_lowerts(void *chptr, void *unused);

static struct service_command operbot_command[] =
{
	{ "OBJOIN",	&o_operbot_objoin,	1, NULL, 1, 0L, 0, 0, CONF_OPER_OB_CHANNEL },
	{ "OBPART",	&o_operbot_obpart,	1, NULL, 1, 0L, 0, 0, CONF_OPER_OB_CHANNEL },
	{ "INVITE",	&s_operbot_invite,	1, NULL, 1, 0L, 0, 1, 0 },
	{ "OP",		&s_operbot_op,		0, NULL, 1, 0L, 0, 1, 0 }
};

static struct ucommand_handler operbot_ucommand[] =
{
	{ "objoin",	o_operbot_objoin,	0, CONF_OPER_OB_CHANNEL, 1, NULL },
	{ "obpart",	o_operbot_obpart,	0, CONF_OPER_OB_CHANNEL, 1, NULL },
	{ "\0",		NULL,			0, 0, 0, NULL }
};

static struct service_handler operbot_service = {
	"OPERBOT", "operbot", "operbot", "services.int",
	"Oper invitation/op services", 0, 0, 
	operbot_command, sizeof(operbot_command), operbot_ucommand, init_s_operbot, NULL
};

static int operbot_db_callback(int, const char **);

void
preinit_s_operbot(void)
{
	operbot_p = add_service(&operbot_service);
}

static void
init_s_operbot(void)
{
	rsdb_exec(operbot_db_callback,
			"SELECT chname, tsinfo FROM operbot");

	hook_add(h_operbot_sjoin_lowerts, HOOK_SJOIN_LOWERTS);
}

static int
operbot_db_callback(int argc, const char **argv)
{
	join_service(operbot_p, argv[0], atol(argv[1]), NULL, 0);
	return 0;
}

static int
h_operbot_sjoin_lowerts(void *v_chptr, void *unused)
{
	struct channel *chptr = v_chptr;

	if (dlink_find(operbot_p, &chptr->services) == NULL)
		return 0;

	/* Save the new TS for later -- jilles */
	rsdb_exec(NULL, "UPDATE operbot SET tsinfo = '%lu' WHERE chname = LOWER('%Q')",
			chptr->tsinfo, chptr->name);
	return 0;
}

static int
o_operbot_objoin(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	time_t tsinfo;

	if(!valid_chname(parv[0]))
	{
		service_snd(operbot_p, client_p, conn_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 0;
	}

	if((chptr = find_channel(parv[0])) && 
	   dlink_find(operbot_p, &chptr->services))
	{
		service_snd(operbot_p, client_p, conn_p, SVC_IRC_ALREADYONCHANNEL,
				operbot_p->name, chptr->name);
		return 0;
	}

	zlog(operbot_p, 1, WATCH_OPERBOT, 1, client_p, conn_p,
		"OBJOIN %s", parv[0]);

	tsinfo = chptr != NULL ? chptr->tsinfo : CURRENT_TIME;

	rsdb_exec(NULL, "INSERT INTO operbot (chname, tsinfo, oper) VALUES(LOWER('%Q'), '%lu', '%Q')",
			parv[0], tsinfo, OPER_NAME(client_p, conn_p));

	join_service(operbot_p, parv[0], tsinfo, NULL, 0);

	service_snd(operbot_p, client_p, conn_p, SVC_SUCCESSFULON,
			operbot_p->name, "OBJOIN", parv[0]);
	return 0;
}

static int
o_operbot_obpart(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	if(part_service(operbot_p, parv[0]))
	{
		zlog(operbot_p, 1, WATCH_OPERBOT, 1, client_p, conn_p,
			"OBPART %s", parv[0]);

		rsdb_exec(NULL, "DELETE FROM operbot WHERE chname = LOWER('%Q')",
				parv[0]);
		service_snd(operbot_p, client_p, conn_p, SVC_SUCCESSFULON,
				operbot_p->name, "OBPART", parv[0]);
	}
	else
		service_snd(operbot_p, client_p, conn_p, SVC_IRC_NOTINCHANNEL,
				operbot_p->name, parv[0]);

	return 0;
}

static int
s_operbot_invite(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_err(operbot_p, client_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 1;
	}

	if(dlink_find(chptr, &operbot_p->service->channels) == NULL)
	{
		service_err(operbot_p, client_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 1;
	}

	if(find_chmember(chptr, client_p) != NULL)
		return 1;

	sendto_server(":%s INVITE %s %s", 
			SVC_UID(operbot_p), UID(client_p), chptr->name);
	return 1;
}

static int
s_operbot_op(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	struct chmember *mptr;

	/* op in all common channels */
	if(!parc)
	{
		dlink_node *ptr;

		DLINK_FOREACH(ptr, operbot_p->service->channels.head)
		{
			chptr = ptr->data;

			if((mptr = find_chmember(chptr, client_p)) == NULL)
				continue;

			if(is_opped(mptr))
				continue;

			mptr->flags |= MODE_OPPED;
			sendto_server(":%s MODE %s +o %s",
					SVC_UID(operbot_p), chptr->name, UID(client_p));
		}

		return 1;
	}

	if((chptr = find_channel(parv[0])) == NULL)
	{
		service_err(operbot_p, client_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 1;
	}

	if(dlink_find(chptr, &operbot_p->service->channels) == NULL)
	{
		service_err(operbot_p, client_p, SVC_IRC_CHANNELINVALID, parv[0]);
		return 1;
	}

	if((mptr = find_chmember(chptr, client_p)) == NULL)
		return 1;

	if(is_opped(mptr))
		return 1;

	mptr->flags |= MODE_OPPED;
	sendto_server(":%s MODE %s +o %s",
			SVC_UID(operbot_p), chptr->name, UID(client_p));
	return 1;
}

#endif
