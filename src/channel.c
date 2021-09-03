/* src/channel.c
 *   Contains code for handling changes within channels.
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
 * $Id: channel.c 27011 2010-03-30 19:46:44Z leeh $
 */
#include "stdinc.h"
#include "rserv.h"
#include "client.h"
#include "conf.h"
#include "tools.h"
#include "channel.h"
#include "scommand.h"
#include "log.h"
#include "balloc.h"
#include "io.h"
#include "hook.h"

static dlink_list channel_table[MAX_CHANNEL_TABLE];
dlink_list channel_list;

static BlockHeap *channel_heap;
static BlockHeap *chmember_heap;

static void c_join(struct client *, const char *parv[], int parc);
static void c_kick(struct client *, const char *parv[], int parc);
static void c_part(struct client *, const char *parv[], int parc);
static void c_sjoin(struct client *, const char *parv[], int parc);
static void c_tb(struct client *, const char *parv[], int parc);
static void c_topic(struct client *, const char *parv[], int parc);

static struct scommand_handler join_command = { "JOIN", c_join, 0, DLINK_EMPTY };
static struct scommand_handler kick_command = { "KICK", c_kick, 0, DLINK_EMPTY };
static struct scommand_handler part_command = { "PART", c_part, 0, DLINK_EMPTY };
static struct scommand_handler sjoin_command = { "SJOIN", c_sjoin, 0, DLINK_EMPTY };
static struct scommand_handler tb_command = { "TB", c_tb, 0, DLINK_EMPTY };
static struct scommand_handler topic_command = { "TOPIC", c_topic, 0, DLINK_EMPTY };

/* init_channel()
 *   initialises various things
 */
void
init_channel(void)
{
        channel_heap = BlockHeapCreate("Channel", sizeof(struct channel), HEAP_CHANNEL);
        chmember_heap = BlockHeapCreate("Channel Member", sizeof(struct chmember), HEAP_CHMEMBER);

	add_scommand_handler(&join_command);
	add_scommand_handler(&kick_command);
	add_scommand_handler(&part_command);
	add_scommand_handler(&sjoin_command);
	add_scommand_handler(&tb_command);
	add_scommand_handler(&topic_command);
}

/* hash_channel()
 *   hashes the name of a channel
 *
 * inputs	- channel name
 * outputs	- hash value
 */
unsigned int
hash_channel(const char *p)
{
	int i = 30;
	unsigned int h = 0;

	while(*p && --i)
	{
		h = (h << 4) - (h + (unsigned char) ToLower(*p++));
	}

	return(h & (MAX_CHANNEL_TABLE-1));
}

int
valid_chname(const char *name)
{
	if(strlen(name) > CHANNELLEN)
		return 0;

	if(name[0] != '#')
		return 0;

	return 1;
}

/* add_channel()
 *   adds a channel to the internal hash, and channel_list
 *
 * inputs	- channel to add
 * outputs	-
 */
void
add_channel(struct channel *chptr)
{
	unsigned int hashv = hash_channel(chptr->name);
	dlink_add(chptr, &chptr->nameptr, &channel_table[hashv]);
	dlink_add(chptr, &chptr->listptr, &channel_list);
}

/* del_channel()
 *   removes a channel from the internal hash and channel_list
 *
 * inputs	- channel to remove
 * outputs	-
 */
void
del_channel(struct channel *chptr)
{
	unsigned int hashv = hash_channel(chptr->name);
	dlink_delete(&chptr->nameptr, &channel_table[hashv]);
	dlink_delete(&chptr->listptr, &channel_list);
}

/* find_channel()
 *   hunts for a channel in the hash
 *
 * inputs	- channel name to find
 * outputs	- channel struct, or NULL if not found
 */
struct channel *
find_channel(const char *name)
{
	struct channel *chptr;
	dlink_node *ptr;
	unsigned int hashv = hash_channel(name);

	DLINK_FOREACH(ptr, channel_table[hashv].head)
	{
		chptr = ptr->data;

		if(!irccmp(chptr->name, name))
			return chptr;
	}

	return NULL;
}

/* free_channel()
 *   removes a channel from hash, and free's the memory its using
 *
 * inputs	- channel to free
 * outputs	-
 */
