/* $Id: channel.h 26911 2010-02-22 19:36:09Z leeh $ */
#ifndef INCLUDED_channel_h
#define INCLUDED_channel_h

#define CHANNELLEN	200
#define KEYLEN		24

#define MAX_MODES	10

#define MAX_CHANNEL_TABLE	16384

extern dlink_list channel_list;

#define DIR_NONE -1
#define DIR_ADD  1
#define DIR_DEL  0

struct chmode
{
	unsigned int mode;
	char key[KEYLEN+1];
	int limit;
};

struct channel
{
	char name[CHANNELLEN+1];
	char topic[TOPICLEN+1];
	char topicwho[NICKUSERHOSTLEN+1];

	time_t tsinfo;
	time_t topic_tsinfo;

	dlink_list users;		/* users in this channel */
	dlink_list services;

	dlink_list bans;		/* +b */
	dlink_list excepts;		/* +e */
	dlink_list invites;		/* +I */

	struct chmode mode;

	dlink_node listptr;		/* node in channel_list */
	dlink_node nameptr;		/* node in channel hash */
};

struct chmember
{
	dlink_node chnode;		/* node in struct channel */
	dlink_node usernode;		/* node in struct client */

	struct channel *chptr;
	struct client *client_p;
	unsigned int flags;
};

#define MODE_INVITEONLY		0x0001
#define MODE_MODERATED		0x0002
#define MODE_NOEXTERNAL		0x0004
#define MODE_PRIVATE		0x0008
#define MODE_SECRET		0x0010
#define MODE_TOPIC		0x0020
#define MODE_LIMIT		0x0040
#define MODE_KEY		0x0080
#define MODE_REGONLY		0x0100
#define MODE_SSLONLY		0x0200

#define MODE_OPPED		0x0001
#define MODE_VOICED		0x0002
#define MODE_DEOPPED		0x0004

#define is_opped(x)	((x)->flags & MODE_OPPED)
#define is_voiced(x)	((x)->flags & MODE_VOICED)

extern void init_channel(void);

unsigned int hash_channel(const char *p);

int valid_chname(const char *name);

extern void add_channel(struct channel *chptr);
extern void del_channel(struct channel *chptr);
extern void free_channel(struct channel *chptr);
extern struct channel *find_channel(const char *name);

void remove_our_modes(struct channel *chptr);
void remove_bans(struct channel *chptr);

extern const char *chmode_to_string(struct chmode *mode);
extern const char *chmode_to_string_simple(struct chmode *mode);

extern struct chmember *add_chmember(struct channel *chptr, struct client *target_p, int flags);
extern void del_chmember(struct chmember *mptr);
extern struct chmember *find_chmember(struct channel *chptr, struct client *target_p);
#define is_member(chptr, target_p) ((find_chmember(chptr, target_p)) ? 1 : 0)

int find_exempt(struct channel *chptr, struct client *target_p);

extern unsigned long count_topics(void);

extern void join_service(struct client *service_p, const char *chname,
			time_t tsinfo, struct chmode *mode, int override);
extern int part_service(struct client *service_p, const char *chname);
extern void rejoin_service(struct client *service_p, struct channel *chptr, int reop);

/* c_mode.c */
int valid_ban(const char *banstr);

/* DO NOT DEREFERENCE THE VOID POINTER RETURNED FROM THIS */
void *del_ban(const char *banstr, dlink_list *list);

int parse_simple_mode(struct chmode *, const char **, int, int, int);
void parse_full_mode(struct channel *, struct client *, const char **, int, int, int);

#endif
