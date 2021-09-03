/* $Id: ucommand.h 23439 2007-01-13 14:58:17Z leeh $ */
#ifndef INCLUDED_ucommand_h
#define INCLUDED_ucommand_h

#define MAX_UCOMMAND_HASH 100

struct lconn;
struct cachefile;
struct client;

extern dlink_list ucommand_list;

struct ucommand_handler
{
	const char *cmd;
	int (*func)(struct client *, struct lconn *, const char **, int);
	unsigned int flags;	/* normal oper flags required */
	unsigned int sflags;	/* services flags required */
	int minpara;
        struct cachefile **helpfile;
};

extern void init_ucommand(void);
extern void handle_ucommand(struct lconn *, const char *command, 
				const char *parv[], int parc);
extern void add_ucommand_handler(struct client *, struct ucommand_handler *);
extern void add_ucommands(struct client *, struct ucommand_handler *);

extern struct ucommand_handler *find_ucommand(const char *command);

void load_ucommand_help(void);
void clear_ucommand_help(void);

#endif