void
free_channel(struct channel *chptr)
{
	if(chptr == NULL)
		return;

	del_channel(chptr);

	BlockHeapFree(channel_heap, chptr);
}

/* add_chmember()
 *   adds a given client to a given channel with given flags
 *
 * inputs	- channel to add to, client to add, flags
 * outputs	-
 */
struct chmember *
add_chmember(struct channel *chptr, struct client *target_p, int flags)
{
	struct chmember *mptr;

	mptr = BlockHeapAlloc(chmember_heap);

	mptr->client_p = target_p;
	mptr->chptr = chptr;
	mptr->flags = flags;

	dlink_add(mptr, &mptr->chnode, &chptr->users);
	dlink_add(mptr, &mptr->usernode, &target_p->user->channels);

	return mptr;
}

/* del_chmember()
 *   removes a given member from a channel
 *
 * inputs	- chmember to remove
 * outputs	-
 */
void
del_chmember(struct chmember *mptr)
{
	struct channel *chptr;
	struct client *client_p;

	if(mptr == NULL)
		return;

	chptr = mptr->chptr;
	client_p = mptr->client_p;

	dlink_delete(&mptr->chnode, &chptr->users);
	dlink_delete(&mptr->usernode, &client_p->user->channels);

	if(dlink_list_length(&chptr->users) == 0 &&
	   dlink_list_length(&chptr->services) == 0)
		free_channel(chptr);

	BlockHeapFree(chmember_heap, mptr);
}

/* find_chmember()
 *   hunts for a chmember struct for the given user in given channel
 *
 * inputs	- channel to search, client to search for
 * outputs	- chmember struct if found, else NULL
 */
struct chmember *
find_chmember(struct channel *chptr, struct client *target_p)
{
	struct chmember *mptr;
	dlink_node *ptr;

	if (dlink_list_length(&chptr->users) < dlink_list_length(&target_p->user->channels))
	{
		DLINK_FOREACH(ptr, chptr->users.head)
		{
			mptr = ptr->data;
			if(mptr->client_p == target_p)
				return mptr;
		}
	}
	else
	{
		DLINK_FOREACH(ptr, target_p->user->channels.head)
		{
			mptr = ptr->data;
			if(mptr->chptr == chptr)
				return mptr;
		}
	}

	return NULL;
}

int
find_exempt(struct channel *chptr, struct client *target_p)
{
	dlink_node *ptr;

	DLINK_FOREACH(ptr, chptr->excepts.head)
	{
		if(match((const char *) ptr->data, target_p->user->mask))
			return 1;
	}

	return 0;
}

/* count_topics()
 *   counts the number of channels which have a topic
 *
 * inputs       -
 * outputs      - number of channels with topics
 */
unsigned long
count_topics(void)
{
        struct channel *chptr;
        dlink_node *ptr;
        unsigned long topic_count = 0;

        DLINK_FOREACH(ptr, channel_list.head)
        {
                chptr = ptr->data;

                if(chptr->topic[0] != '\0')
                        topic_count++;
        }

        return topic_count;
}

/* join service to chname, create channel with TS tsinfo, using mode in the
 * SJOIN. if channel already exists, don't use tsinfo -- jilles */
/* that is, unless override is specified */
void
join_service(struct client *service_p, const char *chname, time_t tsinfo,
		struct chmode *mode, int override)
{
	struct channel *chptr;

	/* channel doesnt exist, have to join it */
	if((chptr = find_channel(chname)) == NULL)
	{
		chptr = BlockHeapAlloc(channel_heap);

		strlcpy(chptr->name, chname, sizeof(chptr->name));
		chptr->tsinfo = tsinfo ? tsinfo : CURRENT_TIME;

		if(mode != NULL)
		{
			chptr->mode.mode = mode->mode;
			chptr->mode.limit = mode->limit;

			if(mode->key[0])
				strlcpy(chptr->mode.key, mode->key,
					sizeof(chptr->mode.key));
		}
		else
			chptr->mode.mode = MODE_NOEXTERNAL|MODE_TOPIC;

		add_channel(chptr);
	}
	/* may already be joined.. */
	else if(dlink_find(service_p, &chptr->services) != NULL)
	{
		return;
	}
	else if(override && tsinfo < chptr->tsinfo)
	{
		chptr->tsinfo = tsinfo;

		if(mode != NULL)
		{
			chptr->mode.mode = mode->mode;
			chptr->mode.limit = mode->limit;

			if(mode->key[0])
				strlcpy(chptr->mode.key, mode->key,
					sizeof(chptr->mode.key));
		}
		else
			chptr->mode.mode = MODE_NOEXTERNAL|MODE_TOPIC;
	}

