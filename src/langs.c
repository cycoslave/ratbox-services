/* src/langs.c
 *   Contains code for dealing with translations
 *
 * Copyright (C) 2007-2008 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2007-2008 ircd-ratbox development team
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
 * $Id: langs.c 26692 2009-10-17 20:13:29Z leeh $
 */
#include "stdinc.h"

#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif

#include "rserv.h"
#include "langs.h"
#include "cache.h"
#include "client.h"
#include "io.h"
#include "conf.h"
#include "log.h"
#ifdef ENABLE_USERSERV
#include "s_userserv.h"
#endif

const char *langs_available[LANG_MAX];
char *langs_description[LANG_MAX];
const char **svc_notice[LANG_MAX];

const char *svc_notice_string[] =
{
	/* general service */
	"SVC_UNKNOWNCOMMAND",
	"SVC_SUCCESSFUL",
	"SVC_SUCCESSFULON",
	"SVC_ISSUED",
	"SVC_NEEDMOREPARAMS",
	"SVC_ISDISABLED",
	"SVC_ISDISABLEDEMAIL",
	"SVC_NOTSUPPORTED",
	"SVC_NOACCESS",
	"SVC_OPTIONINVALID",
	"SVC_RATELIMITEDGENERIC",
	"SVC_RATELIMITED",
	"SVC_RATELIMITEDHOST",
	"SVC_NOTLOGGEDIN",
	"SVC_ENDOFLIST",
	"SVC_ENDOFLISTLIMIT",
	"SVC_USECOMMANDSHORTCUT",
	"SVC_INVALIDMASK",

	/* general irc related */
	"SVC_IRC_NOSUCHCHANNEL",
	"SVC_IRC_CHANNELINVALID",
	"SVC_IRC_CHANNELNOUSERS",
	"SVC_IRC_NOSUCHSERVER",
	"SVC_IRC_SERVERNAMEINVALID",
	"SVC_IRC_ALREADYONCHANNEL",
	"SVC_IRC_YOUALREADYONCHANNEL",
	"SVC_IRC_NOTINCHANNEL",
	"SVC_IRC_YOUNOTINCHANNEL",
	"SVC_IRC_NOTOPPEDONCHANNEL",

	/* email */
	"SVC_EMAIL_INVALID",
	"SVC_EMAIL_INVALIDIGNORED",
	"SVC_EMAIL_BANNEDDOMAIN",
	"SVC_EMAIL_TEMPUNAVAILABLE",
	"SVC_EMAIL_SENDFAILED",

	/* service help */
	"SVC_HELP_INDEXINFO",
	"SVC_HELP_TOPICS",
	"SVC_HELP_UNAVAILABLE",
	"SVC_HELP_UNAVAILABLETOPIC",
	"SVC_HELP_INDEXADMIN",

	/* userserv */
	"SVC_USER_USERLOGGEDIN",
	"SVC_USER_REGISTERDISABLED",
	"SVC_USER_ALREADYREG",
	"SVC_USER_NOTREG",
	"SVC_USER_NOWREG",
	"SVC_USER_NOWREGLOGGEDIN",
	"SVC_USER_NOWREGEMAILED",
	"SVC_USER_REGDROPPED",
	"SVC_USER_INVALIDUSERNAME",
	"SVC_USER_INVALIDPASSWORD",
	"SVC_USER_INVALIDLANGUAGE",
	"SVC_USER_LONGPASSWORD",
	"SVC_USER_LOGINSUSPENDED",
	"SVC_USER_LOGINUNACTIVATED",
	"SVC_USER_LOGINMAX",
	"SVC_USER_ALREADYLOGGEDIN",
	"SVC_USER_NICKNOTLOGGEDIN",
	"SVC_USER_SUSPENDED",
	"SVC_USER_NOEMAIL",
	"SVC_USER_CHANGEDPASSWORD",
	"SVC_USER_CHANGEDOPTION",
	"SVC_USER_QUERYOPTION",
	"SVC_USER_QUERYOPTIONALREADY",
	"SVC_USER_REQUESTISSUED",
	"SVC_USER_REQUESTPENDING",
	"SVC_USER_REQUESTNONE",
	"SVC_USER_TOKENBAD",
	"SVC_USER_TOKENMISMATCH",
	"SVC_USER_DURATIONTOOSHORT",
	"SVC_USER_NOACCESSON",
	/* userserv::activate */
	"SVC_USER_ACT_ALREADY",
	"SVC_USER_ACT_COMPLETE",
	/* userserv::resetpass */
	"SVC_USER_RP_LOGGEDIN",
	/* userserv::userlist */
	"SVC_USER_UL_START",

	/* userserv::info */
	/* chanserv::info */
	/* nickserv::info */
	"SVC_INFO_REGDURATIONUSER",
	"SVC_INFO_REGDURATIONCHAN",
	"SVC_INFO_REGDURATIONNICK",
	"SVC_INFO_SUSPENDED",
	"SVC_INFO_SUSPENDEDADMIN",
	"SVC_INFO_ACCESSLIST",
	"SVC_INFO_NICKNAMES",
	"SVC_INFO_EMAIL",
	"SVC_INFO_URL",
	"SVC_INFO_TOPIC",
	"SVC_INFO_SETTINGS",
	"SVC_INFO_ENFORCEDMODES",
	"SVC_INFO_CURRENTLOGON",

	/* nickserv */
	"SVC_NICK_NOTONLINE",
	"SVC_NICK_ALREADYREG",
	"SVC_NICK_NOTREG",
	"SVC_NICK_NOWREG",
	"SVC_NICK_CANTREGUID",
	"SVC_NICK_USING",
	"SVC_NICK_TOOMANYREG",
	"SVC_NICK_LOGINFIRST",
	"SVC_NICK_REGGEDOTHER",
	"SVC_NICK_CHANGEDOPTION",
	"SVC_NICK_QUERYOPTION",

	/* chanserv */
	"SVC_CHAN_NOWREG",
	"SVC_CHAN_NOTREG",
	"SVC_CHAN_ALREADYREG",
	"SVC_CHAN_CHANGEDOPTION",
	"SVC_CHAN_UNSETOPTION",
	"SVC_CHAN_QUERYOPTION",
	"SVC_CHAN_QUERYOPTIONALREADY",
	"SVC_CHAN_LISTSTART",
	"SVC_CHAN_ISSUSPENDED",
	"SVC_CHAN_NOACCESS",
	"SVC_CHAN_USERNOACCESS",
	"SVC_CHAN_USERALREADYACCESS",
	"SVC_CHAN_USERHIGHERACCESS",
	"SVC_CHAN_INVALIDACCESS",
	"SVC_CHAN_INVALIDAUTOLEVEL",
	"SVC_CHAN_INVALIDSUSPENDLEVEL",
	"SVC_CHAN_USERSETACCESS",
	"SVC_CHAN_USERREMOVED",
	"SVC_CHAN_USERSETAUTOLEVEL",
	"SVC_CHAN_USERSETSUSPEND",
	"SVC_CHAN_USERSUSPENDREMOVED",
	"SVC_CHAN_USERHIGHERSUSPEND",
	"SVC_CHAN_REQUESTPENDING",
	"SVC_CHAN_REQUESTNONE",
	"SVC_CHAN_TOKENMISMATCH",
	"SVC_CHAN_NOMODE",
	"SVC_CHAN_INVALIDMODE",
	"SVC_CHAN_ALREADYOPPED",
	"SVC_CHAN_ALREADYVOICED",
	"SVC_CHAN_YOUNOTBANNED",
	"SVC_CHAN_USEDELOWNER",
	"SVC_CHAN_BANSET",
	"SVC_CHAN_BANREMOVED",
	"SVC_CHAN_ALREADYBANNED",
	"SVC_CHAN_NOTBANNED",
	"SVC_CHAN_BANLISTFULL",
	"SVC_CHAN_INVALIDBAN",
	"SVC_CHAN_BANHIGHERLEVEL",
	"SVC_CHAN_BANHIGHERACCOUNT",
	"SVC_CHAN_BANLISTSTART",

	/* operserv */
	"SVC_OPER_CONNECTIONSSTART",
	"SVC_OPER_CONNECTIONSEND",
	"SVC_OPER_SERVERNAMEMISMATCH",
	"SVC_OPER_OSPARTACCESS",
	"SVC_OPER_IGNORENOTFOUND",
	"SVC_OPER_IGNOREALREADY",
	"SVC_OPER_IGNORELIST",

	/* banserv */
	"SVC_BAN_ISSUED",
	"SVC_BAN_ALREADYPLACED",
	"SVC_BAN_NOTPLACED",
	"SVC_BAN_INVALID",
	"SVC_BAN_LISTSTART",
	"SVC_BAN_NOPERMACCESS",
	"SVC_BAN_REGEXPSUCCESS",
	"SVC_BAN_TOOMANYMATCHES",
	"SVC_BAN_TOOMANYREGEXPMATCHES",

	/* global */
	"SVC_GLOBAL_WELCOMETOOLONG",
	"SVC_GLOBAL_WELCOMEINVALID",
	"SVC_GLOBAL_WELCOMESET",
	"SVC_GLOBAL_WELCOMENOTSET",
	"SVC_GLOBAL_WELCOMEDELETED",
	"SVC_GLOBAL_WELCOMELIST",

	/* jupeserv */
	"SVC_JUPE_ALREADYJUPED",
	"SVC_JUPE_NOTJUPED",
	"SVC_JUPE_ALREADYREQUESTED",
	"SVC_JUPE_PENDINGLIST",

	/* alis */
	"SVC_ALIS_LISTSTART",

	/* memoserv */
	"SVC_MEMO_RECEIVED",
	"SVC_MEMO_SENT",
	"SVC_MEMO_TOOMANYMEMOS",
	"SVC_MEMO_INVALID",
	"SVC_MEMO_DELETED",
	"SVC_MEMO_DELETEDALL",
	"SVC_MEMO_LIST",
	"SVC_MEMO_LISTSTART",
	"SVC_MEMO_READ",
	"SVC_MEMO_UNREAD_COUNT",

	/* must be last */
	"\0"
};

