/* This code is in the public domain.
 * $Id: newconf.c 27003 2010-03-21 13:08:59Z leeh $
 */

#include "stdinc.h"
#include "tools.h"
#include "newconf.h"
#include "rserv.h"
#include "langs.h"
#include "client.h"
#include "channel.h"
#include "log.h"
#include "conf.h"
#include "service.h"
#include "io.h"
#include "event.h"

#define CF_TYPE(x) ((x) & CF_MTYPE)

struct TopConf *conf_cur_block;
static char *conf_cur_block_name;

static dlink_list conf_items;

struct conf_server *yy_server;
struct conf_oper *yy_oper;
struct conf_oper *yy_tmpoper;
static dlink_list yy_oper_list;

struct client *yy_service;

struct mode_table
{
	const char *name;
	int mode;
};

static struct mode_table privs_table[] = {
	{ "admin",	CONF_OPER_ADMIN		},
	{ "dcc",	CONF_OPER_DCC		},
	{ "route",	CONF_OPER_ROUTE		},
	{ "\0",		0			}
};

static struct mode_table userserv_table[] = {
	{ "admin",	CONF_OPER_US_ADMIN	},
	{ "oper",	CONF_OPER_US_OPER	},
	{ "register",	CONF_OPER_US_REGISTER	},
	{ "suspend",	CONF_OPER_US_SUSPEND	},
	{ "drop",	CONF_OPER_US_DROP	},
	{ "list",	CONF_OPER_US_LIST	},
	{ "info",	CONF_OPER_US_INFO	},
	{ "setpass",	CONF_OPER_US_SETPASS	},
	{ "setemail",	CONF_OPER_US_SETEMAIL	},
	{ "\0",		0			}
};

static struct mode_table chanserv_table[] = {
	{ "admin",	CONF_OPER_CS_ADMIN	},
	{ "oper",	CONF_OPER_CS_OPER	},
	{ "register",	CONF_OPER_CS_REGISTER	},
	{ "suspend",	CONF_OPER_CS_SUSPEND	},
	{ "drop",	CONF_OPER_CS_DROP	},
	{ "list",	CONF_OPER_CS_LIST	},
	{ "info",	CONF_OPER_CS_INFO	},
	{ "\0",		0			}
};

static struct mode_table nickserv_table[] = {
	{ "drop",	CONF_OPER_NS_DROP	},
	{ "\0",		0			}
};

static struct mode_table operserv_table[] = {
	{ "maintain",	CONF_OPER_OS_MAINTAIN	},
	{ "ignore",	CONF_OPER_OS_IGNORE	},
	{ "admin",	CONF_OPER_OS_ADMIN	},
	{ "channel",	CONF_OPER_OS_CHANNEL	},
	{ "takeover",	CONF_OPER_OS_TAKEOVER	},
	{ "osmode",	CONF_OPER_OS_OMODE	},
	{ "\0",		0			}
};

static struct mode_table operbot_table[] = {
	{ "channel",	CONF_OPER_OB_CHANNEL	},
	{ "\0",		0			}
};

static struct mode_table global_table[] = {
	{ "netmsg",	CONF_OPER_GLOB_NETMSG	},
	{ "welcome",	CONF_OPER_GLOB_WELCOME	},
	{ "\0",		0			}
};

static struct mode_table jupeserv_table[] = {
	{ "jupe",	CONF_OPER_JS_JUPE	},
	{ "\0",		0			}
};

static struct mode_table banserv_table[] = {
	{ "kline",	CONF_OPER_BAN_KLINE	},
	{ "xline",	CONF_OPER_BAN_XLINE	},
	{ "resv",	CONF_OPER_BAN_RESV	},
	{ "regexp",	CONF_OPER_BAN_REGEXP	},
	{ "perm",	CONF_OPER_BAN_PERM	},
	{ "remove",	CONF_OPER_BAN_REMOVE	},
	{ "sync",	CONF_OPER_BAN_SYNC	},
	{ "nomax",	CONF_OPER_BAN_NOMAX	},
	{ "\0", 0 }
};

static struct mode_table service_flags_table[] = {
	{ "opered",	SERVICE_OPERED		},
	{ "msg_self",	SERVICE_MSGSELF		},
	{ "disabled",	SERVICE_DISABLED	},
	{ "short_help",	SERVICE_SHORTHELP	},
	{ "stealth",	SERVICE_STEALTH		},
	{ "login_help",	SERVICE_LOGINHELP	},
	{ "wallop_adm",	SERVICE_WALLOPADM	},
	{ "require_shortcut", SERVICE_SHORTCUT	},
	{ "\0",		0			}
};

static const char *
conf_strtype(int type)
{
	switch (type & CF_MTYPE)
	{
	case CF_INT:
		return "integer value";
	case CF_STRING:
		return "unquoted string";
	case CF_YESNO:
		return "yes/no value";
	case CF_QSTRING:
		return "quoted string";
	case CF_TIME:
		return "time/size value";
	default:
		return "unknown type";
	}
}

