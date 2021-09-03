/* src/ucommand.c
 *   Contains code for handling of commands received from local users.
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
 * $Id: ucommand.c 23906 2007-05-02 19:13:03Z leeh $
 */
#include "stdinc.h"
#include "ucommand.h"
#include "rserv.h"
#include "langs.h"
#include "tools.h"
#include "io.h"
#include "log.h"
#include "conf.h"
#include "event.h"
#include "service.h"
#include "client.h"
#include "channel.h"
#include "serno.h"
#include "cache.h"
#include "hook.h"
#include "watch.h"
#include "c_init.h"

#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

#define MAX_HELP_ROW 8

static dlink_list ucommand_table[MAX_UCOMMAND_HASH];
dlink_list ucommand_list;

static int u_login(struct client *, struct lconn *, const char **, int);

static int u_boot(struct client *, struct lconn *, const char **, int);
static int u_chat(struct client *, struct lconn *, const char **, int);
static int u_connect(struct client *, struct lconn *, const char **, int);
static int u_events(struct client *, struct lconn *, const char **, int);
static int u_help(struct client *, struct lconn *, const char **, int);
static int u_quit(struct client *, struct lconn *, const char **, int);
static int u_rehash(struct client *, struct lconn *, const char **, int);
static int u_service(struct client *, struct lconn *, const char **, int);
static int u_status(struct client *, struct lconn *, const char **, int);

static struct ucommand_handler ucommands[] =
{
	{ "boot",	u_boot,		CONF_OPER_ADMIN,	0, 1, NULL },
	{ "chat",	u_chat,		0,			0, 0, NULL },
	{ "connect",	u_connect,	CONF_OPER_ROUTE,	0, 1, NULL },
	{ "events",	u_events,	CONF_OPER_ADMIN,	0, 0, NULL },
	{ "help",	u_help,		0,			0, 0, NULL },
	{ "quit",	u_quit,		0,			0, 0, NULL },
	{ "rehash",	u_rehash,	CONF_OPER_ADMIN,	0, 0, NULL },
	{ "service",	u_service,	0,			0, 0, NULL },
	{ "status",	u_status,	0,			0, 0, NULL },
	{ "\0",         NULL,		0,			0, 0, NULL }
};

void
init_ucommand(void)
{
        add_ucommands(NULL, ucommands);
	add_ucommand_handler(NULL, &stats_ucommand);
	load_ucommand_help();
}

static int
hash_command(const char *p)
{
	unsigned int hash_val = 0;

	while(*p)
	{
		hash_val += ((int) (*p) & 0xDF);
		p++;
	}

	return(hash_val % MAX_UCOMMAND_HASH);
}

struct ucommand_handler *
find_ucommand(const char *command)
{
        struct ucommand_handler *handler;
        dlink_node *ptr;
	unsigned int hashv = hash_command(command);

	DLINK_FOREACH(ptr, ucommand_table[hashv].head)
	{
		handler = ptr->data;

		if(!strcasecmp(command, handler->cmd))
			return handler;
	}

        return NULL;
}

void
handle_ucommand(struct lconn *conn_p, const char *command, 
		const char *parv[], int parc)
{
	struct ucommand_handler *handler;

        /* people who arent logged in, can only do .login */
        if(!UserAuth(conn_p))
        {
                if(strcasecmp(command, "login"))
                {
                        sendto_one(conn_p, "You must .login first");
                        return;
                }

                u_login(NULL, conn_p, parv, parc);
                return;
        }

        if((handler = find_ucommand(command)) != NULL)
	{
		if(parc < handler->minpara)
		{
			sendto_one(conn_p, "Insufficient parameters");
			return;
		}

		if((handler->flags && !(conn_p->privs & handler->flags)) ||
		   (handler->sflags && !(conn_p->sprivs & handler->sflags)))
		{
			sendto_one(conn_p, "Insufficient access");
			return;
		}

		handler->func(NULL, conn_p, parv, parc);
	}
        else
                sendto_one(conn_p, "Invalid command: %s", command);
}

void
add_ucommand_handler(struct client *service_p, 
			struct ucommand_handler *chandler)
{
	unsigned int hashv;

	if(chandler == NULL || EmptyString(chandler->cmd))
		return;

