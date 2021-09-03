/* src/conf.c
 *   Contains code for parsing the config file
 *
 * Copyright (C) 2003-2008 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2008 ircd-ratbox development team
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
 * $Id: conf.c 26716 2010-01-03 18:30:48Z leeh $
 */
#include "stdinc.h"
#include "rserv.h"
#include "langs.h"
#include "conf.h"
#include "tools.h"
#include "client.h"
#include "service.h"
#include "io.h"
#include "log.h"

struct _config_file config_file;
dlink_list conf_server_list;
dlink_list conf_oper_list;

time_t first_time;

extern int yyparse();           /* defined in y.tab.c */
extern char linebuf[];
extern char conffilebuf[BUFSIZE + 1];
int scount = 0;                 /* used by yyparse(), etc */

FILE *conf_fbfile_in;
extern char yytext[];

static void
set_default_conf(void)
{
	config_file.dcc_low_port = 1025;
	config_file.dcc_high_port = 65000;

	config_file.ping_time = 300;
	config_file.reconnect_time = 300;

	config_file.ratbox = 1;
	config_file.allow_stats_o = 1;

	config_file.default_language = lang_get_langcode(LANG_DEFAULT);

	config_file.client_flood_max = 20;
	config_file.client_flood_max_ignore = 30;
	config_file.client_flood_ignore_time = 300;
	config_file.client_flood_time = 60;

	config_file.disable_email = 1;
	config_file.email_number = 15;
	config_file.email_duration = 60;

	config_file.disable_uregister = 0;
	config_file.uregister_time = 60;
	config_file.uregister_amount = 5;
	config_file.uhregister_time = 86400;	/* 1 day */
	config_file.uhregister_amount = 2;
	config_file.uregister_email = 0;
	config_file.uregister_verify = 0;
	config_file.uexpire_time = 2419200;	/* 4 weeks */
	config_file.uexpire_suspended_time = 2419200;	/* 4 weeks */
	config_file.uexpire_unverified_time = 86400;	/* 1 day */
	config_file.uexpire_bonus_regtime = 4838400;	/* 8 weeks */
	config_file.uexpire_bonus = 86400;		/* 1 day */
	config_file.uexpire_bonus_per_time = 1209600;	/* 2 weeks */
	config_file.uexpire_bonus_max = 2419200;	/* 4 weeks */
	config_file.allow_set_password = 1;
	config_file.allow_resetpass = 0;
	config_file.allow_resetemail = 0;
	config_file.uresetpass_duration = 86400;	/* 1 day */
	config_file.uresetemail_duration = 86400;    /*1 day */
	config_file.ureset_regtime_duration = 1209600; /* 2 weeks */
	config_file.allow_set_email = 1;
	config_file.umax_logins = 5;
	config_file.ushow_suspend_reasons = 0;

	config_file.disable_cregister = 0;
	config_file.cregister_time = 60;
	config_file.cregister_amount = 5;
	config_file.chregister_time = 86400;	/* 1 day */
	config_file.chregister_amount = 4;
	config_file.cexpire_time = 2419200; 	/* 4 weeks */
	config_file.cexpire_suspended_time = 2419200; 	/* 4 weeks */
	config_file.cmax_bans = 50;
	config_file.cexpireban_frequency = 900;		/* 15 mins */
	config_file.cenforcetopic_frequency = 3600;	/* 1 hour */
	config_file.cdelowner_duration = 86400;	/* 1 day */
	config_file.cemail_delowner = 0;
	config_file.cautojoin_empty = 0;
	config_file.cshow_suspend_reasons = 0;

	config_file.nmax_nicks = 2;
	config_file.nallow_set_warn = 1;

	config_file.os_allow_die = 1;

	config_file.bs_unban_time = 1209600;	/* 2 weeks */
	config_file.bs_temp_workaround = 0;
	config_file.bs_autosync_frequency = DEFAULT_AUTOSYNC_FREQUENCY;
	config_file.bs_regexp_time = 86400;	/* 1 day */
	config_file.bs_merge_into_operserv = 0;
	config_file.bs_max_kline_matches = 200;
	config_file.bs_max_xline_matches = 200;
	config_file.bs_max_resv_matches = 200;
	config_file.bs_max_regexp_matches = 200;

	my_free(config_file.nwarn_string);
	config_file.nwarn_string = my_strdup("This nickname is registered, you may "
				"be disconnected if a user regains this nickname.");

	config_file.oper_score = 3;
	config_file.jupe_score = 15;
	config_file.unjupe_score = 15;
	config_file.pending_time = 1800;
	config_file.js_merge_into_operserv = 0;

	config_file.ws_merge_into_operserv = 0;

	config_file.ms_max_memos = 50;
	config_file.ms_memo_regtime_duration = 604800;	/* 1 week */

	config_file.max_matches = 60;
}

