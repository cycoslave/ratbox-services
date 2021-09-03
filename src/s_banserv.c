/* src/s_banserv.c
 *   Contains the code for the ban (kline etc) service
 *
 * Copyright (C) 2005-2008 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2005-2008 ircd-ratbox development team
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
 * $Id: s_banserv.c 26676 2009-09-26 15:27:16Z leeh $
 */
#include "stdinc.h"

#ifdef ENABLE_BANSERV
#ifdef PCRE_BUILD
#include "pcre.h"
#else
#include <pcre.h>
#endif

#include "rsdb.h"
#include "rserv.h"
#include "langs.h"
#include "io.h"
#include "service.h"
#include "client.h"
#include "channel.h"
#include "c_init.h"
#include "conf.h"
#include "ucommand.h"
#include "log.h"
#include "event.h"
#include "watch.h"
#include "hook.h"
#include "s_banserv.h"

static void init_s_banserv(void);

static struct client *banserv_p;

static int o_banserv_kline(struct client *, struct lconn *, const char **, int);
static int o_banserv_xline(struct client *, struct lconn *, const char **, int);
static int o_banserv_resv(struct client *, struct lconn *, const char **, int);
static int o_banserv_addregexp(struct client *, struct lconn *, const char **, int);
static int o_banserv_addregexpneg(struct client *, struct lconn *, const char **, int);
static int o_banserv_unkline(struct client *, struct lconn *, const char **, int);
static int o_banserv_unxline(struct client *, struct lconn *, const char **, int);
static int o_banserv_unresv(struct client *, struct lconn *, const char **, int);
static int o_banserv_delregexp(struct client *, struct lconn *, const char **, int);
static int o_banserv_delregexpneg(struct client *, struct lconn *, const char **, int);
static int o_banserv_sync(struct client *, struct lconn *, const char **, int);
static int o_banserv_findkline(struct client *, struct lconn *, const char **, int);
static int o_banserv_findxline(struct client *, struct lconn *, const char **, int);
static int o_banserv_findresv(struct client *, struct lconn *, const char **, int);
static int o_banserv_listregexps(struct client *, struct lconn *, const char **, int);

static struct service_command banserv_command[] =
{
	{ "KLINE",		&o_banserv_kline,	2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_KLINE 	},
	{ "XLINE",		&o_banserv_xline,	2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_XLINE 	},
	{ "RESV",		&o_banserv_resv,	2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_RESV 	},
	{ "ADDREGEXP",		&o_banserv_addregexp,	2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_REGEXP	},
	{ "ADDREGEXPNEG",	&o_banserv_addregexpneg,2, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_REGEXP	},
	{ "UNKLINE",		&o_banserv_unkline,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_KLINE 	},
	{ "UNXLINE",		&o_banserv_unxline,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_XLINE 	},
	{ "UNRESV",		&o_banserv_unresv,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_RESV 	},
	{ "DELREGEXP",		&o_banserv_delregexp,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_REGEXP	},
	{ "DELREGEXPNEG",	&o_banserv_delregexpneg,1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_REGEXP	},
	{ "SYNC",		&o_banserv_sync,	1, NULL, 1, 0L, 0, 0, 0 			},
	{ "FINDKLINE",		&o_banserv_findkline,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_KLINE 	},
	{ "FINDXLINE",		&o_banserv_findxline,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_XLINE 	},
	{ "FINDRESV",		&o_banserv_findresv,	1, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_RESV 	},
	{ "LISTREGEXPS",	&o_banserv_listregexps,	0, NULL, 1, 0L, 0, 0, CONF_OPER_BAN_REGEXP	}
};

static struct ucommand_handler banserv_ucommand[] =
{
	{ "kline",		o_banserv_kline,	0, CONF_OPER_BAN_KLINE, 2, NULL },
	{ "xline",		o_banserv_xline,	0, CONF_OPER_BAN_XLINE, 2, NULL },
	{ "resv",		o_banserv_resv,		0, CONF_OPER_BAN_RESV, 2, NULL },
	{ "addregexp",		o_banserv_addregexp,	0, CONF_OPER_BAN_REGEXP, 2, NULL },
	{ "addregexpneg",	o_banserv_addregexpneg,	0, CONF_OPER_BAN_REGEXP, 2, NULL },
	{ "unkline",		o_banserv_unkline,	0, CONF_OPER_BAN_KLINE, 1, NULL },
	{ "unxline",		o_banserv_unxline,	0, CONF_OPER_BAN_XLINE, 1, NULL },
	{ "unresv",		o_banserv_unresv,	0, CONF_OPER_BAN_RESV, 1, NULL },
	{ "delregexp",		o_banserv_delregexp,	0, CONF_OPER_BAN_REGEXP, 1, NULL },
	{ "delregexpneg",	o_banserv_delregexpneg,	0, CONF_OPER_BAN_REGEXP, 1, NULL },
	{ "findkline",		o_banserv_findkline,	0, CONF_OPER_BAN_KLINE, 1, NULL },
	{ "findxline",		o_banserv_findxline,	0, CONF_OPER_BAN_XLINE, 1, NULL },
	{ "findresv",		o_banserv_findresv,	0, CONF_OPER_BAN_RESV, 1, NULL },
	{ "sync",		o_banserv_sync,		0, 0, 1, NULL },
	{ "listregexps",	o_banserv_listregexps,	0, CONF_OPER_BAN_REGEXP, 0, NULL	},
	{ "\0", NULL, 0, 0, 0, NULL }
};

static struct service_handler banserv_service = {
	"BANSERV", "BANSERV", "banserv", "services.int",
	"Global Ban Service", 0, 0, 
	banserv_command, sizeof(banserv_command), banserv_ucommand, init_s_banserv, NULL
};

dlink_list regexp_list;

static int regexp_callback(int argc, const char **argv);
static int regexp_neg_callback(int argc, const char **argv);

static void e_banserv_expire(void *unused);
static void e_banserv_autosync(void *unused);

