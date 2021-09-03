/* s_userserv.c
 *   Contains code for user registration service.
 *
 * Copyright (C) 2004-2007 Lee Hardy
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
 * $Id: s_userserv.c 26963 2010-02-28 13:05:30Z leeh $
 */
#include "stdinc.h"

#ifdef ENABLE_USERSERV
#include "rsdb.h"
#include "rserv.h"
#include "langs.h"
#include "service.h"
#include "client.h"
#include "channel.h"
#include "c_init.h"
#include "log.h"
#include "s_chanserv.h"
#include "s_userserv.h"
#include "s_nickserv.h"
#include "ucommand.h"
#include "balloc.h"
#include "conf.h"
#include "io.h"
#include "event.h"
#include "hook.h"
#include "email.h"
#include "dbhook.h"
#include "watch.h"

#define MAX_HASH_WALK	1024

static void init_s_userserv(void);

static struct client *userserv_p;
static BlockHeap *user_reg_heap;

dlink_list user_reg_table[MAX_NAME_HASH];

static int o_user_userregister(struct client *, struct lconn *, const char **, int);
static int o_user_userdrop(struct client *, struct lconn *, const char **, int);
static int o_user_usersuspend(struct client *, struct lconn *, const char **, int);
static int o_user_userunsuspend(struct client *, struct lconn *, const char **, int);
static int o_user_userlist(struct client *, struct lconn *, const char **, int);
static int o_user_userinfo(struct client *, struct lconn *, const char **, int);
static int o_user_usersetpass(struct client *, struct lconn *, const char **, int);
static int o_user_usersetemail(struct client *, struct lconn *, const char **, int);

static int s_user_register(struct client *, struct lconn *, const char **, int);
static int s_user_activate(struct client *, struct lconn *, const char **, int);
static int s_user_login(struct client *, struct lconn *, const char **, int);
static int s_user_logout(struct client *, struct lconn *, const char **, int);
static int s_user_resetpass(struct client *, struct lconn *, const char **, int);
static int s_user_resetemail(struct client *, struct lconn *, const char **, int);
static int s_user_set(struct client *, struct lconn *, const char **, int);
static int s_user_info(struct client *, struct lconn *, const char **, int);

static struct service_command userserv_command[] =
{
	{ "USERREGISTER",	&o_user_userregister,	2, NULL, 1, 0L, 0, 0, CONF_OPER_US_REGISTER	},
	{ "USERDROP",		&o_user_userdrop,	1, NULL, 1, 0L, 0, 0, CONF_OPER_US_DROP		},
	{ "USERSUSPEND",	&o_user_usersuspend,	2, NULL, 1, 0L, 0, 0, CONF_OPER_US_SUSPEND	},
	{ "USERUNSUSPEND",	&o_user_userunsuspend,	1, NULL, 1, 0L, 0, 0, CONF_OPER_US_SUSPEND	},
	{ "USERLIST",		&o_user_userlist,	1, NULL, 1, 0L, 0, 0, CONF_OPER_US_LIST		},
	{ "USERINFO",		&o_user_userinfo,	1, NULL, 1, 0L, 0, 0, CONF_OPER_US_INFO		},
	{ "USERSETPASS",	&o_user_usersetpass,	2, NULL, 1, 0L, 0, 0, CONF_OPER_US_SETPASS	},
	{ "USERSETEMAIL",	&o_user_usersetemail,	2, NULL, 1, 0L, 0, 0, CONF_OPER_US_SETEMAIL	},
	{ "REGISTER",	&s_user_register,	2, NULL, 1, 0L, 0, 0, 0	},
	{ "ACTIVATE",	&s_user_activate,	2, NULL, 1, 0L, 0, 0, 0	},
	{ "LOGIN",	&s_user_login,		2, NULL, 1, 0L, 0, 0, 0	},
	{ "LOGOUT",	&s_user_logout,		0, NULL, 1, 0L, 1, 0, 0	},
	{ "RESETPASS",	&s_user_resetpass,	1, NULL, 1, 0L, 0, 0, 0	},
	{ "RESETEMAIL",	&s_user_resetemail,	0, NULL, 1, 0L, 1, 0, 0	},
	{ "SET",	&s_user_set,		1, NULL, 1, 0L, 1, 0, 0	},
	{ "INFO",	&s_user_info,		1, NULL, 1, 0L, 1, 0, 0	},
	{ "LANGUAGE",	NULL,			0, NULL, 1, 0L, 0, 0, 0 }
};

static struct ucommand_handler userserv_ucommand[] =
{
	{ "userregister",	o_user_userregister,	0, CONF_OPER_US_REGISTER,	2, NULL },
	{ "userdrop",		o_user_userdrop,	0, CONF_OPER_US_DROP,		1, NULL },
	{ "usersuspend",	o_user_usersuspend,	0, CONF_OPER_US_SUSPEND,	2, NULL },
	{ "userunsuspend",	o_user_userunsuspend,	0, CONF_OPER_US_SUSPEND,	1, NULL },
	{ "userlist",		o_user_userlist,	0, CONF_OPER_US_LIST,		0, NULL },
	{ "userinfo",		o_user_userinfo,	0, CONF_OPER_US_INFO,		1, NULL },
	{ "usersetpass",	o_user_usersetpass,	0, CONF_OPER_US_SETPASS,	2, NULL },
	{ "usersetemail",	o_user_usersetemail,	0, CONF_OPER_US_SETEMAIL,	2, NULL },
	{ "\0",			NULL,			0, 0,				0, NULL }
};

static struct service_handler userserv_service = {
	"USERSERV", "USERSERV", "userserv", "services.int", "User Auth Services",
	0, 0, userserv_command, sizeof(userserv_command), userserv_ucommand, init_s_userserv, NULL
};

static int user_db_callback(int argc, const char **argv);
static int dbh_user_register(struct rsdb_hook *, const char *data);
static int dbh_user_setpass(struct rsdb_hook *, const char *data);
static int dbh_user_setemail(struct rsdb_hook *, const char *data);
static int h_user_burst_login(void *, void *);
static int h_user_dbsync(void *, void *);
static void e_user_expire(void *unused);
static void e_user_expire_reset(void *unused);

static void dump_user_info(struct client *, struct lconn *, struct user_reg *);

static int valid_email(const char *email);
static int valid_email_domain(const char *email);
static void expire_user_suspend(struct user_reg *ureg_p);

void
preinit_s_userserv(void)
{
	userserv_p = add_service(&userserv_service);
}

static void
init_s_userserv(void)
{
	user_reg_heap = BlockHeapCreate("User Reg", sizeof(struct user_reg), HEAP_USER_REG);

	rsdb_exec(user_db_callback, 
			"SELECT username, password, email, suspender, suspend_reason, "
			"suspend_time, reg_time, last_time, flags, language, id FROM users");

	rsdb_hook_add("users_sync", "REGISTER", 900, dbh_user_register);
	rsdb_hook_add("users_sync", "SETPASS", 900, dbh_user_setpass);
	rsdb_hook_add("users_sync", "SETEMAIL", 900, dbh_user_setemail);

	hook_add(h_user_burst_login, HOOK_BURST_LOGIN);
	hook_add(h_user_dbsync, HOOK_DBSYNC);

	eventAdd("userserv_expire", e_user_expire, NULL, 900);
	eventAdd("userserv_expire_reset", e_user_expire_reset, NULL, 3600);
}

static void
add_user_reg(struct user_reg *reg_p)
{
	unsigned int hashv = hash_name(reg_p->name);
	dlink_add(reg_p, &reg_p->node, &user_reg_table[hashv]);
}

static void
free_user_reg(struct user_reg *ureg_p)
{
	dlink_node *ptr, *next_ptr;
	unsigned int hashv = hash_name(ureg_p->name);

	dlink_delete(&ureg_p->node, &user_reg_table[hashv]);

	rsdb_exec(NULL, "DELETE FROM users_resetpass WHERE username = '%Q'",
			ureg_p->name);
	rsdb_exec(NULL, "DELETE FROM users_resetemail WHERE username = '%Q'",
			ureg_p->name);
	rsdb_exec(NULL, "DELETE FROM members WHERE username = '%Q'",
			ureg_p->name);

#ifdef ENABLE_CHANSERV
	DLINK_FOREACH_SAFE(ptr, next_ptr, ureg_p->channels.head)
	{
		free_member_reg(ptr->data, 1);
	}
#endif

#ifdef ENABLE_NICKSERV
	DLINK_FOREACH_SAFE(ptr, next_ptr, ureg_p->nicks.head)
	{
		free_nick_reg(ptr->data);
	}
#endif
			
	rsdb_exec(NULL, "DELETE FROM users WHERE username = '%Q'",
			ureg_p->name);

	my_free(ureg_p->password);
	my_free(ureg_p->email);
	my_free(ureg_p->suspender);
	my_free(ureg_p->suspend_reason);
	BlockHeapFree(user_reg_heap, ureg_p);
}