	hashv = hash_command(chandler->cmd);
	dlink_add_alloc(chandler, &ucommand_table[hashv]);

	/* command not associated with any service */
        if(service_p == NULL)
		dlink_add_tail_alloc(chandler, &ucommand_list);
}

void
add_ucommands(struct client *service_p, struct ucommand_handler *handler)
{
        int i;

        for(i = 0; handler[i].cmd && handler[i].cmd[0] != '\0'; i++)
        {
                add_ucommand_handler(service_p, &handler[i]);
        }
}

void
load_ucommand_help(void)
{
	struct ucommand_handler *ucommand;
	char filename[PATH_MAX];
	dlink_node *ptr;
	int i;

	DLINK_FOREACH(ptr, ucommand_list.head)
	{
		ucommand = ptr->data;
		ucommand->helpfile = my_malloc(sizeof(struct cachefile *) * LANG_MAX);

		for(i = 0; langs_available[i]; i++)
		{
			snprintf(filename, sizeof(filename), "%s/%s/main/u-%s",
				HELP_PATH, langs_available[i], lcase(ucommand->cmd));
			ucommand->helpfile[i] = cache_file(filename, ucommand->cmd, 0);
		}
	}
}

void
clear_ucommand_help(void)
{
	struct ucommand_handler *ucommand;
	dlink_node *ptr;
	int i;

	DLINK_FOREACH(ptr, ucommand_list.head)
	{
		ucommand = ptr->data;

		for(i = 0; langs_available[i]; i++)
		{
			free_cachefile(ucommand->helpfile[i]);
		}
		
		my_free(ucommand->helpfile);
		ucommand->helpfile = NULL;
	}
}

static int
u_login(struct client *unused, struct lconn *conn_p, const char *parv[], int parc)
{
	struct conf_oper *oper_p = conn_p->oper;
	const char *crpass;

        if(parc < 2 || EmptyString(parv[1]))
        {
                sendto_one(conn_p, "Usage: .login <username> <password>");
                return 0;
        }

        if(ConfOperEncrypted(oper_p))
                crpass = crypt(parv[1], oper_p->pass);
        else
                crpass = parv[1];

        if(strcmp(oper_p->pass, crpass))
        {
                sendto_one(conn_p, "Invalid password");
                return 0;
        }

	watch_send(WATCH_AUTH, NULL, conn_p, 1, "has logged in (dcc)");

        /* set them as 'logged in' */
        SetUserAuth(conn_p);
	SetUserChat(conn_p);
	conn_p->privs = oper_p->flags;
	conn_p->sprivs = oper_p->sflags;
	conn_p->watchflags = 0;

        sendto_one(conn_p, "Login successful, for available commands see .help");

	deallocate_conf_oper(oper_p);
	conn_p->oper = NULL;

	hook_call(HOOK_DCC_AUTH, conn_p, NULL);

	return 0;
}

static int
u_boot(struct client *unused, struct lconn *conn_p, const char *parv[], int parc)
{
	struct client *target_p;
	struct lconn *dcc_p;
	dlink_node *ptr, *next_ptr;
	unsigned int count = 0;

	DLINK_FOREACH_SAFE(ptr, next_ptr, oper_list.head)
	{
		target_p = ptr->data;

		if(!irccmp(target_p->user->oper->name, parv[0]))
		{
			count++;

			deallocate_conf_oper(target_p->user->oper);
			target_p->user->oper = NULL;
			dlink_destroy(ptr, &oper_list);

			sendto_server(":%s NOTICE %s :Logged out by %s",
					MYUID, UID(target_p), conn_p->name);
		}
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, connection_list.head)
	{
		dcc_p = ptr->data;

		if(!irccmp(dcc_p->name, parv[0]))
		{
			count++;

			sendto_one(dcc_p, "Logged out by %s", conn_p->name);
			(dcc_p->io_close)(dcc_p);
		}
	}

	sendto_one(conn_p, "%u users booted", count);
	return 0;
}	

