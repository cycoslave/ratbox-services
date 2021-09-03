/* src/watch.c
 *   Contains code for watching issued commands.
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
 * $Id: s_watchserv.c 23827 2007-04-11 22:48:39Z leeh $
 */
#include "stdinc.h"

#ifdef ENABLE_WATCHSERV
#include "rserv.h"
#include "langs.h"
#include "conf.h"
#include "io.h"
#include "client.h"
#include "service.h"
#include "ucommand.h"
#include "c_init.h"
#include "watch.h"
#include "hook.h"
#include "s_userserv.h"

#define WatchCapable(client_p, conn_p, flag) \
	((conn_p) ? ((conn_p)->watchflags & flag) : ((client_p)->user->watchflags & flag))
#define WatchSet(client_p, conn_p, flag) \
	((conn_p) ? ((conn_p)->watchflags |= flag) : ((client_p)->user->watchflags |= flag))
#define WatchClear(client_p, conn_p, flag) \
	((conn_p) ? ((conn_p)->watchflags &= ~flag) : ((client_p)->user->watchflags &= ~flag))

static struct
{
	const char *name;
	unsigned int flag;
} watch_flags[] = {
	{ "auth",		WATCH_AUTH		},
#ifdef ENABLE_BANSERV
	{ "banserv",		WATCH_BANSERV		},
#endif
#ifdef ENABLE_CHANSERV
	{ "csadmin",		WATCH_CSADMIN		},
	{ "csoper",		WATCH_CSOPER		},
	{ "csregister",		WATCH_CSREGISTER	},
#endif
#ifdef ENABLE_GLOBAL
	{ "global",		WATCH_GLOBAL		},
#endif
#ifdef ENABLE_JUPESERV
	{ "jupeserv",		WATCH_JUPESERV		},
#endif
#ifdef ENABLE_NICKSERV
	{ "nsadmin",		WATCH_NSADMIN		},
	{ "nsregister",		WATCH_NSREGISTER	},
#endif
#ifdef ENABLE_OPERBOT
	{ "operbot",		WATCH_OPERBOT		},
#endif
#ifdef ENABLE_OPERSERV
	{ "operserv",		WATCH_OPERSERV		},
#endif
#ifdef ENABLE_USERSERV
	{ "usadmin",		WATCH_USADMIN		},
	{ "usoper",		WATCH_USOPER		},
	{ "usregister",		WATCH_USREGISTER	},
#endif
	{ "all",		WATCH_ALL		},
	{ NULL, 0 }
};

static void init_s_watchserv(void);

static struct client *watchserv_p;

static int o_watch_watch(struct client *, struct lconn *, const char **, int);

static struct service_command watch_command[] =
{
	{ "WATCH",	&o_watch_watch,		0, NULL, 1, 0L, 0, 0, 0 }
};

static struct ucommand_handler watch_ucommand [] =
{
	{ "watch",	o_watch_watch,		0, 0, 0, NULL	},
	{ "\0", NULL, 0, 0, 0, NULL }
};

static struct service_handler watchserv_service = {
	"WATCHSERV", "WATCHSERV", "watchserv", "services.int", "Command Watching Service",
	0, 0, watch_command, sizeof(watch_command), watch_ucommand, init_s_watchserv, NULL
};

void
preinit_s_watchserv(void)
{
	watchserv_p = add_service(&watchserv_service);
}

static void
init_s_watchserv(void)
{
	if(config_file.ws_merge_into_operserv)
	{
		struct client *service_p;

		if((service_p = merge_service(&watchserv_service, "OPERSERV", 1)) != NULL)
		{
			dlink_delete(&watchserv_p->listnode, &service_list);
			watchserv_p = service_p;
		}
	}
}

static unsigned int
watch_find_flag(const char *name)
{
	int i;

	for(i = 0; watch_flags[i].name; i++)
	{
		if(!strcasecmp(name, watch_flags[i].name))
			return watch_flags[i].flag;
	}

	return 0;
}

