/* $Id: rserv.h 25064 2008-02-08 21:16:19Z leeh $ */
#ifndef INCLUDED_rserv_h
#define INCLUDED_rserv_h

#define BUFSIZE 512
#define BUFSIZE_SAFE 450

#define MAX_DATE_STRING	32

#define PASSWDLEN	35
#define EMAILLEN	100
#define OPERNAMELEN	30
#define URLLEN		100

#define SUSPENDREASONLEN	200

int current_mark;
int testing_conf;

extern struct timeval system_time;
#define CURRENT_TIME system_time.tv_sec

extern void set_time(void);

extern void PRINTFLIKE(2, 3) die(int graceful, const char *format, ...);

extern int have_md5_crypt;

void init_crypt_seed(void);
const char *get_crypt(const char *password, const char *csalt);
const char *get_password(void);

char *rebuild_params(const char **, int, int);

int valid_servername(const char *);
int valid_sid(const char *);

struct client;
void count_memory(struct client *);

/* cidr.c */
int match_ips(const char *s1, const char *s2);
int match_cidr(const char *s1, const char *s2);

/* snprintf.c */
int rs_snprintf(char *, const size_t, const char *, ...);
int rs_vsnprintf(char *, const size_t, const char *, va_list);

#endif
