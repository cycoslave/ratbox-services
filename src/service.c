/* src/service.c
 *   Contains code for handling interaction with our services.
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
 * $Id: service.c 26911 2010-02-22 19:36:09Z leeh $
 */
#include "stdinc.h"

#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

#include "rsdb.h"
#include "rserv.h"
#include "langs.h"
#include "service.h"
#include "client.h"
#include "scommand.h"
#include "conf.h"
#include "io.h"
#include "log.h"
#include "ucommand.h"
#include "cache.h"
#include "channel.h"
#include "s_userserv.h"
#include "watch.h"
#include "balloc.h"

dlink_list service_list;
dlink_list ignore_list;

static int ignore_db_callback(int, const char **);

static void unmerge_service(struct client *service_p);

void
init_services(void)
{
	struct client *service_p;
	dlink_node *ptr;
	dlink_node *next_ptr;

	/* init functions may remove themselves from this list */
	DLINK_FOREACH_SAFE(ptr, next_ptr, service_list.head)
	{
		service_p = ptr->data;

		/* generate all our services a UID.. */
		strlcpy(service_p->uid, generate_uid(), sizeof(service_p->uid));

		if(service_p->service->init)
			(service_p->service->init)();
	}

	rsdb_exec(ignore_db_callback, "SELECT hostname, oper, reason FROM ignore_hosts");
}

static int
ignore_db_callback(int argc, const char **argv)
{
	struct service_ignore *ignore_p;

	if(EmptyString(argv[0]) || EmptyString(argv[1]) || EmptyString(argv[2]))
		return 0;

	ignore_p = my_malloc(sizeof(struct service_ignore));
	ignore_p->mask = my_strdup(argv[0]);
	ignore_p->oper = my_strdup(argv[1]);
	ignore_p->reason = my_strdup(argv[2]);

	dlink_add(ignore_p, &ignore_p->ptr, &ignore_list);
	return 0;
}

static int
find_ignore(struct client *client_p)
{
	struct service_ignore *ignore_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, ignore_list.head)
	{
		ignore_p = ptr->data;

		if(match(ignore_p->mask, client_p->user->mask))
			return 1;
	}

	return 0;
}

typedef int (*bqcmp)(const void *, const void *);
static int
scmd_sort(struct service_command *one, struct service_command *two)
{
	return strcasecmp(one->cmd, two->cmd);
}

static int
scmd_compare(const char *name, struct service_command *cmd)
{
	return strcasecmp(name, cmd->cmd);
}

static void
load_service_help(struct client *service_p)
{
	char filename[PATH_MAX];
	unsigned int i;

        if(service_p->service->command == NULL)
		return;

	service_p->service->help = my_malloc(sizeof(struct cachefile *) * LANG_MAX);
	service_p->service->helpadmin = my_malloc(sizeof(struct cachefile *) * LANG_MAX);

	for(i = 0; langs_available[i]; i++)
	{
		snprintf(filename, sizeof(filename), "%s/%s/%s/index",
			HELP_PATH, langs_available[i], lcase(service_p->service->id));
		service_p->service->help[i] = cache_file(filename, "index", 1);

		strlcat(filename, "-admin", sizeof(filename));
		service_p->service->helpadmin[i] = cache_file(filename, "index-admin", 1);
	}
}

static void
append_service_help(struct client *service_p, const char *service_id)
{
	struct cachefile *contents_fileptr;
	struct cachefile *fileptr;
	char filename[PATH_MAX];
	unsigned int i;

	if(service_p->service->help == NULL)
		service_p->service->help = my_malloc(sizeof(struct cachefile *) * LANG_MAX);

	if(service_p->service->helpadmin == NULL)
		service_p->service->helpadmin = my_malloc(sizeof(struct cachefile *) * LANG_MAX);

	for(i = 0; langs_available[i]; i++)
	{
		snprintf(filename, sizeof(filename), "%s/%s/%s/index",
			HELP_PATH, langs_available[i], lcase(service_id));
		fileptr = cache_file(filename, "index", 1);

		contents_fileptr = service_p->service->help[i];

		if(contents_fileptr != NULL && fileptr != NULL)
		{
			dlink_move_list_tail(&fileptr->contents, &contents_fileptr->contents);
		}
		else if(fileptr != NULL)
			service_p->service->help[i] = fileptr;

		free_cachefile(fileptr);

		strlcat(filename, "-admin", sizeof(filename));
		fileptr = cache_file(filename, "index-admin", 1);

		contents_fileptr = service_p->service->helpadmin[i];

		if(contents_fileptr != NULL && fileptr != NULL)
		{
			/* add a blank line to the start of this file to
			 * separate them
			 */
			dlink_add_alloc(emptyline, &fileptr->contents);
			dlink_move_list_tail(&fileptr->contents, &contents_fileptr->contents);
		}
		else if(fileptr != NULL)
			service_p->service->helpadmin[i] = fileptr;

		free_cachefile(fileptr);
	}

}