static int
user_db_callback(int argc, const char **argv)
{
	struct user_reg *reg_p;

	if(EmptyString(argv[0]))
		return 0;

	/* don't particularly want to be trimming this down to fit in..
	 * Ignore it, and move on.
	 */
	if(strlen(argv[0]) > USERREGNAME_LEN)
	{
		mlog("warning: Registered username %s exceeds username length limit, ignoring user",
			argv[0]);
		return 0;
	}

	reg_p = BlockHeapAlloc(user_reg_heap);
	strlcpy(reg_p->name, argv[0], sizeof(reg_p->name));
	reg_p->password = my_strdup(argv[1]);

	if(!EmptyString(argv[2]))
		reg_p->email = my_strdup(argv[2]);

	if(!EmptyString(argv[3]))
		reg_p->suspender = my_strdup(argv[3]);

	if(!EmptyString(argv[4]))
		reg_p->suspend_reason = my_strdup(argv[4]);

	if(!EmptyString(argv[5]))
		reg_p->suspend_time = atol(argv[5]);

	reg_p->reg_time = atol(argv[6]);
	reg_p->last_time = atol(argv[7]);
	reg_p->flags = atoi(argv[8]);

	/* entries may not have a language */
	if(!EmptyString(argv[9]))
		reg_p->language = lang_get_langcode(argv[9]);

	reg_p->id = atoi(argv[10]);

	add_user_reg(reg_p);

	return 0;
}

struct user_reg *
find_user_reg(struct client *client_p, const char *username)
{
	struct user_reg *reg_p;
	unsigned int hashv = hash_name(username);
	dlink_node *ptr;

	DLINK_FOREACH(ptr, user_reg_table[hashv].head)
	{
		reg_p = ptr->data;

		if(!strcasecmp(reg_p->name, username))
			return reg_p;
	}

	if(client_p != NULL)
		service_err(userserv_p, client_p, SVC_USER_NOTREG, username);

	return NULL;
}

struct user_reg *
find_user_reg_nick(struct client *client_p, const char *name)
{
	if(*name == '=')
	{
		struct client *target_p;
		
		if((target_p = find_user(name+1, 0)) == NULL ||
		   target_p->user->user_reg == NULL)
		{
			if(client_p != NULL)
				service_err(userserv_p, client_p, SVC_USER_NICKNOTLOGGEDIN,
						name+1);
			return NULL;
		}

		return target_p->user->user_reg;
	}
	else
		return find_user_reg(client_p, name);
}

static int
valid_username(const char *name)
{
	if(strlen(name) > USERREGNAME_LEN)
		return 0;

	if(IsDigit(*name) || *name == '-')
		return 0;

	for(; *name; name++)
	{
		if(!IsNickChar(*name))
			return 0;
	}

	return 1;
}

static void
logout_user_reg(struct user_reg *ureg_p)
{
	struct client *target_p;
	dlink_node *ptr, *next_ptr;

	if(!dlink_list_length(&ureg_p->users))
		return;

	DLINK_FOREACH_SAFE(ptr, next_ptr, ureg_p->users.head)
	{
		target_p = ptr->data;

		sendto_server(":%s ENCAP * SU %s", MYUID, UID(target_p));

		target_p->user->user_reg = NULL;
		dlink_destroy(ptr, &ureg_p->users);
	}
}

static void
dbh_user_register_update_id(void *arg)
{
	struct user_reg *ureg_p = arg;
	struct rsdb_table data;

	rsdb_exec_fetch(&data, "SELECT id FROM users WHERE username='%Q'",
			ureg_p->name);

	if(data.row_count == 0)
	{
		mlog("warning: Unable to retrieve ID for dbhook registered username %s, deleting user",
			ureg_p->name);

		rsdb_exec_fetch_end(&data);
		free_user_reg(ureg_p);
		return;
	}

	ureg_p->id = atoi(data.row[0][0]);
	rsdb_exec_fetch_end(&data);
}

static int
dbh_user_register(struct rsdb_hook *dbh, const char *c_data)
{
	static char *argv[MAXPARA];
	struct user_reg *ureg_p;
	char *data;
	int argc;

	data = LOCAL_COPY(c_data);

	argc = string_to_array(data, argv);

	if(EmptyString(argv[0]) || EmptyString(argv[1]))
		return 1;

	if((ureg_p = find_user_reg(NULL, argv[0])))
		return 1;

	ureg_p = BlockHeapAlloc(user_reg_heap);
	strlcpy(ureg_p->name, argv[0], sizeof(ureg_p->name));
	ureg_p->password = my_strdup(argv[1]);

	if(!EmptyString(argv[2]))
		ureg_p->email = my_strdup(argv[2]);

	ureg_p->reg_time = ureg_p->last_time = CURRENT_TIME;

	add_user_reg(ureg_p);

	rsdb_hook_schedule(dbh_user_register_update_id, ureg_p,
			"INSERT INTO users (username, password, email, reg_time, last_time, flags, language) "
			"VALUES('%Q','%Q','%Q','%lu','%lu','0', '')",
			ureg_p->name, ureg_p->password, ureg_p->email,
			ureg_p->reg_time, ureg_p->last_time);
	return 1;
}

static int
dbh_user_setpass(struct rsdb_hook *dbh, const char *c_data)
{
	static char *argv[MAXPARA];
	struct user_reg *ureg_p;
	char *data;
	int argc;

	data = LOCAL_COPY(c_data);
	argc = string_to_array(data, argv);

	if(EmptyString(argv[0]) || EmptyString(argv[1]))
		return 1;

	if((ureg_p = find_user_reg(NULL, argv[0])) == NULL)
		return 1;

	my_free(ureg_p->password);
	ureg_p->password = my_strdup(argv[1]);

	rsdb_hook_schedule(NULL, NULL, "UPDATE users SET password='%Q' WHERE username='%Q'",
			ureg_p->password, ureg_p->name);

	return 1;
}

static int
dbh_user_setemail(struct rsdb_hook *dbh, const char *c_data)
{
	static char *argv[MAXPARA];
	struct user_reg *ureg_p;
	char *data;
	int argc;

	data = LOCAL_COPY(c_data);
	argc = string_to_array(data, argv);

	if(EmptyString(argv[0]) || EmptyString(argv[1]))
		return 1;

	if((ureg_p = find_user_reg(NULL, argv[0])) == NULL)
		return 1;

	my_free(ureg_p->email);
	ureg_p->email = my_strdup(argv[1]);

	rsdb_hook_schedule(NULL, NULL, "UPDATE users SET email='%Q' WHERE username='%Q'",
			ureg_p->email, ureg_p->name);

	return 1;
}

static int
h_user_burst_login(void *v_client_p, void *v_username)
{
	struct client *client_p = v_client_p;
	struct user_reg *ureg_p;
	const char *username = v_username;

	/* only accepted during a burst.. */
	if(IsEOB(client_p->uplink))
		return 0;

	/* nickname that isnt actually registered.. log them out */
	if((ureg_p = find_user_reg(NULL, username)) == NULL)
	{
		sendto_server(":%s ENCAP * SU %s", MYUID, UID(client_p));
		return 0;
	}

	/* already logged in.. hmm, this shouldnt really happen */
	if(client_p->user->user_reg)
		dlink_find_destroy(client_p, &client_p->user->user_reg->users);

	/* username is suspended, ignore it and log them out */
	if(ureg_p->flags & US_FLAGS_SUSPENDED)
	{
		sendto_server(":%s ENCAP * SU %s", MYUID, UID(client_p));
		return 0;
	}

	client_p->user->user_reg = ureg_p;
	dlink_add_alloc(client_p, &ureg_p->users);

	ureg_p->last_time = CURRENT_TIME;
	ureg_p->flags |= US_FLAGS_NEEDUPDATE;

	return 0;
}

static int
h_user_dbsync(void *unused, void *unusedd)
{
	struct user_reg *ureg_p;
	dlink_node *ptr;
	int i;

	rsdb_transaction(RSDB_TRANS_START);

	HASH_WALK(i, MAX_NAME_HASH, ptr, user_reg_table)
	{
		ureg_p = ptr->data;

		/* if they're logged in, reset the expiry */
		if(dlink_list_length(&ureg_p->users))
		{
			ureg_p->last_time = CURRENT_TIME;
			ureg_p->flags |= US_FLAGS_NEEDUPDATE;
		}

		if(ureg_p->flags & US_FLAGS_NEEDUPDATE)
		{
			ureg_p->flags &= ~US_FLAGS_NEEDUPDATE;
			rsdb_exec(NULL, "UPDATE users SET last_time='%lu' WHERE username='%Q'",
					ureg_p->last_time, ureg_p->name);
		}
	}
	HASH_WALK_END

	rsdb_transaction(RSDB_TRANS_END);

	return 0;
}

