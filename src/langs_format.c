/* src/langs_format.c
 *   Contains code for validating format strings in translations
 *
 * Copyright (C) 2007 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2007 ircd-ratbox development team
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
 * $Id: langs.c 23640 2007-02-18 12:36:38Z leeh $
 */
#include "stdinc.h"
#include "rserv.h"
#include "langs.h"
#include "log.h"

#define MAX_FMT_ARGS		64

#define LANG_FMT_STRING		0x001
#define LANG_FMT_CHAR		0x002
#define LANG_FMT_INTEGER	0x004
#define LANG_FMT_HEX		0x008

#define LANG_FMT_UNSIGNED	0x001
#define LANG_FMT_INTLONG	0x002
#define LANG_FMT_INTLONGLONG	0x004

struct lang_fmt
{
	unsigned int type;
	unsigned int flags;
};

static int
lang_fmt_parse(struct lang_fmt *fmt, const char *data)
{
	unsigned int pos = 0;
	int parsing = 0;

	for(; *data; data++)
	{
		if(parsing)
		{
			switch(*data)
			{
				case '%':
					parsing = 0;
					break;

				case 's':
					fmt[pos].type = LANG_FMT_STRING;
					pos++;
					parsing = 0;
					break;

				case 'l':
					if(fmt[pos].flags & LANG_FMT_INTLONG)
						fmt[pos].flags |= LANG_FMT_INTLONGLONG;
					else
						fmt[pos].flags |= LANG_FMT_INTLONG;
					break;

				case 'd':
				case 'i':
					fmt[pos].type = LANG_FMT_INTEGER;
					pos++;
					parsing = 0;
					break;

				case 'u':
					fmt[pos].type = LANG_FMT_INTEGER;
					fmt[pos].flags |= LANG_FMT_UNSIGNED;
					pos++;
					parsing = 0;
					break;

				case 'c':
					fmt[pos].type = LANG_FMT_CHAR;
					fmt[pos].flags |= LANG_FMT_UNSIGNED;
					pos++;
					parsing = 0;
					break;

				case 'x':
				case 'X':
					fmt[pos].type = LANG_FMT_HEX;
					fmt[pos].flags |= LANG_FMT_UNSIGNED;
					pos++;
					parsing = 0;
					break;

				case '.':
				case '+':
				case '-':
				case '#':
					if(fmt[pos].flags)
						return -1;
					break;

				case '1': case '2': case '3': case '4': case '5':
				case '6': case '7': case '8': case '9': case '0':
					if(fmt[pos].flags)
						return -1;
					break;

				default:
					return -2;
			}
		}
		else if(*data == '%')
			parsing = 1;

		if(pos+1 >= MAX_FMT_ARGS)
			return 0;
	}

	return 1;
}

int
lang_fmt_check(const char *filename, const char *original, const char *translation)
{
	struct lang_fmt original_fmt[MAX_FMT_ARGS];
	struct lang_fmt translation_fmt[MAX_FMT_ARGS];
	int error = 0;
	int i;

	memset(original_fmt, 0, sizeof(struct lang_fmt) * MAX_FMT_ARGS);
	memset(translation_fmt, 0, sizeof(struct lang_fmt) * MAX_FMT_ARGS);

	if(lang_fmt_parse(original_fmt, original) <= 0)
	{
		mlog("Warning: Error parsing format string: %s", original);
		return 0;
	}

	/* if the original is fine but the translation isn't, then they have
	 * different amounts of format strings
	 */
	if(lang_fmt_parse(translation_fmt, translation) <= 0)
	{
		mlog("Warning: Error parsing format string in %s: %s", 
			filename, translation);
		return 0;
	}

	for(i = 0; i < MAX_FMT_ARGS; i++)
	{
		if(original_fmt[i].type != translation_fmt[i].type ||
		   original_fmt[i].flags != translation_fmt[i].flags)
		{
			mlog("Warning: translation format strings differ in %s: %s",
				filename, translation);
			error++;
		}
	}

	if(error)
		return 0;

	return 1;
}
