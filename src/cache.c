/* src/cache.c
 *   Contains code for caching files
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
 * $Id: cache.c 23922 2007-05-07 14:23:03Z leeh $
 */
#include "stdinc.h"
#include "rserv.h"
#include "tools.h"
#include "balloc.h"
#include "cache.h"
#include "io.h"

static BlockHeap *cachefile_heap = NULL;
BlockHeap *cacheline_heap = NULL;

struct cacheline *emptyline = NULL;

/* init_cache()
 *
 * inputs	-
 * outputs	-
 * side effects - inits the file/line cache blockheaps, loads motds
 */
void
init_cache(void)
{
	cachefile_heap = BlockHeapCreate("Helpfile Cache", sizeof(struct cachefile), HEAP_CACHEFILE);
	cacheline_heap = BlockHeapCreate("Helplines Cache", sizeof(struct cacheline), HEAP_CACHELINE);

	/* allocate the emptyline */
	emptyline = BlockHeapAlloc(cacheline_heap);
	emptyline->data[0] = ' ';
}

/* cache_file()
 *
 * inputs	- file to cache, files "shortname", whether to add blank
 * 		  line at end
 * outputs	- pointer to file cached, else NULL
 * side effects -
 */
struct cachefile *
cache_file(const char *filename, const char *shortname, int add_blank)
{
	FILE *in;
	struct cachefile *cacheptr;
	struct cacheline *lineptr;
	char line[BUFSIZE];
	char *p;

	if((in = fopen(filename, "r")) == NULL)
		return NULL;

	cacheptr = BlockHeapAlloc(cachefile_heap);
	strlcpy(cacheptr->name, shortname, sizeof(cacheptr->name));

	/* cache the file... */
	while(fgets(line, sizeof(line), in) != NULL)
	{
		if((p = strchr(line, '\n')) != NULL)
			*p = '\0';

		if(!EmptyString(line))
		{
			lineptr = BlockHeapAlloc(cacheline_heap);

			strlcpy(lineptr->data, line, sizeof(lineptr->data));
			dlink_add_tail(lineptr, &lineptr->linenode, &cacheptr->contents);
		}
		else
			dlink_add_tail_alloc(emptyline, &cacheptr->contents);
	}

	if(add_blank)
		dlink_add_tail_alloc(emptyline, &cacheptr->contents);

	fclose(in);
	return cacheptr;
}

/* free_cachefile()
 *
 * inputs	- cachefile to free
 * outputs	-
 * side effects - cachefile and its data is free'd
 */
void
free_cachefile(struct cachefile *cacheptr)
{
	dlink_node *ptr;
	dlink_node *next_ptr;

	if(cacheptr == NULL)
		return;

	DLINK_FOREACH_SAFE(ptr, next_ptr, cacheptr->contents.head)
	{
		if(ptr->data != emptyline)
			BlockHeapFree(cacheline_heap, ptr->data);
		else
			free_dlink_node(ptr);
	}

	BlockHeapFree(cachefile_heap, cacheptr);
}

void
send_cachefile(struct cachefile *cacheptr, struct lconn *conn_p)
{
        struct cacheline *lineptr;
        dlink_node *ptr;

        if(cacheptr == NULL || conn_p == NULL)
                return;

        DLINK_FOREACH(ptr, cacheptr->contents.head)
        {
                lineptr = ptr->data;
                sendto_one(conn_p, "%s", lineptr->data);
        }
}
