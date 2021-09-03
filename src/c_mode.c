/* src/c_mode.c
 *   Contains code for handling "MODE" command.
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
 * $Id: c_mode.c 26666 2009-09-17 18:49:37Z leeh $
 */
#include "stdinc.h"
#include "c_init.h"
#include "client.h"
#include "channel.h"
#include "scommand.h"
#include "log.h"
#include "hook.h"
#include "modebuild.h"

static void c_mode(struct client *, const char *parv[], int parc);
static void c_tmode(struct client *, const char *parv[], int parc);
static void c_bmask(struct client *, const char *parv[], int parc);
struct scommand_handler mode_command = { "MODE", c_mode, 0, DLINK_EMPTY };
struct scommand_handler tmode_command = { "TMODE", c_tmode, 0, DLINK_EMPTY };
struct scommand_handler bmask_command = { "BMASK", c_bmask, 0, DLINK_EMPTY };

/* linked list of services that were deopped */
static dlink_list deopped_list;
static dlink_list opped_list;
static dlink_list voiced_list;
static dlink_list ban_list;

/* valid_key()
 *   validates key, and transforms to lower ascii
 *
 * inputs  - key
 * outputs - 'fixed' version of key, NULL if invalid
 */
static const char *
valid_key(const char *data)
{
	static char buf[KEYLEN+1];
	unsigned char *s, c;
	unsigned char *fix = (unsigned char *) buf;

	strlcpy(buf, data, sizeof(buf));

	for(s = (unsigned char *) buf; (c = *s); s++)
	{
		c &= 0x7f;

		if(c == ':' || c <= ' ')
			return NULL;

		*fix++ = c;
	}

	*fix = '\0';

	return buf;
}

int
valid_ban(const char *banstr)
{
	char *tmp = LOCAL_COPY(banstr);
	char *nick, *user, *host;
	const char *p;

	for(p = banstr; *p; p++)
	{
		if(!IsBanChar(*p))
			return 0;
	}

	nick = tmp;

	if((user = strchr(nick, '!')) == NULL)
		return 0;

	*user++ = '\0';

	if((host = strchr(user, '@')) == NULL)
		return 0;

	*host++ = '\0';

	if(EmptyString(nick) || EmptyString(user) || EmptyString(host))
		return 0;

	if(strlen(nick) > NICKLEN || strlen(user) > USERLEN || strlen(host) > HOSTLEN)
		return 0;

	return 1;
}

static void *
add_ban(const char *banstr, dlink_list *list)
{
	char *ban;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, list->head)
	{
		if(!irccmp((const char *) ptr->data, banstr))
			return NULL;
	}

	ban = my_strdup(banstr);
	dlink_add_alloc(ban, list);
	return ban;
}

/* IMPORTANT:  The void * pointer that this function returns refers to
 * memory that has been free()'d by the time the function exits.
 *
 * Do *NOT* dereference the return value from this function.
 */
void *
del_ban(const char *banstr, dlink_list *list)
{
	void *banptr;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, list->head)
	{
		if(!irccmp(banstr, (const char *) ptr->data))
		{
			/* store the memory address of the pointer, we can
			 * then tell whether this exact ban needs to be
			 * removed from ban_list.. --anfl
			 */
			banptr = ptr->data;

			my_free(ptr->data);
			dlink_destroy(ptr, list);
			return banptr;
		}
	}

	return NULL;
}