static const char *
watch_find_name(unsigned int flag)
{
	static const char empty_string[] = "unknown";
	int i;

	for(i = 0; watch_flags[i].name; i++)
	{
		if(watch_flags[i].flag == flag)
			return watch_flags[i].name;
	}

	return empty_string;
}

static void
watch_show(struct client *client_p, struct lconn *conn_p)
{
	static char buf_on[BUFSIZE];
	static char buf_off[BUFSIZE];
	int i;

	buf_on[0] = buf_off[0] = '\0';

	for(i = 0; watch_flags[i].name; i++)
	{
		if(WatchCapable(client_p, conn_p, watch_flags[i].flag))
		{
			strlcat(buf_on, watch_flags[i].name, sizeof(buf_on));
			strlcat(buf_on, " ", sizeof(buf_on));
		}
		else
		{
			strlcat(buf_off, watch_flags[i].name, sizeof(buf_off));
			strlcat(buf_off, " ", sizeof(buf_off));
		}
		
	}

	service_send(watchserv_p, client_p, conn_p, "Watching:");

	if(buf_on[0] != '\0')
		service_send(watchserv_p, client_p, conn_p, "  %s", buf_on);

	if(buf_off[0] != '\0')
	{
		service_send(watchserv_p, client_p, conn_p, "Available flags:");
		service_send(watchserv_p, client_p, conn_p, "  %s", buf_off);
	}
}

int
o_watch_watch(struct client *client_p, struct lconn *conn_p, const char **parv, int parc)
{
	const char *data;
	char *buf;
	char *p, *next;
	unsigned int flag;
	int dir;

	if(parc < 1 || EmptyString(parv[0]))
	{
		watch_show(client_p, conn_p);
		return 0;
	}

	data = rebuild_params(parv, parc, 0);
	buf = LOCAL_COPY(data);
	p = buf;

	while(p)
	{
		if(*p == '-')
		{
			dir = 0;
			p++;
		}
		else
			dir = 1;

		if((next = strchr(p, ' ')))
			*next++ = '\0';

		flag = watch_find_flag(p);

		if(flag)
		{
			if(dir)
				WatchSet(client_p, conn_p, flag);
			else
				WatchClear(client_p, conn_p, flag);
		}

		p = next;
	}

	watch_show(client_p, conn_p);

	return 0;
}

void
watch_send(unsigned int flag, struct client *source_client_p, struct lconn *source_conn_p,
		int oper, const char *format, ...)
{
	static char buf[BUFSIZE];
	struct client *client_p;
	struct lconn *conn_p;
	const char *flagname;
	const char *name;
	va_list args;
	dlink_node *ptr;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	flagname = watch_find_name(flag);

	if(oper)
		name = OPER_NAME(source_client_p, source_conn_p);
	else
		name = source_client_p->user->user_reg ? source_client_p->user->user_reg->name : "-";

	DLINK_FOREACH(ptr, oper_list.head)
	{
		client_p = ptr->data;

		/* We have to cast NULL here, as my (crap?) macro for
		 * WatchCapable() would expand to ((void *)0)->watchflags
		 * otherwise.  For obvious reasons that wont compile. --anfl
		 */
		if(WatchCapable(client_p, (struct lconn *) NULL, flag))
			service_error(watchserv_p, client_p, 
					"[watch:%s] [%s%s:%s] %s", 
					flagname, oper ? "*" : "", name,
					OPER_MASK(source_client_p, source_conn_p), buf);
	}

	DLINK_FOREACH(ptr, connection_list.head)
	{
		conn_p = ptr->data;

		/* Same cast reason as above */
		if(WatchCapable((struct client *) NULL, conn_p, flag))
			sendto_one(conn_p, "[watch:%s] [%s%s:%s] %s",
					flagname, oper ? "*" : "", name,
					OPER_MASK(source_client_p, source_conn_p), 
					buf);
	}
}

#endif
