/* src/client.c
 *   Contains code for handling remote clients.
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
 * $Id: client.c 26911 2010-02-22 19:36:09Z leeh $
 */
#include "stdinc.h"
#include "rserv.h"
#include "langs.h"
#include "client.h"
#include "channel.h"
#include "scommand.h"
#include "io.h"
#include "log.h"
#include "service.h"
#include "balloc.h"
#include "event.h"
#include "hook.h"
#include "s_userserv.h"
#include "conf.h"

static dlink_list name_table[MAX_NAME_HASH];
static dlink_list uid_table[MAX_NAME_HASH];
static dlink_list host_table[MAX_HOST_HASH];

dlink_list user_list;
dlink_list oper_list;
dlink_list server_list;
dlink_list exited_list;

static BlockHeap *client_heap;
static BlockHeap *user_heap;
static BlockHeap *server_heap;
static BlockHeap *host_heap;

static void cleanup_host_table(void *);

static void c_kill(struct client *, const char *parv[], int parc);
static void c_nick(struct client *, const char *parv[], int parc);
static void c_uid(struct client *, const char *parv[], int parc);
static void c_quit(struct client *, const char *parv[], int parc);
static void c_server(struct client *, const char *parv[], int parc);
static void c_sid(struct client *, const char *parv[], int parc);
static void c_squit(struct client *, const char *parv[], int parc);

static struct scommand_handler kill_command = { "KILL", c_kill, 0, DLINK_EMPTY };
static struct scommand_handler nick_command = { "NICK", c_nick, 0, DLINK_EMPTY };
static struct scommand_handler uid_command = { "UID", c_uid, 0, DLINK_EMPTY };
static struct scommand_handler quit_command = { "QUIT", c_quit, 0, DLINK_EMPTY };
static struct scommand_handler server_command = { "SERVER", c_server, FLAGS_UNKNOWN, DLINK_EMPTY};
static struct scommand_handler sid_command = { "SID", c_sid, 0, DLINK_EMPTY};
static struct scommand_handler squit_command = { "SQUIT", c_squit, 0, DLINK_EMPTY };

/* init_client()
 *   initialises various things
 */
void
init_client(void)
{
        client_heap = BlockHeapCreate("Client", sizeof(struct client), HEAP_CLIENT);
        user_heap = BlockHeapCreate("User", sizeof(struct user), HEAP_USER);
        server_heap = BlockHeapCreate("Server", sizeof(struct server), HEAP_SERVER);
	host_heap = BlockHeapCreate("Hostname", sizeof(struct host_entry), HEAP_HOST);

	eventAdd("cleanup_host_table", cleanup_host_table, NULL, 3600);

	add_scommand_handler(&kill_command);
	add_scommand_handler(&nick_command);
	add_scommand_handler(&uid_command);
	add_scommand_handler(&quit_command);
	add_scommand_handler(&server_command);
	add_scommand_handler(&sid_command);
	add_scommand_handler(&squit_command);
}

/* hash_name()
 *   hashes a nickname
 *
 * inputs       - nickname to hash
 * outputs      - hash value of nickname
 */
unsigned int
hash_name(const char *p)
{
	unsigned int h = 0;

	while(*p)
	{
		h = (h << 4) - (h + (unsigned char) ToLower(*p++));
	}

	return(h & (MAX_NAME_HASH-1));
}

static unsigned int
hash_host(const char *p)
{
	unsigned int h = 0;

	while(*p)
	{
		h = (h << 4) - (h + (unsigned char) ToLower(*p++));
	}

	return (h & (MAX_HOST_HASH - 1));
}

/* add_client()
 *   adds a client to the hashtable
 *
 * inputs       - client to add
 * outputs      -
 */
void
add_client(struct client *target_p)
{
	unsigned int hashv = hash_name(target_p->name);
	dlink_add(target_p, &target_p->nameptr, &name_table[hashv]);

	if(!EmptyString(target_p->uid))
	{
		hashv = hash_name(target_p->uid);
		dlink_add(target_p, &target_p->uidptr, &uid_table[hashv]);
	}
}

/* del_client()
 *   removes a client from the hashtable
 *
 * inputs       - client to remove
 * outputs      -
 */
void
del_client(struct client *target_p)
{
	unsigned int hashv = hash_name(target_p->name);
	dlink_delete(&target_p->nameptr, &name_table[hashv]);

	if(!EmptyString(target_p->uid))
	{
		hashv = hash_name(target_p->uid);
		dlink_delete(&target_p->uidptr, &uid_table[hashv]);
	}
}