static void
load_service_command_help(struct service_command *scommand, int maxlen,
			struct ucommand_handler *ucommand, const char *service_id)
{
	char filename[PATH_MAX];
	unsigned long i;
	unsigned int j;

	for(i = 0; i < maxlen; i++)
	{
		scommand[i].helpfile = my_malloc(sizeof(struct cachefile *) * LANG_MAX);

		for(j = 0; langs_available[j]; j++)
		{
			snprintf(filename, sizeof(filename), "%s/%s/%s/",
				HELP_PATH, langs_available[j], lcase(service_id));

			/* we cant lcase() twice in one function call */
			strlcat(filename, lcase(scommand[i].cmd),
				sizeof(filename));

			scommand[i].helpfile[j] = cache_file(filename, scommand[i].cmd, 0);
		}

#ifdef ENABLE_USERSERV
		/* unfortunately, userserv help on language requires extra
		 * work to list the available languages.. do that here.
		 */
		if(!strcmp(service_id, "USERSERV") && !strcmp(scommand[i].cmd, "LANGUAGE"))
		{
			int k;

			/* loop the list of langs available to update each
			 * translation file within that
			 */
			for(j = 0; langs_available[j]; j++)
			{
				/* this doesn't have a language help file */
				if(scommand[i].helpfile[j] == NULL)
					continue;

				/* find all translations */
				for(k = 0; langs_available[k]; k++)
				{
					struct cacheline *lineptr;

					if(EmptyString(langs_description[k]))
						continue;

					lineptr = BlockHeapAlloc(cacheline_heap);
					snprintf(lineptr->data, sizeof(lineptr->data),
						"     %-6s - %s",
						langs_available[k], langs_description[k]);
					dlink_add_tail(lineptr, &lineptr->linenode, &(scommand[i].helpfile[j]->contents));
				}
			}
		}
#endif
	}

	for(i = 0; ucommand && ucommand[i].cmd && ucommand[i].cmd[0] != '\0'; i++)
	{
		ucommand[i].helpfile = my_malloc(sizeof(struct cachefile *) * LANG_MAX);

		for(j = 0; langs_available[j]; j++)
		{
		        /* now see if we can load a helpfile.. */
        		snprintf(filename, sizeof(filename), "%s/%s/%s/u-",
                		 HELP_PATH, langs_available[j], lcase(service_id));
		        strlcat(filename, lcase(ucommand->cmd), sizeof(filename));

	        	ucommand[i].helpfile[j] = cache_file(filename, ucommand->cmd, 0);
		}
	}
}

static void
clear_service_help(struct client *service_p)
{
	struct service_command *scommand;
	struct ucommand_handler *ucommand;
	int maxlen = service_p->service->command_size / sizeof(struct service_command);
	int i, j;
	
	if(service_p->service->command == NULL)
		return;

	scommand = service_p->service->command;

	for(j = 0; langs_available[j]; j++)
	{
		free_cachefile(service_p->service->help[j]);
		free_cachefile(service_p->service->helpadmin[j]);
	}

	my_free(service_p->service->help);
	my_free(service_p->service->helpadmin);
	service_p->service->help = NULL;
	service_p->service->helpadmin = NULL;

	for(i = 0; i < maxlen; i++)
	{
		for(j = 0; langs_available[j]; j++)
		{
			free_cachefile(scommand[i].helpfile[j]);
		}

		my_free(scommand[i].helpfile);
		scommand[i].helpfile = NULL;
	}

	for(i = 0; service_p->service->ucommand && service_p->service->ucommand[i].cmd && service_p->service->ucommand[i].cmd[0] != '\0'; i++)
	{
		ucommand = &service_p->service->ucommand[i];

		for(j = 0; langs_available[j]; j++)
		{
			free_cachefile(ucommand->helpfile[j]);
		}

		my_free(ucommand->helpfile);
		ucommand->helpfile = NULL;
	}
}