	dlink_add_alloc(service_p, &chptr->services);
	dlink_add_alloc(chptr, &service_p->service->channels);

	if(sent_burst)
		sendto_server(":%s SJOIN %lu %s %s :@%s",
				MYUID, (unsigned long) chptr->tsinfo, 
				chptr->name, chmode_to_string(&chptr->mode), 
				SVC_UID(service_p));
}

int
part_service(struct client *service_p, const char *chname)
{
	struct channel *chptr;

	if((chptr = find_channel(chname)) == NULL)
		return 0;

	if(dlink_find(service_p, &chptr->services) == NULL)
		return 0;

	dlink_find_destroy(service_p, &chptr->services);
	dlink_find_destroy(chptr, &service_p->service->channels);

	if(sent_burst)
		sendto_server(":%s PART %s", SVC_UID(service_p), chptr->name);

	if(dlink_list_length(&chptr->users) == 0 &&
	   dlink_list_length(&chptr->services) == 0)
		free_channel(chptr);

	return 1;
}

void
rejoin_service(struct client *service_p, struct channel *chptr, int reop)
{
	/* we are only doing this because we need a reop */
	if(reop)
	{
		/* can do this rather more simply */
		if(config_file.ratbox)
		{
			sendto_server(":%s MODE %s +o %s",
					MYUID, chptr->name, SVC_UID(service_p));
			return;
		}

		sendto_server(":%s PART %s", SVC_UID(service_p), chptr->name);
	}

	sendto_server(":%s SJOIN %lu %s %s :@%s",
			MYUID, (unsigned long) chptr->tsinfo, chptr->name, 
			chmode_to_string(&chptr->mode),  SVC_UID(service_p));
}

/* c_kick()
 *   the KICK handler
 */
static void
c_kick(struct client *client_p, const char *parv[], int parc)
{
	struct client *target_p;
	struct channel *chptr;
	struct chmember *mptr;

	if(parc < 2 || EmptyString(parv[1]))
		return;

	if((chptr = find_channel(parv[0])) == NULL)
		return;

	if((target_p = find_service(parv[1])) != NULL)
	{
		rejoin_service(target_p, chptr, 0);
		return;
	}

	if((target_p = find_user(parv[1], 1)) == NULL)
		return;

	if((mptr = find_chmember(chptr, target_p)) == NULL)
		return;

	del_chmember(mptr);
}
		
/* c_part()
 *   the PART handler
 */
static void
c_part(struct client *client_p, const char *parv[], int parc)
{
	struct chmember *mptr;
	struct channel *chptr;

	if(parc < 1 || EmptyString(parv[0]))
		return;

	if(!IsUser(client_p))
		return;

	if((chptr = find_channel(parv[0])) == NULL)
		return;

	if((mptr = find_chmember(chptr, client_p)) == NULL)
		return;

	del_chmember(mptr);
}

/* c_topic()
 *   the TOPIC handler
 */
static void
c_topic(struct client *client_p, const char *parv[], int parc)
{
	struct channel *chptr;

	if(parc < 2)
		return;

	if((chptr = find_channel(parv[0])) == NULL)
		return;

	if(EmptyString(parv[1]))
	{
		chptr->topic[0] = '\0';
		chptr->topicwho[0] = '\0';
		chptr->topic_tsinfo = 0;
	}
	else
	{
		strlcpy(chptr->topic, parv[1], sizeof(chptr->topic));

		if(IsUser(client_p))
			snprintf(chptr->topicwho, sizeof(chptr->topicwho), "%s!%s@%s", 
				 client_p->name, client_p->user->username, 
				 client_p->user->host);
		else
			strlcpy(chptr->topicwho, client_p->name, sizeof(chptr->topicwho));

		chptr->topic_tsinfo = CURRENT_TIME;
	}

	hook_call(HOOK_CHANNEL_TOPIC, chptr, NULL);
}