/* find_client()
 *   finds a client [user/server/service] from the hashtable
 *
 * inputs       - name of client to find
 * outputs      - struct of client, or NULL if not found
 */
struct client *
find_client(const char *name)
{
	struct client *target_p;
	dlink_node *ptr;
	unsigned int hashv;

	if(IsDigit(*name))
	{
		target_p = find_uid(name);

		if(target_p != NULL)
			return target_p;
	}

	/* search nicks even if its a uid, as it may be possible for a uid
	 * to be a nick in the future
	 */
	hashv = hash_name(name);

	DLINK_FOREACH(ptr, name_table[hashv].head)
	{
		target_p = ptr->data;

		if(!irccmp(target_p->name, name))
			return target_p;
	}

	return NULL;
}

struct client *
find_named_client(const char *name)
{
	struct client *target_p;
	dlink_node *ptr;
	unsigned int hashv; 

	hashv = hash_name(name);

	DLINK_FOREACH(ptr, name_table[hashv].head)
	{
		target_p = ptr->data;

		if(!irccmp(target_p->name, name))
			return target_p;
	}

	return NULL;
}

struct client *
find_uid(const char *name)
{
	struct client *target_p;
	dlink_node *ptr;
	unsigned int hashv = hash_name(name);

	DLINK_FOREACH(ptr, uid_table[hashv].head)
	{
		target_p = ptr->data;

		if(!irccmp(target_p->uid, name))
			return target_p;
	}

	return NULL;
}

/* find_user()
 *   finds a user from the hashtable
 *
 * inputs       - name of user to find
 * outputs      - struct client of user, or NULL if not found
 */
struct client *
find_user(const char *name, int search_uid)
{
	struct client *target_p;

	if(search_uid)
		target_p = find_client(name);
	else
		target_p = find_named_client(name);

	if(target_p != NULL && IsUser(target_p))
		return target_p;

	return NULL;
}

/* find_server()
 *   finds a server from the hashtable
 *
 * inputs       - name of server to find
 * outputs      - struct client of server, or NULL if not found
 */
struct client *
find_server(const char *name)
{
	struct client *target_p = find_client(name);

	if(target_p != NULL && IsServer(target_p))
		return target_p;

	return NULL;
}

/* find_service()
 *   finds a service from the hashtable
 *
 * inputs       - name of service to find
 * outputs      - struct client of service, or NULL if not found
 */
struct client *
find_service(const char *name)
{
	struct client *target_p = find_client(name);

	if(target_p != NULL && IsService(target_p))
		return target_p;

	return NULL;
}

/* cleanup_host_table()
 *   Walks the hostname hash, cleaning out any entries that have expired
 *
 * inputs	- 
 * outputs	- 
 */
static void
cleanup_host_table(void *unused)
{
	struct host_entry *hent;
	dlink_node *ptr, *next_ptr;
	int i;

	HASH_WALK_SAFE(i, MAX_HOST_HASH, ptr, next_ptr, host_table)
	{
		hent = ptr->data;

		if(hent->flood_expire < CURRENT_TIME &&
		   hent->cregister_expire < CURRENT_TIME &&
		   hent->uregister_expire < CURRENT_TIME)
		{
			dlink_delete(&hent->node, &host_table[i]);
			my_free(hent->name);
			BlockHeapFree(host_heap, hent);
		}
	}
	HASH_WALK_END
}

/* find_host()
 *   finds a host entry from the hashtable, adding it if not found
 *
 * inputs	- name of host to find
 * outputs	- host entry for this host
 */
struct host_entry *
find_host(const char *name)
{
	struct host_entry *hent;
	dlink_node *ptr;
	unsigned int hashv = hash_host(name);

	DLINK_FOREACH(ptr, host_table[hashv].head)
	{
		hent = ptr->data;

		if(!irccmp(hent->name, name))
			return hent;
	}

	hent = BlockHeapAlloc(host_heap);
	hent->name = my_strdup(name);
	dlink_add(hent, &hent->node, &host_table[hashv]);

	return hent;
}