void
rehash_help(void)
{
	struct service_handler *handler;
	struct client *service_p;
	dlink_node *ptr;
	dlink_node *merge_ptr;

	DLINK_FOREACH(ptr, service_list.head)
	{
		service_p = ptr->data;

		/* dealing with merged services makes this a little complicated.
		 *
		 * We clear the help when it is merged, because that can work on
		 * the merged list of commands without issue.  The merged version
		 * is an identical copy of the original, so they both point to the
		 * same memory -- which we should only free() once.
		 *
		 * Loading the help however, requires we are looking for the directory
		 * given by the services name.  On a merged list however, this will end
		 * up hunting the parent service for all of the helpfiles.  To avoid
		 * this, we "unmerge" the service back to its original state.
		 *
		 * Once that is done, we load the help as normal.  Then for each
		 * service that was previously merged, we go and load the helpfiles
		 * for it, then remerge it back up to the parent service.
		 */
		clear_service_help(service_p);
		unmerge_service(service_p);
		load_service_help(service_p);
		load_service_command_help(service_p->service->command,
					service_p->service->command_size / sizeof(struct service_command),
					service_p->service->ucommand, service_p->service->id);

		DLINK_FOREACH(merge_ptr, service_p->service->merged_handler_list.head)
		{
			handler = merge_ptr->data;

			load_service_command_help(handler->command,
						handler->command_size / sizeof(struct service_command),
						handler->ucommand, handler->id);
			merge_service(handler, service_p->service->id, 0);
		}
	}

	clear_ucommand_help();
	load_ucommand_help();
}

struct client *
add_service(struct service_handler *service)
{
	struct client *client_p;
	size_t maxlen = service->command_size / sizeof(struct service_command);

	if(strchr(service->name, '.') != NULL)
	{
		mlog("ERR: Invalid service name %s", client_p->name);
		return NULL;
	}

	if((client_p = find_client(service->name)) != NULL)
	{
		if(IsService(client_p))
		{
			mlog("ERR: Tried to add duplicate service %s", service->name);
			return NULL;
		}
		else if(IsServer(client_p))
		{
			mlog("ERR: A server exists with service name %s?!", service->name);
			return NULL;
		}
		else if(IsUser(client_p))
		{
			if(client_p->user->tsinfo <= 1)
				die(1, "services conflict");

			/* we're about to collide it. */
			exit_client(client_p);
		}
	}

	/* now we need to sort the command array */
	if(service->command)
		qsort(service->command, maxlen,
			sizeof(struct service_command), (bqcmp) scmd_sort);

	client_p = my_malloc(sizeof(struct client));
	client_p->service = my_malloc(sizeof(struct service));

	strlcpy(client_p->name, service->name, sizeof(client_p->name));
	strlcpy(client_p->service->username, service->username,
		sizeof(client_p->service->username));
	strlcpy(client_p->service->host, service->host,
		sizeof(client_p->service->host));
	strlcpy(client_p->info, service->info, sizeof(client_p->info));
	strlcpy(client_p->service->id, service->id, sizeof(client_p->service->id));
	client_p->service->command = service->command;
	client_p->service->command_size = service->command_size;
        client_p->service->ucommand = service->ucommand;
	client_p->service->init = service->init;
        client_p->service->stats = service->stats;
	client_p->service->loglevel = 1;

        client_p->service->flood_max = service->flood_max;
        client_p->service->flood_grace = service->flood_grace;

	dlink_add_tail(client_p, &client_p->listnode, &service_list);

        if(service->ucommand != NULL)
                add_ucommands(client_p, service->ucommand);

	open_service_logfile(client_p);
	load_service_help(client_p);
	load_service_command_help(service->command,
				service->command_size / sizeof(struct service_command),
				service->ucommand, service->id);

	return client_p;
}