static int h_banserv_new_client(void *_client_p, void *unused);

static void expire_operbans(void);

static void push_unban(const char *target, char type, const char *mask);
static void sync_bans(const char *target, char banletter);

static void regexp_free(struct regexp_ban *regexp_p, int neg);

void
preinit_s_banserv(void)
{
	banserv_p = add_service(&banserv_service);
}

static void
init_s_banserv(void)
{
	/* merge this service up into operserv if needed */
	if(config_file.bs_merge_into_operserv)
	{
		struct client *service_p;

		if((service_p = merge_service(&banserv_service, "OPERSERV", 1)) != NULL)
		{
			dlink_delete(&banserv_p->listnode, &service_list);
			banserv_p = service_p;
		}
	}

		
	eventAdd("banserv_expire", e_banserv_expire, NULL, 900);
	eventAdd("banserv_autosync", e_banserv_autosync, NULL,
			DEFAULT_AUTOSYNC_FREQUENCY);

	hook_add(h_banserv_new_client, HOOK_NEW_CLIENT);
	hook_add(h_banserv_new_client, HOOK_NEW_CLIENT_BURST);

	rsdb_exec(regexp_callback, "SELECT id, regex, reason, hold, create_time, oper FROM operbans_regexp");
	rsdb_exec(regexp_neg_callback, "SELECT id, parent_id, regex, oper FROM operbans_regexp_neg");
}

static int
regexp_callback(int argc, const char **argv)
{
	struct regexp_ban *regexp_p;
	pcre *regexp_comp;
	const char *re_error;
	int re_error_offset;

	if(EmptyString(argv[1]) || EmptyString(argv[2]) || EmptyString(argv[5]))
		return 0;

	regexp_comp = pcre_compile(argv[1], 0, &re_error, &re_error_offset, NULL);

	if(regexp_comp == NULL)
		return 0;

	regexp_p = my_malloc(sizeof(struct regexp_ban));
	regexp_p->id = atoi(argv[0]);
	regexp_p->regexp_str = my_strdup(argv[1]);
	regexp_p->reason = my_strdup(argv[2]);
	regexp_p->hold = atol(argv[3]);
	regexp_p->create_time = atol(argv[4]);
	regexp_p->oper = my_strdup(argv[5]);
	regexp_p->regexp = regexp_comp;

	dlink_add_tail(regexp_p, &regexp_p->ptr, &regexp_list);
	return 0;
}

static int
regexp_neg_callback(int argc, const char **argv)
{
	struct regexp_ban *regexp_p;
	struct regexp_ban *parent_p;
	pcre *regexp_comp;
	const char *re_error;
	int re_error_offset;
	unsigned int parent_id;
	dlink_node *ptr;

	if(EmptyString(argv[1]) || EmptyString(argv[2]) || EmptyString(argv[3]))
		return 0;

	if((parent_id = atoi(argv[1])) < 1)
		return 0;

	/* find its parent regexp */
	DLINK_FOREACH(ptr, regexp_list.head)
	{
		parent_p = ptr->data;

		if(parent_p->id == parent_id)
			break;
	}

	if(ptr == NULL)
		return 0;

	regexp_comp = pcre_compile(argv[2], 0, &re_error, &re_error_offset, NULL);

	if(regexp_comp == NULL)
		return 0;

	regexp_p = my_malloc(sizeof(struct regexp_ban));
	regexp_p->id = atoi(argv[0]);
	regexp_p->regexp_str = my_strdup(argv[2]);
	regexp_p->oper = my_strdup(argv[3]);
	regexp_p->parent = parent_p;

	dlink_add_tail(regexp_p, &regexp_p->ptr, &parent_p->negations);
	return 0;
}

static void
e_banserv_autosync(void *unused)
{
	sync_bans("*", 0);
}

static void
expire_operbans(void)
{
	/* these bans are temp, so they will expire automatically on 
	 * servers
	 */
	rsdb_exec(NULL, "DELETE FROM operbans WHERE hold != '0' AND hold <= '%lu'",
		CURRENT_TIME);
}

static void
e_banserv_expire(void *unused)
{
	struct regexp_ban *regexp_p;
	dlink_node *ptr;
	dlink_node *next_ptr;

	expire_operbans();

	DLINK_FOREACH_SAFE(ptr, next_ptr, regexp_list.head)
	{
		regexp_p = ptr->data;

		if(regexp_p->hold && regexp_p->hold <= CURRENT_TIME)
			regexp_free(regexp_p, 0);
	}
}

static int
h_banserv_new_client(void *_target_p, void *unused)
{
	char buf[BUFSIZE];
	int ovector[30];
	struct regexp_ban *regexp_p;
	struct regexp_ban *neg_p;
	struct client *target_p = _target_p;
	int buflen;
	dlink_node *ptr;
	dlink_node *next_ptr;
	dlink_node *neg_ptr;

	buflen = snprintf(buf, sizeof(buf), "%s#%s",
			target_p->user->mask, target_p->info);

	DLINK_FOREACH_SAFE(ptr, next_ptr, regexp_list.head)
	{
		regexp_p = ptr->data;

		/* regexp has expired */
		if(regexp_p->hold && regexp_p->hold <= CURRENT_TIME)
		{
			regexp_free(regexp_p, 0);
			continue;
		}

		if(pcre_exec(regexp_p->regexp, NULL, buf, buflen, 0, 0, ovector, 30) >= 0)
		{
			DLINK_FOREACH(neg_ptr, regexp_p->negations.head)
			{
				neg_p = neg_ptr->data;

				/* matches a negation, return */
				if(pcre_exec(regexp_p->regexp, NULL, buf, buflen, 0, 0, ovector, 30) >= 0)
					return 0;
			}

			/* no negations matched */
			sendto_server(":%s ENCAP %s KLINE %u * %s :%s",
					SVC_UID(banserv_p), target_p->user->servername,
					config_file.bs_regexp_time,
					target_p->user->host, regexp_p->reason);
			return 0;
		}
	}

	return 0;
}