static int
expire_bonus(time_t duration)
{
	unsigned int bonus;

	/* disabled */
	if(!config_file.uexpire_bonus_per_time || !config_file.uexpire_bonus)
		return 0;

	if(duration < config_file.uexpire_bonus_regtime)
		return 0;

	bonus = ((duration / config_file.uexpire_bonus_per_time) * config_file.uexpire_bonus);

	if(config_file.uexpire_bonus_max && (bonus > config_file.uexpire_bonus_max))
		return config_file.uexpire_bonus_max;

	return bonus;
}

static void
e_user_expire(void *unused)
{
	static int hash_pos = 0;
	struct user_reg *ureg_p;
	dlink_node *ptr, *next_ptr;
	int i;

	/* Start a transaction, we're going to make a lot of changes */
	rsdb_transaction(RSDB_TRANS_START);

	HASH_WALK_SAFE_POS(i, hash_pos, MAX_HASH_WALK, MAX_NAME_HASH, ptr, next_ptr, user_reg_table)
	{
		ureg_p = ptr->data;

		/* nuke unverified accounts first */
		if(ureg_p->flags & US_FLAGS_NEVERLOGGEDIN &&
		   (ureg_p->reg_time + config_file.uexpire_unverified_time) <= CURRENT_TIME)
		{
			free_user_reg(ureg_p);
			continue;
		}
				
		/* if they're logged in, reset the expiry */
		if(dlink_list_length(&ureg_p->users))
		{
			ureg_p->last_time = CURRENT_TIME;
			ureg_p->flags |= US_FLAGS_NEEDUPDATE;
		}

		if(ureg_p->flags & US_FLAGS_NEEDUPDATE)
		{
			ureg_p->flags &= ~US_FLAGS_NEEDUPDATE;
			rsdb_exec(NULL, "UPDATE users SET last_time='%lu' WHERE username='%Q'",
				ureg_p->last_time, ureg_p->name);
		}

		if(ureg_p->flags & US_FLAGS_SUSPENDED)
		{
			if(USER_SUSPEND_EXPIRED(ureg_p))
				expire_user_suspend(ureg_p);

			if(!config_file.uexpire_suspended_time)
				continue;

			if((ureg_p->last_time + config_file.uexpire_suspended_time) > CURRENT_TIME)
				continue;
		}
		else if((ureg_p->last_time + config_file.uexpire_time + expire_bonus(CURRENT_TIME - ureg_p->reg_time)) > CURRENT_TIME)
			continue;

		free_user_reg(ureg_p);
	}
	HASH_WALK_SAFE_POS_END(i, hash_pos, MAX_NAME_HASH);

	rsdb_transaction(RSDB_TRANS_END);
}

static void
e_user_expire_reset(void *unused)
{
	rsdb_exec(NULL, "DELETE FROM users_resetpass WHERE time <= '%lu'",
			CURRENT_TIME - config_file.uresetpass_duration);
	rsdb_exec(NULL, "DELETE FROM users_resetemail WHERE time <= '%lu'",
			CURRENT_TIME - config_file.uresetemail_duration);
}

static void
expire_user_suspend(struct user_reg *ureg_p)
{
	ureg_p->flags &= ~US_FLAGS_SUSPENDED;
	my_free(ureg_p->suspender);
	ureg_p->suspender = NULL;
	my_free(ureg_p->suspend_reason);
	ureg_p->suspend_reason = NULL;
	ureg_p->suspend_time = 0;
	ureg_p->last_time = CURRENT_TIME;

	rsdb_exec(NULL, "UPDATE users SET flags='%d',suspender=NULL,suspend_reason=NULL,last_time='%lu',suspend_time='0' WHERE username='%Q'",
			ureg_p->flags, ureg_p->last_time, ureg_p->name);
}

static int
o_user_userregister(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *reg_p;
	const char *password;

	if((reg_p = find_user_reg(NULL, parv[0])) != NULL)
	{
		service_snd(userserv_p, client_p, conn_p, SVC_USER_ALREADYREG, 
				reg_p->name);
		return 0;
	}

	if(!valid_username(parv[0]))
	{
		service_snd(userserv_p, client_p, conn_p, SVC_USER_INVALIDUSERNAME, parv[0]);
		return 0;
	}

	if(strlen(parv[1]) > PASSWDLEN)
	{
		service_snd(userserv_p, client_p, conn_p, SVC_USER_LONGPASSWORD);
		return 0;
	}

	zlog(userserv_p, 1, WATCH_USADMIN, 1, client_p, conn_p,
		"USERREGISTER %s %s",
		parv[0], EmptyString(parv[2]) ? "-" : parv[2]);

	reg_p = BlockHeapAlloc(user_reg_heap);
	strlcpy(reg_p->name, parv[0], sizeof(reg_p->name));

	password = get_crypt(parv[1], NULL);
	reg_p->password = my_strdup(password);

	if(!EmptyString(parv[2]))
	{
		if(valid_email(parv[2]))
			reg_p->email = my_strdup(parv[2]);
		else
			service_snd(userserv_p, client_p, conn_p, SVC_EMAIL_INVALIDIGNORED, parv[2]);
	}

	reg_p->reg_time = reg_p->last_time = CURRENT_TIME;

	add_user_reg(reg_p);

	rsdb_exec_insert(&reg_p->id, "users", "id",
			"INSERT INTO users (username, password, email, reg_time, last_time, flags, language) "
			"VALUES('%Q', '%Q', '%Q', '%lu', '%lu', '%u', '')",
			reg_p->name, reg_p->password, 
			EmptyString(reg_p->email) ? "" : reg_p->email, 
			reg_p->reg_time, reg_p->last_time, reg_p->flags);

	service_snd(userserv_p, client_p, conn_p, SVC_USER_NOWREG, parv[0]);
	return 0;
}

static int
o_user_userdrop(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;

	if((ureg_p = find_user_reg(NULL, parv[0])) == NULL)
	{
		service_snd(userserv_p, client_p, conn_p, SVC_USER_NOTREG, parv[0]);
		return 0;
	}

	zlog(userserv_p, 1, WATCH_USADMIN, 1, client_p, conn_p,
		"USERDROP %s", ureg_p->name);

	logout_user_reg(ureg_p);

	service_snd(userserv_p, client_p, conn_p, SVC_USER_REGDROPPED, ureg_p->name);
	free_user_reg(ureg_p);

	return 0;
}

static int
o_user_usersuspend(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *reg_p;
	const char *reason;
	time_t suspend_time;
	int para = 0;

	if((suspend_time = get_temp_time(parv[0])))
		para++;

	if((reg_p = find_user_reg(NULL, parv[para])) == NULL)
	{
		service_snd(userserv_p, client_p, conn_p, SVC_USER_NOTREG, parv[para]);
		return 0;
	}

	para++;

	if(reg_p->flags & US_FLAGS_SUSPENDED)
	{
		/* this suspend may have expired.. */
		if(!USER_SUSPEND_EXPIRED(reg_p))
		{
			service_snd(userserv_p, client_p, conn_p, SVC_USER_QUERYOPTIONALREADY,
					reg_p->name, "SUSPEND", "ON");
			return 0;
		}
		else
			/* clean it up so we dont leak memory */
			expire_user_suspend(reg_p);
	}

	reason = rebuild_params(parv, parc, para);

	if(EmptyString(reason))
	{
		service_err(userserv_p, client_p, SVC_NEEDMOREPARAMS,
				userserv_p->name, "USERSUSPEND");
		return 0;
	}

	zlog(userserv_p, 1, WATCH_USADMIN, 1, client_p, conn_p,
		"USERSUSPEND %s %s", reg_p->name, reason);

	logout_user_reg(reg_p);

	reg_p->flags |= US_FLAGS_SUSPENDED;
	reg_p->last_time = CURRENT_TIME;

	if(suspend_time)
		reg_p->suspend_time = CURRENT_TIME + suspend_time;
	else
		reg_p->suspend_time = 0;

	reg_p->suspender = my_strdup(OPER_NAME(client_p, conn_p));
	reg_p->suspend_reason = my_strndup(reason, SUSPENDREASONLEN);

	rsdb_exec(NULL, "UPDATE users SET flags='%d', suspender='%Q', "
			"suspend_reason='%Q',last_time='%lu', suspend_time='%lu' WHERE username='%Q'",
			reg_p->flags, reg_p->suspender, reg_p->suspend_reason,
			reg_p->last_time, reg_p->suspend_time, reg_p->name);

	service_snd(userserv_p, client_p, conn_p, SVC_USER_CHANGEDOPTION,
			reg_p->name, "SUSPEND", "ON");
	return 0;
}

