/* $Id: conf.h 26716 2010-01-03 18:30:48Z leeh $ */
#ifndef INCLUDED_conf_h
#define INCLUDED_conf_h

struct lconn;
struct FileBuf;

#define MYNAME config_file.name

#define DEFAULT_AUTOSYNC_FREQUENCY	604800	/* 1 week */

#define MAX_EMAIL_PROGRAM_ARGS		10

extern time_t first_time;

struct _config_file
{
	char *name;
	char *sid;
	char *gecos;
	char *vhost;

	char *dcc_vhost;
	int dcc_low_port;
	int dcc_high_port;

	int reconnect_time;
	int ping_time;
	int ratbox;
	int allow_stats_o;
	int allow_sslonly;
	int default_language;

	unsigned int client_flood_time;
	unsigned int client_flood_ignore_time;
	unsigned int client_flood_max;
	unsigned int client_flood_max_ignore;

	char *admin1;
	char *admin2;
	char *admin3;

	char *db_host;
	char *db_name;
	char *db_username;
	char *db_password;

	int disable_email;
	char *email_program[MAX_EMAIL_PROGRAM_ARGS+1];
	char *email_name;
	char *email_address;
	int email_number;
	int email_duration;

	/* userserv */
	int disable_uregister;
	char *uregister_url;
	int uregister_time;		/* overall registrations */
	int uregister_amount;
	int uhregister_time;		/* per host registrations */
	int uhregister_amount;
	int uregister_email;
	int uregister_verify;
	int uexpire_time;
	int uexpire_suspended_time;
	int uexpire_unverified_time;
	int uexpire_bonus_regtime;
	int uexpire_bonus;
	int uexpire_bonus_per_time;
	int uexpire_bonus_max;
	int allow_set_password;
	int allow_resetpass;
	int allow_resetemail;
	int uresetpass_duration;
	int uresetemail_duration;
	int ureset_regtime_duration;
	int allow_set_email;
	int umax_logins;
	int ushow_suspend_reasons;

	/* chanserv */
	int disable_cregister;
	int cregister_time;		/* overall registrations */
	int cregister_amount;
	int chregister_time;		/* per host registrations */
	int chregister_amount;
	int cexpire_time;
	int cexpire_suspended_time;
	int cmax_bans;
	int cexpireban_frequency;
	int cenforcetopic_frequency;
	int cdelowner_duration;
	int cemail_delowner;
	int cautojoin_empty;
	int cshow_suspend_reasons;

	/* nickserv */
	int nmax_nicks;
	int nallow_set_warn;
	char *nwarn_string;

	/* jupeserv */
	int oper_score;
	int jupe_score;
	int unjupe_score;
	int pending_time;
	int js_merge_into_operserv;

	/* operserv */
	int os_allow_die;

	/* banserv */
	int bs_unban_time;
	int bs_temp_workaround;
	int bs_autosync_frequency;
	int bs_regexp_time;
	int bs_merge_into_operserv;
	int bs_max_kline_matches;
	int bs_max_xline_matches;
	int bs_max_resv_matches;
	int bs_max_regexp_matches;

	/* watchserv */
	int ws_merge_into_operserv;

	/* memoserv */
	int ms_max_memos;
	int ms_memo_regtime_duration;

	/* alis */
	int max_matches;
};

struct conf_server
{
	char *name;
	char *host;
	char *pass;
	char *vhost;
	int port;
        int defport;
	int flags;
        time_t last_connect;
};

struct conf_oper
{
        char *name;
        char *username;
        char *host;
        char *pass;
	char *server;
	unsigned int flags;		/* general flags */
	unsigned int sflags;		/* individual service flags */
	int refcount;
};

#define CONF_DEAD		0x0001

#define ConfDead(x)		((x)->flags & CONF_DEAD)
#define SetConfDead(x)		((x)->flags |= CONF_DEAD)
#define ClearConfDead(x)	((x)->flags &= ~CONF_DEAD)

#define CONF_OPER_ENCRYPTED     0x0010
#define CONF_OPER_DCC		0x0020

/* x is an oper_p */
#define ConfOperEncrypted(x)	((x)->flags & CONF_OPER_ENCRYPTED)
#define ConfOperDcc(x)		((x)->flags & CONF_OPER_DCC)