static int
u_connect(struct client *unused, struct lconn *conn_p, const char *parv[], int parc)
{
        struct conf_server *conf_p;
        int port = 0;

        if((conf_p = find_conf_server(parv[0])) == NULL)
        {
                sendto_one(conn_p, "No such server %s", parv[0]);
                return 0;
        }

        if(parc > 1)
        {
                if((port = atoi(parv[1])) <= 0)
                {
                        sendto_one(conn_p, "Invalid port %s", parv[1]);
                        return 0;
                }

                conf_p->port = port;
        }
        else
                conf_p->port = abs(conf_p->defport);

        if(server_p != NULL && (server_p->flags & CONN_DEAD) == 0)
        {
                (server_p->io_close)(server_p);

                sendto_all("Connection to server %s disconnected by %s: (reroute to %s)",
				server_p->name, conn_p->name, conf_p->name);
                mlog("Connection to server %s disconnected by %s: "
                     "(reroute to %s)",
                     server_p->name, conn_p->name, conf_p->name);
        }

        /* remove any pending events for connecting.. */
        eventDelete(connect_to_server, NULL);

        eventAddOnce("connect_to_server", connect_to_server, conf_p, 2);
	return 0;
}

static int
u_events(struct client *unused, struct lconn *conn_p, const char *parv[], int parc)
{
        event_show(conn_p);
	return 0;
}

static int
u_quit(struct client *unused, struct lconn *conn_p, const char *parv[], int parc)
{
	sendto_one(conn_p, "Goodbye.");
	(conn_p->io_close)(conn_p);
	return 0;
}

static int
u_rehash(struct client *unused, struct lconn *conn_p, const char *parv[], int parc)
{
	if(parc > 0 && !irccmp(parv[0], "help"))
	{
		mlog("services rehashing: %s reloading help/translations", conn_p->name);
		sendto_all("services rehashing: %s reloading help/translations",
				conn_p->name);
		rehash_help();
		lang_clear_trans();
		lang_load_trans();
		return 0;
	}

	mlog("services rehashing: %s reloading config file", conn_p->name);
	sendto_all("services rehashing: %s reloading config file", conn_p->name);

	rehash(0);
	return 0;
}

static int
u_service(struct client *unused, struct lconn *conn_p, const char *parv[], int parc)
{
        struct client *service_p;
        dlink_node *ptr;

        if(parc > 0)
        {
                if((service_p = find_service_id(parv[0])) == NULL)
                {
                        sendto_one(conn_p, "No such service %s", parv[0]);
                        return 0;
                }

                service_stats(service_p, conn_p);

                if(service_p->service->stats != NULL)
                        (service_p->service->stats)(conn_p, parv, parc);
                return 0;
        }

        sendto_one(conn_p, "Services:");

        DLINK_FOREACH(ptr, service_list.head)
        {
                service_p = ptr->data;

		if(ServiceDisabled(service_p))
			sendto_one(conn_p, "  %s: Disabled",
					service_p->service->id);
		else
	                sendto_one(conn_p, "  %s: Online as %s!%s@%s [%s]",
        	                   service_p->service->id, service_p->name,
                	           service_p->service->username,
                        	   service_p->service->host, service_p->info);
        }

	return 0;
}

static int
u_status(struct client *unused, struct lconn *conn_p, const char *parv[], int parc)
{
        sendto_one(conn_p, "%s, version ratbox-services-%s(%s), up %s",
			MYNAME, RSERV_VERSION, SERIALNUM,
			get_duration(CURRENT_TIME - first_time));

        if(server_p != NULL)
                sendto_one(conn_p, "Currently connected to %s", server_p->name);
        else
                sendto_one(conn_p, "Currently disconnected");

	sendto_one(conn_p, "Services: %lu",
			dlink_list_length(&service_list));
	sendto_one(conn_p, "Clients: DCC: %lu IRC: %lu",
			dlink_list_length(&connection_list),
			dlink_list_length(&oper_list));
        sendto_one(conn_p, "Network: Users: %lu Servers: %lu",
			dlink_list_length(&user_list),
			dlink_list_length(&server_list));
        sendto_one(conn_p, "         Channels: %lu Topics: %lu",
			dlink_list_length(&channel_list), count_topics());
	return 0;
}

