/* $Id: client.h 23809 2007-04-05 20:09:55Z leeh $ */
#define USERLEN 10
#define UIDLEN 9
#define HOSTLEN 63
#define REALLEN 50
#define REASONLEN 50

#define USERHOSTLEN (USERLEN + HOSTLEN + 1)
#define NICKUSERHOSTLEN	(NICKLEN + USERLEN + HOSTLEN + 2)

#define MAX_NAME_HASH 65536
#define MAX_HOST_HASH 65536

extern dlink_list user_list;
extern dlink_list oper_list;
extern dlink_list server_list;
extern dlink_list exited_list;

struct lconn;
struct service_command;
struct ucommand_handler;
struct cachefile;

struct client
{
	char name[HOSTLEN+1];
	char info[REALLEN+1];
	char uid[UIDLEN+1];
	int flags;

	struct server *server;
	struct user *user;
	struct service *service;
	struct client *uplink;		/* server this is connected to */

	dlink_node nameptr;		/* dlink_node in name_table */
	dlink_node uidptr;
	dlink_node listnode;		/* in client/server/exited_list */
	dlink_node upnode;		/* in uplinks servers/clients list */
};

struct user
{
	char username[USERLEN+1];
	char host[HOSTLEN+1];
	char *ip;
	char *servername;		/* name of server its on */
	char *mask;

	int umode;			/* usermodes this client has */
	time_t tsinfo;

	unsigned int flood_count;
	time_t flood_time;

	struct user_reg *user_reg;
	struct conf_oper *oper;
	int watchflags;

	dlink_list channels;

	dlink_node servptr;
	dlink_node hostptr;
	dlink_node uhostptr;
};

struct server
{
	dlink_list users;
	dlink_list servers;

	int hops;
};

struct service
{
	char username[USERLEN+1];
	char host[HOSTLEN+1];
	char id[NICKLEN+1];
	int status;
	unsigned int flags;

	dlink_list channels;		/* the channels this service is in */

	FILE *logfile;

	int flood;
        int flood_max;
        int flood_grace;

	int loglevel;

	struct service_command *command;
	unsigned long command_size;
        struct ucommand_handler *ucommand;

	/* used when another service is merged into this */
	struct service_command *orig_command;
	unsigned long orig_command_size;
        struct ucommand_handler *orig_ucommand;

	/* list of merged service handlers */
	dlink_list merged_handler_list;

        unsigned long help_count;
        unsigned long ehelp_count;
        unsigned long paced_count;
        unsigned long ignored_count;

	struct cachefile **help;
	struct cachefile **helpadmin;

	void (*init)(void);
        void (*stats)(struct lconn *, const char **, int);
};

struct host_entry
{
	char *name;
	int flood;
	time_t flood_expire;
	int cregister;
	time_t cregister_expire;
	int uregister;
	time_t uregister_expire;
	dlink_node node;
};

#define UID(x) (EmptyString((x)->uid) ? (x)->name : (x)->uid)
#define MYUID ((server_p && !EmptyString(server_p->sid)) ? config_file.sid : config_file.name)
#define SVC_UID(x) ((server_p && !EmptyString(server_p->sid)) ? (x)->uid : (x)->name)

#define IsServer(x) ((x)->server != NULL)
#define IsUser(x) ((x)->user != NULL)
#define IsService(x) ((x)->service != NULL)

#define FLAGS_DEAD	0x0001
#define FLAGS_EOB	0x0002
#define FLAGS_RSFNC	0x0004

#define IsDead(x)	((x) && (x)->flags & FLAGS_DEAD)
#define SetDead(x)	((x)->flags |= FLAGS_DEAD)
#define IsEOB(x)	((x) && (x)->flags & FLAGS_EOB)
#define SetEOB(x)	((x)->flags |= FLAGS_EOB)

#define CLIENT_INVIS	0x001
#define CLIENT_OPER	0x002
#define CLIENT_ADMIN	0x004

#define ClientInvis(x)	 ((x)->user && (x)->user->umode & CLIENT_INVIS)
#define ClientOper(x)	 ((x)->user && (x)->user->umode & CLIENT_OPER)
#define is_oper(x)       ((x)->user && (x)->user->umode & CLIENT_OPER)
#define ClientAdmin(x)	 ((x)->user && (x)->user->umode & CLIENT_ADMIN)

#define SERVICE_OPERED		0x001 /* service is opered */
#define SERVICE_MSGSELF		0x002 /* messages come from services nick */
#define SERVICE_DISABLED	0x004 /* should this service be disabled? */
#define SERVICE_SHORTHELP	0x008 /* service gives short help */
#define SERVICE_STEALTH		0x010 /* ignores non-opers */
#define SERVICE_LOGINHELP	0x020 /* needs to be logged in with userserv */
#define SERVICE_WALLOPADM	0x040 /* sends wallops for admin commands */
#define SERVICE_SHORTCUT	0x080 /* privmsg disabled, /service commands required */

#define ServiceOpered(x)	((x)->service->flags & SERVICE_OPERED)
#define ServiceMsgSelf(x)	((x)->service->flags & SERVICE_MSGSELF)
#define ServiceDisabled(x)	((x)->service->flags & SERVICE_DISABLED)
#define ServiceShortHelp(x)	((x)->service->flags & SERVICE_SHORTHELP)
#define ServiceStealth(x)	((x)->service->flags & SERVICE_STEALTH)
#define ServiceLoginHelp(x)	((x)->service->flags & SERVICE_LOGINHELP)
#define ServiceWallopAdm(x)	((x)->service->flags & SERVICE_WALLOPADM)
#define ServiceShortcut(x)	((x)->service->flags & SERVICE_SHORTCUT)

#define SERVICE_INTRODUCED	0x001 /* service has been introduced */
#define SERVICE_REINTRODUCE	0x002 /* service needs reintroducing */

#define ServiceIntroduced(x)	((x)->service->status & SERVICE_INTRODUCED)
#define ServiceReintroduce(x)	((x)->service->status & SERVICE_REINTRODUCE)

#define SetServiceIntroduced(x)	((x)->service->status |= SERVICE_INTRODUCED)
#define SetServiceReintroduce(x) ((x)->service->status |= SERVICE_REINTRODUCE)
#define ClearServiceIntroduced(x)  ((x)->service->status &= ~SERVICE_INTRODUCED)
#define ClearServiceReintroduce(x) ((x)->service->status &= ~SERVICE_REINTRODUCE)

extern void init_client(void);

unsigned int hash_name(const char *p);

char *generate_uid(void);

extern void add_client(struct client *target_p);
extern void del_client(struct client *target_p);
extern struct client *find_client(const char *name);
extern struct client *find_named_client(const char *name);
extern struct client *find_user(const char *name, int search_uid);
extern struct client *find_uid(const char *name);
extern struct client *find_server(const char *name);
extern struct client *find_service(const char *name);
struct host_entry *find_host(const char *name);

extern void exit_client(struct client *target_p);
extern void free_client(struct client *target_p);

extern int string_to_umode(const char *p, int current_umode);
extern const char *umode_to_string(int umode);
