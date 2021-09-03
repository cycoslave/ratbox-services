/* src/c_error.c
 *   Contains code for handling "ERROR" command
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
 * $Id: c_error.c 23383 2007-01-07 20:21:49Z leeh $
 */
#include "stdinc.h"
#include "c_init.h"
#include "rserv.h"
#include "io.h"
#include "scommand.h"
#include "client.h"
#include "log.h"

static void c_error(struct client *, const char *parv[], int parc);
struct scommand_handler error_command = { "ERROR", c_error, FLAGS_UNKNOWN, DLINK_EMPTY };

static void
c_error(struct client *client_p, const char *parv[], int parc)
{
        if(parc < 1 || EmptyString(parv[0]))
                return;

        mlog("Connection to server %s error: (%s)",
             server_p->name, parv[0]);
        sendto_all("Connection to server %s error: (%s)",
                   server_p->name, parv[0]);

        (server_p->io_close)(server_p);
}
