#ifndef INCLUDED_config_h
#define INCLUDED_config_h

/* Paths to various things.
 * IMPORTANT: if you alter the directories these files go to,
 *            you must create those paths yourself.
 */
#define CONF_PATH	SYSCONFDIR "/ratbox-services.conf"
#define PID_PATH	RUNDIR "/ratbox-services.pid"
#define LOG_PATH	LOGDIR "/ratbox-services.log"
#define HELP_PATH       HELPDIR
#define DB_PATH		SYSCONFDIR "/ratbox-services.db"

/* SMALL_NETWORK
 * If your network is fairly small, enable this to save some memory.
 */
#define SMALL_NETWORK

/*              ---------------------------             */
/*              END OF CONFIGURABLE OPTIONS             */
/*              ---------------------------             */




#define MAX_FD		1000

#define RSERV_VERSION		"1.2.4"

#ifdef SMALL_NETWORK
#define HEAP_CHANNEL    64
#define HEAP_CHMEMBER   128
#define HEAP_CLIENT     128
#define HEAP_USER       128
#define HEAP_SERVER     16
#define HEAP_HOST	128
#define HEAP_DLINKNODE	128
#else
#define HEAP_CHANNEL    1024
#define HEAP_CHMEMBER   1024
#define HEAP_CLIENT     1024
#define HEAP_USER       1024
#define HEAP_HOST	1024
#define HEAP_SERVER     32
#define HEAP_DLINKNODE	1024
#endif

#define HEAP_CACHEFILE  16
#define HEAP_CACHELINE  128
#define HEAP_USER_REG	256
#define HEAP_CHANNEL_REG	128
#define HEAP_MEMBER_REG	256
#define HEAP_BAN_REG	512
#define HEAP_NICK_REG	256

#endif
/* $Id: config.h 27023 2010-04-22 18:27:28Z leeh $ */
