/* $Id: s_chanserv.h 23704 2007-03-16 21:46:42Z leeh $ */
#ifndef INCLUDED_s_chanserv_h
#define INCLUDED_s_chanserv_h

struct user_reg;
struct chmode;

/* Flags stored in the DB: 0xFFFF */
#define CS_FLAGS_SUSPENDED	0x0001
#define CS_FLAGS_NOOPS		0x0002
#define CS_FLAGS_AUTOJOIN	0x0004
#define CS_FLAGS_WARNOVERRIDE	0x0008
#define CS_FLAGS_RESTRICTOPS	0x0010
#define CS_FLAGS_NOVOICES	0x0020
#define CS_FLAGS_NOVOICECMD	0x0040
#define CS_FLAGS_NOUSERBANS	0x0080

/* those flags shown in CHANSERV::INFO */
#define CS_FLAGS_SHOW	(CS_FLAGS_NOOPS|CS_FLAGS_AUTOJOIN|\
			 CS_FLAGS_WARNOVERRIDE|CS_FLAGS_RESTRICTOPS|\
			 CS_FLAGS_NOVOICES|CS_FLAGS_NOVOICECMD|\
			 CS_FLAGS_NOUSERBANS)

/* Flags not stored in the DB: 0xFFFF0000 */
#define CS_FLAGS_NEEDUPDATE	0x00010000
#define CS_FLAGS_INHABIT	0x00020000

#define CS_MEMBER_AUTOOP	0x001
#define CS_MEMBER_AUTOVOICE	0x002

/* used to validate flags on db load.. */
#define CS_MEMBER_ALL		(CS_MEMBER_AUTOOP|CS_MEMBER_AUTOVOICE)

struct chan_reg
{
	char *name;
	char *topic;
	char *url;
	char *suspender;
	char *suspend_reason;
	time_t suspend_time;
	struct chmode cmode;
	struct chmode emode;

	int flags;

	time_t tsinfo;
	time_t reg_time;
	time_t last_time;
	unsigned long bants;

	dlink_node node;

	dlink_list users;
	dlink_list bans;
};

struct member_reg
{
	struct user_reg *user_reg;
	struct chan_reg *channel_reg;

	int level;
	int flags;
	int suspend;
	unsigned long bants;

	char *lastmod;			/* last user to modify this membership */

	dlink_node usernode;
	dlink_node channode;
};

struct ban_reg
{
	char *mask;
	char *reason;
	char *username;
	int level;
	time_t hold;
	int marked;

	dlink_node channode;
};

#define CHAN_SUSPEND_EXPIRED(x) ((x)->flags & CS_FLAGS_SUSPENDED && (x)->suspend_time && \
				(x)->suspend_time <= CURRENT_TIME)

void free_channel_reg(struct chan_reg *);
void free_member_reg(struct member_reg *, int);

void s_chanserv_countmem(size_t *, size_t *, size_t *, size_t *, size_t *, size_t *, size_t *);

#endif