static int
split_ban(const char *mask, char **user, char **host)
{
	static char buf[BUFSIZE];
	char *p;

	strlcpy(buf, mask, sizeof(buf));

	if((p = strchr(buf, '@')) == NULL)
		return 0;

	*p++ = '\0';

	if(EmptyString(buf) || EmptyString(p))
		return 0;

	if(strlen(buf) > USERLEN || strlen(p) > HOSTLEN)
		return 0;

	if(user)
		*user = buf;

	if(host)
		*host = p;

	return 1;
}

static int
find_ban(const char *mask, char type)
{
	struct rsdb_table data;
	int retval;

	/* The case of "ban exists but has expired" can be a bit of a pain to deal 
	 * with, so shortcut it by simply expiring all bans prior to checking.
	 *
	 * This function shouldn't be used enough for it to be a problem --anfl
	 */
	expire_operbans();

	rsdb_exec_fetch(&data, "SELECT remove FROM operbans WHERE type='%c' AND mask=LOWER('%Q') LIMIT 1",
			type, mask);

	if(data.row_count)
	{
		/* pgsql stores booleans as t/f */
		if(atoi(data.row[0][0]) == 1 || (data.row[0][0] && data.row[0][0][0] == 't'))
			retval = -1;
		else
			retval = 1;
	}
	else
		retval = 0;

	rsdb_exec_fetch_end(&data);

	return retval;
}

/* find_ban_remove()
 * Finds bans in the database suitable for removing.
 * 
 * inputs	- mask, type (K/X/R)
 * outputs	- oper who set the ban, NULL if none found
 * side effects	-
 */
static const char *
find_ban_remove(const char *mask, char type)
{
	static char buf[BUFSIZE];
	struct rsdb_table data;
	const char *retval;

	/* The case of "ban exists but has expired" can be a bit of a pain to deal 
	 * with, so shortcut it by simply expiring all bans prior to checking.
	 *
	 * This function shouldn't be used enough for it to be a problem --anfl
	 */
	expire_operbans();

	rsdb_exec_fetch(&data, "SELECT remove, oper FROM operbans WHERE type='%c' AND mask=LOWER('%Q') LIMIT 1",
			type, mask);

	if(data.row_count && (atoi(data.row[0][0]) == 0 || (data.row[0][0] && data.row[0][0][0] == 'f')))
	{
		strlcpy(buf, data.row[0][1], sizeof(buf));
		retval = buf;
	}
	else
		retval = NULL;

	rsdb_exec_fetch_end(&data);

	return retval;
}

static void
regexp_free(struct regexp_ban *regexp_p, int neg)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

	if(!neg)
	{
		rsdb_exec(NULL, "DELETE FROM operbans_regexp_neg WHERE parent_id='%u'",
				regexp_p->id);
		rsdb_exec(NULL, "DELETE FROM operbans_regexp WHERE id='%u'",
				regexp_p->id);
	}

	/* this will be empty for negations themselves */
	DLINK_FOREACH_SAFE(ptr, next_ptr, regexp_p->negations.head)
	{
		regexp_free(ptr->data, 1);
	}

	pcre_free(regexp_p->regexp);

	if(neg)
		dlink_delete(&regexp_p->ptr, &regexp_p->parent->negations);
	else
		dlink_delete(&regexp_p->ptr, &regexp_list);

	my_free(regexp_p->regexp_str);
	my_free(regexp_p->reason);
	my_free(regexp_p->oper);
	my_free(regexp_p);
}

static void
push_ban(const char *target, char type, const char *mask, 
		const char *reason, time_t hold)
{
	/* when ircd receives a temp ban it already has banned, it just
	 * ignores the request and doesnt update the expiry.  This can cause
	 * problems with the old max temp time of 4 weeks.  This work
	 * around issues an unban first, allowing the new ban to be set,
	 * which just delays the expiry somewhat.
	 *
	 * We only do this for temporary bans. --anfl
	 */
	if(config_file.bs_temp_workaround && hold)
		push_unban(target, type, mask);

	if(type == 'K')
	{
		char *user, *host;

		if(!split_ban(mask, &user, &host))
			return;

		sendto_server(":%s ENCAP %s KLINE %lu %s %s :%s",
				SVC_UID(banserv_p), target,
				(unsigned long) hold,
				user, host, reason);
	}
	else if(type == 'X')
		sendto_server(":%s ENCAP %s XLINE %lu %s 2 :%s",
				SVC_UID(banserv_p), target,
				(unsigned long) hold, mask, reason);
	else if(type == 'R')
		sendto_server(":%s ENCAP %s RESV %lu %s 0 :%s",
				SVC_UID(banserv_p), target,
				(unsigned long) hold, mask, reason);
}

static void
push_unban(const char *target, char type, const char *mask)
{
	if(type == 'K')
	{
		char *user, *host;

		if(!split_ban(mask, &user, &host))
			return;

		sendto_server(":%s ENCAP %s UNKLINE %s %s",
				SVC_UID(banserv_p), target, user, host);
	}
	else if(type == 'X')
		sendto_server(":%s ENCAP %s UNXLINE %s",
				SVC_UID(banserv_p), target, mask);
	else if(type == 'R')
		sendto_server(":%s ENCAP %s UNRESV %s",
				SVC_UID(banserv_p), target, mask);
}