/* c_tb()
 *   the TB handler
 */
static void
c_tb(struct client *client_p, const char *parv[], int parc)
{
	struct channel *chptr;
	time_t topicts;

	if(parc < 3 || !IsServer(client_p))
		return;

	if((chptr = find_channel(parv[0])) == NULL)
		return;

	topicts = atol(parv[1]);

	/* If we have a topic that is older than the one burst to us, ours
	 * wins -- otherwise process the topic burst
	 */
	if(!EmptyString(chptr->topic) && topicts > chptr->topic_tsinfo)
		return;

	/* :<server> TB <#channel> <topicts> <topicwho> :<topic> */
	if(parc == 4)
	{
		if(EmptyString(parv[3]))
			return;

		strlcpy(chptr->topic, parv[3], sizeof(chptr->topic));
		strlcpy(chptr->topicwho, parv[2], sizeof(chptr->topicwho));
		chptr->topic_tsinfo = CURRENT_TIME;
	}
	/* :<server> TB <#channel> <topicts> :<topic> */
	else
	{
		if(EmptyString(parv[2]))
			return;

		strlcpy(chptr->topic, parv[2], sizeof(chptr->topic));
		strlcpy(chptr->topicwho, client_p->name, sizeof(chptr->topicwho));
		chptr->topic_tsinfo = CURRENT_TIME;
	}

	hook_call(HOOK_CHANNEL_TOPIC, chptr, NULL);
}

/* remove_our_modes()
 *   clears our channel modes from a channel
 *
 * inputs	- channel to remove modes from
 * outputs	-
 */
void
remove_our_modes(struct channel *chptr)
{
	struct chmember *msptr;
	dlink_node *ptr;

	chptr->mode.mode = 0;
	chptr->mode.key[0] = '\0';
	chptr->mode.limit = 0;

	DLINK_FOREACH(ptr, chptr->users.head)
	{
		msptr = ptr->data;

		msptr->flags &= ~(MODE_OPPED|MODE_VOICED);
	}
}

/* remove_bans()
 *   clears +beI modes from a channel
 *
 * inputs	- channel to remove modes from
 * outputs	-
 */
void
remove_bans(struct channel *chptr)
{
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->bans.head)
	{
		my_free(ptr->data);
		dlink_destroy(ptr, &chptr->bans);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->excepts.head)
	{
		my_free(ptr->data);
		dlink_destroy(ptr, &chptr->excepts);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, chptr->invites.head)
	{
		my_free(ptr->data);
		dlink_destroy(ptr, &chptr->invites);
	}
}

/* chmode_to_string()
 *   converts a channels mode into a string
 *
 * inputs	- channel to get modes for
 * outputs	- string version of modes
 */
const char *
chmode_to_string(struct chmode *mode)
{
	static char buf[BUFSIZE];
	char *p;

	p = buf;

	*p++ = '+';

	if(mode->mode & MODE_INVITEONLY)
		*p++ = 'i';
	if(mode->mode & MODE_MODERATED)
		*p++ = 'm';
	if(mode->mode & MODE_NOEXTERNAL)
		*p++ = 'n';
	if(mode->mode & MODE_PRIVATE)
		*p++ = 'p';
	if(mode->mode & MODE_SECRET)
		*p++ = 's';
	if(mode->mode & MODE_TOPIC)
		*p++ = 't';
	if(mode->mode & MODE_REGONLY)
		*p++ = 'r';
	if(mode->mode & MODE_SSLONLY)
		*p++ = 'S';

	if(mode->limit && mode->key[0])
	{
		sprintf(p, "lk %d %s", mode->limit, mode->key);
	}
	else if(mode->limit)
	{
		sprintf(p, "l %d", mode->limit);
	}
	else if(mode->key[0])
	{
		sprintf(p, "k %s", mode->key);
	}
	else
		*p = '\0';

	return buf;
}

/* chmode_to_string_string()
 *   converts a channels mode into a simple string (doesnt contain key/limit)
 *
 * inputs	- channel to get modes for
 * outputs	- string version of modes
 */