int
parse_simple_mode(struct chmode *mode, const char *parv[], int parc, 
		int start, int allow_sslonly)
{
	const char *p = parv[start];
	int dir = 1;

	if(parc <= start)
		return 0;

	start++;

	for(; *p; p++)
	{
		switch(*p)
		{
			case '+':
				dir = 1;
				break;
			case '-':
				dir = 0;
				break;

			case 'i':
				if(dir)
					mode->mode |= MODE_INVITEONLY;
				else
					mode->mode &= ~MODE_INVITEONLY;
				break;
			case 'm':
				if(dir)
					mode->mode |= MODE_MODERATED;
				else
					mode->mode &= ~MODE_MODERATED;
				break;
			case 'n':
				if(dir)
					mode->mode |= MODE_NOEXTERNAL;
				else
					mode->mode &= ~MODE_NOEXTERNAL;
				break;
			case 'p':
				if(dir)
					mode->mode |= MODE_PRIVATE;
				else
					mode->mode &= ~MODE_PRIVATE;
				break;
			case 's':
				if(dir)
					mode->mode |= MODE_SECRET;
				else
					mode->mode &= ~MODE_SECRET;
				break;
			case 't':
				if(dir)
					mode->mode |= MODE_TOPIC;
				else
					mode->mode &= ~MODE_TOPIC;
				break;
			case 'r':
				if(dir)
					mode->mode |= MODE_REGONLY;
				else
					mode->mode &= ~MODE_REGONLY;
				break;
			case 'S':
				if(allow_sslonly)
				{
					if(dir)
						mode->mode |= MODE_SSLONLY;
					else
						mode->mode &= ~MODE_SSLONLY;
				}
				else
					return 0;

				break;

			case 'k':
				if(EmptyString(parv[start]))
					return 0;

				if(dir)
				{
					const char *fixed = valid_key(parv[start]);

					if(fixed == NULL)
						return 0;

					mode->mode |= MODE_KEY;
					strlcpy(mode->key, fixed,
						sizeof(mode->key));
				}
				else
				{
					mode->mode &= ~MODE_KEY;
					mode->key[0] = '\0';
				}

				start++;
				break;
			case 'l':
				if(dir)
				{
					if(EmptyString(parv[start]))
						return 0;

					mode->mode |= MODE_LIMIT;
					mode->limit = atoi(parv[start]);
					start++;
				}
				else
				{
					mode->mode &= ~MODE_LIMIT;
					mode->limit = 0;
				}

				break;

			default:
				return 0;
				break;
		}
	}

	return start;
}