static int
o_user_userunsuspend(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *reg_p;

	if((reg_p = find_user_reg(NULL, parv[0])) == NULL)
	{
		service_snd(userserv_p, client_p, conn_p, SVC_USER_NOTREG, parv[0]);
		return 0;
	}

	if((reg_p->flags & US_FLAGS_SUSPENDED) == 0)
	{
		service_snd(userserv_p, client_p, conn_p, SVC_USER_QUERYOPTIONALREADY,
				reg_p->name, "SUSPEND", "OFF");
		return 0;
	}

	zlog(userserv_p, 1, WATCH_USADMIN, 1, client_p, conn_p,
		"USERUNSUSPEND %s", reg_p->name);

	reg_p->flags &= ~US_FLAGS_SUSPENDED;
	my_free(reg_p->suspender);
	reg_p->suspender = NULL;
	my_free(reg_p->suspend_reason);
	reg_p->suspend_reason = NULL;
	reg_p->suspend_time = 0;
	reg_p->last_time = CURRENT_TIME;

	rsdb_exec(NULL, "UPDATE users SET flags='%d',suspender=NULL,suspend_reason=NULL,last_time='%lu',suspend_time='0' WHERE username='%Q'",
			reg_p->flags, reg_p->last_time, reg_p->name);

	service_snd(userserv_p, client_p, conn_p, SVC_USER_CHANGEDOPTION,
			reg_p->name, "SUSPEND", "OFF");
	return 0;
}

#define USERLIST_LEN	350	/* should be long enough */

static int
o_user_userlist(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	static char def_mask[] = "*";
	static char buf[BUFSIZE];
	struct user_reg *ureg_p;
	const char *mask = def_mask;
	dlink_node *ptr;
	unsigned int limit = 100;
	int para = 0;
	int longlist = 0, suspended = 0;
	int i;
	int buflen = 0;
	int arglen;

	buf[0] = '\0';

	if(parc > para && !strcmp(parv[para], "-long"))
	{
		longlist++;
		para++;
	}

	if(parc > para && !strcmp(parv[para], "-suspended"))
	{
		suspended++;
		para++;
	}

	if(parc > para)
	{
		mask = parv[para];
		para++;
	
		if(parc > para)
			limit = atoi(parv[para]);
	}

	service_snd(userserv_p, client_p, conn_p, SVC_USER_UL_START,
			mask, limit, suspended ? ", suspended" : "");

	HASH_WALK(i, MAX_NAME_HASH, ptr, user_reg_table)
	{
		ureg_p = ptr->data;

		if(!match(mask, ureg_p->name))
			continue;

		/* expire any suspends */
		if(USER_SUSPEND_EXPIRED(ureg_p))
			expire_user_suspend(ureg_p);

		if(suspended)
		{
			if((ureg_p->flags & US_FLAGS_SUSPENDED) == 0)
				continue;
		}
		else if(ureg_p->flags & US_FLAGS_SUSPENDED)
			continue;

		if(!longlist)
		{
			arglen = strlen(ureg_p->name);

			if(buflen + arglen >= USERLIST_LEN)
			{
				service_send(userserv_p, client_p, conn_p,
						"  %s", buf);
				buf[0] = '\0';
				buflen = 0;
			}

			strcat(buf, ureg_p->name);
			strcat(buf, " ");
			buflen += arglen+1;
		}
		else
		{
			static char last_active[] = "Active";
			char timebuf[BUFSIZE];
			const char *p = last_active;

			if(suspended)
			{
				snprintf(timebuf, sizeof(timebuf),
					"Suspended by %s: %s",
					ureg_p->suspender,
					ureg_p->suspend_reason ? ureg_p->suspend_reason : NULL);
				p = timebuf;
			}
			else if(!dlink_list_length(&ureg_p->users))
			{
				snprintf(timebuf, sizeof(timebuf), "Last %s",
					get_short_duration(CURRENT_TIME - ureg_p->last_time));
				p = timebuf;
			}

			service_send(userserv_p, client_p, conn_p,
				"  %s - Email %s For %s %s",
				ureg_p->name,
				EmptyString(ureg_p->email) ? "<>" : ureg_p->email,
				get_short_duration(CURRENT_TIME - ureg_p->reg_time),
				p);
		}

		if(limit == 1)
		{
			/* two loops to exit here, kludge it */
			i = MAX_NAME_HASH;
			break;
		}

		limit--;
	}
	HASH_WALK_END

	if(!longlist)
		service_send(userserv_p, client_p, conn_p, "  %s", buf);

	if(limit == 1)
		service_snd(userserv_p, client_p, conn_p, SVC_ENDOFLISTLIMIT);
	else
		service_snd(userserv_p, client_p, conn_p, SVC_ENDOFLIST);

	zlog(userserv_p, 1, WATCH_USADMIN, 1, client_p, conn_p,
		"USERLIST %s", mask);

	return 0;
}

static int
o_user_userinfo(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;

	if((ureg_p = find_user_reg_nick(NULL, parv[0])) == NULL)
	{
		if(parv[0][0] == '=')
			service_snd(userserv_p, client_p, conn_p, SVC_USER_NICKNOTLOGGEDIN, parv[0]);
		else
			service_snd(userserv_p, client_p, conn_p, SVC_USER_NOTREG, parv[0]);

		return 0;
	}

	zlog(userserv_p, 1, WATCH_USADMIN, 1, client_p, conn_p,
		"USERINFO %s", ureg_p->name);

	service_snd(userserv_p, client_p, conn_p, SVC_INFO_REGDURATIONUSER,
			ureg_p->name,
			get_duration((time_t) (CURRENT_TIME - ureg_p->reg_time)));

	if(USER_SUSPEND_EXPIRED(ureg_p))
		expire_user_suspend(ureg_p);

	if(ureg_p->flags & US_FLAGS_SUSPENDED)
	{
		time_t suspend_time = ureg_p->suspend_time;

		if(suspend_time)
			suspend_time -= CURRENT_TIME;

		service_snd(userserv_p, client_p, conn_p, SVC_INFO_SUSPENDED,
				ureg_p->name, ureg_p->suspender,
				suspend_time ? get_short_duration(suspend_time) : "never",
				ureg_p->suspend_reason ? ureg_p->suspend_reason : "");
	}

	dump_user_info(client_p, conn_p, ureg_p);
	return 0;
}

static int
o_user_usersetpass(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;
	const char *password;

	if((ureg_p = find_user_reg(NULL, parv[0])) == NULL)
	{
		service_snd(userserv_p, client_p, conn_p, SVC_USER_NOTREG, parv[0]);
		return 0;
	}

	if(strlen(parv[1]) > PASSWDLEN)
	{
		service_snd(userserv_p, client_p, conn_p, SVC_USER_LONGPASSWORD);
		return 0;
	}

	zlog(userserv_p, 1, WATCH_USADMIN, 1, client_p, conn_p,
		"USERSETPASS %s", ureg_p->name);

	password = get_crypt(parv[1], NULL);
	my_free(ureg_p->password);
	ureg_p->password = my_strdup(password);

	rsdb_exec(NULL, "UPDATE users SET password='%Q' WHERE username='%Q'", 
			password, ureg_p->name);

	service_snd(userserv_p, client_p, conn_p, SVC_USER_CHANGEDPASSWORD, ureg_p->name);

	return 0;
}

static int
o_user_usersetemail(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;

	if((ureg_p = find_user_reg(NULL, parv[0])) == NULL)
	{
		service_snd(userserv_p, client_p, conn_p, SVC_USER_NOTREG, parv[0]);
		return 0;
	}

	if(!valid_email(parv[1]))
	{
		service_snd(userserv_p, client_p, conn_p, SVC_EMAIL_INVALID, parv[1]);
		return 0;
	}

	zlog(userserv_p, 1, WATCH_USADMIN, 1, client_p, conn_p,
		"USERSETEMAIL %s", ureg_p->name);

	my_free(ureg_p->email);
	ureg_p->email = my_strdup(parv[1]);

	rsdb_exec(NULL, "UPDATE users SET email='%Q' WHERE username='%Q'", 
			parv[1], ureg_p->name);

	service_snd(userserv_p, client_p, conn_p, SVC_USER_CHANGEDOPTION, 
			ureg_p->name, "EMAIL", parv[1]);

	return 0;
}

static int
valid_email(const char *email)
{
	char *p;

	if(strlen(email) > EMAILLEN)
		return 0;

	/* no username, or no '@' */
	if(*email == '@' || (p = strchr(email, '@')) == NULL)
		return 0;

	p++;

	/* no host, or no '.' in host */
	if(EmptyString(p) || (p = strrchr(p, '.')) == NULL)
		return 0;

	p++;

	/* it ends in a '.' */
	if(EmptyString(p))
		return 0;

	return 1;
}

static int
valid_email_domain(const char *email)
{
	struct rsdb_table data;
	char *p;
	int retval = 1;

	if((p = strchr(email, '@')) == NULL)
		return 0;

	p++;

	rsdb_exec_fetch(&data, "SELECT COUNT(domain) FROM email_banned_domain WHERE LOWER(domain) = LOWER('%Q')", p);

	if(data.row_count == 0)
	{
		mlog("fatal error: SELECT COUNT() returned 0 rows in valid_email_domain()");
		die(0, "problem with db file");
	}

	if(atoi(data.row[0][0]) > 0)
		retval = 0;

	rsdb_exec_fetch_end(&data);

	return retval;
}