void
init_langs(void)
{
	char pathbuf[PATH_MAX];
	DIR *helpdir;
	struct dirent *subdir;
	struct stat subdirinfo;
	int i;

	/* sanity check! that the table of codes against strings for
	 * services notices are actually the same size
	 */
	if((unsigned int) SVC_LAST != ((sizeof(svc_notice_string) / sizeof(const char *)) - 1))
	{
		die(1, "fatal error: svc_notice_string != svc_notice_enum");
	}

	/* ensure the default language is always at position 0 */
	memset(langs_available, 0, sizeof(const char *) * LANG_MAX);
	(void) lang_get_langcode(LANG_DEFAULT);
	langs_description[0] = my_strdup("English");

	/* reset svc_notice, and load default messages */
	memset(svc_notice, 0, sizeof(char **) * LANG_MAX);
	svc_notice[0] = my_calloc(1, sizeof(char *) * SVC_LAST);

	for(i = 0; lang_internal[i].id != SVC_LAST; i++)
	{
		svc_notice[0][lang_internal[i].id] = lang_internal[i].msg;
	}

	for(i = 0; i < SVC_LAST; i++)
	{
		if(svc_notice[0][i] == NULL)
		{
			die(1, "Unable to find default message for %s", svc_notice_string[i]);
		}
	}

	if((helpdir = opendir(HELPDIR)) == NULL)
	{
		mlog("Warning: Unable to open helpfile directory: %s", HELPDIR);
		return;
	}

	while((subdir = readdir(helpdir)))
	{
		/* skip '.' and '..' */
		if(!strcmp(subdir->d_name, ".") || !strcmp(subdir->d_name, ".."))
			continue;

		snprintf(pathbuf, sizeof(pathbuf), "%s/%s",
				HELPDIR, subdir->d_name);

		if(stat(pathbuf, &subdirinfo) >= 0)
		{
			if(S_ISDIR(subdirinfo.st_mode))
				(void) lang_get_langcode(subdir->d_name);
		}
	}

	(void) closedir(helpdir);

	lang_load_trans();
}