int
add_top_conf(const char *name, int (*sfunc) (struct TopConf *),
		int (*efunc) (struct TopConf *), struct ConfEntry *items)
{
	struct TopConf *tc;

	tc = my_malloc(sizeof(struct TopConf));

	tc->tc_name = my_strdup(name);
	tc->tc_sfunc = sfunc;
	tc->tc_efunc = efunc;
	tc->tc_entries = items;

	dlink_add_alloc(tc, &conf_items);
	return 0;
}

static struct TopConf *
find_top_conf(const char *name)
{
	dlink_node *d;
	struct TopConf *tc;

	DLINK_FOREACH(d, conf_items.head)
	{
		tc = d->data;
		if(strcasecmp(tc->tc_name, name) == 0)
			return tc;
	}

	return NULL;
}

static void
add_conf_extension(const char *top, const char *name, struct ConfEntry *items)
{
	struct TopConf *tc;
	struct ConfExtension *ext;

	if((tc = find_top_conf(top)) == NULL)
		return;

	ext = my_malloc(sizeof(struct ConfExtension));

	ext->name = my_strdup(name);
	ext->items = items;

	dlink_add_alloc(ext, &tc->extensions);
}

static struct ConfEntry *
find_conf_item(const struct TopConf *top, const char *name)
{
	dlink_node *ptr;
	struct ConfEntry *cf;
	struct ConfExtension *ext;

	if(top->tc_entries)
	{
		int i;

		for(i = 0; top->tc_entries[i].cf_type; i++)
		{
			if(!strcasecmp(top->tc_entries[i].cf_name, name))
				return &top->tc_entries[i];
		}
	}

	DLINK_FOREACH(ptr, top->tc_items.head)
	{
		cf = ptr->data;
		if(strcasecmp(cf->cf_name, name) == 0)
			return cf;
	}

	if(EmptyString(conf_cur_block_name))
		return NULL;

	DLINK_FOREACH(ptr, top->extensions.head)
	{
		ext = ptr->data;

		if(!strcasecmp(ext->name, conf_cur_block_name))
		{
			int i;

			for(i = 0; ext->items[i].cf_type; i++)
			{
				if(!strcasecmp(ext->items[i].cf_name, name))
					return &ext->items[i];
			}
		}
	}

	return NULL;
}

static int
find_umode(struct mode_table *tab, char *name)
{
	int i;

	for (i = 0; tab[i].name; i++)
	{
		if(strcmp(tab[i].name, name) == 0)
			return tab[i].mode;
	}

	return 0;
}

static void
set_modes_from_table(unsigned int *modes, const char *whatis, struct mode_table *tab, conf_parm_t * args)
{
	for (; args; args = args->next)
	{
		int mode;

		if((args->type & CF_MTYPE) != CF_STRING)
		{
			conf_report_error("Warning -- %s is not a string; ignoring.", whatis);
			continue;
		}

		mode = find_umode(tab, args->v.string);

		if(!mode)
		{
			conf_report_error("Warning -- unknown %s %s.", whatis, args->v.string);
			continue;
		}

		*modes |= mode;
	}
}

void
conf_report_error(const char *fmt, ...)
{
	va_list ap;
	char msg[BUFSIZE + 1] = { 0 };
	extern char *current_file;

	va_start(ap, fmt);
	vsnprintf(msg, BUFSIZE, fmt, ap);
	va_end(ap);

	if(testing_conf)
		fprintf(stderr, "\"%s\", line %d: %s\n",
			current_file, lineno+1, msg);
	else
		mlog("conf error: \"%s\", line %d: %s", 
			current_file, lineno + 1, msg);
}

int
conf_start_block(const char *block, const char *name)
{
	if((conf_cur_block = find_top_conf(block)) == NULL)
	{
		conf_report_error("Configuration block '%s' is not defined.", block);
		return -1;
	}

	if(name)
		conf_cur_block_name = my_strdup(name);
	else
		conf_cur_block_name = NULL;

	if(conf_cur_block->tc_sfunc)
		if(conf_cur_block->tc_sfunc(conf_cur_block) < 0)
			return -1;

	return 0;
}

int
conf_end_block(struct TopConf *tc)
{
	if(tc->tc_efunc)
		return tc->tc_efunc(tc);

	my_free(conf_cur_block_name);
	return 0;
}

static void
conf_set_generic_int(void *data, void *location)
{
	int val = *((int *) data);

	if(val >= 0)
		*((int *) location) = val;
#if 0
	*((int *) location) = *((unsigned int *) data);
#endif
}

static void
conf_set_generic_string(void *data, int len, void *location)
{
	char **loc = location;
	char *input = data;

	if(len > 0 && strlen(input) > len)
		input[len] = '\0';

	my_free(*loc);
	*loc = my_strdup(input);
}