static int
s_user_register(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *reg_p;
	struct host_entry *hent = NULL;
	const char *password;
	const char *token = NULL;

	if(config_file.disable_uregister)
	{
		if(config_file.uregister_url)
			service_err(userserv_p, client_p, SVC_USER_REGISTERDISABLED,
					userserv_p->name, "REGISTER", config_file.uregister_url);
		else
			service_err(userserv_p, client_p, SVC_ISDISABLED,
					userserv_p->name, "REGISTER");

		return 1;
	}

	if(client_p->user->user_reg != NULL)
	{
		service_err(userserv_p, client_p, SVC_USER_ALREADYLOGGEDIN);
		return 1;
	}

	if((reg_p = find_user_reg(NULL, parv[0])) != NULL)
	{
		service_err(userserv_p, client_p, SVC_USER_ALREADYREG, parv[0]);
		return 1;
	}

	if(!valid_username(parv[0]))
	{
		service_err(userserv_p, client_p, SVC_USER_INVALIDUSERNAME, parv[0]);
		return 1;
	}

	if(strlen(parv[1]) > PASSWDLEN)
	{
		service_snd(userserv_p, client_p, conn_p, SVC_USER_LONGPASSWORD);
		return 0;
	}

	if(parc < 3 || EmptyString(parv[2]))
	{
		if(config_file.uregister_email)
		{
			service_err(userserv_p, client_p, SVC_NEEDMOREPARAMS,
					userserv_p->name, "REGISTER");
			return 1;
		}
	}
	else if(!valid_email(parv[2]))
	{
		service_err(userserv_p, client_p, SVC_EMAIL_INVALID, parv[2]);
		return 1;
	}
	else if(!valid_email_domain(parv[2]))
	{
		service_err(userserv_p, client_p, SVC_EMAIL_BANNEDDOMAIN);
		return 1;
	}

	/* apply timed registration limits */
	if(config_file.uregister_time && config_file.uregister_amount)
	{
		static time_t last_time = 0;
		static int last_count = 0;

		if((last_time + config_file.uregister_time) < CURRENT_TIME)
		{
			last_time = CURRENT_TIME;
			last_count = 1;
		}
		else if(last_count >= config_file.uregister_amount)
		{
			service_err(userserv_p, client_p, SVC_RATELIMITED,
					userserv_p->name, "REGISTER");
			return 1;
		}
		else
			last_count++;
	}

	/* check per host registration limits */
	if(config_file.uhregister_time && config_file.uhregister_amount)
	{
		hent = find_host(client_p->user->host);

		/* this host has gone over the limits.. */
		if(hent->uregister >= config_file.uhregister_amount &&
		   hent->uregister_expire > CURRENT_TIME)
		{
			service_err(userserv_p, client_p, SVC_RATELIMITEDHOST,
					userserv_p->name, "REGISTER");
			return 1;
		}

		/* its expired.. reset limits */
		if(hent->uregister_expire <= CURRENT_TIME)
		{
			hent->uregister_expire = CURRENT_TIME + config_file.uhregister_time;
			hent->uregister = 0;
		}

		/* dont penalise the individual user just because we cant send emails, so
		 * raise hent lower down..
		 */
	}

	if(config_file.uregister_verify)
	{
		if(config_file.disable_email)
		{
			service_err(userserv_p, client_p, SVC_ISDISABLEDEMAIL,
					userserv_p->name, "REGISTER");
			return 1;
		}

		if(!can_send_email())
		{
			service_err(userserv_p, client_p, SVC_EMAIL_TEMPUNAVAILABLE);
			return 1;
		}

		token = get_password();

		if(!send_email(parv[2], "Username registration verification",
				"The username %s has been registered to this email address "
				"by %s!%s@%s\n\n"
				"Your verification token is: %s\n\n"
				"To activate this account you must send %s ACTIVATE %s %s "
				"within %s\n",
				parv[0], client_p->name, client_p->user->username,
				client_p->user->host, token, userserv_p->name, parv[0], token,
				get_short_duration(config_file.uexpire_unverified_time)))
		{
			service_err(userserv_p, client_p, SVC_EMAIL_SENDFAILED,
					userserv_p->name, "REGISTER");
			return 1;
		}
	}

	if(hent)
		hent->uregister++;

	zlog(userserv_p, 2, WATCH_USREGISTER, 0, client_p, NULL,
		"REGISTER %s %s", parv[0], EmptyString(parv[2]) ? "" : parv[2]);

	password = get_crypt(parv[1], NULL);

	reg_p = BlockHeapAlloc(user_reg_heap);
	strcpy(reg_p->name, parv[0]);
	reg_p->password = my_strdup(password);

	if(!EmptyString(parv[2]))
		reg_p->email = my_strdup(parv[2]);

	reg_p->reg_time = reg_p->last_time = CURRENT_TIME;

	if(config_file.uregister_verify)
		reg_p->flags |= US_FLAGS_NEVERLOGGEDIN;

	add_user_reg(reg_p);

	rsdb_exec_insert(&reg_p->id, "users", "id",
			"INSERT INTO users (username, password, email, reg_time, last_time, flags, verify_token, language) "
			"VALUES('%Q', '%Q', '%Q', '%lu', '%lu', '%u', '%Q', '')",
			reg_p->name, reg_p->password, 
			EmptyString(reg_p->email) ? "" : reg_p->email, 
			reg_p->reg_time, reg_p->last_time, reg_p->flags, 
			EmptyString(token) ? "" : token);

	if(!config_file.uregister_verify)
	{
		dlink_add_alloc(client_p, &reg_p->users);
		client_p->user->user_reg = reg_p;

		sendto_server(":%s ENCAP * SU %s %s", 
				MYUID, UID(client_p), reg_p->name);

		service_err(userserv_p, client_p, SVC_USER_NOWREGLOGGEDIN, parv[0]);

		hook_call(HOOK_USER_LOGIN, client_p, NULL);
	}
	else
		service_err(userserv_p, client_p, SVC_USER_NOWREGEMAILED, parv[0]);

	return 5;
}

static int
s_user_activate(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;
	struct rsdb_table data;

	if(client_p->user->user_reg != NULL)
	{
		service_err(userserv_p, client_p, SVC_USER_ALREADYLOGGEDIN);
		return 1;
	}

	if((ureg_p = find_user_reg(client_p, parv[0])) == NULL)
		return 1;

	if((ureg_p->flags & US_FLAGS_NEVERLOGGEDIN) == 0)
	{
		service_err(userserv_p, client_p, SVC_USER_ACT_ALREADY, ureg_p->name);
		return 1;
	}

	rsdb_exec_fetch(&data, "SELECT verify_token FROM users WHERE username='%Q' LIMIT 1",
			ureg_p->name);

	if(!data.row_count || EmptyString(data.row[0][0]))
	{
		service_err(userserv_p, client_p, SVC_USER_TOKENBAD,
				ureg_p->name, "ACTIVATE");
		rsdb_exec_fetch_end(&data);
		return 1;
	}

	if(strcmp(data.row[0][0], parv[1]))
	{
		service_err(userserv_p, client_p, SVC_USER_TOKENMISMATCH,
				ureg_p->name, "ACTIVATE");
		rsdb_exec_fetch_end(&data);
		return 1;
	}

	rsdb_exec_fetch_end(&data);

	zlog(userserv_p, 5, 0, 0, client_p, NULL,
		"ACTIVATE %s", ureg_p->name);

	ureg_p->flags &= ~US_FLAGS_NEVERLOGGEDIN;

	rsdb_exec(NULL, "UPDATE users SET flags='%d' WHERE username='%Q'",
			ureg_p->flags, ureg_p->name);

	service_err(userserv_p, client_p, SVC_USER_ACT_COMPLETE, ureg_p->name);

	return 1;
}

static int
s_user_login(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *reg_p;
	const char *password;
	dlink_node *ptr;

	if(client_p->user->user_reg != NULL)
	{
		service_err(userserv_p, client_p, SVC_USER_ALREADYLOGGEDIN);
		return 1;
	}

	if((reg_p = find_user_reg(client_p, parv[0])) == NULL)
		return 1;

	if(reg_p->flags & US_FLAGS_SUSPENDED)
	{
		if(!USER_SUSPEND_EXPIRED(reg_p))
		{
			service_err(userserv_p, client_p, SVC_USER_LOGINSUSPENDED);
			return 1;
		}
		else
			expire_user_suspend(reg_p);
	}

	if(reg_p->flags & US_FLAGS_NEVERLOGGEDIN)
	{
		service_err(userserv_p, client_p, SVC_USER_LOGINUNACTIVATED,
				userserv_p->name);
		return 1;
	}

	if(config_file.umax_logins && 
	   dlink_list_length(&reg_p->users) >= config_file.umax_logins)
	{
		service_err(userserv_p, client_p, SVC_USER_LOGINMAX,
				config_file.umax_logins);
		return 1;
	}

	password = get_crypt(parv[1], reg_p->password);

	if(strcmp(password, reg_p->password))
	{
		service_err(userserv_p, client_p, SVC_USER_INVALIDPASSWORD);
		return 1;
	}

	zlog(userserv_p, 5, 0, 0, client_p, NULL,
		"LOGIN %s", reg_p->name);

	DLINK_FOREACH(ptr, reg_p->users.head)
	{
		service_err(userserv_p, ptr->data, SVC_USER_USERLOGGEDIN,
				client_p->user->mask, reg_p->name);
	}

	sendto_server(":%s ENCAP * SU %s %s",
			MYUID, UID(client_p), reg_p->name);

	client_p->user->user_reg = reg_p;
	reg_p->last_time = CURRENT_TIME;
	reg_p->flags |= US_FLAGS_NEEDUPDATE;
	dlink_add_alloc(client_p, &reg_p->users);
	service_err(userserv_p, client_p, SVC_SUCCESSFUL,
			userserv_p->name, "LOGIN");

	hook_call(HOOK_USER_LOGIN, client_p, NULL);

	return 1;
}