static void
sync_bans(const char *target, char banletter)
{
	struct rsdb_table data;
	int i;

	/* first is temporary bans */
	if(banletter)
		rsdb_exec_fetch(&data, "SELECT type, mask, reason, hold FROM operbans "
					"WHERE hold > '%lu' AND remove='0' AND type='%c'",
				CURRENT_TIME, banletter);
	else
		rsdb_exec_fetch(&data, "SELECT type, mask, reason, hold FROM operbans "
					"WHERE hold > '%lu' AND remove='0'",
				CURRENT_TIME);

	for(i = 0; i < data.row_count; i++)
	{
		push_ban(target, data.row[i][0][0], data.row[i][1], data.row[i][2],
			(unsigned long) (atol(data.row[i][3]) - CURRENT_TIME));
	}

	rsdb_exec_fetch_end(&data);

	/* permanent bans */
	if(banletter)
		rsdb_exec_fetch(&data, "SELECT type, mask, reason, hold FROM operbans "
					"WHERE hold='0' AND remove='0' AND type='%c'",
				CURRENT_TIME, banletter);
	else
		rsdb_exec_fetch(&data, "SELECT type, mask, reason, hold FROM operbans "
					"WHERE hold='0' AND remove='0'",
				CURRENT_TIME);

	for(i = 0; i < data.row_count; i++)
	{
		push_ban(target, data.row[i][0][0], data.row[i][1], data.row[i][2], 0);
	}

	rsdb_exec_fetch_end(&data);

	/* bans to remove */
	if(banletter)
		rsdb_exec_fetch(&data, "SELECT type, mask FROM operbans "
					"WHERE hold > '%lu' AND remove='1' AND type='%c'",
				CURRENT_TIME, banletter);
	else
		rsdb_exec_fetch(&data, "SELECT type, mask FROM operbans "
					"WHERE hold > '%lu' AND remove='1'",
				CURRENT_TIME);

	for(i = 0; i < data.row_count; i++)
	{
		push_unban(target, data.row[i][0][0], data.row[i][1]);
	}

	rsdb_exec_fetch_end(&data);
}

static int
regexp_match(pcre *regexp, int kline, const char *kline_reason)
{
	char buf[BUFSIZE];
	int ovector[30];
	struct client *target_p;
	unsigned int matches = 0;
	int buflen;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, user_list.head)
	{
		target_p = ptr->data;

		buflen = snprintf(buf, sizeof(buf), "%s#%s", 
				target_p->user->mask, target_p->info);

		if(pcre_exec(regexp, NULL, buf, buflen, 0, 0, ovector, 30) >= 0)
		{
			matches++;

			if(kline)
			{
				sendto_server(":%s ENCAP %s KLINE %u * %s :%s",
						SVC_UID(banserv_p), target_p->user->servername,
						config_file.bs_regexp_time,
						target_p->user->host, kline_reason);
			}
		}	
	}

	return matches;
}

static int
o_banserv_kline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *mask;
	char *username;
	char *host;
	char *reason;
	time_t temptime = 0;
	int para = 0;
	int res;

	if((temptime = get_temp_time(parv[para])))
		para++;

	res = find_ban(parv[para], 'K');

	if(res == 1)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_ALREADYPLACED,
				"KLINE", parv[para]);
		return 0;
	}

	if(!temptime)
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_PERM))
				hit++;
		}
		else if(!(conn_p->sprivs & CONF_OPER_BAN_PERM))
			hit++;

		if(hit)
		{
			service_snd(banserv_p, client_p, conn_p, SVC_BAN_NOPERMACCESS,
					"KLINE");
			return 0;
		}
	}

	mask = parv[para++];

	if(!split_ban(mask, &username, &host))
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_INVALID,
				"KLINE", mask);
		return 0;
	}

	reason = rebuild_params(parv, parc, para);

	if(EmptyString(reason))
	{
		service_snd(banserv_p, client_p, conn_p, SVC_NEEDMOREPARAMS,
				banserv_p->name, "KLINE");
		return 0;
	}

	if(strlen(reason) > REASONLEN)
		reason[REASONLEN] = '\0';

	if(config_file.bs_max_kline_matches &&
		!((client_p && client_p->user->oper->sflags & CONF_OPER_BAN_NOMAX) || (conn_p && conn_p->sprivs & CONF_OPER_BAN_NOMAX)))
	{
		struct client *target_p;
		unsigned int matches = 0;
		dlink_node *ptr;


		DLINK_FOREACH(ptr, user_list.head)
		{
			target_p = ptr->data;

			if(match(username, target_p->user->username) && match(host, target_p->user->host))
				matches++;
		}

		if(matches > config_file.bs_max_kline_matches)
		{
			service_snd(banserv_p, client_p, conn_p, SVC_BAN_TOOMANYMATCHES,
					username, "@", host, matches, config_file.bs_max_kline_matches);
			return 0;
		}
	}

	if(res)
		rsdb_exec(NULL, "UPDATE operbans SET reason='%Q', "
				"hold='%ld', oper='%Q', remove='0' WHERE "
				"type='K' AND mask=LOWER('%Q')",
				reason,
				temptime ? CURRENT_TIME + temptime : 0,
				OPER_NAME(client_p, conn_p), mask);
	else
		rsdb_exec(NULL, "INSERT INTO operbans "
				"(type, mask, reason, hold, create_time, "
				"oper, remove, flags) "
				"VALUES('K', LOWER('%Q'), '%Q', '%lu', '%lu', '%Q', '0', '0')",
				mask, reason,
				temptime ? CURRENT_TIME + temptime : 0,
				CURRENT_TIME, OPER_NAME(client_p, conn_p));
			
	service_snd(banserv_p, client_p, conn_p, SVC_BAN_ISSUED,
			"KLINE", mask);

	push_ban("*", 'K', mask, reason, temptime);

	zlog(banserv_p, 1, WATCH_BANSERV, 1, client_p, conn_p,
		"KLINE %s %s %s",
		temptime ? get_short_duration(temptime) : "perm",
		mask, reason);

	return 0;
}