int
conf_call_set(struct TopConf *tc, const char *item, conf_parm_t * value, int type)
{
	struct ConfEntry *cf;
	conf_parm_t *cp;

	if(!tc)
		return -1;

	if((cf = find_conf_item(tc, item)) == NULL)
	{
		conf_report_error("Non-existant configuration setting %s::%s.",
				  tc->tc_name, (char *) item);
		return -1;
	}

	/* if it takes one thing, make sure they only passed one thing,
	   and handle as needed. */
	if(value->type & CF_FLIST && !(cf->cf_type & CF_FLIST))
	{
		conf_report_error("Option %s::%s does not take a list of values.",
				  tc->tc_name, item);
		return -1;
	}

	cp = value->v.list;


	if(CF_TYPE(value->v.list->type) != CF_TYPE(cf->cf_type))
	{
		/* if it expects a string value, but we got a yesno, 
		 * convert it back
		 */
		if((CF_TYPE(value->v.list->type) == CF_YESNO) &&
		   (CF_TYPE(cf->cf_type) == CF_STRING))
		{
			value->v.list->type = CF_STRING;

			if(cp->v.number == 1)
				cp->v.string = my_strdup("yes");
			else
				cp->v.string = my_strdup("no");
		}

		/* maybe it's a CF_TIME and they passed CF_INT --
		   should still be valid */
		else if(!((CF_TYPE(value->v.list->type) == CF_INT) &&
			  (CF_TYPE(cf->cf_type) == CF_TIME)))
		{
			conf_report_error("Wrong type for %s::%s (expected %s, got %s)",
					  tc->tc_name, (char *) item, conf_strtype(cf->cf_type),
					  conf_strtype(value->v.list->type));
			return -1;
		}
	}

	if(cf->cf_type & CF_FLIST)
	{
		/* just pass it the extended argument list */
		cf->cf_func(value->v.list);
	}
	else
	{
		/* it's old-style, needs only one arg */
		switch (cf->cf_type)
		{
		case CF_INT:
		case CF_TIME:
		case CF_YESNO:
			if(cf->cf_arg)
				conf_set_generic_int(&cp->v.number, cf->cf_arg);
			else
				cf->cf_func(&cp->v.number);
			break;
		case CF_STRING:
		case CF_QSTRING:
			if(EmptyString(cp->v.string))
				conf_report_error("Ignoring %s::%s -- empty string",
						tc->tc_name, item);
			else if(cf->cf_arg)
				conf_set_generic_string(cp->v.string, cf->cf_len, cf->cf_arg);
			else
				cf->cf_func(cp->v.string);

			break;
		}
	}


	return 0;
}

int
add_conf_item(const char *topconf, const char *name, int type, void (*func) (void *))
{
	struct TopConf *tc;
	struct ConfEntry *cf;

	if((tc = find_top_conf(topconf)) == NULL)
		return -1;

	if((cf = find_conf_item(tc, name)) != NULL)
		return -1;

	cf = my_malloc(sizeof(struct ConfEntry));

	cf->cf_name = my_strdup(name);
	cf->cf_type = type;
	cf->cf_func = func;
	cf->cf_arg = NULL;

	dlink_add_alloc(cf, &tc->tc_items);

	return 0;
}

static void
split_user_host(const char *userhost, const char **user, const char **host)
{
        static const char star[] = "*";
        static char uh[USERHOSTLEN+1];
        char *p;

        strlcpy(uh, userhost, sizeof(uh));

        if((p = strchr(uh, '@')) != NULL)
        {
                *p++ = '\0';
                *user = &uh[0];
                *host = p;
        }
        else
        {
                *user = star;
                *host = userhost;
        }
}


static void
conf_set_serverinfo_name(void *data)
{
	if(config_file.name == NULL)
	{
		if(!valid_servername((const char *) data))
		{
			conf_report_error("Ignoring serverinfo::name -- invalid servername");
			return;
		}
		
	        config_file.name = my_strdup(data);
	}
}

static void
conf_set_serverinfo_sid(void *data)
{
	if(config_file.sid == NULL)
	{
		if(valid_sid((const char *) data))
	        	config_file.sid = my_strdup(data);
		else
			conf_report_error("Ignoring serverinfo::sid -- invalid sid");
	}
}

static void
conf_set_serverinfo_lang(void *data)
{
	config_file.default_language = lang_get_langcode((const char *) data);
}

static void
conf_set_email_program(void *data)
{
	conf_parm_t *args = data;
	int i = 0;

	for(; args; args = args->next)
	{
		if((args->type & CF_MTYPE) != CF_QSTRING)
		{
			conf_report_error("Warning -- email_program is not a quoted string; ignoring further parameters");
			break;
		}

		config_file.email_program[i++] = my_strdup(args->v.string);

		if(i >= MAX_EMAIL_PROGRAM_ARGS)
		{
			conf_report_error("Warning -- email_program has too many arguments; ignoring further parameters");
			break;
		}
	}

	config_file.email_program[i] = NULL;
}
		
