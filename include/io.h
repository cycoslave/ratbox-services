/* $Id: io.h 26911 2010-02-22 19:36:09Z leeh $ */
#ifndef INCLUDED_io_h
#define INCLUDED_io_h

struct client;
struct conf_oper;

struct send_queue
{
	const char *buf;
	int len;
	int pos;
};

struct lconn
{
	struct client *client_p;
	char *name;
	char *sid;
        char *pass;

	int fd;
	int flags;
	unsigned int privs;		/* privs as an oper */
	unsigned int sprivs;		/* privs on services */
	int watchflags;
	time_t first_time;
	time_t last_time;

        struct conf_oper *oper;

	void (*io_read)(struct lconn *);
	int (*io_write)(struct lconn *);
	void (*io_close)(struct lconn *);

	char recvbuf[BUFSIZE+1];
	size_t recvbuf_offset;

	dlink_list sendq;
};

extern struct lconn *server_p;
extern dlink_list connection_list;

#define CONN_CONNECTING		0x0001
#define CONN_DCCIN		0x0002
#define CONN_DCCOUT		0x0004
#define CONN_HANDSHAKE          0x0008
#define CONN_DEAD		0x0010
#define CONN_SENTPING           0x0020
#define CONN_TS			0x0040
#define CONN_CAP_SERVICE	0x0080
#define CONN_CAP_RSFNC		0x0100
#define CONN_CAP_TB		0x0200
#define CONN_TS6		0x0400
/* CONTINUES ... */

#define ConnConnecting(x)	((x)->flags & CONN_CONNECTING)
#define ConnDccIn(x)		((x)->flags & CONN_DCCIN)
#define ConnDccOut(x)		((x)->flags & CONN_DCCOUT)
#define ConnHandshake(x)	((x)->flags & CONN_HANDSHAKE)
#define ConnDead(x)		((x)->flags & CONN_DEAD)
#define ConnSentPing(x)		((x)->flags & CONN_SENTPING)
#define ConnTS(x)		((x)->flags & CONN_TS)
#define ConnTS6(x)		((x)->flags & CONN_TS6)
#define ConnCapService(x)	((x)->flags & CONN_CAP_SERVICE)
#define ConnCapRSFNC(x)		((x)->flags & CONN_CAP_RSFNC)
#define ConnCapTB(x)		((x)->flags & CONN_CAP_TB)

#define SetConnConnecting(x)	((x)->flags |= CONN_CONNECTING)
#define SetConnDccIn(x)		((x)->flags |= CONN_DCCIN)
#define SetConnDccOut(x)	((x)->flags |= CONN_DCCOUT)
#define SetConnHandshake(x)	((x)->flags |= CONN_HANDSHAKE)
#define SetConnDead(x)		((x)->flags |= CONN_DEAD)
#define SetConnSentPing(x)	((x)->flags |= CONN_SENTPING)
#define SetConnTS(x)		((x)->flags |= CONN_TS)
#define SetConnTS6(x)		((x)->flags |= CONN_TS6)

#define ClearConnConnecting(x)	((x)->flags &= ~CONN_CONNECTING)
#define ClearConnHandshake(x)	((x)->flags &= ~CONN_HANDSHAKE)
#define ClearConnSentPing(x)	((x)->flags &= ~CONN_SENTPING)

/* server flags */
#define CONN_FLAGS_UNTERMINATED	0x01000
#define CONN_FLAGS_EOB		0x02000
#define CONN_FLAGS_SENTBURST	0x04000
/* CONTINUES ... */

#define SetConnSentBurst(x)	((x)->flags |= CONN_FLAGS_SENTBURST)
#define SetConnEOB(x)		((x)->flags |= CONN_FLAGS_EOB)

#define finished_bursting	((server_p) && (server_p->flags & CONN_FLAGS_EOB))
#define sent_burst		((server_p) && (server_p->flags & CONN_FLAGS_SENTBURST))

/* user flags */
#define CONN_FLAGS_AUTH		0x10000
#define CONN_FLAGS_CHAT		0x20000

#define UserAuth(x)		((x)->flags & CONN_FLAGS_AUTH)
#define SetUserAuth(x)		((x)->flags |= CONN_FLAGS_AUTH)
#define UserChat(x)		((x)->flags & CONN_FLAGS_CHAT)
#define SetUserChat(x)		((x)->flags |= CONN_FLAGS_CHAT)
#define ClearUserChat(x)	((x)->flags &= ~CONN_FLAGS_CHAT)

extern void read_io(void);

extern void connect_to_server(void *unused);
extern void connect_to_client(struct client *client_p, struct conf_oper *oper_p,
				const char *host, int port);
extern void connect_from_client(struct client *client_p, struct conf_oper *oper_p,
				const char *servicenick);

extern void PRINTFLIKE(1, 2) sendto_server(const char *format, ...);
extern void PRINTFLIKE(2, 3) sendto_one(struct lconn *, const char *format, ...);
extern void PRINTFLIKE(1, 2) sendto_all(const char *format, ...);
extern void PRINTFLIKE(2, 3) sendto_all_chat(struct lconn *, const char *format, ...);

extern int sock_create(int);
extern int sock_open(const char *host, int port, const char *vhost, int type);
extern void sock_close(struct lconn *conn_p);
extern int sock_write(struct lconn *conn_p, const char *buf, size_t len);

extern unsigned long get_sendq(struct lconn *conn_p);

#endif