unsigned int
lang_get_langcode(const char *name)
{
	unsigned int i;

	/* first hunt for a match */
	for(i = 0; langs_available[i]; i++)
	{
		if(!strcasecmp(langs_available[i], name))
			return i;
	}

	/* not found, add it in at i */
	if(i+1 >= LANG_MAX)
	{
		mlog("Warning: Reach maximum amount of languages, translations may not be loaded correctly");
		return 0;
	}

	langs_available[i] = my_strdup(name);
	return i;
}

struct cachefile *
lang_get_cachefile(struct cachefile **translations, struct client *client_p)
{
#ifdef ENABLE_USERSERV
	if(client_p != NULL && client_p->user != NULL && client_p->user->user_reg != NULL)
	{
		unsigned int language = client_p->user->user_reg->language;

		if(translations[language] != NULL)
			return translations[language];
	}
#endif

	if(translations[config_file.default_language] != NULL)
		return translations[config_file.default_language];

	/* base translation is always first */
	return translations[0];
}

struct cachefile *
lang_get_cachefile_u(struct cachefile **translations, struct lconn *conn_p)
{
	/* base translation is always first */
	return translations[0];
}

const char *
lang_get_notice(enum svc_notice_enum msgid, struct client *client_p, struct lconn *conn_p)
{
#ifdef ENABLE_USERSERV
	if(client_p != NULL && client_p->user && client_p->user->user_reg != NULL)
	{
		unsigned int language = client_p->user->user_reg->language;

		if(svc_notice[language] && svc_notice[language][msgid])
			return svc_notice[language][msgid];
	}
#endif

	if(svc_notice[config_file.default_language] && svc_notice[config_file.default_language][msgid])
		return svc_notice[config_file.default_language][msgid];

	/* base translation is always first */
	return svc_notice[0][msgid];
}