static int
conf_begin_connect(struct TopConf *tc)
{
        if(yy_server != NULL)
        {
                my_free(yy_server);
                yy_server = NULL;
        }

        yy_server = my_malloc(sizeof(struct conf_server));
        yy_server->defport = 6667;
	yy_server->flags = CONF_SERVER_AUTOCONN;

        return 0;
}

static int
conf_end_connect(struct TopConf *tc)
{
        if(conf_cur_block_name != NULL)
                yy_server->name = my_strdup(conf_cur_block_name);

        if(EmptyString(yy_server->name) || EmptyString(yy_server->host) ||
           EmptyString(yy_server->pass))
        {
                conf_report_error("Ignoring connect block, missing fields.");
                my_free(yy_server);
                yy_server = NULL;
                return 0;
        }

	if(strlen(yy_server->name) > HOSTLEN)
	{
		conf_report_error("Ignoring connect block, servername length invalid.");
		my_free(yy_server);
		yy_server = NULL;
		return 0;
	}

        dlink_add_tail_alloc(yy_server, &conf_server_list);

        yy_server = NULL;
        return 0;
}

static void
conf_set_connect_host(void *data)
{
        my_free(yy_server->host);
        yy_server->host = my_strdup(data);
}

static void
conf_set_connect_password(void *data)
{
        my_free(yy_server->pass);
        yy_server->pass = my_strdup(data);
}

static void
conf_set_connect_vhost(void *data)
{
        my_free(yy_server->vhost);
        yy_server->vhost = my_strdup(data);
}

static void
conf_set_connect_port(void *data)
{
        yy_server->defport = *(unsigned int *) data;
}

static void
conf_set_connect_autoconn(void *data)
{
	int yesno = *(unsigned int *) data;

	if(yesno)
		yy_server->flags |= CONF_SERVER_AUTOCONN;
	else
		yy_server->flags &= ~CONF_SERVER_AUTOCONN;
}

static int
conf_begin_operator(struct TopConf *tc)
{
        dlink_node *ptr;
        dlink_node *next_ptr;

        if(yy_oper != NULL)
        {
                free_conf_oper(yy_oper);
                yy_oper = NULL;
        }

        DLINK_FOREACH_SAFE(ptr, next_ptr, yy_oper_list.head)
        {
                free_conf_oper(ptr->data);
                dlink_destroy(ptr, &yy_oper_list);
        }

        yy_oper = my_malloc(sizeof(struct conf_oper));
        yy_oper->flags |= CONF_OPER_ENCRYPTED;

        return 0;
}

static int
conf_end_operator(struct TopConf *tc)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

        if(conf_cur_block_name != NULL)
                yy_oper->name = my_strdup(conf_cur_block_name);

        if(EmptyString(yy_oper->name) || EmptyString(yy_oper->pass))
	{
		conf_report_error("Error -- missing name/password");
                return 0;
	}

	if(strlen(yy_oper->name) > OPERNAMELEN)
	{
		conf_report_error("Error -- oper name is excessively long");
		return 0;
	}
		
	DLINK_FOREACH_SAFE(ptr, next_ptr, yy_oper_list.head)
	{
		yy_tmpoper = ptr->data;
		yy_tmpoper->name = my_strdup(yy_oper->name);
		yy_tmpoper->pass = my_strdup(yy_oper->pass);
		yy_tmpoper->flags = yy_oper->flags;
		yy_tmpoper->sflags = yy_oper->sflags;

		dlink_add_tail_alloc(yy_tmpoper, &conf_oper_list);
		dlink_destroy(ptr, &yy_oper_list);
	}

	free_conf_oper(yy_oper);
        yy_oper = yy_tmpoper = NULL;

        return 0;
}

static void
conf_set_oper_user(void *data)
{
	conf_parm_t *args;
	conf_parm_t *args_server;
	const char *username;
	const char *host;

	args = data;
	args_server = args->next;

	if(yy_tmpoper != NULL)
	{
		free_conf_oper(yy_tmpoper);
		yy_tmpoper = NULL;
	}

	yy_tmpoper = my_malloc(sizeof(struct conf_oper));

	/* we have a servername.. */
	if(args_server != NULL)
	{
		if((args_server->type & CF_MTYPE) != CF_QSTRING)
		{
			conf_report_error("Warning -- server is not a qstring; ignoring.");
			return;
		}

		yy_tmpoper->server = my_strdup(args_server->v.string);
	}

	if((args->type & CF_MTYPE) != CF_QSTRING)
	{
		conf_report_error("Warning -- user@host is not a qstring; ignoring.");
		return;
	}

	split_user_host(args->v.string, &username, &host);
	yy_tmpoper->username = my_strdup(username);
	yy_tmpoper->host = my_strdup(host);

        dlink_add_tail_alloc(yy_tmpoper, &yy_oper_list);
	yy_tmpoper = NULL;
}

