/* $Id: hook.h 27003 2010-03-21 13:08:59Z leeh $ */
#ifndef INCLUDED_hook_h
#define INCLUDED_hook_h

#define HOOK_JOIN_CHANNEL	0	/* someone joining a channel */
#define HOOK_MODE_OP		1
#define HOOK_MODE_SIMPLE	2	/* +ntsimplk */
#define HOOK_SQUIT_UNKNOWN	3	/* squit an unknown server */
#define HOOK_FINISHED_BURSTING	4
#define HOOK_SJOIN_LOWERTS	5
#define HOOK_BURST_LOGIN	6
#define HOOK_USER_LOGIN		7
#define HOOK_MODE_VOICE		8
#define HOOK_NEW_CLIENT		9	/* client is introduced to the network,
					 * outside of burst
					 */
#define HOOK_NICKCHANGE		10	/* client changing nick */
#define HOOK_SERVER_EOB		11	/* specific server sent EOB */
#define HOOK_DBSYNC		12
#define HOOK_NEW_CLIENT_BURST	13	/* new client during burst */
#define HOOK_DCC_AUTH		14	/* dcc client auths */
#define HOOK_DCC_EXIT		15	/* dcc client exits */
#define HOOK_USER_EXIT		16	/* user exits the network */
#define HOOK_SERVER_EXIT	17	/* server exits the network */
#define HOOK_MODE_BAN		18	/* mode +b done by a user only */
#define HOOK_CHANNEL_TOPIC	19	/* TOPIC/TB on a channel */
#define HOOK_LAST_HOOK		20

typedef int (*hook_func)(void *, void *);

extern void hook_add(hook_func func, int hook);
extern int hook_call(int hook, void *arg, void *arg2);

#endif