static void
lang_parse_transfile(FILE *fp, const char *filename, unsigned int langcode,
			char **langcode_str, char **langdesc_str)
{
	char buf[BUFSIZE];
	char tmpbuf[BUFSIZE];
	char *data;
	char *p;
	unsigned int i;

	while(fgets(buf, sizeof(buf), fp))
	{
		/* last line in file may not have a \n, look for any other
		 * that is excessively long
		 */
		if(strchr(buf, '\n') == NULL && strlen(buf) >= BUFSIZE-1)
		{
			if(langcode_str == NULL)
				mlog("Warning: Ignoring long line in translation %s: %s",
					filename, buf);

			/* and skip the entire line.. */
			while(fgets(tmpbuf, sizeof(tmpbuf), fp))
			{
				if(strchr(tmpbuf, '\n'))
					break;
			}

			continue;
		}

		/* comment */
		if(buf[0] == '#')
			continue;

		if((p = strchr(buf, '\n')) != NULL)
			*p = '\0';
		if((p = strchr(buf, '\r')) != NULL)
			*p = '\0';

		/* empty lines */
		if(buf[0] == '\0')
			continue;

		/* declarations of the code and the description */
		if(strncmp(buf, "set LANG_CODE", 13) == 0)
		{
			if(langcode_str)
			{
				if(*langcode_str)
				{
					my_free(*langcode_str);
					*langcode_str = NULL;
				}

				if((data = strchr(buf, '"')))
				{
					*data++ = '\0';

					if((p = strrchr(data, '"')))
						*p = '\0';

					*langcode_str = my_strdup(data);
				}
			}

			continue;
		}

		if(strncmp(buf, "set LANG_DESCRIPTION", 20) == 0)
		{
			if(langdesc_str)
			{
				if(*langdesc_str)
				{
					my_free(*langdesc_str);
					*langdesc_str = NULL;
				}

				if((data = strchr(buf, '"')))
				{
					*data++ = '\0';

					if((p = strrchr(data, '"')))
						*p = '\0';

					*langdesc_str = my_strdup(data);
				}
			}

			continue;
		}

		/* initial run through, only hunting for language codes */
		if(langcode_str)
			continue;

		/* lines containing only tabs/spaces */
		for(p = buf; *p; p++)
		{
			if(*p != ' ' && *p != '\t')
				break;
		}

		if(*p == '\0')
			continue;

		/* ',' delimits the notice name */
		if((data = strchr(buf, ',')) == NULL)
		{
			mlog("Warning: Ignoring bogus line in translation %s: %s",
				filename, buf);
			continue;
		}
		
		*data++ = '\0';

		/* hunt for the index of the translation */
		for(i = 0; lang_internal[i].id != SVC_LAST; i++)
		{
			if(strcmp(svc_notice_string[i], buf) == 0)
				break;
		}

		/* not found */
		if(lang_internal[i].id == SVC_LAST)
		{
			mlog("Warning: Invalid notice code in translation %s: %s",
				filename, buf);
			continue;
		}

		/* now hunt for the string itself, and continue if found */
		if((p = strchr(data, '"')))
		{
			*p++ = '\0';
			data = p;

			if((p = strrchr(data, '"')))
			{
				*p = '\0';

				if(lang_fmt_check(filename, svc_notice[0][i], data) > 0)
					svc_notice[langcode][i] = my_strdup(data);

				continue;
			}
		}

		mlog("Warning: Invalid translation string in translation %s: %s",
			filename, data);
	}

}