static void
conf_set_oper_password(void *data)
{
        my_free(yy_oper->pass);
        yy_oper->pass = my_strdup(data);
}

static void
conf_set_oper_encrypted(void *data)
{
        int yesno = *(unsigned int *) data;

        if(yesno)
                yy_oper->flags |= CONF_OPER_ENCRYPTED;
        else
                yy_oper->flags &= ~CONF_OPER_ENCRYPTED;
}

static void
conf_set_oper_flags(void *data)
{
	set_modes_from_table(&yy_oper->flags, "flag", privs_table, data);
}

static void
conf_set_oper_userserv(void *data)
{
	set_modes_from_table(&yy_oper->sflags, "flag",
				userserv_table, data);
}

static void
conf_set_oper_chanserv(void *data)
{
	set_modes_from_table(&yy_oper->sflags, "flag",
				chanserv_table, data);
}

static void
conf_set_oper_nickserv(void *data)
{
	set_modes_from_table(&yy_oper->sflags, "flag",
				nickserv_table, data);
}

static void
conf_set_oper_operserv(void *data)
{
	set_modes_from_table(&yy_oper->sflags, "flag",
				operserv_table, data);
}

static void
conf_set_oper_operbot(void *data)
{
	set_modes_from_table(&yy_oper->sflags, "flag",
				operbot_table, data);
}

static void
conf_set_oper_global(void *data)
{
	set_modes_from_table(&yy_oper->sflags, "flag",
				global_table, data);
}

static void
conf_set_oper_jupeserv(void *data)
{
	set_modes_from_table(&yy_oper->sflags, "flag",
				jupeserv_table, data);
}

static void
conf_set_oper_banserv(void *data)
{
	set_modes_from_table(&yy_oper->sflags, "flag",
				banserv_table, data);
}

static int
conf_begin_service(struct TopConf *tc)
{
	if(conf_cur_block_name == NULL)
	{
		conf_report_error("Ignoring service block, missing service id");
		yy_service = NULL;
		return 0;
	}

	yy_service = find_service_id(conf_cur_block_name);

	if(yy_service == NULL)
		conf_report_error("Ignoring service block, unknown service id %s",
				conf_cur_block_name);

	return 0;
}

static int
conf_end_service(struct TopConf *tc)
{
	yy_service = NULL;
	return 0;
}

static void
conf_set_service_nick(void *data)
{
	if(yy_service == NULL || !strcmp(yy_service->name, (const char *) data))
		return;

	if(sent_burst)
	{
		del_client(yy_service);
		strlcpy(yy_service->name, (const char *) data,
			sizeof(yy_service->name));
		add_client(yy_service);
		SetServiceReintroduce(yy_service);
	}
	else
		strlcpy(yy_service->name, (const char *) data,
			sizeof(yy_service->name));
}

static void
conf_set_service_username(void *data)
{
	if(yy_service == NULL ||
	   !strcmp(yy_service->service->username, (const char *) data))
		return;

	strlcpy(yy_service->service->username, (const char *) data,
		sizeof(yy_service->service->username));

	if(sent_burst)
		SetServiceReintroduce(yy_service);
}

static void
conf_set_service_host(void *data)
{
	if(yy_service == NULL ||
	   !strcmp(yy_service->service->host, (const char *) data))
		return;

	strlcpy(yy_service->service->host, (const char *) data,
		sizeof(yy_service->service->host));

	if(sent_burst)
		SetServiceReintroduce(yy_service);
}

static void
conf_set_service_realname(void *data)
{
	if(yy_service == NULL || !strcmp(yy_service->info, (const char *) data))
		return;

	strlcpy(yy_service->info, (const char *) data,
		sizeof(yy_service->info));

	if(sent_burst)
		SetServiceReintroduce(yy_service);
}

static void
conf_set_service_flags(void *data)
{
	if(yy_service == NULL)
		return;

	yy_service->service->flags = 0;
	set_modes_from_table(&yy_service->service->flags, "flag",
				service_flags_table, data);
}

static void
conf_set_service_loglevel(void *data)
{
	if(yy_service == NULL)
		return;

	yy_service->service->loglevel = *(unsigned int *) data;
}

static void
conf_set_service_flood_max(void *data)
{
	if(yy_service == NULL)
		return;

	yy_service->service->flood_max = *(unsigned int *) data;
}

static void
conf_set_service_flood_grace(void *data)
{
	if(yy_service == NULL)
		return;

	yy_service->service->flood_grace = *(unsigned int *) data;
}

