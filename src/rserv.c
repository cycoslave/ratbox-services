/* src/rserv.c
 *   Contains initialisation stuff for ratbox-services.
 *
 * Copyright (C) 2003-2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2007 ircd-ratbox development team
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3.The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * $Id: rserv.c 27021 2010-04-22 18:21:17Z leeh $
 */
#include "stdinc.h"
#include <signal.h>
#include <sys/resource.h>

#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#include "rserv.h"
#include "langs.h"
#include "rsdb.h"
#include "conf.h"
#include "io.h"
#include "event.h"
#include "scommand.h"
#include "ucommand.h"
#include "client.h"
#include "channel.h"
#include "log.h"
#include "c_init.h"
#include "service.h"
#include "balloc.h"
#include "cache.h"
#include "newconf.h"
#include "hook.h"
#include "watch.h"
#include "serno.h"
#include "s_userserv.h"
#include "s_chanserv.h"

struct timeval system_time;

int have_md5_crypt;
int current_mark;
int testing_conf = 0;

static int need_rehash = 0;
static int need_rehash_help = 0;
static int need_waitpid = 0;
static void sig_hup(int);
static void sig_term(int);
static void sig_usr1(int);
static void sig_chld(int);
static void check_rehash(void *);

void
die(int graceful, const char *format, ...)
{
	char buf[BUFSIZE];
	va_list args;

	if(graceful)
		hook_call(HOOK_DBSYNC, NULL, NULL);

	rsdb_shutdown();

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(testing_conf)
	{
		fprintf(stderr, "Services terminated: (%s)\n", buf);
		exit(1);
	}

	sendto_all("Services terminated: (%s)", buf);
	mlog("ratbox-services terminated: (%s)", buf);
	exit(1);
}

void
set_time(void)
{
	struct timeval newtime;

	newtime.tv_sec = newtime.tv_usec = 0;

	if(gettimeofday(&newtime, NULL) == -1)
		die(1, "Clock failure.");

	system_time.tv_sec = newtime.tv_sec;
	system_time.tv_usec = newtime.tv_usec;
}

static void
setup_corefile(void)
{
	struct rlimit rlim;

	if(!getrlimit(RLIMIT_CORE, &rlim))
	{
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_CORE, &rlim);
	}
}

static void
check_pidfile(void)
{
	FILE *fb;
	char buf[32];
	pid_t filepid;

	if((fb = fopen(PID_PATH, "r")) != NULL)
	{
		if(fgets(buf, 20, fb) != NULL && errno != ENOENT)
		{
			filepid = atoi(buf);
			if(!kill(filepid, 0))
			{
				printf("ratbox-services: daemon already running\n");
				exit(-1);
			}
		}

		fclose(fb);
	}
}

static void
write_pidfile(void)
{
	FILE *fb;
	char buf[32];
	
	if((fb = fopen(PID_PATH, "w")) != NULL)
	{
		snprintf(buf, sizeof(buf), "%u\n", (unsigned int) getpid());

		if(fputs(buf, fb) == -1)
			mlog("ERR: Error writing to pid file %s (%s)",
			     PID_PATH, strerror(errno));
		fflush(fb);
		fclose(fb);
	}
	else
		mlog("ERR: Error opening pid file %s", PID_PATH);
}

static void
print_help(void)
{
	printf("ratbox-services [-h|-v|-f|-t]\n");
	printf(" -h show this help\n");
	printf(" -v show version\n");
	printf(" -f foreground mode\n");
	printf(" -t test config\n");
	printf(" -r change root directory\n");
	printf(" -g change the real and effective group ID\n");
	printf(" -u change the real and effective user ID\n");
}

static void
check_md5_crypt(void)
{
	if(strcmp((crypt("validate", "$1$tEsTiNg1")), "$1$tEsTiNg1$Orp/Maa6pOxfOpGWjmtVE/") == 0)
		have_md5_crypt = 1;
	else
		have_md5_crypt = 0;
}

static void
print_startup(int pid, int nofork)
{
	printf("ratbox-services: version %s(%s)\n",
		RSERV_VERSION, SERIALNUM);
	printf("ratbox-services: pid %d\n", pid);
	printf("ratbox-services: running in %s\n",
		nofork ? "foreground" : "background");
}