void
parse_full_mode(struct channel *chptr, struct client *source_p,
		const char **parv, int parc, int start, int allow_sslonly)
{
	const char *p = parv[start];
	int dir = DIR_ADD;

	if(parc <= start)
		return;

	if(source_p)
		modebuild_start(source_p, chptr);

	start++;

	for(; *p; p++)
	{
		switch(*p)
		{
		case '+':
			dir = DIR_ADD;
			break;
		case '-':
			dir = DIR_DEL;
			break;

		case 'i':
			if(dir)
				chptr->mode.mode |= MODE_INVITEONLY;
			else
				chptr->mode.mode &= ~MODE_INVITEONLY;

			if(source_p)
				modebuild_add(dir, "i", NULL);

			break;
		case 'm':
			if(dir)
				chptr->mode.mode |= MODE_MODERATED;
			else
				chptr->mode.mode &= ~MODE_MODERATED;

			if(source_p)
				modebuild_add(dir, "m", NULL);

			break;
		case 'n':
			if(dir)
				chptr->mode.mode |= MODE_NOEXTERNAL;
			else
				chptr->mode.mode &= ~MODE_NOEXTERNAL;

			if(source_p)
				modebuild_add(dir, "n", NULL);

			break;
		case 'p':
			if(dir)
				chptr->mode.mode |= MODE_PRIVATE;
			else
				chptr->mode.mode &= ~MODE_PRIVATE;

			if(source_p)
				modebuild_add(dir, "p", NULL);

			break;
		case 's':
			if(dir)
				chptr->mode.mode |= MODE_SECRET;
			else
				chptr->mode.mode &= ~MODE_SECRET;

			if(source_p)
				modebuild_add(dir, "s", NULL);

			break;
		case 't':
			if(dir)
				chptr->mode.mode |= MODE_TOPIC;
			else
				chptr->mode.mode &= ~MODE_TOPIC;

			if(source_p)
				modebuild_add(dir, "t", NULL);

			break;
		case 'r':
			if(dir)
				chptr->mode.mode |= MODE_REGONLY;
			else
				chptr->mode.mode &= ~MODE_REGONLY;

			if(source_p)
				modebuild_add(dir, "r", NULL);

			break;
		case 'S':
			if(allow_sslonly)
			{
				if(dir)
					chptr->mode.mode |= MODE_SSLONLY;
				else
					chptr->mode.mode &= ~MODE_SSLONLY;

				if(source_p)
					modebuild_add(dir, "S", NULL);
			}
			else
				return;

			break;

		case 'k':
			if(EmptyString(parv[start]))
				return;

			if(dir)
			{
				chptr->mode.mode |= MODE_KEY;
				strlcpy(chptr->mode.key, parv[start],
					sizeof(chptr->mode.key));

				if(source_p)
					modebuild_add(dir, "k",	chptr->mode.key);
			}
			else
			{
				chptr->mode.mode &= ~MODE_KEY;
				chptr->mode.key[0] = '\0';

				if(source_p)
					modebuild_add(dir, "k", "*");
			}


			start++;
			break;
		case 'l':
			if(dir)
			{
				const char *limit_s;
				char *endptr;
				int limit;

				if(EmptyString(parv[start]))
					return;

				limit_s = parv[start];
				start++;

				limit = strtol(limit_s, &endptr, 10);

				if(limit <= 0)
					return;

				if(source_p)
				{
					/* we used what they passed as the
					 * mode issued, so it has to be valid
					 */
					if(!EmptyString(endptr))
						return;

					modebuild_add(dir, "l", limit_s);
				}

				chptr->mode.mode |= MODE_LIMIT;
				chptr->mode.limit = limit;
			}
			else
			{
				chptr->mode.mode &= ~MODE_LIMIT;
				chptr->mode.limit = 0;

				if(source_p)
					modebuild_add(dir, "l", NULL);
			}

			break;

		case 'o':
		case 'v':
		{
			struct client *target_p;
			struct chmember *mptr;
			const char *nick;

			if(EmptyString(parv[start]))
				return;

			nick = parv[start];
			start++;

			if((target_p = find_service(nick)) != NULL)
			{
				/* dont allow generating modes against
				 * services.. dont care about anything other
				 * than +o either.  We lose state of +v on
				 * services, but it doesnt matter.
				 */
				if(source_p || *p != 'o')
					break;

				/* handle -o+o */
				if(dir)
					dlink_find_destroy(target_p, &deopped_list);
				/* this is a -o */
				else if(dlink_find(target_p, &deopped_list) == NULL)
					dlink_add_alloc(target_p, &deopped_list);

				break;
			}

			if((target_p = find_user(nick, 1)) == NULL)
				break;

			if((mptr = find_chmember(chptr, target_p)) == NULL)
				break;

			if(*p == 'o')
			{
				if(dir)
				{
					mptr->flags &= ~MODE_DEOPPED;

					/* ignore redundant modes */
					if(mptr->flags & MODE_OPPED)
						continue;

					mptr->flags |= MODE_OPPED;
					dlink_add_alloc(mptr, &opped_list);
				}
				else if(mptr->flags & MODE_OPPED)
				{
					dlink_find_destroy(mptr, &opped_list);
					mptr->flags &= ~MODE_OPPED;
				}

				if(source_p)
					modebuild_add(dir, "o", nick);
			}
			else
			{
				if(dir)
				{
					/* ignore redundant modes */
					if(mptr->flags & MODE_VOICED)
						continue;

					mptr->flags |= MODE_VOICED;
					dlink_add_alloc(mptr, &voiced_list);
				}
				else if(mptr->flags & MODE_VOICED)
				{
					dlink_find_destroy(mptr, &voiced_list);
					mptr->flags &= ~MODE_VOICED;
				}

				if(source_p)
					modebuild_add(dir, "v", nick);
			}

			break;
		}


		case 'b':
			if(EmptyString(parv[start]))
				return;

			if(dir)
			{
				void *banstr = add_ban(parv[start], &chptr->bans);

				if(banstr)
					dlink_add_alloc(banstr, &ban_list);
			}
			else
			{
				/* DO NOT DEREFERENCE THIS */
				void *banptr = del_ban(parv[start], &chptr->bans);

				if(banptr)
					dlink_find_destroy(banptr, &ban_list);
			}

			if(source_p)
				modebuild_add(dir, "b", parv[start]);

			start++;
			break;

		case 'e':
			if(EmptyString(parv[start]))
				return;

			if(dir)
				add_ban(parv[start], &chptr->excepts);
			else
				del_ban(parv[start], &chptr->excepts);

			if(source_p)
				modebuild_add(dir, "e", parv[start]);

			start++;
			break;

		case 'I':
			if(EmptyString(parv[start]))
				return;

			if(dir)
				add_ban(parv[start], &chptr->invites);
			else
				del_ban(parv[start], &chptr->invites);

			if(source_p)
				modebuild_add(dir, "I", parv[start]);

			start++;
			break;
		}
	}

	if(source_p)
		modebuild_finish();
}