struct client *
find_service_id(const char *name)
{
	struct client *client_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, service_list.head)
	{
		client_p = ptr->data;

		if(!strcasecmp(client_p->service->id, name))
			return client_p;
	}

	return NULL;
}

struct client *
merge_service(struct service_handler *handler_p, const char *service_id, int startup)
{
	struct client *service_p;
	struct service_command *svc_cmd;
	struct ucommand_handler *svc_ucommand;
	unsigned long merged_command_size;
	unsigned int merged_command_length;
	unsigned int original_command_length;

	if((service_p = find_service_id(service_id)) == NULL)
		return NULL;

	if(startup)
		dlink_add_tail_alloc(handler_p, &service_p->service->merged_handler_list);

	/* play nice, and merge the index/index-admin helpfiles */
	append_service_help(service_p, handler_p->id);

	/* first, we do irc commands */
	merged_command_size = service_p->service->command_size + handler_p->command_size;
	original_command_length = service_p->service->command_size / sizeof(struct service_command);
	merged_command_length = merged_command_size / sizeof(struct service_command);

	svc_cmd = my_malloc(merged_command_size);
	memcpy(svc_cmd, service_p->service->command, service_p->service->command_size);
	memcpy(svc_cmd + original_command_length, handler_p->command, handler_p->command_size);

	/* sort the now bigger array */
	qsort(svc_cmd, merged_command_length,
			sizeof(struct service_command), (bqcmp) scmd_sort);

	/* we have already merged some commands into this, and we have built
	 * the new structure via memcpy(), so we don't need the old copy anymore
	 */
	if(service_p->service->orig_command)
		my_free(service_p->service->command);

	if(service_p->service->orig_command == NULL)
	{
		service_p->service->orig_command = service_p->service->command;
		service_p->service->orig_command_size = service_p->service->command_size;
	}

	service_p->service->command = svc_cmd;
	service_p->service->command_size = merged_command_size;

	/* now do dcc commands */
	if(handler_p->ucommand)
	{
		unsigned long original_command_size;
		unsigned long new_command_size;
		unsigned int new_command_length;
		int i;

		original_command_length = 0;
		new_command_length = 0;

		/* always need space for a NULL entry at the end */
		merged_command_length = 1;

		for(i = 0; service_p->service->ucommand && service_p->service->ucommand[i].cmd && service_p->service->ucommand[i].cmd[0] != '\0'; i++)
		{
			original_command_length++;
			merged_command_length++;
		}

		for(i = 0; handler_p->ucommand[i].cmd && handler_p->ucommand[i].cmd[0] != '\0'; i++)
		{
			new_command_length++;
			merged_command_length++;
		}

		original_command_size = sizeof(struct ucommand_handler) * original_command_length;
		new_command_size = sizeof(struct ucommand_handler) * new_command_length;
		merged_command_size = sizeof(struct ucommand_handler) * merged_command_length;

		svc_ucommand = my_malloc(merged_command_size);

		if(original_command_size)
			memcpy(svc_ucommand, service_p->service->ucommand, original_command_size);

		memcpy(svc_ucommand + original_command_length, handler_p->ucommand, new_command_size);

		if(service_p->service->orig_ucommand)
			my_free(service_p->service->ucommand);

		if(service_p->service->orig_ucommand == NULL)
			service_p->service->orig_ucommand = service_p->service->ucommand;

		service_p->service->ucommand = svc_ucommand;
	}

	return service_p;
}

static void
unmerge_service(struct client *service_p)
{
	if(service_p->service->orig_command == NULL)
		return;

	/* allocated in ram */
	my_free(service_p->service->command);
	service_p->service->command = service_p->service->orig_command;
	service_p->service->command_size = service_p->service->orig_command_size;

	/* may not have merged any dcc commands */
	if(service_p->service->orig_ucommand)
	{
		my_free(service_p->service->ucommand);
		service_p->service->ucommand = service_p->service->orig_ucommand;
	}

	/* mark this service as unmerged */
	service_p->service->orig_command = NULL;
	service_p->service->orig_command_size = 0;
	service_p->service->orig_ucommand = NULL;
}