const char *
chmode_to_string_simple(struct chmode *mode)
{
	static char buf[10];
	char *p;

	p = buf;

	*p++ = '+';

	if(mode->mode & MODE_INVITEONLY)
		*p++ = 'i';
	if(mode->mode & MODE_MODERATED)
		*p++ = 'm';
	if(mode->mode & MODE_NOEXTERNAL)
		*p++ = 'n';
	if(mode->mode & MODE_PRIVATE)
		*p++ = 'p';
	if(mode->mode & MODE_SECRET)
		*p++ = 's';
	if(mode->mode & MODE_TOPIC)
		*p++ = 't';
	if(mode->mode & MODE_REGONLY)
		*p++ = 'r';
	if(mode->mode & MODE_SSLONLY)
		*p++ = 'S';
	if(mode->limit)
		*p++ = 'l';
	if(mode->key[0])
		*p++ = 'k';

	*p = '\0';

	return buf;
}

/* c_sjoin()
 *   the SJOIN handler
 */
static void
c_sjoin(struct client *client_p, const char *parv[], int parc)
{
	struct channel *chptr;
	struct client *target_p;
	struct chmode newmode;
	struct chmember *member_p;
	dlink_list joined_members;
	dlink_node *ptr;
	dlink_node *next_ptr;
	char *p;
	const char *s;
	char *nicks;
	time_t newts;
	int flags = 0;
	int keep_old_modes = 1;
	int keep_new_modes = 1;
	int args = 0;

	memset(&joined_members, 0, sizeof(dlink_list));

	/* :<server> SJOIN <TS> <#channel> +[modes [key][limit]] :<nicks> */
	if(parc < 4 || EmptyString(parv[3]))
		return;

	if(!valid_chname(parv[1]))
		return;

	if((chptr = find_channel(parv[1])) == NULL)
	{
		chptr = BlockHeapAlloc(channel_heap);

		strlcpy(chptr->name, parv[1], sizeof(chptr->name));
		newts = chptr->tsinfo = atol(parv[0]);
		add_channel(chptr);

		keep_old_modes = 0;
	}
	else
	{
		newts = atol(parv[0]);

		if(newts == 0 || chptr->tsinfo == 0)
			chptr->tsinfo = 0;
		else if(newts < chptr->tsinfo)
			keep_old_modes = 0;
		else if(chptr->tsinfo < newts)
			keep_new_modes = 0;
	}

	newmode.mode = 0;
	newmode.key[0] = '\0';
	newmode.limit = 0;

	/* mode of 0 is sent when someone joins remotely with higher TS. */
	if(strcmp(parv[2], "0"))
	{
		args = parse_simple_mode(&newmode, parv, parc, 2, 1);

		/* invalid mode */
		s_assert(args);
		if(!args)
		{
			mlog("PROTO: SJOIN issued with invalid mode: %s",
				rebuild_params(parv, parc, 2));
			return;
		}
	}
	else
		args = 3;

	if(!keep_old_modes)
	{
		chptr->tsinfo = newts;
		remove_our_modes(chptr);
		/* If the source does TS6, also remove all +beI modes */
		if (!EmptyString(client_p->uid))
			remove_bans(chptr);

		/* services is in there.. rejoin */
		if(sent_burst)
		{
			DLINK_FOREACH(ptr, chptr->services.head)
			{
				rejoin_service(ptr->data, chptr, 1);
			}
		}
	}

	if(keep_new_modes)
	{
		chptr->mode.mode |= newmode.mode;

		if(!chptr->mode.limit || chptr->mode.limit < newmode.limit)
			chptr->mode.limit = newmode.limit;

		if(!chptr->mode.key[0] || strcmp(chptr->mode.key, newmode.key) > 0)
			strlcpy(chptr->mode.key, newmode.key,
				sizeof(chptr->mode.key));

	}

	/* this must be done after we've updated the modes */
	if(!keep_old_modes)
		hook_call(HOOK_SJOIN_LOWERTS, chptr, NULL);

	if(EmptyString(parv[args]))
		return;

	nicks = LOCAL_COPY(parv[args]);

        /* now parse the nicklist */
	for(s = nicks; !EmptyString(s); s = p)
	{
		flags = 0;

		/* remove any leading spaces.. */
		while(*s == ' ')
			s++;

		/* point p to the next nick */
		if((p = strchr(s, ' ')) != NULL)
			*p++ = '\0';

		if(*s == '@')
		{
			flags |= MODE_OPPED;
			s++;

			if(*s == '+')
			{
				flags |= MODE_VOICED;
				s++;
			}
		}
		else if(*s == '+')
		{
			flags |= MODE_VOICED;
			s++;

			if(*s == '@')
			{
				flags |= MODE_OPPED;
				s++;
			}
		}

		if(!keep_new_modes)
		{
			if(flags & MODE_OPPED)
				flags = MODE_DEOPPED;
			else
				flags = 0;
		}

		if((target_p = find_user(s, 1)) == NULL)
			continue;

		if(!is_member(chptr, target_p))
		{
			member_p = add_chmember(chptr, target_p, flags);
			dlink_add_alloc(member_p, &joined_members);
		}
	}

	/* we didnt join any members in the sjoin above, so destroy the
	 * channel we just created.  This has to be tested before we call the
	 * hook, as the hook may empty the channel and free it itself.
	 */
	if(dlink_list_length(&chptr->users) == 0 &&
	   dlink_list_length(&chptr->services) == 0)
		free_channel(chptr);
	else
		hook_call(HOOK_JOIN_CHANNEL, chptr, &joined_members);

	DLINK_FOREACH_SAFE(ptr, next_ptr, joined_members.head)
	{
		free_dlink_node(ptr);
	}
}