static int
s_user_logout(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	dlink_find_destroy(client_p, &client_p->user->user_reg->users);
	client_p->user->user_reg = NULL;
	service_err(userserv_p, client_p, SVC_SUCCESSFUL,
			userserv_p->name, "LOGOUT");

	sendto_server(":%s ENCAP * SU %s", MYUID, UID(client_p));

	return 1;
}

static int
s_user_resetpass(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct rsdb_table data;
	struct user_reg *reg_p;

	if(config_file.disable_email || !config_file.allow_resetpass)
	{
		service_err(userserv_p, client_p, SVC_ISDISABLED,
				userserv_p->name, "RESETPASS");
		return 1;
	}

	if(client_p->user->user_reg != NULL)
	{
		service_err(userserv_p, client_p, SVC_USER_RP_LOGGEDIN);
		return 1;
	}

	if((reg_p = find_user_reg(client_p, parv[0])) == NULL)
		return 1;

	if((CURRENT_TIME - reg_p->reg_time) < config_file.ureset_regtime_duration)
	{
		service_err(userserv_p, client_p, SVC_USER_DURATIONTOOSHORT,
				reg_p->name, userserv_p->name, "RESETPASS");
		return 1;
	}

	if(reg_p->flags & US_FLAGS_SUSPENDED)
	{
		if(!USER_SUSPEND_EXPIRED(reg_p))
		{
			service_err(userserv_p, client_p, SVC_USER_SUSPENDED, reg_p->name);
			return 1;
		}
		else
			expire_user_suspend(reg_p);
	}

	/* initial password reset */
	if(EmptyString(parv[1]))
	{
		const char *token;

		if(EmptyString(reg_p->email))
		{
			service_err(userserv_p, client_p, SVC_USER_NOEMAIL, reg_p->name);
			return 1;
		}

		rsdb_exec_fetch(&data, "SELECT COUNT(username) FROM users_resetpass WHERE username='%Q' AND time > '%lu'",
				reg_p->name, CURRENT_TIME - config_file.uresetpass_duration);

		if(data.row_count == 0)
		{
			mlog("fatal error: SELECT COUNT() returned 0 rows in s_user_resetpass()");
			die(0, "problem with db file");
		}

		/* already issued one within the past day.. */
		if(atoi(data.row[0][0]))
		{
			service_err(userserv_p, client_p, SVC_USER_REQUESTPENDING,
					reg_p->name, "RESETPASS");
			rsdb_exec_fetch_end(&data);
			return 1;
		}

		rsdb_exec_fetch_end(&data);

		if(!can_send_email())
		{
			service_err(userserv_p, client_p, SVC_EMAIL_TEMPUNAVAILABLE);
			return 1;
		}

		zlog(userserv_p, 3, 0, 0, client_p, NULL,
			"RESETPASS %s", reg_p->name);

		/* perform a blind delete here, as there may be an entry
		 * still in the table, just expired and so uncaught by the
		 * above select --fl
		 */
		rsdb_exec(NULL, "DELETE FROM users_resetpass WHERE username='%Q'", reg_p->name);

		token = get_password();
		rsdb_exec(NULL, "INSERT INTO users_resetpass (username, token, time) VALUES('%Q', '%Q', '%lu')",
				reg_p->name, token, CURRENT_TIME);

		if(!send_email(reg_p->email, "Password reset",
				"%s!%s@%s has requested a password reset for username %s which "
				"is registered to this email address.\n\n"
				"To authenticate this request, send %s RESETPASS %s %s <new_password>\n\n"
				"If you did not request this, simply ignore this message, no "
				"action will be taken on your account and your password will "
				"NOT be reset.\n",
				client_p->name, client_p->user->username, client_p->user->host,
				reg_p->name, userserv_p->name, reg_p->name, token))
		{
			service_err(userserv_p, client_p, SVC_EMAIL_SENDFAILED,
					userserv_p->name, "RESETPASS");
		}
		else
		{
			service_err(userserv_p, client_p, SVC_USER_REQUESTISSUED,
					reg_p->name, "RESETPASS");
		}

		
			
		return 2;
	}

	if(EmptyString(parv[2]))
	{
		service_err(userserv_p, client_p, SVC_NEEDMOREPARAMS,
				userserv_p->name, "RESETPASS");
		return 1;
	}

	if(strlen(parv[2]) > PASSWDLEN)
	{
		service_err(userserv_p, client_p, SVC_USER_LONGPASSWORD);
		return 1;
	}

	zlog(userserv_p, 3, 0, 0, client_p, NULL,
		"RESETPASS %s (auth)", reg_p->name);

	/* authenticating a password reset */
	rsdb_exec_fetch(&data, "SELECT token FROM users_resetpass WHERE username='%Q' AND time > '%lu'",
			reg_p->name, CURRENT_TIME - config_file.uresetpass_duration);

	/* ok, found the entry.. */
	if(data.row_count)
	{
		if(strcmp(data.row[0][0], parv[1]) == 0)
		{
			const char *password = get_crypt(parv[2], NULL);

			/* need to execute another query.. */
			rsdb_exec_fetch_end(&data);

			rsdb_exec(NULL, "DELETE FROM users_resetpass WHERE username='%Q'",
					reg_p->name);
			rsdb_exec(NULL, "UPDATE users SET password='%Q' WHERE username='%Q'",
					password, reg_p->name);

			my_free(reg_p->password);
			reg_p->password = my_strdup(password);

			service_err(userserv_p, client_p, SVC_USER_CHANGEDPASSWORD, reg_p->name);
			return 1;
		}
		else
			service_err(userserv_p, client_p, SVC_USER_TOKENMISMATCH,
					reg_p->name, "RESETPASS");
	}
	else
		service_err(userserv_p, client_p, SVC_USER_REQUESTNONE,
				reg_p->name, "RESETPASS");

	rsdb_exec_fetch_end(&data);
	return 1;
}

