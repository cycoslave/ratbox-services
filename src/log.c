/* src/log.c
 *   Contains code for writing to logfile
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
 * $Id: log.c 23427 2007-01-12 20:48:17Z leeh $
 */
#include "stdinc.h"
#include "rserv.h"
#include "langs.h"
#include "log.h"
#include "io.h"
#include "client.h"
#include "service.h"
#include "conf.h"
#include "watch.h"
#include "s_userserv.h"

static FILE *logfile;

void
open_logfile(void)
{
	logfile = fopen(LOG_PATH, "a");
}

void
open_service_logfile(struct client *service_p)
{
	char buf[PATH_MAX];

	snprintf(buf, sizeof(buf), "%s/%s.log", LOGDIR, lcase(service_p->service->id));

	service_p->service->logfile = fopen(buf, "a");
}

void
reopen_logfiles(void)
{
	struct client *service_p;
	dlink_node *ptr;

	if(logfile != NULL)
		fclose(logfile);

	open_logfile();

	DLINK_FOREACH(ptr, service_list.head)
	{
		service_p = ptr->data;

		if(service_p->service->logfile != NULL)
			fclose(service_p->service->logfile);

		open_service_logfile(service_p);
	}
}

static const char *
smalldate(void)
{
	static char buf[MAX_DATE_STRING];
	struct tm *lt;
	time_t ltime = CURRENT_TIME;

	lt = localtime(&ltime);

	snprintf(buf, sizeof(buf), "%d/%d/%d %02d.%02d",
		 lt->tm_year + 1900, lt->tm_mon + 1,
		 lt->tm_mday, lt->tm_hour, lt->tm_min);

	return buf;
}

void
mlog(const char *format, ...)
{
	char buf[BUFSIZE];
	char buf2[BUFSIZE];
	va_list args;

	if(logfile == NULL)
		return;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	snprintf(buf2, sizeof(buf2), "%s %s\n", smalldate(), buf);
	fputs(buf2, logfile);
	fflush(logfile);
}

void
zlog(struct client *service_p, int loglevel, unsigned int watchlevel, int oper,
	struct client *client_p, struct lconn *conn_p,
	const char *format, ...)
{
	char buf[BUFSIZE];
	char buf2[BUFSIZE];
	va_list args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(loglevel == 1 && ServiceWallopAdm(service_p))
		sendto_server(":%s WALLOPS :%s: %s %s %s",
				MYUID, service_p->name, OPER_NAME(client_p, conn_p),
				OPER_MASK(client_p, conn_p), buf);

	if(watchlevel)
		watch_send(watchlevel, client_p, conn_p, oper, "%s", buf);

	if(service_p->service->logfile == NULL)
		return;

	if(service_p->service->loglevel < loglevel)
		return;

	if(oper)
		snprintf(buf2, sizeof(buf2), "%s *%s %s %s\n", 
			smalldate(), OPER_NAME(client_p, conn_p), 
			OPER_MASK(client_p, conn_p), buf);
	else
		snprintf(buf2, sizeof(buf2), "%s %s %s %s\n",
			smalldate(),
			client_p->user->user_reg ? client_p->user->user_reg->name : "-",
			OPER_MASK(client_p, conn_p), buf);

	fputs(buf2, service_p->service->logfile);
	fflush(service_p->service->logfile);
}