static void
handle_chmode(struct channel *chptr, struct client *source_p, int parc, const char **parv)
{
	struct client *target_p;
	dlink_node *ptr;
	dlink_node *next_ptr;
	struct chmode oldmode;

	oldmode.mode = chptr->mode.mode;
	oldmode.limit = chptr->mode.limit;
	if(EmptyString(chptr->mode.key))
		oldmode.key[0] = '\0';
	else
		strlcpy(oldmode.key, chptr->mode.key, sizeof(chptr->mode.key));

	parse_full_mode(chptr, NULL, (const char **) parv, parc, 0, 1);

	if(dlink_list_length(&opped_list))
		hook_call(HOOK_MODE_OP, chptr, &opped_list);

	if(dlink_list_length(&voiced_list))
		hook_call(HOOK_MODE_VOICE, chptr, &voiced_list);

	if(oldmode.mode != chptr->mode.mode || oldmode.limit != chptr->mode.limit ||
	   strcasecmp(oldmode.key, chptr->mode.key))
		hook_call(HOOK_MODE_SIMPLE, chptr, NULL);

	if(IsUser(source_p) && dlink_list_length(&ban_list))
		hook_call(HOOK_MODE_BAN, chptr, &ban_list);

	DLINK_FOREACH_SAFE(ptr, next_ptr, opped_list.head)
	{
		free_dlink_node(ptr);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, voiced_list.head)
	{
		free_dlink_node(ptr);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, ban_list.head)
	{
		free_dlink_node(ptr);
	}

	opped_list.head = opped_list.tail = NULL;
	voiced_list.head = voiced_list.tail = NULL;
	opped_list.length = voiced_list.length = 0;
	ban_list.head = ban_list.tail = NULL;
	ban_list.length = 0;

	/* some services were deopped.. */
	DLINK_FOREACH_SAFE(ptr, next_ptr, deopped_list.head)
	{
		target_p = ptr->data;
		rejoin_service(target_p, chptr, 1);
		dlink_destroy(ptr, &deopped_list);
	}
}

/* c_mode()
 *   the MODE handler
 */
static void
c_mode(struct client *client_p, const char *parv[], int parc)
{
	struct client *target_p;
	struct channel *chptr;
	struct chmember *msptr;

	if(parc < 1 || EmptyString(parv[0]))
		return;

	/* user setting mode:
	 * :<user> MODE <user> +<modes>
	 */
	if(!IsChanPrefix(parv[0][0]))
	{
		if(parc < 2 || EmptyString(parv[1]))
			return;

		if((target_p = find_user(parv[0], 1)) == NULL)
			return;

		if(target_p != client_p)
			return;

		target_p->user->umode = string_to_umode(parv[1], target_p->user->umode);
		return;
	}

	/* channel mode, need 3 params */
	if(parc < 2 || EmptyString(parv[1]))
		return;

	if((chptr = find_channel(parv[0])) == NULL)
		return;

	/* user marked as being deopped, bounce mode changes */
	if(IsUser(client_p) && (msptr = find_chmember(chptr, client_p)) &&
	   (msptr->flags & MODE_DEOPPED))
		return;

	handle_chmode(chptr, client_p, parc - 1, parv + 1);
}

/* c_tmode()
 *   the TMODE handler
 */
static void
c_tmode(struct client *client_p, const char *parv[], int parc)
{
	struct channel *chptr;

	if(parc < 3 || EmptyString(parv[2]))
		return;

	if((chptr = find_channel(parv[1])) == NULL)
		return;

	if(atol(parv[0]) > chptr->tsinfo)
		return;

	/* MODE_DEOPPED check removed, this is not possible given the
	 * TS on the mode is correct -- jilles */

	handle_chmode(chptr, client_p, parc - 2, parv + 2);
}

static void
c_bmask(struct client *client_p, const char *parv[], int parc)
{
	struct channel *chptr;
	dlink_list *banlist;
	const char *s;
	char *t;

	if((chptr = find_channel(parv[1])) == NULL)
		return;

	/* TS is higher, drop it. */
	if(atol(parv[0]) > chptr->tsinfo)
		return;

	switch(parv[2][0])
	{
		case 'b':
			banlist = &chptr->bans;
			break;

		case 'e':
			banlist = &chptr->excepts;
			break;

		case 'I':
			banlist = &chptr->invites;
			break;

		default:
			return;
	}

	s = LOCAL_COPY(parv[3]);

	while(*s == ' ')
		s++;

	/* next char isnt a space, point t to the next one */
	if((t = strchr(s, ' ')) != NULL)
	{
		*t++ = '\0';

		/* double spaces will break the parser */
		while(*t == ' ')
			t++;
	}

	/* couldve skipped spaces and got nothing.. */
	while(!EmptyString(s))
	{
		/* ban with a leading ':' -- this will break the protocol */
		if(*s != ':' && valid_ban(s))
			add_ban(s, banlist);

		s = t;

		if(s != NULL)
		{
			if((t = strchr(s, ' ')) != NULL)
			{
				*t++ = '\0';

				while(*t == ' ')
					t++;
			}
		}
	}
}