/* set in conf, but are moved to ->privs, x here is a connection */
#define CONF_OPER_ADMIN		0x0000100
#define CONF_OPER_ROUTE		0x0000200

#define CONF_OPER_US_REGISTER	0x00000001
#define CONF_OPER_US_SUSPEND	0x00000002
#define CONF_OPER_US_DROP	0x00000004
#define CONF_OPER_US_LIST	0x00000008
#define CONF_OPER_US_INFO	0x00000010
#define CONF_OPER_US_SETPASS	0x00000020
#define CONF_OPER_US_SETEMAIL	0x00000040

#define CONF_OPER_US_OPER	(CONF_OPER_US_LIST|CONF_OPER_US_INFO)
#define CONF_OPER_US_ADMIN	(CONF_OPER_US_REGISTER|CONF_OPER_US_SUSPEND|CONF_OPER_US_DROP|\
				 CONF_OPER_US_SETPASS|CONF_OPER_US_SETEMAIL|CONF_OPER_US_OPER)

#define CONF_OPER_CS_REGISTER	0x00000100
#define CONF_OPER_CS_SUSPEND	0x00000200
#define CONF_OPER_CS_DROP	0x00000400
#define CONF_OPER_CS_LIST	0x00000800
#define CONF_OPER_CS_INFO	0x00001000

#define CONF_OPER_CS_OPER	(CONF_OPER_CS_LIST|CONF_OPER_CS_INFO)
#define CONF_OPER_CS_ADMIN	(CONF_OPER_CS_OPER|CONF_OPER_CS_REGISTER|CONF_OPER_CS_SUSPEND|\
				 CONF_OPER_CS_DROP)

#define CONF_OPER_NS_DROP	0x00002000

#define CONF_OPER_OS_CHANNEL	0x00004000
#define CONF_OPER_OS_TAKEOVER	0x00008000
#define CONF_OPER_OS_OMODE	0x00010000
#define CONF_OPER_OS_MAINTAIN	0x00020000	/* NOT PART OF CONF_OPER_OS_ADMIN */
#define CONF_OPER_OS_IGNORE	0x00040000	/* NOT PART OF CONF_OPER_OS_ADMIN */
/****** CONF_OPER_BAN_NOMAX	0x00080000 *******/

#define CONF_OPER_OS_ADMIN	(CONF_OPER_OS_CHANNEL|CONF_OPER_OS_TAKEOVER|CONF_OPER_OS_OMODE)

#define CONF_OPER_OB_CHANNEL	0x00100000
#define CONF_OPER_GLOB_NETMSG	0x00200000
#define CONF_OPER_GLOB_WELCOME	0x00400000
#define CONF_OPER_JS_JUPE	0x00800000

#define CONF_OPER_BAN_KLINE	0x01000000
#define CONF_OPER_BAN_XLINE	0x02000000
#define CONF_OPER_BAN_RESV	0x04000000
#define CONF_OPER_BAN_PERM	0x08000000
#define CONF_OPER_BAN_REMOVE	0x10000000
#define CONF_OPER_BAN_SYNC	0x20000000
#define CONF_OPER_BAN_REGEXP	0x40000000
#define CONF_OPER_BAN_NOMAX	0x00080000

#define CONF_SERVER_AUTOCONN	0x0001

#define ConfServerAutoconn(x)	((x)->flags & CONF_SERVER_AUTOCONN)

extern struct _config_file config_file;
extern dlink_list conf_server_list;
extern dlink_list conf_oper_list;
extern FILE *conf_fbfile_in;

extern void conf_parse(int cold);

extern int lineno;
extern void yyerror(const char *msg);
extern int conf_fbgets(char *lbuf, int max_size);

extern void rehash(int sig);

extern void free_conf_oper(struct conf_oper *conf_p);
extern void deallocate_conf_oper(struct conf_oper *conf_p);
extern const char *conf_oper_flags(unsigned int flags);
extern const char *conf_service_flags(unsigned int flags);

extern void free_conf_server(struct conf_server *conf_p);

extern struct conf_server *find_conf_server(const char *name);

extern struct conf_oper *find_conf_oper(const char *username, const char *host,
					const char *server, const char *oper_username);

#endif