static int
s_user_resetemail(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct rsdb_table data;
	struct user_reg *reg_p;
	const char *token;

	reg_p = client_p->user->user_reg;

	if(config_file.disable_email || !config_file.allow_resetemail)
	{
		service_err(userserv_p, client_p, SVC_ISDISABLED,
				userserv_p->name, "RESETEMAIL");
		return 1;
	}

	if((CURRENT_TIME - reg_p->reg_time) < config_file.ureset_regtime_duration)
	{
		service_err(userserv_p, client_p, SVC_USER_DURATIONTOOSHORT,
				reg_p->name, userserv_p->name, "RESETEMAIL");
		return 1;
	}

	if(EmptyString(reg_p->email))
	{
		service_err(userserv_p, client_p, SVC_USER_NOEMAIL, reg_p->name);
		return 1;
	}

	/* initial email reset. no params. */
	if(parc == 0)
	{
		rsdb_exec_fetch(&data, "SELECT COUNT(username) FROM users_resetemail WHERE username='%Q' AND time > '%lu'",
				reg_p->name, CURRENT_TIME - config_file.uresetemail_duration);

		if(data.row_count == 0)
		{
			mlog("fatal error: SELECT COUNT() returned 0 rows in s_user_resetemail()");
			die(0, "problem with db file");
		}

		/* already issued one within the past day.. */
		if(atoi(data.row[0][0]))
		{
			service_err(userserv_p, client_p, SVC_USER_REQUESTPENDING,
					reg_p->name, "RESETEMAIL");
			rsdb_exec_fetch_end(&data);
			return 1;
		}

		rsdb_exec_fetch_end(&data);

		if(!can_send_email())
		{
			service_err(userserv_p, client_p, SVC_EMAIL_TEMPUNAVAILABLE);
			return 1;
		}

		zlog(userserv_p, 3, 0, 0, client_p, NULL, "RESETEMAIL");

		/* may be one still there thats expired */
		rsdb_exec(NULL, "DELETE FROM users_resetemail WHERE username='%Q'", reg_p->name);

		/* insert email as blank, as we need to check for it in AUTH */
		token = get_password();
		rsdb_exec(NULL, "INSERT INTO users_resetemail (username, token, time) VALUES('%Q', '%Q', '%lu')",
				reg_p->name, token, CURRENT_TIME);

		if(!send_email(reg_p->email, "E-Mail reset",
				"%s!%s@%s has requested a e-mail reset for username %s which "
				"is registered to this email address.\n\n"
				"To authenticate this request, send %s RESETEMAIL CONFIRM %s <new_e-mail>\n\n"
				"If you did not request this, simply ignore this message, no "
				"action will be taken on your account and your e-mail will "
				"NOT be reset.\n",
				client_p->name, client_p->user->username, client_p->user->host,
				reg_p->name, userserv_p->name, token))
		{
			service_err(userserv_p, client_p, SVC_EMAIL_SENDFAILED,
					userserv_p->name, "RESETEMAIL");
		}
		else
		{
			service_err(userserv_p, client_p, SVC_USER_REQUESTISSUED,
					reg_p->name, "RESETEMAIL");
		}

		return 2;
	}
	/* confirm from old email */
	else if(!strcasecmp(parv[0], "CONFIRM"))
	{
		if(EmptyString(parv[1]) || EmptyString(parv[2]))
		{
			service_err(userserv_p, client_p, SVC_NEEDMOREPARAMS,
					userserv_p->name, "RESETEMAIL");
			return 1;
		}

		if(!valid_email(parv[2]))
		{
			service_err(userserv_p, client_p, SVC_EMAIL_INVALID, parv[2]);
			return 1;
		}
		else if(!valid_email_domain(parv[2]))
		{
			service_err(userserv_p, client_p, SVC_EMAIL_BANNEDDOMAIN);
			return 1;
		}

		rsdb_exec_fetch(&data, "SELECT token FROM users_resetemail WHERE username='%Q' AND time > '%lu' AND email is NULL",
				reg_p->name, CURRENT_TIME - config_file.uresetemail_duration);

		/* ok, found the entry.. */
		if(data.row_count)
		{
			if(strcmp(data.row[0][0], parv[1]) == 0)
			{
				const char *email = parv[2];
				token = get_password();

				rsdb_exec_fetch_end(&data);

				if(!send_email(email, "E-Mail reset",
						"%s!%s@%s has requested an e-mail change for username %s to "
						"this email address.\n\n"
						"To authenticate this request, send %s RESETEMAIL AUTH %s\n\n"
						"If you did not request this, simply ignore this message, no "
						"action will be taken.\n",
						client_p->name, client_p->user->username, client_p->user->host,
						reg_p->name, userserv_p->name, token))
				{
					service_err(userserv_p, client_p, SVC_EMAIL_SENDFAILED,
							userserv_p->name, "RESETEMAIL");
					return 2;
				}

				service_err(userserv_p, client_p, SVC_USER_REQUESTISSUED,
						reg_p->name, "RESETEMAIL");

				rsdb_exec(NULL, "DELETE FROM users_resetemail WHERE username='%Q'",
						reg_p->name);
				rsdb_exec(NULL, "INSERT INTO users_resetemail (username, token, time, email) VALUES('%Q', '%Q', '%lu', '%Q')",
						reg_p->name, token, CURRENT_TIME, email);

				zlog(userserv_p, 3, 0, 0, client_p, NULL,
						"RESETEMAIL %s %s (confirm)", reg_p->name, email);
				return 2;
			}
			else
			{
				service_err(userserv_p, client_p, SVC_USER_TOKENMISMATCH,
						reg_p->name, "RESETEMAIL");
				zlog(userserv_p, 3, 0, 0, client_p, NULL,
						"RESETEMAIL %s (confirm failed)", reg_p->name);
			}
		}
		else
		{
			service_err(userserv_p, client_p, SVC_USER_REQUESTNONE,
					reg_p->name, "RESETEMAIL::CONFIRM");
		}

		rsdb_exec_fetch_end(&data);
		return 2;
	}
	/* confirm from new email */
	else if(!strcasecmp(parv[0], "AUTH"))
	{
		if(EmptyString(parv[1]))
		{
			service_err(userserv_p, client_p, SVC_NEEDMOREPARAMS,
					userserv_p->name, "RESETEMAIL");
			return 1;
		}

		rsdb_exec_fetch(&data, "SELECT token, email FROM users_resetemail WHERE username='%Q' AND time > '%lu' AND email is not NULL",
				reg_p->name, CURRENT_TIME - config_file.uresetemail_duration);

		/* ok, found the entry.. */
		if(data.row_count)
		{
			if(strcmp(data.row[0][0], parv[1]) == 0)
			{
				const char *email = data.row[0][1];
				
				my_free(reg_p->email);
				reg_p->email = my_strdup(email);

				/* need to execute another query.. */
				rsdb_exec_fetch_end(&data);

				rsdb_exec(NULL, "DELETE FROM users_resetemail WHERE username='%Q'",
						reg_p->name);
				rsdb_exec(NULL, "UPDATE users SET email='%Q' WHERE username='%Q'",
						reg_p->email, reg_p->name);

				zlog(userserv_p, 3, 0, 0, client_p, NULL,
						"RESETEMAIL %s (auth)", reg_p->name);

				service_err(userserv_p, client_p, SVC_USER_CHANGEDOPTION, 
						reg_p->name, "EMAIL", reg_p->email);

				return 1;
			}
			else
			{
				service_err(userserv_p, client_p, SVC_USER_TOKENMISMATCH,
						reg_p->name, "RESETEMAIL");
			}
		}
		else
		{
			service_err(userserv_p, client_p, SVC_USER_REQUESTNONE,
					reg_p->name, "RESETEMAIL::AUTH");
		}

		rsdb_exec_fetch_end(&data);
		return 2;
	}

	service_err(userserv_p, client_p, SVC_OPTIONINVALID,
			userserv_p->name, "RESETEMAIL");
	return 1;
}