static void
validate_conf(void)
{
	if(EmptyString(config_file.name))
		die(0, "No servername specified");

	if(EmptyString(config_file.sid))
		die(0, "No SID specified");

	if(EmptyString(config_file.gecos))
		config_file.gecos = my_strdup("ratbox services");

	if(config_file.dcc_low_port <= 1024)
		config_file.dcc_low_port = 1025;

	if(config_file.dcc_high_port < config_file.dcc_low_port)
		config_file.dcc_high_port = 65000;

	if(config_file.ping_time <= 0)
		config_file.ping_time = 300;

	if(config_file.reconnect_time <= 0)
		config_file.reconnect_time = 300;

	if(config_file.pending_time <= 0)
		config_file.pending_time = 1800;

	if(config_file.max_matches >= 250)
		config_file.max_matches = 250;
	else if(config_file.max_matches <= 0)
		config_file.max_matches = 250;

	if(config_file.umax_logins < 0)
		config_file.umax_logins = 0;

	/* email verification requires we're given an email address */
	if(config_file.uregister_verify)
		config_file.uregister_email = 1;
}

static void
clear_old_conf(void)
{
	struct conf_oper *oper_p;
	dlink_node *ptr;
	dlink_node *next_ptr;
	int i;

	for(i = 0; config_file.email_program[i]; i++)
	{
		my_free(config_file.email_program[i]);
		config_file.email_program[i] = NULL;
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, conf_oper_list.head)
	{
		oper_p = ptr->data;

		/* still in use */
		if(oper_p->refcount)
			SetConfDead(oper_p);
		else
			free_conf_oper(oper_p);

		dlink_destroy(ptr, &conf_oper_list);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, conf_server_list.head)
	{
		free_conf_server(ptr->data);
		dlink_destroy(ptr, &conf_server_list);
	}	
}

void
conf_parse(int cold)
{
	struct client *target_p;
	dlink_node *ptr;

        if((conf_fbfile_in = fopen(CONF_PATH, "r")) == NULL)
	{
		if(!cold)
		{
			mlog("Failed to open config file");
			sendto_all("Failed to open config file");
			return;
		}
		else
	                die(0, "Failed to open config file");
	}

	if(!cold)
		clear_old_conf();
	else
		set_default_conf();

        yyparse();
	validate_conf();

	/* if we havent sent our burst, the following will just break */
	if(!testing_conf && sent_burst)
	{
		DLINK_FOREACH(ptr, service_list.head)
		{
			target_p = ptr->data;

			if(ServiceIntroduced(target_p))
			{
				if(ServiceDisabled(target_p))
				{
					deintroduce_service(target_p);
					continue;
				}
				else if(ServiceReintroduce(target_p))
				{
					reintroduce_service(target_p);
					continue;
				}
			}
			else if(!ServiceDisabled(target_p))
				introduce_service(target_p);

			ClearServiceReintroduce(target_p);
		}
	}

        fclose(conf_fbfile_in);
}

void
rehash(int sig)
{
	if(sig)
	{
		mlog("services rehashing: got SIGHUP");
		sendto_all("services rehashing: got SIGHUP");
	}

	reopen_logfiles();

	conf_parse(0);
}

void
free_conf_oper(struct conf_oper *conf_p)
{
	my_free(conf_p->name);
	my_free(conf_p->pass);
	my_free(conf_p->username);
	my_free(conf_p->host);
	my_free(conf_p->server);
	my_free(conf_p);
}

void
free_conf_server(struct conf_server *conf_p)
{
	my_free(conf_p->name);
	my_free(conf_p->host);
	my_free(conf_p->pass);
	my_free(conf_p->vhost);
}

void
deallocate_conf_oper(struct conf_oper *conf_p)
{
	conf_p->refcount--;

	/* marked as dead, now unused, free. */
	if(ConfDead(conf_p) && !conf_p->refcount)
		free_conf_oper(conf_p);
}

