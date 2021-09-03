/* src/tools.c
 *   Contains various useful functions.
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
 * $Id: tools.c 23896 2007-05-01 17:56:28Z leeh $
 */
#include "stdinc.h"
#include "rserv.h"
#include "rsdb.h"
#include "balloc.h"

static BlockHeap *dlinknode_heap;

void
init_tools(void)
{
	dlinknode_heap = BlockHeapCreate("DLINK Node", sizeof(dlink_node), HEAP_DLINKNODE);
}

void
my_sleep(unsigned int seconds, unsigned int microseconds)
{
#ifdef HAVE_NANOSLEEP
	struct timespec tv;
	tv.tv_nsec = (microseconds * 1000);
	tv.tv_sec = seconds;
	nanosleep(&tv, NULL);
#else 
	struct timeval tv;
	tv.tv_sec = seconds;
	tv.tv_usec = microseconds;
	select(0, NULL, NULL, NULL, &tv);
#endif
}

/* my_calloc()
 *   wrapper for calloc() to detect out of memory
 */
void *
my_calloc(size_t nmemb, size_t size)
{
    void *p;

    p = calloc(nmemb, size);

    if(p == NULL)
	    die(0, "out of memory");

    return p;
}

/* my_free()
 *   wrapper for free() that checks what we're freeing exists
 */
void
my_free(void *p)
{
    if(p != NULL)
	    free(p);
}

/* my_strdup()
 *   wrapper for strdup() to detect out of memory
 */
char *
my_strdup(const char *s)
{
    char *n;

    n = strdup(s);

    if(n == NULL)
	    die(0, "out of memory");

    return n;
}

/* my_strndup()
 *   wrapper for a size limited strdup() which is non portable
 */
char *
my_strndup(const char *s, size_t len)
{
	char *n;

	n = my_malloc(len);

	if(n == NULL)
		die(0, "out of memory");

	strlcpy(n, s, len);

	return n;
}

/* get_duration()
 *   converts duration in seconds to a string form
 *
 * inputs       - duration in seconds
 * outputs      - string form of duration, "n day(s), h:mm:ss"
 */
const char *
get_duration(time_t seconds)
{
        static char buf[BUFSIZE];
        int days, hours, minutes;

        days = (int) (seconds / 86400);
        seconds %= 86400;
        hours = (int) (seconds / 3600);
        seconds %= 3600;
        minutes = (int) (seconds / 60);
        seconds %= 60;

        snprintf(buf, sizeof(buf), "%d day%s, %d:%02d:%02lu",
                 days, (days == 1) ? "" : "s", hours,
                 minutes, (unsigned long) seconds);

        return buf;
}

const char *
get_short_duration(time_t seconds)
{
	static char buf[BUFSIZE];
        int days, hours, minutes;

        days = (int) (seconds / 86400);
        seconds %= 86400;
        hours = (int) (seconds / 3600);
	seconds %= 3600;
	minutes = (int) (seconds / 60);

        snprintf(buf, sizeof(buf), "%dd%dh%dm", days, hours, minutes);

        return buf;
}

const char *
get_time(time_t when, int tz)
{
	static char timebuffer[BUFSIZE];
	struct tm *tmptr;

	if(!when)
		when = CURRENT_TIME;

	tmptr = localtime(&when);

	if(tz)
		strftime(timebuffer, MAX_DATE_STRING, "%d/%m/%Y %H:%M %Z", tmptr);
	else
		strftime(timebuffer, MAX_DATE_STRING, "%d/%m/%Y %H:%M", tmptr);

	return timebuffer;
}

time_t
get_temp_time(const char *duration)
{
	time_t result = 0;

	for(; *duration; duration++)
	{
		if(IsDigit(*duration))
		{
			result *= 10;
			result += ((*duration) & 0xF);
		}
		else
		{
			if (!result || *(duration+1))
				return 0;
			switch (*duration)
			{
				case 'h': case 'H':
					result *= 60; 
					break;
				case 'd': case 'D': 
					result *= 1440; 
					break;
				case 'w': case 'W':
					result *= 10080; 
					break;
				default:
					return 0;
			}
		}
	}

	/* max at 1 year */
	/* time_t is signed, so if we've overflowed, reset to max */
	if(result > (60*24*7*52) || result < 0)
		result = (60*24*7*52);

	return(result*60);
}

const char *
lcase(const char *text)
{
        static char buf[BUFSIZE+1];
        int i = 0;

        buf[0] = '\0';

        while(text[i] != '\0' && i < BUFSIZE-1)
        {
                buf[i] = tolower(text[i]);
                i++;
        }

        buf[i] = '\0';

        return buf;
}

