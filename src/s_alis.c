/* src/s_alis.c
 *   Contains the code for ALIS, the Advanced List Service
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
 * $Id: s_alis.c 26819 2010-02-13 11:33:46Z leeh $
 */
#include "stdinc.h"

#ifdef ENABLE_ALIS
#include "service.h"
#include "rserv.h"
#include "langs.h"
#include "io.h"
#include "client.h"
#include "channel.h"
#include "c_init.h"
#include "log.h"
#include "conf.h"

#define ALIS_MAX_PARC	10

#define DIR_UNSET	0
#define DIR_SET		1
#define DIR_EQUAL	2

static struct client *alis_p;

static int s_alis_list(struct client *, struct lconn *, const char **, int);

static struct service_command alis_command[] =
{
	{ "LIST",	&s_alis_list,	1, NULL, 1, 0L, 0, 0, 0 }
};

static struct service_handler alis_service = {
	"ALIS", "ALIS", "alis", "services.int", "Advanced List Service",
        0, 0, alis_command, sizeof(alis_command), NULL, NULL, NULL
};

void
preinit_s_alis(void)
{
	alis_p = add_service(&alis_service);
}

/* alis_parse_mode()
 *   parses a given string into modes
 *
 * inputs	- text to parse, pointer to key, pointer to limit
 * outputs	- mode, or -1 on error.
 */
static int
alis_parse_mode(const char *text, int *key, int *limit)
{
	int mode = 0;

	if(EmptyString(text))
		return -1;

	while(*text)
	{
		switch(*text)
		{
			case 'i':
				mode |= MODE_INVITEONLY;
				break;
			case 'm':
				mode |= MODE_MODERATED;
				break;
			case 'n':
				mode |= MODE_NOEXTERNAL;
				break;
			case 't':
				mode |= MODE_TOPIC;
				break;
			case 'r':
				mode |= MODE_REGONLY;
				break;
			case 'S':
				mode |= MODE_SSLONLY;
				break;
			case 'l':
				*limit = 1;
				break;
			case 'k':
				*key = 1;
				break;
			default:
				return -1;
		}

		text++;
	}

	return mode;
}

struct alis_query
{
	const char *mask;
	const char *topic;
	int min;
	int max;
	int show_mode;
	int show_topicwho;
	int mode;
	int mode_dir;
	int mode_key;
	int mode_limit;
	int skip;
};

static int
parse_alis(struct client *client_p, struct alis_query *query,
	   const char *parv[], int parc)
{
	int i = 1;
	int param = 2;

	while(i < parc)
	{
		if(param >= parc || EmptyString(parv[param]))
		{
			service_err(alis_p, client_p, SVC_NEEDMOREPARAMS,
					alis_p->name, "LIST");
			return 0;
		}

		if(!strcasecmp(parv[i], "-min"))
		{
			if((query->min = atoi(parv[param])) < 1)
			{
				service_err(alis_p, client_p, SVC_OPTIONINVALID,
						alis_p->name, "LIST -min");
				return 0;
			}
		}
		else if(!strcasecmp(parv[i], "-max"))
		{
			if((query->max = atoi(parv[param])) < 1)
			{
				service_err(alis_p, client_p, SVC_OPTIONINVALID,
						alis_p->name, "LIST -max");
				return 0;
			}
		}
		else if(!strcasecmp(parv[i], "-skip"))
		{
			if((query->skip = atoi(parv[param])) < 1)
			{
				service_err(alis_p, client_p, SVC_OPTIONINVALID,
						alis_p->name, "LIST -skip");
				return 0;
			}
		}
		else if(!strcasecmp(parv[i], "-topic"))
		{
			query->topic = parv[param];
		}
		else if(!strcasecmp(parv[i], "-show"))
		{
			if(parv[param][0] == 'm')
			{
				query->show_mode = 1;

				if(parv[param][1] == 't')
					query->show_topicwho = 1;
			}
			else if(parv[param][0] == 't')
			{
				query->show_topicwho = 1;

				if(parv[param][1] == 'm')
					query->show_mode = 1;
			}
		}
		else if(!strcasecmp(parv[i], "-mode"))
		{
			const char *modestring;

			modestring = parv[param];

			switch(*modestring)
			{
				case '+':
					query->mode_dir = DIR_SET;
					break;
				case '-':
					query->mode_dir = DIR_UNSET;
					break;
				case '=':
					query->mode_dir = DIR_EQUAL;
					break;
				default:
					service_err(alis_p, client_p, SVC_OPTIONINVALID,
							alis_p->name, "LIST -mode");
					return 0;
			}

			query->mode = alis_parse_mode(modestring+1, 
					&query->mode_key, 
					&query->mode_limit);

			if(query->mode == -1)
			{
				service_err(alis_p, client_p, SVC_OPTIONINVALID,
						alis_p->name, "LIST -mode");
				return 0;
			}
		}
		else
		{
			service_err(alis_p, client_p, SVC_OPTIONINVALID,
					alis_p->name, "LIST");
			return 0;
		}

		i += 2;
		param += 2;
	}

	return 1;
}