/* c_join()
 *   the JOIN handler
 */
static void
c_join(struct client *client_p, const char *parv[], int parc)
{
	struct channel *chptr;
	struct chmember *member_p;
	struct chmode newmode;
	dlink_list joined_members;
	dlink_node *ptr;
	dlink_node *next_ptr;
	time_t newts;
	int keep_old_modes = 1;
	int args = 0;

	if(parc < 0 || EmptyString(parv[0]))
		return;

	if(!IsUser(client_p))
		return;

	memset(&joined_members, 0, sizeof(dlink_list));

	/* check for join 0 first */
	if(parc == 1 && parv[0][0] == '0')
	{
		DLINK_FOREACH_SAFE(ptr, next_ptr, client_p->user->channels.head)
		{
			del_chmember(ptr->data);
		}
		return;
	}

	/* a TS6 join */
	if(parc < 3)
	{
		mlog("PROTO: JOIN issued with insufficient parameters");
		return;
	}

	if(!valid_chname(parv[1]))
		return;

	if((chptr = find_channel(parv[1])) == NULL)
	{
		chptr = BlockHeapAlloc(channel_heap);

		strlcpy(chptr->name, parv[1], sizeof(chptr->name));
		newts = chptr->tsinfo = atol(parv[0]);
		add_channel(chptr);

		keep_old_modes = 0;
	}
	else
	{
		newts = atol(parv[0]);

		if(newts == 0 || chptr->tsinfo == 0)
			chptr->tsinfo = 0;
		else if(newts < chptr->tsinfo)
			keep_old_modes = 0;
	}

	newmode.mode = 0;
	newmode.key[0] = '\0';
	newmode.limit = 0;

	args = parse_simple_mode(&newmode, parv, parc, 2, 1);

	/* invalid mode */
	s_assert(args);
	if(!args)
	{
		mlog("PROTO: JOIN issued with invalid modestring: %s",
			rebuild_params(parv, parc, 2));
		return;
	}

	if(!keep_old_modes)
	{
		chptr->tsinfo = newts;
		remove_our_modes(chptr);
		/* Note that JOIN does not remove bans */

		/* services is in there.. rejoin */
		if(sent_burst)
		{
			DLINK_FOREACH(ptr, chptr->services.head)
			{
				rejoin_service(ptr->data, chptr, 1);
			}
		}
	}

	/* this must be done after we've updated the modes */
	if(!keep_old_modes)
		hook_call(HOOK_SJOIN_LOWERTS, chptr, NULL);

	if(!is_member(chptr, client_p))
	{
		member_p = add_chmember(chptr, client_p, 0);
		dlink_add_alloc(member_p, &joined_members);
	}

	hook_call(HOOK_JOIN_CHANNEL, chptr, &joined_members);

	DLINK_FOREACH_SAFE(ptr, next_ptr, joined_members.head)
	{
		free_dlink_node(ptr);
	}

}