struct conf_server *
find_conf_server(const char *name)
{
        struct conf_server *server;
        dlink_node *ptr;

        DLINK_FOREACH(ptr, conf_server_list.head)
        {
                server = ptr->data;

                if(!strcasecmp(name, server->name))
                        return server;
        }

        return NULL;
}

struct conf_oper *
find_conf_oper(const char *username, const char *host, const char *server,
		const char *oper_username)
{
        struct conf_oper *oper_p;
        dlink_node *ptr;

        DLINK_FOREACH(ptr, conf_oper_list.head)
        {
                oper_p = ptr->data;

                if(match(oper_p->username, username) &&
                   match(oper_p->host, host) &&
		   (EmptyString(oper_p->server) || match(oper_p->server, server)) &&
		   (EmptyString(oper_username) || !irccmp(oper_p->name, oper_username)))
                        return oper_p;
        }

        return NULL;
}

static struct flag_table
{
	char mode;
	int flag;
} oper_flags[] = {
	{ 'D', CONF_OPER_DCC		},
	{ 'A', CONF_OPER_ADMIN		},
	{ '\0',0 }
};

const char *
conf_oper_flags(unsigned int flags)
{
	static char buf[20];
	static const char *empty_flags = "-";
	char *p = buf;
	int i;

	for(i = 0; oper_flags[i].mode; i++)
	{
		if(flags & oper_flags[i].flag)
			*p++ = oper_flags[i].mode;
	}

	*p = '\0';

	if(EmptyString(buf))
		return empty_flags;

	return buf;
}

#undef SERVICE_FLAGS_FULL

static struct sflag_table
{
	int serviceid;
	const char *sname;
	int flag;
	const char *name;
} service_flags[] = {
#ifdef SERVICE_FLAGS_FULL
	{ 1, "operserv", CONF_OPER_OS_MAINTAIN, "maintain"	},
	{ 1, "operserv", CONF_OPER_OS_IGNORE,	"ignore"	},
	{ 1, "operserv", CONF_OPER_OS_CHANNEL,	"channel"	},
	{ 1, "operserv", CONF_OPER_OS_TAKEOVER,	"takeover"	},
	{ 1, "operserv", CONF_OPER_OS_OMODE,	"omode"		},
	{ 2, "userserv", CONF_OPER_US_REGISTER,	"register"	},
	{ 2, "userserv", CONF_OPER_US_SUSPEND,	"suspend"	},
	{ 2, "userserv", CONF_OPER_US_DROP,	"drop"		},
	{ 2, "userserv", CONF_OPER_US_SETPASS,	"setpass"	},
	{ 2, "userserv", CONF_OPER_US_LIST,	"list"		},
	{ 2, "userserv", CONF_OPER_US_INFO,	"info"		},
	{ 3, "chanserv", CONF_OPER_CS_REGISTER,	"register"	},
	{ 3, "chanserv", CONF_OPER_CS_SUSPEND,	"suspend"	},
	{ 3, "chanserv", CONF_OPER_CS_DROP,	"drop"		},
	{ 3, "chanserv", CONF_OPER_CS_LIST,	"list"		},
	{ 3, "chanserv", CONF_OPER_CS_INFO,	"info"		},
	{ 4, "nickserv", CONF_OPER_NS_DROP,	"drop"		},
	{ 5, "operbot",  CONF_OPER_OB_CHANNEL,	"channel"	},
	{ 6, "global",   CONF_OPER_GLOB_NETMSG,	"netmsg"	},
	{ 6, "global",   CONF_OPER_GLOB_WELCOME,"welcome"	},
	{ 7, "jupeserv", CONF_OPER_JS_JUPE,	"jupe"		},
	{ 8, "banserv",  CONF_OPER_BAN_KLINE,	"kline"		},
	{ 8, "banserv",  CONF_OPER_BAN_XLINE,	"xline"		},
	{ 8, "banserv",  CONF_OPER_BAN_RESV,	"resv"		},
	{ 8, "banserv",  CONF_OPER_BAN_REGEXP,	"regexp"	},
	{ 8, "banserv",  CONF_OPER_BAN_PERM,	"perm"		},
	{ 8, "banserv",  CONF_OPER_BAN_REMOVE,	"remove"	},
	{ 8, "banserv",  CONF_OPER_BAN_SYNC,	"sync"		},
	{ 8, "banserv",	 CONF_OPER_BAN_NOMAX,	"nomax"		},
#else
	{ 1, "OS", CONF_OPER_OS_MAINTAIN,	"M" },
	{ 1, "OS", CONF_OPER_OS_IGNORE,		"I" },
	{ 1, "OS", CONF_OPER_OS_CHANNEL,	"C" },
	{ 1, "OS", CONF_OPER_OS_TAKEOVER,	"T" },
	{ 1, "OS", CONF_OPER_OS_OMODE,		"O" },
	{ 2, "US", CONF_OPER_US_REGISTER,	"R" },
	{ 2, "US", CONF_OPER_US_SUSPEND,	"S" },
	{ 2, "US", CONF_OPER_US_DROP,		"D" },
	{ 2, "US", CONF_OPER_US_SETPASS,	"P" },
	{ 2, "US", CONF_OPER_US_LIST,		"L" },
	{ 2, "US", CONF_OPER_US_INFO,		"I" },
	{ 3, "CS", CONF_OPER_CS_REGISTER,	"R" },
	{ 3, "CS", CONF_OPER_CS_SUSPEND,	"S" },
	{ 3, "CS", CONF_OPER_CS_DROP,		"D" },
	{ 3, "CS", CONF_OPER_CS_LIST,		"L" },
	{ 3, "CS", CONF_OPER_CS_INFO,		"I" },
	{ 4, "NS", CONF_OPER_NS_DROP,		"D" },
	{ 5, "OB", CONF_OPER_OB_CHANNEL,	"C" },
	{ 6, "GL", CONF_OPER_GLOB_NETMSG,	"N" },
	{ 6, "GL", CONF_OPER_GLOB_WELCOME,	"W" },
	{ 7, "JS", CONF_OPER_JS_JUPE,		"J" },
	{ 8, "BS", CONF_OPER_BAN_KLINE,		"K" },
	{ 8, "BS", CONF_OPER_BAN_XLINE,		"X" },
	{ 8, "BS", CONF_OPER_BAN_REGEXP,	"P" },
	{ 8, "BS", CONF_OPER_BAN_RESV,		"R" },
	{ 8, "BS", CONF_OPER_BAN_PERM,		"P" },
	{ 8, "BS", CONF_OPER_BAN_REMOVE,	"V" },
	{ 8, "BS", CONF_OPER_BAN_SYNC,		"S" },
	{ 8, "BS", CONF_OPER_BAN_NOMAX,		"M" },
#endif
	{ 0, NULL, 0, NULL }
};