static int
o_banserv_xline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *gecos;
	char *reason;
	time_t temptime = 0;
	int para = 0;
	int res;

	if((temptime = get_temp_time(parv[para])))
		para++;

	res = find_ban(parv[para], 'X');

	if(res == 1)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_ALREADYPLACED,
				"XLINE", parv[para]);
		return 0;
	}

	if(!temptime)
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_PERM))
				hit++;
		}
		else if(!(conn_p->sprivs & CONF_OPER_BAN_PERM))
			hit++;

		if(hit)
		{
			service_snd(banserv_p, client_p, conn_p, SVC_BAN_NOPERMACCESS, "XLINE");
			return 0;
		}
	}

	gecos = parv[para++];

	if(strlen(gecos) > NICKUSERHOSTLEN)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_INVALID,
				"XLINE", gecos);
		return 0;
	}

	reason = rebuild_params(parv, parc, para);

	if(EmptyString(reason))
	{
		service_snd(banserv_p, client_p, conn_p, SVC_NEEDMOREPARAMS,
				banserv_p->name, "XLINE");
		return 0;
	}

	if(strlen(reason) > REASONLEN)
		reason[REASONLEN] = '\0';

	if(config_file.bs_max_xline_matches &&
		!((client_p && client_p->user->oper->sflags & CONF_OPER_BAN_NOMAX) || (conn_p && conn_p->sprivs & CONF_OPER_BAN_NOMAX)))
	{
		struct client *target_p;
		unsigned int matches = 0;
		dlink_node *ptr;

		DLINK_FOREACH(ptr, user_list.head)
		{
			target_p = ptr->data;

			if(match(gecos, target_p->info))
				matches++;
		}

		if(matches > config_file.bs_max_xline_matches)
		{
			service_snd(banserv_p, client_p, conn_p, SVC_BAN_TOOMANYMATCHES,
					gecos, "", "", matches, config_file.bs_max_xline_matches);
			return 0;
		}
	}

	if(res)
		rsdb_exec(NULL, "UPDATE operbans SET reason='%Q', "
				"hold='%ld', oper='%Q', remove='0' WHERE "
				"type='X' AND mask=LOWER('%Q')",
				reason,
				temptime ? CURRENT_TIME + temptime : 0,
				OPER_NAME(client_p, conn_p), gecos);
	else
		rsdb_exec(NULL, "INSERT INTO operbans "
				"(type, mask, reason, hold, create_time, "
				"oper, remove, flags) "
				"VALUES('X', LOWER('%Q'), '%Q', '%lu', '%lu', '%Q', '0', '0')",
				gecos, reason, 
				temptime ? CURRENT_TIME + temptime : 0,
				CURRENT_TIME, OPER_NAME(client_p, conn_p));

	service_snd(banserv_p, client_p, conn_p, SVC_BAN_ISSUED,
			"XLINE", gecos);

	push_ban("*", 'X', gecos, reason, temptime);

	zlog(banserv_p, 1, WATCH_BANSERV, 1, client_p, conn_p,
		"XLINE %s %s %s",
		temptime ? get_short_duration(temptime) : "perm",
		gecos, reason);

	return 0;
}

static int
o_banserv_resv(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *mask;
	char *reason;
	time_t temptime = 0;
	int para = 0;
	int res;

	if((temptime = get_temp_time(parv[para])))
		para++;

	res = find_ban(parv[para], 'R');

	if(res == 1)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_ALREADYPLACED,
				"RESV", parv[para]);
		return 0;
	}

	if(!temptime)
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_PERM))
				hit++;
		}
		else if(!(conn_p->sprivs & CONF_OPER_BAN_PERM))
			hit++;

		if(hit)
		{
			service_snd(banserv_p, client_p, conn_p, SVC_BAN_NOPERMACCESS, "RESV");
			return 0;
		}
	}

	mask = parv[para++];

	if(strlen(mask) > CHANNELLEN)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_INVALID,
				"RESV", mask);
		return 0;
	}

	reason = rebuild_params(parv, parc, para);

	if(EmptyString(reason))
	{
		service_snd(banserv_p, client_p, conn_p, SVC_NEEDMOREPARAMS,
				banserv_p->name, "RESV");
		return 0;
	}

	if(strlen(reason) > REASONLEN)
		reason[REASONLEN] = '\0';

	if(res)
		rsdb_exec(NULL, "UPDATE operbans SET reason='%Q', "
				"hold='%ld', oper='%Q', remove='0' WHERE "
				"type='R' AND mask=LOWER('%Q')",
				reason,
				temptime ? CURRENT_TIME + temptime : 0,
				OPER_NAME(client_p, conn_p), mask);
	else
		rsdb_exec(NULL, "INSERT INTO operbans "
				"(type, mask, reason, hold, create_time, "
				"oper, remove, flags) "
				"VALUES('R', LOWER('%Q'), '%Q', '%lu', '%lu', '%Q', '0', '0')",
				mask, reason, 
				temptime ? CURRENT_TIME + temptime : 0,
				CURRENT_TIME, OPER_NAME(client_p, conn_p));

	service_snd(banserv_p, client_p, conn_p, SVC_BAN_ISSUED,
			"RESV", mask);

	push_ban("*", 'R', mask, reason, temptime);

	zlog(banserv_p, 1, WATCH_BANSERV, 1, client_p, conn_p,
		"RESV %s %s %s",
		temptime ? get_short_duration(temptime) : "perm",
		mask, reason);

	return 0;
}