void
introduce_service(struct client *target_p)
{
	if(ConnTS6(server_p))
		sendto_server("UID %s 1 1 +iDS%s %s %s 0 %s :%s",
				target_p->name, ServiceOpered(target_p) ? "o" : "",
				target_p->service->username,
				target_p->service->host, target_p->uid,
				target_p->info);
	else
		sendto_server("NICK %s 1 1 +iDS%s %s %s %s :%s",
				target_p->name, ServiceOpered(target_p) ? "o" : "",
				target_p->service->username,
				target_p->service->host, MYNAME, target_p->info);

	SetServiceIntroduced(target_p);
	add_client(target_p);
}

void
introduce_service_channels(struct client *target_p, int send_tb)
{
	struct channel *chptr;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, target_p->service->channels.head)
	{
		chptr = ptr->data;

		sendto_server(":%s SJOIN %lu %s %s :@%s",
				MYUID, (unsigned long) chptr->tsinfo, 
				chptr->name, chmode_to_string(&chptr->mode), 
				SVC_UID(target_p));

		if(send_tb && ConnCapTB(server_p) && !EmptyString(chptr->topic))
			sendto_server(":%s TB %s %lu %s :%s",
					MYUID, chptr->name,
					(unsigned long) chptr->topic_tsinfo,
					chptr->topicwho, chptr->topic);

	}
}

void
introduce_services()
{
	struct client *service_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, service_list.head)
	{
		service_p = ptr->data;

		if(ServiceDisabled(service_p))
			continue;

		introduce_service(ptr->data);
	}
}

void
introduce_services_channels()
{
	struct client *service_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, service_list.head)
	{
		service_p = ptr->data;

		if(!ServiceDisabled(service_p))
			introduce_service_channels(ptr->data, 1);
	}
}

void
reintroduce_service(struct client *target_p)
{
	sendto_server(":%s QUIT :Updating information", SVC_UID(target_p));
	del_client(target_p);
	introduce_service(target_p);
	introduce_service_channels(target_p, 0);

	ClearServiceReintroduce(target_p);
}

void
deintroduce_service(struct client *target_p)
{
	sendto_server(":%s QUIT :Disabled", SVC_UID(target_p));
	ClearServiceIntroduced(target_p);
	del_client(target_p);
}

void
update_service_floodcount(void *unused)
{
	struct client *client_p;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, service_list.head)
	{
		client_p = ptr->data;

		client_p->service->flood -= client_p->service->flood_grace;

		if(client_p->service->flood < 0)
			client_p->service->flood = 0;
	}
}

static void
handle_service_help_index(struct client *service_p, struct client *client_p)
{
	struct cachefile *fileptr;
	struct cacheline *lineptr;
	dlink_node *ptr;
	int i;

	/* if this service has short help enabled, or there is no index 
	 * file and theyre either unopered (so cant see admin file), 
	 * or theres no admin file.
	 */
	if(ServiceShortHelp(service_p) ||
	   ((!service_p->service->help || lang_get_cachefile(service_p->service->help, client_p) == NULL) &&
	    (!client_p->user->oper || 
	     (!service_p->service->helpadmin || lang_get_cachefile(service_p->service->helpadmin, client_p) == NULL))))
	{
		char buf[BUFSIZE];
		struct service_command *cmd_table;

		buf[0] = '\0';
		cmd_table = service_p->service->command;

		SCMD_WALK(i, service_p)
		{
			if((cmd_table[i].operonly && !is_oper(client_p)) ||
			   (cmd_table[i].operflags && 
			    (!client_p->user->oper || 
			     (client_p->user->oper->flags & cmd_table[i].operflags) == 0)))
				continue;

			strlcat(buf, cmd_table[i].cmd, sizeof(buf));
			strlcat(buf, " ", sizeof(buf));
		}
		SCMD_END;

		if(buf[0] != '\0')
		{
			service_err(service_p, client_p, SVC_HELP_INDEXINFO, service_p->name);
			service_err(service_p, client_p, SVC_HELP_TOPICS, buf);
		}
		else
			service_err(service_p, client_p, SVC_HELP_UNAVAILABLE);

		service_p->service->help_count++;
		return;
	}

	service_p->service->flood++;
	fileptr = lang_get_cachefile(service_p->service->help, client_p);

	if(fileptr)
	{
		/* dump them the index file */
		/* this contains a short introduction and a list of commands */

		DLINK_FOREACH(ptr, fileptr->contents.head)
		{
			lineptr = ptr->data;
			service_error(service_p, client_p, "%s",
					lineptr->data);
		}
	}

	fileptr = lang_get_cachefile(service_p->service->helpadmin, client_p);

	if(client_p->user->oper && fileptr)
	{
		service_err(service_p, client_p, SVC_HELP_INDEXADMIN);

		DLINK_FOREACH(ptr, fileptr->contents.head)
		{
			lineptr = ptr->data;
			service_error(service_p, client_p, "%s",
					lineptr->data);
		}
	}
}