char *
generate_uid(void)
{
	static int done_init = 0;
	static char current_uid[UIDLEN+1];
	int i;

	if(!done_init)
	{
		for(i = 0; i < 3; i++)
			current_uid[i] = config_file.sid[i];

		for(i = 3; i < 9; i++)
			current_uid[i] = 'A';

		current_uid[9] = '\0';

		done_init++;

		return current_uid;
	}
		
	for(i = 8; i > 3; i--)
	{
		if(current_uid[i] == 'Z')
		{
			current_uid[i] = '0';
			return current_uid;
		}
		else if(current_uid[i] != '9')
		{
			current_uid[i]++;
			return current_uid;
		}
		else
			current_uid[i] = 'A';
	}

	/* if this next if() triggers, we're fucked. */
	if(current_uid[3] == 'Z')
	{
		current_uid[i] = 'A';
		s_assert(0);
	}
	else
		current_uid[i]++;

	return current_uid;
}


/* exit_user()
 *   exits a user, removing them from channels and lists
 *
 * inputs       - client to exit
 * outputs      -
 */
static void
exit_user(struct client *target_p)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

	if(IsDead(target_p))
		return;

	SetDead(target_p);

	hook_call(HOOK_USER_EXIT, target_p, NULL);

#ifdef ENABLE_USERSERV
	if(target_p->user->user_reg)
		dlink_find_destroy(target_p, &target_p->user->user_reg->users);
#endif

	if(target_p->user->oper)
	{
		dlink_find_destroy(target_p, &oper_list);
		deallocate_conf_oper(target_p->user->oper);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, target_p->user->channels.head)
	{
		del_chmember(ptr->data);
	}

	dlink_move_node(&target_p->listnode, &user_list, &exited_list);
	dlink_delete(&target_p->upnode, &target_p->uplink->server->users);
}

/* exit_server()
 *   exits a server, removing their dependencies
 *
 * inputs       - client to exit
 * outputs      -
 */
static void
exit_server(struct client *target_p)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

	if(IsDead(target_p))
		return;

	SetDead(target_p);

        /* first exit each of this servers users */
	DLINK_FOREACH_SAFE(ptr, next_ptr, target_p->server->users.head)
	{
		exit_client(ptr->data);
	}

        /* then exit each of their servers.. */
	DLINK_FOREACH_SAFE(ptr, next_ptr, target_p->server->servers.head)
	{
		exit_client(ptr->data);
	}

	/* do this after all its leaf servers have been removed */
	hook_call(HOOK_SERVER_EXIT, target_p, NULL);

	dlink_move_node(&target_p->listnode, &server_list, &exited_list);

	/* if it has an uplink, remove it from its uplinks list */
	if(target_p->uplink != NULL)
		dlink_delete(&target_p->upnode, &target_p->uplink->server->servers);
}

/* exit_client()
 *   exits a generic client, calling functions specific for that client
 *
 * inputs       - client to exit
 * outputs      -
 */
void
exit_client(struct client *target_p)
{
        s_assert(!IsService(target_p));

        if(IsService(target_p))
                return;

	if(IsServer(target_p))
		exit_server(target_p);
	else if(IsUser(target_p))
		exit_user(target_p);

	del_client(target_p);
}

/* free_client()
 *   frees the memory in use by a client
 *
 * inputs       - client to free
 * outputs      -
 */
void
free_client(struct client *target_p)
{
        if(target_p->user != NULL)
	{
		my_free(target_p->user->ip);
		my_free(target_p->user->mask);
                BlockHeapFree(user_heap, target_p->user);
	}

	if(target_p->server != NULL)
	        BlockHeapFree(server_heap, target_p->server);

	BlockHeapFree(client_heap, target_p);
}

/* string_to_umode()
 *   Converts a given string into a usermode
 *
 * inputs       - string to convert, current usermodes
 * outputs      - new usermode
 */
int
string_to_umode(const char *p, int current_umode)
{
	int umode = current_umode;
	int dir = 1;

	while(*p)
	{
		switch(*p)
		{
			case '+':
				dir = 1;
				break;

			case '-':
				dir = 0;
				break;

			case 'a':
				if(dir)
					umode |= CLIENT_ADMIN;
				else
					umode &= ~CLIENT_ADMIN;
				break;

			case 'i':
				if(dir)
					umode |= CLIENT_INVIS;
				else
					umode &= ~CLIENT_INVIS;
				break;

			case 'o':
				if(dir)
					umode |= CLIENT_OPER;
				else
					umode &= ~CLIENT_OPER;
				break;

			default:
				break;
		}

		p++;
	}

	return umode;
}