static void
print_channel(struct client *client_p, struct channel *chptr,
	     struct alis_query *query)
{
	int show_topicwho = query->show_topicwho;

        /* cant show a topicwho, when a channel has no topic. */
        if(chptr->topic[0] == '\0')
                show_topicwho = 0;

	if(query->show_mode && show_topicwho)
		service_error(alis_p, client_p, "%-50s %-8s %3ld :%s (%s)",
			chptr->name, chmode_to_string_simple(&chptr->mode),
			dlink_list_length(&chptr->users),
			chptr->topic, chptr->topicwho);
	else if(query->show_mode)
		service_error(alis_p, client_p, "%-50s %-8s %3ld :%s",
			chptr->name, chmode_to_string_simple(&chptr->mode),
			dlink_list_length(&chptr->users),
			chptr->topic);
	else if(show_topicwho)
		service_error(alis_p, client_p, "%-50s %3ld :%s (%s)",
			chptr->name, dlink_list_length(&chptr->users),
			chptr->topic, chptr->topicwho);
	else
		service_error(alis_p, client_p, "%-50s %3ld :%s",
			chptr->name, dlink_list_length(&chptr->users),
			chptr->topic);
}

static int
show_channel(struct channel *chptr, struct alis_query *query)
{
        /* skip +s channels */
        if(chptr->mode.mode & MODE_SECRET)
                return 0;

        if(dlink_list_length(&chptr->users) < query->min ||
           (query->max && dlink_list_length(&chptr->users) > query->max))
                return 0;

        if(query->mode)
        {
                if(query->mode_dir == DIR_SET)
                {
                        if(((chptr->mode.mode & query->mode) == 0) ||
                           (query->mode_key && chptr->mode.key[0] == '\0') ||
                           (query->mode_limit && !chptr->mode.limit))
                                return 0;
                }
                else if(query->mode_dir == DIR_UNSET)
                {
                        if((chptr->mode.mode & query->mode) ||
                           (query->mode_key && chptr->mode.key[0] != '\0') ||
                           (query->mode_limit && chptr->mode.limit))
                                return 0;
                }
                else if(query->mode_dir == DIR_EQUAL)
                {
                        if((chptr->mode.mode != query->mode) ||
                           (query->mode_key && chptr->mode.key[0] == '\0') ||
                           (query->mode_limit && !chptr->mode.limit))
                                return 0;
                }
        }

        if(!match(query->mask, chptr->name))
                return 0;

        if(query->topic != NULL && !match(query->topic, chptr->topic))
                return 0;

        if(query->skip)
        {
                query->skip--;
                return 0;
        }

        return 1;
}

/* s_alis()
 *   Handles the listing of channels for ALIS.
 *
 * inputs	- client requesting list, params
 * outputs	-
 */
static int
s_alis_list(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct channel *chptr;
	struct alis_query query;
	dlink_node *ptr;
	int maxmatch = config_file.max_matches;

	memset(&query, 0, sizeof(struct alis_query));

        query.mask = parv[0];

        if(parc > 1)
        {
                if(!parse_alis(client_p, &query, parv, parc))
                        return 1;
        }

	zlog(alis_p, 1, 0, 0, client_p, NULL, "LIST %s", query.mask);

        service_err(alis_p, client_p, SVC_ALIS_LISTSTART,
		config_file.max_matches, query.mask);

        /* hunting for one channel.. */
        if(strchr(query.mask, '*') == NULL)
        {
                if((chptr = find_channel(query.mask)) != NULL)
                {
                        if(!(chptr->mode.mode & MODE_SECRET))
                                print_channel(client_p, chptr, &query);
                }

                service_err(alis_p, client_p, SVC_ENDOFLIST);
                return 1;
        }

        DLINK_FOREACH(ptr, channel_list.head)
        {
                chptr = ptr->data;

                /* matches, so show it */
                if(show_channel(chptr, &query))
                {
                        print_channel(client_p, chptr, &query);

                        if(--maxmatch == 0)
                        {
                                service_err(alis_p, client_p, SVC_ENDOFLISTLIMIT);
                                break;
                        }
                }
        }

        service_err(alis_p, client_p, SVC_ENDOFLIST);
        return 3;
}

#endif
