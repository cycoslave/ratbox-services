/* $Id: s_nickserv.h 20344 2005-05-06 21:51:24Z leeh $ */
#ifndef INCLUDED_nickserv_h
#define INCLUDED_nickserv_h

#define MAX_NICK_REG_HASH	65536

struct client;
struct user_reg;

struct nick_reg
{
	struct user_reg *user_reg;
	char name[NICKLEN+1];
	time_t reg_time;
	time_t last_time;
	int flags;
	dlink_node node;
	dlink_node usernode;
};

/* flags stored in db: 0xFFFF */
#define NS_FLAGS_WARN		0x0001

/* flags not stored in db: 0xFFFF000 */
#define NS_FLAGS_NEEDUPDATE	0x00010000

extern void free_nick_reg(struct nick_reg *);

#endif