int 
main(int argc, char *argv[])
{
	char c;
	int nofork = 0;
	int childpid;
	int chroot_uid = 0;
	int chroot_gid = 0;
	int retval;
	char *chroot_path = NULL;

	/* The seteuid() system call (setegid()) sets the effective user ID (group
	 * ID) of the current process.  The effective user ID may be set to the
	 * value of the real user ID or the saved set-user-ID (see intro(2) and
	 * execve(2)); in this way, the effective user ID of a set-user-ID exe-
	 * cutable may be toggled by switching to the real user ID, then re-enabled
	 * by reverting to the set-user-ID value.  Similarly, the effective group ID
	 * may be set to the value of the real group ID or the saved set-group-ID.
	 */
	if(getuid() == 0 && geteuid() != 0)
	{
		if(seteuid(0) != 0)
		{
			printf("ratbox-services terminated: unable to geteuid(0) whilst getuid() == 0\n");
			perror("seteuid(0)");
			exit(EXIT_FAILURE);
		}
	}

	while((c = getopt(argc, argv, "hvftr:g:u:")) != -1)
	{
		switch(c)
		{
			case 'h':
				print_help();
				exit(0);
				break;
			case 'v':
				printf("ratbox-services: version %s(%s)\n",
					RSERV_VERSION, SERIALNUM);
				exit(0);
				break;
			case 'f':
				nofork = 1;
				break;
			case 't':
				testing_conf = 1;
				break;
			case 'r':
				chroot_path = my_strdup(optarg);
				break;

			case 'g':
				chroot_gid = atoi(optarg);
				break;
			case 'u':
				chroot_uid = atoi(optarg);
				break;
		}
	}

	if(chroot_path)
	{
		/* The chdir() system call causes the named directory to become the
		 * current working directory, that is, the starting point for path
		 * searches of pathnames not beginning with a slash, `/'.
		 *
		 * In order for a directory to become the current directory, a
		 * process must have execute (search) access to the directory.
		 */
		retval = chdir(chroot_path);

		if(retval)
		{
			printf("ratbox-services terminated: unable to chdir() to %s: %s\n",
				chroot_path, strerror(errno));

			perror("chdir()");
			exit(EXIT_FAILURE);
		}

		retval = chroot(chroot_path);

		if(retval)
		{
			printf("ratbox-services terminated: unable to chroot() to %s: %s\n",
				chroot_path, strerror(errno));

			perror("chroot()");
			exit(EXIT_FAILURE);
		}

		printf("ratbox-services: root directory now %s\n", chroot_path);

		my_free(chroot_path);
	}

	if(chroot_gid)
	{
		/* The setgid() system call sets the real and effective group IDs
		 * and the saved set-group-ID of the current process to the
		 * specified value.  The setgid() system call is permitted if the
		 * specified ID is equal to the real group ID or the effective
		 * group ID of the process, or if the effective user ID is that of
		 * the super user.
		 */
		retval = setgid(chroot_gid);
		if(retval)
		{
			printf("ratbox-services terminated: unable to setgid(%d): %s\n",
				chroot_gid, strerror(errno));
				
			perror("setgid()");
			exit(EXIT_FAILURE);
		}

		printf("ratbox-services: real and effective group ID now %d\n",
			chroot_gid);
	}

	if(chroot_uid)
	{
		/* The setuid() system call sets the real and effective user IDs
		 * and the saved set-user-ID of the current process to the
		 * specified value.  The setuid() system call is permitted if the
		 * specified ID is equal to the real user ID or the effective user
		 * ID of the process, or if the effective user ID is that of the
		 * super user.
		 */
		retval = setuid(chroot_uid);
		if(retval)
		{
			printf("ratbox-services terminated: unable to setuid(%d): %s\n",
				chroot_uid, strerror(errno));
			perror("setuid()");
			exit(EXIT_FAILURE);
		}

		printf("ratbox-services: real and effective user ID now %u\n",
			chroot_uid);
	}

	if(geteuid() == 0)
	{
		printf("ratbox-services will not run as root\n");
		return -1;
	}

	if(chdir(PREFIX))
	{
		printf("ratbox-services terminated: unable to chdir() to %s: %s\n",
			PREFIX, strerror(errno));
		return -1;
	}

	check_md5_crypt();

	setup_corefile();

	set_time();

#ifdef __CYGWIN__
        nofork = 1;
#endif

	if(testing_conf)
		nofork = 1;

        if(!testing_conf)
	{
        	check_pidfile();

		if(!nofork)
		{
			childpid = fork();

			switch (childpid)
			{
				case -1:
					perror("fork()");
					exit(3);
				case 0:
					close(STDIN_FILENO);
					close(STDOUT_FILENO);
					close(STDERR_FILENO);
					if (setsid() == -1)
						die(0, "setsid() error");

					break;
				default:
					print_startup(childpid, nofork);
					return 0;
			}
		}
		else
			print_startup(getpid(), nofork);
	}

	/* log requires time is set */
	open_logfile();

	mlog("ratbox-services started%s",
		testing_conf ? " (config test)" : "");

	signal(SIGHUP, sig_hup);
	signal(SIGTERM, sig_term);
	signal(SIGUSR1, sig_usr1);
	signal(SIGCHLD, sig_chld);

	signal(SIGTRAP, SIG_IGN); /* Needed on FreeBSD and possibly others */
	signal(SIGPIPE, SIG_IGN);

	/* in case of typo */
	signal(SIGUSR2, SIG_IGN);

	current_mark = 0;

	init_events();

	/* adding events uses the PRNG */
	init_crypt_seed();

	/* balloc requires events */
        init_balloc();

	/* tools requires balloc */
	init_tools();

	/* conf/commands/help all need base language stuff */
	init_langs();

	/* commands require cache */
	init_cache();
	init_scommand();
	init_ucommand();
	init_client();
	init_channel();

	/* pre initialise our services so the conf parser is ok, these
	 * require the balloc, events and init_client()
	 */
#ifdef ENABLE_USERSERV
	preinit_s_userserv();
#ifdef ENABLE_CHANSERV
	/* requires userserv inited */
	preinit_s_chanserv();
#endif
#ifdef ENABLE_NICKSERV
	/* requires userserv inited */
	preinit_s_nickserv();
#endif
#endif
#ifdef ENABLE_OPERSERV
	preinit_s_operserv();
#endif
#ifdef ENABLE_JUPESERV
	preinit_s_jupeserv();
#endif
#ifdef ENABLE_GLOBAL
	preinit_s_global();
#endif
#ifdef ENABLE_BANSERV
	preinit_s_banserv();
#endif
#ifdef ENABLE_ALIS
	preinit_s_alis();
#endif
#ifdef ENABLE_OPERBOT
	preinit_s_operbot();
#endif
#ifdef ENABLE_WATCHSERV
	preinit_s_watchserv();
#endif
#ifdef ENABLE_MEMOSERV
	preinit_s_memoserv();
#endif

	/* load specific commands */
        add_scommand_handler(&error_command);
	add_scommand_handler(&mode_command);
	add_scommand_handler(&tmode_command);
	add_scommand_handler(&bmask_command);
	add_scommand_handler(&privmsg_command);

	first_time = CURRENT_TIME;

	if(testing_conf)
		fprintf(stderr, "Conf check started\n");

	/* conf requires log is opened */
        newconf_init();
	/* must be done after adding services. */
	conf_parse(1);

	if(testing_conf)
	{
		fprintf(stderr, "\nConf check finished\n");
		exit(0);
	}

	/* must be done after parsing the config, for database {}; */
	rsdb_init();

	/* db must be done before this */
	init_services();

	eventAdd("update_service_floodcount", update_service_floodcount, 
		NULL, 1);
	eventAdd("check_rehash", check_rehash, NULL, 2);

       	write_pidfile();

	/* we need the correct time here so the timeout to connect() will
	 * work.
	 */
	set_time();
	connect_to_server(NULL);

	/* enter main IO loop */
	read_io();

	return 0;
}

