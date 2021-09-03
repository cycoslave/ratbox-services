/* $Id: c_init.h 23888 2007-04-28 15:01:11Z leeh $ */
#ifndef INCLUDED_c_init_h
#define INCLUDED_c_init_h

/* c_error.c */
extern struct scommand_handler error_command;

/* c_message.c */
extern struct scommand_handler privmsg_command;

/* c_mode.c */
extern struct scommand_handler mode_command;
extern struct scommand_handler tmode_command;
extern struct scommand_handler bmask_command;

void preinit_s_alis(void);
void preinit_s_operbot(void);
void preinit_s_userserv(void);
void preinit_s_chanserv(void);
void preinit_s_jupeserv(void);
void preinit_s_operserv(void);
void preinit_s_nickserv(void);
void preinit_s_global(void);
void preinit_s_banserv(void);
void preinit_s_watchserv(void);
void preinit_s_memoserv(void);

/* u_stats.c */
extern struct ucommand_handler stats_ucommand;

#endif