static void
handle_service_help(struct client *service_p, struct client *client_p, const char *arg)
{
	struct service_command *cmd_entry;

	if((cmd_entry = bsearch(arg, service_p->service->command,
				service_p->service->command_size / sizeof(struct service_command),
				sizeof(struct service_command), (bqcmp) scmd_compare)))
	{
		struct cachefile *fileptr;
		struct cacheline *lineptr;
		dlink_node *ptr;

		if(cmd_entry->helpfile == NULL || lang_get_cachefile(cmd_entry->helpfile, client_p) == NULL ||
		   (cmd_entry->operonly && !is_oper(client_p)))
		{
			service_err(service_p, client_p, SVC_HELP_UNAVAILABLETOPIC, arg);
			return;
		}

		fileptr = lang_get_cachefile(cmd_entry->helpfile, client_p);

		DLINK_FOREACH(ptr, fileptr->contents.head)
		{
			lineptr = ptr->data;
			service_error(service_p, client_p, "%s", lineptr->data);
		}

		service_p->service->flood += cmd_entry->help_penalty;
		service_p->service->ehelp_count++;
	}
	else
		service_err(service_p, client_p, SVC_HELP_UNAVAILABLETOPIC, arg);
}

void
handle_service_msg(struct client *service_p, struct client *client_p, char *text)
{
        char *parv[MAXPARA+1];
        char *p;
        int parc = 0;

        if(!IsUser(client_p))
                return;

        if((p = strchr(text, ' ')) != NULL)
	{
                *p++ = '\0';
	        parc = string_to_array(p, parv);
	}

	handle_service(service_p, client_p, text, parc, (const char **) parv, 1);
}