static int
o_banserv_addregexp(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static pcre *regexp_validity = NULL;
	pcre *regexp_comp;
	struct regexp_ban *regexp_p;
	const char *mask;
	const char *re_error;
	char *reason;
	time_t temptime = 0;
	int para = 0;
	int re_error_offset;
	unsigned int matches;
	dlink_node *ptr;

	if(regexp_validity == NULL)
	{
		regexp_validity = pcre_compile("^\\^.+!.+@.+#.+\\$$", 0, &re_error, &re_error_offset, NULL);

		if(regexp_validity == NULL)
		{
			die(1, "fatal error: Unable to compile validity regexp: %s", re_error);
		}
	}

	if((temptime = get_temp_time(parv[para])))
		para++;

	DLINK_FOREACH(ptr, regexp_list.head)
	{
		regexp_p = ptr->data;

		if(!strcmp(regexp_p->regexp_str, parv[para]))
		{
			service_snd(banserv_p, client_p, conn_p, SVC_BAN_ALREADYPLACED,
					"REGEXP", parv[para]);
			return 0;
		}
	}

	if(!temptime)
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_PERM))
				hit++;
		}
		else if(!(conn_p->sprivs & CONF_OPER_BAN_PERM))
			hit++;

		if(hit)
		{
			service_snd(banserv_p, client_p, conn_p, SVC_BAN_NOPERMACCESS, "REGEXP");
			return 0;
		}
	}

	mask = parv[para++];

	if(strlen(mask) >= 255 || pcre_exec(regexp_validity, NULL, mask, strlen(mask), 0, 0, NULL, 0) < 0)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_INVALID,
				"REGEXP", mask);
		return 0;
	}

	reason = rebuild_params(parv, parc, para);

	if(EmptyString(reason))
	{
		service_snd(banserv_p, client_p, conn_p, SVC_NEEDMOREPARAMS,
				banserv_p->name, "ADDREGEXP");
		return 0;
	}

	if(strlen(reason) > REASONLEN)
		reason[REASONLEN] = '\0';

	regexp_comp = pcre_compile(mask, 0, &re_error, &re_error_offset, NULL);

	if(regexp_comp == NULL)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_INVALID,
			"REGEXP", mask);
		return 0;
	}

	/* run the regexp over clients to see how many it matches */
	matches = regexp_match(regexp_comp, 0, NULL);

	/* then check its not over the limit */
	if(config_file.bs_max_regexp_matches && (matches > config_file.bs_max_regexp_matches))
	{
		pcre_free(regexp_comp);

		service_snd(banserv_p, client_p, conn_p, SVC_BAN_TOOMANYREGEXPMATCHES,
				mask, matches, config_file.bs_max_regexp_matches);
		return 0;
	}

	regexp_p = my_malloc(sizeof(struct regexp_ban));
	regexp_p->regexp = regexp_comp;
	regexp_p->regexp_str = my_strdup(mask);
	regexp_p->reason = my_strdup(reason);
	regexp_p->oper = my_strdup(OPER_NAME(client_p, conn_p));
	regexp_p->hold = temptime ? CURRENT_TIME + temptime : 0;
	regexp_p->create_time = CURRENT_TIME;

	dlink_add_tail(regexp_p, &regexp_p->ptr, &regexp_list);

	rsdb_exec_insert(&regexp_p->id, "operbans_regexp", "id", 
			"INSERT INTO operbans_regexp (regex, reason, hold, create_time, oper) "
			"VALUES('%Q', '%Q', '%lu', '%lu', '%Q')",
			mask, reason, 
			temptime ? CURRENT_TIME + temptime : 0,
			CURRENT_TIME, OPER_NAME(client_p, conn_p));

	matches = regexp_match(regexp_p->regexp, 1, regexp_p->reason);

	service_snd(banserv_p, client_p, conn_p, SVC_BAN_REGEXPSUCCESS,
			banserv_p->name, mask, matches);

	zlog(banserv_p, 1, WATCH_BANSERV, 1, client_p, conn_p,
		"ADDREGEXP %s %s %s",
		temptime ? get_short_duration(temptime) : "perm",
		mask, reason);

	return 0;
}

static int
o_banserv_addregexpneg(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static pcre *regexp_validity = NULL;
	pcre *regexp_comp;
	struct regexp_ban *regexp_p;
	struct regexp_ban *parent_p;
	const char *re_error;
	int re_error_offset;
	unsigned int parent_id;
	dlink_node *ptr;

	if(regexp_validity == NULL)
	{
		regexp_validity = pcre_compile("^\\^.+!.+@.+#.+\\$$", 0, &re_error, &re_error_offset, NULL);

		if(regexp_validity == NULL)
		{
			die(1, "fatal error: Unable to compile validity regexp: %s", re_error);
		}
	}

	parent_id = atoi(parv[0]);

	DLINK_FOREACH(ptr, regexp_list.head)
	{
		parent_p = ptr->data;

		if(parent_p->id == parent_id)
			break;
	}

	if(ptr == NULL)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_INVALID,
				"REGEXP", parv[0]);
		return 0;
	}

	DLINK_FOREACH(ptr, parent_p->negations.head)
	{
		regexp_p = ptr->data;

		if(!strcmp(parent_p->regexp_str, parv[1]))
		{
			service_snd(banserv_p, client_p, conn_p, SVC_BAN_ALREADYPLACED,
					"REGEXPNEG", parv[1]);
			return 0;
		}
	}


	if(strlen(parv[1]) >= 255 || pcre_exec(regexp_validity, NULL, parv[1], strlen(parv[1]), 0, 0, NULL, 0) < 0)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_INVALID,
				"REGEXPNEG", parv[1]);
		return 0;
	}

	regexp_comp = pcre_compile(parv[1], 0, &re_error, &re_error_offset, NULL);

	if(regexp_comp == NULL)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_INVALID,
			"REGEXPNEG", parv[1]);
		return 0;
	}

	regexp_p = my_malloc(sizeof(struct regexp_ban));
	regexp_p->regexp = regexp_comp;
	regexp_p->regexp_str = my_strdup(parv[1]);
	regexp_p->oper = my_strdup(OPER_NAME(client_p, conn_p));
	regexp_p->create_time = CURRENT_TIME;
	regexp_p->parent = parent_p;

	dlink_add_tail(regexp_p, &regexp_p->ptr, &parent_p->negations);

	rsdb_exec_insert(&regexp_p->id, "operbans_regexp_neg", "id", 
			"INSERT INTO operbans_regexp_neg (parent_id, regex, oper) "
			"VALUES('%u', '%Q', '%Q')",
			parent_p->id, parv[1], OPER_NAME(client_p, conn_p));

	service_snd(banserv_p, client_p, conn_p, SVC_SUCCESSFULON,
			banserv_p->name, "ADDREGEXPNEG", parv[1]);

	zlog(banserv_p, 1, WATCH_BANSERV, 1, client_p, conn_p,
		"ADDREGEXPNEG %u %s",
		parent_p->id, parv[1]);

	return 0;
}