/* umode_to_string()
 *   converts a usermode into string form
 *
 * inputs       - usermode to convert
 * outputs      - usermode in string form
 */
const char *
umode_to_string(int umode)
{
	static char buf[5];
	char *p;

	p = buf;

	*p++ = '+';

	if(umode & CLIENT_ADMIN)
		*p++ = 'a';
	if(umode & CLIENT_INVIS)
		*p++ = 'i';
	if(umode & CLIENT_OPER)
		*p++ = 'o';

	*p = '\0';
	return buf;
}

/* c_nick()
 *   the UID handler
 */
void
c_nick(struct client *client_p, const char *parv[], int parc)
{
	static char buf[BUFSIZE];
	struct client *target_p;
	struct client *uplink_p;
	time_t newts;

        s_assert((parc == 2) || (parc == 8));

        if(parc != 8 && parc != 2)
                return;

        /* new client being introduced */
	if(parc == 8)
	{
		target_p = find_named_client(parv[0]);
		newts = atol(parv[2]);

		if((uplink_p = find_server(parv[6])) == NULL)
		{
			mlog("PROTO: NICK %s introduced on non-existant server %s",
				parv[0], parv[6]);
			return;
		}

		if(strlen(parv[0]) > NICKLEN)
			die(1, "Compiled NICKLEN appears to be wrong (nick %s (%u > %d).  Read INSTALL.",
				parv[0], (unsigned int) strlen(parv[0]), NICKLEN);

                /* something already exists with this nick */
		if(target_p != NULL)
		{
                        s_assert(!IsServer(target_p));

                        if(IsServer(target_p))
                                return;

			if(IsUser(target_p))
			{
                                /* our uplink shouldve dealt with this. */
				if(target_p->user->tsinfo < newts)
				{
					mlog("PROTO: NICK %s with higher TS introduced causing collision.",
					     target_p->name);
					return;
				}

				/* normal nick collision.. exit old */
				exit_client(target_p);
			}
			else if(IsService(target_p))
			{
				/* ugh. anything with a ts this low is
				 * either someone fucking about, or another
				 * service.  we go byebye.
				 */
				if(newts <= 1)
					die(1, "service fight");

				return;
			}
		}

		target_p = BlockHeapAlloc(client_heap);
		target_p->user = BlockHeapAlloc(user_heap);

		target_p->uplink = uplink_p;

		strlcpy(target_p->name, parv[0], sizeof(target_p->name));
		strlcpy(target_p->user->username, parv[4],
			sizeof(target_p->user->username));
		strlcpy(target_p->user->host, parv[5], 
                        sizeof(target_p->user->host));
                strlcpy(target_p->info, parv[7], sizeof(target_p->info));

		target_p->user->servername = uplink_p->name;
		target_p->user->tsinfo = newts;
		target_p->user->umode = string_to_umode(parv[3], 0);

		snprintf(buf, sizeof(buf), "%s!%s@%s",
			target_p->name, target_p->user->username, 
			target_p->user->host);
		target_p->user->mask = my_strdup(buf);

		add_client(target_p);
		dlink_add(target_p, &target_p->listnode, &user_list);
		dlink_add(target_p, &target_p->upnode, &uplink_p->server->users);

		if(IsEOB(uplink_p))
			hook_call(HOOK_NEW_CLIENT, target_p, NULL);
		else
			hook_call(HOOK_NEW_CLIENT_BURST, target_p, NULL);
	}

        /* client changing nicks */
	else if(parc == 2)
	{
		s_assert(IsUser(client_p));

                if(!IsUser(client_p))
                        return;

		if(strlen(parv[0]) > NICKLEN)
			die(1, "Compiled NICKLEN appears to be wrong (nick %s (%u > %d).  Read INSTALL.",
				parv[0], (unsigned int) strlen(parv[0]), NICKLEN);

		del_client(client_p);
		strlcpy(client_p->name, parv[0], sizeof(client_p->name));
		add_client(client_p);

		/* need to update their mask with new nick */
		snprintf(buf, sizeof(buf), "%s!%s@%s",
			client_p->name, client_p->user->username, 
			client_p->user->host);
		my_free(client_p->user->mask);
		client_p->user->mask = my_strdup(buf);

		client_p->user->tsinfo = atol(parv[1]);

		hook_call(HOOK_NICKCHANGE, client_p, NULL);
	}
}

/* c_uid()
 *   the NICK handler
 */