static void
conf_set_chanserv_expireban(void *data)
{
	time_t val = *(time_t *) data;

	if(val < 60)
	{
		conf_report_error("Ignoring chanserv::expireban_frequency "
			"-- invalid duration");
		return;
	}

	if(config_file.cexpireban_frequency == val)
		return;

	config_file.cexpireban_frequency = val;
	eventUpdate("chanserv_expireban", val);
}

static void
conf_set_chanserv_enforcetopic(void *data)
{
	time_t val = *(time_t *) data;

	if(config_file.cenforcetopic_frequency == val)
		return;

	config_file.cenforcetopic_frequency = val;
	eventUpdate("chanserv_enforcetopic", val);
}

static void
conf_set_banserv_autosync(void *data)
{
	time_t val = *(time_t *) data;

	if(val < 60)
	{
		conf_report_error("Ignoring banserv::autosync_frequency "
			"-- invalid duration");
		return;
	}

	if(config_file.bs_autosync_frequency == val)
		return;

	config_file.bs_autosync_frequency = val;
	eventUpdate("banserv_autosync", val);
}

static struct ConfEntry conf_serverinfo_table[] =
{
	{ "client_flood_max",		CF_INT,  NULL, 0, &config_file.client_flood_max	},
	{ "client_flood_max_ignore",	CF_INT,	 NULL, 0, &config_file.client_flood_max_ignore },
	{ "client_flood_ignore_time",	CF_TIME, NULL, 0, &config_file.client_flood_ignore_time },
	{ "client_flood_time",		CF_TIME, NULL, 0, &config_file.client_flood_time },
	{ "description",	CF_QSTRING, NULL, 0, &config_file.gecos		},
	{ "vhost",		CF_QSTRING, NULL, 0, &config_file.vhost		},
	{ "dcc_vhost",		CF_QSTRING, NULL, 0, &config_file.dcc_vhost	},
	{ "dcc_low_port",	CF_INT,     NULL, 0, &config_file.dcc_low_port	},
	{ "dcc_high_port",	CF_INT,     NULL, 0, &config_file.dcc_high_port },
	{ "reconnect_time",	CF_TIME,    NULL, 0, &config_file.reconnect_time },
	{ "ping_time",		CF_TIME,    NULL, 0, &config_file.ping_time	},
	{ "ratbox",		CF_YESNO,   NULL, 0, &config_file.ratbox	},
	{ "allow_stats_o",	CF_YESNO,   NULL, 0, &config_file.allow_stats_o },
	{ "allow_sslonly",	CF_YESNO,   NULL, 0, &config_file.allow_sslonly },
	{ "name",		CF_QSTRING, conf_set_serverinfo_name, 0, NULL	},
	{ "sid",		CF_QSTRING, conf_set_serverinfo_sid, 0, NULL	},
	{ "default_language",	CF_QSTRING, conf_set_serverinfo_lang, 0, NULL	},
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_email_table[] =
{
	{ "disable_email",	CF_YESNO,   NULL, 0, &config_file.disable_email },
	{ "email_program",	CF_QSTRING|CF_FLIST, conf_set_email_program, 0, NULL },
	{ "email_name",		CF_QSTRING, NULL, 0, &config_file.email_name	},
	{ "email_address",	CF_QSTRING, NULL, 0, &config_file.email_address },
	{ "email_number",	CF_INT,     NULL, 0, &config_file.email_number	},
	{ "email_duration",	CF_TIME,    NULL, 0, &config_file.email_duration },
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_database_table[] =
{
	{ "host",	CF_QSTRING,	NULL, 0, &config_file.db_host		},
	{ "name",	CF_QSTRING,	NULL, 0, &config_file.db_name		},
	{ "username",	CF_QSTRING,	NULL, 0, &config_file.db_username	},
	{ "password",	CF_QSTRING,	NULL, 0, &config_file.db_password	},
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_admin_table[] =
{
	{ "name",		CF_QSTRING, NULL, 0, &config_file.admin1	},
	{ "description",	CF_QSTRING, NULL, 0, &config_file.admin2	},
	{ "email",		CF_QSTRING, NULL, 0, &config_file.admin3	},
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_connect_table[] =
{
	{ "host",	CF_QSTRING, conf_set_connect_host,	0, NULL	},
	{ "password",	CF_QSTRING, conf_set_connect_password,	0, NULL },
	{ "vhost",	CF_QSTRING, conf_set_connect_vhost,	0, NULL },
	{ "port",	CF_INT,     conf_set_connect_port,	0, NULL },
	{ "autoconn",   CF_YESNO,   conf_set_connect_autoconn,	0, NULL },
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_oper_table[] =
{
	{ "user",	CF_QSTRING|CF_FLIST, conf_set_oper_user, 0, NULL },
	{ "password",	CF_QSTRING, conf_set_oper_password,	0, NULL },
	{ "encrypted",	CF_YESNO,   conf_set_oper_encrypted,	0, NULL },
	{ "flags",	CF_STRING|CF_FLIST,  conf_set_oper_flags,	0, NULL },
	{ "userserv",	CF_STRING|CF_FLIST,  conf_set_oper_userserv,	0, NULL },
	{ "chanserv",	CF_STRING|CF_FLIST,  conf_set_oper_chanserv,	0, NULL },
	{ "nickserv",	CF_STRING|CF_FLIST,  conf_set_oper_nickserv,	0, NULL },
	{ "operserv",	CF_STRING|CF_FLIST,  conf_set_oper_operserv,	0, NULL },
	{ "operbot",	CF_STRING|CF_FLIST,  conf_set_oper_operbot,	0, NULL },
	{ "global",	CF_STRING|CF_FLIST,  conf_set_oper_global,	0, NULL },
	{ "jupeserv",	CF_STRING|CF_FLIST,  conf_set_oper_jupeserv,	0, NULL },
	{ "banserv",	CF_STRING|CF_FLIST,  conf_set_oper_banserv,	0, NULL },
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_service_table[] =
{
	{ "nick",	CF_QSTRING, conf_set_service_nick,	0, NULL },
	{ "username",	CF_QSTRING, conf_set_service_username,	0, NULL },
	{ "host",	CF_QSTRING, conf_set_service_host,	0, NULL },
	{ "realname",	CF_QSTRING, conf_set_service_realname,	0, NULL },
	{ "loglevel",	CF_INT,     conf_set_service_loglevel,  0, NULL },
	{ "flags",	CF_STRING|CF_FLIST, conf_set_service_flags, 0, NULL },
	{ "flood_max",	CF_INT,	    conf_set_service_flood_max,	0, NULL },
	{ "flood_grace",CF_INT,     conf_set_service_flood_grace, 0, NULL },
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_userserv_table[] =
{
	{ "disable_register",	CF_YESNO, NULL, 0, &config_file.disable_uregister	},
	{ "register_url",	CF_QSTRING,NULL,0, &config_file.uregister_url },
	{ "register_time",	CF_TIME,  NULL, 0, &config_file.uregister_time		},
	{ "register_amount",	CF_INT,   NULL, 0, &config_file.uregister_amount	},
	{ "register_email",	CF_YESNO, NULL, 0, &config_file.uregister_email		},
	{ "register_verify",	CF_YESNO, NULL, 0, &config_file.uregister_verify	},
	{ "host_register_time", CF_TIME,  NULL, 0, &config_file.uhregister_time		},
	{ "host_register_amount",CF_INT,  NULL, 0, &config_file.uhregister_amount	},
	{ "expire_time",	CF_TIME,  NULL, 0, &config_file.uexpire_time		},
	{ "expire_suspended_time",	CF_TIME, NULL, 0, &config_file.uexpire_suspended_time	},
	{ "expire_unverified_time",	CF_TIME, NULL, 0, &config_file.uexpire_unverified_time	},
	{ "expire_bonus_regtime",	CF_TIME, NULL, 0, &config_file.uexpire_bonus_regtime	},
	{ "expire_bonus",		CF_TIME, NULL, 0, &config_file.uexpire_bonus		},
	{ "expire_bonus_per_time",	CF_TIME, NULL, 0, &config_file.uexpire_bonus_per_time	},
	{ "expire_bonus_max",		CF_TIME, NULL, 0, &config_file.uexpire_bonus_max	},
	{ "allow_set_password",	CF_YESNO, NULL, 0, &config_file.allow_set_password	},
	{ "allow_resetpass",	CF_YESNO, NULL, 0, &config_file.allow_resetpass		},
	{ "allow_resetemail",	CF_YESNO, NULL, 0, &config_file.allow_resetemail	},
	{ "resetpass_duration",	CF_TIME,  NULL, 0, &config_file.uresetpass_duration	},
	{ "resetemail_duration", CF_TIME, NULL, 0, &config_file.uresetemail_duration	},
	{ "reset_regtime_duration", CF_TIME, NULL, 0, &config_file.ureset_regtime_duration },
	{ "allow_set_email",	CF_YESNO, NULL, 0, &config_file.allow_set_email		},
	{ "max_logins",		CF_INT,   NULL, 0, &config_file.umax_logins		},
	{ "show_suspend_reasons",CF_YESNO,NULL, 0, &config_file.ushow_suspend_reasons	},
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_chanserv_table[] =
{
	{ "disable_register",	CF_YESNO, NULL, 0, &config_file.disable_cregister	},
	{ "register_time",	CF_TIME,  NULL, 0, &config_file.cregister_time		},
	{ "register_amount",	CF_INT,   NULL, 0, &config_file.cregister_amount	},
	{ "host_register_time", CF_TIME,  NULL, 0, &config_file.chregister_time		},
	{ "host_register_amount",CF_INT,  NULL, 0, &config_file.chregister_amount	},
	{ "expire_time",	CF_TIME,  NULL, 0, &config_file.cexpire_time		},
	{ "max_bans",		CF_INT,	  NULL, 0, &config_file.cmax_bans		},
	{ "expire_suspended_time",CF_TIME,NULL, 0, &config_file.cexpire_suspended_time	},
	{ "expireban_frequency", CF_TIME,	conf_set_chanserv_expireban, 0, NULL	},
	{ "enforcetopic_frequency", CF_TIME,	conf_set_chanserv_enforcetopic, 0, NULL },
	{ "delowner_duration",	CF_TIME,  NULL, 0, &config_file.cdelowner_duration	},
	{ "email_delowner",	CF_YESNO, NULL, 0, &config_file.cemail_delowner	},
	{ "autojoin_empty",	CF_YESNO, NULL, 0, &config_file.cautojoin_empty		},
	{ "show_suspend_reasons",CF_YESNO,NULL, 0, &config_file.cshow_suspend_reasons	},
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_nickserv_table[] =
{
	{ "allow_set_warn",	CF_YESNO, NULL, 0, &config_file.nallow_set_warn		},
	{ "max_nicks",		CF_INT,   NULL, 0, &config_file.nmax_nicks		},
	{ "warn_string",	CF_QSTRING,NULL,0, &config_file.nwarn_string		},
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_jupeserv_table[] =
{
	{ "oper_score",		CF_INT, NULL, 0, &config_file.oper_score	},
	{ "jupe_score",		CF_INT, NULL, 0, &config_file.jupe_score	},
	{ "unjupe_score",	CF_INT, NULL, 0, &config_file.unjupe_score	},
	{ "pending_time",	CF_TIME,NULL, 0, &config_file.pending_time	},
	{ "merge_into_operserv",CF_YESNO,NULL, 0, &config_file.js_merge_into_operserv	},
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_operserv_table[] =
{
	{ "allow_die",		CF_YESNO, NULL, 0, &config_file.os_allow_die	},
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_alis_table[] =
{
	{ "max_matches",	CF_INT, NULL, 0, &config_file.max_matches	},
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_banserv_table[] =
{
	{ "unban_time",		CF_TIME, NULL, 0, &config_file.bs_unban_time	},
	{ "temp_workaround",	CF_YESNO,NULL, 0, &config_file.bs_temp_workaround },
	{ "regexp_time",	CF_TIME, NULL, 0, &config_file.bs_regexp_time	},
	{ "merge_into_operserv",CF_YESNO,NULL, 0, &config_file.bs_merge_into_operserv	},
	{ "max_kline_matches",	CF_INT,  NULL, 0, &config_file.bs_max_kline_matches	},
	{ "max_xline_matches",	CF_INT,  NULL, 0, &config_file.bs_max_xline_matches	},
	{ "max_resv_matches",	CF_INT,  NULL, 0, &config_file.bs_max_resv_matches	},
	{ "max_regexp_matches",	CF_INT,  NULL, 0, &config_file.bs_max_regexp_matches	},
	{ "autosync_frequency",	CF_TIME, 	conf_set_banserv_autosync, 0, NULL },
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_watchserv_table[] =
{
	{ "merge_into_operserv",CF_YESNO,NULL, 0, &config_file.ws_merge_into_operserv	},
	{ "\0", 0, NULL, 0, NULL }
};

static struct ConfEntry conf_memoserv_table[] =
{
	{ "max_memos",			CF_INT,  NULL, 0, &config_file.ms_max_memos		},
	{ "memo_regtime_duration",	CF_TIME, NULL, 0, &config_file.ms_memo_regtime_duration	},
	{ "\0", 0, NULL, 0, NULL }
};

void
newconf_init()
{
	add_top_conf("serverinfo", NULL, NULL, conf_serverinfo_table);
	add_top_conf("database", NULL, NULL, conf_database_table);
	add_top_conf("email", NULL, NULL, conf_email_table);
        add_top_conf("admin", NULL, NULL, conf_admin_table);
        add_top_conf("connect", conf_begin_connect, conf_end_connect, conf_connect_table);
        add_top_conf("operator", conf_begin_operator, conf_end_operator, 
			conf_oper_table);
	add_top_conf("service", conf_begin_service, conf_end_service, conf_service_table);

	add_conf_extension("service", "userserv", conf_userserv_table);
	add_conf_extension("service", "chanserv", conf_chanserv_table);
	add_conf_extension("service", "nickserv", conf_nickserv_table);
	add_conf_extension("service", "jupeserv", conf_jupeserv_table);
	add_conf_extension("service", "operserv", conf_operserv_table);
	add_conf_extension("service", "alis", conf_alis_table);
	add_conf_extension("service", "banserv", conf_banserv_table);
	add_conf_extension("service", "watchserv", conf_watchserv_table);
	add_conf_extension("service", "memoserv", conf_memoserv_table);
}
