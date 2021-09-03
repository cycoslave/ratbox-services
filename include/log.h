/* $Id: log.h 22828 2006-06-25 18:34:00Z leeh $ */
#ifndef INCLUDED_log_h
#define INCLUDED_log_h

struct client;
struct lconn;

extern void open_logfile(void);
extern void open_service_logfile(struct client *service_p);
extern void reopen_logfiles(void);

extern void PRINTFLIKE(1, 2) mlog(const char *format, ...);

extern void PRINTFLIKE(7, 8) zlog(struct client *, int loglevel, unsigned int watchlevel, int oper,
					struct client *, struct lconn *,
					const char *format, ...);

#endif