void sig_hup(int sig)
{
	need_rehash = 1;
	signal(SIGHUP, sig_hup);
}

void sig_term(int sig)
{
	die(0, "Got signal SIGTERM");
}

void sig_usr1(int sig)
{
	need_rehash_help = 1;
	signal(SIGUSR1, sig_usr1);
}

void
sig_chld(int sig)
{
	need_waitpid = 1;
	signal(SIGCHLD, sig_chld);
}

void check_rehash(void *unused)
{
	if(need_rehash)
	{
		rehash(1); /* Caught a signal */
		need_rehash = 0;
	}

	if(need_rehash_help)
	{
		mlog("services rehashing: got SIGUSR1, reloading help/translations");
		sendto_all("services rehashing: got SIGUSR1, reloading help/translations");
		rehash_help();
		lang_clear_trans();
		lang_load_trans();
		need_rehash_help = 0;
	}

	if(need_waitpid)
	{
		while(waitpid(-1, NULL, WNOHANG) > 0)
			;

		need_waitpid = 0;
	}
}

char *
rebuild_params(const char **parv, int parc, int start)
{
	static char buf[BUFSIZE];

	buf[0] = '\0';

	if (start < parc)
	{
		strlcat(buf, parv[start], sizeof(buf));
		start++;

		for(; start < parc; start++)
		{
			strlcat(buf, " ", sizeof(buf));

			if(strlcat(buf, parv[start], sizeof(buf)) >= sizeof(buf))
				break;
		}
	}

	return buf;
}

