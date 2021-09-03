/* $Id: service.h 26911 2010-02-22 19:36:09Z leeh $ */
#ifndef INCLUDED_service_h
#define INCLUDED_service_h

struct client;
struct lconn;
struct ucommand_handler;
struct cachefile;

#define SCMD_WALK(i, svc) do { int m = svc->service->command_size / sizeof(struct service_command); \
				for(i = 0; i < m; i++)
#define SCMD_END		} while(0)

struct service_command
{
        const char *cmd;
        int (*func)(struct client *, struct lconn *, const char **, int);
	int minparc;
	struct cachefile **helpfile;
        int help_penalty;
        unsigned long cmd_use;
	int userreg;
	int operonly;
	int operflags;
};

struct service_handler
{
	const char *id;
	const char *name;
	const char *username;
	const char *host;
	const char *info;

        int flood_max;
        int flood_grace;

	struct service_command *command;
	unsigned long command_size;
        struct ucommand_handler *ucommand;

	void (*init)(void);
        void (*stats)(struct lconn *, const char **, int);
};

struct service_ignore
{
	char *mask;
	char *reason;
	char *oper;

	dlink_node ptr;
};

extern dlink_list service_list;
extern dlink_list ignore_list;

#define OPER_NAME(client_p, conn_p) ((conn_p) ? (conn_p)->name : \
		((client_p)->user->oper ? (client_p)->user->oper->name : "-"))
#define OPER_MASK(client_p, conn_p) ((conn_p) ? "-" : (client_p)->user->mask)

extern void rehash_help(void);

void init_services(void);

extern struct client *add_service();
extern struct client *find_service_id(const char *name);
extern void introduce_service(struct client *client_p);
extern void introduce_service_channels(struct client *client_p, int send_tb);
extern void introduce_services(void);
extern void introduce_services_channels(void);
extern void reintroduce_service(struct client *client_p);
extern void deintroduce_service(struct client *client_p);

extern struct client *merge_service(struct service_handler *handler_p, const char *target, int startup);

extern void update_service_floodcount(void *unused);

extern void handle_service_msg(struct client *service_p,
				struct client *client_p, char *text);
extern void handle_service(struct client *service_p, struct client *client_p,
                           const char *command, int parc, const char **parv, int msg);
extern void PRINTFLIKE(3, 4) service_error(struct client *service_p,
                          struct client *client_p, const char *, ...);
void service_err(struct client *service_p, struct client *client_p, int msgid, ...);

void PRINTFLIKE(4, 5) service_send(struct client *, struct client *,
                struct lconn *, const char *, ...);
void service_snd(struct client *, struct client *, struct lconn *, int msgid, ...);

extern void service_stats(struct client *service_p, struct lconn *);

#endif
