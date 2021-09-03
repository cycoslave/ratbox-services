/* src/modebuild.c
 *   Contains functions to allow services to build a mode buffer.
 *
 * Copyright (C) 2004-2007 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2004-2007 ircd-ratbox development team
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
 * $Id: modebuild.c 23902 2007-05-01 21:57:39Z jilles $
 */
#include "stdinc.h"

#include "rserv.h"
#include "client.h"
#include "channel.h"
#include "io.h"
#include "modebuild.h"

static dlink_list kickbuild_list;
static dlink_list kickbuildr_list;

static char modebuf[BUFSIZE];
static char parabuf[BUFSIZE];
static int modedir;
static int modecount;
static int mlen;
static int plen;
static int cur_len;

static void
modebuild_init(void)
{
	cur_len = mlen;
	plen = 0;
	parabuf[0] = '\0';
	modedir = DIR_NONE;
	modecount = 0;
}

void
modebuild_start(struct client *source_p, struct channel *chptr)
{
	if (server_p && !EmptyString(server_p->sid))
		mlen = snprintf(modebuf, sizeof(modebuf), ":%s TMODE %lu %s ",
				source_p->uid, (unsigned long) chptr->tsinfo,
				chptr->name);
	else
		mlen = snprintf(modebuf, sizeof(modebuf), ":%s MODE %s ",
				source_p->name, chptr->name);

	modebuild_init();
}

void
modebuild_add(int dir, const char *mode, const char *arg)
{
	int len;

	len = arg != NULL ? strlen(arg) : 0;

	if((cur_len + plen + len + 4) > (BUFSIZE - 3) ||
	   modecount >= MAX_MODES)
	{
		sendto_server("%s %s", modebuf, parabuf);

		modebuf[mlen] = '\0';
		modebuild_init();
	}

	if(modedir != dir)
	{
		if(dir == DIR_ADD)
			strcat(modebuf, "+");
		else
			strcat(modebuf, "-");
		cur_len++;
	}

	strcat(modebuf, mode);
	if (arg != NULL)
	{
		strcat(parabuf, arg);
		strcat(parabuf, " ");
		modecount++;
		plen += (len + 1);
	}

	cur_len++;
}

void
modebuild_finish(void)
{
	if(cur_len != mlen)
		sendto_server("%s %s", modebuf, parabuf);
}

struct kickbuilder
{
	const char *name;
	const char *reason;
};

void
kickbuild_start(void)
{
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, kickbuild_list.head)
	{
		dlink_destroy(ptr, &kickbuild_list);
	}

	DLINK_FOREACH_SAFE(ptr, next_ptr, kickbuildr_list.head)
	{
		dlink_destroy(ptr, &kickbuildr_list);
	}
}

void
kickbuild_add(const char *nick, const char *reason)
{
	dlink_add_tail_alloc((void *) nick, &kickbuild_list);
	dlink_add_tail_alloc((void *) reason, &kickbuildr_list);
}

void
kickbuild_finish(struct client *service_p, struct channel *chptr)
{
	dlink_node *ptr, *next_ptr;
	dlink_node *rptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, kickbuild_list.head)
	{
		rptr = kickbuildr_list.head;

		sendto_server(":%s KICK %s %s :%s",
				SVC_UID(service_p), chptr->name,
				(const char *) ptr->data, 
				(const char *) rptr->data);
		dlink_destroy(rptr, &kickbuildr_list);
		dlink_destroy(ptr, &kickbuild_list);
	}
}
