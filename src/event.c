/* src/event.c
 *   Contains code for calling/managing events.
 *
 * Copyright (C) 2003-2005 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2005 ircd-ratbox development team
 *
 * $Id: event.c 27003 2010-03-21 13:08:59Z leeh $
 */
/* Taken from ircd-ratbox.  Original header:
 *
 *  Copyright (C) 1998-2000 Regents of the University of California
 *  Copyright (C) 2001-2002 Hybrid Development Team
 *  Copyright (C) 2002 ircd-ratbox development team
 *
 *  Code borrowed from the squid web cache by Adrian Chadd.
 *
 *  DEBUG: section 41   Event Processing
 *  AUTHOR: Henrik Nordstrom
 *
 *  SQUID Internet Object Cache  http://squid.nlanr.net/Squid/
 *  ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from the
 *  Internet community.  Development is led by Duane Wessels of the
 *  National Laboratory for Applied Network Research and funded by the
 *  National Science Foundation.  Squid is Copyrighted (C) 1998 by
 *  the Regents of the University of California.  Please see the
 *  COPYRIGHT file for full details.  Squid incorporates software
 *  developed and/or copyrighted by other sources.  Please see the
 *  CREDITS file for full details.
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
 */
#include "stdinc.h"
#include "rserv.h"
#include "event.h"
#include "io.h"

struct ev_entry event_table[MAX_EVENTS];
static time_t event_time_min = -1;

static int
event_get_delta(time_t duration)
{
	int delta;
	int i;

	if(duration <= 10)
		return 0;

	if(duration <= 60)
		delta = 3;
	else if(duration <= 300)
		delta = 6;
	else
		delta = 10;

	i = 1 + (int) ((delta * 2) * (rand() / (RAND_MAX + 1.0)));
	i -= delta;

	return i;
}

/*
 * void eventAdd(const char *name, EVH *func, void *arg, time_t when)
 *
 * Input: Name of event, function to call, arguments to pass, and frequency
 *	  of the event.
 * Output: None
 * Side Effects: Adds the event to the event list.
 */
void
eventAdd(const char *name, EVH * func, void *arg, time_t when)
{
	int i;

	/* find first inactive index */
	for (i = 0; i < MAX_EVENTS; i++)
	{
		if(event_table[i].active == 0)
		{
			int delta = event_get_delta(when);

			event_table[i].func = func;
			event_table[i].name = name;
			event_table[i].arg = arg;
			event_table[i].active = 1;

			if(when)
			{
				event_table[i].when = CURRENT_TIME + when + delta;
				event_table[i].frequency = when + delta;

				if((event_table[i].when < event_time_min) || (event_time_min == -1))
					event_time_min = event_table[i].when;
			}
			/* when == 0 means "disabled", set frequency to -1
			 * because events with a frequency of 0 are
			 * "oneshot" --anfl
			 */
			else
			{
				event_table[i].when = 0;
				event_table[i].frequency = -1;
			}

			return;
		}
	}
}

void
eventAddOnce(const char *name, EVH * func, void *arg, time_t when)
{
	int i;

	/* find first inactive index */
	for (i = 0; i < MAX_EVENTS; i++)
	{
		if(event_table[i].active == 0)
		{
			event_table[i].func = func;
			event_table[i].name = name;
			event_table[i].arg = arg;
			event_table[i].when = CURRENT_TIME + when;
			event_table[i].frequency = 0;
			event_table[i].active = 1;

			if((event_table[i].when < event_time_min) || (event_time_min == -1))
				event_time_min = event_table[i].when;

			return;
		}
	}
}

/*
 * void eventDelete(EVH *func, void *arg)
 *
 * Input: Function handler, argument that was passed.
 * Output: None
 * Side Effects: Removes the event from the event list
 */
void
eventDelete(EVH * func, void *arg)
{
	int i;

	i = eventFind(func, arg);

	if(i == -1)
		return;

	event_table[i].name = NULL;
	event_table[i].func = NULL;
	event_table[i].arg = NULL;
	event_table[i].active = 0;
}

/*
 * void eventRun(void)
 *
 * Input: None
 * Output: None
 * Side Effects: Runs pending events in the event list
 */
void
eventRun(void)
{
	int i;

	for (i = 0; i < MAX_EVENTS; i++)
	{
		/* skip inactive events, and ones "disabled" (freq == -1) */
		if(event_table[i].active && event_table[i].frequency >= 0 &&
			(event_table[i].when <= CURRENT_TIME))
		{
			event_table[i].func(event_table[i].arg);

			/* if the event is only scheduled to run once, remove it from
			 * the table.
			 */
			if(event_table[i].frequency)
			{
				event_table[i].when = CURRENT_TIME + event_table[i].frequency;
			}
			else
			{
				event_table[i].name = NULL;
				event_table[i].func = NULL;
				event_table[i].arg = NULL;
				event_table[i].active = 0;
			}

			event_time_min = -1;
		}
	}
}


/*
 * time_t eventNextTime(void)
 * 
 * Input: None
 * Output: Specifies the next time eventRun() should be run
 * Side Effects: None
 */
time_t
eventNextTime(void)
{
	int i;

	if(event_time_min == -1)
	{
		for (i = 0; i < MAX_EVENTS; i++)
		{
			/* skip inactive events, and ones "disabled" (freq == -1) */
			if(event_table[i].active && event_table[i].frequency >= 0 &&
			   ((event_table[i].when < event_time_min) || (event_time_min == -1)))
				event_time_min = event_table[i].when;
		}
	}

	return event_time_min;
}

void
eventUpdate(const char *name, time_t freq)
{
	int i;

	for(i = 0; i < MAX_EVENTS; i++)
	{
		if(event_table[i].active && 
		   !irccmp(event_table[i].name, name))
		{
			if(freq > 0)
			{
				event_table[i].frequency = freq;

				/* update when its scheduled to run if its higher
				 * than the new frequency
				 */
				if((CURRENT_TIME + freq) < event_table[i].when)
					event_table[i].when = CURRENT_TIME + freq;
			}
			else
				event_table[i].frequency = -1;

			return;
		}
	}
}

/*
 * void eventInit(void)
 *
 * Input: None
 * Output: None
 * Side Effects: Initializes the event system. 
 */
void
init_events(void)
{
	memset((void *) event_table, 0, sizeof(event_table));
}

/*
 * int eventFind(EVH *func, void *arg)
 *
 * Input: Event function and the argument passed to it
 * Output: Index to the slow in the event_table
 * Side Effects: None
 */
int
eventFind(EVH * func, void *arg)
{
	int i;

	for (i = 0; i < MAX_EVENTS; i++)
	{
		if((event_table[i].func == func) &&
		   (event_table[i].arg == arg) && event_table[i].active)
			return i;
	}

	return -1;
}

void
event_show(struct lconn *conn_p)
{
	time_t duration;
        int i;

        sendto_one(conn_p, "Events: Function                    Next");
        
        for(i = 0; i < MAX_EVENTS; i++)
        {
                if(!event_table[i].active)
                        continue;

		duration = event_table[i].when - CURRENT_TIME;

		if(event_table[i].frequency == -1)
			duration = -1;

                sendto_one(conn_p, "        %-27s %-4ld seconds",
                           event_table[i].name, duration);
        }
}