int
valid_servername(const char *name)
{
	int dots = 0;

	if(IsDigit(*name))
		return 0;

	for(; *name; name++)
	{
		if(!IsServChar(*name))
			return 0;
		else if(*name == '.')
			dots++;
	}

	if(!dots)
		return 0;

	return 1;
}

int
valid_sid(const char *name)
{
	if(IsDigit(name[0]) && (IsDigit(name[1]) || IsLetter(name[1])) &&
	   (IsDigit(name[2]) || IsLetter(name[2])) && name[3] == '\0')
		return 1;

	return 0;
}

/* This is in s_watchserv.c -- if we dont have that, we cant use it. */
#ifndef ENABLE_WATCHSERV
void
watch_send(unsigned int flag, struct client *client_p, struct lconn *conn_p,
		int oper, const char *format, ...)
{
}
#endif

static size_t
count_memory_string(const char *str)
{
	if(!EmptyString(str))
		return(strlen(str) + 1);

	return 0;
}

void
count_memory(struct client *client_p)
{
	BlockHeap *bh;
	struct conf_server *sconf;
	struct conf_oper *oconf;
	dlink_node *ptr;

#ifdef ENABLE_USERSERV
	size_t sz_user_reg_password = 0;
	size_t sz_user_reg_email = 0;
	size_t sz_user_reg_suspend = 0;
	size_t sz_member_reg_lastmod = 0;
#endif

#ifdef ENABLE_CHANSERV
	size_t sz_chan_reg_name = 0;
	size_t sz_chan_reg_topic = 0;
	size_t sz_chan_reg_url = 0;
	size_t sz_chan_reg_suspend = 0;

	size_t sz_ban_reg_mask = 0;
	size_t sz_ban_reg_reason = 0;
	size_t sz_ban_reg_username = 0;
#endif

	size_t sz_hash_overhead = 0;

	size_t sz_conf = 0;

#ifdef ENABLE_USERSERV
	s_userserv_countmem(&sz_user_reg_password, &sz_user_reg_email, 
				&sz_user_reg_suspend, &sz_member_reg_lastmod);
#endif

#ifdef ENABLE_CHANSERV
	s_chanserv_countmem(&sz_chan_reg_name, &sz_chan_reg_topic,
				&sz_chan_reg_url, &sz_chan_reg_suspend,
				&sz_ban_reg_mask, &sz_ban_reg_reason,
				&sz_ban_reg_username);
#endif

	/* XXX NUMERIC */
#ifdef ENABLE_USERSERV
	sendto_server(":%s 988 %s :USERSERV", MYNAME, client_p->name);
	sendto_server(":%s 988 %s :   Password  : %u",
			MYNAME, client_p->name, (unsigned int) sz_user_reg_password);
	sendto_server(":%s 988 %s :   Email     : %u",
			MYNAME, client_p->name, (unsigned int) sz_user_reg_email);
	sendto_server(":%s 988 %s :   Suspend   : %u",
			MYNAME, client_p->name, (unsigned int) sz_user_reg_suspend);
	sendto_server(":%s 988 %s :   Member mod: %u",
			MYNAME, client_p->name, (unsigned int) sz_member_reg_lastmod);
#endif
#ifdef ENABLE_CHANSERV
	sendto_server(":%s 988 %s :CHANSERV", MYNAME, client_p->name);
	sendto_server(":%s 988 %s :   Name      : %u",
			MYNAME, client_p->name, (unsigned int) sz_chan_reg_name);
	sendto_server(":%s 988 %s :   Topic     : %u",
			MYNAME, client_p->name, (unsigned int) sz_chan_reg_topic);
	sendto_server(":%s 988 %s :   URL       : %u",
			MYNAME, client_p->name, (unsigned int) sz_chan_reg_url);
	sendto_server(":%s 988 %s :   Suspend   : %u",
			MYNAME, client_p->name, (unsigned int) sz_chan_reg_suspend);
	sendto_server(":%s 988 %s :   Ban Mask  : %u",
			MYNAME, client_p->name, (unsigned int) sz_ban_reg_mask);
	sendto_server(":%s 988 %s :   Ban Reason: %u",
			MYNAME, client_p->name, (unsigned int) sz_ban_reg_reason);
	sendto_server(":%s 988 %s :   Ban User  : %u",
			MYNAME, client_p->name, (unsigned int) sz_ban_reg_username);
#endif

	sendto_server(":%s 988 %s :BLOCKHEAP", MYNAME, client_p->name);

	DLINK_FOREACH(ptr, heap_lists.head)
	{
		size_t sz_bh_used;
		size_t sz_bh_free;
		size_t sz_bh_usedmem;
		size_t sz_bh_freemem;

		bh = ptr->data;

		BlockHeapUsage(bh, &sz_bh_used, &sz_bh_free, &sz_bh_usedmem, &sz_bh_freemem);

		sendto_server(":%s 988 %s :   %s: %u(%u) %u(%u)",
				MYNAME, client_p->name, bh->name, 
				sz_bh_used, sz_bh_usedmem,
				sz_bh_free, sz_bh_freemem);
	}

	sz_hash_overhead += sizeof(dlink_list) * MAX_NAME_HASH;		/* name_table */
	sz_hash_overhead += sizeof(dlink_list) * MAX_NAME_HASH;		/* uid_table */
	sz_hash_overhead += sizeof(dlink_list) * MAX_HOST_HASH;		/* host_table */
	sz_hash_overhead += sizeof(dlink_list) * MAX_CHANNEL_TABLE;	/* channel_table */
	sz_hash_overhead += sizeof(dlink_list) * MAX_NAME_HASH;		/* user_reg_table */
	sz_hash_overhead += sizeof(dlink_list) * MAX_CHANNEL_TABLE;	/* chan_reg_table */
	sz_hash_overhead += sizeof(dlink_list) * MAX_NAME_HASH;		/* nick_reg_table */

	sendto_server(":%s 988 %s :Hash Overhead: %u",
			MYNAME, client_p->name, (unsigned int) sz_hash_overhead);

	sz_conf += count_memory_string(config_file.name);
	sz_conf += count_memory_string(config_file.sid);
	sz_conf += count_memory_string(config_file.gecos);
	sz_conf += count_memory_string(config_file.vhost);
	sz_conf += count_memory_string(config_file.dcc_vhost);
	sz_conf += count_memory_string(config_file.admin1);
	sz_conf += count_memory_string(config_file.admin2);
	sz_conf += count_memory_string(config_file.admin3);
	sz_conf += count_memory_string(config_file.db_host);
	sz_conf += count_memory_string(config_file.db_name);
	sz_conf += count_memory_string(config_file.db_username);
	sz_conf += count_memory_string(config_file.db_password);
	sz_conf += count_memory_string(config_file.email_name);
	sz_conf += count_memory_string(config_file.email_address);
	sz_conf += count_memory_string(config_file.uregister_url);
	sz_conf += count_memory_string(config_file.nwarn_string);

	DLINK_FOREACH(ptr, conf_server_list.head)
	{
		sconf = ptr->data;

		sz_conf += count_memory_string(sconf->name);
		sz_conf += count_memory_string(sconf->host);
		sz_conf += count_memory_string(sconf->pass);
		sz_conf += count_memory_string(sconf->vhost);
	}

	sz_conf += dlink_list_length(&conf_server_list) * sizeof(struct conf_server);

	DLINK_FOREACH(ptr, conf_oper_list.head)
	{
		oconf = ptr->data;

		sz_conf += count_memory_string(oconf->name);
		sz_conf += count_memory_string(oconf->username);
		sz_conf += count_memory_string(oconf->host);
		sz_conf += count_memory_string(oconf->pass);
		sz_conf += count_memory_string(oconf->server);
	}

	sz_conf += dlink_list_length(&conf_oper_list) * sizeof(struct conf_oper);

	sendto_server(":%s 988 %s :Config File: %u", 
			MYNAME, client_p->name, sz_conf);
}