void
handle_service(struct client *service_p, struct client *client_p, 
		const char *command, int parc, const char *parv[], int msg)
{
	struct service_command *cmd_entry;
        int retval;

        /* this service doesnt handle commands via privmsg */
        if(service_p->service->command == NULL)
                return;

	/* do flood limiting */
	if(!client_p->user->oper)
	{
		/* we allow opers to traverse ignores (above), together with
		 * any oper who is about to login
		 */
		if(find_ignore(client_p) && 
			(strcasecmp(command, "OLOGIN") ||
			 find_conf_oper(client_p->user->username, client_p->user->host, client_p->user->servername, NULL) == NULL))
			return;

		if((client_p->user->flood_time + config_file.client_flood_time) < CURRENT_TIME)
		{
			client_p->user->flood_time = CURRENT_TIME;
			client_p->user->flood_count = 0;
		}

		if(client_p->user->flood_count > config_file.client_flood_max_ignore)
		{
			client_p->user->flood_count++;
			service_p->service->ignored_count++;
			return;
		}

		if((service_p->service->flood_max && service_p->service->flood > service_p->service->flood_max) ||
		   client_p->user->flood_count > config_file.client_flood_max)
		{
			service_err(service_p, client_p, SVC_RATELIMITEDGENERIC);
			client_p->user->flood_count++;
			service_p->service->paced_count++;
			return;
		}
	}

	if(msg && ServiceShortcut(service_p))
	{
		if(ServiceStealth(service_p) && !client_p->user->oper && !is_oper(client_p))
			return;

		client_p->user->flood_count += 1;
                service_p->service->flood += 1;

		service_err(service_p, client_p, SVC_USECOMMANDSHORTCUT, service_p->name);
		return;
	}

        if(!strcasecmp(command, "HELP"))
        {
		if(ServiceStealth(service_p) && !client_p->user->oper && !is_oper(client_p))
			return;

#ifdef ENABLE_USERSERV
		if(ServiceLoginHelp(service_p) && !client_p->user->user_reg &&
		   !client_p->user->oper && !is_oper(client_p))
		{
			service_err(service_p, client_p, SVC_NOTLOGGEDIN,
					service_p->name, "HELP");
			return;
		}
#endif

		client_p->user->flood_count += 2;
                service_p->service->flood += 2;

                if(parc < 1 || EmptyString(parv[0]))
			handle_service_help_index(service_p, client_p);
		else
			handle_service_help(service_p, client_p, parv[0]);

		return;
        }
	else if(!strcasecmp(command, "OPERLOGIN") || !strcasecmp(command, "OLOGIN"))
	{
		struct conf_oper *oper_p;
		const char *crpass;

		if(client_p->user->oper)
		{
			sendto_server(":%s NOTICE %s :You are already logged in as an oper",
					MYUID, UID(client_p));
			return;
		}

		if(parc < 2)
		{
			sendto_server(":%s NOTICE %s :Insufficient parameters to %s::OLOGIN",
					MYUID, UID(client_p), service_p->name);
			client_p->user->flood_count++;
			return;
		}

		if((oper_p = find_conf_oper(client_p->user->username, client_p->user->host,
						client_p->user->servername, parv[0])) == NULL)
		{
			sendto_server(":%s NOTICE %s :No access to %s::OLOGIN",
					MYUID, UID(client_p), ucase(service_p->name));
			client_p->user->flood_count++;
			return;
		}

		if(ConfOperEncrypted(oper_p))
			crpass = crypt(parv[1], oper_p->pass);
		else
			crpass = parv[1];

		if(strcmp(crpass, oper_p->pass))
		{
			sendto_server(":%s NOTICE %s :Invalid password",
					MYUID, UID(client_p));
			return;
		}

		sendto_server(":%s NOTICE %s :Oper login successful",
				MYUID, UID(client_p));

		client_p->user->oper = oper_p;
		oper_p->refcount++;
		dlink_add_alloc(client_p, &oper_list);

		watch_send(WATCH_AUTH, client_p, NULL, 1, "has logged in (irc)");

		return;
	}
	else if(!strcasecmp(command, "OPERLOGOUT") || !strcasecmp(command, "OLOGOUT"))
	{
		if(client_p->user->oper == NULL)
		{
			sendto_server(":%s NOTICE %s :You are not logged in as an oper",
					MYUID, UID(client_p));
			client_p->user->flood_count++;
			return;
		}

		watch_send(WATCH_AUTH, client_p, NULL, 1, "has logged out (irc)");

		deallocate_conf_oper(client_p->user->oper);
		client_p->user->oper = NULL;
		dlink_find_destroy(client_p, &oper_list);

		sendto_server(":%s NOTICE %s :Oper logout successful",
				MYUID, UID(client_p));
		return;
	}

	if(ServiceStealth(service_p) && !client_p->user->oper && !is_oper(client_p))
		return;

	if((cmd_entry = bsearch(command, service_p->service->command, 
				service_p->service->command_size / sizeof(struct service_command),
				sizeof(struct service_command), (bqcmp) scmd_compare)))
	{
		if((cmd_entry->operonly && !is_oper(client_p)) ||
		   (cmd_entry->operflags && 
		    (!client_p->user->oper || 
		     (client_p->user->oper->sflags & cmd_entry->operflags) == 0)))
		{
			service_err(service_p, client_p, SVC_NOACCESS,
					service_p->name, cmd_entry->cmd);
			client_p->user->flood_count++;
			service_p->service->flood++;
			return;
		}

#ifdef ENABLE_USERSERV
		if(cmd_entry->userreg)
		{
			if(client_p->user->user_reg == NULL)
			{
				service_err(service_p, client_p, SVC_NOTLOGGEDIN,
						service_p->name, cmd_entry->cmd);
				client_p->user->flood_count++;
				service_p->service->flood++;
				return;
			}
			else
			{
				client_p->user->user_reg->last_time = CURRENT_TIME;
				client_p->user->user_reg->flags |= US_FLAGS_NEEDUPDATE;
			}
		}
#endif

		if(parc < cmd_entry->minparc)
		{
			service_err(service_p, client_p, SVC_NEEDMOREPARAMS,
					service_p->name, cmd_entry->cmd);
			client_p->user->flood_count++;
			service_p->service->flood++;
			return;
		}

		cmd_entry->cmd_use++;

		if(cmd_entry->func)
			retval = (cmd_entry->func)(client_p, NULL, (const char **) parv, parc);
		else
			retval = 0;

		/* NOTE, at this point cmd_entry may now be invalid.
		 * Particularly if we have just done a rehash help
		 */
		cmd_entry = NULL;

		client_p->user->flood_count += retval;
		service_p->service->flood += retval;
		return;
        }

        service_err(service_p, client_p, SVC_UNKNOWNCOMMAND,
			service_p->name, command);
        service_p->service->flood++;
	client_p->user->flood_count++;
}