static int      
u_chat(struct client *unused, struct lconn *conn_p, const char *parv[], int parc)
{
	if(parc < 1 || EmptyString(parv[0]))
	{
		sendto_one(conn_p, "Chat status is: %s",
				UserChat(conn_p) ? "on" : "off");
		return 0;
	}

	if(!strcasecmp(parv[0], "on"))
	{
		SetUserChat(conn_p);
		sendto_one(conn_p, "Chat status set on");
	}
	else if(!strcasecmp(parv[0], "off"))
	{
		ClearUserChat(conn_p);
		sendto_one(conn_p, "Chat status set off");
	}
	else
		sendto_one(conn_p, "Chat status must either be 'on' or 'off'");

	return 0;
}

static void
dump_commands_list(struct lconn *conn_p, struct client *service_p, dlink_list *list)
{
	struct ucommand_handler *handler;
	const char *hparv[MAX_HELP_ROW];
	dlink_node *ptr;
	int j = 0;
	int header = 0;

	DLINK_FOREACH(ptr, list->head)
	{
		handler = ptr->data;

		if(handler->flags && !(conn_p->privs & handler->flags))
			continue;

		if(!header)
		{
			header++;
			sendto_one(conn_p, "%s commands:",
					service_p ? ucase(service_p->name) : "Available");
		}

		hparv[j] = handler->cmd;
		j++;

		if(j >= MAX_HELP_ROW)
		{
			sendto_one(conn_p,
				"   %-8s %-8s %-8s %-8s %-8s %-8s %-8s %-8s",
				hparv[0], hparv[1], hparv[2], hparv[3],
				hparv[4], hparv[5], hparv[6], hparv[7]);
			j = 0;
		}
	}

	if(j)
	{
		char buf[BUFSIZE];
		char *p = buf;
		int i;

		for(i = 0; i < j; i++)
		{
			p += sprintf(p, "%-8s ", hparv[i]);
		}

		sendto_one(conn_p, "   %s", buf);
	}
}

static void
dump_commands_handler(struct lconn *conn_p, struct client *service_p, struct ucommand_handler *handler)
{
	const char *hparv[MAX_HELP_ROW];
	int i;
	int j = 0;
	int header = 0;

        for(i = 0; handler[i].cmd && handler[i].cmd[0] != '\0'; i++)
	{
		if(handler[i].flags && !(conn_p->privs & handler[i].flags))
			continue;

		if(!header)
		{
			header++;
			sendto_one(conn_p, "%s commands:",
					service_p ? ucase(service_p->name) : "Available");
		}

		hparv[j] = handler[i].cmd;
		j++;

		if(j >= MAX_HELP_ROW)
		{
			sendto_one(conn_p,
				"   %-8s %-8s %-8s %-8s %-8s %-8s %-8s %-8s",
				hparv[0], hparv[1], hparv[2], hparv[3],
				hparv[4], hparv[5], hparv[6], hparv[7]);
			j = 0;
		}
	}

	if(j)
	{
		char buf[BUFSIZE];
		char *p = buf;

		for(i = 0; i < j; i++)
		{
			p += sprintf(p, "%-8s ", hparv[i]);
		}

		sendto_one(conn_p, "   %s", buf);
	}
}

static int
u_help(struct client *unused, struct lconn *conn_p, const char *parv[], int parc)
{
        struct ucommand_handler *handler;
	dlink_node *ptr;

        if(parc < 1 || EmptyString(parv[0]))
        {
		struct client *service_p;

		dump_commands_list(conn_p, NULL, &ucommand_list);

		DLINK_FOREACH(ptr, service_list.head)
		{
			service_p = ptr->data;

			if(service_p->service->ucommand)
				dump_commands_handler(conn_p, service_p, service_p->service->ucommand);
		}

                sendto_one(conn_p, "For more information see .help <command>");
                return 0;
        }

        if((handler = find_ucommand(parv[0])) != NULL)
        {
                if(handler->helpfile == NULL || lang_get_cachefile_u(handler->helpfile, conn_p) == NULL)
                        sendto_one(conn_p, "No help available on %s", parv[0]);
                else
                        send_cachefile(lang_get_cachefile_u(handler->helpfile, conn_p), conn_p);

                return 0;
        }

        sendto_one(conn_p, "Unknown help topic: %s", parv[0]);
	return 0;
}