static int
o_banserv_unkline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *oper = find_ban_remove(parv[0], 'K');

	if(oper == NULL)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_NOTPLACED,
				"KLINE", parv[0]);
		return 0;
	}

	if(irccmp(oper, OPER_NAME(client_p, conn_p)))
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_REMOVE))
				hit++;
		}
		else if(!(conn_p->sprivs & CONF_OPER_BAN_REMOVE))
			hit++;

		if(hit)
		{
			service_snd(banserv_p, client_p, conn_p, SVC_NOACCESS,
					banserv_p->name, "UNKLINE");
			return 0;
		}
	}

	if(!split_ban(parv[0], NULL, NULL))
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_INVALID,
				"KLINE", parv[0]);
		return 0;
	}

	rsdb_exec(NULL, "UPDATE operbans SET remove='1', hold='%lu' "
			"WHERE mask=LOWER('%Q') AND type='K'",
			CURRENT_TIME + config_file.bs_unban_time, parv[0]);

	service_snd(banserv_p, client_p, conn_p, SVC_BAN_ISSUED,
			"UNKLINE", parv[0]);

	push_unban("*", 'K', parv[0]);

	zlog(banserv_p, 1, WATCH_BANSERV, 1, client_p, conn_p,
		"UNKLINE %s", parv[0]);

	return 0;
}

static int
o_banserv_unxline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *oper = find_ban_remove(parv[0], 'X');

	if(oper == NULL)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_NOTPLACED,
				"XLINE", parv[0]);
		return 0;
	}

	if(irccmp(oper, OPER_NAME(client_p, conn_p)))
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_REMOVE))
				hit++;
		}
		else if(!(conn_p->sprivs & CONF_OPER_BAN_REMOVE))
			hit++;

		if(hit)
		{
			service_snd(banserv_p, client_p, conn_p, SVC_NOACCESS,
					banserv_p->name, "UNXLINE");
			return 0;
		}
	}

	rsdb_exec(NULL, "UPDATE operbans SET remove='1', hold='%lu' "
			"WHERE mask=LOWER('%Q') AND type='X'",
			CURRENT_TIME + config_file.bs_unban_time, parv[0]);

	service_snd(banserv_p, client_p, conn_p, SVC_BAN_ISSUED,
			"UNXLINE", parv[0]);

	push_unban("*", 'X', parv[0]);

	zlog(banserv_p, 1, WATCH_BANSERV, 1, client_p, conn_p,
		"UNXLINE %s", parv[0]);

	return 0;
}

static int
o_banserv_unresv(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	const char *oper = find_ban_remove(parv[0], 'R');

	if(oper == NULL)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_NOTPLACED,
				"RESV", parv[0]);
		return 0;
	}

	if(irccmp(oper, OPER_NAME(client_p, conn_p)))
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_REMOVE))
				hit++;
		}
		else if(!(conn_p->sprivs & CONF_OPER_BAN_REMOVE))
			hit++;

		if(hit)
		{
			service_snd(banserv_p, client_p, conn_p, SVC_NOACCESS,
					banserv_p->name, "UNRESV");
			return 0;
		}
	}

	rsdb_exec(NULL, "UPDATE operbans SET remove='1', hold='%lu' "
			"WHERE mask=LOWER('%Q') AND type='R'",
			CURRENT_TIME + config_file.bs_unban_time, parv[0]);

	service_snd(banserv_p, client_p, conn_p, SVC_BAN_ISSUED,
			"UNRESV", parv[0]);

	push_unban("*", 'R', parv[0]);

	zlog(banserv_p, 1, WATCH_BANSERV, 1, client_p, conn_p,
		"UNRESV %s", parv[0]);

	return 0;
}

static int
o_banserv_delregexp(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct regexp_ban *regexp_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, regexp_list.head)
	{
		regexp_p = ptr->data;

		if(!strcmp(regexp_p->regexp_str, parv[0]))
			break;
	}

	if(ptr == NULL)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_NOTPLACED,
				"REGEXP", parv[0]);
		return 0;
	}

	if(irccmp(regexp_p->oper, OPER_NAME(client_p, conn_p)))
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_REMOVE))
				hit++;
		}
		else if(!(conn_p->sprivs & CONF_OPER_BAN_REMOVE))
			hit++;

		if(hit)
		{
			service_snd(banserv_p, client_p, conn_p, SVC_NOACCESS,
					banserv_p->name, "DELREGEXP");
			return 0;
		}
	}

	/* this does our SQL */
	regexp_free(regexp_p, 0);

	service_snd(banserv_p, client_p, conn_p, SVC_SUCCESSFULON,
			banserv_p->name, "DELREGEXP", parv[0]);

	zlog(banserv_p, 1, WATCH_BANSERV, 1, client_p, conn_p,
		"DELREGEXP %s", parv[0]);

	return 0;
}

static int
o_banserv_delregexpneg(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct regexp_ban *regexp_p;
	struct regexp_ban *parent_p;
	unsigned int regexp_id;
	dlink_node *ptr;

	regexp_id = atoi(parv[0]);

	DLINK_FOREACH(ptr, regexp_list.head)
	{
		parent_p = ptr->data;

		if(parent_p->id == regexp_id)
			break;
	}

	if(ptr == NULL)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_NOTPLACED,
				"REGEXP", parv[0]);
		return 0;
	}

	DLINK_FOREACH(ptr, parent_p->negations.head)
	{
		regexp_p = ptr->data;

		if(!strcmp(regexp_p->regexp_str, parv[1]))
			break;
	}

	if(ptr == NULL)
	{
		service_snd(banserv_p, client_p, conn_p, SVC_BAN_NOTPLACED,
				"REGEXPNEG", parv[1]);
		return 0;
	}

	if(irccmp(regexp_p->oper, OPER_NAME(client_p, conn_p)))
	{
		unsigned int hit = 0;

		if(client_p)
		{
			if(!(client_p->user->oper->sflags & CONF_OPER_BAN_REMOVE))
				hit++;
		}
		else if(!(conn_p->sprivs & CONF_OPER_BAN_REMOVE))
			hit++;

		if(hit)
		{
			service_snd(banserv_p, client_p, conn_p, SVC_NOACCESS,
					banserv_p->name, "DELREGEXPNEG");
			return 0;
		}
	}

	rsdb_exec(NULL, "DELETE FROM operbans_regexp_neg WHERE id='%u'", regexp_p->id);

	/* deleting a negation, does not do our sql */
	regexp_free(regexp_p, 1);

	service_snd(banserv_p, client_p, conn_p, SVC_SUCCESSFULON,
			banserv_p->name, "DELREGEXPNEG", parv[1]);

	zlog(banserv_p, 1, WATCH_BANSERV, 1, client_p, conn_p,
		"DELREGEXPNEG %s", parv[1]);

	return 0;
}