static int
s_user_set(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;
	const char *arg;

	ureg_p = client_p->user->user_reg;

	arg = EmptyString(parv[1]) ? "" : parv[1];

	if(!strcasecmp(parv[0], "PASSWORD"))
	{
		const char *password;

		if(!config_file.allow_set_password)
		{
			service_err(userserv_p, client_p, SVC_ISDISABLED,
				userserv_p->name, "SET::PASSWORD");
			return 1;
		}

		if(EmptyString(parv[1]) || EmptyString(parv[2]))
		{
			service_err(userserv_p, client_p, SVC_NEEDMOREPARAMS,
					userserv_p->name, "SET::PASSWORD");
			return 1;
		}

		if(strlen(parv[2]) > PASSWDLEN)
		{
			service_snd(userserv_p, client_p, conn_p, SVC_USER_LONGPASSWORD);
			return 0;
		}

		password = get_crypt(parv[1], ureg_p->password);

		if(strcmp(password, ureg_p->password))
		{
			service_err(userserv_p, client_p, SVC_USER_INVALIDPASSWORD);
			return 1;
		}

		zlog(userserv_p, 3, 0, 0, client_p, NULL, "SET PASS");

		password = get_crypt(parv[2], NULL);
		my_free(ureg_p->password);
		ureg_p->password = my_strdup(password);

		rsdb_exec(NULL, "UPDATE users SET password='%Q' "
				"WHERE username='%Q'", password, ureg_p->name);

		service_err(userserv_p, client_p, SVC_USER_CHANGEDPASSWORD,
				ureg_p->name);
		return 1;
	}
	else if(!strcasecmp(parv[0], "EMAIL"))
	{
		if(!config_file.allow_set_email)
		{
			service_err(userserv_p, client_p, SVC_ISDISABLED,
					userserv_p->name, "SET::EMAIL");
			return 1;
		}

		if(EmptyString(arg))
		{
			service_err(userserv_p, client_p, SVC_NEEDMOREPARAMS,
					userserv_p->name, "SET::EMAIL");
			return 1;
		}

		if(!valid_email(arg))
		{
			service_err(userserv_p, client_p, SVC_EMAIL_INVALID, arg);
			return 1;
		}
		else if(!valid_email_domain(arg))
		{
			service_err(userserv_p, client_p, SVC_EMAIL_BANNEDDOMAIN);
			return 1;
		}

		zlog(userserv_p, 3, 0, 0, client_p, NULL, 
			"SET EMAIL %s", arg);

		my_free(ureg_p->email);
		ureg_p->email = my_strdup(arg);

		rsdb_exec(NULL, "UPDATE users SET email='%Q' "
				"WHERE username='%Q'", arg, ureg_p->name);

		service_err(userserv_p, client_p, SVC_USER_CHANGEDOPTION,
				ureg_p->name, "EMAIL", arg);
		return 1;
	}
	else if(!strcasecmp(parv[0], "PRIVATE"))
	{
		if(!strcasecmp(arg, "ON"))
			ureg_p->flags |= US_FLAGS_PRIVATE;
		else if(!strcasecmp(arg, "OFF"))
			ureg_p->flags &= ~US_FLAGS_PRIVATE;
		else
		{
			service_err(userserv_p, client_p, SVC_USER_QUERYOPTION,
					ureg_p->name, "PRIVATE", 
					(ureg_p->flags & US_FLAGS_PRIVATE) ? "ON" : "OFF");
			return 1;
		}

		service_err(userserv_p, client_p, SVC_USER_CHANGEDOPTION,
				ureg_p->name, "PRIVATE",
				(ureg_p->flags & US_FLAGS_PRIVATE) ? "ON" : "OFF");

		rsdb_exec(NULL, "UPDATE users SET flags='%d' WHERE username='%Q'",
				ureg_p->flags, ureg_p->name);
		return 1;
	}
	else if(!strcasecmp(parv[0], "NOACCESS"))
	{
		if(!strcasecmp(arg, "ON"))
			ureg_p->flags |= US_FLAGS_NOACCESS;
		else if(!strcasecmp(arg, "OFF"))
			ureg_p->flags &= ~US_FLAGS_NOACCESS;
		else
		{
			service_err(userserv_p, client_p, SVC_USER_QUERYOPTION,
					ureg_p->name, "NOACCESS",
					(ureg_p->flags & US_FLAGS_NOACCESS) ? "ON" : "OFF");
			return 1;
		}

		service_err(userserv_p, client_p, SVC_USER_CHANGEDOPTION,
				ureg_p->name, "NOACCESS",
				(ureg_p->flags & US_FLAGS_NOACCESS) ? "ON" : "OFF");

		rsdb_exec(NULL, "UPDATE users SET flags='%d' WHERE username='%Q'",
				ureg_p->flags, ureg_p->name);
		return 1;
	}
	else if(!strcasecmp(parv[0], "NOMEMOS"))
	{
		if(!strcasecmp(arg, "ON"))
			ureg_p->flags |= US_FLAGS_NOMEMOS;
		else if(!strcasecmp(arg, "OFF"))
			ureg_p->flags &= ~US_FLAGS_NOMEMOS;
		else
		{
			service_err(userserv_p, client_p, SVC_USER_QUERYOPTION,
					ureg_p->name, "NOMEMOS",
					(ureg_p->flags & US_FLAGS_NOMEMOS) ? "ON" : "OFF");
			return 1;
		}

		service_err(userserv_p, client_p, SVC_USER_CHANGEDOPTION,
				ureg_p->name, "NOMEMOS",
				(ureg_p->flags & US_FLAGS_NOMEMOS) ? "ON" : "OFF");

		rsdb_exec(NULL, "UPDATE users SET flags='%d' WHERE username='%Q'",
				ureg_p->flags, ureg_p->name);
		return 1;
	}
	else if(!strcasecmp(parv[0], "LANGUAGE"))
	{
		int i;

		if(EmptyString(arg))
		{
			service_err(userserv_p, client_p, SVC_USER_QUERYOPTION,
					ureg_p->name, "LANGUAGE", 
					langs_available[ureg_p->language]);
			return 1;
		}

		for(i = 0; langs_available[i]; i++)
		{
			if(!strcasecmp(arg, langs_available[i]))
				break;
		}

		if(langs_available[i] == NULL)
		{
			service_err(userserv_p, client_p, SVC_USER_INVALIDLANGUAGE, arg);
			return 1;
		}

		ureg_p->language = i;

		service_err(userserv_p, client_p, SVC_USER_CHANGEDOPTION,
				ureg_p->name, "LANGUAGE", langs_available[i]);

		rsdb_exec(NULL, "UPDATE users SET language='%Q' WHERE username='%Q'",
				arg, ureg_p->name);
		return 1;
	}

	service_err(userserv_p, client_p, SVC_OPTIONINVALID,
			userserv_p->name, "SET");
	return 1;
}

static void
dump_user_info(struct client *client_p, struct lconn *conn_p, struct user_reg *ureg_p)
{
	char buf[BUFSIZE];
	struct member_reg *mreg_p;
#ifdef ENABLE_NICKSERV
	struct nick_reg *nreg_p;
#endif
	struct client *target_p;
	dlink_node *ptr;
	char *p;
	int buflen = 0;
	int mlen;

	p = buf;

	DLINK_FOREACH(ptr, ureg_p->channels.head)
	{
		mreg_p = ptr->data;

		/* "Access to: " + " 200, " */
		if((buflen + strlen(mreg_p->channel_reg->name) + 17) >= (BUFSIZE - 3))
		{
			service_snd(userserv_p, client_p, conn_p, SVC_INFO_ACCESSLIST,
					ureg_p->name, buf);
			p = buf;
			buflen = 0;
		}

		mlen = sprintf(p, "%s%s %d",
				(buflen ? ", " : ""),
				mreg_p->channel_reg->name, mreg_p->level);

		buflen += mlen;
		p += mlen;
	}

	/* could have access to no channels.. */
	if(buflen)
		service_snd(userserv_p, client_p, conn_p, SVC_INFO_ACCESSLIST,
				ureg_p->name, buf);

#ifdef ENABLE_NICKSERV
	p = buf;
	buflen = 0;

	DLINK_FOREACH(ptr, ureg_p->nicks.head)
	{
		nreg_p = ptr->data;

		/* "Registered nicknames: " + " " */
		if((buflen + strlen(nreg_p->name) + 25) >= (BUFSIZE - 3))
		{
			service_snd(userserv_p, client_p, conn_p, SVC_INFO_NICKNAMES,
					ureg_p->name, buf);
			p = buf;
			buflen = 0;
		}

		mlen = sprintf(p, "%s ", nreg_p->name);
		buflen += mlen;
		p += mlen;
	}

	if(buflen)
		service_snd(userserv_p, client_p, conn_p, SVC_INFO_NICKNAMES,
				ureg_p->name, buf);
#endif

	if(!EmptyString(ureg_p->email))
		service_snd(userserv_p, client_p, conn_p, SVC_INFO_EMAIL,
				ureg_p->name, ureg_p->email);

	if(dlink_list_length(&ureg_p->users))
	{
		service_snd(userserv_p, client_p, conn_p, SVC_INFO_CURRENTLOGON,
				ureg_p->name);

		DLINK_FOREACH(ptr, ureg_p->users.head)
		{
			target_p = ptr->data;

			service_send(userserv_p, client_p, conn_p,
					"[%s]  %s", ureg_p->name, target_p->user->mask);
		}
	}
}

static int
s_user_info(struct client *client_p, struct lconn *conn_p, const char *parv[], int parc)
{
	struct user_reg *ureg_p;

	if((ureg_p = find_user_reg_nick(client_p, parv[0])) == NULL)
		return 1;

	service_err(userserv_p, client_p, SVC_INFO_REGDURATIONUSER,
			ureg_p->name,
			get_duration((time_t) (CURRENT_TIME - ureg_p->reg_time)));

	if(USER_SUSPEND_EXPIRED(ureg_p))
		expire_user_suspend(ureg_p);

	if(ureg_p->flags & US_FLAGS_SUSPENDED)
	{
		time_t suspend_time = ureg_p->suspend_time;

		if(suspend_time)
			suspend_time -= CURRENT_TIME;

		if(config_file.ushow_suspend_reasons)
			service_err(userserv_p, client_p, SVC_INFO_SUSPENDEDADMIN,
					ureg_p->name,
					suspend_time ? get_short_duration(suspend_time) : "never",
					": ", ureg_p->suspend_reason ? ureg_p->suspend_reason : "");
		else
			service_err(userserv_p, client_p, SVC_INFO_SUSPENDEDADMIN,
					ureg_p->name, 
					suspend_time ? get_short_duration(suspend_time) : "never",
					"", "");
	}
	else if(ureg_p == client_p->user->user_reg)
	{
		dump_user_info(client_p, NULL, ureg_p);
		return 3;
	}

	return 1;
}

void
s_userserv_countmem(size_t *sz_user_reg_password, size_t *sz_user_reg_email,
		size_t *sz_user_reg_suspend, size_t *sz_member_reg_lastmod)
{
	struct user_reg *ureg_p;
	struct member_reg *mreg_p;
	dlink_node *ptr, *vptr;
	int i;

	HASH_WALK(i, MAX_NAME_HASH, ptr, user_reg_table)
	{
		ureg_p = ptr->data;

		if(!EmptyString(ureg_p->password))
			*sz_user_reg_password += strlen(ureg_p->password) + 1;

		if(!EmptyString(ureg_p->email))
			*sz_user_reg_email += strlen(ureg_p->email) + 1;

		if(!EmptyString(ureg_p->suspender))
			*sz_user_reg_suspend += strlen(ureg_p->suspender) + 1;

		if(!EmptyString(ureg_p->suspend_reason))
			*sz_user_reg_suspend += strlen(ureg_p->suspend_reason) + 1;

		DLINK_FOREACH(vptr, ureg_p->channels.head)
		{
			mreg_p = vptr->data;
			*sz_member_reg_lastmod += strlen(mreg_p->lastmod) + 1;
		}
	}
	HASH_WALK_END
}

#endif