const char *
conf_service_flags(unsigned int flags)
{
	static char buf[BUFSIZE];
	static const char *empty_flags = "-";
	int i;
	int serviceid = 0;

	buf[0] = '\0';

	for(i = 0; service_flags[i].flag; i++)
	{
		if(flags & service_flags[i].flag)
		{
			if(serviceid != service_flags[i].serviceid)
			{
				if(serviceid)
					strlcat(buf, " ", sizeof(buf));

				strlcat(buf, service_flags[i].sname, sizeof(buf));
				strlcat(buf, ":", sizeof(buf));

				serviceid = service_flags[i].serviceid;
			}
#ifdef SERVICE_FLAGS_FULL
			else
				strlcat(buf, ",", sizeof(buf));
#endif
			strlcat(buf, service_flags[i].name, sizeof(buf));
		}
	}

	if(EmptyString(buf))
		return empty_flags;

	return buf;
}

/*
 * yyerror
 *
 * inputs	- message from parser
 * output	- none
 * side effects	- message to opers and log file entry is made
 */
void
yyerror(const char *msg)
{
	char newlinebuf[BUFSIZE];

	strip_tabs(newlinebuf, (const unsigned char *) linebuf, strlen(linebuf));

	sendto_all("\"%s\", line %d: %s at '%s'",
                   conffilebuf, lineno + 1, msg, newlinebuf);

	mlog("conf error: \"%s\", line %d: %s at '%s'", conffilebuf, lineno + 1, msg, newlinebuf);
}

int
conf_fbgets(char *lbuf, int max_size)
{
	char *buff;

	if((buff = fgets(lbuf, max_size, conf_fbfile_in)) == NULL)
		return (0);

	return (strlen(lbuf));
}