static int
o_banserv_sync(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	char banletter = '\0';

	if(conn_p || irccmp(client_p->user->servername, parv[0]))
	{
		struct client *target_p;
		dlink_node *ptr;
		unsigned int hit = 0;

		if(client_p)
		{
			if(!client_p->user->oper || !(client_p->user->oper->sflags & CONF_OPER_BAN_SYNC))
				hit++;
		}
		else if(!(conn_p->sprivs & CONF_OPER_BAN_SYNC))
			hit++;

		if(hit)
		{
			service_snd(banserv_p, client_p, conn_p, SVC_NOACCESS,
					banserv_p->name, "SYNC");
			return 0;
		}

		/* check their mask matches at least one server */
		DLINK_FOREACH(ptr, server_list.head)
		{
			target_p = ptr->data;

			if(match(parv[0], target_p->name))
				break;
		}

		/* NULL if loop terminated without a break */
		if(ptr == NULL)
		{
			service_snd(banserv_p, client_p, conn_p, SVC_IRC_NOSUCHSERVER, parv[0]);
			return 0;
		}
	}

	if(!EmptyString(parv[1]))
	{
		if(!irccmp(parv[1], "klines"))
			banletter = 'K';
		else if(!irccmp(parv[1], "xlines"))
			banletter = 'X';
		else if(!irccmp(parv[1], "resvs"))
			banletter = 'R';
		else
		{
			service_snd(banserv_p, client_p, conn_p, SVC_OPTIONINVALID,
					banserv_p->name, "SYNC");
			return 0;
		}
	}

	sync_bans(parv[0], banletter);

	service_snd(banserv_p, client_p, conn_p, SVC_BAN_ISSUED,
			"SYNC", parv[0]);

	zlog(banserv_p, 1, WATCH_BANSERV, 1, client_p, conn_p,
		"SYNC %s %s",
		parv[0], EmptyString(parv[1]) ? "" : parv[1]);

	return 0;
}

static void
list_bans(struct client *client_p, struct lconn *conn_p, 
		const char *mask, char type)
{
	struct rsdb_table data;
	time_t duration;
	int i;

	rsdb_exec_fetch(&data, "SELECT mask, reason, operreason, hold, oper "
				"FROM operbans WHERE type='%c' AND remove='0' AND (hold='0' OR hold > '%lu')",
			type, (unsigned long) CURRENT_TIME);

	service_snd(banserv_p, client_p, conn_p, SVC_BAN_LISTSTART, mask);

	for(i = 0; i < data.row_count; i++)
	{
		if(!match(mask, data.row[i][0]))
			continue;

		duration = (unsigned long) atol(data.row[i][3]);

		if(duration)
			duration -= CURRENT_TIME;

		service_send(banserv_p, client_p, conn_p,
				"  %-30s exp:%s oper:%s [%s%s]",
				data.row[i][0], duration ? get_short_duration(duration) : "never",
				data.row[i][4], data.row[i][1],
				EmptyString(data.row[i][2]) ? "" : data.row[i][2]);
	}

	rsdb_exec_fetch_end(&data);

	service_snd(banserv_p, client_p, conn_p, SVC_ENDOFLIST);
}

static int
o_banserv_findkline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	list_bans(client_p, conn_p, parv[0], 'K');
	return 0;
}

static int
o_banserv_findxline(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	list_bans(client_p, conn_p, parv[0], 'X');
	return 0;
}

static int
o_banserv_findresv(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	list_bans(client_p, conn_p, parv[0], 'R');
	return 0;
}

static int
o_banserv_listregexps(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct regexp_ban *regexp_p;
	struct regexp_ban *neg_p;
	time_t duration;
	dlink_node *ptr;
	dlink_node *next_ptr;
	dlink_node *neg_ptr;

	service_snd(banserv_p, client_p, conn_p, SVC_BAN_LISTSTART, "REGEXPS");

	DLINK_FOREACH_SAFE(ptr, next_ptr, regexp_list.head)
	{
		regexp_p = ptr->data;

		if(regexp_p->hold)
		{
			if(regexp_p->hold <= CURRENT_TIME)
			{
				regexp_free(regexp_p, 0);
				continue;
			}

			duration = regexp_p->hold - CURRENT_TIME;
		}
		else
			duration = 0;

		service_send(banserv_p, client_p, conn_p, 
				"  %-4u %-40s exp:%s oper:%s [%s]",
				regexp_p->id, regexp_p->regexp_str,
				duration ? get_short_duration(duration) : "never",
				regexp_p->oper, regexp_p->reason);

		DLINK_FOREACH(neg_ptr, regexp_p->negations.head)
		{
			neg_p = neg_ptr->data;

			service_send(banserv_p, client_p, conn_p,
					"    NEG     %-40s oper:%s",
					neg_p->regexp_str, neg_p->oper);
		}
				
	}

	service_snd(banserv_p, client_p, conn_p, SVC_ENDOFLIST);
	return 0;
}

#endif