void
service_send(struct client *service_p, struct client *client_p,
		struct lconn *conn_p, const char *format, ...)
{
	static char buf[BUFSIZE];
	va_list args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(client_p)
		sendto_server(":%s NOTICE %s :%s",
				ServiceMsgSelf(service_p) ? SVC_UID(service_p) : MYUID, 
				UID(client_p), buf);
	else
		sendto_one(conn_p, "%s", buf);
}

void
service_snd(struct client *service_p, struct client *client_p,
		struct lconn *conn_p, int msgid, ...)
{
	static char buf[BUFSIZE];
	va_list args;

	va_start(args, msgid);
	vsnprintf(buf, sizeof(buf), lang_get_notice(msgid, client_p, conn_p), args);
	va_end(args);

	if(client_p)
		sendto_server(":%s NOTICE %s :%s",
				ServiceMsgSelf(service_p) ? SVC_UID(service_p) : MYUID, 
				UID(client_p), buf);
	else
		sendto_one(conn_p, "%s", buf);
}

void
service_error(struct client *service_p, struct client *client_p,
		const char *format, ...)
{
	static char buf[BUFSIZE];
	va_list args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	sendto_server(":%s NOTICE %s :%s",
			ServiceMsgSelf(service_p) ? SVC_UID(service_p) : MYUID, 
			UID(client_p), buf);
}

void
service_err(struct client *service_p, struct client *client_p, int msgid, ...)
{
	static char buf[BUFSIZE];
	va_list args;

	va_start(args, msgid);
	vsnprintf(buf, sizeof(buf), lang_get_notice(msgid, client_p, NULL), args);
	va_end(args);

	sendto_server(":%s NOTICE %s :%s",
			ServiceMsgSelf(service_p) ? SVC_UID(service_p) : MYUID, 
			UID(client_p), buf);
}

void
service_stats(struct client *service_p, struct lconn *conn_p)
{
        struct service_command *cmd_table;
        char buf[BUFSIZE];
        char buf2[40];
        int i;
        int j = 0;

        sendto_one(conn_p, "%s Service:", service_p->service->id);

	if(ServiceDisabled(service_p))
	{
		sendto_one(conn_p, " Disabled");
		return;
	}

        sendto_one(conn_p, " Online as %s!%s@%s [%s]",
                   service_p->name, service_p->service->username,
                   service_p->service->host, service_p->info);

        if(service_p->service->command == NULL)
                return;

        sendto_one(conn_p, " Current load: %d/%d Paced: %lu [%lu]",
                   service_p->service->flood, service_p->service->flood_max,
                   service_p->service->paced_count,
                   service_p->service->ignored_count);

        sendto_one(conn_p, " Help usage: %lu Extended: %lu",
                   service_p->service->help_count,
                   service_p->service->ehelp_count);

        cmd_table = service_p->service->command;

        sprintf(buf, " Command usage: ");

	SCMD_WALK(i, service_p)
        {
                snprintf(buf2, sizeof(buf2), "%s:%lu ",
                         cmd_table[i].cmd, cmd_table[i].cmd_use);
                strlcat(buf, buf2, sizeof(buf));

                j++;

                if(j > 6)
                {
                        sendto_one(conn_p, "%s", buf);
                        sprintf(buf, "                ");
                        j = 0;
                }
        }
	SCMD_END;

        if(j)
                sendto_one(conn_p, "%s", buf);
}
