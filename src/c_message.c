/* src/c_message.c
 *   Contains code for directing received privmsgs at services.
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
 * $Id: c_message.c 26694 2009-10-17 20:22:02Z leeh $
 */
#include "stdinc.h"
#include "rserv.h"
#include "io.h"
#include "service.h"
#include "client.h"
#include "conf.h"
#include "scommand.h"
#include "c_init.h"
#include "log.h"

static void c_message(struct client *, const char *parv[], int parc);

struct scommand_handler privmsg_command = { "PRIVMSG", c_message, 0, DLINK_EMPTY };

static void
c_message(struct client *client_p, const char *parv[], int parc)
{
	struct client *target_p = NULL;
	struct client *tmp_p;
	char *target;
	char *text;
	char *p;

	if(parc < 2 || EmptyString(parv[1]))
		return;

	target = LOCAL_COPY(parv[0]);

	/* username@server messaged? */
	if((p = strchr(target, '@')) != NULL)
	{
		dlink_node *ptr;

		*p = '\0';

		/* walk list manually hunting for this username.. */
		DLINK_FOREACH(ptr, service_list.head)
		{
			tmp_p = ptr->data;

			if(ServiceDisabled(tmp_p))
				continue;

			if(!irccmp(target, tmp_p->service->username))
			{
				target_p = tmp_p;
				break;
			}
		}
	}
	/* hunt for the nick.. */
	else
		target_p = find_service(target);

	if(target_p == NULL)
		return;

	/* ctcp.. doesnt matter who its addressed to. */
	if(parv[1][0] == '\001')
	{
		struct conf_oper *oper_p;

		/* some ctcp we dont care about */
		if(strncasecmp(parv[1], "\001CHAT\001", 6) &&
		   strncasecmp(parv[1], "\001DCC CHAT ", 10))
			return;

		oper_p = find_conf_oper(client_p->user->username,
					client_p->user->host,
					client_p->user->servername, 
					NULL);

		if(oper_p == NULL || !ConfOperDcc(oper_p))
		{
			sendto_server(":%s NOTICE %s :No access.",
					MYUID, UID(client_p));
			return;
		}

		/* request for us to dcc them.. */
		if(!strncasecmp(parv[1], "\001CHAT\001", 6))
		{
			connect_from_client(client_p, oper_p, target_p->name);
			return;
		}

		/* dcc request.. \001DCC CHAT chat <HOST> <IP>\001 */
		else if(!strncasecmp(parv[1], "\001DCC CHAT ", 10))
		{
			/* skip the first bit.. */
			char *host;
			char *cport;
			int port;

			p = LOCAL_COPY(parv[1]);
			p += 10;

			/* skip the 'chat' */
			if((host = strchr(p, ' ')) == NULL)
			{
				sendto_server(":%s NOTICE %s :Invalid dcc parameters",
						MYUID, UID(client_p));
				return;
			}

			*host++ = '\0';

			/* <host> <port>\001 */
			if((cport = strchr(host, ' ')) == NULL)
			{
				sendto_server(":%s NOTICE %s :Invalid dcc parameters",
						MYUID, UID(client_p));
				return;
			}

			*cport++ = '\0';

			/* another space? hmm. */
			if(strchr(cport, ' ') != NULL)
			{
				sendto_server(":%s NOTICE %s :Invalid dcc parameters",
						MYUID, UID(client_p));
				return;
			}

			if((p = strchr(cport, '\001')) == NULL)
			{
				sendto_server(":%s NOTICE %s :Invalid dcc parameters",
						MYUID, UID(client_p));
				return;
			}

			*p = '\0';

			if((port = atoi(cport)) <= 1024)
			{
				sendto_server(":%s NOTICE %s :Invalid dcc port",
						MYUID, UID(client_p));
				return;
			}

			connect_to_client(client_p, oper_p, host, port);
		}

		return;
	}

	text = LOCAL_COPY(parv[1]);
	handle_service_msg(target_p, client_p, text);
}