void
c_uid(struct client *client_p, const char *parv[], int parc)
{
	static char buf[BUFSIZE];
	struct client *target_p;
	time_t newts;

        s_assert(parc == 9);

        if(parc != 9)
                return;

	target_p = find_client(parv[0]);
	/* XXX UID */
	newts = atol(parv[2]);

	if(strlen(parv[0]) > NICKLEN)
		die(1, "Compiled NICKLEN appears to be wrong (nick %s (%u > %d).  Read INSTALL.",
			parv[0], (unsigned int) strlen(parv[0]), NICKLEN);

	/* something already exists with this nick */
	if(target_p != NULL)
	{
		s_assert(!IsServer(target_p));

		if(IsServer(target_p))
			return;

		if(IsUser(target_p))
		{
			/* our uplink shouldve dealt with this. */
			if(target_p->user->tsinfo < newts)
			{
				mlog("PROTO: NICK %s with higher TS introduced causing collision.",
					target_p->name);
				return;
			}

			/* normal nick collision.. exit old */
			exit_client(target_p);
		}
		else if(IsService(target_p))
		{
			/* ugh. anything with a ts this low is
			 * either someone fucking about, or another
			 * service.  we go byebye.
			 */
			if(newts <= 1)
				die(1, "service fight");

			return;
		}
	}

	target_p = BlockHeapAlloc(client_heap);
	target_p->user = BlockHeapAlloc(user_heap);

	target_p->uplink = client_p;

	strlcpy(target_p->name, parv[0], sizeof(target_p->name));
	strlcpy(target_p->user->username, parv[4],
		sizeof(target_p->user->username));
	strlcpy(target_p->user->host, parv[5], 
		sizeof(target_p->user->host));

	if(parv[6][0] != '0' && parv[6][1] != '\0')
		target_p->user->ip = my_strdup(parv[6]);

	strlcpy(target_p->uid, parv[7], sizeof(target_p->uid));
	strlcpy(target_p->info, parv[8], sizeof(target_p->info));

	target_p->user->servername = client_p->name;
	target_p->user->tsinfo = newts;
	target_p->user->umode = string_to_umode(parv[3], 0);

	snprintf(buf, sizeof(buf), "%s!%s@%s",
		target_p->name, target_p->user->username, 
		target_p->user->host);
	target_p->user->mask = my_strdup(buf);

	add_client(target_p);
	dlink_add(target_p, &target_p->listnode, &user_list);
	dlink_add(target_p, &target_p->upnode, &client_p->server->users);

	if(IsEOB(client_p))
		hook_call(HOOK_NEW_CLIENT, target_p, NULL);
	else
		hook_call(HOOK_NEW_CLIENT_BURST, target_p, NULL);
}

/* c_quit()
 *   the QUIT handler
 */
void
c_quit(struct client *client_p, const char *parv[], int parc)
{
	if(!IsUser(client_p))
	{
		mlog("PROTO: QUIT received from server %s", client_p->name);
		return;
	}

	exit_client(client_p);
}

/* c_kill()
 *   the KILL handler
 */
void
c_kill(struct client *client_p, const char *parv[], int parc)
{
	static time_t first_kill = 0;
	static int num_kill = 0;
	struct client *target_p;

	if(parc < 1 || EmptyString(parv[0]))
		return;

	if((target_p = find_client(parv[0])) == NULL)
		return;

	if(IsServer(target_p))
	{
		mlog("PROTO: KILL received for server %s", target_p->name);
		return;
	}

	/* grmbl. */
	if(IsService(target_p))
	{
		if(IsUser(client_p))
			mlog("service %s killed by %s!%s@%s{%s}",
				target_p->name, client_p->name, 
				client_p->user->username, client_p->user->host,
				client_p->user->servername);
		else
			mlog("service %s killed by %s",
				target_p->name, client_p->name);

		/* no kill in the last 20 seconds, reset. */
		if((first_kill + 20) < CURRENT_TIME)
		{
			first_kill = CURRENT_TIME;
			num_kill = 1;
		}
                /* 20 kills in 20 seconds.. service fight. */
		else if(num_kill > 20)
			die(1, "service kill fight!");

		num_kill++;

		/* has to be done because introduce_service() calls
		 * add_client()
		 */
		del_client(target_p);
		introduce_service(target_p);
		introduce_service_channels(target_p, 0);

		return;
	}

        /* its a user, just exit them */
	exit_client(target_p);
}

/* c_server()
 *   the SERVER handler
 */