static void
lang_load_transfile(FILE *fp, const char *filename)
{
	char *langcode_str = NULL;
	char *langdesc_str = NULL;
	unsigned int langcode;

	lang_parse_transfile(fp, filename, -1, &langcode_str, &langdesc_str);

	if(langcode_str == NULL || langcode_str[0] == '\0')
	{
		mlog("Warning: LANG_CODE is not set in translation %s", filename);
		return;
	}

	if(langdesc_str == NULL || langdesc_str[0] == '\0')
	{
		mlog("Warning: LANG_DESCRIPTION is not set in translation %s", filename);
		return;
	}

	/* LANG_DEFAULT *MUST* *ALWAYS* come from messages.c.
	 * It must be fully working, and we cannot guarantee that in a
	 * translation file. --anfl
	 */
	if(strcmp(langcode_str, LANG_DEFAULT) == 0)
	{
		mlog("Warning: Attempted override of compiled translations from translation %s", filename);
		return;
	}

	rewind(fp);

	langcode = lang_get_langcode(langcode_str);

	/* language code conflict */
	if(svc_notice[langcode])
	{
		mlog("Warning: Attempted override of %s translations from translation %s", 
			langcode_str, filename);
		return;
	}

	my_free(langs_description[langcode]);
	langs_description[langcode] = my_strdup(langdesc_str);
	svc_notice[langcode] = my_calloc(1, sizeof(char *) * SVC_LAST);
	lang_parse_transfile(fp, filename, langcode, NULL, NULL);
}

void
lang_load_trans(void)
{
	char pathbuf[PATH_MAX];
	FILE *fp;
	DIR *langdir;
	struct dirent *fileent;
	struct stat fileinfo;

	if((langdir = opendir(LANGDIR)) == NULL)
	{
		mlog("Warning: Unable to open translation directory: %s", LANGDIR);
		return;
	}

	while((fileent = readdir(langdir)))
	{
		snprintf(pathbuf, sizeof(pathbuf), "%s/%s",
				LANGDIR, fileent->d_name);

		/* we want only regular files */
		if(stat(pathbuf, &fileinfo) >= 0 && S_ISREG(fileinfo.st_mode))
		{
			/* open the file pointer here just so its easier to 
			 * close if lang_load_transfile() aborts
			 */
			if((fp = fopen(pathbuf, "r")) == NULL)
			{
				mlog("Warning: Unable to open translation %s: %s", 
					pathbuf, strerror(errno));
				continue;
			}

			lang_load_transfile(fp, pathbuf);
			fclose(fp);
		}
	}

	(void) closedir(langdir);
}

void
lang_clear_trans(void)
{
	int i, j;

	for(i = 1; i < LANG_MAX; i++)
	{
		if(svc_notice[i] == NULL)
			continue;

		for(j = 0; j < SVC_LAST; j++)
		{
			if(svc_notice[i][j])
				my_free((void *) svc_notice[i][j]);
		}

		my_free(svc_notice[i]);
		svc_notice[i] = NULL;
	}
}
