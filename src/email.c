/* src/email.c
 *   Contains code for generating emails.
 *
 * Copyright (C) 2006-2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2006-2007 ircd-ratbox development team
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
 * $Id: cache.c 20234 2005-04-07 13:12:33Z leeh $
 */
#include "stdinc.h"
#include "rserv.h"
#include "conf.h"
#include "log.h"
#include "email.h"

static time_t email_last;
static int email_count;

int
can_send_email(void)
{
	if(email_last + config_file.email_duration < CURRENT_TIME)
	{
		email_last = CURRENT_TIME;
		email_count = 0;

		return 1;
	}

	if(email_count < config_file.email_number)
		return 1;

	return 0;
}

int
send_email(const char *address, const char *subject, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	FILE *out;
	va_list args;
	pid_t childpid;
	int pfd[2];

	/* master override is enabled.. cant send emails */
	if(config_file.disable_email)
		return 0;

	if(!can_send_email())
		return 0;

	email_count++;

	if(EmptyString(config_file.email_program[0]))
	{
		mlog("warning: unable to send email, email program is not set");
		return 0;
	}

	if(pipe(pfd) == -1)
	{
		mlog("warning: unable to send email, cannot pipe(): %s",
			strerror(errno));
		return 0;
	}

	if((out = fdopen(pfd[1], "w")) == NULL)
	{
		close(pfd[0]);
		close(pfd[1]);
		mlog("warning: unable to send email, cannot fdopen(): %s",
			strerror(errno));
		return 0;
	}

	childpid = fork();

	switch(childpid)
	{
		case -1:
			close(pfd[0]);
			close(pfd[1]);
			mlog("warning: unable to send email, cannot fork(): %s",
				strerror(errno));
			return 0;

		/* child process, become the process to deal with sending the email */
		case 0:
			close(pfd[1]);
			dup2(pfd[0], 0);
			if(execv(config_file.email_program[0], config_file.email_program) < 0)
			{
				mlog("warning: unable to send email, cannot execute email program: %s",
					strerror(errno));
				exit(1);
			}

			exit(0);

		/* parent process.. wait for the child to exit */
		default:
			break;
	}

	close(pfd[0]);
	snprintf(buf, sizeof(buf),
		"From: %s <%s>\n"
		"To: %s\n"
		"Subject: %s\n\n",
		EmptyString(config_file.email_name) ? "" : config_file.email_name, 
		config_file.email_address,
		address, subject);

	fputs(buf, out);

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	fputs(buf, out);

	fclose(out);

	waitpid(childpid, NULL, WNOHANG);
	return 1;
}