const char *
ucase(const char *text)
{
        static char buf[BUFSIZE+1];
        int i = 0;

        buf[0] = '\0';

        while(text[i] != '\0' && i < BUFSIZE-1)
        {
                buf[i] = toupper(text[i]);
                i++;
        }

        buf[i] = '\0';

        return buf;
}

/*
 * strip_tabs(dst, src, length)
 *
 *   Copies src to dst, while converting all \t (tabs) into spaces.
 *
 * NOTE: jdc: I have a gut feeling there's a faster way to do this.
 */
char *
strip_tabs(char *dest, const unsigned char *src, size_t len)
{
	char *d = dest;

	if(dest == NULL || src == NULL)
		return NULL;

	while (*src && (len > 0))
	{
		if(*src == '\t')
		{
			*d++ = ' ';	/* Translate the tab into a space */
		}
		else
		{
			*d++ = *src;	/* Copy src to dst */
		}
		++src;
		--len;
	}
	*d = '\0';		/* Null terminate, thanks and goodbye */
	return dest;
}

__inline int
string_to_array(char *string, char *parv[])
{
        char *p, *buf = string;
        int x = 0;

        parv[x] = NULL;

        if(EmptyString(string))
                return x;

        while(*buf == ' ')
		buf++;

        if(*buf == '\0')
                return x;

        do
        {
                parv[x++] = buf;
                parv[x] = NULL;

                if((p = strchr(buf, ' ')) != NULL)
                {
                        *p++ = '\0';
                        buf = p;
                }
                else
                        return x;

                while(*buf == ' ')
			buf++;
                if(*buf == '\0')
                        return x;
        }
        while(x < MAXPARA - 1);

        parv[x++] = p;
        parv[x] = NULL;
        return x;
}

__inline int
string_to_array_delim(char *string, char *parv[], char delim, int maxpara)
{
	static char empty_string[] = "";
        char *p, *buf = string;
        int x = 0;

        parv[x] = NULL;

        if(EmptyString(string))
                return x;

        if(*buf == '\0')
                return x;

	while((x < maxpara - 1) && (p = strchr(buf, delim)))
	{
		/* empty field */
		if(p == buf)
		{
			parv[x++] = empty_string;
			buf++;
			continue;
		}

		*p++ = '\0';
		parv[x++] = buf;
		buf = p;
	}

	parv[x++] = buf;
	parv[x] = NULL;

	return x;
}

/*
 * strlcat and strlcpy were ripped from openssh 2.5.1p2
 * They had the following Copyright info: 
 *
 *
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright    
 *    notice, this list of conditions and the following disclaimer in the  
 *    documentation and/or other materials provided with the distribution. 
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED `AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef HAVE_STRLCAT
size_t
strlcat(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz, dlen;

	while (n-- != 0 && *d != '\0')
		d++;
	dlen = d - dst;
	n = siz - dlen;

	if(n == 0)
		return (dlen + strlen(s));
	while (*s != '\0')
	{
		if(n != 1)
		{
			*d++ = *s;
			n--;
		}
		s++;
	}
	*d = '\0';
	return (dlen + (s - src));	/* count does not include NUL */
}
#endif

#ifndef HAVE_STRLCPY
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;
	/* Copy as many bytes as will fit */
	if(n != 0 && --n != 0)
	{
		do
		{
			if((*d++ = *s++) == 0)
				break;
		}
		while (--n != 0);
	}
	/* Not enough room in dst, add NUL and traverse rest of src */
	if(n == 0)
	{
		if(siz != 0)
			*d = '\0';	/* NUL-terminate dst */
		while (*s++)
			;
	}

	return (s - src - 1);	/* count does not include NUL */
}
#endif

/* dlink stuff is stolen from ircd-ratbox, original header:
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 * dlink_ routines are stolen from squid, except for dlinkAddBefore,
 * which is mine.
 *   -- adrian
 */
dlink_node *
make_dlink_node(void)
{
	dlink_node *lp = BlockHeapAlloc(dlinknode_heap);
	lp->next = lp->prev = NULL;
	return lp;
}

void
free_dlink_node(dlink_node *lp)
{
	BlockHeapFree(dlinknode_heap, lp);
}

void
dlink_add(void *data, dlink_node * m, dlink_list * list)
{
	m->data = data;
	m->next = list->head;
	m->prev = NULL;

	/* Assumption: If list->tail != NULL, list->head != NULL */
	if(list->head != NULL)
		list->head->prev = m;
	else if(list->tail == NULL)
		list->tail = m;

	list->head = m;
	list->length++;
}