void
c_server(struct client *client_p, const char *parv[], int parc)
{
	struct client *target_p;
	static char default_gecos[] = "(Unknown Location)";

	if(parc < 3)
		return;

	if(strlen(parv[0]) > HOSTLEN)
	{
		die(1, "Compiled HOSTLEN appears to be wrong, received %s (%u > %d)",
			parv[0], (unsigned int) strlen(parv[0]), HOSTLEN);
	}

        /* our uplink introducing themselves */
        if(client_p == NULL)
        {
		if(!ConnTS(server_p))
		{
			mlog("Connection to server %s failed: "
				"(Protocol mismatch)",
				server_p->name);
			(server_p->io_close)(server_p);
			return;
		}

                if(irccmp(server_p->name, parv[0]))
                {
                        mlog("Connection to server %s failed: "
                             "(Servername mismatch)",
                             server_p->name);
                        (server_p->io_close)(server_p);
                        return;
                }

                ClearConnHandshake(server_p);
                server_p->first_time = CURRENT_TIME;
        }

	target_p = BlockHeapAlloc(client_heap);
	target_p->server = BlockHeapAlloc(server_heap);

	strlcpy(target_p->name, parv[0], sizeof(target_p->name));
	strlcpy(target_p->info, EmptyString(parv[2]) ? default_gecos : parv[2],
		sizeof(target_p->info));

	/* local TS6 servers use SERVER and pass the SID on the PASS command */
	if(!EmptyString(server_p->sid) && client_p == NULL)
		strlcpy(target_p->uid, server_p->sid, sizeof(target_p->uid));

	target_p->server->hops = atoi(parv[1]);

	/* this server has an uplink */
	if(client_p != NULL)
	{
		target_p->uplink = client_p;
		dlink_add(target_p, &target_p->upnode, 
                          &client_p->server->servers);
	}
	/* its connected to us */
	else
	{
		server_p->client_p = target_p;

		/* got RSFNC in capab line, set it in struct client */
		if(ConnCapRSFNC(server_p))
			target_p->flags |= FLAGS_RSFNC;
	}

	add_client(target_p);
	dlink_add(target_p, &target_p->listnode, &server_list);

	if(client_p == NULL)
	{
		/* has to be done before the burst as chanserv will deop clients */
		introduce_services();
		introduce_services_channels();
	}

	SetConnSentBurst(server_p);

	sendto_server(":%s PING %s %s", MYUID, target_p->name, UID(target_p));
}

/* c_sid()
 *   the SID handler
 */
void
c_sid(struct client *client_p, const char *parv[], int parc)
{
	struct client *target_p;
	static char default_gecos[] = "(Unknown Location)";

	s_assert(parc == 4);
	if(parc < 4)
		return;

	if(strlen(parv[0]) > HOSTLEN)
	{
		die(1, "Compiled HOSTLEN appears to be wrong, received %s (%u > %d)",
			parv[0], (unsigned int) strlen(parv[0]), HOSTLEN);
	}

	target_p = BlockHeapAlloc(client_heap);
	target_p->server = BlockHeapAlloc(server_heap);

	strlcpy(target_p->name, parv[0], sizeof(target_p->name));
	strlcpy(target_p->uid, parv[2], sizeof(target_p->uid));
	strlcpy(target_p->info, EmptyString(parv[3]) ? default_gecos : parv[3],
		sizeof(target_p->info));

	target_p->server->hops = atoi(parv[1]);

	target_p->uplink = client_p;
	dlink_add(target_p, &target_p->upnode, &client_p->server->servers);

	add_client(target_p);
	dlink_add(target_p, &target_p->listnode, &server_list);

	sendto_server(":%s PING %s %s", MYUID, target_p->name, UID(target_p));
}

/* c_squit()
 *   the SQUIT handler
 */
void
c_squit(struct client *client_p, const char *parv[], int parc)
{
	struct client *target_p;

	/* malformed squit, byebye. */
	if(parc < 1 || EmptyString(parv[0]))
	{
		exit_client(server_p->client_p);
		return;
	}

	target_p = find_server(parv[0]);

	if(target_p == NULL)
	{
		/* returns -1 if it handled it */
		if(hook_call(HOOK_SQUIT_UNKNOWN, (void *) parv[0], NULL) == 0)
			mlog("PROTO: SQUIT for unknown server %s", parv[0]);

		return;
	}

	exit_client(target_p);
}
