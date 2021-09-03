/* $Id: scommand.h 20082 2005-01-27 17:46:55Z leeh $ */
#ifndef INCLUDED_scommand_h
#define INCLUDED_scommand_h

#define MAX_SCOMMAND_HASH 100

struct client;

typedef void (*scommand_func)(struct client *, const char *parv[], int parc);

struct scommand_handler
{
	const char *cmd;
	scommand_func func;
	int flags;
	dlink_list hooks;
};

#define FLAGS_UNKNOWN	0x0001

extern void init_scommand(void);
extern void handle_scommand(const char *, const char *, const char **, int);
extern void add_scommand_handler(struct scommand_handler *);

extern void add_scommand_hook(scommand_func func, const char *command);
extern void del_scommand_hook(scommand_func func, const char *command);

#endif