void
dlink_add_tail(void *data, dlink_node * m, dlink_list * list)
{
	m->data = data;
	m->next = NULL;
	m->prev = list->tail;

	/* Assumption: If list->tail != NULL, list->head != NULL */
	if(list->tail != NULL)
		list->tail->next = m;
	else if(list->head == NULL)
		list->head = m;

	list->tail = m;
	list->length++;
}

void
dlink_add_before(void *data, dlink_node *m, dlink_node *pos, dlink_list *list)
{
	dlink_node *prev;

	if(pos)
	{
		prev = pos->prev;

		if(prev)
		{
			m->data = data;

			prev->next = m;
			m->prev = prev;

			pos->prev = m;
			m->next = pos;

			list->length++;
		}
		/* adding to front of list, shortcut this */
		else
			dlink_add(data, m, list);
	}
	/* adding to tail of list, shortcut this too */
	else
		dlink_add_tail(data, m, list);
}

void
dlink_delete(dlink_node * m, dlink_list * list)
{
	/* Assumption: If m->next == NULL, then list->tail == m
	 *      and:   If m->prev == NULL, then list->head == m
	 */
	if(m->next)
		m->next->prev = m->prev;
	else
		list->tail = m->prev;

	if(m->prev)
		m->prev->next = m->next;
	else
		list->head = m->next;

	/* Set this to NULL does matter */
	m->next = m->prev = NULL;
	list->length--;
}

dlink_node *
dlink_find(void *data, dlink_list *list)
{
	dlink_node *ptr;

	DLINK_FOREACH(ptr, list->head)
	{
		if(ptr->data == data)
			return (ptr);
	}
	return (NULL);
}

void
dlink_move_list(dlink_list * from, dlink_list * to)
{
	/* There are three cases */
	/* case one, nothing in from list */
	if(from->head == NULL)
		return;

	/* case two, nothing in to list */
	if(to->head == NULL)
	{
		to->head = from->head;
		to->tail = from->tail;
		from->head = from->tail = NULL;
		to->length = from->length;
		from->length = 0;
		return;
	}

	/* third case play with the links */
	from->tail->next = to->head;
	to->head->prev = from->tail;
	to->head = from->head;
	from->head = from->tail = NULL;
	to->length += from->length;
	from->length = 0;
}

void
dlink_move_list_tail(dlink_list * from, dlink_list * to)
{
	/* There are three cases */
	/* case one, nothing in from list */
	if(from->head == NULL)
		return;

	/* case two, nothing in to list */
	if(to->head == NULL)
	{
		to->head = from->head;
		to->tail = from->tail;
		from->head = from->tail = NULL;
		to->length = from->length;
		from->length = 0;
		return;
	}

	/* third case play with the links */
	from->head->prev = to->tail;
	to->tail->next = from->head;
	to->tail = from->tail;
	to->length += from->length;
	from->head = from->tail = NULL;
	from->length = 0;
}

dlink_node *
dlink_find_delete(void *data, dlink_list *list)
{
	dlink_node *m;

	DLINK_FOREACH(m, list->head)
	{
		if(m->data != data)
			continue;

		if(m->next)
			m->next->prev = m->prev;
		else
			list->tail = m->prev;

		if(m->prev)
			m->prev->next = m->next;
		else
			list->head = m->next;

		m->next = m->prev = NULL;
		list->length--;
		return m;
	}
	return NULL;
}

int
dlink_find_destroy(void *data, dlink_list *list)
{
	void *ptr = dlink_find_delete(data, list);
	if(ptr != NULL)
	{
		free_dlink_node(ptr);
		return 1;
	}
	return 0;
}

dlink_node *
dlink_find_string(const char *data, dlink_list *list)
{
	dlink_node *ptr;

	DLINK_FOREACH(ptr, list->head)
	{
		if(!irccmp((const char *) ptr->data, data))
			return ptr;
	}

	return NULL;
}

void
dlink_move_node(dlink_node * m, dlink_list * oldlist, dlink_list * newlist)
{
	/* Assumption: If m->next == NULL, then list->tail == m
	 *      and:   If m->prev == NULL, then list->head == m
	 */
	if(m->next)
		m->next->prev = m->prev;
	else
		oldlist->tail = m->prev;

	if(m->prev)
		m->prev->next = m->next;
	else
		oldlist->head = m->next;

	m->prev = NULL;
	m->next = newlist->head;
	if(newlist->head != NULL)
		newlist->head->prev = m;
	else if(newlist->tail == NULL)
		newlist->tail = m;
	newlist->head = m;

	oldlist->length--;
	newlist->length++;
}
